#include "imagemeta.h"

#include <algorithm>
#include <cctype>
#include <regex>

#include <zlib.h>

namespace imagemeta {
namespace {

const char kSignature[] = "\x89PNG\r\n\x1a\n";

uint32_t be32(const unsigned char* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]); }
void putBe32(std::string& out, uint32_t v) {
  out.push_back(static_cast<char>((v >> 24) & 0xff));
  out.push_back(static_cast<char>((v >> 16) & 0xff));
  out.push_back(static_cast<char>((v >> 8) & 0xff));
  out.push_back(static_cast<char>(v & 0xff));
}

struct Chunk {
  std::string type;
  std::string data;
};

std::vector<Chunk> chunks(const std::string& png) {
  std::vector<Chunk> out;
  if (!isPng(png)) return out;
  size_t pos = 8;
  while (pos + 12 <= png.size()) {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(png.data() + pos);
    const uint32_t len = be32(p);
    if (pos + 12 + len > png.size()) break;
    out.push_back(Chunk{png.substr(pos + 4, 4), png.substr(pos + 8, len)});
    pos += 12 + len;
  }
  return out;
}

std::string chunkBytes(const Chunk& c) {
  std::string out;
  putBe32(out, static_cast<uint32_t>(c.data.size()));
  out += c.type;
  out += c.data;
  const std::string crcInput = c.type + c.data;
  const uint32_t crc = static_cast<uint32_t>(::crc32(0L, reinterpret_cast<const Bytef*>(crcInput.data()), static_cast<uInt>(crcInput.size())));
  putBe32(out, crc);
  return out;
}

std::string inflate(const std::string& data) {
  std::string out;
  if (data.empty()) return out;
  z_stream zs{};
  if (inflateInit(&zs) != Z_OK) return out;
  zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
  zs.avail_in = static_cast<uInt>(data.size());
  char buf[8192];
  int rc;
  do {
    zs.next_out = reinterpret_cast<Bytef*>(buf);
    zs.avail_out = sizeof(buf);
    rc = ::inflate(&zs, Z_NO_FLUSH);
    if (rc != Z_OK && rc != Z_STREAM_END) break;
    out.append(buf, sizeof(buf) - zs.avail_out);
  } while (rc != Z_STREAM_END && out.size() < (64u << 20));
  inflateEnd(&zs);
  return out;
}

std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// A1111: "<prompt>\nNegative prompt: <neg>\nSteps: 20, Sampler: ..., Seed: 123, Size: 512x512, Model: x, ..."
void parseParameters(const std::string& text, Meta* m) {
  m->parameters = text;
  std::vector<std::string> lines;
  size_t start = 0;
  while (start <= text.size()) {
    const size_t nl = text.find('\n', start);
    lines.push_back(text.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
    if (nl == std::string::npos) break;
    start = nl + 1;
  }
  std::string prompt;
  for (const std::string& line : lines) {
    if (line.rfind("Negative prompt:", 0) == 0) {
      m->negative = trim(line.substr(16));
      continue;
    }
    if (line.find("Steps:") == 0 || line.find("Seed:") != std::string::npos && line.find(", ") != std::string::npos && line.find(':') < 12) {
      std::regex kv(R"(([A-Za-z ]+): ([^,]+))");
      for (auto it = std::sregex_iterator(line.begin(), line.end(), kv); it != std::sregex_iterator(); ++it) {
        const std::string key = lower(trim((*it)[1].str()));
        const std::string value = trim((*it)[2].str());
        if (key == "seed") m->seed = value;
        else if (key == "size") m->size = value;
        else if (key == "model") m->model = value;
      }
      continue;
    }
    if (!prompt.empty()) prompt += "\n";
    prompt += line;
  }
  if (m->prompt.empty()) m->prompt = trim(prompt);
}

// ComfyUI: the "prompt" chunk is the workflow's node graph as JSON.
void parseComfy(const std::string& text, Meta* m) {
  const Json j = Json::parse(text, nullptr, false);
  if (j.is_discarded() || !j.is_object()) return;
  m->engine = m->engine.empty() ? "comfyui" : m->engine;
  for (const auto& [id, node] : j.items()) {
    if (!node.is_object()) continue;
    const Json inputs = node.value("inputs", Json::object());
    const std::string type = node.value("class_type", "");
    if (inputs.contains("seed") && inputs["seed"].is_number() && m->seed.empty()) m->seed = std::to_string(inputs["seed"].get<long long>());
    if (inputs.contains("noise_seed") && inputs["noise_seed"].is_number() && m->seed.empty()) m->seed = std::to_string(inputs["noise_seed"].get<long long>());
    if (type == "CheckpointLoaderSimple" && inputs.contains("ckpt_name") && inputs["ckpt_name"].is_string() && m->model.empty()) m->model = inputs["ckpt_name"];
    if (type == "EmptyLatentImage" && inputs.contains("width") && inputs.contains("height") && m->size.empty()) {
      m->size = std::to_string(inputs.value("width", 0)) + "x" + std::to_string(inputs.value("height", 0));
    }
    if (type == "CLIPTextEncode" && inputs.contains("text") && inputs["text"].is_string()) {
      const std::string t = inputs["text"];
      if (m->prompt.empty()) m->prompt = t;
      else if (m->negative.empty() && t != m->prompt) m->negative = t;
    }
  }
}

}  // namespace

