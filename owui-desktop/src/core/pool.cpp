#include "pool.h"

#include <algorithm>
#include <cmath>

namespace pool {
namespace {

const Health* find(const std::vector<Health>& health, const std::string& id) {
  for (const Health& h : health) {
    if (h.id == id) return &h;
  }
  return nullptr;
}

double load(const Health* h) {
  if (!h) return 0.0;
  return h->inFlight * 10.0 + h->running * 1.0 + h->latencyMs / 1000.0;
}

}  // namespace

Prefs fromJson(const Json& extra) {
  Prefs p;
  const Json j = extra.is_object() ? extra.value("backendPool", Json::object()) : Json::object();
  if (!j.is_object()) return p;
  p.enabled = j.value("enabled", false);
  if (j.contains("members") && j["members"].is_array()) {
    for (const auto& m : j["members"]) {
      if (m.is_string() && std::find(p.members.begin(), p.members.end(), m.get<std::string>()) == p.members.end()) p.members.push_back(m.get<std::string>());
    }
  }
  const std::string s = j.value("strategy", p.strategy);
  if (s == "round-robin" || s == "first-alive" || s == "least-loaded") p.strategy = s;
  p.checkEverySec = std::clamp(j.value("checkEverySec", p.checkEverySec), 5, 600);
  p.failover = j.value("failover", true);
  return p;
}

Json toJson(const Prefs& p) { return Json{{"enabled", p.enabled}, {"members", p.members}, {"strategy", p.strategy}, {"checkEverySec", p.checkEverySec}, {"failover", p.failover}}; }

int runningFrom(const Json& response) {
  if (!response.is_object() || !response.contains("models") || !response["models"].is_array()) return 0;
  int n = 0;
  for (const auto& m : response["models"]) {
    if (m.is_object() && (m.contains("size_vram") || m.contains("expires_at") || m.contains("digest"))) ++n;
  }
  return n;
}

std::vector<std::string> order(const Prefs& prefs, const std::vector<Health>& health, int counter) {
  std::vector<std::string> alive;
  for (const std::string& id : prefs.members) {
    const Health* h = find(health, id);
    if (!h || h->alive) alive.push_back(id);
  }
  if (alive.empty()) return alive;
  if (prefs.strategy == "round-robin") {
    const size_t start = static_cast<size_t>(std::max(0, counter)) % alive.size();
    std::rotate(alive.begin(), alive.begin() + static_cast<long>(start), alive.end());
    return alive;
  }
  if (prefs.strategy == "first-alive") return alive;
  std::stable_sort(alive.begin(), alive.end(), [&](const std::string& a, const std::string& b) { return load(find(health, a)) < load(find(health, b)); });
  return alive;
}

std::string pick(const Prefs& prefs, const std::vector<Health>& health, int counter) {
  const std::vector<std::string> o = order(prefs, health, counter);
  return o.empty() ? "" : o.front();
}

std::string describe(const Health& h) {
  if (h.checkedAt == 0) return "not checked yet";
  if (!h.alive) return "down: " + (h.error.empty() ? std::string("no answer") : h.error);
  std::string out = "alive - " + std::to_string(h.running) + " model" + (h.running == 1 ? "" : "s") + " loaded, " + std::to_string(h.inFlight) + " in flight, " +
                    std::to_string(static_cast<int>(std::lround(h.latencyMs))) + " ms";
  return out;
}

}  // namespace pool
