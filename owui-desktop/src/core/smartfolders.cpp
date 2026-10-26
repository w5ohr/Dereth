#include "smartfolders.h"

#include <algorithm>
#include <set>

#include "common.h"

namespace smartfolders {
namespace {

std::string str(const Json& j, const char* key) {
  if (!j.is_object() || !j.contains(key) || !j[key].is_string()) return "";
  return j[key].get<std::string>();
}

int num(const Json& j, const char* key) {
  if (!j.is_object() || !j.contains(key) || !j[key].is_number_integer()) return 0;
  return std::max(0, j[key].get<int>());
}

}  // namespace

std::vector<Rule> rulesFromJson(const Json& j) {
  std::vector<Rule> out;
  if (!j.is_array()) return out;
  for (const auto& r : j) {
    if (!r.is_object()) continue;
    Rule rule;
    rule.id = str(r, "id");
    rule.name = str(r, "name");
    rule.folder = core::trim(str(r, "folder"));
    rule.tag = normalizeTag(str(r, "tag"));
    rule.modelContains = str(r, "modelContains");
    rule.titleContains = str(r, "titleContains");
    rule.textContains = str(r, "textContains");
    rule.hasTag = normalizeTag(str(r, "hasTag"));
    rule.olderThanDays = num(r, "olderThanDays");
    rule.newerThanDays = num(r, "newerThanDays");
    rule.enabled = r.value("enabled", true);
    if (rule.folder.empty() && rule.tag.empty()) continue;
    if (rule.id.empty()) rule.id = core::uuid();
    if (rule.name.empty()) rule.name = rule.folder.empty() ? "tag " + rule.tag : "file into " + rule.folder;
    out.push_back(rule);
  }
  return out;
}

Json rulesToJson(const std::vector<Rule>& rules) {
  Json out = Json::array();
  for (const Rule& r : rules) {
    out.push_back(Json{{"id", r.id}, {"name", r.name}, {"folder", r.folder}, {"tag", r.tag}, {"modelContains", r.modelContains}, {"titleContains", r.titleContains},
                       {"textContains", r.textContains}, {"hasTag", r.hasTag}, {"olderThanDays", r.olderThanDays}, {"newerThanDays", r.newerThanDays}, {"enabled", r.enabled}});
  }
  return out;
}

bool needsChatBody(const std::vector<Rule>& rules) {
  for (const Rule& r : rules) {
    if (r.enabled && (!r.modelContains.empty() || !r.textContains.empty())) return true;
  }
  return false;
}

std::string describe(const Rule& r) {
  std::vector<std::string> conds;
  if (!r.modelContains.empty()) conds.push_back("model contains \"" + r.modelContains + "\"");
  if (!r.titleContains.empty()) conds.push_back("title contains \"" + r.titleContains + "\"");
  if (!r.textContains.empty()) conds.push_back("first message contains \"" + r.textContains + "\"");
  if (!r.hasTag.empty()) conds.push_back("tagged #" + r.hasTag);
  if (r.olderThanDays > 0) conds.push_back("older than " + std::to_string(r.olderThanDays) + " days");
  if (r.newerThanDays > 0) conds.push_back("newer than " + std::to_string(r.newerThanDays) + " days");
  std::string out = conds.empty() ? "every chat" : core::join(conds, ", ");
  out += " -> ";
  std::vector<std::string> acts;
  if (!r.folder.empty()) acts.push_back("folder " + r.folder);
  if (!r.tag.empty()) acts.push_back("tag #" + r.tag);
  return out + core::join(acts, ", ");
}

bool matches(const Rule& r, const ChatFacts& c, int64_t now) {
  if (!r.enabled) return false;
  if (!r.titleContains.empty() && !core::containsNoCase(c.title, r.titleContains)) return false;
  if (!r.textContains.empty() && !core::containsNoCase(c.text, r.textContains)) return false;
  if (!r.modelContains.empty()) {
    bool any = false;
    for (const std::string& m : c.models) any = any || core::containsNoCase(m, r.modelContains);
    if (!any) return false;
  }
  if (!r.hasTag.empty()) {
    bool any = false;
    const std::string wanted = normalizeTag(r.hasTag);
    for (const std::string& t : c.tags) any = any || normalizeTag(t) == wanted;
    if (!any) return false;
  }
  const int64_t age = c.updatedAt > 0 ? (now - c.updatedAt) / 86400 : 0;
  if (r.olderThanDays > 0 && !(c.updatedAt > 0 && age >= r.olderThanDays)) return false;
  if (r.newerThanDays > 0 && !(c.updatedAt > 0 && age < r.newerThanDays)) return false;
  return true;
}

std::vector<Action> plan(const std::vector<Rule>& rules, const std::vector<ChatFacts>& chats, int64_t now, const std::map<std::string, std::string>& folderIds) {
  std::vector<Action> out;
  for (const ChatFacts& c : chats) {
    bool moved = false;
    std::set<std::string> tagsNow;
    for (const std::string& t : c.tags) tagsNow.insert(normalizeTag(t));
    for (const Rule& r : rules) {
      if (!matches(r, c, now)) continue;
      Action a;
      a.chatId = c.id;
      a.chatTitle = c.title;
      a.ruleName = r.name;
      if (!r.folder.empty() && !moved) {
        auto it = folderIds.find(core::lower(r.folder));
        const bool already = it != folderIds.end() && it->second == c.folderId;
        if (!already) {
          a.folder = r.folder;
          moved = true;
        }
      }
      if (!r.tag.empty() && !tagsNow.count(r.tag)) {
        a.tag = r.tag;
        tagsNow.insert(r.tag);
      }
      if (!a.folder.empty() || !a.tag.empty()) out.push_back(a);
    }
  }
  return out;
}

std::string summary(const std::vector<Action>& actions) {
  int moves = 0, tags = 0;
  std::set<std::string> chats;
  for (const Action& a : actions) {
    if (!a.folder.empty()) ++moves;
    if (!a.tag.empty()) ++tags;
    chats.insert(a.chatId);
  }
  if (actions.empty()) return "Nothing to do - every chat is already where the rules want it";
  std::string out = std::to_string(moves) + " chat" + (moves == 1 ? "" : "s") + " moved, " + std::to_string(tags) + " tag" + (tags == 1 ? "" : "s") + " added";
  return out + " (" + std::to_string(chats.size()) + " chat" + (chats.size() == 1 ? "" : "s") + " touched)";
}

std::string tagSystemPrompt() {
  return "You label chat transcripts. Reply with a JSON array of one to three short topic tags for the conversation - lower-case, one or two words joined by a hyphen, no hashes, "
         "no generic words like \"chat\" or \"question\". Example: [\"python\", \"unit-tests\"]. Reply with the array only. /no_think";
}

std::string tagUserPrompt(const std::string& title, const std::string& userText, const std::string& replyText) {
  std::string out = "Title: " + (title.empty() ? std::string("(none)") : title) + "\n\nUser:\n" + userText.substr(0, 1500);
  if (!replyText.empty()) out += "\n\nAssistant:\n" + replyText.substr(0, 1000);
  return out + "\n\nTags:";
}

std::string normalizeTag(const std::string& raw) {
  std::string out;
  for (unsigned char c : core::lower(core::trim(raw))) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '+') out += static_cast<char>(c);
    else if (c == ' ' || c == '/' || c == ':') {
      if (!out.empty() && out.back() != '-') out += '-';
    }
  }
  while (!out.empty() && (out.back() == '-' || out.back() == '.')) out.pop_back();
  while (!out.empty() && out.front() == '-') out.erase(0, 1);
  if (out.size() > 24) out = out.substr(0, 24);
  return out;
}

