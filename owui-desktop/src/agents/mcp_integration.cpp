// MCP in the app: the server registry, the chat tool loop that uses it, the
// tool-call cards above a reply, and the MCP tab in the Tools pane.
//
// Settings keys: extra["mcpServers"] (array of mcp::ServerConfig),
// extra["mcpApprovals"] ({"<server>__<tool>": "always"|"never"}),
// extra["mcpEnabled"], extra["mcpAutoApprove"], extra["mcpMaxRounds"].
#include "app.h"

#include <algorithm>
#include <chrono>

#include "core/async.h"
#include "core/common.h"
#include "imgui_stdlib.h"
#include "native.h"
#include "pane_common.h"
#include "platform.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

// UI-thread bookkeeping for the MCP tab, keyed by server id.
struct ServerUi {
  bool connecting = false;
  std::string error;
  std::string tryTool;        // tool whose "try it" box is open
  std::string tryArgs = "{}";
  std::string tryResult;
  bool tryRunning = false;
  bool showLog = false;
};
std::map<std::string, ServerUi> gServerUi;

bool gShowEditor = false;
bool gEditorIsNew = false;
mcp::ServerConfig gEdit;
std::string gEditArgs;
std::string gEditEnv;
std::string gEditHeaders;
int gEditTransport = 0;

// Shell-style split: spaces separate, quotes group, backslash escapes.
std::vector<std::string> splitArgs(const std::string& line) {
  std::vector<std::string> out;
  std::string current;
  bool inWord = false;
  char quote = 0;
  for (size_t i = 0; i < line.size(); ++i) {
    const char ch = line[i];
    if (quote) {
      if (ch == quote) {
        quote = 0;
      } else if (ch == '\\' && quote == '"' && i + 1 < line.size()) {
        current += line[++i];
      } else {
        current += ch;
      }
    } else if (ch == '\'' || ch == '"') {
      quote = ch;
      inWord = true;
    } else if (ch == '\\' && i + 1 < line.size()) {
      current += line[++i];
      inWord = true;
    } else if (std::isspace(static_cast<unsigned char>(ch))) {
      if (inWord) out.push_back(current);
      current.clear();
      inWord = false;
    } else {
      current += ch;
      inWord = true;
    }
  }
  if (inWord) out.push_back(current);
  return out;
}

std::string joinArgs(const std::vector<std::string>& args) {
  std::string out;
  for (const std::string& a : args) {
    if (!out.empty()) out += ' ';
    if (a.find_first_of(" \t'\"") != std::string::npos) {
      out += '"' + core::replaceAll(a, "\"", "\\\"") + '"';
    } else {
      out += a;
    }
  }
  return out;
}

// KEY=VALUE per line (env) or Name: value per line (headers).
std::vector<std::pair<std::string, std::string>> parsePairs(const std::string& text, char sep) {
  std::vector<std::pair<std::string, std::string>> out;
  for (const std::string& raw : core::split(text, '\n')) {
    const std::string line = core::trim(raw);
    if (line.empty() || line[0] == '#') continue;
    const size_t at = line.find(sep);
    if (at == std::string::npos) continue;
    out.emplace_back(core::trim(line.substr(0, at)), core::trim(line.substr(at + 1)));
  }
  return out;
}

void openEditor(const mcp::ServerConfig& config, bool isNew) {
  gEdit = config;
  gEditorIsNew = isNew;
  gEditArgs = joinArgs(config.args);
  gEditEnv.clear();
  for (const auto& [k, v] : config.env) gEditEnv += k + "=" + v + "\n";
  gEditHeaders.clear();
  for (const auto& [k, v] : config.headers) gEditHeaders += k + ": " + v + "\n";
  gEditTransport = config.transport == "http" ? 1 : 0;
  gShowEditor = true;
}

// A starter argument object from a tool's JSON schema, so "try it" needs
// only the values filled in.
Json exampleArgs(const Json& schema) {
  Json out = Json::object();
  if (!schema.is_object() || !schema.contains("properties") || !schema["properties"].is_object()) return out;
  for (auto it = schema["properties"].begin(); it != schema["properties"].end(); ++it) {
    const std::string type = it.value().is_object() ? it.value().value("type", "string") : "string";
    if (type == "number" || type == "integer") out[it.key()] = 0;
    else if (type == "boolean") out[it.key()] = false;
    else if (type == "array") out[it.key()] = Json::array();
    else if (type == "object") out[it.key()] = Json::object();
    else out[it.key()] = "";
  }
  return out;
}

const char* stateLabel(int state) {
  switch (state) {
    case agents::Pending: return "waiting for approval";
    case agents::Running: return "running";
    case agents::Done: return "done";
    case agents::Failed: return "failed";
    case agents::Denied: return "denied";
  }
  return "";
}

}  // namespace

