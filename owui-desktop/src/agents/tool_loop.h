// The tool-calling loop: ask the model, run the tool calls it makes, feed the
// results back, repeat until it answers in prose. UI-agnostic on purpose - the
// app drives it through hooks and the self-test drives it headless.
//
// Tool-enabled turns bypass Open WebUI's chat pipeline and talk to Ollama's
// native tool calling through the server's /ollama/api/chat passthrough (or
// straight to Ollama when the passthrough is unavailable), because the
// pipeline would try to run our client-side tools itself.
#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "api/client.h"

namespace agents {

using Json = nlohmann::json;

// One tool as the model sees it, plus where it really lives.
struct ToolSpec {
  std::string name;          // qualified "<server>__<tool>" the model calls
  std::string description;
  Json parameters;           // JSON schema
  std::string serverId;
  std::string serverName;
  std::string tool;          // the server's own tool name
};

// A streaming chat transport in the Ollama /api/chat dialect: payload in,
// one parsed NDJSON chunk per onLine call. Return false from onLine to stop.
using Transport = std::function<api::Result(const Json& payload, const std::function<bool(const Json&)>& onLine,
                                            const std::atomic<bool>* cancel)>;

struct Call {
  std::string id;
  std::string name;          // qualified name
  Json args = Json::object();
};

struct ToolOutcome {
  std::string text;          // what the model gets back
  bool isError = false;      // the tool ran and reported failure
  std::string error;         // the call itself failed (unknown tool, transport)
  double durationMs = 0.0;
};

enum CallState { Pending = 0, Running = 1, Done = 2, Failed = 3, Denied = 4 };
enum Decision { Allow = 1, AllowAlways = 2, Deny = 3 };

struct LoopConfig {
  std::string model;         // an Ollama model name
  Json messages;             // OpenAI-shaped chat history (buildCompletionPayload output)
  std::vector<ToolSpec> tools;
  Json options = Json::object();   // Ollama options (temperature, num_ctx, ...)
  std::string systemPrefix;        // prepended as a system message when set
  int maxRounds = 8;
  int maxToolResultChars = 24000;
  Json format;                     // Ollama `format`: "json" or a JSON schema; null = free text
  Json extra = Json::object();     // more top-level fields (grammar, tool_choice); tool_choice only on the first round
  // When set, every request goes through this instead of the server passthrough
  // / direct Ollama pair: a direct backend (core/backends) speaking the same
  // Ollama NDJSON dialect. Nothing then touches the Open WebUI server.
  Transport transport;
};

struct LoopHooks {
  // Streamed text. Return false to stop the generation.
  std::function<bool(const std::string& content, const std::string& reasoning)> onDelta;
  // Blocks until the user decides; returns a Decision. Absent = allow.
  std::function<int(const Call&)> approve;
  // Runs the tool. Blocks.
  std::function<ToolOutcome(const Call&)> invoke;
  // Progress for the UI; `outcome` is set for Done/Failed/Denied.
  std::function<void(const Call&, CallState, const ToolOutcome* outcome)> onCallState;
  std::function<void(const Json& usage)> onUsage;
  // Token log-probabilities for streamed content (Ollama's chunk["logprobs"]
  // entries); thinking tokens are not passed.
  std::function<void(const Json& entries)> onLogprobs;
};

struct LoopResult {
  bool ok = false;
  std::string error;
  int rounds = 0;
  int toolCalls = 0;
  Json usage = Json::object();
};

LoopResult runToolLoop(api::Client& api, const std::string& directOllamaUrl, const LoopConfig& config,
                       const LoopHooks& hooks, const std::atomic<bool>* cancel);

// Helpers, exposed for the self-test.
Json toOllamaMessages(const Json& openaiMessages);
Json toOllamaTools(const std::vector<ToolSpec>& tools);
Json usageFromOllama(const Json& finalChunk);
// Translates Open WebUI chat params (max_tokens, ...) to Ollama option names.
Json ollamaOptionsFromParams(const Json& params);

// Pulls <think>...</think> out of a token stream, tags split across chunks included.
struct ThinkSplitter {
  bool inThink = false;
  std::string carry;
  void feed(const std::string& delta, std::string* content, std::string* reasoning);
  void flush(std::string* content, std::string* reasoning);
};

}  // namespace agents
