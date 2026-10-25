// Saved answers library (#148): bookmarks across every chat - an answer (or
// question) with a note and tags, searchable and exportable. Pins are per
// chat; this is the global shelf. Stored in support/library.json. App-free;
// --librarytest covers it.
#pragma once

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace library {

using Json = nlohmann::json;

struct Entry {
  std::string id;
  std::string chatId, chatTitle, messageId;
  std::string role, model;
  std::string text;                 // the message as saved
  std::string note;                 // yours
  std::vector<std::string> tags;    // lower-case, unique
  int64_t at = 0;                   // saved (unix seconds)
};

std::vector<Entry> fromJson(const Json& j);
Json toJson(const std::vector<Entry>& entries);

// Adds or, when the same message is already saved, refreshes its text and
// title (note and tags are kept). Returns the entry's id.
std::string add(std::vector<Entry>& entries, Entry entry);
bool remove(std::vector<Entry>& entries, const std::string& id);
const Entry* find(const std::vector<Entry>& entries, const std::string& chatId, const std::string& messageId);
std::vector<std::string> parseTags(const std::string& text);   // "a, B  #c" -> {a, b, c}

// Every word of `query` must appear in the text, note, tags, title or model
// (case-insensitive); "#tag" words match tags only. Newest first among equals.
std::vector<Entry> search(const std::vector<Entry>& entries, const std::string& query);
std::vector<std::pair<std::string, int>> tagCounts(const std::vector<Entry>& entries);   // most used first
std::string snippet(const std::string& text, size_t maxChars);
std::string markdown(const std::vector<Entry>& entries);   // export

}  // namespace library