bool isPng(const std::string& bytes) { return bytes.size() >= 8 && bytes.compare(0, 8, kSignature, 8) == 0; }

std::map<std::string, std::string> readText(const std::string& png) {
  std::map<std::string, std::string> out;
  for (const Chunk& c : chunks(png)) {
    if (c.type == "tEXt") {
      const size_t nul = c.data.find('\0');
      if (nul == std::string::npos) continue;
      out[c.data.substr(0, nul)] = c.data.substr(nul + 1);
    } else if (c.type == "zTXt") {
      const size_t nul = c.data.find('\0');
      if (nul == std::string::npos || nul + 2 > c.data.size()) continue;
      out[c.data.substr(0, nul)] = inflate(c.data.substr(nul + 2));
    } else if (c.type == "iTXt") {
      const size_t nul = c.data.find('\0');
      if (nul == std::string::npos || nul + 3 > c.data.size()) continue;
      const bool compressed = c.data[nul + 1] == 1;
      size_t pos = nul + 3;
      const size_t langEnd = c.data.find('\0', pos);
      if (langEnd == std::string::npos) continue;
      const size_t transEnd = c.data.find('\0', langEnd + 1);
      if (transEnd == std::string::npos) continue;
      const std::string body = c.data.substr(transEnd + 1);
      out[c.data.substr(0, nul)] = compressed ? inflate(body) : body;
    }
  }
  return out;
}

std::string writeText(const std::string& png, const std::map<std::string, std::string>& text) {
  if (!isPng(png)) return "";
  std::string out(kSignature, 8);
  bool written = false;
  for (const Chunk& c : chunks(png)) {
    if (c.type == "tEXt" || c.type == "zTXt" || c.type == "iTXt") {
      const size_t nul = c.data.find('\0');
      if (nul != std::string::npos && text.count(c.data.substr(0, nul))) continue;   // replaced below
    }
    out += chunkBytes(c);
    if (c.type == "IHDR" && !written) {
      written = true;
      for (const auto& [key, value] : text) {
        if (key.empty() || key.size() > 79) continue;
        Chunk t;
        t.type = "iTXt";
        t.data = key;
        t.data.push_back('\0');
        t.data.push_back('\0');   // not compressed
        t.data.push_back('\0');   // compression method
        t.data.push_back('\0');   // language tag (empty)
        t.data.push_back('\0');   // translated keyword (empty)
        t.data += value;
        out += chunkBytes(t);
      }
    }
  }
  return written ? out : "";
}

std::map<std::string, std::string> toText(const Meta& m) {
  std::map<std::string, std::string> out;
  if (!m.prompt.empty()) out["prompt"] = m.prompt;
  if (!m.negative.empty()) out["negative_prompt"] = m.negative;
  if (!m.model.empty()) out["model"] = m.model;
  if (!m.size.empty()) out["size"] = m.size;
  if (!m.seed.empty()) out["seed"] = m.seed;
  if (!m.engine.empty()) out["engine"] = m.engine;
  if (!m.created.empty()) out["created"] = m.created;
  out["parameters"] = parametersLine(m);
  return out;
}