// ---------------------------------------------------------------- ToolCall

Json ToolCall::toJson() const {
  Json j{{"id", id},       {"server", server},       {"tool", tool},     {"name", qualifiedName},
         {"args", args},   {"isError", isError},     {"state", state},   {"durationMs", durationMs}};
  j["result"] = result.size() > 100000 ? result.substr(0, 100000) + "\n...[truncated]" : result;
  if (!error.empty()) j["error"] = error;
  return j;
}

ToolCall ToolCall::fromJson(const Json& j) {
  ToolCall t;
  if (!j.is_object()) return t;
  t.id = j.value("id", "");
  t.server = j.value("server", "");
  t.tool = j.value("tool", "");
  t.qualifiedName = j.value("name", "");
  if (j.contains("args")) t.args = j["args"];
  t.result = j.value("result", "");
  t.error = j.value("error", "");
  t.isError = j.value("isError", false);
  t.state = j.value("state", static_cast<int>(agents::Done));
  t.durationMs = j.value("durationMs", 0.0);
  return t;
}

// ---------------------------------------------------------------- registry

std::vector<mcp::ServerConfig> App::mcpServers() const {
  std::vector<mcp::ServerConfig> out;
  const Json& list = core::Settings::instance().extra.value("mcpServers", Json::array());
  if (!list.is_array()) return out;
  for (const auto& entry : list) {
    if (entry.is_object()) out.push_back(mcp::ServerConfig::fromJson(entry));
  }
  return out;
}

void App::saveMcpServers(const std::vector<mcp::ServerConfig>& list) {
  Json arr = Json::array();
  for (const auto& config : list) arr.push_back(config.toJson());
  settings().extra["mcpServers"] = arr;
  settings().save();
}

std::shared_ptr<mcp::Client> App::mcpClient(const std::string& serverId) const {
  std::lock_guard<std::mutex> lock(mcpMutex_);
  auto it = mcpClients_.find(serverId);
  return it == mcpClients_.end() ? nullptr : it->second;
}

int App::mcpConnectedCount() const {
  std::lock_guard<std::mutex> lock(mcpMutex_);
  int n = 0;
  for (const auto& [id, client] : mcpClients_) {
    if (client && client->connected()) ++n;
  }
  return n;
}

std::vector<agents::ToolSpec> App::mcpToolSpecs() const {
  std::vector<agents::ToolSpec> out;
  std::lock_guard<std::mutex> lock(mcpMutex_);
  for (const auto& [id, client] : mcpClients_) {
    if (!client || !client->connected()) continue;
    const std::string serverName = client->config().name;
    for (const mcp::Tool& tool : client->tools()) {
      agents::ToolSpec spec;
      spec.name = mcp::qualifiedToolName(serverName, tool.name);
      spec.description = tool.description;
      spec.parameters = tool.inputSchema;
      spec.serverId = id;
      spec.serverName = serverName;
      spec.tool = tool.name;
      out.push_back(spec);
    }
  }
  return out;
}

void App::mcpConnect(const std::string& serverId) {
  mcp::ServerConfig config;
  bool found = false;
  for (const auto& entry : mcpServers()) {
    if (entry.id == serverId) {
      config = entry;
      found = true;
    }
  }
  if (!found) return;
  auto client = std::make_shared<mcp::Client>(config);
  if (!projectFolder.empty()) client->setRoots({projectFolder});
  std::shared_ptr<mcp::Client> previous;
  {
    std::lock_guard<std::mutex> lock(mcpMutex_);
    previous = mcpClients_[serverId];
    mcpClients_[serverId] = client;
  }
  ServerUi& ui = gServerUi[serverId];
  ui.connecting = true;
  ui.error.clear();
  async::run([client, previous, serverId] {
    if (previous) previous->close();
    std::string error;
    const bool ok = client->connect(&error);
    async::post([client, serverId, ok, error] {
      ServerUi& state = gServerUi[serverId];
      state.connecting = false;
      state.error = ok ? "" : error;
      if (ok) {
        widgets::toast(widgets::ToastLevel::Success,
                       client->config().name + ": " + std::to_string(client->tools().size()) + " tools");
      } else {
        widgets::toastError(client->config().name + ": " + error);
      }
    });
  });
}

void App::mcpDisconnect(const std::string& serverId) {
  std::shared_ptr<mcp::Client> client;
  {
    std::lock_guard<std::mutex> lock(mcpMutex_);
    auto it = mcpClients_.find(serverId);
    if (it == mcpClients_.end()) return;
    client = it->second;
    mcpClients_.erase(it);
  }
  gServerUi[serverId].connecting = false;
  if (client) async::run([client] { client->close(); });
}

