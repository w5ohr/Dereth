// `Open WebUI --backendstest`: offline checks of the direct-backend
// conversions (Ollama dialect <-> OpenAI chat completions / Anthropic Messages).
#include <cstdio>
#include <string>

#include "core/backends.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
}  // namespace

int runBackendsSelfTest() {
  using namespace backends;
  printf("Open WebUI desktop direct-backends self-test\n\n");

  // Registry.
  report("normalizeUrl adds /v1 for OpenAI, keeps a version segment, strips slashes",
         normalizeUrl("localhost:1234/", Kind::OpenAI) == "http://localhost:1234/v1" &&
             normalizeUrl("https://openrouter.ai/api/v1/", Kind::OpenAI) == "https://openrouter.ai/api/v1" &&
             normalizeUrl("http://host:11434/", Kind::Ollama) == "http://host:11434" &&
             normalizeUrl("https://api.anthropic.com/v1", Kind::Anthropic) == "https://api.anthropic.com");
  Json list = Json::array({Json{{"id", "b1"}, {"name", "Studio"}, {"kind", "openai"}, {"url", "http://localhost:1234"}},
                           Json{{"id", ""}, {"url", "x"}}, Json{{"id", "b2"}, {"kind", "anthropic"}, {"url", "https://api.anthropic.com"}}});
  std::vector<Backend> parsed = fromJson(list);
  report("fromJson drops broken rows, names unnamed ones after the URL",
         parsed.size() == 2 && parsed[0].kind == Kind::OpenAI && parsed[0].url == "http://localhost:1234/v1" &&
             parsed[1].name == "https://api.anthropic.com");
  report("toJson round-trip", fromJson(toJson(parsed)).size() == 2 && toJson(parsed)[1]["kind"] == "anthropic");

  // An Ollama-shaped tool-loop payload: system, user with image, assistant tool call, tool result, options, tools.
  Json payload{{"model", "m"},
               {"stream", true},
               {"messages", Json::array({Json{{"role", "system"}, {"content", "Be brief."}},
                                         Json{{"role", "user"}, {"content", "What is in the picture?"}, {"images", Json::array({"iVBORw0KGgo="})}},
                                         Json{{"role", "assistant"}, {"content", ""}, {"tool_calls", Json::array({Json{{"function", Json{{"name", "calc__add"}, {"arguments", Json{{"a", 1}, {"b", 2}}}}}}})}},
                                         Json{{"role", "tool"}, {"content", "3"}, {"tool_name", "calc__add"}},
                                         Json{{"role", "assistant"}, {"content", "It is 3."}}})},
               {"tools", Json::array({Json{{"type", "function"}, {"function", Json{{"name", "calc__add"}, {"description", "adds"}, {"parameters", Json{{"type", "object"}, {"properties", Json{{"a", Json{{"type", "number"}}}}}}}}}}})},
               {"options", Json{{"temperature", 0.2}, {"num_predict", 300}, {"num_ctx", 8192}, {"stop", Json::array({"END"})}}},
               {"format", Json{{"type", "object"}, {"properties", Json::object()}}}};

  // OpenAI request.
  Json oa = openaiRequest(payload);
  const Json& oam = oa["messages"];
  report("openai: system kept, image becomes a data URL part",
         oam.size() == 5 && oam[0]["role"] == "system" && oam[1]["content"].is_array() && oam[1]["content"].size() == 2 &&
             oam[1]["content"][1]["image_url"]["url"].get<std::string>().rfind("data:image/png;base64,", 0) == 0);
  report("openai: tool call gets an id and string arguments, the tool result the same id",
         oam[2]["tool_calls"][0]["id"] == "call_1" && oam[2]["tool_calls"][0]["function"]["arguments"] == "{\"a\":1,\"b\":2}" &&
             oam[2]["content"].is_null() && oam[3]["tool_call_id"] == "call_1" && oam[3]["role"] == "tool");
  report("openai: options map (max_tokens, stop, temperature; num_ctx dropped), schema -> json_schema, usage requested",
         oa["max_tokens"] == 300 && oa["temperature"] == 0.2 && oa["stop"][0] == "END" && !oa.contains("num_ctx") &&
             oa["response_format"]["type"] == "json_schema" && oa["stream_options"]["include_usage"] == true && oa["tools"].size() == 1);
  Json jsonMode = payload;
  jsonMode["format"] = "json";
  report("openai: format json -> json_object", openaiRequest(jsonMode)["response_format"]["type"] == "json_object");

  // OpenAI stream decode: text, a tool call split over chunks, usage, [DONE].
  OpenAIDecoder dec;
  std::vector<Json> lines;
  auto feed = [&](const Json& chunk) { for (const Json& l : dec.feed(chunk)) lines.push_back(l); };
  feed(Json{{"choices", Json::array({Json{{"delta", Json{{"role", "assistant"}, {"content", "Let me "}}}}})}});
  feed(Json{{"choices", Json::array({Json{{"delta", Json{{"reasoning_content", "hmm"}, {"content", "add."}}}}})}});
  feed(Json{{"choices", Json::array({Json{{"delta", Json{{"tool_calls", Json::array({Json{{"index", 0}, {"id", "call_x"}, {"function", Json{{"name", "calc__add"}, {"arguments", "{\"a\":"}}}}})}}}}})}});
  feed(Json{{"choices", Json::array({Json{{"delta", Json{{"tool_calls", Json::array({Json{{"index", 0}, {"function", Json{{"arguments", "1,\"b\":2}"}}}}})}}}, {"finish_reason", "tool_calls"}}})}});
  feed(Json{{"choices", Json::array()}, {"usage", Json{{"prompt_tokens", 40}, {"completion_tokens", 7}}}});
  for (const Json& l : dec.finish()) lines.push_back(l);
  report("openai decode: content and reasoning become Ollama chunks",
         lines.size() == 3 && lines[0]["message"]["content"] == "Let me " && lines[1]["message"]["content"] == "add." &&
             lines[1]["message"]["thinking"] == "hmm");
  report("openai decode: tool call arguments reassembled as an object, usage mapped, done",
         lines[2]["done"] == true && lines[2]["message"]["tool_calls"][0]["function"]["name"] == "calc__add" &&
             lines[2]["message"]["tool_calls"][0]["function"]["arguments"]["b"] == 2 && lines[2]["prompt_eval_count"] == 40 &&
             lines[2]["eval_count"] == 7);
  OpenAIDecoder errDec;
  std::vector<Json> err = errDec.feed(Json{{"error", Json{{"message", "invalid api key"}}}});
  report("openai decode: error object becomes an error line", err.size() == 1 && err[0]["error"] == "invalid api key");

  // Anthropic request.
  Json an = anthropicRequest(payload);
  const Json& anm = an["messages"];
  report("anthropic: system lifted out, image block, tool_use and tool_result blocks share ids, user/assistant alternate",
         an["system"] == "Be brief." && anm.size() == 4 && anm[0]["content"][0]["type"] == "image" &&
             anm[0]["content"][0]["source"]["media_type"] == "image/png" && anm[1]["content"][0]["type"] == "tool_use" &&
             anm[1]["content"][0]["id"] == "call_1" && anm[1]["content"][0]["input"]["a"] == 1 && anm[2]["role"] == "user" &&
             anm[2]["content"][0]["type"] == "tool_result" && anm[2]["content"][0]["tool_use_id"] == "call_1" &&
             anm[3]["content"][0]["text"] == "It is 3.");
  report("anthropic: max_tokens from num_predict, stop_sequences, tools as input_schema, no stream_options",
         an["max_tokens"] == 300 && an["stop_sequences"][0] == "END" && an["tools"][0]["input_schema"]["type"] == "object" &&
             an["tools"][0]["name"] == "calc__add" && !an.contains("stream_options") && an["stream"] == true);
  Json noMax = payload;
  noMax["options"] = Json::object();
  report("anthropic: max_tokens defaults to 4096", anthropicRequest(noMax)["max_tokens"] == 4096);

  // Anthropic stream decode.
  AnthropicDecoder ad;
  std::vector<Json> al;
  auto afeed = [&](const std::string& ev, const Json& d) { for (const Json& l : ad.feed(ev, d)) al.push_back(l); };
  afeed("message_start", Json{{"type", "message_start"}, {"message", Json{{"usage", Json{{"input_tokens", 25}}}}}});
  afeed("content_block_start", Json{{"index", 0}, {"content_block", Json{{"type", "thinking"}}}});
  afeed("content_block_delta", Json{{"index", 0}, {"delta", Json{{"type", "thinking_delta"}, {"thinking", "plan"}}}});
  afeed("content_block_start", Json{{"index", 1}, {"content_block", Json{{"type", "text"}, {"text", ""}}}});
  afeed("content_block_delta", Json{{"index", 1}, {"delta", Json{{"type", "text_delta"}, {"text", "Adding"}}}});
  afeed("content_block_start", Json{{"index", 2}, {"content_block", Json{{"type", "tool_use"}, {"id", "toolu_1"}, {"name", "calc__add"}}}});
  afeed("content_block_delta", Json{{"index", 2}, {"delta", Json{{"type", "input_json_delta"}, {"partial_json", "{\"a\": 1,"}}}});
  afeed("content_block_delta", Json{{"index", 2}, {"delta", Json{{"type", "input_json_delta"}, {"partial_json", " \"b\": 2}"}}}});
  afeed("message_delta", Json{{"usage", Json{{"output_tokens", 9}}}});
  afeed("message", Json{{"type", "message_stop"}});   // unnamed event: type read from the data
  report("anthropic decode: thinking and text chunks",
         al.size() == 3 && al[0]["message"]["thinking"] == "plan" && al[1]["message"]["content"] == "Adding");
  report("anthropic decode: tool_use input reassembled, usage mapped, done on message_stop",
         al[2]["done"] == true && al[2]["message"]["tool_calls"][0]["function"]["name"] == "calc__add" &&
             al[2]["message"]["tool_calls"][0]["function"]["arguments"]["b"] == 2 && al[2]["message"]["tool_calls"][0]["id"] == "toolu_1" &&
             al[2]["prompt_eval_count"] == 25 && al[2]["eval_count"] == 9);
  AnthropicDecoder aerr;
  std::vector<Json> ae = aerr.feed("error", Json{{"type", "error"}, {"error", Json{{"type", "authentication_error"}, {"message", "invalid x-api-key"}}}});
  report("anthropic decode: error event becomes an error line", ae.size() == 1 && ae[0]["error"] == "invalid x-api-key");

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
