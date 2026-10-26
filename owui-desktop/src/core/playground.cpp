#include "playground.h"

#include <sqlite3.h>
#include <sys/stat.h>

#include <algorithm>
#include <chrono>
#include <regex>

#include "common.h"

namespace playground {
namespace {

bool looksNumeric(const std::string& s) {
  if (s.empty()) return false;
  char* end = nullptr;
  strtod(s.c_str(), &end);
  return end && *end == '\0';
}

bool isInteger(const std::string& s) {
  if (s.empty()) return false;
  size_t i = (s[0] == '-' || s[0] == '+') ? 1 : 0;
  if (i >= s.size()) return false;
  for (; i < s.size(); ++i) {
    if (!isdigit(static_cast<unsigned char>(s[i]))) return false;
  }
  return true;
}

bool exists(const std::string& path) {
  struct stat st{};
  return stat(path.c_str(), &st) == 0;
}

}  // namespace

const char* kindName(Kind kind) {
  switch (kind) {
    case Kind::Regex: return "regex";
    case Kind::Sql: return "SQL";
    case Kind::Jq: return "jq";
    default: return "";
  }
}

Kind kindFor(const std::string& language, const std::string& code) {
  const std::string lang = core::lower(core::trim(language));
  if (lang == "regex" || lang == "regexp" || lang == "re" || lang == "pcre") return Kind::Regex;
  if (lang == "sql" || lang == "sqlite" || lang == "mysql" || lang == "postgresql" || lang == "postgres" || lang == "psql") return Kind::Sql;
  if (lang == "jq") return Kind::Jq;
  if (!lang.empty()) return Kind::None;
  const std::string t = core::trim(code);
  if (t.empty()) return Kind::None;
  const std::string upper = core::lower(t.substr(0, std::min<size_t>(t.size(), 8)));
  if (core::startsWith(upper, "select ") || core::startsWith(upper, "with ") || core::startsWith(upper, "insert ") || core::startsWith(upper, "update ") ||
      core::startsWith(upper, "delete ") || core::startsWith(upper, "create ")) {
    return Kind::Sql;
  }
  if (t.size() >= 2 && t[0] == '/' && t.find('\n') == std::string::npos) {
    const size_t last = t.rfind('/');
    if (last > 0) {
      bool flagsOnly = true;
      for (size_t i = last + 1; i < t.size(); ++i) flagsOnly = flagsOnly && (t[i] == 'i' || t[i] == 'g' || t[i] == 'm' || t[i] == 's' || t[i] == 'u' || t[i] == 'x');
      if (flagsOnly) return Kind::Regex;
    }
  }
  if (t.find('\n') == std::string::npos && (t[0] == '.' || core::startsWith(t, "[.") || core::startsWith(t, "map(") || core::startsWith(t, "select("))) return Kind::Jq;
  return Kind::None;
}

RegexResult runRegex(const std::string& patternIn, const std::string& sample, const std::string& replacement) {
  RegexResult r;
  std::string pattern = core::trim(patternIn);
  if (pattern.size() >= 2 && pattern[0] == '/') {
    const size_t last = pattern.rfind('/');
    if (last > 0) {
      const std::string flags = pattern.substr(last + 1);
      bool flagsOnly = true;
      for (char c : flags) flagsOnly = flagsOnly && isalpha(static_cast<unsigned char>(c));
      if (flagsOnly) {
        r.flags = flags;
        pattern = pattern.substr(1, last - 1);
      }
    }
  }
  if (pattern.empty()) {
    r.error = "Empty pattern";
    return r;
  }
  auto flags = std::regex::ECMAScript;
  if (r.flags.find('i') != std::string::npos) flags |= std::regex::icase;
  if (r.flags.find('m') != std::string::npos) flags |= std::regex::multiline;
  try {
    const std::regex re(pattern, flags);
    for (auto it = std::sregex_iterator(sample.begin(), sample.end(), re); it != std::sregex_iterator(); ++it) {
      const std::smatch& m = *it;
      Match match;
      match.start = static_cast<size_t>(m.position(0));
      match.end = match.start + static_cast<size_t>(m.length(0));
      match.text = m[0].str();
      for (size_t g = 1; g < m.size(); ++g) match.groups.push_back(m[g].matched ? m[g].str() : "");
      r.matches.push_back(match);
      if (r.matches.size() >= 5000) break;
      if (m.length(0) == 0 && match.end >= sample.size()) break;   // an empty match at the end would loop
    }
    if (!replacement.empty()) r.replaced = std::regex_replace(sample, re, replacement);
    r.ok = true;
  } catch (const std::regex_error& e) {
    r.error = std::string("Invalid pattern: ") + e.what();
    if (pattern.find("(?<") != std::string::npos || pattern.find("(?<=") != std::string::npos) r.error += " (lookbehind and named groups are not supported by the ECMAScript engine)";
  }
  return r;
}

std::vector<std::vector<std::string>> parseCsv(const std::string& text) {
  std::vector<std::vector<std::string>> rows;
  if (text.empty()) return rows;
  const std::string header = text.substr(0, text.find('\n'));
  char sep = ',';
  if (header.find('\t') != std::string::npos) sep = '\t';
  else if (header.find(';') != std::string::npos && header.find(',') == std::string::npos) sep = ';';
  std::vector<std::string> row;
  std::string cell;
  bool quoted = false;
  for (size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (quoted) {
      if (c == '"') {
        if (i + 1 < text.size() && text[i + 1] == '"') {
          cell += '"';
          ++i;
        } else {
          quoted = false;
        }
      } else {
        cell += c;
      }
      continue;
    }
    if (c == '"') {
      quoted = true;
    } else if (c == sep) {
      row.push_back(cell);
      cell.clear();
    } else if (c == '\n') {
      if (!cell.empty() && cell.back() == '\r') cell.pop_back();
      row.push_back(cell);
      cell.clear();
      if (!(row.size() == 1 && row[0].empty())) rows.push_back(row);
      row.clear();
    } else {
      cell += c;
    }
  }
  if (!cell.empty() || !row.empty()) {
    if (!cell.empty() && cell.back() == '\r') cell.pop_back();
    row.push_back(cell);
    if (!(row.size() == 1 && row[0].empty())) rows.push_back(row);
  }
  return rows;
}

std::string identifier(const std::string& raw) {
  std::string out;
  for (unsigned char c : core::trim(raw)) {
    if (isalnum(c)) out += static_cast<char>(tolower(c));
    else if (c == '_' || c == ' ' || c == '-' || c == '.') out += '_';
  }
  while (!out.empty() && out.back() == '_') out.pop_back();
  if (out.empty()) out = "col";
  if (isdigit(static_cast<unsigned char>(out[0]))) out = "c" + out;
  return out;
}

SqlResult runSql(const std::string& sql, const std::string& csv, const std::string& csvTable, const std::string& sqlitePath, size_t maxRows) {
  SqlResult result;
  const auto started = std::chrono::steady_clock::now();
  sqlite3* db = nullptr;
  int rc = sqlitePath.empty() ? sqlite3_open(":memory:", &db) : sqlite3_open_v2(sqlitePath.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
  if (rc != SQLITE_OK) {
    result.error = std::string("Could not open the database: ") + (db ? sqlite3_errmsg(db) : "unknown error");
    if (db) sqlite3_close(db);
    return result;
  }
  auto fail = [&](const std::string& msg) {
    result.error = msg;
    sqlite3_close(db);
    return result;
  };
  if (!csv.empty()) {
    const std::vector<std::vector<std::string>> rows = parseCsv(csv);
    if (rows.empty()) return fail("The CSV is empty");
    const std::string table = identifier(csvTable.empty() ? "data" : csvTable);
    std::vector<std::string> cols;
    for (const std::string& h : rows[0]) {
      std::string name = identifier(h);
      int n = 2;
      while (std::find(cols.begin(), cols.end(), name) != cols.end()) name = identifier(h) + "_" + std::to_string(n++);
      cols.push_back(name);
    }
    std::vector<std::string> types(cols.size(), "INTEGER");
    for (size_t c = 0; c < cols.size(); ++c) {
      bool any = false;
      for (size_t r = 1; r < rows.size(); ++r) {
        if (c >= rows[r].size() || rows[r][c].empty()) continue;
        any = true;
        if (types[c] == "INTEGER" && !isInteger(rows[r][c])) types[c] = "REAL";
        if (types[c] == "REAL" && !looksNumeric(rows[r][c])) {
          types[c] = "TEXT";
          break;
        }
      }
      if (!any) types[c] = "TEXT";
    }
    std::string create = "CREATE TABLE \"" + table + "\" (";
    std::string schemaLine = table + " (";
    for (size_t c = 0; c < cols.size(); ++c) {
      create += (c ? ", " : "") + ("\"" + cols[c] + "\" " + types[c]);
      schemaLine += (c ? ", " : "") + cols[c] + " " + core::lower(types[c]);
    }
    create += ")";
    result.schema.push_back(schemaLine + ")");
    char* err = nullptr;
    if (sqlite3_exec(db, create.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
      const std::string msg = err ? err : "create failed";
      sqlite3_free(err);
      return fail(msg);
    }
    std::string insert = "INSERT INTO \"" + table + "\" VALUES (";
    for (size_t c = 0; c < cols.size(); ++c) insert += (c ? ",?" : "?");
    insert += ")";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, insert.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return fail(sqlite3_errmsg(db));
    sqlite3_exec(db, "BEGIN", nullptr, nullptr, nullptr);
    for (size_t r = 1; r < rows.size(); ++r) {
      for (size_t c = 0; c < cols.size(); ++c) {
        const std::string v = c < rows[r].size() ? rows[r][c] : "";
        if (v.empty()) sqlite3_bind_null(stmt, static_cast<int>(c + 1));
        else if (types[c] == "INTEGER") sqlite3_bind_int64(stmt, static_cast<int>(c + 1), strtoll(v.c_str(), nullptr, 10));
        else if (types[c] == "REAL") sqlite3_bind_double(stmt, static_cast<int>(c + 1), strtod(v.c_str(), nullptr));
        else sqlite3_bind_text(stmt, static_cast<int>(c + 1), v.c_str(), -1, SQLITE_TRANSIENT);
      }
      sqlite3_step(stmt);
      sqlite3_reset(stmt);
    }
    sqlite3_finalize(stmt);
    sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);
  } else if (!sqlitePath.empty()) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT name, sql FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' ORDER BY name", -1, &stmt, nullptr) == SQLITE_OK) {
      while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const char* ddl = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        std::string line = name ? name : "";
        if (ddl) {
          const std::string d = ddl;
          const size_t open = d.find('(');
          if (open != std::string::npos) line += " " + core::replaceAll(core::replaceAll(d.substr(open), "\n", " "), "  ", " ");
        }
        result.schema.push_back(line);
      }
      sqlite3_finalize(stmt);
    }
  }

  // Run every statement.
  const char* tail = sql.c_str();
  while (tail && *tail) {
    sqlite3_stmt* stmt = nullptr;
    const char* next = nullptr;
    if (sqlite3_prepare_v2(db, tail, -1, &stmt, &next) != SQLITE_OK) {
      const std::string msg = sqlite3_errmsg(db);
      return fail(msg);
    }
    if (!stmt) {   // only whitespace or a comment left
      tail = next;
      continue;
    }
    const int cols = sqlite3_column_count(stmt);
    Table table;
    for (int c = 0; c < cols; ++c) {
      const char* n = sqlite3_column_name(stmt, c);
      table.columns.push_back(n ? n : "");
    }
    int rc2;
    while ((rc2 = sqlite3_step(stmt)) == SQLITE_ROW) {
      if (table.rows.size() >= maxRows) {
        table.truncated = true;
        continue;   // keep stepping so changes are applied for data-modifying statements with RETURNING
      }
      std::vector<std::string> row;
      for (int c = 0; c < cols; ++c) {
        const unsigned char* v = sqlite3_column_text(stmt, c);
        row.push_back(v ? reinterpret_cast<const char*>(v) : "NULL");
      }
      table.rows.push_back(row);
    }
    if (rc2 != SQLITE_DONE) {
      const std::string msg = sqlite3_errmsg(db);
      sqlite3_finalize(stmt);
      return fail(msg);
    }
    sqlite3_finalize(stmt);
    if (cols > 0) result.tables.push_back(table);
    else result.changes += sqlite3_changes(db);
    tail = next;
  }
  result.ok = true;
  result.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
  sqlite3_close(db);
  return result;
}