void App::mcpInit() {
  mcpEnabled = settings().extra.value("mcpEnabled", false);
  agentMode = settings().extra.value("agentMode", false);
  for (const auto& config : mcpServers()) {
    if (config.enabled) mcpConnect(config.id);
  }
}

void App::mcpShutdown() {
  std::map<std::string, std::shared_ptr<mcp::Client>> clients;
  {
    std::lock_guard<std::mutex> lock(mcpMutex_);
    clients.swap(mcpClients_);
  }
  // close() waits for each child to exit; do them together so quitting with
  // several servers up still takes about one server's worth of time.
  std::vector<std::thread> closers;
  for (auto& [id, client] : clients) {
    closers.emplace_back([client] { client->close(); });
  }
  for (auto& t : closers) t.join();
}

std::string App::mcpApprovalFor(const std::string& qualifiedName) const {
  const Json& approvals = core::Settings::instance().extra.value("mcpApprovals", Json::object());
  if (approvals.is_object() && approvals.contains(qualifiedName) && approvals[qualifiedName].is_string()) {
    return approvals[qualifiedName];
  }
  return "ask";
}

void App::setMcpApproval(const std::string& qualifiedName, const std::string& policy) {
  Json& approvals = settings().extra["mcpApprovals"];
  if (!approvals.is_object()) approvals = Json::object();
  if (policy == "ask") {
    approvals.erase(qualifiedName);
  } else {
    approvals[qualifiedName] = policy;
  }
  settings().save();
}

void App::mcpImportConfig(const std::string& path) {
  std::string error;
  std::vector<mcp::ServerConfig> imported = mcp::importConfigFile(path, &error);
  if (!error.empty()) {
    widgets::toastError(error);
    return;
  }
  std::vector<mcp::ServerConfig> list = mcpServers();
  int added = 0;
  for (const auto& config : imported) {
    const bool exists = std::any_of(list.begin(), list.end(), [&](const mcp::ServerConfig& c) {
      return c.name == config.name;
    });
    if (exists) continue;
    list.push_back(config);
    ++added;
  }
  saveMcpServers(list);
  for (const auto& config : imported) {
    if (config.enabled) mcpConnect(config.id);
  }
  widgets::toast(widgets::ToastLevel::Success,
                 "Imported " + std::to_string(added) + " server" + (added == 1 ? "" : "s") + " from " +
                     core::baseName(path));
}

// ---------------------------------------------------------------- tool stream

// The parts of a tool-loop configuration that do not depend on the turn:
// the Ollama model behind a workspace model, its options, the tools and
// the system prefix with every connected server's instructions.
agents::LoopConfig App::baseToolConfig(const std::string& model) {
  agents::LoopConfig config;
  config.tools = mcpToolSpecs();
  config.options = agents::ollamaOptionsFromParams(chat.params);

  // A workspace model is an Ollama model plus a system prompt; Ollama only
  // knows the former, so unwrap it and carry the prompt along.
  config.model = model;
  std::string systemPrompt;
  for (const ModelInfo& info : models) {
    if (info.id != model || !info.raw.contains("info") || !info.raw["info"].is_object()) continue;
    const Json& meta = info.raw["info"];
    if (meta.contains("base_model_id") && meta["base_model_id"].is_string() &&
        !meta["base_model_id"].get<std::string>().empty()) {
      config.model = meta["base_model_id"];
    }
    const Json params = meta.value("params", Json::object());
    if (params.is_object() && params.contains("system") && params["system"].is_string()) {
      systemPrompt = params["system"];
    }
  }
  std::string prefix = systemPrompt;
  if (!prefix.empty()) prefix += "\n\n";
  prefix += "You can call tools provided by MCP servers. Call a tool whenever it helps answer; once the "
            "results arrive, answer the user directly and do not narrate the call itself.";
  {
    std::lock_guard<std::mutex> lock(mcpMutex_);
    for (const auto& [id, client] : mcpClients_) {
      if (!client || !client->connected()) continue;
      const std::string instructions = core::trim(client->instructions());
      if (!instructions.empty()) prefix += "\n\nServer \"" + client->config().name + "\": " + instructions;
    }
  }
  config.systemPrefix = prefix;
  return config;
}

