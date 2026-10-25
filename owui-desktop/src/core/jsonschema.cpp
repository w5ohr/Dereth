#include "jsonschema.h"

#include <cmath>
#include <regex>
#include <set>

namespace schema {
namespace {

struct Context {
  const Json* root = nullptr;
  std::vector<std::string> errors;
  int depth = 0;
};

std::string typeName(const Json& value) {
  if (value.is_null()) return "null";
  if (value.is_boolean()) return "boolean";
  if (value.is_string()) return "string";
  if (value.is_array()) return "array";
  if (value.is_object()) return "object";
  if (value.is_number_integer() || value.is_number_unsigned()) return "integer";
  return "number";
}

bool matchesType(const Json& value, const std::string& type) {
  if (type == "integer") {
    if (value.is_number_integer() || value.is_number_unsigned()) return true;
    return value.is_number_float() && std::floor(value.get<double>()) == value.get<double>();
  }
  if (type == "number") return value.is_number();
  return typeName(value) == type;
}

std::string brief(const Json& value) {
  std::string s = value.dump();
  if (s.size() > 40) s = s.substr(0, 37) + "...";
  return s;
}

// Resolves "#/$defs/name" (or definitions) against the root document.
const Json* resolveRef(const Context& ctx, const std::string& ref) {
  if (!ctx.root || ref.empty() || ref[0] != '#') return nullptr;
  const Json* node = ctx.root;
  std::string rest = ref.substr(1);
  if (!rest.empty() && rest[0] == '/') rest.erase(0, 1);
  for (const std::string& part : core::split(rest, '/')) {
    if (part.empty()) continue;
    std::string key = part;
    // JSON pointer escapes.
    size_t pos;
    while ((pos = key.find("~1")) != std::string::npos) key.replace(pos, 2, "/");
    while ((pos = key.find("~0")) != std::string::npos) key.replace(pos, 2, "~");
    if (node->is_object() && node->contains(key)) {
      node = &(*node)[key];
    } else if (node->is_array()) {
      char* end = nullptr;
      const long index = std::strtol(key.c_str(), &end, 10);
      if (!end || *end != '\0' || index < 0 || static_cast<size_t>(index) >= node->size()) return nullptr;
      node = &(*node)[static_cast<size_t>(index)];
    } else {
      return nullptr;
    }
  }
  return node;
}

void validateAt(Context& ctx, const Json& schema, const Json& value, const std::string& path);

// Runs a sub-schema and reports whether it passed without adding errors.
bool passes(Context& ctx, const Json& schema, const Json& value, const std::string& path) {
  Context probe;
  probe.root = ctx.root;
  probe.depth = ctx.depth;
  validateAt(probe, schema, value, path);
  return probe.errors.empty();
}

void validateAt(Context& ctx, const Json& schema, const Json& value, const std::string& path) {
  if (ctx.depth > 64) {
    ctx.errors.push_back(path + ": schema nesting too deep");
    return;
  }
  if (schema.is_boolean()) {
    if (!schema.get<bool>()) ctx.errors.push_back(path + ": not allowed here");
    return;
  }
  if (!schema.is_object()) return;
  ++ctx.depth;

  if (schema.contains("$ref") && schema["$ref"].is_string()) {
    const Json* target = resolveRef(ctx, schema["$ref"]);
    if (!target) {
      ctx.errors.push_back(path + ": unresolvable $ref " + schema["$ref"].get<std::string>());
    } else {
      validateAt(ctx, *target, value, path);
    }
  }

  // type
  if (schema.contains("type")) {
    const Json& type = schema["type"];
    bool ok = false;
    std::string wanted;
    if (type.is_string()) {
      ok = matchesType(value, type);
      wanted = type;
    } else if (type.is_array()) {
      for (const auto& t : type) {
        if (t.is_string()) {
          if (!wanted.empty()) wanted += " | ";
          wanted += t.get<std::string>();
          if (matchesType(value, t)) ok = true;
        }
      }
    } else {
      ok = true;
    }
    if (!ok) {
      ctx.errors.push_back(path + ": expected " + wanted + ", got " + typeName(value) + " " + brief(value));
      --ctx.depth;
      return;  // further checks assume the type
    }
  }

  if (schema.contains("enum") && schema["enum"].is_array()) {
    bool found = false;
    for (const auto& option : schema["enum"]) {
      if (option == value) found = true;
    }
    if (!found) {
      std::string options;
      for (const auto& option : schema["enum"]) {
        if (!options.empty()) options += ", ";
        options += option.dump();
      }
      ctx.errors.push_back(path + ": " + brief(value) + " is not one of " + options);
    }
  }
  if (schema.contains("const") && schema["const"] != value) {
    ctx.errors.push_back(path + ": must be " + schema["const"].dump());
  }

  // strings
  if (value.is_string()) {
    const std::string& s = value.get_ref<const std::string&>();
    if (schema.contains("minLength") && schema["minLength"].is_number() &&
        s.size() < schema["minLength"].get<size_t>()) {
      ctx.errors.push_back(path + ": shorter than " + std::to_string(schema["minLength"].get<size_t>()) + " characters");
    }
    if (schema.contains("maxLength") && schema["maxLength"].is_number() &&
        s.size() > schema["maxLength"].get<size_t>()) {
      ctx.errors.push_back(path + ": longer than " + std::to_string(schema["maxLength"].get<size_t>()) + " characters");
    }
    if (schema.contains("pattern") && schema["pattern"].is_string()) {
      try {
        std::regex re(schema["pattern"].get<std::string>(), std::regex::ECMAScript);
        if (!std::regex_search(s, re)) {
          ctx.errors.push_back(path + ": does not match /" + schema["pattern"].get<std::string>() + "/");
        }
      } catch (const std::regex_error&) {
        ctx.errors.push_back(path + ": schema pattern is not a valid regex");
      }
    }
  }

  // numbers
  if (value.is_number()) {
    const double n = value.get<double>();
    auto bound = [&](const char* key, bool lower, bool exclusive) {
      if (!schema.contains(key) || !schema[key].is_number()) return;
      const double limit = schema[key].get<double>();
      const bool bad = lower ? (exclusive ? n <= limit : n < limit) : (exclusive ? n >= limit : n > limit);
      if (bad) {
        ctx.errors.push_back(path + ": " + core::trim(brief(value)) + (lower ? " is below " : " is above ") +
                             (exclusive ? std::string(lower ? "or equal to " : "or equal to ") : "") +
                             schema[key].dump());
      }
    };
    bound("minimum", true, false);
    bound("maximum", false, false);
    bound("exclusiveMinimum", true, true);
    bound("exclusiveMaximum", false, true);
    if (schema.contains("multipleOf") && schema["multipleOf"].is_number()) {
      const double m = schema["multipleOf"].get<double>();
      if (m > 0) {
        const double q = n / m;
        if (std::fabs(q - std::round(q)) > 1e-9) {
          ctx.errors.push_back(path + ": " + brief(value) + " is not a multiple of " + schema["multipleOf"].dump());
        }
      }
    }
  }

  // arrays
  if (value.is_array()) {
    if (schema.contains("minItems") && schema["minItems"].is_number() &&
        value.size() < schema["minItems"].get<size_t>()) {
      ctx.errors.push_back(path + ": fewer than " + std::to_string(schema["minItems"].get<size_t>()) + " items");
    }
    if (schema.contains("maxItems") && schema["maxItems"].is_number() &&
        value.size() > schema["maxItems"].get<size_t>()) {
      ctx.errors.push_back(path + ": more than " + std::to_string(schema["maxItems"].get<size_t>()) + " items");
    }
    if (schema.contains("uniqueItems") && schema["uniqueItems"].is_boolean() && schema["uniqueItems"].get<bool>()) {
      std::set<std::string> seen;
      for (size_t i = 0; i < value.size(); ++i) {
        if (!seen.insert(value[i].dump()).second) {
          ctx.errors.push_back(path + "[" + std::to_string(i) + "]: duplicate item");
        }
      }
    }
    if (schema.contains("items")) {
      const Json& items = schema["items"];
      if (items.is_array()) {
        for (size_t i = 0; i < value.size() && i < items.size(); ++i) {
          validateAt(ctx, items[i], value[i], path + "[" + std::to_string(i) + "]");
        }
      } else {
        for (size_t i = 0; i < value.size(); ++i) {
          validateAt(ctx, items, value[i], path + "[" + std::to_string(i) + "]");
        }
      }
    }
  }

  // objects
  if (value.is_object()) {
    const Json& props = schema.contains("properties") && schema["properties"].is_object()
                            ? schema["properties"]
                            : Json::object();
    if (schema.contains("required") && schema["required"].is_array()) {
      for (const auto& key : schema["required"]) {
        if (key.is_string() && !value.contains(key.get<std::string>())) {
          ctx.errors.push_back(path + ": missing required \"" + key.get<std::string>() + "\"");
        }
      }
    }
    for (const auto& [key, propSchema] : props.items()) {
      if (value.contains(key)) validateAt(ctx, propSchema, value[key], path + "." + key);
    }
    if (schema.contains("additionalProperties")) {
      const Json& extra = schema["additionalProperties"];
      for (const auto& [key, member] : value.items()) {
        if (props.contains(key)) continue;
        if (extra.is_boolean()) {
          if (!extra.get<bool>()) ctx.errors.push_back(path + ": unexpected property \"" + key + "\"");
        } else if (extra.is_object()) {
          validateAt(ctx, extra, member, path + "." + key);
        }
      }
    }
    if (schema.contains("minProperties") && schema["minProperties"].is_number() &&
        value.size() < schema["minProperties"].get<size_t>()) {
      ctx.errors.push_back(path + ": fewer than " + std::to_string(schema["minProperties"].get<size_t>()) + " properties");
    }
    if (schema.contains("maxProperties") && schema["maxProperties"].is_number() &&
        value.size() > schema["maxProperties"].get<size_t>()) {
      ctx.errors.push_back(path + ": more than " + std::to_string(schema["maxProperties"].get<size_t>()) + " properties");
    }
  }

  // combinators
  if (schema.contains("allOf") && schema["allOf"].is_array()) {
    for (const auto& sub : schema["allOf"]) validateAt(ctx, sub, value, path);
  }
  if (schema.contains("anyOf") && schema["anyOf"].is_array()) {
    bool any = false;
    for (const auto& sub : schema["anyOf"]) {
      if (passes(ctx, sub, value, path)) any = true;
    }
    if (!any) ctx.errors.push_back(path + ": matches none of the anyOf alternatives");
  }
  if (schema.contains("oneOf") && schema["oneOf"].is_array()) {
    int count = 0;
    for (const auto& sub : schema["oneOf"]) {
      if (passes(ctx, sub, value, path)) ++count;
    }
    if (count != 1) {
      ctx.errors.push_back(path + ": matches " + std::to_string(count) + " of the oneOf alternatives, needs exactly 1");
    }
  }
  if (schema.contains("not") && passes(ctx, schema["not"], value, path)) {
    ctx.errors.push_back(path + ": matches the forbidden schema");
  }
  --ctx.depth;
}

void checkAt(const Json& root, const Json& node, std::string* problem, int depth) {
  if (!problem->empty() || depth > 64) return;
  if (!node.is_object()) return;
  static const std::set<std::string> kTypes = {"string", "number", "integer", "boolean", "array", "object", "null"};
  if (node.contains("type")) {
    const Json& type = node["type"];
    auto bad = [&](const Json& t) {
      return !t.is_string() || !kTypes.count(t.get<std::string>());
    };
    if (type.is_string() ? bad(type) : !type.is_array()) {
      *problem = "unknown type " + type.dump();
      return;
    }
    if (type.is_array()) {
      for (const auto& t : type) {
        if (bad(t)) {
          *problem = "unknown type " + t.dump();
          return;
        }
      }
    }
  }
  if (node.contains("$ref")) {
    Context ctx;
    ctx.root = &root;
    if (!node["$ref"].is_string() || !resolveRef(ctx, node["$ref"])) {
      *problem = "unresolvable $ref " + node["$ref"].dump();
      return;
    }
  }
  if (node.contains("pattern") && node["pattern"].is_string()) {
    try {
      std::regex re(node["pattern"].get<std::string>());
    } catch (const std::regex_error&) {
      *problem = "invalid pattern " + node["pattern"].dump();
      return;
    }
  }
  for (const char* key : {"properties", "$defs", "definitions"}) {
    if (node.contains(key) && node[key].is_object()) {
      for (const auto& [name, sub] : node[key].items()) checkAt(root, sub, problem, depth + 1);
    }
  }
  for (const char* key : {"items", "additionalProperties", "not"}) {
    if (node.contains(key)) checkAt(root, node[key], problem, depth + 1);
  }
  for (const char* key : {"allOf", "anyOf", "oneOf"}) {
    if (node.contains(key) && node[key].is_array()) {
      for (const auto& sub : node[key]) checkAt(root, sub, problem, depth + 1);
    }
  }
}

}  // namespace

std::vector<std::string> validate(const Json& schema, const Json& value) {
  Context ctx;
  ctx.root = &schema;
  validateAt(ctx, schema, value, "$");
  return ctx.errors;
}

std::string check(const Json& schema) {
  if (schema.is_boolean()) return "";
  if (!schema.is_object()) return "a schema is a JSON object";
  std::string problem;
  checkAt(schema, schema, &problem, 0);
  return problem;
}

Json infer(const Json& example) {
  Json out = Json::object();
  if (example.is_object()) {
    out["type"] = "object";
    Json props = Json::object();
    Json required = Json::array();
    for (const auto& [key, member] : example.items()) {
      props[key] = infer(member);
      required.push_back(key);
    }
    out["properties"] = props;
    out["required"] = required;
    out["additionalProperties"] = false;
  } else if (example.is_array()) {
    out["type"] = "array";
    if (!example.empty()) out["items"] = infer(example[0]);
  } else if (example.is_string()) {
    out["type"] = "string";
  } else if (example.is_boolean()) {
    out["type"] = "boolean";
  } else if (example.is_number_integer() || example.is_number_unsigned()) {
    out["type"] = "integer";
  } else if (example.is_number()) {
    out["type"] = "number";
  } else {
    out["type"] = "null";
  }
  return out;
}

bool extractJson(const std::string& text, Json* out) {
  auto tryParse = [&](const std::string& candidate) {
    const std::string t = core::trim(candidate);
    if (t.empty() || (t[0] != '{' && t[0] != '[')) return false;
    Json parsed = Json::parse(t, nullptr, false);
    if (parsed.is_discarded()) return false;
    *out = parsed;
    return true;
  };
  if (tryParse(text)) return true;

  // A fenced block, with or without a language tag.
  size_t fence = text.find("```");
  while (fence != std::string::npos) {
    size_t lineEnd = text.find('\n', fence);
    if (lineEnd == std::string::npos) break;
    const size_t close = text.find("```", lineEnd + 1);
    if (close == std::string::npos) break;
    if (tryParse(text.substr(lineEnd + 1, close - lineEnd - 1))) return true;
    fence = text.find("```", close + 3);
  }

  // The first balanced object or array in the prose.
  for (size_t start = 0; start < text.size(); ++start) {
    if (text[start] != '{' && text[start] != '[') continue;
    int depth = 0;
    bool inString = false;
    for (size_t i = start; i < text.size(); ++i) {
      const char c = text[i];
      if (inString) {
        if (c == '\\') ++i;
        else if (c == '"') inString = false;
        continue;
      }
      if (c == '"') inString = true;
      else if (c == '{' || c == '[') ++depth;
      else if (c == '}' || c == ']') {
        if (--depth == 0) {
          if (tryParse(text.substr(start, i - start + 1))) return true;
          break;
        }
      }
    }
  }
  return false;
}

}  // namespace schema