std::string jqPath() {
  for (const char* p : {"/usr/bin/jq", "/opt/homebrew/bin/jq", "/usr/local/bin/jq"}) {
    if (exists(p)) return p;
  }
  return "";
}

bool jqAvailable() { return !jqPath().empty(); }

std::vector<std::string> jqArgv(const std::string& filter, const std::string& inputPath, bool rawOutput) {
  std::vector<std::string> argv{jqPath().empty() ? "jq" : jqPath(), "-M", "--indent", "2"};
  if (rawOutput) argv.push_back("-r");
  argv.push_back(filter);
  argv.push_back(inputPath);
  return argv;
}

std::string tableMarkdown(const Table& t) {
  if (t.columns.empty()) return "";
  std::string out = "|";
  for (const std::string& c : t.columns) out += " " + c + " |";
  out += "\n|";
  for (size_t i = 0; i < t.columns.size(); ++i) out += " --- |";
  for (const auto& row : t.rows) {
    out += "\n|";
    for (size_t i = 0; i < t.columns.size(); ++i) out += " " + (i < row.size() ? core::replaceAll(row[i], "|", "\\|") : "") + " |";
  }
  return out + "\n";
}

std::string tableCsv(const Table& t) {
  auto cell = [](const std::string& s) {
    if (s.find_first_of(",\"\n") == std::string::npos) return s;
    return "\"" + core::replaceAll(s, "\"", "\"\"") + "\"";
  };
  std::string out;
  for (size_t i = 0; i < t.columns.size(); ++i) out += (i ? "," : "") + cell(t.columns[i]);
  out += "\n";
  for (const auto& row : t.rows) {
    for (size_t i = 0; i < t.columns.size(); ++i) out += (i ? "," : "") + cell(i < row.size() ? row[i] : "");
    out += "\n";
  }
  return out;
}