std::vector<std::string> parseTags(const std::string& replyIn, size_t max) {
  std::string reply = replyIn;
  size_t think;
  while ((think = reply.find("<think>")) != std::string::npos) {
    const size_t end = reply.find("</think>", think);
    reply.erase(think, end == std::string::npos ? std::string::npos : end + 8 - think);
  }
  std::vector<std::string> out;
  auto push = [&](const std::string& raw) {
    const std::string t = normalizeTag(raw);
    if (t.empty() || t == "chat" || t == "question" || t == "conversation" || t == "general") return;
    if (std::find(out.begin(), out.end(), t) == out.end() && out.size() < max) out.push_back(t);
  };
  const size_t open = reply.find('[');
  const size_t close = open == std::string::npos ? std::string::npos : reply.find(']', open);
  if (open != std::string::npos && close != std::string::npos) {
    Json arr = Json::parse(reply.substr(open, close - open + 1), nullptr, false);
    if (arr.is_array()) {
      for (const auto& t : arr) {
        if (t.is_string()) push(t.get<std::string>());
      }
      if (!out.empty()) return out;
    }
  }
  std::string line = core::trim(reply);
  const size_t nl = line.find('\n');
  if (nl != std::string::npos) line = line.substr(0, nl);
  if (core::lower(line).rfind("tags", 0) == 0) {   // "Tags: a, b"
    const size_t colon = line.find(':');
    if (colon != std::string::npos) line = line.substr(colon + 1);
  }
  for (const std::string& part : core::split(line, ',')) push(part);
  return out;
}

}  // namespace smartfolders
