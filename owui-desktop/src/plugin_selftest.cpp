// `Open WebUI --plugintest`: loads plugins from a scratch folder and drives
// the host offline - commands, tools, hooks, renderers, storage, errors and
// reload. No server needed.
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <string>
#include <vector>

#include "core/common.h"
#include "plugins/plugin_host.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
}  // namespace

int runPluginSelfTest() {
  using plugins::Host;
  printf("Open WebUI desktop plugin self-test\n\n");
  const std::string dir = "/tmp/owui-plugintest-" + std::to_string(getpid());
  mkdir(dir.c_str(), 0755);

  std::vector<std::string> toasts, inserted, sent, artifacts, logs;
  plugins::HostCallbacks cb;
  cb.toast = [&](const std::string& t) { toasts.push_back(t); };
  cb.insert = [&](const std::string& t) { inserted.push_back(t); };
  cb.send = [&](const std::string& t) { sent.push_back(t); };
  cb.openArtifact = [&](const std::string& h) { artifacts.push_back(h); };
  cb.clipboardRead = [] { return std::string("alpha beta gamma"); };
  cb.log = [&](const std::string&, const std::string& line) { logs.push_back(line); };
  cb.allowNetwork = false;
  Host& host = Host::instance();
  host.setCallbacks(cb);
  host.setDirectory(dir);

  core::writeFile(dir + "/good.js", R"JS(
owui.plugin({ name: "Good", version: "2.1", description: "test plugin" });
owui.registerCommand("Count clipboard", "counts", (ctx) => {
  owui.toast(owui.clipboard.read().split(" ").length + " words; model " + ctx.model);
});
owui.registerCommand("Shout last", (ctx) => {
  const last = ctx.chat.messages[ctx.chat.messages.length - 1];
  owui.insert(last.content.toUpperCase());
});
owui.registerTool({ name: "adder", description: "adds", parameters: { type: "object", properties: { a: {type: "number"}, b: {type: "number"} } } },
  (args) => ({ sum: args.a + args.b }));
owui.registerTool({ name: "texty", description: "string result" }, (args) => "plain " + args.x);
owui.registerTool({ name: "promised", description: "resolves at once" }, async (args) => args.x * 2);
owui.registerTool({ name: "boom", description: "throws" }, () => { throw new Error("kaboom"); });
owui.onBeforeSend((text) => text.startsWith("!up ") ? text.slice(4).toUpperCase() : undefined);
owui.registerRenderer("CSV", "Table", (code) => "<table>" + code.split("\n").length + "</table>");
owui.storage.set("n", (owui.storage.get("n") || 0) + 1);
console.log("loaded", owui.storage.get("n"));
const fetched = owui.fetch("http://127.0.0.1:9/nothing");
console.log("fetch ok", fetched.ok, fetched.error);
)JS");
  core::writeFile(dir + "/broken.js", "owui.plugin({name: 'Broken'});\nnoSuchFunction();\n");
  core::writeFile(dir + "/_ignored.js", "owui.toast('should not load');\n");

  host.loadAll();
  printf("load\n");
  const auto list = host.plugins();
  report("two plugins loaded (underscore file skipped)", list.size() == 2, std::to_string(list.size()));
  const plugins::PluginInfo* good = nullptr;
  const plugins::PluginInfo* broken = nullptr;
  for (const auto& p : list) {
    if (p.id == "good") good = &p;
    if (p.id == "broken") broken = &p;
  }
  report("good: metadata", good && good->ok && good->name == "Good" && good->version == "2.1");
  report("good: counts", good && good->commands == 2 && good->tools == 4 && good->hooks == 1 && good->renderers == 1,
         good ? std::to_string(good->commands) + "/" + std::to_string(good->tools) + "/" + std::to_string(good->hooks) + "/" + std::to_string(good->renderers) : "none");
  report("broken: error captured", broken && !broken->ok && !broken->errors.empty(),
         broken && !broken->errors.empty() ? broken->errors[0] : "");
  report("broken: metadata before the error kept", broken && broken->name == "Broken");
  report("console.log reached the log", good && good->log.size() >= 2 && good->log[0] == "loaded 1", good && !good->log.empty() ? good->log[0] : "");
  report("fetch refused when network is off", good && good->log.size() >= 2 && good->log[1].find("fetch ok false") == 0, good && good->log.size() >= 2 ? good->log[1] : "");
  report("renderer language lower-cased", host.renderers().size() == 1 && host.renderers()[0].language == "csv" && host.renderers()[0].label == "Table");
  report("tool spec carries schema", host.tools().size() == 4 && host.tools()[0].parameters.contains("properties"));

  printf("\ncalls\n");
  plugins::Json snapshot = plugins::Json::object();
  snapshot["chat"] = plugins::Json::object();
  snapshot["chat"]["id"] = "c1";
  snapshot["chat"]["title"] = "T";
  snapshot["chat"]["messages"] = plugins::Json::array({plugins::Json{{"role", "user"}, {"content", "hello there"}}});
  snapshot["model"] = "qwen3:8b";
  host.runCommand("good", "Count clipboard", snapshot);
  host.runCommand("good", "Shout last", snapshot);
  // Commands run in the background; a blocking call afterwards orders them.
  std::string error;
  std::string out = host.callTool("good", "adder", plugins::Json{{"a", 2}, {"b", 40}}, snapshot, &error);
  std::string lastLog;
  for (const auto& p : host.plugins()) {
    if (p.id == "good" && !p.log.empty()) lastLog = p.log.back();
  }
  report("command used clipboard + ctx.model", toasts.size() == 1 && toasts[0] == "3 words; model qwen3:8b", toasts.empty() ? lastLog : toasts[0]);
  report("command inserted from ctx.chat", inserted.size() == 1 && inserted[0] == "HELLO THERE");
  report("tool object result as json", error.empty() && out == "{\n  \"sum\": 42\n}", out + " " + error);
  out = host.callTool("good", "texty", plugins::Json{{"x", "x"}}, snapshot, &error);
  report("tool string result as is", error.empty() && out == "plain x", out);
  out = host.callTool("good", "promised", plugins::Json{{"x", 21}}, snapshot, &error);
  report("async tool settles", error.empty() && out == "42", out + " " + error);
  out = host.callTool("good", "boom", plugins::Json::object(), snapshot, &error);
  report("throwing tool reports the error", !error.empty() && error.find("kaboom") != std::string::npos, error);
  out = host.callTool("good", "missing", plugins::Json::object(), snapshot, &error);
  report("unknown tool", !error.empty());
  report("before-send rewrites", host.beforeSend("!up quiet please", snapshot) == "QUIET PLEASE");
  report("before-send leaves others alone", host.beforeSend("plain", snapshot) == "plain");
  out = host.render("good", "csv", "a,b\n1,2\n3,4", &error);
  report("renderer returns html", error.empty() && out == "<table>3</table>", out);

  printf("\nreload\n");
  core::writeFile(dir + "/good.js", "owui.plugin({name: 'Good2'});\nowui.registerCommand('Only one', () => {});\n");
  host.loadAll();
  bool renamed = false;
  for (const auto& p : host.plugins()) {
    if (p.id == "good" && p.name == "Good2" && p.commands == 1 && p.tools == 0) renamed = true;
  }
  report("reload picks up the new file", renamed);
  report("storage survived the reload", core::readFile(dir + "/.data/good.json").find("\"n\": 1") != std::string::npos);
  report("old tools gone", host.tools().empty());
  report("no hooks left", !host.hasBeforeSendHooks());
  host.unloadAll();
  report("unload clears everything", host.plugins().empty() && host.commands().empty());

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
