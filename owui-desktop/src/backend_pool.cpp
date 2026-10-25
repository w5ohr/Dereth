// Multi-server pool in the app (#75): health checks on the member backends,
// the virtual "pool" backend whose transport picks a member per request and
// fails over, the union model list, and the Settings card.
#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>

#include "app.h"
#include "core/async.h"
#include "core/common.h"
#include "core/http.h"
#include "core/pool.h"
#include "core/settings.h"
#include "pane_common.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

std::mutex gMutex;
std::map<std::string, pool::Health> gHealth;
std::atomic<int> gCounter{0};
double gLastCheck = 0.0;
bool gChecking = false;
std::string gLastRoute;

pool::Prefs prefs() { return pool::fromJson(core::Settings::instance().extra); }

std::vector<pool::Health> snapshot() {
  std::lock_guard<std::mutex> lock(gMutex);
  std::vector<pool::Health> out;
  for (const auto& [id, h] : gHealth) out.push_back(h);
  return out;
}

void bump(const std::string& id, int delta) {
  std::lock_guard<std::mutex> lock(gMutex);
  pool::Health& h = gHealth[id];
  if (h.id.empty()) h.id = id;
  h.inFlight = std::max(0, h.inFlight + delta);
}

void markDown(const std::string& id, const std::string& error) {
  std::lock_guard<std::mutex> lock(gMutex);
  pool::Health& h = gHealth[id];
  h.id = id;
  h.alive = false;
  h.error = error;
  h.checkedAt = core::nowSeconds();
}

}  // namespace

pool::Prefs App::poolPrefs() const { return prefs(); }
std::vector<pool::Health> App::poolHealth() const { return snapshot(); }
std::string App::poolLastRoute() const { return gLastRoute; }

void App::poolCheckNow() {
  const pool::Prefs p = prefs();
  if (gChecking || p.members.empty()) return;
  gChecking = true;
  gLastCheck = core::nowMillis();
  std::vector<backends::Backend> members;
  std::vector<std::string> keys;
  for (const std::string& id : p.members) {
    backends::Backend b;
    if (findBackend(id, &b)) {
      members.push_back(b);
      keys.push_back(backendKey(id));
    }
  }
  async::run([members, keys] {
    for (size_t i = 0; i < members.size(); ++i) {
      const backends::Backend& b = members[i];
      net::Request req;
      req.timeoutSec = 8;
      req.connectTimeoutSec = 4;
      if (b.kind == backends::Kind::Ollama) req.url = b.url + "/api/ps";
      else if (b.kind == backends::Kind::OpenAI) req.url = b.url + "/models";
      else req.url = b.url + "/v1/models";
      if (!keys[i].empty()) req.headers.emplace_back(b.kind == backends::Kind::Anthropic ? "x-api-key" : "Authorization", b.kind == backends::Kind::Anthropic ? keys[i] : "Bearer " + keys[i]);
      if (b.kind == backends::Kind::Anthropic) req.headers.emplace_back("anthropic-version", "2023-06-01");
      const double started = core::nowMillis();
      const net::Response res = net::perform(req);
      pool::Health h;
      h.id = b.id;
      h.latencyMs = core::nowMillis() - started;
      h.checkedAt = core::nowSeconds();
      h.alive = res.error.empty() && res.status >= 200 && res.status < 500;
      h.error = res.error.empty() ? (h.alive ? "" : "HTTP " + std::to_string(res.status)) : res.error;
      if (h.alive && b.kind == backends::Kind::Ollama) h.running = pool::runningFrom(Json::parse(res.body, nullptr, false));
      std::lock_guard<std::mutex> lock(gMutex);
      const int inFlight = gHealth.count(b.id) ? gHealth[b.id].inFlight : 0;
      h.inFlight = inFlight;
      gHealth[b.id] = h;
    }
    async::post([] { gChecking = false; });
  });
}

void App::tickBackendPool() {
  const pool::Prefs p = prefs();
  if (!p.enabled || p.members.empty()) return;
  if (core::nowMillis() - gLastCheck < p.checkEverySec * 1000.0) return;
  poolCheckNow();
}