Meta fromText(const std::map<std::string, std::string>& text) {
  Meta m;
  auto take = [&](const char* key) {
    auto it = text.find(key);
    return it == text.end() ? std::string() : it->second;
  };
  m.prompt = take("prompt");
  m.negative = take("negative_prompt");
  m.model = take("model");
  m.size = take("size");
  m.seed = take("seed");
  m.engine = take("engine");
  m.created = take("created");
  // ComfyUI stores the graph under "prompt": JSON, not a sentence.
  if (!m.prompt.empty() && m.prompt[0] == '{') {
    const std::string graph = m.prompt;
    m.prompt.clear();
    parseComfy(graph, &m);
  }
  if (text.count("parameters")) {
    const std::string params = text.at("parameters");
    Meta a;
    parseParameters(params, &a);
    if (m.prompt.empty()) m.prompt = a.prompt;
    if (m.negative.empty()) m.negative = a.negative;
    if (m.seed.empty()) m.seed = a.seed;
    if (m.size.empty()) m.size = a.size;
    if (m.model.empty()) m.model = a.model;
    m.parameters = params;
    if (m.engine.empty() && params.find("Steps:") != std::string::npos) m.engine = "automatic1111";
  }
  return m;
}

Meta read(const std::string& png) { return fromText(readText(png)); }

std::string parametersLine(const Meta& m) {
  std::string out = m.prompt;
  if (!m.negative.empty()) out += "\nNegative prompt: " + m.negative;
  std::vector<std::string> parts;
  if (!m.seed.empty()) parts.push_back("Seed: " + m.seed);
  if (!m.size.empty()) parts.push_back("Size: " + m.size);
  if (!m.model.empty()) parts.push_back("Model: " + m.model);
  if (!m.engine.empty()) parts.push_back("Engine: " + m.engine);
  if (!parts.empty()) {
    out += "\n";
    for (size_t i = 0; i < parts.size(); ++i) out += (i ? ", " : "") + parts[i];
  }
  return out;
}

bool empty(const Meta& m) { return m.prompt.empty() && m.negative.empty() && m.model.empty() && m.seed.empty() && m.size.empty(); }

std::vector<Item> itemsFromJson(const Json& j) {
  std::vector<Item> out;
  if (!j.is_array()) return out;
  for (const auto& e : j) {
    if (!e.is_object() || !e.contains("file") || !e["file"].is_string()) continue;
    Item it;
    it.file = e["file"];
    it.favourite = e.value("favourite", false);
    it.created = e.value("created", static_cast<int64_t>(0));
    it.width = e.value("width", 0);
    it.height = e.value("height", 0);
    it.meta.prompt = e.value("prompt", "");
    it.meta.negative = e.value("negative", "");
    it.meta.model = e.value("model", "");
    it.meta.size = e.value("size", "");
    it.meta.seed = e.value("seed", "");
    it.meta.engine = e.value("engine", "");
    it.meta.created = e.value("createdText", "");
    it.meta.parameters = e.value("parameters", "");
    out.push_back(it);
  }
  return out;
}

Json itemsToJson(const std::vector<Item>& items) {
  Json out = Json::array();
  for (const Item& it : items) {
    out.push_back(Json{{"file", it.file},
                       {"favourite", it.favourite},
                       {"created", it.created},
                       {"width", it.width},
                       {"height", it.height},
                       {"prompt", it.meta.prompt},
                       {"negative", it.meta.negative},
                       {"model", it.meta.model},
                       {"size", it.meta.size},
                       {"seed", it.meta.seed},
                       {"engine", it.meta.engine},
                       {"createdText", it.meta.created},
                       {"parameters", it.meta.parameters}});
  }
  return out;
}

std::string fileNameFor(int64_t created, int n, const std::string& ext) { return std::to_string(created) + "-" + std::to_string(n) + "." + ext; }

std::vector<Item> filter(const std::vector<Item>& items, const std::string& query, bool favouritesOnly) {
  std::vector<Item> out;
  const std::string q = lower(trim(query));
  for (const Item& it : items) {
    if (favouritesOnly && !it.favourite) continue;
    if (!q.empty()) {
      const std::string hay = lower(it.meta.prompt + "\n" + it.meta.negative + "\n" + it.meta.model + "\n" + it.meta.seed + "\n" + it.file);
      if (hay.find(q) == std::string::npos) continue;
    }
    out.push_back(it);
  }
  std::stable_sort(out.begin(), out.end(), [](const Item& a, const Item& b) { return a.created > b.created; });
  return out;
}

bool pngSize(const std::string& png, int* width, int* height) {
  if (!isPng(png) || png.size() < 24 || png.compare(12, 4, "IHDR") != 0) return false;
  const unsigned char* p = reinterpret_cast<const unsigned char*>(png.data());
  if (width) *width = static_cast<int>(be32(p + 16));
  if (height) *height = static_cast<int>(be32(p + 20));
  return true;
}

}  // namespace imagemeta