// Approval, invocation and progress hooks shared by every loop that runs
// MCP tools for a stream (the tool loop and the agent loop).
agents::LoopHooks App::makeToolHooks(const std::shared_ptr<Stream>& stream,
                                     const std::map<std::string, std::shared_ptr<mcp::Client>>& clients,
                                     const std::map<std::string, agents::ToolSpec>& byName,
                                     bool autoApprove, const Json& approvals) {
  agents::LoopHooks hooks;
  hooks.onCallState = [stream, byName](const agents::Call& call, agents::CallState state,
                                       const agents::ToolOutcome* outcome) {
    std::lock_guard<std::mutex> lock(stream->mutex);
    ToolCall* entry = nullptr;
    for (ToolCall& t : stream->toolCalls) {
      if (t.id == call.id) entry = &t;
    }
    if (!entry) {
      ToolCall fresh;
      fresh.id = call.id;
      fresh.qualifiedName = call.name;
      fresh.args = call.args;
      auto spec = byName.find(call.name);
      if (spec != byName.end()) {
        fresh.server = spec->second.serverName;
        fresh.tool = spec->second.tool;
      } else {
        fresh.tool = call.name;
      }
      stream->toolCalls.push_back(fresh);
      entry = &stream->toolCalls.back();
    }
    entry->state = state;
    if (state == agents::Running) entry->startedMs = core::nowMillis();
    if (outcome) {
      entry->result = outcome->text;
      entry->error = outcome->error;
      entry->isError = outcome->isError;
      entry->durationMs = outcome->durationMs;
    }
    if (state == agents::Pending) {
      stream->status = "Waiting for approval: " + entry->tool;
    } else if (state == agents::Running) {
      stream->status = "Running " + entry->tool + "...";
    } else {
      stream->status.clear();
    }
  };
  hooks.approve = [stream, autoApprove, approvals](const agents::Call& call) -> int {
    const std::string policy = approvals.is_object() ? approvals.value(call.name, "ask") : "ask";
    if (policy == "never") return agents::Deny;
    if (autoApprove || policy == "always") return agents::Allow;
    std::unique_lock<std::mutex> lock(stream->mutex);
    while (true) {
      for (const ToolCall& t : stream->toolCalls) {
        if (t.id == call.id && t.decision != 0) {
          return t.decision == agents::Deny ? agents::Deny : agents::Allow;
        }
      }
      if (stream->cancel.load()) return agents::Deny;
      stream->approvalCv.wait_for(lock, std::chrono::milliseconds(200));
    }
  };
  hooks.invoke = [clients, byName](const agents::Call& call) {
    agents::ToolOutcome out;
    auto spec = byName.find(call.name);
    if (spec == byName.end()) {
      out.error = "unknown tool '" + call.name + "'";
      return out;
    }
    auto client = clients.find(spec->second.serverId);
    if (client == clients.end() || !client->second || !client->second->connected()) {
      out.error = "server '" + spec->second.serverName + "' is not connected";
      return out;
    }
    mcp::CallResult result = client->second->callTool(spec->second.tool, call.args, 120);
    out.durationMs = result.durationMs;
    if (!result.ok) {
      out.error = result.error;
      return out;
    }
    out.text = result.text;
    out.isError = result.isError;
    return out;
  };
  hooks.onUsage = [stream](const Json& usage) {
    std::lock_guard<std::mutex> lock(stream->mutex);
    stream->usage = usage;
  };
  return hooks;
}

void App::startToolStream(const std::string& assistantId, const std::string& model) {
  auto stream = std::make_shared<Stream>();
  stream->chatId = chat.id;
  stream->messageId = assistantId;
  stream->model = model;
  stream->sessionId = sessionId_;
  streams_.push_back(stream);
  timings[assistantId] = MessageTiming{core::nowMillis(), 0.0, 0.0};

  Json payload = buildCompletionPayload(assistantId, model);
  agents::LoopConfig config = baseToolConfig(model);
  config.messages = payload["messages"];
  config.maxRounds = std::max(1, settings().extra.value("mcpMaxRounds", 8));

  lastPayload = payload;
  lastPayload["tools"] = agents::toOllamaTools(config.tools);
  lastPayload["transport"] = "ollama /api/chat (MCP tool loop)";
  lastPayload["model"] = config.model;

  std::map<std::string, std::shared_ptr<mcp::Client>> clients;
  {
    std::lock_guard<std::mutex> lock(mcpMutex_);
    clients = mcpClients_;
  }
  std::map<std::string, agents::ToolSpec> byName;
  for (const auto& spec : config.tools) byName[spec.name] = spec;
  const bool autoApprove = settings().extra.value("mcpAutoApprove", false);
  const Json approvals = settings().extra.value("mcpApprovals", Json::object());
  const std::string directUrl = settings().ollamaUrl;

  async::runDetached([this, stream, config, clients, byName, autoApprove, approvals, directUrl] {
    agents::LoopHooks hooks = makeToolHooks(stream, clients, byName, autoApprove, approvals);
    hooks.onDelta = [stream](const std::string& content, const std::string& reasoning) {
      std::lock_guard<std::mutex> lock(stream->mutex);
      stream->started = true;
      stream->content += content;
      stream->reasoning += reasoning;
      return !stream->cancel.load();
    };
    agents::LoopResult result = agents::runToolLoop(api_, directUrl, config, hooks, &stream->cancel);
    std::lock_guard<std::mutex> lock(stream->mutex);
    if (!result.ok && !stream->cancel.load()) stream->error = result.error;
    stream->done = true;
  });
}

