// `Open WebUI --layouttest`: offline checks of the layout presets.
#include <cstdio>
#include <string>

#include "core/layouts.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
}  // namespace

int runLayoutSelfTest() {
  using namespace layouts;
  printf("Open WebUI desktop layout-presets self-test\n\n");

  const auto b = builtins();
  report("builtins: five, flagged, Zen hides the sidebar and reads, Split puts the reference below", b.size() == 5 && b[0].name == "Zen" && b[0].builtin && !b[0].sidebar && b[0].reading && b[3].name == "Split" &&
                                                                                                        b[3].column == "reference" && b[3].columnBelow && b[2].column == "reference" && !b[2].columnBelow);
  Layout l;
  l.name = "Mine";
  l.sidebar = false;
  l.sidebarWidth = 999.0f;
  l.column = "pins";
  l.columnBelow = true;
  l.reading = true;
  l.floating = true;
  l.windowW = 1200.0f;
  l.windowH = 100.0f;
  l.referenceChatId = "abc";
  const Layout back = fromJson(toJson(l));
  report("round-trip clamps the sidebar and window and keeps the rest", back.name == "Mine" && !back.sidebar && back.sidebarWidth == 480.0f && back.column == "pins" && back.columnBelow && back.reading && back.floating &&
                                                                            back.windowW == 1200.0f && back.windowH == 360.0f && back.referenceChatId == "abc" && !back.builtin);
  report("fromJson: unknown column -> none, junk -> defaults", fromJson(Json{{"name", "x"}, {"column", "weird"}}).column == "none" && fromJson(Json("nope")).sidebar && fromJson(Json("nope")).name.empty());
  report("validColumn / columnKinds", validColumn("artifacts") && !validColumn("document") && columnKinds().size() == 5);

  Json extra{{"layouts", Json::array({toJson(l), Json{{"name", "Zen"}, {"sidebar", true}, {"reading", false}}, Json{{"name", ""}}, Json{{"name", "Mine"}, {"column", "tree"}}})}};
  const auto s = saved(extra);
  report("saved: nameless dropped, duplicates keep the first", s.size() == 2 && s[0].name == "Mine" && s[0].column == "pins" && s[1].name == "Zen");
  const auto a = all(extra);
  report("all: built-ins first, a saved 'Zen' replaces the built-in, saved ones appended", a.size() == 6 && a[0].name == "Zen" && a[0].sidebar && !a[0].reading && !a[0].builtin && a[5].name == "Mine" && indexOf(a, "Split") == 3 &&
                                                                                               indexOf(a, "Nope") == -1);
  report("toSaved round-trips through saved()", saved(Json{{"layouts", toSaved(s)}}).size() == 2);
  report("summary", summary(b[0]) == "no sidebar - reading" && summary(b[3]) == "sidebar 280 - reference below" && summary(back) == "no sidebar - pins below - reading - floating - 1200x360");

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
