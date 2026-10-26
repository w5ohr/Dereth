// Auto mode for tool calls, like Claude Code's: instead of asking about every
// call (or approving everything), each call is judged before it runs. Calls
// that only read run; calls that are clearly destructive, or that do not fit
// what the user asked for, are stopped and the user decides; the rest go to
// a short model call - the judge - that weighs the call against the user's
// own request (so a tool result cannot talk the assistant into something).
//
// This part is the logic: fixed rules, the danger scan, the judge's prompt
// and reading its answer. mcp_integration.cpp runs it in the approval hook.
#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace automode {

using Json = nlohmann::json;

enum class Mode { Ask, Auto, All };   // ask every time / auto mode / approve everything
Mode modeFrom(const std::string& text);
const char* modeName(Mode mode);       // "ask", "auto", "all"
const char* modeLabel(Mode mode);

struct Call {
  std::string serverId;        // "builtin:browser", "builtin:mac", "builtin:corpus", "builtin:scripts", "plugin:..", an MCP id
  std::string server, tool;    // display server name, the server's tool name
  std::string description;
  Json args = Json::object();
  Json annotations;            // MCP tool annotations (readOnlyHint, destructiveHint, ...) when the server gives them
  std::string userRequest;     // the user's last message, the only instruction that counts
  std::string projectFolder;   // the chat's project folder, "" when none
};

enum class Verdict { Allow, Ask, Block };
struct Judgement {
  Verdict verdict = Verdict::Ask;
  std::string reason;          // shown on the card and in the audit log
  bool fromJudge = false;      // the model decided (else a fixed rule)
};

// The fixed rules; Ask with an empty reason means "no rule applies - ask the judge".
Judgement rules(const Call& call);
// Something in the arguments that should never run unseen ("rm -rf", "sudo",
// "curl | sh", a write outside the project); "" when none.
std::string dangerIn(const Json& args, const std::string& projectFolder);
bool readOnlyTool(const Call& call);

// The judge: messages for a short completion, and its answer read back.
Json judgeMessages(const Call& call);
Judgement parseJudge(const std::string& reply);

// What to do with a judgement under the setting for risky calls.
Verdict apply(const Judgement& judgement, bool blockRisky);

}  // namespace automode
