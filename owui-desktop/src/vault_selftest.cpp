// `Open WebUI --vaulttest`: offline checks of the vault file formats.
#include <cstdio>
#include <string>

#include "core/vault.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
}  // namespace

int runVaultSelfTest() {
  using namespace vault;
  printf("Open WebUI desktop vault-sync self-test\n\n");

  report("safeName strips path and Obsidian-hostile characters, collapses spaces, caps length",
         safeName("  Plan: a/b \"c\" [d] #e  ") == "Plan a b c d e" && safeName("") == "Untitled" && safeName("...") == "Untitled" &&
             safeName(std::string(120, 'x')).size() == 80, safeName("  Plan: a/b \"c\" [d] #e  "));
  report("yamlQuote escapes quotes and newlines", yamlQuote("say \"hi\"\nnow") == "\"say \\\"hi\\\"\\nnow\"");

  Json chat{{"id", "abcdef1234567890"},
            {"title", "Sailing: upwind"},
            {"created_at", 1761350400},
            {"updated_at", 1761354000},
            {"chat", Json{{"models", Json::array({"qwen3:8b"})},
                          {"tags", Json::array({"boats", "howto"})},
                          {"history", Json{{"currentId", "a2"},
                                           {"messages", Json{{"u1", Json{{"id", "u1"}, {"parentId", nullptr}, {"role", "user"}, {"content", "How do I trim?"}, {"timestamp", 1761350400}}},
                                                             {"a1", Json{{"id", "a1"}, {"parentId", "u1"}, {"role", "assistant"}, {"content", "Old branch"}, {"model", "qwen3:8b"}}},
                                                             {"a2", Json{{"id", "a2"}, {"parentId", "u1"}, {"role", "assistant"}, {"content", "Ease the sheet."}, {"modelName", "Qwen 3"}}}}}}}}}};
  report("chatFileName: safe title plus 8 of the id", chatFileName(chat) == "Sailing upwind (abcdef12).md", chatFileName(chat));
  const std::string md = chatMarkdown(chat, "http://localhost:3001");
  std::string body;
  const auto fm = readFrontMatter(md, &body);
  report("chatMarkdown: front matter fields", fm.at("id") == "abcdef1234567890" && fm.at("title") == "Sailing: upwind" && fm.at("models") == "[qwen3:8b]" &&
                                                 fm.at("tags") == "[boats, howto]" && fm.at("type") == "chat" && fm.at("source") == "http://localhost:3001/c/abcdef1234567890" &&
                                                 !fm.at("created").empty());
  report("chatMarkdown: current branch only, in order, with names", body.find("# Sailing: upwind") != std::string::npos && body.find("## You") != std::string::npos &&
                                                                        body.find("## Qwen 3") != std::string::npos && body.find("Ease the sheet.") != std::string::npos &&
                                                                        body.find("Old branch") == std::string::npos && body.find("## You") < body.find("## Qwen 3"));

  Json note{{"id", "n1"}, {"title", "Groceries / week"}, {"updated_at", 1761354000}, {"data", Json{{"content", Json{{"md", "- eggs\n- milk"}}}}}};
  report("noteFileName", noteFileName(note) == "Groceries week.md");
  const std::string nmd = noteMarkdown(note);
  NoteFile parsed = parseNoteFile(nmd, "Groceries week.md");
  report("noteMarkdown <-> parseNoteFile round trip", parsed.ok && parsed.id == "n1" && parsed.title == "Groceries / week" && parsed.body == "- eggs\n- milk\n", parsed.body);
  NoteFile fresh = parseNoteFile("Just some text\n\nSecond paragraph", "My new idea.md");
  report("parseNoteFile: a file without front matter is a new note titled after the file", fresh.ok && fresh.id.empty() && fresh.title == "My new idea" && fresh.body == "Just some text\n\nSecond paragraph");
  NoteFile crlf = parseNoteFile("---\r\nid: n2\r\ntitle: \"Win\"\r\n---\r\nbody here\r\n", "x.md");
  report("parseNoteFile: CRLF front matter", crlf.id == "n2" && crlf.title == "Win" && crlf.body.rfind("body here", 0) == 0);
  std::string b2;
  const auto fm2 = readFrontMatter("---\ntitle: \"with: colon\"\nlist: [a, b]\n---\n\n# Head\n---\nnot front matter\n", &b2);
  report("readFrontMatter: quoted colon, list kept raw, body after the closing rule", fm2.at("title") == "with: colon" && fm2.at("list") == "[a, b]" && b2 == "\n# Head\n---\nnot front matter\n");

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
