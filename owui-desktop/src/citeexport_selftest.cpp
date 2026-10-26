// `Open WebUI --citetest`: offline checks of the cited export.
#include <cstdio>
#include <string>

#include "core/citeexport.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
}  // namespace

int runCiteSelfTest() {
  using namespace citeexport;
  printf("Open WebUI desktop cited-export self-test\n\n");

  Json sources = Json::array({
      Json{{"source", Json{{"name", "Lighthouse maintenance guide"}, {"url", "https://example.org/guide"}, {"type", "web_search"}}},
           {"document", Json::array({"Grease the winches\nevery spring."})},
           {"metadata", Json::array({Json{{"source", "https://example.org/guide"}}})}},
      Json{{"source", Json{{"name", "report.pdf"}, {"type", "file"}}},
           {"document", Json::array({"The approved budget is 48,200 euros."})},
           {"metadata", Json::array({Json{{"name", "report.pdf"}, {"page", 1}}})}},
      Json{{"source", Json{{"name", "Lighthouse maintenance guide"}, {"url", "https://example.org/guide"}}}},   // duplicate
      "https://example.com/plain",
  });
  std::vector<Source> server = fromServerSources(sources);
  report("fromServerSources: web + file + string, duplicate collapsed, page 0-based -> 1-based, snippet on one line",
         server.size() == 3 && server[0].kind == "web" && server[0].url == "https://example.org/guide" && server[0].snippet == "Grease the winches every spring." &&
             server[1].kind == "file" && server[1].title == "report.pdf" && server[1].page == 2 && server[2].kind == "web" && server[2].title == "https://example.com/plain",
         std::to_string(server.size()));

  const std::string content = "Grease the winches each spring [1]. The budget is 48,200 euros [2], see [lantern-report.docx p.2] and [deck slide 3]. Not a cite [9].";
  std::vector<Source> local = fromLocalCitations(content, {"lantern-report.docx", "lantern-deck.pptx"});
  report("fromLocalCitations: matches by name or stem, keeps page, ignores unknown", local.size() == 2 && local[0].title == "lantern-report.docx" && local[0].page == 2 &&
                                                                                        local[1].title == "lantern-deck.pptx" && local[1].page == 3);
  const std::string md = markdownWithFootnotes(content, server, local);
  report("markdownWithFootnotes: [n] -> [^n] only for existing sources, local cites numbered after, definitions appended",
         md.find("spring [^1].") != std::string::npos && md.find("euros [^2]") != std::string::npos && md.find("see [^4] and [^5]") != std::string::npos &&
             md.find("[9]") != std::string::npos && md.find("[^9]") == std::string::npos &&
             md.find("[^1]: Lighthouse maintenance guide - https://example.org/guide - \"Grease the winches every spring.\"") != std::string::npos &&
             md.find("[^2]: report.pdf, p. 2") != std::string::npos && md.find("[^4]: lantern-report.docx, p. 2 (attached document)") != std::string::npos,
         md.substr(md.find("[^1]:"), 90));
  report("markdownWithFootnotes: no sources leaves the text alone", markdownWithFootnotes("plain [1]", {}, {}) == "plain [1]");

  std::vector<Source> all = server;
  all.insert(all.end(), local.begin(), local.end());
  const std::string bib = bibtex(all, "2026-10-25");
  report("bibtex: @misc entries with url, page notes, accessed date and year",
         bib.find("@misc{lighthousema1,\n  title = {Lighthouse maintenance guide},\n  howpublished = {\\url{https://example.org/guide}},\n  note = {accessed 2026-10-25},\n  year = {2026}\n}") != std::string::npos &&
             bib.find("note = {page 2}") != std::string::npos && bib.find("attached document") != std::string::npos,
         bib.substr(0, 160));
  report("bibtex escapes braces and ampersands", bibtex({Source{"web", "A & B {c}", "https://x", "", 0}}, "").find("title = {A \\& B \\{c\\}}") != std::string::npos);
  report("plainList", plainList(server).find("Sources:\n1. Lighthouse maintenance guide - https://example.org/guide\n2. report.pdf (p. 2)") == 0 && plainList({}).empty());

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
