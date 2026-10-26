// `Open WebUI --playgroundtest`: offline checks of the playgrounds.
#include <sqlite3.h>

#include <cstdio>
#include <string>

#include "core/common.h"
#include "core/playground.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
}  // namespace

int runPlaygroundSelfTest() {
  using namespace playground;
  printf("Open WebUI desktop playground self-test\n\n");

  report("kindFor: language tags", kindFor("regex", "") == Kind::Regex && kindFor("SQL", "") == Kind::Sql && kindFor("sqlite", "") == Kind::Sql && kindFor("jq", "") == Kind::Jq &&
                                       kindFor("python", "SELECT 1") == Kind::None);
  report("kindFor: untagged code by shape", kindFor("", "select * from t") == Kind::Sql && kindFor("", "WITH x AS (SELECT 1) SELECT * FROM x") == Kind::Sql && kindFor("", "/ab+c/i") == Kind::Regex &&
                                                kindFor("", ".orders[] | .id") == Kind::Jq && kindFor("", "map(.x)") == Kind::Jq && kindFor("", "hello world") == Kind::None &&
                                                kindFor("", "/not a regex\nline/") == Kind::None);

  const RegexResult r = runRegex("/(\\d{4})-(\\d{2})-(\\d{2})/g", sampleInput(Kind::Regex), "$3/$2/$1");
  report("runRegex: slash form with flags, every match, groups, positions, replacement", r.ok && r.flags == "g" && r.matches.size() == 3 && r.matches[0].text == "2026-03-14" && r.matches[0].groups.size() == 3 &&
                                                                                           r.matches[0].groups[2] == "14" && r.matches[0].start == 22 && r.matches[0].end == 32 && r.replaced.find("14/03/2026") != std::string::npos,
         std::to_string(r.matches.size()) + " matches, flags '" + r.flags + "'");
  report("runRegex: bare pattern, icase flag, no matches is still ok", runRegex("Order", "order Order", "").matches.size() == 1 && runRegex("/ORDER/i", "order Order", "").matches.size() == 2 &&
                                                                          runRegex("zzz", "abc", "").ok && runRegex("zzz", "abc", "").matches.empty());
  const RegexResult bad = runRegex("(unclosed", "x", "");
  report("runRegex: invalid pattern reports, empty pattern reports, empty match does not loop", !bad.ok && bad.error.find("Invalid pattern") == 0 && !runRegex("", "x", "").ok && runRegex("a*", "baaa", "").ok);

  const auto rows = parseCsv("name,\"city, big\",n\r\n\"Anna \"\"A\"\"\",Oslo,1\nBo,\"multi\nline\",2\n");
  report("parseCsv: quotes, embedded separators and newlines, CRLF", rows.size() == 3 && rows[0][1] == "city, big" && rows[1][0] == "Anna \"A\"" && rows[2][1] == "multi\nline" && rows[2][2] == "2");
  report("parseCsv: tab and semicolon separators detected from the header", parseCsv("a\tb\n1\t2")[1][1] == "2" && parseCsv("a;b\n1;2")[0].size() == 2 && parseCsv("").empty());
  report("identifier", identifier("Total Amount (EUR)") == "total_amount_eur" && identifier("2024") == "c2024" && identifier("") == "col" && identifier("a-b.c") == "a_b_c");

  const SqlResult s = runSql(sampleCode(Kind::Sql), sampleInput(Kind::Sql), "data", "");
  report("runSql: CSV becomes a typed table, aggregates work, rows ordered", s.ok && s.tables.size() == 1 && s.tables[0].columns.size() == 3 && s.tables[0].rows.size() == 3 && s.tables[0].rows[0][0] == "Oslo" &&
                                                                                 s.tables[0].rows[0][2] == "210.5" && s.schema.size() == 1 && s.schema[0].find("amount real") != std::string::npos &&
                                                                                 s.schema[0].find("id integer") != std::string::npos && s.schema[0].find("name text") != std::string::npos,
         s.ok ? s.schema[0] : s.error);
  const SqlResult num = runSql("SELECT name FROM data WHERE amount > 100 ORDER BY name; SELECT COUNT(*) FROM data WHERE amount IS NULL", sampleInput(Kind::Sql), "data", "");
  report("runSql: numeric comparisons, NULL for empty cells, several statements", num.ok && num.tables.size() == 2 && num.tables[0].rows.size() == 2 && num.tables[0].rows[0][0] == "Anna" && num.tables[1].rows[0][0] == "1");
  const SqlResult err = runSql("SELECT nope FROM data", sampleInput(Kind::Sql), "data", "");
  const SqlResult lim = runSql("SELECT id FROM data", sampleInput(Kind::Sql), "data", "", 2);
  report("runSql: errors reported, maxRows truncates, changes counted", !err.ok && err.error.find("no such column") != std::string::npos && lim.ok && lim.tables[0].rows.size() == 2 && lim.tables[0].truncated &&
                                                                            runSql("UPDATE data SET paid='no'", sampleInput(Kind::Sql), "data", "").changes == 5);
  const std::string dbPath = core::supportDir() + "/playground-selftest.sqlite";
  remove(dbPath.c_str());
  {
    sqlite3* db = nullptr;
    sqlite3_open(dbPath.c_str(), &db);
    sqlite3_exec(db, "CREATE TABLE pets (name TEXT, legs INTEGER); INSERT INTO pets VALUES ('cat', 4), ('hen', 2)", nullptr, nullptr, nullptr);
    sqlite3_close(db);
  }
  const SqlResult file = runSql("SELECT name FROM pets WHERE legs = 2", "", "", dbPath);
  const SqlResult ro = runSql("DELETE FROM pets", "", "", dbPath);
  report("runSql: a SQLite file is queried read-only and its schema listed", file.ok && file.tables[0].rows.size() == 1 && file.tables[0].rows[0][0] == "hen" && file.schema.size() == 1 && file.schema[0].find("pets") == 0 &&
                                                                                 !ro.ok && ro.error.find("readonly") != std::string::npos,
         ro.error);
  remove(dbPath.c_str());

  const std::vector<std::string> argv = jqArgv(".a", "/tmp/in.json", true);
  report("jqArgv and jqAvailable", argv.size() == 7 && argv[4] == "-r" && argv[5] == ".a" && argv[6] == "/tmp/in.json" && jqArgv(".a", "x", false).size() == 6 && (jqAvailable() == !jqPath().empty()));
  Table t;
  t.columns = {"a", "b|c"};
  t.rows = {{"1", "x,y"}, {"2", "q\"r"}};
  report("tableMarkdown / tableCsv", tableMarkdown(t) == "| a | b|c |\n| --- | --- |\n| 1 | x,y |\n| 2 | q\"r |\n" && tableCsv(t) == "a,b|c\n1,\"x,y\"\n2,\"q\"\"r\"\n" && tableMarkdown(Table{}).empty());
  report("matchesText and samples", matchesText(r).find("3 matches\n1. [22-32] 2026-03-14  $1=2026") == 0 && !sampleInput(Kind::Jq).empty() && !sampleCode(Kind::Regex).empty() && sampleInput(Kind::None).empty());

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
