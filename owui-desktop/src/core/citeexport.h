// Cited export (#110): a reply copied with its sources as Markdown footnotes
// or as BibTeX. Sources come from the server's citation array (web search,
// knowledge files) and from local-document citations ("[name p.3]") in the
// text. App-free; --citetest covers it.
#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace citeexport {

using Json = nlohmann::json;

struct Source {
  std::string kind;      // "web" | "file" | "document"
  std::string title;
  std::string url;       // web: the page; file: empty
  std::string snippet;   // first retrieved passage, trimmed
  int page = 0;          // 1-based when known
};

// Open WebUI's `sources`: [{source:{name,url,type}, document:[...], metadata:[{source,page,...}]}, ...].
// Order is kept (the reply cites them as [1], [2], ...); duplicates by url/title collapse.
std::vector<Source> fromServerSources(const Json& sources);
// "[name p.N]" / "[name slide N]" / "[name part N]" citations of locally read documents.
std::vector<Source> fromLocalCitations(const std::string& content, const std::vector<std::string>& docNames);

// The reply with "[n]" turned into "[^n]" (server sources, by index) and local
// citations into "[^k]" after them, and the footnote definitions appended.
std::string markdownWithFootnotes(const std::string& content, const std::vector<Source>& server, const std::vector<Source>& local);
std::string bibtex(const std::vector<Source>& sources, const std::string& accessedDate);
std::string plainList(const std::vector<Source>& sources);

}  // namespace citeexport
