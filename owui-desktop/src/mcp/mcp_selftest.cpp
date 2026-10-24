// `Open WebUI --mcptest [server.py]` exercises the MCP client against the
// bundled Python test server over both transports, the helpers behind the
// tool loop, and - when an Ollama is reachable - one real tool-calling turn.
//
//   OWUI_MCP_TEST_SERVER   path to tools/mcp_test_server.py (default: search cwd)
//   OWUI_SERVER + OWUI_TOKEN   run the live loop through the passthrough, or
//   OLLAMA_URL                 run it against Ollama directly
//   OWUI_MCP_MODEL             model for the live loop (default qwen3:8b)
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "agents/tool_loop.h"
#include "api/client.h"
#include "core/common.h"
#include "core/http.h"
#include "mcp/mcp_client.h"

extern char** environ;

namespace {

int gPassed = 0;
int gFailed = 0;

void report(const std::string& name, bool ok, const std::string& detail = "") {
  (ok ? gPassed : gFailed)++;
  printf("  %s  %-38s %s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.substr(0, 110).c_str());
  fflush(stdout);
}

std::string env(const char* key, const char* fallback = "") {
  const char* value = std::getenv(key);
  return value ? value : fallback;
}

std::string findServerScript(int argc, const char* argv[]) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::string(argv[i]) == "--mcptest" && argv[i + 1][0] != '-') return argv[i + 1];
  }
  const std::string fromEnv = env("OWUI_MCP_TEST_SERVER");
  if (!fromEnv.empty()) return fromEnv;
  for (const char* candidate : {"tools/mcp_test_server.py", "../tools/mcp_test_server.py",
                                "../../tools/mcp_test_server.py", "owui-desktop/tools/mcp_test_server.py"}) {
    if (access(candidate, R_OK) == 0) return candidate;
  }
  return "";
}

// Starts the HTTP flavour of the test server in the background.
pid_t spawnHttpServer(const std::string& script, int port, bool sse) {
  std::vector<std::string> store = {"python3", script, "--http", std::to_string(port)};
  if (sse) store.push_back("--sse");
  std::vector<char*> argv;
  for (auto& s : store) argv.push_back(s.data());
  argv.push_back(nullptr);
  pid_t pid = -1;
  if (posix_spawnp(&pid, "python3", nullptr, nullptr, argv.data(), environ) != 0) return -1;
  // Give it a moment to bind.
  for (int i = 0; i < 30; ++i) {
    net::Request probe;
    probe.method = "POST";
    probe.url = "http://127.0.0.1:" + std::to_string(port) + "/mcp";
    probe.body = "{}";
    probe.timeoutSec = 2;
    net::Response res = net::perform(probe);
    if (res.status != 0) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  return pid;
}

void stopServer(pid_t pid) {
  if (pid <= 0) return;
  kill(pid, SIGTERM);
  int status = 0;
  waitpid(pid, &status, 0);
}

void unitTests() {
  printf("helpers\n");
  report("qualifiedToolName", mcp::qualifiedToolName("my server!", "read.file") == "my_server___read_file",
         mcp::qualifiedToolName("my server!", "read.file"));

  agents::ThinkSplitter splitter;
  std::string content, reasoning;
  for (const std::string& piece : {"Hello <thi", "nk>deep ", "thought</th", "ink> world <", "b>"}) {
    splitter.feed(piece, &content, &reasoning);
  }
  splitter.flush(&content, &reasoning);
  report("ThinkSplitter across chunks", content == "Hello  world <b>" && reasoning == "deep thought",
         "content='" + content + "' reasoning='" + reasoning + "'");

  const nlohmann::json messages = nlohmann::json::parse(R"([
    {"role":"system","content":"sys"},
    {"role":"user","content":[{"type":"text","text":"look"},{"type":"image_url","image_url":{"url":"data:image/png;base64,AAAA"}}]}
  ])");
  const nlohmann::json converted = agents::toOllamaMessages(messages);
  report("toOllamaMessages parts+images",
         converted.size() == 2 && converted[1]["content"] == "look" && converted[1]["images"][0] == "AAAA",
         converted.dump());

  const nlohmann::json options =
      agents::ollamaOptionsFromParams(nlohmann::json{{"temperature", 0.2}, {"max_tokens", 50}, {"junk", 1}});
  report("ollamaOptionsFromParams", options.value("temperature", 0.0) == 0.2 && options.value("num_predict", 0) == 50 &&
                                        !options.contains("junk"),
         options.dump());

  const nlohmann::json usage = agents::usageFromOllama(
      nlohmann::json{{"prompt_eval_count", 10}, {"eval_count", 20}, {"eval_duration", 2e9}});
  report("usageFromOllama", usage.value("total_tokens", 0) == 30 && usage.value("response_token/s", 0.0) == 10.0,
         usage.dump());
}

