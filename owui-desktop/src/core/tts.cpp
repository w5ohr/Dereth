#include "tts.h"

#include <algorithm>
#include <regex>
#include <set>

#include "common.h"

namespace tts {
namespace {

std::string stripInline(std::string line) {
  // Images and links keep their text.
  static const std::regex kImage(R"(!\[([^\]]*)\]\([^)]*\))");
  static const std::regex kLink(R"(\[([^\]]+)\]\([^)]*\))");
  // The host ends on a letter or digit and the path never claims trailing
  // punctuation, so "see https://x.org/a." keeps its full stop.
  static const std::regex kUrl(R"((https?://)([A-Za-z0-9.\-]*[A-Za-z0-9])(?::\d+)?(?:[^\s)>\]]*[^\s)>\].,;:!?])?)");
  static const std::regex kCite(R"(\s?\[\^?\d{1,3}\])");
  line = std::regex_replace(line, kImage, "$1");
  line = std::regex_replace(line, kLink, "$1");
  line = std::regex_replace(line, kUrl, "link to $2");
  line = std::regex_replace(line, kCite, "");
  line = core::replaceAll(line, "**", "");
  line = core::replaceAll(line, "__", "");
  line = core::replaceAll(line, "~~", "");
  line = core::replaceAll(line, "`", "");
  // Single asterisks mark emphasis; a lone one in prose is rare enough to drop.
  line.erase(std::remove(line.begin(), line.end(), '*'), line.end());
  return line;
}

std::string stripBlock(std::string line) {
  size_t i = 0;
  while (i < line.size() && line[i] == ' ') ++i;
  std::string rest = line.substr(i);
  // Headings.
  if (!rest.empty() && rest[0] == '#') {
    size_t h = 0;
    while (h < rest.size() && rest[h] == '#') ++h;
    if (h < rest.size() && rest[h] == ' ') return core::trim(rest.substr(h));
  }
  // Blockquotes.
  while (core::startsWith(rest, "> ") || rest == ">") rest = core::trim(rest.substr(1));
  // Bullets.
  if (core::startsWith(rest, "- ") || core::startsWith(rest, "* ") || core::startsWith(rest, "+ ")) rest = rest.substr(2);
  else if (core::startsWith(rest, "- [ ] ") || core::startsWith(rest, "- [x] ")) rest = rest.substr(6);
  // Numbered lists keep their number ("1." reads naturally).
  // Tables: separator rows go, cells become a comma list.
  if (!rest.empty() && rest[0] == '|') {
    bool separator = true;
    for (char c : rest) {
      if (c != '|' && c != '-' && c != ':' && c != ' ') {
        separator = false;
        break;
      }
    }
    if (separator) return "";
    std::string row;
    for (const std::string& cell : core::split(rest.substr(1), '|')) {
      const std::string t = core::trim(cell);
      if (t.empty()) continue;
      if (!row.empty()) row += ", ";
      row += t;
    }
    return row + ".";
  }
  // Horizontal rules.
  if (rest == "---" || rest == "***" || rest == "___") return "";
  return rest;
}

bool endsSentence(char c) { return c == '.' || c == '!' || c == '?'; }

}  // namespace

Prefs fromJson(const Json& extra) {
  Prefs p;
  const Json j = extra.is_object() ? extra.value("tts", Json::object()) : Json::object();
  if (!j.is_object()) return p;
  p.mode = modeFromName(j.value("mode", std::string(modeName(p.mode))));
  if (j.contains("localVoice") && j["localVoice"].is_string()) p.localVoice = j["localVoice"].get<std::string>();
  p.rate = std::clamp(j.value("rate", p.rate), 0.5f, 2.0f);
  p.pitch = std::clamp(j.value("pitch", p.pitch), 0.5f, 2.0f);
  p.skipCode = j.value("skipCode", p.skipCode);
  p.serverChunkChars = std::clamp(j.value("serverChunkChars", p.serverChunkChars), 200, 20000);
  return p;
}

Json toJson(const Prefs& p) {
  return Json{{"mode", modeName(p.mode)}, {"localVoice", p.localVoice}, {"rate", p.rate}, {"pitch", p.pitch}, {"skipCode", p.skipCode}, {"serverChunkChars", p.serverChunkChars}};
}

const char* modeName(Mode mode) {
  switch (mode) {
    case Mode::Server: return "server";
    case Mode::Local: return "local";
    default: return "auto";
  }
}

Mode modeFromName(const std::string& name) {
  if (name == "server") return Mode::Server;
  if (name == "local") return Mode::Local;
  return Mode::Auto;
}

Engine choose(const Prefs& prefs, bool serverTtsMissing, bool localAvailable) {
  switch (prefs.mode) {
    case Mode::Server: return Engine::Server;
    case Mode::Local: return localAvailable ? Engine::Local : Engine::None;
    default:
      if (!serverTtsMissing) return Engine::Server;
      return localAvailable ? Engine::Local : Engine::None;
  }
}

std::string speakable(const std::string& markdown, bool skipCode) {
  std::string out;
  bool inFence = false;
  int blankRun = 0;
  for (const std::string& raw : core::split(markdown, '\n')) {
    std::string line = raw;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const std::string trimmed = core::trim(line);
    if (core::startsWith(trimmed, "```") || core::startsWith(trimmed, "~~~")) {
      if (!inFence) {
        inFence = true;
        if (skipCode) out += "Code block omitted.\n";
      } else {
        inFence = false;
        if (!skipCode) out += "\n";
      }
      continue;
    }
    if (inFence) {
      if (!skipCode) out += line + "\n";
      continue;
    }
    if (trimmed.empty()) {
      if (++blankRun == 1 && !out.empty()) out += "\n";
      continue;
    }
    std::string text = core::trim(stripInline(stripBlock(line)));
    if (text.empty()) continue;   // a table separator or a rule: nothing to hear, not a pause
    blankRun = 0;
    out += text + "\n";
  }
  return core::trim(out);
}

std::vector<std::string> chunk(const std::string& text, size_t maxChars) {
  std::vector<std::string> out;
  if (maxChars < 20) maxChars = 20;
  // Sentences: cut after ". " / "! " / "? " and at line ends.
  std::vector<std::string> sentences;
  std::string cur;
  for (size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '\n') {
      if (!core::trim(cur).empty()) sentences.push_back(core::trim(cur));
      cur.clear();
      continue;
    }
    cur += c;
    if (endsSentence(c) && i + 1 < text.size() && (text[i + 1] == ' ' || text[i + 1] == '\n')) {
      sentences.push_back(core::trim(cur));
      cur.clear();
    }
  }
  if (!core::trim(cur).empty()) sentences.push_back(core::trim(cur));
  // An oversize sentence is cut at spaces.
  std::vector<std::string> pieces;
  for (std::string s : sentences) {
    while (s.size() > maxChars) {
      size_t cut = s.rfind(' ', maxChars);
      if (cut == std::string::npos || cut < maxChars / 2) cut = maxChars;
      pieces.push_back(core::trim(s.substr(0, cut)));
      s = core::trim(s.substr(cut));
    }
    if (!s.empty()) pieces.push_back(s);
  }
  // Pack.
  std::string pack;
  for (const std::string& p : pieces) {
    if (!pack.empty() && pack.size() + 1 + p.size() > maxChars) {
      out.push_back(pack);
      pack.clear();
    }
    if (!pack.empty()) pack += ' ';
    pack += p;
  }
  if (!pack.empty()) out.push_back(pack);
  return out;
}

