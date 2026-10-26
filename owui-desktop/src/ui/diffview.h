// Unified diff parsing, rendering with per-hunk apply/reject, and a small
// line-diff generator for "apply this code block to a file" previews.
#pragma once

#include <string>
#include <vector>

namespace diffview {

struct Line {
  char tag;            // ' ', '+', '-'
  std::string text;
};

struct Hunk {
  int oldStart = 0, oldCount = 0, newStart = 0, newCount = 0;
  std::string header;
  std::vector<Line> lines;
  bool accepted = true;
};

struct FileDiff {
  std::string oldPath;
  std::string newPath;
  std::vector<Hunk> hunks;
};

std::vector<FileDiff> parse(const std::string& unified);
bool looksLikeDiff(const std::string& text);

// Applies the accepted hunks of `diff` to `original`. Context is matched by
// searching near the stated line, then anywhere, so hunks with wrong line
// numbers (the failure mode we measured) still land when their context is
// unambiguous. Returns false when a hunk cannot be placed.
bool apply(const FileDiff& diff, const std::string& original, std::string* result,
           std::string* error);

// Produces a unified diff between two texts (LCS-based), with git-style paths.
std::string generate(const std::string& path, const std::string& before, const std::string& after,
                     int context = 3);

// Renders the diff with coloured lines and accept/reject toggles per hunk.
// `editable` enables the toggles.
void render(const char* id, std::vector<FileDiff>& diffs, bool editable, float width);

}  // namespace diffview
