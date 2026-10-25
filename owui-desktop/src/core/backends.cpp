#include "backends.h"

#include <algorithm>
#include <cctype>
#include <random>

#include "common.h"
#include "http.h"

namespace backends {
namespace {

constexpr const char* kAnthropicVersion = "2023-06-01";

std::string bearer(const std::string& key) { return "Bearer " + key; }

std::string argumentsText(const Json& arguments) {
  if (arguments.is_string()) return arguments.get<std::string>();
  if (arguments.is_null()) return "{}";
  return arguments.dump();
}

Json argumentsObject(const std::string& text) {
  Json parsed = Json::parse(text.empty() ? "{}" : text, nullptr, false);
  if (parsed.is_discarded() || !parsed.is_object()) return Json::object();
  return parsed;
}

// Ollama tool_calls carry no ids; the OpenAI and Anthropic dialects need them.
// Ids are handed out per assistant message, and the tool messages that follow
// take them in order - the order the tool loop produces.
struct IdQueue {
  std::vector<std::string> pending;
  int counter = 0;
  std::string next() { return "call_" + std::to_string(++counter); }
  std::string take() {
    if (pending.empty()) return next();
    std::string id = pending.front();
    pending.erase(pending.begin());
    return id;
  }
};

std::string imageMime(const std::string& base64) {
  if (core::startsWith(base64, "/9j/")) return "image/jpeg";
  if (core::startsWith(base64, "R0lG")) return "image/gif";
  if (core::startsWith(base64, "UklG")) return "image/webp";
  return "image/png";
}

Json ollamaChunk(const std::string& content, const std::string& thinking) {
  Json message{{"role", "assistant"}, {"content", content}};
  if (!thinking.empty()) message["thinking"] = thinking;
  return Json{{"message", message}, {"done", false}};
}

// SSE over net::stream, keeping whatever did not parse as an event (an error
// body is plain JSON with no blank line after it) so a failure can be explained.
net::Response sseStream(const net::Request& req,
                        const std::function<bool(const std::string& event, const std::string& data)>& onEvent,
                        const std::atomic<bool>* cancel, std::string* tail) {
  std::string buffer;
  auto flush = [&](const std::string& block) {
    std::string event, data;
    for (const std::string& rawLine : core::split(block, '\n')) {
      std::string line = rawLine;
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.empty() || line[0] == ':') continue;
      const size_t colon = line.find(':');
      std::string field = colon == std::string::npos ? line : line.substr(0, colon);
      std::string value = colon == std::string::npos ? "" : line.substr(colon + 1);
      if (!value.empty() && value[0] == ' ') value.erase(0, 1);
      if (field == "event") event = value;
      else if (field == "data") data += (data.empty() ? "" : "\n") + value;
    }
    if (data.empty()) return true;
    return onEvent(event, data);
  };
  net::Response res = net::stream(
      req,
      [&](const char* data, size_t len) {
        buffer.append(data, len);
        size_t pos;
        while ((pos = buffer.find("\n\n")) != std::string::npos) {
          const std::string block = buffer.substr(0, pos);
          buffer.erase(0, pos + 2);
          if (!flush(block)) return false;
        }
        return true;
      },
      cancel);
  if (!buffer.empty() && buffer.find("data:") != std::string::npos) flush(buffer);
  if (tail) *tail = buffer;
  return res;
}

Json errorLine(long status, const std::string& body, const std::string& transport) {
  std::string text = transport;
  if (text.empty()) {
    Json j = Json::parse(body, nullptr, false);
    if (!j.is_discarded() && j.is_object()) {
      if (j.contains("error")) {
        const Json& e = j["error"];
        text = e.is_string() ? e.get<std::string>() : e.value("message", e.dump());
      } else if (j.contains("message")) {
        text = j["message"].is_string() ? j["message"].get<std::string>() : j.dump();
      } else if (j.contains("detail")) {
        text = j["detail"].is_string() ? j["detail"].get<std::string>() : j.dump();
      }
    }
    if (text.empty()) text = core::trim(body).substr(0, 300);
    if (text.empty()) text = "HTTP " + std::to_string(status);
    else text = "HTTP " + std::to_string(status) + ": " + text;
  }
  return Json{{"error", text}};
}

}  // namespace

// ---------------------------------------------------------------- registry

