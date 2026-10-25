// File editor (#100): the app-free parts of the editor for Apply targets -
// the per-file snapshot history that spans applies, saves and restores,
// language detection from the path, and line / search helpers for the
// editor window. --editortest covers it.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fileeditor {

struct Snapshot {
  int64_t at = 0;        // unix seconds
  std::string label;     // "before apply", "apply", "saved in the editor", "before restore"
  std::string content;
};

// History lives in one JSON file per path under historyDir, named by a hash
// of the path so any path works.
std::string historyFile(const std::string& historyDir, const std::string& path);
std::vector<Snapshot> loadHistory(const std::string& historyDir, const std::string& path);
// Appends a snapshot (skipped when its content equals the newest one's),
// keeping the newest `keep`. Returns false when it could not be written.
bool record(const std::string& historyDir, const std::string& path, const std::string& content, const std::string& label, size_t keep = 30);
std::string describe(const Snapshot& s, int64_t now);   // "apply - 3 lines - 2 min ago"

std::string languageFor(const std::string& path);   // highlight language or "" when unknown
int lineCount(const std::string& text);
int lineOf(const std::string& text, size_t offset);        // 1-based
size_t offsetOfLine(const std::string& text, int line);    // start of the 1-based line (clamped)
// Next match at or after `from` (wraps to the start once); npos when absent.
size_t findNext(const std::string& text, const std::string& needle, size_t from, bool caseless);
// The first line that differs between two texts (1-based), 0 when equal.
int firstDifferingLine(const std::string& a, const std::string& b);

}  // namespace fileeditor
