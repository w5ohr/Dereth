// Vault sync (#127): chats and notes as Markdown files with YAML front matter
// in a folder of the user's choosing - typically inside an Obsidian vault.
// Chats are written one way (the app is their source); notes go both ways
// (a note edited in the vault is sent back to the server, a note changed on
// the server rewrites its file). This module is the pure part: file names,
// the Markdown a chat or note becomes, and reading a note file back.
// App-free; --vaulttest covers it.
#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace vault {

using Json = nlohmann::json;

// A file-system-safe stem from a title: no slashes or colons, trimmed, at
// most 80 characters, "Untitled" when nothing is left.
std::string safeName(const std::string& title);

// Chats: "<title> (<first 8 of id>).md" so renames never collide.
std::string chatFileName(const Json& chatResponse);
// The whole chat as Markdown: front matter (id, title, created, updated,
// models, tags, source) then the current branch, one section per message.
std::string chatMarkdown(const Json& chatResponse, const std::string& serverUrl);

// Notes: "<title>.md"; the id lives in the front matter.
std::string noteFileName(const Json& note);
std::string noteMarkdown(const Json& note);
struct NoteFile {
  bool ok = false;
  std::string id;      // empty for a file the user created in the vault
  std::string title;
  std::string body;
};
NoteFile parseNoteFile(const std::string& text, const std::string& fileName);

// Front matter helpers.
std::string frontMatter(const std::vector<std::pair<std::string, std::string>>& fields);
std::map<std::string, std::string> readFrontMatter(const std::string& text, std::string* body);
std::string yamlQuote(const std::string& value);

}  // namespace vault
