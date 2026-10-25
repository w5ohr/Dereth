// Smart folders and tags (#147): rules that file chats into folders and tag
// them automatically (by model, title, text, tag and age), plus auto-tagging
// of untagged chats by the small model. The pure parts: rules as JSON,
// matching, the action plan, the tagging prompt and its parser. App-free;
// --foldertest covers it.
#pragma once

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace smartfolders {

using Json = nlohmann::json;

struct Rule {
  std::string id, name;
  std::string folder;           // file into this folder (created when missing); "" = don't move
  std::string tag;              // add this tag; "" = don't tag
  std::string modelContains;    // conditions - all non-empty ones must hold
  std::string titleContains;
  std::string textContains;     // in the first user message
  std::string hasTag;
  int olderThanDays = 0;        // 0 = any age
  int newerThanDays = 0;
  bool enabled = true;
};
std::vector<Rule> rulesFromJson(const Json& j);
Json rulesToJson(const std::vector<Rule>& rules);
bool needsChatBody(const std::vector<Rule>& rules);   // a rule looks at models or text
std::string describe(const Rule& rule);               // "title contains sql, older than 30 days -> folder Archive, tag old"

struct ChatFacts {
  std::string id, title, folderId;
  std::vector<std::string> models, tags;
  int64_t updatedAt = 0;        // unix seconds
  std::string text;             // first user message (when fetched)
};
bool matches(const Rule& rule, const ChatFacts& chat, int64_t now);

struct Action {
  std::string chatId, chatTitle, ruleName;
  std::string folder;           // folder name to file into ("" = none)
  std::string tag;              // tag to add ("" = none)
};
// What the rules would do: skips chats already in the folder / already
// tagged; the first matching rule that moves wins the move, every matching
// rule may add its tag. `folderIds` maps lower-case folder name -> id.
std::vector<Action> plan(const std::vector<Rule>& rules, const std::vector<ChatFacts>& chats, int64_t now, const std::map<std::string, std::string>& folderIds);
std::string summary(const std::vector<Action>& actions);   // "3 chats moved, 2 tags added"

// Auto-tagging by the small model.
std::string tagSystemPrompt();
std::string tagUserPrompt(const std::string& title, const std::string& userText, const std::string& replyText);
std::string normalizeTag(const std::string& raw);          // lower-case, spaces -> hyphens, no '#', <= 24 chars
std::vector<std::string> parseTags(const std::string& reply, size_t max = 3);   // JSON array or comma list

}  // namespace smartfolders
