// Grammar and tool-choice controls (#80): per-chat output mode (free text,
// JSON object, GBNF grammar) and tool choice (auto, none, required, one
// named tool). The pure parts: the record, a GBNF validator, the wire
// shapes for OpenAI- and Anthropic-style servers, the prompt nudge for
// backends without a tool_choice, and the Ollama-payload application.
// App-free; --outputtest covers it.
#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace outputctl {

using Json = nlohmann::json;

struct Controls {
  std::string mode = "free";        // "free" | "json" | "grammar"
  std::string grammar;              // GBNF text, used when mode == "grammar"
  std::string toolChoice = "auto";  // "auto" | "none" | "required" | "tool:<name>"
};
Controls fromJson(const Json& raw);   // raw["outputControls"]
Json toJson(const Controls& c);
bool active(const Controls& c);       // anything besides free text + auto

// "" when the grammar parses: every rule is `name ::= body`, `root` exists,
// every referenced rule is defined, quotes and brackets balance per rule.
std::string validateGrammar(const std::string& gbnf);
int ruleCount(const std::string& gbnf);

std::string forcedTool(const std::string& choice);     // "x" for "tool:x", else ""
Json openaiToolChoice(const std::string& choice);      // "auto" / "none" / "required" / {type:function,function:{name}}
Json anthropicToolChoice(const std::string& choice);   // {type:auto} / {type:any} / {type:tool,name}; null for none
// The instruction added for backends that have no tool_choice (Ollama) and
// for JSON mode on models that only follow the prompt. "" when nothing applies.
std::string nudge(const Controls& c, const std::vector<std::string>& toolNames);
// Ollama-dialect payload (the tool loop's): format "json", `grammar` and
// `tool_choice` fields for the OpenAI/Anthropic mappers, tools dropped for "none".
void applyToOllamaPayload(Json& payload, const Controls& c);
std::string summary(const Controls& c);   // "JSON", "GBNF (3 rules)", "tool: calc__add", ...

}  // namespace outputctl