std::string matchesText(const RegexResult& r) {
  if (!r.ok) return r.error;
  std::string out = std::to_string(r.matches.size()) + " match" + (r.matches.size() == 1 ? "" : "es") + "\n";
  int n = 1;
  for (const Match& m : r.matches) {
    out += std::to_string(n++) + ". [" + std::to_string(m.start) + "-" + std::to_string(m.end) + "] " + m.text;
    for (size_t g = 0; g < m.groups.size(); ++g) out += "  $" + std::to_string(g + 1) + "=" + m.groups[g];
    out += "\n";
  }
  return out;
}

std::string sampleInput(Kind kind) {
  switch (kind) {
    case Kind::Regex:
      return "Order 1042 shipped on 2026-03-14 to anna.lind@example.org (+46 70 123 45 67).\n"
             "Order 1043 is pending since 2026-04-02; contact bo@example.com.\n"
             "Refund REF-77 issued 2026-04-09, EUR 149.90.";
    case Kind::Sql:
      return "id,name,city,amount,paid\n1,Anna,Stockholm,149.90,yes\n2,Bo,Oslo,80,no\n3,Cleo,Oslo,210.5,yes\n4,Dev,Helsinki,35,yes\n5,Eli,Stockholm,,no";
    case Kind::Jq:
      return "{\n  \"orders\": [\n    {\"id\": 1042, \"customer\": \"Anna\", \"total\": 149.9, \"items\": [\"lamp\", \"cable\"]},\n    {\"id\": 1043, \"customer\": \"Bo\", \"total\": 80, \"items\": [\"desk\"]},\n"
             "    {\"id\": 1044, \"customer\": \"Cleo\", \"total\": 210.5, \"items\": []}\n  ]\n}";
    default: return "";
  }
}

std::string sampleCode(Kind kind) {
  switch (kind) {
    case Kind::Regex: return "/(\\d{4})-(\\d{2})-(\\d{2})/g";
    case Kind::Sql: return "SELECT city, COUNT(*) AS customers, SUM(amount) AS total\nFROM data\nWHERE paid = 'yes'\nGROUP BY city\nORDER BY total DESC";
    case Kind::Jq: return ".orders[] | select(.total > 100) | {id, customer}";
    default: return "";
  }
}

}  // namespace playground
