// Generation metadata (#117): prompt, negative prompt, model, size, seed and
// engine stored in PNG text chunks (iTXt, UTF-8) and read back from ours,
// from Automatic1111's "parameters" chunk and from ComfyUI's "prompt" JSON;
// plus the gallery index (files, metadata, favourites). App-free;
// --imagemetatest covers it.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace imagemeta {

using Json = nlohmann::json;

struct Meta {
  std::string prompt, negative, model, size, seed, engine, created;   // all optional
  std::string parameters;   // the raw A1111 line when one was read
};

bool isPng(const std::string& bytes);
// Every text chunk (tEXt, zTXt, iTXt) as keyword -> text.
std::map<std::string, std::string> readText(const std::string& png);
// The PNG with these keywords written as iTXt chunks right after IHDR;
// existing chunks with the same keywords are dropped. "" when not a PNG.
std::string writeText(const std::string& png, const std::map<std::string, std::string>& text);
std::map<std::string, std::string> toText(const Meta& m);   // our keywords: prompt, negative_prompt, model, size, seed, engine, created, parameters
Meta fromText(const std::map<std::string, std::string>& text);
Meta read(const std::string& png);                // readText + fromText
std::string parametersLine(const Meta& m);        // A1111-style one-liner for copying
bool empty(const Meta& m);

struct Item {
  std::string file;        // file name inside the gallery folder
  Meta meta;
  bool favourite = false;
  int64_t created = 0;     // unix seconds
  int width = 0, height = 0;
};
std::vector<Item> itemsFromJson(const Json& j);
Json itemsToJson(const std::vector<Item>& items);
std::string fileNameFor(int64_t created, int n, const std::string& ext = "png");
// Newest first; `query` matches prompt / negative / model / seed, case-insensitively.
std::vector<Item> filter(const std::vector<Item>& items, const std::string& query, bool favouritesOnly);
bool pngSize(const std::string& png, int* width, int* height);   // from IHDR

}  // namespace imagemeta
