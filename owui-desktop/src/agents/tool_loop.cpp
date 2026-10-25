#include "agents/tool_loop.h"

#include "core/common.h"
#include "core/http.h"

namespace agents {
namespace {

// Streams one /api/chat call, preferring the Open WebUI passthrough (it carries
// the user's auth and model access rules) and falling back to Ollama directly
// when the passthrough yields nothing at all.
api::Result streamChat(api::Client& api, const std::string& directUrl, const Json& payload,
                       const std::function<bool(const Json&)>& onLine, const std::atomic<bool>* cancel,
                       bool* sawLine) {
  bool any = false;
  auto counted = [&](const Json& line) {
    any = true;
    return onLine(line);
  };
  api::Result res = api.ollamaChat(payload, counted, cancel);
  if (!res.ok && !any && !directUrl.empty() && !(cancel && cancel->load())) {
    core::debugLog("tool loop: passthrough failed (%s), trying %s directly", res.error.c_str(),
                   directUrl.c_str());
    net::Request req;
    req.method = "POST";
    req.url = directUrl + "/api/chat";
    req.body = payload.dump();
    std::string buffer;
    net::Response direct = net::stream(
        req,
        [&](const char* data, size_t len) {
          buffer.append(data, len);
          size_t pos;
          while ((pos = buffer.find('\n')) != std::string::npos) {
            const std::string line = core::trim(buffer.substr(0, pos));
            buffer.erase(0, pos + 1);
            if (line.empty()) continue;
            Json j = Json::parse(line, nullptr, false);
            if (!j.is_discarded() && !counted(j)) return false;
          }
          return true;
        },
        cancel);
    api::Result out;
    out.status = direct.status;
    out.ok = direct.error.empty() && direct.status >= 200 && direct.status < 300;
    if (!out.ok) {
      out.error = res.error + "; direct: " +
                  (direct.error.empty() ? "HTTP " + std::to_string(direct.status) : direct.error);
    }
    res = out;
  }
  *sawLine = any;
  return res;
}

std::string truncateForModel(const std::string& text, int limit) {
  if (limit <= 0 || static_cast<int>(text.size()) <= limit) return text;
  return text.substr(0, static_cast<size_t>(limit)) + "\n...[truncated " +
         std::to_string(text.size() - static_cast<size_t>(limit)) + " chars]";
}

}  // namespace

// ---------------------------------------------------------------- helpers

Json toOllamaMessages(const Json& openaiMessages) {
  Json out = Json::array();
  if (!openaiMessages.is_array()) return out;
  for (const auto& m : openaiMessages) {
    if (!m.is_object()) continue;
    Json entry{{"role", m.value("role", "user")}};
    const Json& content = m.contains("content") ? m["content"] : Json("");
    if (content.is_string()) {
      entry["content"] = content;
    } else if (content.is_array()) {
      std::string text;
      Json images = Json::array();
      for (const auto& part : content) {
        if (!part.is_object()) continue;
        const std::string type = part.value("type", "");
        if (type == "text") {
          if (!text.empty()) text += "\n";
          text += part.value("text", "");
        } else if (type == "image_url") {
          const std::string url = part.value("image_url", Json::object()).value("url", "");
          // Ollama takes raw base64; only inline data URLs can be forwarded.
          const size_t comma = url.find(',');
          if (core::startsWith(url, "data:") && comma != std::string::npos) {
            images.push_back(url.substr(comma + 1));
          }
        }
      }
      entry["content"] = text;
      if (!images.empty()) entry["images"] = images;
    } else {
      entry["content"] = content.is_null() ? "" : content.dump();
    }
    if (m.contains("tool_calls")) entry["tool_calls"] = m["tool_calls"];
    if (m.contains("tool_name")) entry["tool_name"] = m["tool_name"];
    out.push_back(entry);
  }
  return out;
}

Json toOllamaTools(const std::vector<ToolSpec>& tools) {
  Json out = Json::array();
  for (const ToolSpec& tool : tools) {
    Json schema = tool.parameters.is_object() ? tool.parameters : Json::object();
    if (!schema.contains("type")) schema["type"] = "object";
    if (!schema.contains("properties")) schema["properties"] = Json::object();
    out.push_back(Json{{"type", "function"},
                       {"function", Json{{"name", tool.name},
                                         {"description", tool.description},
                                         {"parameters", schema}}}});
  }
  return out;
}

Json usageFromOllama(const Json& chunk) {
  Json usage = Json::object();
  if (!chunk.is_object()) return usage;
  const int prompt = chunk.value("prompt_eval_count", 0);
  const int completion = chunk.value("eval_count", 0);
  usage["prompt_tokens"] = prompt;
  usage["completion_tokens"] = completion;
  usage["total_tokens"] = prompt + completion;
  const double evalNs = chunk.value("eval_duration", 0.0);
  if (evalNs > 0 && completion > 0) usage["response_token/s"] = completion / (evalNs / 1e9);
  return usage;
}

Json ollamaOptionsFromParams(const Json& params) {
  Json options = Json::object();
  if (!params.is_object()) return options;
  static const char* kPassThrough[] = {"temperature", "top_p",       "top_k",      "min_p",
                                       "seed",        "num_ctx",     "num_predict", "repeat_penalty",
                                       "stop",        "num_gpu",     "num_thread", "presence_penalty",
                                       "frequency_penalty"};
  for (const char* key : kPassThrough) {
    if (params.contains(key) && !params[key].is_null()) options[key] = params[key];
  }
  if (params.contains("max_tokens") && params["max_tokens"].is_number()) {
    options["num_predict"] = params["max_tokens"];
  }
  return options;
}

void ThinkSplitter::feed(const std::string& delta, std::string* content, std::string* reasoning) {
  carry += delta;
  while (!carry.empty()) {
    const std::string tag = inThink ? "</think>" : "<think>";
    const size_t at = carry.find(tag);
    if (at != std::string::npos) {
      (inThink ? *reasoning : *content) += carry.substr(0, at);
      carry.erase(0, at + tag.size());
      inThink = !inThink;
      continue;
    }
    // Hold back a trailing fragment that could be the start of the tag.
    size_t keep = 0;
    for (size_t n = std::min(carry.size(), tag.size() - 1); n > 0; --n) {
      if (carry.compare(carry.size() - n, n, tag, 0, n) == 0) {
        keep = n;
        break;
      }
    }
    (inThink ? *reasoning : *content) += carry.substr(0, carry.size() - keep);
    carry.erase(0, carry.size() - keep);
    break;
  }
}

void ThinkSplitter::flush(std::string* content, std::string* reasoning) {
  (inThink ? *reasoning : *content) += carry;
  carry.clear();
}

// ---------------------------------------------------------------- the loop

LoopResult runToolLoop(api::Client& api, const std::string& directOllamaUrl, const LoopConfig& config,
                       const LoopHooks& hooks, const std::atomic<bool>* cancel) {
  LoopResult result;
  auto cancelled = [&] { return cancel && cancel->load(); };

  Json messages = toOllamaMessages(config.messages);
  if (!config.systemPrefix.empty()) {
    messages.insert(messages.begin(), Json{{"role", "system"}, {"content", config.systemPrefix}});
  }
  const Json tools = toOllamaTools(config.tools);
  Json usageTotal{{"prompt_tokens", 0}, {"completion_tokens", 0}, {"total_tokens", 0}};
  int callCounter = 0;

  for (int round = 0; round < config.maxRounds; ++round) {
    result.rounds = round + 1;
    if (cancelled()) {
      result.ok = true;
      break;
    }
    Json payload{{"model", config.model}, {"messages", messages}, {"stream", true}};
    if (!tools.empty()) payload["tools"] = tools;
    if (config.options.is_object() && !config.options.empty()) payload["options"] = config.options;
    if (!config.format.is_null()) payload["format"] = config.format;

    std::string roundContent;
    Json toolCalls = Json::array();
    Json finalChunk;
    std::string streamError;
    ThinkSplitter splitter;

    auto deliver = [&](const std::string& content, const std::string& reasoning) {
      roundContent += content;
      if (content.empty() && reasoning.empty()) return true;
      return !hooks.onDelta || hooks.onDelta(content, reasoning);
    };

    bool sawLine = false;
    api::Result res = streamChat(
        api, directOllamaUrl, payload,
        [&](const Json& line) {
          if (line.contains("error")) {
            streamError = line["error"].is_string() ? line["error"].get<std::string>() : line["error"].dump();
            return false;
          }
          if (line.contains("detail")) {   // Open WebUI's own error shape
            streamError = line["detail"].is_string() ? line["detail"].get<std::string>() : line["detail"].dump();
            return false;
          }
          const Json message = line.value("message", Json::object());
          std::string content;
          std::string reasoning = message.value("thinking", "");
          splitter.feed(message.value("content", ""), &content, &reasoning);
          if (!deliver(content, reasoning)) return false;
          if (message.contains("tool_calls") && message["tool_calls"].is_array()) {
            for (const auto& call : message["tool_calls"]) toolCalls.push_back(call);
          }
          if (line.value("done", false)) finalChunk = line;
          return !cancelled();
        },
        cancel, &sawLine);
    {
      std::string content, reasoning;
      splitter.flush(&content, &reasoning);
      deliver(content, reasoning);
    }

    if (!streamError.empty()) {
      result.error = streamError;
      return result;
    }
    if (!res.ok && !sawLine) {
      result.error = res.error.empty() ? "HTTP " + std::to_string(res.status) : res.error;
      return result;
    }
    if (finalChunk.is_object()) {
      const Json usage = usageFromOllama(finalChunk);
      usageTotal["prompt_tokens"] = usageTotal["prompt_tokens"].get<int>() + usage.value("prompt_tokens", 0);
      usageTotal["completion_tokens"] =
          usageTotal["completion_tokens"].get<int>() + usage.value("completion_tokens", 0);
      usageTotal["total_tokens"] =
          usageTotal["prompt_tokens"].get<int>() + usageTotal["completion_tokens"].get<int>();
      if (usage.contains("response_token/s")) usageTotal["response_token/s"] = usage["response_token/s"];
    }

    if (toolCalls.empty() || cancelled()) {
      result.ok = true;
      break;
    }

    // The model narrated, then called tools; keep its answer visually apart.
    if (!roundContent.empty() && hooks.onDelta) hooks.onDelta("\n\n", "");

    messages.push_back(Json{{"role", "assistant"}, {"content", roundContent}, {"tool_calls", toolCalls}});
    for (const auto& raw : toolCalls) {
      const Json fn = raw.value("function", Json::object());
      Call call;
      call.id = "call_" + std::to_string(++callCounter);
      call.name = fn.value("name", "");
      if (fn.contains("arguments")) {
        if (fn["arguments"].is_object()) {
          call.args = fn["arguments"];
        } else if (fn["arguments"].is_string()) {
          Json parsed = Json::parse(fn["arguments"].get<std::string>(), nullptr, false);
          if (!parsed.is_discarded() && parsed.is_object()) call.args = parsed;
        }
      }
      result.toolCalls++;

      if (hooks.onCallState) hooks.onCallState(call, Pending, nullptr);
      int decision = hooks.approve ? hooks.approve(call) : Allow;
      if (cancelled()) decision = Deny;

      ToolOutcome outcome;
      if (decision == Deny) {
        outcome.isError = true;
        outcome.text = "The user declined to run this tool. Do not call it again; continue without it.";
        if (hooks.onCallState) hooks.onCallState(call, Denied, &outcome);
      } else {
        if (hooks.onCallState) hooks.onCallState(call, Running, nullptr);
        const double started = core::nowMillis();
        outcome = hooks.invoke ? hooks.invoke(call) : ToolOutcome{"", true, "no tool runner", 0.0};
        if (outcome.durationMs <= 0.0) outcome.durationMs = core::nowMillis() - started;
        if (hooks.onCallState) hooks.onCallState(call, outcome.error.empty() ? Done : Failed, &outcome);
      }

      std::string text = outcome.text;
      if (!outcome.error.empty()) {
        text = "Tool call failed: " + outcome.error;
      } else if (outcome.isError && decision != Deny) {
        text = "The tool reported an error: " + text;
      }
      if (text.empty()) text = "(the tool returned nothing)";
      messages.push_back(Json{{"role", "tool"},
                              {"content", truncateForModel(text, config.maxToolResultChars)},
                              {"tool_name", call.name}});
    }

    if (round + 1 == config.maxRounds) {
      result.ok = true;
      result.error = "Stopped after " + std::to_string(config.maxRounds) + " tool rounds";
      if (hooks.onDelta) hooks.onDelta("\n\n_(stopped after " + std::to_string(config.maxRounds) + " tool rounds)_", "");
    }
  }

  result.usage = usageTotal;
  if (hooks.onUsage) hooks.onUsage(usageTotal);
  return result;
}

}  // namespace agents
