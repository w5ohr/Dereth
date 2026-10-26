// `Open WebUI --selectiontest`: offline checks of range selection, splicing and export.
#include <cstdio>
#include <string>

#include "core/msgselect.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
msgselect::Graph chain() {
  // u1 -> a1 -> u2 -> a2 (current), with a dead sibling a1b under u1.
  msgselect::Graph g;
  g["u1"] = {"u1", "", "user", "", "first question", {"a1", "a1b"}};
  g["a1"] = {"a1", "u1", "assistant", "Qwen", "first answer", {"u2"}};
  g["a1b"] = {"a1b", "u1", "assistant", "Qwen", "other answer", {}};
  g["u2"] = {"u2", "a1", "user", "", "second question", {"a2"}};
  g["a2"] = {"a2", "u2", "assistant", "Qwen", "second answer", {}};
  return g;
}
}  // namespace

int runSelectionSelfTest() {
  using namespace msgselect;
  printf("Open WebUI desktop message-selection self-test\n\n");

  const std::vector<std::string> path = {"u1", "a1", "u2", "a2"};
  report("rangeBetween: forward, backward, no anchor, unknown target",
         rangeBetween(path, "a1", "a2") == std::vector<std::string>{"a1", "u2", "a2"} && rangeBetween(path, "a2", "a1") == std::vector<std::string>{"a1", "u2", "a2"} &&
             rangeBetween(path, "", "u2") == std::vector<std::string>{"u2"} && rangeBetween(path, "u1", "zz").empty());

  Graph g = chain();
  std::string current = "a2";
  report("spliceOut middle: child re-attaches to the parent in place, chain survives",
         spliceOut(&g, "a1", &current) && g["u1"].children == std::vector<std::string>{"u2", "a1b"} && g["u2"].parentId == "u1" && current == "a2" && !g.count("a1"));
  g = chain();
  current = "a2";
  report("spliceOut the current leaf: current moves to the parent", spliceOut(&g, "a2", &current) && current == "u2" && g["u2"].children.empty());
  g = chain();
  current = "a1";
  report("spliceOut a current node with children: current moves to its last child", spliceOut(&g, "a1", &current) && current == "u2");
  g = chain();
  current = "a2";
  report("spliceOut a root: children become roots", spliceOut(&g, "u1", &current) && g["a1"].parentId.empty() && g["a1b"].parentId.empty() && current == "a2");
  report("spliceOut unknown id", !spliceOut(&g, "nope", &current));

  g = chain();
  const std::string md = markdown(g, {"u1", "a1"});
  report("markdown: You / name headings and bodies in order", md.find("## You\n\nfirst question") != std::string::npos && md.find("## Qwen\n\nfirst answer") != std::string::npos &&
                                                                 md.find("## You") < md.find("## Qwen"));
  std::vector<Node> moved = linearChain(g, {"u2", "a2"}, {"n1", "n2"});
  report("linearChain: fresh ids, linked in order, no dangling children",
         moved.size() == 2 && moved[0].id == "n1" && moved[0].parentId.empty() && moved[0].children == std::vector<std::string>{"n2"} && moved[1].parentId == "n1" &&
             moved[1].children.empty() && moved[1].content == "second answer");

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