Kind kindFrom(const std::string& text) {
  const std::string k = core::lower(core::trim(text));
  if (k == "openai" || k == "openai-compatible") return Kind::OpenAI;
  if (k == "anthropic" || k == "claude") return Kind::Anthropic;
  return Kind::Ollama;
}

std::string kindName(Kind kind) {
  switch (kind) {
    case Kind::OpenAI: return "openai";
    case Kind::Anthropic: return "anthropic";
    default: return "ollama";
  }
}

std::string kindLabel(Kind kind) {
  switch (kind) {
    case Kind::OpenAI: return "OpenAI-compatible";
    case Kind::Anthropic: return "Anthropic";
    default: return "Ollama";
  }
}

std::string normalizeUrl(const std::string& raw, Kind kind) {
  std::string url = core::trim(raw);
  while (!url.empty() && url.back() == '/') url.pop_back();
  if (url.empty()) return url;
  if (url.find("://") == std::string::npos) url = "http://" + url;
  if (kind == Kind::OpenAI) {
    // "http://localhost:1234" -> ".../v1"; a base that already ends in a
    // version segment (v1, v1beta, openai/v1) is kept.
    const size_t slash = url.rfind('/');
    const std::string last = slash == std::string::npos ? "" : url.substr(slash + 1);
    if (!(last.size() >= 2 && last[0] == 'v' && std::isdigit(static_cast<unsigned char>(last[1])))) url += "/v1";
  }
  if (kind == Kind::Anthropic && core::endsWith(url, "/v1")) url.erase(url.size() - 3);
  return url;
}

std::string newId() {
  static const char* kAlphabet = "abcdefghijklmnopqrstuvwxyz0123456789";
  std::random_device rd;
  std::string out = "b";
  for (int i = 0; i < 8; ++i) out += kAlphabet[rd() % 36];
  return out;
}

std::vector<Backend> fromJson(const Json& list) {
  std::vector<Backend> out;
  if (!list.is_array()) return out;
  for (const auto& j : list) {
    if (!j.is_object()) continue;
    Backend b;
    b.id = j.value("id", "");
    b.name = j.value("name", "");
    b.kind = kindFrom(j.value("kind", "ollama"));
    b.url = normalizeUrl(j.value("url", ""), b.kind);
    if (b.id.empty() || b.url.empty()) continue;
    if (b.name.empty()) b.name = b.url;
    out.push_back(b);
  }
  return out;
}

Json toJson(const std::vector<Backend>& list) {
  Json out = Json::array();
  for (const Backend& b : list) {
    out.push_back(Json{{"id", b.id}, {"name", b.name}, {"kind", kindName(b.kind)}, {"url", b.url}});
  }
  return out;
}

// ---------------------------------------------------------------- models