void protocolTests(mcp::Client& client, const std::string& label) {
  report(label + " serverInfo", client.serverInfo().value("name", "") == "mcp-test-server",
         client.serverInfo().dump());
  report(label + " instructions", !client.instructions().empty(), client.instructions());
  const auto tools = client.tools();
  report(label + " tools/list", tools.size() == 6, std::to_string(tools.size()) + " tools");

  mcp::CallResult add = client.callTool("add", {{"a", 2}, {"b", 3}});
  report(label + " tools/call add", add.ok && !add.isError && add.text == "5" && add.structured["sum"] == 5,
         add.ok ? add.text : add.error);
  mcp::CallResult echo = client.callTool("echo", {{"text", "héllo"}});
  report(label + " tools/call echo", echo.ok && echo.text == "héllo", echo.text);
  mcp::CallResult fail = client.callTool("fail", nlohmann::json::object());
  report(label + " isError surfaced", fail.ok && fail.isError, fail.text);
  mcp::CallResult slow = client.callTool("slow", {{"seconds", 3}}, 1);
  report(label + " call timeout", !slow.ok && slow.error.find("timed out") != std::string::npos, slow.error);
  // After a timeout the connection must still be usable.
  mcp::CallResult again = client.callTool("add", {{"a", 40}, {"b", 2}});
  report(label + " usable after timeout", again.ok && again.text == "42", again.ok ? again.text : again.error);

  std::string err;
  nlohmann::json resource = client.readResource("test://greeting", &err);
  report(label + " resources/read",
         !resource.is_null() && resource["contents"][0].value("text", "").find("Hello") != std::string::npos,
         err.empty() ? resource.dump().substr(0, 80) : err);
  report(label + " resources listed", client.resources().size() == 1);
  nlohmann::json prompt = client.getPrompt("greet", {{"name", "Ada"}}, &err);
  report(label + " prompts/get",
         !prompt.is_null() && prompt["messages"][0]["content"].value("text", "").find("Ada") != std::string::npos,
         err.empty() ? prompt.dump().substr(0, 80) : err);
  report(label + " prompts listed", client.prompts().size() == 1);
  nlohmann::json missing = client.request("nope/nothing", nlohmann::json::object(), 10, &err);
  report(label + " method-not-found error", missing.is_null() && err.find("-32601") != std::string::npos, err);
}

}  // namespace

