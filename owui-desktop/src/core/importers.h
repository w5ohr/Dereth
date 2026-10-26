// Importers: other assistants' chat exports turned into Open WebUI chat
// documents (the body `POST /api/v1/chats/new` takes). ChatGPT's
// conversations.json (a mapping tree; the current branch is followed) and
// Claude's conversations.json (a flat chat_messages list). App-free;
// --importtest covers both with sample documents.
#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace importers {

using Json = nlohmann::json;

enum class Source { Unknown, ChatGPT, Claude };

struct Imported {
  std::string title;
  int64_t createdAt = 0;        // unix seconds
  Json chat;                    // {title, models, history{messages, currentId}, messages, tags}
  int messages = 0;
};

Source detect(const Json& document);
// Converts every conversation in the document; `model` is the model id the
// imported chats are tagged with (the source's own model name is kept as
// modelName when the export carries one).
std::vector<Imported> convert(const Json& document, Source source, const std::string& model);

const char* sourceName(Source source);

}  // namespace importers