agents::Transport App::poolTransport() {
  return [this](const Json& payload, const std::function<bool(const Json&)>& onLine, const std::atomic<bool>* cancel) {
    const pool::Prefs p = prefs();
    const std::vector<std::string> tryOrder = pool::order(p, snapshot(), gCounter++);
    api::Result last;
    last.ok = false;
    last.error = tryOrder.empty() ? "no backend in the pool is reachable" : "every backend in the pool failed";
    for (const std::string& id : tryOrder) {
      backends::Backend b;
      if (!findBackend(id, &b)) continue;
      // Only a real chat chunk counts as output. streamChat also forwards
      // whatever JSON an HTTP failure returned and then an {"error": ...}
      // line; those are held back so a failed member can be retried on the
      // next one without the chat seeing them, and replayed when it can't.
      bool emitted = false;
      std::vector<Json> held;
      auto wrapped = [&](const Json& line) {
        const bool output = line.is_object() && (line.contains("message") || line.contains("choices") || line.contains("delta") || line.contains("content_block") ||
                                                  line.contains("response") || (line.contains("done") && !line.contains("error")));
        if (!output) {
          held.push_back(line);
          return true;
        }
        emitted = true;
        return onLine(line);
      };
      bump(id, +1);
      api::Result r = backendTransport(b)(payload, wrapped, cancel);
      bump(id, -1);
      if (r.ok || emitted) {
        for (const Json& h : held) onLine(h);
        async::post([name = b.name, ok = r.ok] { gLastRoute = name + (ok ? "" : " (failed mid-stream)"); });
        return r;
      }
      core::debugLog("pool: %s failed before any output (%s)%s", id.c_str(), r.error.c_str(), p.failover ? " - trying the next member" : "");
      markDown(id, r.error);
      last = r;
      last.error = b.name + ": " + r.error;
      if (!p.failover || &id == &tryOrder.back()) onLine(Json{{"error", last.error}});
      if (!p.failover) break;
    }
    async::post([] { gLastRoute = "(no member answered)"; });
    return last;
  };
}

std::vector<std::string> App::poolModelUnion() {
  const pool::Prefs p = prefs();
  std::vector<std::string> out;
  for (const std::string& id : p.members) {
    backends::Backend b;
    if (!findBackend(id, &b)) continue;
    std::string error;
    for (const std::string& m : backends::listModels(b, backendKey(id), &error)) {
      if (std::find(out.begin(), out.end(), m) == out.end()) out.push_back(m);
    }
  }
  return out;
}

void App::drawBackendPoolSection(float width) {
  const auto& palette = theme::palette();
  pool::Prefs p = prefs();
  bool changed = false;
  ImGui::PushTextWrapPos(width - 20.0f);
  ImGui::TextUnformatted("Several backends behind one \"Pool\" entry: each request goes to the least-loaded member that answered its last health check, and fails over to the next when one errors before replying. Private chats pick the pool from their backend button.");
  ImGui::PopTextWrapPos();
  changed |= ImGui::Checkbox("Enable the pool", &p.enabled);
  widgets::dimText("Members:");
  for (const backends::Backend& b : backendList()) {
    if (b.id == pool::kPoolId) continue;
    ImGui::SameLine();
    bool on = std::find(p.members.begin(), p.members.end(), b.id) != p.members.end();
    ImGui::PushID(b.id.c_str());
    if (ImGui::Checkbox(b.name.c_str(), &on)) {
      if (on) p.members.push_back(b.id);
      else p.members.erase(std::remove(p.members.begin(), p.members.end(), b.id), p.members.end());
      changed = true;
    }
    ImGui::PopID();
  }
  static const char* kStrategies[] = {"least-loaded", "round-robin", "first-alive"};
  int strategy = p.strategy == "round-robin" ? 1 : p.strategy == "first-alive" ? 2 : 0;
  ImGui::SetNextItemWidth(160);
  if (ImGui::Combo("Routing", &strategy, kStrategies, 3)) {
    p.strategy = kStrategies[strategy];
    changed = true;
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(140);
  changed |= ImGui::SliderInt("Check every (s)", &p.checkEverySec, 5, 600);
  ImGui::SameLine();
  changed |= ImGui::Checkbox("Fail over", &p.failover);
  if (changed) {
    settings().extra["backendPool"] = pool::toJson(p);
    settings().save();
    gLastCheck = 0.0;   // re-check soon
  }
  widgets::beginDisabled(gChecking || p.members.empty());
  if (ImGui::SmallButton(gChecking ? "Checking..." : "Check now")) poolCheckNow();
  widgets::endDisabled(gChecking || p.members.empty());
  if (!gLastRoute.empty()) {
    ImGui::SameLine();
    widgets::dimText("last request went to %s", gLastRoute.c_str());
  }
  const std::vector<pool::Health> health = snapshot();
  if (!p.members.empty() && ImGui::BeginTable("##pool", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp, ImVec2(width - 20.0f, 0))) {
    ImGui::TableSetupColumn("Member", ImGuiTableColumnFlags_WidthFixed, 160.0f);
    ImGui::TableSetupColumn("URL", ImGuiTableColumnFlags_WidthFixed, 260.0f);
    ImGui::TableSetupColumn("Health");
    ImGui::TableHeadersRow();
    for (const std::string& id : p.members) {
      backends::Backend b;
      if (!findBackend(id, &b)) continue;
      const pool::Health* h = nullptr;
      for (const pool::Health& x : health) {
        if (x.id == id) h = &x;
      }
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted(b.name.c_str());
      ImGui::TableSetColumnIndex(1);
      widgets::dimText("%s", b.url.c_str());
      ImGui::TableSetColumnIndex(2);
      const std::string text = h ? pool::describe(*h) : "not checked yet";
      ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(h && h->checkedAt ? (h->alive ? palette.success : palette.danger) : palette.textDim));
      ImGui::TextUnformatted(text.c_str());
      ImGui::PopStyleColor();
    }
    ImGui::EndTable();
  }
}

}  // namespace app
