#import <CoreText/CoreText.h>
#import <Foundation/Foundation.h>

#include "core/i18n.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <unordered_map>

namespace i18n {
namespace {

std::string gChoice;   // "" = system
bool gFormatsFromLanguage = false;
std::string gResolved = "en";
std::mutex gMutex;
std::map<std::string, std::string> gCache;   // "lang|english" -> translated, stable c_str
int columnFor(const std::string& lang) { return lang == "de" ? 1 : lang == "es" ? 2 : lang == "fr" ? 3 : lang == "ja" ? 4 : 0; }

const char* cell(const Row& r, int col) { return col == 1 ? r.de : col == 2 ? r.es : col == 3 ? r.fr : col == 4 ? r.ja : r.en; }

const std::unordered_map<std::string, const Row*>& index() {
  static const auto* m = [] {
    auto* x = new std::unordered_map<std::string, const Row*>();
    for (const Row& r : table()) (*x)[r.en] = &r;
    return x;
  }();
  return *m;
}

std::string toStd(NSString* s) { return s ? std::string(s.UTF8String) : std::string(); }

NSLocale* localeFor(const std::string& id) { return id.empty() ? [NSLocale currentLocale] : [NSLocale localeWithLocaleIdentifier:[NSString stringWithUTF8String:id.c_str()]]; }

uint32_t decode(const std::string& s, size_t* i) {
  const unsigned char c = static_cast<unsigned char>(s[*i]);
  uint32_t cp = c;
  int extra = 0;
  if (c >= 0xF0) cp = c & 0x07, extra = 3;
  else if (c >= 0xE0) cp = c & 0x0F, extra = 2;
  else if (c >= 0xC0) cp = c & 0x1F, extra = 1;
  ++*i;
  for (int k = 0; k < extra && *i < s.size(); ++k, ++*i) cp = (cp << 6) | (static_cast<unsigned char>(s[*i]) & 0x3F);
  return cp;
}

bool rtlChar(uint32_t cp) {
  return (cp >= 0x0590 && cp <= 0x08FF) || (cp >= 0xFB1D && cp <= 0xFDFF) || (cp >= 0xFE70 && cp <= 0xFEFF) || (cp >= 0x10800 && cp <= 0x10FFF) || (cp >= 0x1E800 && cp <= 0x1EFFF);
}

}  // namespace

const std::vector<Language>& languages() {
  static const std::vector<Language> l = {{"", "System"}, {"en", "English"}, {"de", "Deutsch"}, {"es", "Español"}, {"fr", "Français"}, {"ja", "日本語"}, {"qps", "Pseudo (shows untranslated text)"}};
  return l;
}

std::string systemLanguage() {
  @autoreleasepool {
    for (NSString* pref in [NSLocale preferredLanguages]) {
      const std::string code = toStd(pref).substr(0, 2);
      if (code == "en" || code == "de" || code == "es" || code == "fr" || code == "ja") return code;
    }
  }
  return "en";
}

void setLanguage(const std::string& code) {
  std::lock_guard<std::mutex> lock(gMutex);
  gChoice = code;
  gResolved = code.empty() ? systemLanguage() : code;
}

void setFormatsFromLanguage(bool on) {
  std::lock_guard<std::mutex> lock(gMutex);
  gFormatsFromLanguage = on;
}

bool formatsFromLanguage() {
  std::lock_guard<std::mutex> lock(gMutex);
  return gFormatsFromLanguage;
}

std::string language() {
  std::lock_guard<std::mutex> lock(gMutex);
  return gResolved;
}

size_t tableSize() { return table().size(); }

std::string pseudo(const std::string& english) {
  static const std::map<char, const char*> accent = {{'a', "á"}, {'e', "é"}, {'i', "î"}, {'o', "ö"}, {'u', "ü"}, {'c', "ç"}, {'n', "ñ"}, {'s', "š"}, {'A', "Å"}, {'E', "É"}, {'O', "Ö"}, {'S', "Š"}, {'C', "Ç"}};
  std::string out = "[";
  for (size_t i = 0; i < english.size(); ++i) {
    const char c = english[i];
    if (c == '%' && i + 1 < english.size()) {   // keep format specifiers as they are
      out += c;
      out += english[++i];
      continue;
    }
    auto it = accent.find(c);
    if (it != accent.end()) out += it->second;
    else out += c;
  }
  return out + "]";
}

const char* tr(const char* english) {
  if (!english || !*english) return english;
  std::lock_guard<std::mutex> lock(gMutex);
  if (gResolved == "en") return english;
  const std::string all = english;
  const size_t hash = all.find("##");
  const std::string label = hash == std::string::npos ? all : all.substr(0, hash);
  const std::string id = hash == std::string::npos ? "" : all.substr(hash);
  if (label.empty()) return english;
  const std::string key = gResolved + "|" + all;
  auto hit = gCache.find(key);
  if (hit != gCache.end()) return hit->second.c_str();
  std::string out;
  if (gResolved == "qps") {
    out = pseudo(label) + id;
  } else {
    auto it = index().find(label);
    if (it == index().end()) return english;   // untranslated: English
    const char* t = cell(*it->second, columnFor(gResolved));
    out = std::string(t && *t ? t : label.c_str()) + id;
  }
  return gCache.emplace(key, out).first->second.c_str();
}

std::string tr(const std::string& english) { return tr(english.c_str()); }

std::string localeId() {
  const std::string lang = language();
  if (gChoice.empty() || lang == "qps") return "";   // the system's own locale
  if (formatsFromLanguage()) {
    static const std::map<std::string, std::string> home = {{"en", "en_US"}, {"de", "de_DE"}, {"es", "es_ES"}, {"fr", "fr_FR"}, {"ja", "ja_JP"}};
    auto it = home.find(lang);
    if (it != home.end()) return it->second;
  }
  @autoreleasepool {
    NSString* region = [[NSLocale currentLocale] objectForKey:NSLocaleCountryCode];
    return lang + (region ? "_" + toStd(region) : "");
  }
}

std::string numberIn(const std::string& locale, double value, int decimals) {
  @autoreleasepool {
    NSNumberFormatter* f = [[NSNumberFormatter alloc] init];
    f.locale = localeFor(locale);
    f.numberStyle = NSNumberFormatterDecimalStyle;
    f.minimumFractionDigits = decimals;
    f.maximumFractionDigits = decimals;
    return toStd([f stringFromNumber:@(value)]);
  }
}

std::string relativeIn(const std::string& locale, int64_t unixSeconds, int64_t now) {
  if (unixSeconds <= 0) return "";
  const int64_t delta = std::max<int64_t>(0, now - unixSeconds);
  @autoreleasepool {
    NSRelativeDateTimeFormatter* f = [[NSRelativeDateTimeFormatter alloc] init];
    f.locale = localeFor(locale);
    f.unitsStyle = NSRelativeDateTimeFormatterUnitsStyleFull;
    f.dateTimeStyle = NSRelativeDateTimeFormatterStyleNamed;   // "yesterday", "gestern"
    if (delta >= 86400 * 30) {
      NSDateFormatter* d = [[NSDateFormatter alloc] init];
      d.locale = f.locale;
      d.dateStyle = NSDateFormatterMediumStyle;
      d.timeStyle = NSDateFormatterNoStyle;
      return toStd([d stringFromDate:[NSDate dateWithTimeIntervalSince1970:static_cast<double>(unixSeconds)]]);
    }
    // Whole units, as the old English helper did: 90 s is "1 minute ago".
    const int64_t rounded = delta < 3600 ? delta / 60 * 60 : delta < 86400 ? delta / 3600 * 3600 : delta / 86400 * 86400;
    return toStd([f localizedStringFromTimeInterval:-static_cast<double>(rounded)]);
  }
}

std::string dateIn(const std::string& locale, int64_t unixSeconds) {
  @autoreleasepool {
    NSDateFormatter* d = [[NSDateFormatter alloc] init];
    d.locale = localeFor(locale);
    d.timeZone = [NSTimeZone timeZoneForSecondsFromGMT:0];
    d.dateStyle = NSDateFormatterLongStyle;
    d.timeStyle = NSDateFormatterNoStyle;
    return toStd([d stringFromDate:[NSDate dateWithTimeIntervalSince1970:static_cast<double>(unixSeconds)]]);
  }
}

std::string relative(int64_t unixSeconds, int64_t now) {
  if (unixSeconds > 0 && now - unixSeconds < 60) return tr("just now");
  return relativeIn(localeId(), unixSeconds, now);
}

std::string dateTime(int64_t unixSeconds, bool withTime) {
  if (unixSeconds <= 0) return "";
  @autoreleasepool {
    NSDateFormatter* d = [[NSDateFormatter alloc] init];
    d.locale = localeFor(localeId());
    d.dateStyle = NSDateFormatterMediumStyle;
    d.timeStyle = withTime ? NSDateFormatterShortStyle : NSDateFormatterNoStyle;
    return toStd([d stringFromDate:[NSDate dateWithTimeIntervalSince1970:static_cast<double>(unixSeconds)]]);
  }
}

std::string number(double value, int decimals) { return numberIn(localeId(), value, decimals); }

std::string bytes(int64_t count) {
  const char* units[] = {"B", "KB", "MB", "GB", "TB"};
  double v = static_cast<double>(count);
  int u = 0;
  while (v >= 1024.0 && u < 4) {
    v /= 1024.0;
    ++u;
  }
  return number(v, u == 0 ? 0 : 1) + " " + units[u];
}

bool isRtl(const std::string& s) {
  size_t i = 0;
  while (i < s.size()) {
    const uint32_t cp = decode(s, &i);
    if (rtlChar(cp)) return true;
    if (cp < 0x80 ? std::isalpha(static_cast<int>(cp)) : (cp >= 0x00C0 && !(cp >= 0x2000 && cp <= 0x2BFF) && !(cp >= 0x3000 && cp <= 0x303F))) return false;
  }
  return false;
}

std::string visual(const std::string& logical) {
  if (logical.empty()) return logical;
  @autoreleasepool {
    NSString* text = [NSString stringWithUTF8String:logical.c_str()];
    if (!text) return logical;
    CTWritingDirection dir = kCTWritingDirectionRightToLeft;
    CTParagraphStyleSetting setting = {kCTParagraphStyleSpecifierBaseWritingDirection, sizeof(dir), &dir};
    CTParagraphStyleRef style = CTParagraphStyleCreate(&setting, 1);
    NSDictionary* attrs = @{(__bridge id)kCTParagraphStyleAttributeName : (__bridge id)style};
    NSAttributedString* a = [[NSAttributedString alloc] initWithString:text attributes:attrs];
    CTLineRef line = CTLineCreateWithAttributedString((__bridge CFAttributedStringRef)a);
    CFRelease(style);
    NSArray* runs = (__bridge NSArray*)CTLineGetGlyphRuns(line);   // in visual order, left to right
    NSMutableString* out = [NSMutableString string];
    static NSDictionary* mirror = @{@"(" : @")", @")" : @"(", @"[" : @"]", @"]" : @"[", @"{" : @"}", @"}" : @"{", @"<" : @">", @">" : @"<", @"«" : @"»", @"»" : @"«"};
    for (id r in runs) {
      CTRunRef run = (__bridge CTRunRef)r;
      const CFRange range = CTRunGetStringRange(run);
      NSString* part = [text substringWithRange:NSMakeRange(static_cast<NSUInteger>(range.location), static_cast<NSUInteger>(range.length))];
      if (CTRunGetStatus(run) & kCTRunStatusRightToLeft) {
        // Reverse by grapheme cluster so combining marks stay on their letter; mirror brackets.
        NSMutableArray* clusters = [NSMutableArray array];
        [part enumerateSubstringsInRange:NSMakeRange(0, part.length)
                                 options:NSStringEnumerationByComposedCharacterSequences
                              usingBlock:^(NSString* sub, NSRange, NSRange, BOOL*) { [clusters addObject:mirror[sub] ?: sub]; }];
        for (NSString* c in clusters.reverseObjectEnumerator) [out appendString:c];
      } else {
        [out appendString:part];
      }
    }
    CFRelease(line);
    return toStd(out);
  }
}

std::vector<std::string> wrapWords(const std::string& text, float width, float (*measure)(const std::string&)) {
  std::vector<std::string> lines;
  std::string line, word;
  auto flushWord = [&] {
    if (word.empty()) return;
    const std::string candidate = line.empty() ? word : line + " " + word;
    if (!line.empty() && measure(candidate) > width) {
      lines.push_back(line);
      line = word;
    } else {
      line = candidate;
    }
    word.clear();
  };
  for (char c : text) {
    if (c == ' ' || c == '\n') {
      flushWord();
      if (c == '\n') {
        lines.push_back(line);
        line.clear();
      }
    } else {
      word += c;
    }
  }
  flushWord();
  if (!line.empty()) lines.push_back(line);
  return lines;
}

}  // namespace i18n
