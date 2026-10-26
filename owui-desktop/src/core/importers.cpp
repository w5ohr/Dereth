#include "importers.h"

#include <algorithm>
#include <ctime>
#include <map>

#include "common.h"

namespace importers {
namespace {

struct Turn {
  std::string role;       // user | assistant
  std::string content;
  std::string model;      // as the source names it, may be empty
  int64_t at = 0;
};

int64_t parseIso(const std::string& iso) {
  // "2024-05-01T12:34:56.789Z" or with an offset; seconds precision is enough.
  if (iso.size() < 19) return 0;
  std::tm tm{};
  if (!strptime(iso.substr(0, 19).c_str(), "%Y-%m-%dT%H:%M:%S", &tm)) return 0;
  return static_cast<int64_t>(timegm(&tm));
}

Imported assemble(const std::string& title, int64_t createdAt, const std::vector<Turn>& turns,
                  const std::string& model, const char* tag) {
  Imported out;
  out.title = title.empty() ? "Imported chat" : title;
  out.createdAt = createdAt;
  Json messages = Json::object();
  Json linear = Json::array();
  std::string previous;
  std::vector<std::string> ids;
  for (const Turn& t : turns) {
    if (core::trim(t.content).empty()) continue;
    const std::string id = core::uuid();
    Json m{{"id", id},
           {"parentId", previous.empty() ? Json(nullptr) : Json(previous)},
           {"childrenIds", Json::array()},
           {"role", t.role},
           {"content", t.content},
           {"timestamp", t.at ? t.at : createdAt},
           {"done", true}};
    if (t.role == "assistant") {
      m["model"] = model;
      m["modelName"] = t.model.empty() ? model : t.model;
      m["modelIdx"] = 0;
    }
    if (!previous.empty()) messages[previous]["childrenIds"].push_back(id);
    messages[id] = m;
    linear.push_back(m);
    ids.push_back(id);
    previous = id;
  }
  out.messages = static_cast<int>(ids.size());
  out.chat = Json{{"title", out.title},
                  {"models", Json::array({model})},
                  {"params", Json::object()},
                  {"history", Json{{"messages", messages}, {"currentId", previous.empty() ? Json(nullptr) : Json(previous)}}},
                  {"messages", linear},
                  {"tags", Json::array({tag})},
                  {"timestamp", createdAt * 1000}};
  return out;
}

// ChatGPT: follow current_node up to the root for the branch that was shown.
std::vector<Imported> convertChatGpt(const Json& document, const std::string& model) {
  std::vector<Imported> out;
  if (!document.is_array()) return out;
  for (const Json& conv : document) {
    if (!conv.is_object() || !conv.contains("mapping") || !conv["mapping"].is_object()) continue;
    const Json& mapping = conv["mapping"];
    std::string node = conv.value("current_node", "");
    if (node.empty() || !mapping.contains(node)) {
      // No current node: pick a leaf (a node without children).
      for (const auto& [id, n] : mapping.items()) {
        if (n.is_object() && (!n.contains("children") || n["children"].empty())) node = id;
      }
    }
    std::vector<Turn> turns;
    int guard = 0;
    while (!node.empty() && mapping.contains(node) && guard++ < 10000) {
      const Json& n = mapping[node];
      const Json& message = n.contains("message") && n["message"].is_object() ? n["message"] : Json();
      if (message.is_object()) {
        const std::string role = message.value("author", Json::object()).value("role", "");
        const Json& content = message.contains("content") ? message["content"] : Json();
        std::string text;
        if (content.is_object()) {
          const std::string type = content.value("content_type", "text");
          if (content.contains("parts") && content["parts"].is_array()) {
            for (const auto& part : content["parts"]) {
              if (part.is_string()) text += part.get<std::string>();
              else if (part.is_object() && part.contains("text") && part["text"].is_string()) text += part["text"].get<std::string>();
            }
          } else if (content.contains("text") && content["text"].is_string()) {
            text = content["text"];
          }
          if (type == "code" && !text.empty()) text = "```\n" + text + "\n```";
        }
        const bool hidden = message.value("metadata", Json::object()).value("is_visually_hidden_from_conversation", false);
        if ((role == "user" || role == "assistant") && !hidden && !core::trim(text).empty()) {
          Turn t;
          t.role = role;
          t.content = text;
          t.model = message.value("metadata", Json::object()).value("model_slug", "");
          if (message.contains("create_time") && message["create_time"].is_number()) t.at = static_cast<int64_t>(message["create_time"].get<double>());
          turns.push_back(t);
        }
      }
      node = n.contains("parent") && n["parent"].is_string() ? n["parent"].get<std::string>() : "";
    }
    std::reverse(turns.begin(), turns.end());
    if (turns.empty()) continue;
    int64_t created = 0;
    if (conv.contains("create_time") && conv["create_time"].is_number()) created = static_cast<int64_t>(conv["create_time"].get<double>());
    if (!created && !turns.empty()) created = turns.front().at;
    out.push_back(assemble(conv.value("title", ""), created, turns, model, "imported-chatgpt"));
  }
  return out;
}

std::vector<Imported> convertClaude(const Json& document, const std::string& model) {
  std::vector<Imported> out;
  if (!document.is_array()) return out;
  for (const Json& conv : document) {
    if (!conv.is_object() || !conv.contains("chat_messages") || !conv["chat_messages"].is_array()) continue;
    std::vector<Turn> turns;
    for (const Json& m : conv["chat_messages"]) {
      if (!m.is_object()) continue;
      const std::string sender = m.value("sender", "");
      std::string text = m.value("text", "");
      if (text.empty() && m.contains("content") && m["content"].is_array()) {
        for (const auto& part : m["content"]) {
          if (part.is_object() && part.value("type", "") == "text") text += part.value("text", "");
        }
      }
      if (core::trim(text).empty()) continue;
      Turn t;
      t.role = sender == "human" ? "user" : "assistant";
      t.content = text;
      t.at = parseIso(m.value("created_at", ""));
      turns.push_back(t);
    }
    if (turns.empty()) continue;
    int64_t created = parseIso(conv.value("created_at", ""));
    if (!created) created = turns.front().at;
    out.push_back(assemble(conv.value("name", ""), created, turns, model, "imported-claude"));
  }
  return out;
}

}  // namespace

Source detect(const Json& document) {
  if (!document.is_array() || document.empty()) return Source::Unknown;
  for (const Json& item : document) {
    if (!item.is_object()) continue;
    if (item.contains("mapping")) return Source::ChatGPT;
    if (item.contains("chat_messages")) return Source::Claude;
  }
  return Source::Unknown;
}

std::vector<Imported> convert(const Json& document, Source source, const std::string& model) {
  switch (source) {
    case Source::ChatGPT: return convertChatGpt(document, model);
    case Source::Claude: return convertClaude(document, model);
    default: return {};
  }
}

const char* sourceName(Source source) {
  switch (source) {
    case Source::ChatGPT: return "ChatGPT";
    case Source::Claude: return "Claude";
    default: return "unknown";
  }
}

}  // namespace importers