// ---------------------------------------------------------------- cards

void App::drawToolCallCards(Message& message, float width) {
  const auto& palette = theme::palette();
  for (size_t i = 0; i < message.toolCalls.size(); ++i) {
    ToolCall& call = message.toolCalls[i];
    ImGui::PushID(static_cast<int>(i));

    ImU32 statusColor = palette.textDim;
    switch (call.state) {
      case agents::Pending: statusColor = palette.warning; break;
      case agents::Running: statusColor = palette.accent; break;
      case agents::Done: statusColor = call.isError ? palette.warning : palette.success; break;
      case agents::Failed: statusColor = palette.danger; break;
      case agents::Denied: statusColor = palette.textFaint; break;
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::toVec4(palette.panel));
    ImGui::PushStyleColor(ImGuiCol_Border, theme::toVec4(palette.border));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    ImGui::BeginChild("##toolcall", ImVec2(width, 0),
                      ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders |
                          ImGuiChildFlags_AlwaysUseWindowPadding);

    // Header: dot, tool, server, timing, status; the whole row toggles details.
    const ImVec2 rowPos = ImGui::GetCursorScreenPos();
    const float rowHeight = ImGui::GetTextLineHeight() + 6.0f;
    const float innerWidth = ImGui::GetContentRegionAvail().x;
    if (ImGui::InvisibleButton("##header", ImVec2(innerWidth, rowHeight))) call.expanded = !call.expanded;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float midY = rowPos.y + rowHeight * 0.5f;
    if (call.state == agents::Running) {
      // A ring that sweeps while the tool runs.
      const float t = static_cast<float>(ImGui::GetTime());
      dl->PathArcTo(ImVec2(rowPos.x + 8.0f, midY), 5.0f, t * 4.0f, t * 4.0f + 4.2f, 12);
      dl->PathStroke(statusColor, 2.0f);
    } else {
      dl->AddCircleFilled(ImVec2(rowPos.x + 8.0f, midY), 4.5f, statusColor);
    }
    float x = rowPos.x + 22.0f;
    theme::pushBold();
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(x, rowPos.y + 3.0f), palette.text,
                call.tool.c_str());
    x += ImGui::CalcTextSize(call.tool.c_str()).x + 8.0f;
    theme::pop();
    std::string meta = call.server;
    if (call.durationMs > 0.0) {
      char buffer[48];
      snprintf(buffer, sizeof(buffer), "%s%.0f ms", meta.empty() ? "" : " · ", call.durationMs);
      meta += buffer;
    }
    dl->AddText(ImVec2(x, rowPos.y + 3.0f), palette.textDim, meta.c_str());
    const char* status = stateLabel(call.state);
    const float statusWidth = ImGui::CalcTextSize(status).x;
    dl->AddText(ImVec2(rowPos.x + innerWidth - statusWidth - 22.0f, rowPos.y + 3.0f), statusColor, status);
    icons::draw(dl, call.expanded ? icons::Icon::Chevron : icons::Icon::ChevronRight,
                ImVec2(rowPos.x + innerWidth - 8.0f, midY), 10.0f, palette.textDim);

    // Approval buttons stay visible however the card is folded.
    if (call.state == agents::Pending && message.streaming) {
      auto decide = [&](int decision) {
        call.decision = decision;
        if (decision == agents::AllowAlways) setMcpApproval(call.qualifiedName, "always");
        if (auto stream = streamFor(message.id)) {
          std::lock_guard<std::mutex> lock(stream->mutex);
          for (ToolCall& pending : stream->toolCalls) {
            if (pending.id == call.id) pending.decision = decision;
          }
          stream->approvalCv.notify_all();
        }
      };
      ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.warning));
      ImGui::TextWrapped("%s wants to run %s with these arguments:", call.server.c_str(), call.tool.c_str());
      ImGui::PopStyleColor();
      const std::string pendingArgs = call.args.dump(2);
      const float pendingLines = static_cast<float>(std::count(pendingArgs.begin(), pendingArgs.end(), '\n') + 1);
      monoOutput("##pendingargs", pendingArgs,
                 ImVec2(innerWidth, std::min(140.0f, ImGui::GetTextLineHeight() * pendingLines + 12.0f)), false);
      if (widgets::primaryButton("Allow once", ImVec2(110, 0))) decide(agents::Allow);
      ImGui::SameLine();
      if (ImGui::Button("Always allow", ImVec2(110, 0))) decide(agents::AllowAlways);
      ImGui::SameLine();
      if (widgets::dangerButton("Deny", ImVec2(80, 0))) decide(agents::Deny);
    } else if (call.expanded) {
      widgets::dimText("Arguments");
      const std::string args = call.args.dump(2);
      const float argLines = static_cast<float>(std::count(args.begin(), args.end(), '\n') + 1);
      monoOutput("##args", args,
                 ImVec2(innerWidth, std::min(160.0f, ImGui::GetTextLineHeight() * argLines + 12.0f)), false);
      const std::string& body = call.error.empty() ? call.result : call.error;
      widgets::dimText(call.error.empty() ? (call.isError ? "Result (tool reported an error)" : "Result")
                                          : "Error");
      const float bodyLines = static_cast<float>(std::count(body.begin(), body.end(), '\n') + 1);
      monoOutput("##result", body,
                 ImVec2(innerWidth, std::min(260.0f, ImGui::GetTextLineHeight() * bodyLines + 12.0f)), false);
      if (ImGui::SmallButton("Copy result")) {
        platform::setClipboardText(body);
        widgets::toast(widgets::ToastLevel::Success, "Copied");
      }
      if (!call.qualifiedName.empty()) {
        ImGui::SameLine();
        const std::string policy = mcpApprovalFor(call.qualifiedName);
        widgets::dimText("approval: %s", policy.c_str());
      }
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::PopID();
  }
}

