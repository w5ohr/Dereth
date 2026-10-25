// Health (Tools > Health): CPU, GPU, memory, swap, Metal, thermal state,
// power and disk from core/sysmetrics sampled once a second, plus the models
// Ollama has in memory (polled every 5 s while the tab shows) with a
// residency timeline and an Unload button per model.
#include <ctime>
#include <deque>
#include <map>

#include "app.h"
#include "core/async.h"
#include "core/common.h"
#include "core/http.h"
#include "core/sysmetrics.h"
#include "pane_common.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

sysmetrics::Ring gRing;
double gLastSample = 0.0;
bool gPaused = false;
bool gVisible = false;   // set by the draw; the tick polls Ollama only then

struct Resident {
  std::string name;
  double size = 0.0, vram = 0.0;
  int context = 0;
  double expiresIn = -1.0;   // seconds, -1 unknown
};
std::vector<Resident> gModels;
double gLastPs = 0.0;
bool gPsBusy = false;
std::string gPsError;
// Residency timeline: one entry per poll with the model names in memory.
std::deque<std::pair<double, std::vector<std::string>>> gResidency;
constexpr size_t kResidencyCap = 180;   // 15 minutes at 5 s

double parseIso(const std::string& iso) {
  std::tm tm{};
  if (!strptime(iso.c_str(), "%Y-%m-%dT%H:%M:%S", &tm)) return 0.0;
  tm.tm_isdst = -1;
  return static_cast<double>(mktime(&tm));   // the server's offset is this Mac's: a local Ollama
}

std::string gb(double bytes) {
  char buf[32];
  if (bytes >= 1e9) snprintf(buf, sizeof buf, "%.1f GB", bytes / 1e9);
  else snprintf(buf, sizeof buf, "%.0f MB", bytes / 1e6);
  return buf;
}

std::string minutes(double seconds) {
  if (seconds < 0) return "?";
  if (seconds < 60) return std::to_string(static_cast<int>(seconds)) + " s";
  if (seconds < 3600) return std::to_string(static_cast<int>(seconds / 60)) + " min";
  char buf[32];
  snprintf(buf, sizeof buf, "%.1f h", seconds / 3600.0);
  return buf;
}

void tile(const char* title, const std::string& value, const std::string& sub, const std::vector<float>& series, float maxValue,
          float width, ImU32 color) {
  ImGui::BeginChild(title, ImVec2(width, 122.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
  widgets::dimText("%s", title);
  theme::pushBold();
  ImGui::TextUnformatted(value.c_str());
  theme::pop();
  if (!sub.empty()) widgets::dimText("%s", sub.c_str());
  ImGui::PushStyleColor(ImGuiCol_PlotLines, color);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, 0);
  if (!series.empty()) {
    ImGui::PlotLines("##plot", series.data(), static_cast<int>(series.size()), 0, nullptr, 0.0f, maxValue, ImVec2(-1, 34.0f));
  } else {
    ImGui::Dummy(ImVec2(0, 34.0f));
  }
  ImGui::PopStyleColor(2);
  ImGui::EndChild();
}

}  // namespace

void App::tickHealth() {
  if (!signedIn_ || gPaused) return;
  const double now = core::nowMillis();
  if (now - gLastSample >= 1000.0) {
    gLastSample = now;
    gRing.push(sysmetrics::sample());
  }
  if (!gVisible || gPsBusy || now - gLastPs < 5000.0) return;
  gLastPs = now;
  gPsBusy = true;
  const std::string direct = settings().ollamaUrl;
  async::run([this, direct] {
    api::Result res = api_.ollamaPs();
    Json data = res.ok ? res.data : Json();
    std::string error = res.ok ? "" : api::errorMessage(res);
    if (!res.ok && !direct.empty()) {   // the server's passthrough is away: ask Ollama itself
      net::Request req;
      req.url = direct + "/api/ps";
      req.timeoutSec = 5;
      net::Response r = net::perform(req);
      if (r.ok()) {
        data = Json::parse(r.body, nullptr, false);
        error.clear();
      }
    }
    std::vector<Resident> models;
    if (data.is_object() && data.contains("models") && data["models"].is_array()) {
      const double now = core::nowSeconds();
      for (const auto& m : data["models"]) {
        if (!m.is_object()) continue;
        Resident r;
        r.name = m.value("name", m.value("model", ""));
        r.size = m.value("size", 0.0);
        r.vram = m.value("size_vram", 0.0);
        r.context = m.value("context_length", 0);
        const std::string expires = m.value("expires_at", "");
        if (!expires.empty()) {
          const double at = parseIso(expires);
          if (at > 0) r.expiresIn = at - now;
        }
        models.push_back(r);
      }
    }
    async::post([models, error] {
      gPsBusy = false;
      gPsError = error;
      if (error.empty()) {
        gModels = models;
        std::vector<std::string> names;
        for (const Resident& r : models) names.push_back(r.name);
        gResidency.push_back({core::nowSeconds(), names});
        while (gResidency.size() > kResidencyCap) gResidency.pop_front();
      }
    });
  });
}

