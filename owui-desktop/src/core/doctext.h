// Native document text: PDF (PDFKit), Word / PowerPoint / Excel (the zipped
// XML read with zlib), RTF / .doc / .odt (textutil) and plain text, extracted
// on this Mac into pages so an answer can cite "[name p.3]". Nothing is
// uploaded. `render` fits a document into a character budget: whole when it
// fits, else the parts that best match the question, chosen lexically so it
// needs no model and no network. App-free; --doctest covers it.
#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace doctext {

using Json = nlohmann::json;

struct Page {
  int number = 0;
  std::string label;   // "p.3", "slide 2", "sheet Sales", "part 4"
  std::string text;
};

struct Document {
  std::string kind;    // pdf | docx | pptx | xlsx | rtf | text
  std::vector<Page> pages;
  std::string error;
  size_t chars() const;
  bool ok() const { return error.empty() && !pages.empty(); }
};

// "" when the file is not a document this module reads.
std::string kindFor(const std::string& name, const std::string& mime);
bool supported(const std::string& name, const std::string& mime);

// Blocking; PDFKit and textutil are slow on big files - call from a worker.
Document extract(const std::string& name, const std::string& mime, const std::string& bytes);

Json toJson(const Document& doc);
Document fromJson(const Json& j);

// The prompt block for one document. `budgetChars` bounds the whole result.
std::string render(const Document& doc, const std::string& name, size_t budgetChars, const std::string& query);

// ---- pieces, exposed for the self-test ------------------------------------
bool zipEntry(const std::string& zip, const std::string& entryName, std::string* out);
std::vector<std::string> zipNames(const std::string& zip);
// Text of an XML document: the contents of `textTags` elements, a newline at
// each of `breakTags` (open or close tag names, "/w:p" for a close), "\f" at
// Word page breaks, entities decoded.
std::string xmlText(const std::string& xml, const std::vector<std::string>& textTags,
                    const std::vector<std::string>& breakTags);
// Splits free text into pages of about `pageChars` at paragraph boundaries.
std::vector<Page> paginate(const std::string& text, size_t pageChars, const std::string& labelPrefix);

}  // namespace doctext