// ---------------------------------------------------------------- MCP tab

void App::drawMcpSection(float width) {
  const auto& palette = theme::palette();
  std::vector<mcp::ServerConfig> servers = mcpServers();
  const std::vector<agents::ToolSpec> specs = mcpToolSpecs();
  const int connected = mcpConnectedCount();

  card("mcpglobal", [&] {
    bool enabled = mcpEnabled;
    if (ImGui::Checkbox("Use MCP tools in chat", &enabled)) {
      mcpEnabled = enabled;
      settings().extra["mcpEnabled"] = enabled;
      settings().save();
    }
    ImGui::SameLine();
    widgets::dimText("%d tool%s from %d connected server%s", static_cast<int>(specs.size()),
                     specs.size() == 1 ? "" : "s", connected, connected == 1 ? "" : "s");
    bool autoApprove = settings().extra.value("mcpAutoApprove", false);
    if (ImGui::Checkbox("Approve every tool call automatically", &autoApprove)) {
      settings().extra["mcpAutoApprove"] = autoApprove;
      settings().save();
    }
    int rounds = settings().extra.value("mcpMaxRounds", 8);
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::SliderInt("Max tool rounds per reply", &rounds, 1, 24)) {
      settings().extra["mcpMaxRounds"] = rounds;
      settings().save();
    }
    ImGui::Dummy(ImVec2(0, 4));
    if (widgets::primaryButton("Add server...", ImVec2(140, 0))) {
      mcp::ServerConfig fresh;
      fresh.id = core::uuid();
      fresh.name = "my-server";
      openEditor(fresh, true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Import Claude Desktop config")) {
      mcpImportConfig(native::homeDirectory() +
                      "/Library/Application Support/Claude/claude_desktop_config.json");
    }
    ImGui::SameLine();
    if (ImGui::Button("Import Cursor config")) mcpImportConfig(native::homeDirectory() + "/.cursor/mcp.json");
    ImGui::SameLine();
    if (ImGui::Button("Import file...")) {
      const auto files = platform::openFiles(false);
      if (!files.empty()) mcpImportConfig(files.front());
    }
    ImGui::Dummy(ImVec2(0, 2));
    ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.textDim));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - 24.0f);
    ImGui::TextUnformatted(
        "Replies that may call tools go through Ollama's native tool calling, so for those turns the "
        "server's web search, knowledge files and Open WebUI tools are not applied. Tools ask for "
        "approval the first time; \"Always allow\" remembers the answer per tool.");
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
  }, width);

  if (servers.empty()) {
    emptyState("No MCP servers yet. Add one, or import your Claude Desktop or Cursor configuration.");
  }

  std::string removeId;
  for (const mcp::ServerConfig& config : servers) {
    const std::shared_ptr<mcp::Client> client = mcpClient(config.id);
    ServerUi& ui = gServerUi[config.id];
    const bool isConnected = client && client->connected();

    card(config.id.c_str(), [&] {
      theme::pushBold();
      ImGui::TextUnformatted(config.name.c_str());
      theme::pop();
      ImGui::SameLine();
      widgets::pill(config.transport, palette.panelAlt, palette.textDim);
      ImGui::SameLine();
      if (ui.connecting) {
        widgets::pill("connecting", palette.panelAlt, palette.warning);
      } else if (isConnected) {
        widgets::pill(std::to_string(client->tools().size()) + " tools", palette.panelAlt, palette.success);
      } else if (!ui.error.empty()) {
        widgets::pill("error", palette.panelAlt, palette.danger);
      } else {
        widgets::pill(config.enabled ? "disconnected" : "disabled", palette.panelAlt, palette.textFaint);
      }

      // Right-aligned actions.
      const float actionsWidth = 300.0f;
      ImGui::SameLine(std::max(ImGui::GetCursorPosX(), width - actionsWidth));
      if (isConnected || ui.connecting) {
        if (ImGui::Button("Disconnect", ImVec2(90, 0))) mcpDisconnect(config.id);
      } else {
        if (ImGui::Button("Connect", ImVec2(90, 0))) mcpConnect(config.id);
      }
      ImGui::SameLine();
      if (ImGui::Button("Edit", ImVec2(60, 0))) openEditor(config, false);
      ImGui::SameLine();
      if (widgets::dangerButton("Remove", ImVec2(70, 0))) removeId = config.id;

      theme::pushMono();
      ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.textDim));
      if (config.transport == "http") {
        ImGui::TextUnformatted(config.url.c_str());
      } else {
        ImGui::TextUnformatted((config.command + " " + joinArgs(config.args)).c_str());
      }
      ImGui::PopStyleColor();
      theme::pop();
      if (!ui.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.danger));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - 24.0f);
        ImGui::TextUnformatted(ui.error.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
      }
      if (isConnected) {
        const Json info = client->serverInfo();
        const std::string instructions = client->instructions();
        if (!info.empty()) {
          widgets::dimText("%s %s", info.value("name", "").c_str(), info.value("version", "").c_str());
        }
        if (!instructions.empty()) {
          ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.textDim));
          ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - 24.0f);
          ImGui::TextUnformatted(instructions.c_str());
          ImGui::PopTextWrapPos();
          ImGui::PopStyleColor();
        }
        if (client->toolsChanged()) {
          if (ImGui::SmallButton("Tools changed - refresh")) {
            async::run([client] { client->refreshTools(nullptr); });
          }
        }

        const std::vector<mcp::Tool> tools = client->tools();
        const std::string toolsLabel = "Tools (" + std::to_string(tools.size()) + ")";
        if (ImGui::CollapsingHeader(toolsLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
          for (const mcp::Tool& tool : tools) {
            ImGui::PushID(tool.name.c_str());
            const std::string qualified = mcp::qualifiedToolName(config.name, tool.name);
            theme::pushBold();
            ImGui::TextUnformatted(tool.name.c_str());
            theme::pop();
            ImGui::SameLine();
            const std::string policy = mcpApprovalFor(qualified);
            int policyIndex = policy == "always" ? 1 : policy == "never" ? 2 : 0;
            ImGui::SetNextItemWidth(90.0f);
            if (ImGui::Combo("##policy", &policyIndex, "Ask\0Always\0Never\0")) {
              setMcpApproval(qualified, policyIndex == 1 ? "always" : policyIndex == 2 ? "never" : "ask");
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(ui.tryTool == tool.name ? "Hide" : "Try")) {
              if (ui.tryTool == tool.name) {
                ui.tryTool.clear();
              } else {
                ui.tryTool = tool.name;
                ui.tryArgs = exampleArgs(tool.inputSchema).dump(2);
                ui.tryResult.clear();
              }
            }
            if (!tool.description.empty()) {
              ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.textDim));
              ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - 40.0f);
              ImGui::TextUnformatted(tool.description.c_str());
              ImGui::PopTextWrapPos();
              ImGui::PopStyleColor();
            }
            if (ui.tryTool == tool.name) {
              ImGui::Indent(12.0f);
              widgets::dimText("Arguments (JSON)");
              ImGui::InputTextMultiline("##tryargs", &ui.tryArgs, ImVec2(width - 60.0f, 90.0f));
              widgets::beginDisabled(ui.tryRunning);
              if (ImGui::Button(ui.tryRunning ? "Calling..." : "Call", ImVec2(90, 0))) {
                Json args = Json::parse(ui.tryArgs, nullptr, false);
                if (args.is_discarded() || !args.is_object()) {
                  widgets::toastError("Arguments must be a JSON object");
                } else {
                  ui.tryRunning = true;
                  const std::string serverId = config.id;
                  const std::string toolName = tool.name;
                  async::run([client, serverId, toolName, args] {
                    mcp::CallResult result = client->callTool(toolName, args, 120);
                    async::post([serverId, result] {
                      ServerUi& state = gServerUi[serverId];
                      state.tryRunning = false;
                      char header[64];
                      snprintf(header, sizeof(header), "%.0f ms%s\n", result.durationMs,
                               result.isError ? " - tool reported an error" : "");
                      state.tryResult = header + (result.ok ? result.text : "Call failed: " + result.error);
                    });
                  });
                }
              }
              widgets::endDisabled(ui.tryRunning);
              if (!ui.tryResult.empty()) {
                monoOutput("##tryresult", ui.tryResult, ImVec2(width - 60.0f, 120.0f), false);
              }
              ImGui::Unindent(12.0f);
            }
            ImGui::Dummy(ImVec2(0, 2));
            ImGui::PopID();
          }
        }

        const std::vector<mcp::Resource> resources = client->resources();
        if (!resources.empty()) {
          const std::string label = "Resources (" + std::to_string(resources.size()) + ")";
          if (ImGui::CollapsingHeader(label.c_str())) {
            for (const mcp::Resource& resource : resources) {
              ImGui::BulletText("%s", resource.name.empty() ? resource.uri.c_str() : resource.name.c_str());
              ImGui::SameLine();
              widgets::dimText("%s", resource.uri.c_str());
            }
          }
        }
        const std::vector<mcp::Prompt> prompts = client->prompts();
        if (!prompts.empty()) {
          const std::string label = "Prompts (" + std::to_string(prompts.size()) + ")";
          if (ImGui::CollapsingHeader(label.c_str())) {
            for (const mcp::Prompt& prompt : prompts) {
              ImGui::BulletText("%s", prompt.name.c_str());
              if (!prompt.description.empty()) {
                ImGui::SameLine();
                widgets::dimText("%s", prompt.description.c_str());
              }
            }
          }
        }
      }
      if (client) {
        const std::vector<std::string> log = client->log();
        if (!log.empty()) {
          const std::string label = "Log (" + std::to_string(log.size()) + ")";
          if (ImGui::CollapsingHeader(label.c_str())) {
            monoOutput("##log", core::join(log, "\n"), ImVec2(width - 40.0f, 140.0f), true);
          }
        }
      }
    }, width);
  }

  if (!removeId.empty()) {
    mcpDisconnect(removeId);
    servers.erase(std::remove_if(servers.begin(), servers.end(),
                                 [&](const mcp::ServerConfig& c) { return c.id == removeId; }),
                  servers.end());
    saveMcpServers(servers);
    gServerUi.erase(removeId);
  }

  // Add / edit modal.
  if (gShowEditor) {
    ImGui::OpenPopup("MCP server");
    gShowEditor = false;
  }
  ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
  if (ImGui::BeginPopupModal("MCP server", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::InputText("Name", &gEdit.name);
    ImGui::Combo("Transport", &gEditTransport, "stdio (spawn a command)\0http (Streamable HTTP)\0");
    if (gEditTransport == 0) {
      ImGui::InputText("Command", &gEdit.command);
      ImGui::InputText("Arguments", &gEditArgs);
      widgets::dimText("e.g. npx -y @modelcontextprotocol/server-filesystem ~/Documents");
      ImGui::InputTextMultiline("Environment", &gEditEnv, ImVec2(0, 70));
      widgets::dimText("KEY=VALUE per line");
    } else {
      ImGui::InputText("URL", &gEdit.url);
      ImGui::InputTextMultiline("Headers", &gEditHeaders, ImVec2(0, 70));
      widgets::dimText("Name: value per line, e.g. Authorization: Bearer ...");
    }
    ImGui::Checkbox("Enabled (connect at launch)", &gEdit.enabled);
    ImGui::Dummy(ImVec2(0, 6));
    const bool valid = !core::trim(gEdit.name).empty() &&
                       (gEditTransport == 0 ? !core::trim(gEdit.command).empty()
                                            : core::startsWith(gEdit.url, "http"));
    widgets::beginDisabled(!valid);
    if (widgets::primaryButton(gEditorIsNew ? "Add" : "Save", ImVec2(120, 0))) {
      gEdit.transport = gEditTransport == 0 ? "stdio" : "http";
      gEdit.args = splitArgs(gEditArgs);
      gEdit.env.clear();
      for (const auto& [k, v] : parsePairs(gEditEnv, '=')) gEdit.env[k] = v;
      gEdit.headers = parsePairs(gEditHeaders, ':');
      std::vector<mcp::ServerConfig> list = mcpServers();
      bool replaced = false;
      for (auto& entry : list) {
        if (entry.id == gEdit.id) {
          entry = gEdit;
          replaced = true;
        }
      }
      if (!replaced) list.push_back(gEdit);
      saveMcpServers(list);
      if (gEdit.enabled) {
        mcpConnect(gEdit.id);
      } else {
        mcpDisconnect(gEdit.id);
      }
      ImGui::CloseCurrentPopup();
    }
    widgets::endDisabled(!valid);
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
}

}  // namespace app
