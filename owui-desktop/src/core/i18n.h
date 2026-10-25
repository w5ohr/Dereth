// Localisation (#144): a strings table for the app's frame (rail, pane
// headers, sidebar, sign-in, composer, the empty chat, section headers) in
// German, Spanish, French and Japanese plus a pseudo-locale that shows what
// is still untranslated; locale-aware dates, relative times, numbers and
// sizes through Apple's formatters; and right-to-left paragraphs (Hebrew,
// Arabic) reordered for display. --i18ntest covers it.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace i18n {

struct Language {
  const char* code;     // "" = follow the system
  const char* name;     // in its own language
};
const std::vector<Language>& languages();   // System, English, Deutsch, Español, Français, 日本語, Pseudo
void setLanguage(const std::string& code);  // "" = system
// Formats (dates, numbers) from this Mac's region - the macOS convention, the
// default - or from the chosen language's home region (de -> de_DE).
void setFormatsFromLanguage(bool on);
bool formatsFromLanguage();
std::string language();                     // the resolved code: en, de, es, fr, ja, qps
std::string systemLanguage();               // the first of the system's preferred languages we have (else en)

// The translation of an English UI string, or the string itself. "Label##id"
// translates the label and keeps the id. Pointers stay valid for the process.
const char* tr(const char* english);
std::string tr(const std::string& english);
size_t tableSize();
// Rows of the table: english, de, es, fr, ja. For tests and coverage.
struct Row {
  const char* en;
  const char* de;
  const char* es;
  const char* fr;
  const char* ja;
};
const std::vector<Row>& table();
std::string pseudo(const std::string& english);   // "[Ŝéttîñĝš]" keeping %s / %d

// Locale formatting (Apple formatters; the locale is the chosen language
// with the system's region, or the system locale).
std::string localeId();
std::string relative(int64_t unixSeconds, int64_t now);   // "2 minutes ago", "vor 2 Minuten"
std::string dateTime(int64_t unixSeconds, bool withTime);
std::string number(double value, int decimals);
std::string bytes(int64_t count);
// Same, for an explicit locale identifier (tests).
std::string numberIn(const std::string& locale, double value, int decimals);
std::string relativeIn(const std::string& locale, int64_t unixSeconds, int64_t now);
std::string dateIn(const std::string& locale, int64_t unixSeconds);

// Right to left.
bool isRtl(const std::string& utf8);                 // the first strong character is Hebrew / Arabic / ...
std::string visual(const std::string& logicalLine);  // display order for an RTL-base line
std::vector<std::string> wrapWords(const std::string& text, float width, float (*measure)(const std::string&));

}  // namespace i18n
