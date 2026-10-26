// `Open WebUI --notebooktest`: offline checks of notebook mode, with a live kernel.
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "core/notebook.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
}  // namespace

int runNotebookSelfTest() {
  using namespace notebook;
  printf("Open WebUI desktop notebook self-test\n\n");

  report("sourceLines keeps newlines per line", sourceLines("a\nb\n") == std::vector<std::string>({"a\n", "b\n"}) && sourceLines("a\nb") == std::vector<std::string>({"a\n", "b"}) && sourceLines("").empty());

  Notebook nb;
  nb.title = "Areas";
  nb.pythonVersion = "3.15.0";
  Cell md{newCellId(), "markdown", "# Circle areas", {}, 0, ""};
  Cell code{newCellId(), "code", "r = 3\nprint('radius', r)\n3.14159 * r * r", {}, 2, ""};
  code.outputs = {Output{"stream", "stdout", "radius 3\n", {}, ""}, Output{"result", "", "28.27431", {}, ""}, Output{"image", "", "", {}, "iVBORw0KGgo="}};
  Cell bad{newCellId(), "code", "1/0", {Output{"error", "", "ZeroDivisionError: division by zero", {"Traceback ...\n"}, ""}}, 3, ""};
  Cell prompt{newCellId(), "prompt", "Plot r against area", {Output{"answer", "", "Use matplotlib:\n```python\nimport matplotlib\n```", {}, ""}}, 0, "qwen3:8b"};
  nb.cells = {md, code, bad, prompt};
  const Json ipynb = toIpynb(nb);
  report("toIpynb: nbformat 4.5, kernelspec, cell ids, code outputs as stream / execute_result / display_data / error",
         ipynb["nbformat"] == 4 && ipynb["nbformat_minor"] == 5 && ipynb["metadata"]["kernelspec"]["name"] == "python3" && ipynb["cells"].size() == 4 && ipynb["cells"][1]["id"] == code.id &&
             ipynb["cells"][1]["execution_count"] == 2 && ipynb["cells"][1]["outputs"][0]["output_type"] == "stream" && ipynb["cells"][1]["outputs"][1]["data"]["text/plain"][0] == "28.27431" &&
             ipynb["cells"][1]["outputs"][2]["data"]["image/png"] == "iVBORw0KGgo=" && ipynb["cells"][2]["outputs"][0]["ename"] == "ZeroDivisionError" && ipynb["cells"][1]["source"][0] == "r = 3\n");
  report("toIpynb: a prompt cell is a markdown cell anyone can read, with the prompt and answer kept in metadata",
         ipynb["cells"][3]["cell_type"] == "markdown" && ipynb["cells"][3]["source"][0] == "**Prompt:** Plot r against area\n" && ipynb["cells"][3]["metadata"]["owui"]["answer"] == prompt.outputs[0].text);
  const Notebook back = fromIpynb(Json::parse(ipynb.dump()));
  report("fromIpynb round-trips every cell kind and output", back.title == "Areas" && back.cells.size() == 4 && back.cells[0].kind == "markdown" && back.cells[1].source == code.source && back.cells[1].count == 2 &&
                                                                  back.cells[1].outputs.size() == 3 && back.cells[1].outputs[2].png == "iVBORw0KGgo=" && back.cells[2].outputs[0].text == "ZeroDivisionError: division by zero" &&
                                                                  back.cells[3].kind == "prompt" && back.cells[3].model == "qwen3:8b" && back.cells[3].outputs[0].type == "answer");
  std::string err;
  report("fromIpynb rejects non-notebooks and nbformat 3", fromIpynb(Json{{"x", 1}}, &err).cells.empty() && err.find("no cells") != std::string::npos && fromIpynb(Json{{"nbformat", 3}, {"cells", Json::array()}}, &err).cells.empty() &&
                                                                  err.find("too old") != std::string::npos);
  const Json foreign = Json::parse(R"js({"nbformat":4,"nbformat_minor":2,"metadata":{},"cells":[{"cell_type":"code","execution_count":null,"metadata":{},"source":"x = 1","outputs":[{"output_type":"execute_result","data":{"text/plain":["1"]},"execution_count":1,"metadata":{}}]},{"cell_type":"raw","metadata":{},"source":["raw text"]}]})js");
  const Notebook f = fromIpynb(foreign);
  report("fromIpynb: Jupyter-made notebooks (string source, null count, raw cells, no ids)", f.cells.size() == 2 && f.cells[0].source == "x = 1" && f.cells[0].count == 0 && !f.cells[0].id.empty() && f.cells[0].outputs[0].text == "1" &&
                                                                                                f.cells[1].kind == "markdown");

  const std::string ctx = promptContext(nb, 3);
  report("promptContext: code with its outputs, markdown, figures noted, stops before the cell", ctx.find("# Circle areas") != std::string::npos && ctx.find("In [2]:\n```python\nr = 3") != std::string::npos &&
                                                                                                    ctx.find("Out:\nradius 3\n28.27431\n[figure]") != std::string::npos && ctx.find("ZeroDivisionError") != std::string::npos &&
                                                                                                    ctx.find("Plot r against") == std::string::npos);
  report("promptContext: oldest cells dropped first to fit", promptContext(nb, 3, 120).find("earlier cell") != std::string::npos && promptContext(nb, 3, 120).find("Circle areas") == std::string::npos);
  report("extractPython / stripThinking", extractPython("Try:\n```python\nprint(1)\n```\nand") == "print(1)\n" && extractPython("```\nx=2\n```") == "x=2\n" && extractPython("no code").empty() &&
                                              stripThinking("<think>hmm</think>\n\nHello") == "Hello" && stripThinking("plain") == "plain");

  // Live kernel.
  Kernel k;
  std::string error;
  if (!k.start(&error)) {
    report("live kernel starts", false, error);
  } else {
    report("live kernel starts and reports its Python version", !k.version().empty(), k.version());
    const ExecResult a = k.execute("x = 6\nprint('hello')\nx * 7", 10000);
    report("execute: stdout captured, last expression returned, counts from 1", a.ok && a.out == "hello\n" && a.result == "42" && a.count == 1 && a.ename.empty());
    const ExecResult b = k.execute("x + 1", 10000);
    report("state persists between cells", b.ok && b.result == "7" && b.count == 2);
    const ExecResult c = k.execute("import sys\nsys.stderr.write('warn\\n')\nprint(undefined_name)", 10000);
    report("errors: NameError with a traceback naming the cell, stderr kept, driver frames hidden", c.ok && c.ename == "NameError" && c.err == "warn\n" && !c.traceback.empty() &&
                                                                                                         c.traceback.back().find("undefined_name") != std::string::npos && [&] {
                                                                                                           for (const auto& t : c.traceback) {
                                                                                                             if (t.find("<string>") != std::string::npos) return false;
                                                                                                           }
                                                                                                           return true;
                                                                                                         }());
    const ExecResult d = k.execute("def f():\n    return [i*i for i in range(4)]\nf()", 10000);
    report("multi-line definitions; None results stay silent", d.ok && d.result == "[0, 1, 4, 9]" && k.execute("y = None\ny", 10000).result.empty());
    const auto t0 = std::chrono::steady_clock::now();
    std::thread stopper([&] {
      std::this_thread::sleep_for(std::chrono::milliseconds(400));
      k.interrupt();
    });
    const ExecResult e = k.execute("while True:\n    pass", 20000);
    stopper.join();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    report("interrupt stops a busy loop with KeyboardInterrupt and the kernel lives on", e.ok && e.ename == "KeyboardInterrupt" && ms < 5000 && k.execute("x", 10000).result == "6", std::to_string(ms) + " ms");
    const ExecResult t = k.execute("import time\ntime.sleep(30)", 600);
    report("a timeout interrupts the cell and says so", t.timedOut && t.ok && k.alive());
    const ExecResult s = k.execute("import os\nos.write(1, b'raw\\n')\n'after'", 10000);
    report("stray writes to fd 1 don't break the protocol (shown as output)", s.ok && s.result == "'after'" && s.out.find("raw") != std::string::npos);
    const ExecResult q = k.execute("raise SystemExit(3)", 10000);
    report("SystemExit is reported, not fatal", q.ok && q.ename == "SystemExit" && k.alive());
    std::string rerr;
    const bool restarted = k.restart(&rerr);
    const ExecResult r = k.execute("x", 10000);
    report("restart clears the namespace and the count", restarted && r.ename == "NameError" && r.count == 1);
    const auto outs = toOutputs(a);
    report("toOutputs: stdout then result", outs.size() == 2 && outs[0].type == "stream" && outs[1].type == "result" && outs[1].text == "42");
    k.stop();
    report("stop ends the kernel", !k.alive() && k.execute("1", 1000).kernelError == "the kernel is not running");
  }

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
