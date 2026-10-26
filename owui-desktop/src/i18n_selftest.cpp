// `Open WebUI --i18ntest`: the strings table, lookup, the pseudo-locale,
// locale formatting and right-to-left ordering.
#include <cstdio>
#include <set>
#include <string>

#include "core/i18n.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
int count(const std::string& s, const std::string& needle) {
  int n = 0;
  for (size_t p = s.find(needle); p != std::string::npos; p = s.find(needle, p + 1)) ++n;
  return n;
}
float measure(const std::string& s) { return static_cast<float>(s.size()); }
}  // namespace

int runI18nSelfTest() {
  using namespace i18n;
  printf("Open WebUI desktop localisation self-test\n\n");

  std::set<std::string> keys;
  bool complete = true, unique = true, placeholders = true;
  std::string bad;
  for (const Row& r : table()) {
    if (!keys.insert(r.en).second) unique = false, bad = r.en;
    for (const char* t : {r.de, r.es, r.fr, r.ja}) {
      if (!t || !*t) complete = false, bad = r.en;
      if (t && (count(t, "%s") != count(r.en, "%s") || count(t, "%d") != count(r.en, "%d"))) placeholders = false, bad = r.en;
    }
  }
  report("table: " + std::to_string(table().size()) + " strings, every one in de / es / fr / ja, no duplicate keys, placeholders kept", table().size() > 90 && complete && unique && placeholders, bad);

  setLanguage("en");
  const char* settings = "Settings";
  report("English returns the very string it was given", tr(settings) == settings && language() == "en");
  setLanguage("de");
  report("German: plain, with an ##id kept, a format string, unknown strings fall back to English", std::string(tr("Settings")) == "Einstellungen" && std::string(tr("New chat##sidebar")) == "Neuer Chat##sidebar" &&
                                                                                                         std::string(tr("Hello, %s")) == "Hallo, %s" && std::string(tr("Nothing like this")) == "Nothing like this");
  const char* p1 = tr("Settings");
  const char* p2 = tr("Settings");
  report("translations are cached: the same pointer twice", p1 == p2);
  setLanguage("ja");
  report("Japanese", std::string(tr("Send a message...")) == "メッセージを送信..." && std::string(tr("Today")) == "今日");
  setLanguage("qps");
  report("pseudo-locale marks every looked-up string and keeps placeholders and ids",
         std::string(tr("Settings")) == "[Šéttîñgš]" && std::string(tr("Hello, %s")).find("%s") != std::string::npos && std::string(tr("Open##x")).rfind("##x") != std::string::npos, tr("Settings"));
  setLanguage("");
  report("system language resolves to one we have", language() == systemLanguage() && !systemLanguage().empty(), systemLanguage());

  report("numbers per locale", numberIn("en_US", 1234.5, 1) == "1,234.5" && numberIn("de_DE", 1234.5, 1) == "1.234,5" && numberIn("fr_FR", 1234.5, 1).find("234,5") != std::string::npos &&
                                   numberIn("ja_JP", 1234.5, 1) == "1,234.5",
         numberIn("de_DE", 1234.5, 1) + " / " + numberIn("fr_FR", 1234.5, 1));
  const int64_t now = 1792900000;
  report("relative times per locale", relativeIn("en_US", now - 125, now) == "2 minutes ago" && relativeIn("de_DE", now - 125, now) == "vor 2 Minuten" && relativeIn("es_ES", now - 7300, now) == "hace 2 horas" &&
                                          relativeIn("en_US", now - 90000, now) == "yesterday" && relativeIn("ja_JP", now - 125, now).find("分前") != std::string::npos,
         relativeIn("de_DE", now - 125, now) + " / " + relativeIn("ja_JP", now - 125, now) + " / " + relativeIn("en_US", now - 90000, now));
  report("dates per locale", dateIn("de_DE", now).find("Oktober 2026") != std::string::npos && dateIn("fr_FR", now).find("octobre 2026") != std::string::npos && dateIn("ja_JP", now).find("2026年10月") != std::string::npos,
         dateIn("de_DE", now) + " / " + dateIn("ja_JP", now));

  report("isRtl: Hebrew and Arabic first, digits are weak, Latin first is not", isRtl("שלום עולם") && isRtl("مرحبا بالعالم") && isRtl("123 שלום") && !isRtl("hello שלום") && !isRtl("") && !isRtl("2026"));
  const std::string v1 = visual("שלום world");
  const std::string v2 = visual("(שלום)");
  const std::string v3 = visual("הגרסה 1.2 מוכנה");
  report("visual: an English word stays left-to-right inside an RTL line; brackets mirror; numbers keep their order", v1 == "world םולש" && v2 == "(םולש)" && v3 == "הנכומ 1.2 הסרגה", v1 + " | " + v2 + " | " + v3);
  const auto lines = wrapWords("one two three four five", 9.0f, measure);
  setLanguage("de");
  setFormatsFromLanguage(false);
  const std::string regional = localeId();
  setFormatsFromLanguage(true);
  report("locale: the Mac's region by default, the language's home region when asked", regional.rfind("de", 0) == 0 && localeId() == "de_DE" && number(1234.5, 1) == "1.234,5", regional + " / " + localeId());
  setFormatsFromLanguage(false);
  setLanguage("");
  report("wrapWords", lines.size() == 3 && lines[0] == "one two" && lines[1] == "three" && lines[2] == "four five");

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
