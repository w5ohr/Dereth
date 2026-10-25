// Direct backends: talk to a model server without Open WebUI in between.
// Three dialects - Ollama (/api/chat), OpenAI-compatible chat completions
// (OpenAI, LM Studio, llama.cpp server, vLLM, OpenRouter, Groq, ...) and
// Anthropic Messages. Everything speaks the Ollama /api/chat NDJSON dialect
// to the rest of the app: a request is an Ollama-shaped payload and every
// streamed line is an Ollama-shaped chunk, so the tool loop, private chats
// and the usage log work unchanged whichever backend answers. API keys are
// never here - the caller fetches them from the keychain and passes them in.
// App-free; --backendstest covers the conversions.
#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace backends {

using Json = nlohmann::json;

enum class Kind { Ollama, OpenAI, Anthropic };

struct Backend {
  std::string id;      // stable, used as the keychain account suffix
  std::string name;    // what the user called it
  Kind kind = Kind::Ollama;
  std::string url;     // Ollama: http://host:11434; OpenAI: base incl. /v1; Anthropic: https://api.anthropic.com
};

Kind kindFrom(const std::string& text);        // "ollama" | "openai" | "anthropic"
std::string kindName(Kind kind);
std::string kindLabel(Kind kind);              // "Ollama", "OpenAI-compatible", "Anthropic"
std::string normalizeUrl(const std::string& url, Kind kind);   // trims, strips trailing slash, adds /v1 for OpenAI when missing
std::string newId();

std::vector<Backend> fromJson(const Json& list);
Json toJson(const std::vector<Backend>& list);

// Blocking. Model ids the backend offers.
std::vector<std::string> listModels(const Backend& backend, const std::string& key, std::string* error);

struct StreamResult {
  bool ok = false;
  long status = 0;
  std::string error;
};

// Blocking. Streams one chat request; `onLine` gets Ollama-shaped chunks
// ({"message":{"role","content","thinking","tool_calls"}}, "done",
// "prompt_eval_count", "eval_count" - or {"error": "..."}). Return false to stop.
StreamResult streamChat(const Backend& backend, const std::string& key, const Json& ollamaPayload,
                        const std::function<bool(const Json& line)>& onLine, const std::atomic<bool>* cancel);

// ---- conversions, exposed for the self-test --------------------------------

// Request bodies from an Ollama-shaped payload.
Json openaiRequest(const Json& ollamaPayload);
Json anthropicRequest(const Json& ollamaPayload);

// Turns OpenAI streamed chunks (one parsed `data:` object each) into Ollama
// chunks; `finish` flushes the tool calls and the final done chunk.
struct OpenAIDecoder {
  Json toolCalls = Json::array();   // accumulated by index
  Json usage;
  bool sawFinish = false;
  std::vector<Json> feed(const Json& chunk);
  std::vector<Json> finish();
};

// Turns Anthropic SSE events into Ollama chunks.
struct AnthropicDecoder {
  Json blocks = Json::object();     // index -> {type, id, name, json}
  int inputTokens = 0, outputTokens = 0;
  bool done = false;
  std::vector<Json> feed(const std::string& event, const Json& data);
};

}  // namespace backends
