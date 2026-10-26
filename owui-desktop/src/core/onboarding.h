// Onboarding (#145): the first-run tour, the "what's new" sheet shown once
// per update, and the state that remembers both. App-free; --onboardtest.
#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace onboarding {

using Json = nlohmann::json;

struct TourStep {
  std::string id, title, body;
  std::string anchor;   // "rail", "sidebar", "header", "composer", "palette", "tools", "settings", "none"
};
const std::vector<TourStep>& tourSteps();

struct State {
  bool tourDone = false;
  int tourStep = 0;
  std::string lastSeenVersion;   // the last "what's new" the user dismissed
};
State fromJson(const Json& extra);   // extra["onboarding"]
Json toJson(const State& state);

struct Release {
  std::string version, date;
  std::vector<std::string> items;
};
const std::vector<Release>& releases();   // newest first
int compareVersion(const std::string& a, const std::string& b);   // -1, 0, 1 on dotted numbers
// Releases newer than `lastSeen` and not newer than `current`, newest first.
std::vector<Release> pendingReleases(const std::string& lastSeen, const std::string& current);
std::string whatsNewText(const std::vector<Release>& list);

}  // namespace onboarding
