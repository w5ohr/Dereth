#include "memoryprop.h"

#include <algorithm>
#include <cctype>
#include <set>

#include <nlohmann/json.hpp>

#include "common.h"

namespace memoryprop {
namespace {

using Json = nlohmann::json;

std::set<std::string> tokens(const std::string& normalized) {
  std::set<std::string> out;
  for (const std::string& t : core::split(normalized, ' ')) {
    if (t.size() >= 3) out.insert(t);
  }
  return out;
}

std::string clean(std::string s) {
  s = core::trim(s);
  // Bullet or numbering the model may have added.
  while (!s.empty() && (s[0] == '-' || s[0] == '*' || s[0] == ' ')) s.erase(0, 1);
  if (s.size() > 2 && isdigit(static_cast<unsigned char>(s[0])) && (s[1] == '.' || s[1] == ')')) s = core::trim(s.substr(2));
  if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
  return core::trim(s);
}

}  // namespace

std::string systemPrompt() {
  return "You pick out durable facts about the USER from one turn of a conversation: their preferences, background, "
         "work, projects, tools, people, constraints and decisions. Use only what the user stated or clearly implied, "
         "never what the assistant said or guessed. Write each fact as one short sentence in the user's own voice "
         "(\"I prefer ...\", \"My team uses ...\", \"I am working on ...\"). Skip anything temporary, trivial, or already "
         "in the existing memories. Answer with JSON only: {\"memories\": [\"...\", \"...\"]} with 0 to 3 items, and "
         "{\"memories\": []} when there is nothing worth keeping.";
}

std::string userPrompt(const std::string& userText, const std::string& assistantText, const std::vector<std::string>& existing) {
  std::string out = "User said:\n" + userText.substr(0, 3000) + "\n\nAssistant replied:\n" + assistantText.substr(0, 1500) + "\n\n";
  if (existing.empty()) {
    out += "Existing memories: none.\n";
  } else {
    out += "Existing memories (do not repeat these):\n";
    for (size_t i = 0; i < existing.size() && i < 40; ++i) out += "- " + existing[i].substr(0, 200) + "\n";
  }
  return out;
}

std::vector<std::string> parse(const std::string& replyIn) {
  std::vector<std::string> out;
  std::string reply = replyIn;
  const size_t think = reply.rfind("</think>");
  if (think != std::string::npos) reply = reply.substr(think + 8);
  reply = core::trim(reply);
  if (core::startsWith(reply, "```")) {
    const size_t nl = reply.find('\n');
    const size_t end = reply.rfind("```");
    if (nl != std::string::npos && end != std::string::npos && end > nl) reply = core::trim(reply.substr(nl + 1, end - nl - 1));
  }
  // JSON object or array somewhere in the text.
  const size_t brace = reply.find('{');
  const size_t bracket = reply.find('[');
  size_t start = std::string::npos;
  if (brace != std::string::npos && (bracket == std::string::npos || brace < bracket)) start = brace;
  else if (bracket != std::string::npos) start = bracket;
  if (start != std::string::npos) {
    const char open = reply[start];
    const size_t end = reply.rfind(open == '{' ? '}' : ']');
    if (end != std::string::npos && end > start) {
      Json j = Json::parse(reply.substr(start, end - start + 1), nullptr, false);
      const Json* list = nullptr;
      if (j.is_array()) list = &j;
      else if (j.is_object() && j.contains("memories") && j["memories"].is_array()) list = &j["memories"];
      if (list) {
        for (const auto& item : *list) {
          if (item.is_string()) out.push_back(clean(item.get<std::string>()));
          else if (item.is_object()) {
            for (const char* key : {"memory", "text", "content", "fact"}) {
              if (item.contains(key) && item[key].is_string()) {
                out.push_back(clean(item[key].get<std::string>()));
                break;
              }
            }
          }
        }
        return out;
      }
    }
  }
  // Bullets as a last resort.
  for (const std::string& line : core::split(reply, '\n')) {
    const std::string t = core::trim(line);
    if (core::startsWith(t, "- ") || core::startsWith(t, "* ")) out.push_back(clean(t));
  }
  return out;
}

std::string normalize(const std::string& text) {
  static const std::set<std::string> kFiller = {"i", "my", "me", "the", "a", "an", "am", "is", "are", "to", "of", "and", "that",
                                                "this", "in", "on", "for", "with", "user", "prefers", "prefer", "likes", "like"};
  std::string out, word;
  auto push = [&] {
    if (!word.empty() && !kFiller.count(word)) out += (out.empty() ? "" : " ") + word;
    word.clear();
  };
  for (unsigned char c : text) {
    if (isalnum(c) || c >= 0x80) word += static_cast<char>(tolower(c));
    else push();
  }
  push();
  return out;
}

bool similar(const std::string& a, const std::string& b) {
  const std::string na = normalize(a), nb = normalize(b);
  if (na.empty() || nb.empty()) return false;
  if (na == nb) return true;
  if (na.size() >= 12 && nb.find(na) != std::string::npos) return true;
  if (nb.size() >= 12 && na.find(nb) != std::string::npos) return true;
  const std::set<std::string> ta = tokens(na), tb = tokens(nb);
  if (ta.empty() || tb.empty()) return false;
  size_t common = 0;
  for (const std::string& t : ta) common += tb.count(t);
  const size_t smaller = std::min(ta.size(), tb.size());
  return smaller >= 2 && static_cast<double>(common) / static_cast<double>(smaller) >= 0.75;
}

std::vector<std::string> filter(const std::vector<std::string>& candidates, const std::vector<std::string>& existing,
                                const std::vector<std::string>& skipped, size_t maxCount) {
  std::vector<std::string> out;
  for (const std::string& raw : candidates) {
    const std::string c = clean(raw);
    if (c.size() < 8 || c.size() > 240) continue;
    bool dup = false;
    for (const std::string& e : existing) dup = dup || similar(c, e);
    for (const std::string& s : skipped) dup = dup || similar(c, s);
    for (const std::string& o : out) dup = dup || similar(c, o);
    if (dup) continue;
    out.push_back(c);
    if (out.size() >= maxCount) break;
  }
  return out;
}

}  // namespace memoryprop
