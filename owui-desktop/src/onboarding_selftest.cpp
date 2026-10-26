// `Open WebUI --onboardtest`: offline checks of onboarding.
#include <cstdio>
#include <set>
#include <string>

#include "core/onboarding.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
}  // namespace

int runOnboardingSelfTest() {
  using namespace onboarding;
  printf("Open WebUI desktop onboarding self-test\n\n");

  const auto& steps = tourSteps();
  std::set<std::string> ids, anchors;
  for (const TourStep& s : steps) {
    ids.insert(s.id);
    anchors.insert(s.anchor);
  }
  report("tourSteps: several steps, unique ids, titles and bodies, known anchors", steps.size() >= 6 && ids.size() == steps.size() && !steps[0].title.empty() && !steps.back().body.empty() &&
                                                                                      anchors.count("rail") && anchors.count("composer") && anchors.count("none") && steps[0].anchor == "none");

  State s = fromJson(Json::object());
  report("state defaults", !s.tourDone && s.tourStep == 0 && s.lastSeenVersion.empty());
  s.tourDone = true;
  s.tourStep = 3;
  s.lastSeenVersion = "1.1";
  const State back = fromJson(Json{{"onboarding", toJson(s)}});
  report("state round-trip; step clamped; junk ignored", back.tourDone && back.tourStep == 3 && back.lastSeenVersion == "1.1" && fromJson(Json{{"onboarding", Json{{"tourStep", 99}}}}).tourStep == static_cast<int>(steps.size()) - 1 &&
                                                              !fromJson(Json{{"onboarding", "x"}}).tourDone);

  report("compareVersion: numeric parts, missing parts are zero", compareVersion("1.10", "1.9") == 1 && compareVersion("1.0", "1.0.0") == 0 && compareVersion("0.9", "1.0") == -1 && compareVersion("2", "1.99") == 1);
  report("releases: newest first, items present, dated", releases().size() >= 2 && compareVersion(releases()[0].version, releases()[1].version) > 0 && !releases()[0].items.empty() && releases()[0].date.size() == 10);
  const std::vector<Release> all = pendingReleases("", "1.1");
  report("pendingReleases: never seen -> everything up to the current version; seen 1.0 -> only 1.1; seen 1.1 -> nothing; old build hides newer notes",
         all.size() == 2 && all[0].version == "1.1" && pendingReleases("1.0", "1.1").size() == 1 && pendingReleases("1.1", "1.1").empty() && pendingReleases("", "1.0").size() == 1 && pendingReleases("", "1.0")[0].version == "1.0");
  const std::string text = whatsNewText(pendingReleases("1.0", "1.1"));
  report("whatsNewText", text.find("Version 1.1 - 2026-10-25\n  - Theme editor") == 0 && text.find("Version 1.0") == std::string::npos && whatsNewText({}).empty());

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