void App::drawHealthSection(float width) {
  const auto& palette = theme::palette();
  gVisible = true;   // cleared implicitly when the tab is not drawn: see below
  static int framesSinceDraw = 0;
  framesSinceDraw = 0;
  (void)framesSinceDraw;

  const sysmetrics::Sample* s = gRing.latest();
  if (!s) {
    widgets::loadingRow("Sampling");
    return;
  }
  const float gap = 10.0f;
  const float tileW = (width - gap * 3) / 4.0f;
  char buf[64];

  snprintf(buf, sizeof buf, "%.0f%%", std::max(0.0, s->cpuPercent));
  tile("CPU", s->cpuPercent < 0 ? "..." : buf, "all cores, last 5 minutes", gRing.series(&sysmetrics::Sample::cpuPercent), 100.0f, tileW, palette.accent);
  ImGui::SameLine(0, gap);
  snprintf(buf, sizeof buf, "%.0f%%", std::max(0.0, s->gpuPercent));
  tile("GPU", s->gpuPercent < 0 ? "n/a" : buf, s->gpuMemUsed >= 0 ? "GPU memory in use " + gb(s->gpuMemUsed) : "no IOKit figure",
       gRing.series(&sysmetrics::Sample::gpuPercent), 100.0f, tileW, palette.success);
  ImGui::SameLine(0, gap);
  tile("Memory", gb(s->memUsed) + " / " + gb(s->memTotal),
       "compressed " + gb(s->memCompressed) + ", swap " + gb(s->swapUsed), gRing.series(&sysmetrics::Sample::memUsed),
       static_cast<float>(s->memTotal), tileW, s->swapUsed > 4e9 ? palette.warning : palette.accent);
  ImGui::SameLine(0, gap);
  tile("Metal", s->metalWorkingSet > 0 ? gb(s->metalWorkingSet) + " working set" : "n/a",
       s->metalAllocated >= 0 ? "this app has " + gb(s->metalAllocated) + " allocated" : "", gRing.series(&sysmetrics::Sample::metalAllocated),
       static_cast<float>(std::max(1.0, s->metalAllocated * 1.5)), tileW, palette.textDim);

  ImGui::Dummy(ImVec2(0, 6));
  const ImU32 thermalColor = s->thermal == 0 ? palette.success : s->thermal == 1 ? palette.warning : palette.danger;
  widgets::pill("Thermal: " + sysmetrics::thermalName(s->thermal), palette.panelAlt, thermalColor);
  ImGui::SameLine();
  widgets::pill(s->batteryPercent < 0 ? "Mains power"
                                      : (s->onBattery ? "On battery " : "On power, battery ") + std::to_string(static_cast<int>(s->batteryPercent)) + "%",
                palette.panelAlt);
  ImGui::SameLine();
  widgets::pill(s->modelsDiskFree >= 0 ? gb(s->modelsDiskFree) + " free for models" : "disk: n/a", palette.panelAlt);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", sysmetrics::modelsFolder().c_str());
  ImGui::SameLine();
  ImGui::Checkbox("Pause", &gPaused);
  if (s->swapUsed > 4e9) {
    ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.warning));
    ImGui::TextWrapped("Heavy swap use: a model that does not fit beside everything else will run from disk. Unloading one below frees memory at once.");
    ImGui::PopStyleColor();
  }

  ImGui::Dummy(ImVec2(0, 8));
  widgets::sectionHeader("MODELS IN MEMORY (OLLAMA)");
  if (!gPsError.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.danger));
    ImGui::TextWrapped("Could not read Ollama's model list: %s", gPsError.c_str());
    ImGui::PopStyleColor();
  } else if (gModels.empty()) {
    widgets::dimText(gResidency.empty() ? "Polling..." : "Nothing loaded right now.");
  }
  if (!gModels.empty() && ImGui::BeginTable("##resident", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp, ImVec2(width - 20.0f, 0))) {
    ImGui::TableSetupColumn("Model");
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("In GPU memory", ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableSetupColumn("Context", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableSetupColumn("Unloads in", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableHeadersRow();
    for (const Resident& r : gModels) {
      ImGui::PushID(r.name.c_str());
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted(r.name.c_str());
      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted(gb(r.size).c_str());
      ImGui::TableSetColumnIndex(2);
      ImGui::Text("%s (%.0f%%)", gb(r.vram).c_str(), r.size > 0 ? 100.0 * r.vram / r.size : 0.0);
      ImGui::TableSetColumnIndex(3);
      ImGui::Text("%d", r.context);
      ImGui::TableSetColumnIndex(4);
      ImGui::TextUnformatted(minutes(r.expiresIn).c_str());
      ImGui::TableSetColumnIndex(5);
      if (ImGui::SmallButton("Unload")) {
        const std::string name = r.name;
        async::run([this, name] {
          api::Result res = api_.ollamaUnload(name);
          async::post([res, name] {
            if (res.ok) widgets::toastInfo("Unloading " + name);
            else widgets::toastError("Unload failed: " + api::errorMessage(res));
            gLastPs = 0.0;
          });
        });
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  // Residency timeline: a lane per model seen lately, filled while it was in memory.
  if (gResidency.size() >= 2) {
    std::map<std::string, int> lanes;
    for (const auto& [at, names] : gResidency) {
      for (const std::string& n : names) {
        if (!lanes.count(n)) lanes[n] = static_cast<int>(lanes.size());
      }
    }
    if (!lanes.empty()) {
      ImGui::Dummy(ImVec2(0, 6));
      widgets::dimText("Residency, last %s", minutes(gResidency.back().first - gResidency.front().first).c_str());
      const float laneH = 16.0f, labelW = 220.0f;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const float plotW = width - labelW - 30.0f;
      const double t0 = gResidency.front().first, t1 = std::max(gResidency.back().first, t0 + 1.0);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      for (const auto& [name, lane] : lanes) {
        const float y = origin.y + lane * (laneH + 4.0f);
        dl->AddText(ImVec2(origin.x, y), palette.textDim, name.substr(0, 32).c_str());
        dl->AddRectFilled(ImVec2(origin.x + labelW, y), ImVec2(origin.x + labelW + plotW, y + laneH), palette.panel, 3.0f);
        for (size_t i = 0; i + 1 < gResidency.size(); ++i) {
          const auto& [at, names] = gResidency[i];
          if (std::find(names.begin(), names.end(), name) == names.end()) continue;
          const float x0 = origin.x + labelW + static_cast<float>((at - t0) / (t1 - t0)) * plotW;
          const float x1 = origin.x + labelW + static_cast<float>((gResidency[i + 1].first - t0) / (t1 - t0)) * plotW;
          dl->AddRectFilled(ImVec2(x0, y + 2.0f), ImVec2(std::max(x1, x0 + 2.0f), y + laneH - 2.0f), palette.accent, 2.0f);
        }
      }
      ImGui::Dummy(ImVec2(0, static_cast<float>(lanes.size()) * (laneH + 4.0f) + 4.0f));
    }
  }
  widgets::dimText("Sampled every second; Ollama is asked every 5 s while this tab is open. GPU figures come from IOKit's accelerator statistics.");
}

void App::healthTabHidden() { gVisible = false; }

}  // namespace app