float appleRate(float relative) {
  relative = std::clamp(relative, 0.5f, 2.0f);
  // 0.5 is the natural pace; the top of Apple's scale is unintelligible, so
  // 2x maps to 0.8 rather than 1.0.
  const float rate = relative <= 1.0f ? 0.5f * relative : 0.5f + (relative - 1.0f) * 0.3f;
  return std::clamp(rate, 0.1f, 0.9f);
}

std::string pickVoice(const std::vector<Voice>& voices, const std::string& preferredId, const std::string& language) {
  if (!preferredId.empty()) {
    for (const Voice& v : voices) {
      if (v.id == preferredId) return v.id;
    }
  }
  // Without a preference, an enhanced or premium voice for the language beats
  // the system voice; among the plain ones the system's own choice is best
  // (the alphabetical first is as likely a novelty voice as a real one).
  if (language.empty()) return "";
  const std::string lang = core::lower(language);
  const std::string prefix = lang.substr(0, lang.find_first_of("-_"));
  const Voice* best = nullptr;
  int bestScore = -1;
  for (const Voice& v : voices) {
    if (v.quality < 2) continue;
    const std::string vl = core::lower(v.language);
    int score = -1;
    if (vl == lang) score = 10 + v.quality;
    else if (vl.substr(0, vl.find_first_of("-_")) == prefix) score = v.quality;
    if (score > bestScore) {
      bestScore = score;
      best = &v;
    }
  }
  return best ? best->id : "";
}

std::string voiceLabel(const Voice& v) {
  std::string label = v.name + " (" + v.language;
  if (v.quality == 2) label += ", enhanced";
  else if (v.quality >= 3) label += ", premium";
  return label + ")";
}

bool Queue::add(const std::string& messageId, const std::string& text, size_t chunkChars) {
  if (has(messageId)) return false;
  const std::vector<std::string> pieces = chunk(text, chunkChars);
  if (pieces.empty()) return false;
  for (size_t i = 0; i < pieces.size(); ++i) {
    items.push_back(Item{messageId, pieces[i], static_cast<int>(i + 1), static_cast<int>(pieces.size())});
  }
  return true;
}

bool Queue::next(Item* out) {
  if (items.empty()) {
    active = false;
    current = Item{};
    return false;
  }
  current = items.front();
  items.pop_front();
  active = true;
  if (out) *out = current;
  return true;
}

void Queue::finish() {
  active = false;
  current = Item{};
}

void Queue::remove(const std::string& messageId) {
  items.erase(std::remove_if(items.begin(), items.end(), [&](const Item& i) { return i.messageId == messageId; }), items.end());
  if (active && current.messageId == messageId) finish();
}

void Queue::clear() {
  items.clear();
  finish();
}

bool Queue::has(const std::string& messageId) const {
  if (active && current.messageId == messageId) return true;
  return std::any_of(items.begin(), items.end(), [&](const Item& i) { return i.messageId == messageId; });
}

size_t Queue::messagesPending() const {
  std::set<std::string> ids;
  for (const Item& i : items) {
    if (!(active && i.messageId == current.messageId)) ids.insert(i.messageId);
  }
  return ids.size();
}

std::string Queue::status() const {
  if (!active && items.empty()) return "";
  const size_t more = messagesPending();
  std::string s = "Reading aloud";
  if (active && current.parts > 1) s += " (part " + std::to_string(current.part) + " of " + std::to_string(current.parts) + ")";
  if (more > 0) s += ", " + std::to_string(more) + " more queued";
  return s;
}

}  // namespace tts