std::vector<std::string> listModels(const Backend& backend, const std::string& key, std::string* error) {
  std::vector<std::string> out;
  net::Request req;
  req.timeoutSec = 20;
  switch (backend.kind) {
    case Kind::Ollama:
      req.url = backend.url + "/api/tags";
      if (!key.empty()) req.headers.push_back({"Authorization", bearer(key)});
      break;
    case Kind::OpenAI:
      req.url = backend.url + "/models";
      if (!key.empty()) req.headers.push_back({"Authorization", bearer(key)});
      break;
    case Kind::Anthropic:
      req.url = backend.url + "/v1/models?limit=100";
      req.headers.push_back({"x-api-key", key});
      req.headers.push_back({"anthropic-version", kAnthropicVersion});
      break;
  }
  net::Response res = net::perform(req);
  if (!res.error.empty() || res.status < 200 || res.status >= 300) {
    if (error) *error = errorLine(res.status, res.body, res.error)["error"];
    return out;
  }
  Json j = Json::parse(res.body, nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    if (error) *error = "The server did not answer with JSON";
    return out;
  }
  const Json& items = backend.kind == Kind::Ollama ? j["models"] : j["data"];
  if (items.is_array()) {
    for (const auto& m : items) {
      if (!m.is_object()) continue;
      const std::string id = backend.kind == Kind::Ollama ? m.value("name", m.value("model", "")) : m.value("id", "");
      if (!id.empty()) out.push_back(id);
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}

// ---------------------------------------------------------------- OpenAI

Json openaiRequest(const Json& p) {
  Json body{{"model", p.value("model", "")}, {"stream", true}, {"stream_options", Json{{"include_usage", true}}}};
  Json messages = Json::array();
  IdQueue ids;
  for (const auto& m : p.value("messages", Json::array())) {
    if (!m.is_object()) continue;
    const std::string role = m.value("role", "user");
    Json out{{"role", role}};
    const std::string text = m.value("content", "");
    if (m.contains("images") && m["images"].is_array() && !m["images"].empty()) {
      Json parts = Json::array();
      if (!text.empty()) parts.push_back(Json{{"type", "text"}, {"text", text}});
      for (const auto& img : m["images"]) {
        if (!img.is_string()) continue;
        const std::string b64 = img.get<std::string>();
        parts.push_back(Json{{"type", "image_url"},
                             {"image_url", Json{{"url", "data:" + imageMime(b64) + ";base64," + b64}}}});
      }
      out["content"] = parts;
    } else {
      out["content"] = text;
    }
    if (role == "assistant" && m.contains("tool_calls") && m["tool_calls"].is_array()) {
      Json calls = Json::array();
      ids.pending.clear();
      for (const auto& c : m["tool_calls"]) {
        const Json fn = c.value("function", Json::object());
        const std::string id = c.value("id", "");
        const std::string useId = id.empty() ? ids.next() : id;
        ids.pending.push_back(useId);
        calls.push_back(Json{{"id", useId},
                             {"type", "function"},
                             {"function", Json{{"name", fn.value("name", "")}, {"arguments", argumentsText(fn.value("arguments", Json::object()))}}}});
      }
      out["tool_calls"] = calls;
      if (text.empty()) out["content"] = nullptr;
    }
    if (role == "tool") out["tool_call_id"] = m.value("tool_call_id", ids.take());
    messages.push_back(out);
  }
  body["messages"] = messages;
  if (p.contains("tools") && p["tools"].is_array() && !p["tools"].empty()) body["tools"] = p["tools"];
  const Json options = p.value("options", Json::object());
  static const char* kSame[] = {"temperature", "top_p", "seed", "presence_penalty", "frequency_penalty", "stop"};
  for (const char* key : kSame) {
    if (options.contains(key) && !options[key].is_null()) body[key] = options[key];
  }
  if (options.contains("num_predict") && options["num_predict"].is_number() && options["num_predict"].get<int>() > 0) {
    body["max_tokens"] = options["num_predict"];
  }
  if (p.contains("format") && !p["format"].is_null()) {
    const Json& format = p["format"];
    if (format.is_string() && format == "json") {
      body["response_format"] = Json{{"type", "json_object"}};
    } else if (format.is_object()) {
      body["response_format"] = Json{{"type", "json_schema"},
                                     {"json_schema", Json{{"name", "response"}, {"schema", format}}}};
    }
  }
  // Grammar and tool-choice controls ride through as is (llama.cpp-style
  // servers read `grammar`; everyone OpenAI-shaped reads `tool_choice`).
  for (const char* key : {"grammar", "tool_choice"}) {
    if (p.contains(key) && !p[key].is_null()) body[key] = p[key];
  }
  return body;
}

std::vector<Json> OpenAIDecoder::feed(const Json& chunk) {
  std::vector<Json> out;
  if (!chunk.is_object()) return out;
  if (chunk.contains("error")) {
    const Json& e = chunk["error"];
    out.push_back(Json{{"error", e.is_string() ? e.get<std::string>() : e.value("message", e.dump())}});
    return out;
  }
  if (chunk.contains("usage") && chunk["usage"].is_object()) usage = chunk["usage"];
  const Json choices = chunk.value("choices", Json::array());
  if (!choices.is_array() || choices.empty()) return out;
  const Json& choice = choices[0];
  const Json delta = choice.value("delta", Json::object());
  std::string content, thinking;
  if (delta.contains("content") && delta["content"].is_string()) content = delta["content"];
  for (const char* key : {"reasoning_content", "reasoning"}) {
    if (delta.contains(key) && delta[key].is_string()) thinking += delta[key].get<std::string>();
  }
  if (!content.empty() || !thinking.empty()) out.push_back(ollamaChunk(content, thinking));
  if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
    for (const auto& tc : delta["tool_calls"]) {
      const int index = tc.value("index", static_cast<int>(toolCalls.size()));
      while (static_cast<int>(toolCalls.size()) <= index) {
        toolCalls.push_back(Json{{"id", ""}, {"name", ""}, {"arguments", ""}});
      }
      Json& slot = toolCalls[index];
      if (tc.contains("id") && tc["id"].is_string() && !tc["id"].get<std::string>().empty()) slot["id"] = tc["id"];
      const Json fn = tc.value("function", Json::object());
      if (fn.contains("name") && fn["name"].is_string()) slot["name"] = slot["name"].get<std::string>() + fn["name"].get<std::string>();
      if (fn.contains("arguments") && fn["arguments"].is_string()) {
        slot["arguments"] = slot["arguments"].get<std::string>() + fn["arguments"].get<std::string>();
      }
    }
  }
  if (choice.contains("finish_reason") && !choice["finish_reason"].is_null()) sawFinish = true;
  return out;
}

std::vector<Json> OpenAIDecoder::finish() {
  std::vector<Json> out;
  Json message{{"role", "assistant"}, {"content", ""}};
  if (!toolCalls.empty()) {
    Json calls = Json::array();
    for (const auto& slot : toolCalls) {
      Json call{{"function", Json{{"name", slot.value("name", "")}, {"arguments", argumentsObject(slot.value("arguments", ""))}}}};
      if (!slot.value("id", "").empty()) call["id"] = slot["id"];
      calls.push_back(call);
    }
    message["tool_calls"] = calls;
  }
  Json line{{"message", message}, {"done", true}, {"done_reason", "stop"}};
  if (usage.is_object()) {
    line["prompt_eval_count"] = usage.value("prompt_tokens", 0);
    line["eval_count"] = usage.value("completion_tokens", 0);
  }
  out.push_back(line);
  toolCalls = Json::array();
  return out;
}

// ---------------------------------------------------------------- Anthropic

Json anthropicRequest(const Json& p) {
  Json body{{"model", p.value("model", "")}, {"stream", true}};
  const Json options = p.value("options", Json::object());
  int maxTokens = 4096;
  if (options.contains("num_predict") && options["num_predict"].is_number() && options["num_predict"].get<int>() > 0) {
    maxTokens = options["num_predict"];
  }
  body["max_tokens"] = maxTokens;
  for (const char* key : {"temperature", "top_p", "top_k"}) {
    if (options.contains(key) && options[key].is_number()) body[key] = options[key];
  }
  if (options.contains("stop")) {
    body["stop_sequences"] = options["stop"].is_array() ? options["stop"] : Json::array({options["stop"]});
  }

  std::string system;
  Json messages = Json::array();
  IdQueue ids;
  auto appendToolResult = [&](const Json& block) {
    // Consecutive tool results share one user turn.
    if (!messages.empty() && messages.back()["role"] == "user" && messages.back()["content"].is_array() &&
        !messages.back()["content"].empty() && messages.back()["content"].back().value("type", "") == "tool_result") {
      messages.back()["content"].push_back(block);
    } else {
      messages.push_back(Json{{"role", "user"}, {"content", Json::array({block})}});
    }
  };
  for (const auto& m : p.value("messages", Json::array())) {
    if (!m.is_object()) continue;
    const std::string role = m.value("role", "user");
    const std::string text = m.value("content", "");
    if (role == "system") {
      if (!system.empty()) system += "\n\n";
      system += text;
      continue;
    }
    if (role == "tool") {
      appendToolResult(Json{{"type", "tool_result"}, {"tool_use_id", m.value("tool_call_id", ids.take())}, {"content", text}});
      continue;
    }
    Json content = Json::array();
    if (role == "user" && m.contains("images") && m["images"].is_array()) {
      for (const auto& img : m["images"]) {
        if (!img.is_string()) continue;
        const std::string b64 = img.get<std::string>();
        content.push_back(Json{{"type", "image"},
                               {"source", Json{{"type", "base64"}, {"media_type", imageMime(b64)}, {"data", b64}}}});
      }
    }
    if (!text.empty()) content.push_back(Json{{"type", "text"}, {"text", text}});
    if (role == "assistant" && m.contains("tool_calls") && m["tool_calls"].is_array()) {
      ids.pending.clear();
      for (const auto& c : m["tool_calls"]) {
        const Json fn = c.value("function", Json::object());
        const std::string id = c.value("id", "");
        const std::string useId = id.empty() ? ids.next() : id;
        ids.pending.push_back(useId);
        Json input = fn.value("arguments", Json::object());
        if (input.is_string()) input = argumentsObject(input.get<std::string>());
        content.push_back(Json{{"type", "tool_use"}, {"id", useId}, {"name", fn.value("name", "")}, {"input", input}});
      }
    }
    if (content.empty()) content.push_back(Json{{"type", "text"}, {"text", "(empty)"}});
    // Anthropic wants strict alternation: merge same-role neighbours.
    if (!messages.empty() && messages.back()["role"] == role) {
      for (const auto& block : content) messages.back()["content"].push_back(block);
    } else {
      messages.push_back(Json{{"role", role}, {"content", content}});
    }
  }
  if (!system.empty()) body["system"] = system;
  body["messages"] = messages;
  if (p.contains("tools") && p["tools"].is_array() && !p["tools"].empty()) {
    Json tools = Json::array();
    for (const auto& t : p["tools"]) {
      const Json fn = t.value("function", Json::object());
      tools.push_back(Json{{"name", fn.value("name", "")},
                           {"description", fn.value("description", "")},
                           {"input_schema", fn.value("parameters", Json{{"type", "object"}, {"properties", Json::object()}})}});
    }
    body["tools"] = tools;
    if (p.contains("tool_choice") && !p["tool_choice"].is_null()) {
      const Json& tc = p["tool_choice"];
      if (tc.is_string() && tc == "required") body["tool_choice"] = Json{{"type", "any"}};
      else if (tc.is_string() && tc == "auto") body["tool_choice"] = Json{{"type", "auto"}};
      else if (tc.is_object() && tc.contains("function")) body["tool_choice"] = Json{{"type", "tool"}, {"name", tc["function"].value("name", "")}};
    }
  }
  return body;
}

std::vector<Json> AnthropicDecoder::feed(const std::string& eventIn, const Json& data) {
  std::vector<Json> out;
  // libcurl's SSE helper names unnamed events "message"; Anthropic puts the type in the data too.
  const std::string event = (eventIn.empty() || eventIn == "message") && data.is_object() ? data.value("type", eventIn) : eventIn;
  if (event == "error") {
    const Json e = data.value("error", Json::object());
    out.push_back(Json{{"error", e.value("message", data.dump())}});
    return out;
  }
  if (event == "message_start") {
    inputTokens = data.value("message", Json::object()).value("usage", Json::object()).value("input_tokens", 0);
  } else if (event == "content_block_start") {
    const std::string index = std::to_string(data.value("index", 0));
    const Json block = data.value("content_block", Json::object());
    blocks[index] = Json{{"type", block.value("type", "text")}, {"id", block.value("id", "")}, {"name", block.value("name", "")}, {"json", ""}};
    if (block.value("type", "") == "text" && !block.value("text", "").empty()) out.push_back(ollamaChunk(block["text"], ""));
  } else if (event == "content_block_delta") {
    const std::string index = std::to_string(data.value("index", 0));
    const Json delta = data.value("delta", Json::object());
    const std::string type = delta.value("type", "");
    if (type == "text_delta") {
      out.push_back(ollamaChunk(delta.value("text", ""), ""));
    } else if (type == "thinking_delta") {
      out.push_back(ollamaChunk("", delta.value("thinking", "")));
    } else if (type == "input_json_delta") {
      if (!blocks.contains(index)) blocks[index] = Json{{"type", "tool_use"}, {"id", ""}, {"name", ""}, {"json", ""}};
      blocks[index]["json"] = blocks[index].value("json", "") + delta.value("partial_json", "");
    }
  } else if (event == "message_delta") {
    outputTokens = data.value("usage", Json::object()).value("output_tokens", outputTokens);
  } else if (event == "message_stop") {
    Json message{{"role", "assistant"}, {"content", ""}};
    Json calls = Json::array();
    for (const auto& [index, block] : blocks.items()) {
      if (block.value("type", "") != "tool_use") continue;
      calls.push_back(Json{{"id", block.value("id", "")},
                           {"function", Json{{"name", block.value("name", "")}, {"arguments", argumentsObject(block.value("json", ""))}}}});
    }
    if (!calls.empty()) message["tool_calls"] = calls;
    out.push_back(Json{{"message", message},
                       {"done", true},
                       {"done_reason", "stop"},
                       {"prompt_eval_count", inputTokens},
                       {"eval_count", outputTokens}});
    done = true;
  }
  return out;
}

// ---------------------------------------------------------------- streaming

StreamResult streamChat(const Backend& backend, const std::string& key, const Json& payload,
                        const std::function<bool(const Json& line)>& onLine, const std::atomic<bool>* cancel) {
  StreamResult result;
  net::Request req;
  req.method = "POST";
  req.timeoutSec = 0;
  std::string errorBody;
  bool stopped = false;
  auto emit = [&](const Json& line) {
    if (stopped) return false;
    if (!onLine(line)) stopped = true;
    return !stopped;
  };

  if (backend.kind == Kind::Ollama) {
    Json body = payload;
    body["stream"] = true;
    body.erase("grammar");       // Ollama has neither; the tool loop's system line stands in
    body.erase("tool_choice");
    req.url = backend.url + "/api/chat";
    req.body = body.dump();
    if (!key.empty()) req.headers.push_back({"Authorization", bearer(key)});
    std::string buffer;
    net::Response res = net::stream(
        req,
        [&](const char* data, size_t len) {
          buffer.append(data, len);
          size_t pos;
          while ((pos = buffer.find('\n')) != std::string::npos) {
            const std::string line = core::trim(buffer.substr(0, pos));
            buffer.erase(0, pos + 1);
            if (line.empty()) continue;
            Json j = Json::parse(line, nullptr, false);
            if (!j.is_discarded() && !emit(j)) return false;
          }
          return true;
        },
        cancel);
    if (!core::trim(buffer).empty()) {
      Json j = Json::parse(buffer, nullptr, false);
      if (!j.is_discarded()) emit(j);
      else errorBody = buffer;
    }
    result.status = res.status;
    result.ok = res.error.empty() && res.status >= 200 && res.status < 300;
    if (!result.ok) {
      result.error = errorLine(res.status, errorBody, res.error)["error"];
      if (!stopped) emit(Json{{"error", result.error}});
    }
    return result;
  }

  if (backend.kind == Kind::OpenAI) {
    req.url = backend.url + "/chat/completions";
    req.body = openaiRequest(payload).dump();
    if (!key.empty()) req.headers.push_back({"Authorization", bearer(key)});
    OpenAIDecoder decoder;
    bool finished = false;
    std::string tail;
    net::Response res = sseStream(
        req,
        [&](const std::string&, const std::string& data) {
          const std::string trimmed = core::trim(data);
          if (trimmed.empty()) return true;
          if (trimmed == "[DONE]") {
            finished = true;
            for (const Json& line : decoder.finish()) {
              if (!emit(line)) return false;
            }
            return true;
          }
          Json chunk = Json::parse(trimmed, nullptr, false);
          if (chunk.is_discarded()) return true;
          for (const Json& line : decoder.feed(chunk)) {
            if (!emit(line)) return false;
          }
          return true;
        },
        cancel, &tail);
    result.status = res.status;
    result.ok = res.error.empty() && res.status >= 200 && res.status < 300;
    if (!result.ok) {
      result.error = errorLine(res.status, tail, res.error)["error"];
      if (!stopped) emit(Json{{"error", result.error}});
    } else if (!finished && !stopped) {
      // A server that closes without [DONE] still gets its tool calls and usage flushed.
      for (const Json& line : decoder.finish()) {
        if (!emit(line)) break;
      }
    }
    return result;
  }

  // Anthropic
  req.url = backend.url + "/v1/messages";
  req.body = anthropicRequest(payload).dump();
  req.headers.push_back({"x-api-key", key});
  req.headers.push_back({"anthropic-version", kAnthropicVersion});
  AnthropicDecoder decoder;
  std::string tail;
  net::Response res = sseStream(
      req,
      [&](const std::string& event, const std::string& data) {
        Json j = Json::parse(core::trim(data), nullptr, false);
        if (j.is_discarded()) return true;
        for (const Json& line : decoder.feed(event, j)) {
          if (!emit(line)) return false;
        }
        return true;
      },
      cancel, &tail);
  result.status = res.status;
  result.ok = res.error.empty() && res.status >= 200 && res.status < 300;
  if (!result.ok) {
    result.error = errorLine(res.status, tail, res.error)["error"];
    if (!stopped) emit(Json{{"error", result.error}});
  } else if (!decoder.done && !stopped) {
    for (const Json& line : decoder.feed("message_stop", Json::object())) {
      if (!emit(line)) break;
    }
  }
  return result;
}

}  // namespace backends
