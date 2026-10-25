// Playgrounds (#99): run a reply's regex against sample text, its SQL against
// a CSV or SQLite file, its jq against JSON - locally and at once. The pure
// parts: kind detection, the regex and SQL runners (std::regex, in-process
// SQLite), CSV parsing, the jq command line, result formatting and sample
// inputs. App-free; --playgroundtest covers it.
#pragma once

#include <string>
#include <vector>

namespace playground {

enum class Kind { None, Regex, Sql, Jq };
const char* kindName(Kind kind);
// From the block's language tag, or the code itself when untagged.
Kind kindFor(const std::string& language, const std::string& code);

struct Match {
  size_t start = 0, end = 0;
  std::string text;
  std::vector<std::string> groups;   // capture groups 1..n
};
struct RegexResult {
  bool ok = false;
  std::string error;
  std::string flags;                 // the flags understood from "/.../flags"
  std::vector<Match> matches;
  std::string replaced;              // the sample with `replacement` applied, when one was given
};
// `pattern` is bare or "/.../flags" (i = ignore case, m = multiline; every
// match is reported). `replacement` uses $1, $2 ... ; empty = no replace.
RegexResult runRegex(const std::string& pattern, const std::string& sample, const std::string& replacement);

struct Table {
  std::vector<std::string> columns;
  std::vector<std::vector<std::string>> rows;
  bool truncated = false;
};
struct SqlResult {
  bool ok = false;
  std::string error;
  std::vector<Table> tables;         // one per statement that returned rows
  int changes = 0;                   // rows changed by INSERT/UPDATE/DELETE
  double ms = 0.0;
  std::vector<std::string> schema;   // "table (col, col)" lines for the loaded data
};
// Splits CSV (or TSV / semicolons, detected from the header) with quotes.
std::vector<std::vector<std::string>> parseCsv(const std::string& text);
std::string identifier(const std::string& raw);   // a safe SQL identifier
// Runs `sql` in a fresh in-memory database that holds `csv` as a table
// named `csvTable` (numeric columns typed), or read-only against the
// SQLite file at `sqlitePath` when that is given.
SqlResult runSql(const std::string& sql, const std::string& csv, const std::string& csvTable, const std::string& sqlitePath, size_t maxRows = 200);

bool jqAvailable();                                // /usr/bin/jq (macOS 15+) or Homebrew's
std::string jqPath();
std::vector<std::string> jqArgv(const std::string& filter, const std::string& inputPath, bool rawOutput);

std::string tableMarkdown(const Table& table);
std::string tableCsv(const Table& table);
std::string matchesText(const RegexResult& result);
std::string sampleInput(Kind kind);                // something to try the block on
std::string sampleCode(Kind kind);

}  // namespace playground
