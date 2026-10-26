#include "screenwatch.h"

#include <algorithm>
#include <cctype>
#include <ctime>

#include "core/common.h"

namespace screenwatch {

int clampInterval(int seconds) { return std::clamp(seconds, 10, 3600); }

Json buildMessages(const Config& config, const std::string& screenText, const std::string& pngBase64) {
  const std::string system =
      "You watch a window on the user's screen and answer one standing question about it. "
      "Reply with YES or NO alone on the first line - YES only when the answer is clearly yes from what is shown - "
      "then one short sentence saying what you see that decides it.";
  Json messages = Json::array();
  messages.push_back(Json{{"role", "system"}, {"content", system}});
  const std::string question = "Question: " + core::trim(config.question);
  if (config.mode == Mode::Image && !pngBase64.empty()) {
    messages.push_back(Json{{"role", "user"},
                            {"content", Json::array({Json{{"type", "text"}, {"text", question + "\nThe window is attached as a screenshot."}},
                                                     Json{{"type", "image_url"}, {"image_url", {{"url", "data:image/png;base64," + pngBase64}}}}})}});
  } else {
    std::string text = screenText;
    constexpr size_t kMax = 12000;   // the end of a log is what changes; keep the tail
    if (text.size() > kMax) text = "[...]\n" + text.substr(text.size() - kMax);
    messages.push_back(Json{{"role", "user"},
                            {"content", question + "\n\nThe text on the window now (read from the screen, may have OCR slips):\n<<<\n" +
                                            (core::trim(text).empty() ? std::string("(no readable text)") : text) + "\n>>>"}});
  }
  return messages;
}

Verdict parseVerdict(const std::string& replyIn) {
  std::string reply = replyIn;
  // Reasoning models think first: drop that.
  for (;;) {
    const size_t a = reply.find("<think>");
    if (a == std::string::npos) break;
    const size_t b = reply.find("</think>", a);
    reply.erase(a, b == std::string::npos ? std::string::npos : b + 8 - a);
  }
  reply = core::trim(reply);
  Verdict v;
  // The first word, past any markdown or punctuation, decides.
  size_t i = 0;
  while (i < reply.size() && !std::isalpha(static_cast<unsigned char>(reply[i]))) ++i;
  size_t j = i;
  while (j < reply.size() && std::isalpha(static_cast<unsigned char>(reply[j]))) ++j;
  const std::string word = core::lower(reply.substr(i, j - i));
  i = j;
  if (word == "yes" || word == "no") {
    v.known = true;
    v.met = word == "yes";
  }
  std::string rest = i < reply.size() ? reply.substr(i) : "";
  // Up to the first full line of explanation.
  rest = core::trim(rest);
  while (!rest.empty() && (rest[0] == '*' || rest[0] == '.' || rest[0] == ',' || rest[0] == '-' || rest[0] == ':' || rest[0] == '!')) rest.erase(0, 1);
  rest = core::trim(rest);
  const size_t nl = rest.find('\n');
  v.note = core::trim(nl == std::string::npos ? rest : rest.substr(0, nl));
  if (!v.known) v.note = core::trim(reply.substr(0, std::min<size_t>(reply.size(), 200)));
  if (v.note.size() > 240) v.note = v.note.substr(0, 240) + "...";
  return v;
}

uint64_t textFingerprint(const std::string& text) {
  // Clocks and counters tick ("12:04:31", "3.2s"); collapse runs of digits
  // and whitespace so a still log is still.
  uint64_t h = 1469598103934665603ull;
  bool space = false, digit = false;
  for (unsigned char c : text) {
    if (std::isspace(c)) {
      space = true;
      continue;
    }
    if (std::isdigit(c)) {
      if (digit) continue;
      digit = true;
      c = '#';
    } else {
      digit = false;
    }
    if (space) {
      h ^= ' ';
      h *= 1099511628211ull;
      space = false;
    }
    h ^= c;
    h *= 1099511628211ull;
  }
  return h;
}

uint64_t imageFingerprint(const std::vector<uint8_t>& g) {
  if (g.size() < 72) return 0;
  uint64_t bits = 0;
  int n = 0;
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 8; ++x) {
      if (g[static_cast<size_t>(y * 9 + x)] > g[static_cast<size_t>(y * 9 + x + 1)]) bits |= 1ull << n;
      ++n;
    }
  }
  return bits;
}

int hammingDistance(uint64_t a, uint64_t b) { return __builtin_popcountll(a ^ b); }

bool Gate::shouldAsk(uint64_t fingerprint, Mode mode, int maxUnchangedSkips) {
  const bool same = hasLast && (mode == Mode::Text ? fingerprint == lastSent : hammingDistance(fingerprint, lastSent) <= 2);
  if (same && skipsSinceSent < maxUnchangedSkips) {
    ++skipsSinceSent;
    return false;
  }
  lastSent = fingerprint;
  hasLast = true;
  skipsSinceSent = 0;
  return true;
}

std::string describe(const Check& check) {
  std::time_t t = static_cast<std::time_t>(check.at);
  std::tm local{};
  localtime_r(&t, &local);
  char hm[16];
  std::strftime(hm, sizeof hm, "%H:%M:%S", &local);
  std::string out = std::string(hm) + "  ";
  if (!check.error.empty()) return out + "error: " + check.error;
  if (check.skipped) return out + "unchanged, not asked";
  if (!check.verdict.known) return out + "unclear: " + check.verdict.note;
  return out + (check.verdict.met ? "YES - " : "no - ") + check.verdict.note;
}

}  // namespace screenwatch