int runMcpSelfTest(int argc, const char* argv[]) {
  net::globalInit();
  const std::string script = findServerScript(argc, argv);
  printf("MCP self-test (test server: %s)\n\n", script.empty() ? "NOT FOUND" : script.c_str());

  unitTests();
  if (script.empty()) {
    printf("\ntools/mcp_test_server.py not found - pass its path after --mcptest or set OWUI_MCP_TEST_SERVER\n");
    return 1;
  }

  // ---- stdio
  printf("stdio transport\n");
  {
    mcp::ServerConfig config;
    config.id = "test-stdio";
    config.name = "test";
    config.command = "python3";
    config.args = {script};
    config.env["MCP_TEST_VAR"] = "from-config";
    mcp::Client client(config);
    client.setRoots({"/tmp/example-root"});
    std::string err;
    const bool ok = client.connect(&err);
    report("stdio connect", ok, err);
    if (ok) {
      protocolTests(client, "stdio");
      mcp::CallResult envCall = client.callTool("env", {{"name", "MCP_TEST_VAR"}});
      report("stdio env passed to child", envCall.ok && envCall.text == "from-config", envCall.text);
      mcp::CallResult roots = client.callTool("roots", nlohmann::json::object());
      report("stdio roots/list answered", roots.ok && roots.text.find("/tmp/example-root") != std::string::npos,
             roots.text);
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      bool sawLog = false;
      for (const std::string& line : client.log()) {
        if (line.find("test server ready") != std::string::npos) sawLog = true;
      }
      report("stdio log notification", sawLog, std::to_string(client.log().size()) + " log lines");
      client.close();
      report("stdio close", !client.connected());
      nlohmann::json after = client.request("ping", nlohmann::json::object(), 2, &err);
      report("stdio request after close fails", after.is_null() && err == "not connected", err);
    }
  }
  {
    mcp::ServerConfig config;
    config.id = "test-missing";
    config.name = "missing";
    config.command = "definitely-not-a-real-command-xyz";
    mcp::Client client(config);
    std::string err;
    const bool ok = client.connect(&err);
    report("stdio missing command", !ok && err.find("not found") != std::string::npos, err);
  }
  {
    mcp::ServerConfig config;
    config.id = "test-notmcp";
    config.name = "notmcp";
    config.command = "cat";   // echoes our request back: a malformed reply
    mcp::Client client(config);
    std::string err;
    const bool ok = client.connect(&err, 3);
    report("stdio non-MCP process", !ok, err);
  }

  // ---- http
  for (const bool sse : {false, true}) {
    const std::string label = sse ? "http(sse)" : "http(json)";
    printf("%s transport\n", label.c_str());
    const int port = 38000 + static_cast<int>(getpid() % 1000) + (sse ? 1 : 0);
    pid_t pid = spawnHttpServer(script, port, sse);
    report(label + " server spawned", pid > 0);
    if (pid <= 0) continue;
    mcp::ServerConfig config;
    config.id = "test-http";
    config.name = "test";
    config.transport = "http";
    config.url = "http://127.0.0.1:" + std::to_string(port) + "/mcp";
    mcp::Client client(config);
    std::string err;
    const bool ok = client.connect(&err);
    report(label + " connect", ok, err);
    if (ok) {
      protocolTests(client, label);
      client.close();
      report(label + " close", !client.connected());
    }
    stopServer(pid);
  }

  // ---- importer
  {
    const std::string path = core::cacheDir() + "/mcp-import-test.json";
    core::writeFile(path, R"({"mcpServers":{"fs":{"command":"npx","args":["-y","x"],"env":{"A":"1"}},
                               "remote":{"url":"http://h/mcp","headers":{"Authorization":"Bearer t"}},
                               "off":{"command":"x","disabled":true}}})");
    std::string err;
    const auto imported = mcp::importConfigFile(path, &err);
    bool good = imported.size() == 3;
    for (const auto& c : imported) {
      if (c.name == "fs") good = good && c.transport == "stdio" && c.args.size() == 2 && c.env.at("A") == "1";
      if (c.name == "remote") good = good && c.transport == "http" && c.headers.size() == 1;
      if (c.name == "off") good = good && !c.enabled;
    }
    report("importConfigFile", good, err.empty() ? std::to_string(imported.size()) + " servers" : err);
    unlink(path.c_str());
  }

  // ---- live loop
  const std::string owuiServer = env("OWUI_SERVER");
  const std::string owuiToken = env("OWUI_TOKEN");
  const std::string ollamaUrl = env("OLLAMA_URL");
  if (!ollamaUrl.empty() || (!owuiServer.empty() && !owuiToken.empty())) {
    printf("tool loop (live)\n");
    api::Client api;
    api.setServer(owuiServer.empty() ? "http://localhost:1" : owuiServer);
    api.setToken(owuiToken);
    mcp::ServerConfig config;
    config.id = "loop";
    config.name = "calc";
    config.command = "python3";
    config.args = {script};
    mcp::Client client(config);
    std::string err;
    if (!client.connect(&err)) {
      report("loop server connect", false, err);
    } else {
      agents::LoopConfig loop;
      loop.model = env("OWUI_MCP_MODEL", "qwen3:8b");
      loop.messages = nlohmann::json::array(
          {{{"role", "user"},
            {"content", "Use the calc__add tool to compute 1234 + 4321, then reply with just the number."}}});
      for (const mcp::Tool& tool : client.tools()) {
        agents::ToolSpec spec;
        spec.name = mcp::qualifiedToolName("calc", tool.name);
        spec.description = tool.description;
        spec.parameters = tool.inputSchema;
        spec.serverId = "loop";
        spec.serverName = "calc";
        spec.tool = tool.name;
        loop.tools.push_back(spec);
      }
      loop.systemPrefix = "Use the tools when asked to.";
      std::string content;
      std::vector<std::string> called;
      agents::LoopHooks hooks;
      hooks.onDelta = [&](const std::string& c, const std::string&) {
        content += c;
        return true;
      };
      hooks.invoke = [&](const agents::Call& call) {
        called.push_back(call.name + " " + call.args.dump());
        agents::ToolOutcome out;
        mcp::CallResult res = client.callTool(call.name.substr(call.name.find("__") + 2), call.args, 60);
        out.text = res.ok ? res.text : "";
        out.error = res.ok ? "" : res.error;
        out.isError = res.isError;
        return out;
      };
      std::atomic<bool> cancel{false};
      const double started = core::nowMillis();
      agents::LoopResult result = agents::runToolLoop(api, ollamaUrl, loop, hooks, &cancel);
      char timing[64];
      snprintf(timing, sizeof(timing), "%.1fs", (core::nowMillis() - started) / 1000.0);
      report("loop completed", result.ok && result.error.empty(), result.error + " " + timing);
      report("loop called add", !called.empty() && called.front().find("calc__add") == 0,
             called.empty() ? "no tool calls" : called.front());
      report("loop answer contains 5555", content.find("5555") != std::string::npos, core::trim(content).substr(0, 100));
      report("loop usage counted", result.usage.value("total_tokens", 0) > 0, result.usage.dump());

      // Denied call: the loop tells the model and it must still finish in prose.
      content.clear();
      called.clear();
      hooks.approve = [](const agents::Call&) { return static_cast<int>(agents::Deny); };
      int denied = 0;
      hooks.onCallState = [&](const agents::Call&, agents::CallState state, const agents::ToolOutcome*) {
        if (state == agents::Denied) ++denied;
      };
      agents::LoopResult deniedResult = agents::runToolLoop(api, ollamaUrl, loop, hooks, &cancel);
      report("loop deny completes", deniedResult.ok && deniedResult.error.empty() && called.empty(),
             deniedResult.error);
      report("loop deny reported", denied >= 1 && !core::trim(content).empty(),
             std::to_string(denied) + " denied; reply: " + core::trim(content).substr(0, 60));
      client.close();
    }
  } else {
    printf("\n(set OLLAMA_URL, or OWUI_SERVER and OWUI_TOKEN, to also run a live tool-calling turn)\n");
  }

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
