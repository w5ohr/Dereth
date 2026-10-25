// Structured output mode: a JSON schema per chat. The request asks the
// backend for it (OpenAI `response_format`, which Open WebUI turns into
// Ollama's `format`), the schema is also spelled out in a system message for
// models that only listen to the prompt, the reply is validated locally and
// rendered as a table or tree, and a violation is sent back to the model with
// the errors for another go (up to `maxRetries` times). The configuration
// lives in the chat document itself (chat.raw["structuredOutput"]) so it
// round-trips through the server with the chat.
#include "app.h"

#include <algorithm>

#include "core/common.h"
#include "core/jsonschema.h"
#include "imgui_stdlib.h"
#include "platform.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

constexpr size_t kTableRowLimit = 200;
constexpr int kTableColumnLimit = 12;

struct Example {
  const char* label;
  const char* name;
  const char* schema;
};

const Example kExamples[] = {
    {"List of records", "records",
     R"({
  "type": "array",
  "items": {
    "type": "object",
    "properties": {
      "name": {"type": "string"},
      "role": {"type": "string"},
      "year": {"type": "integer"}
    },
    "required": ["name", "role", "year"],
    "additionalProperties": false
  }
})"},
    {"Classification", "classification",
     R"({
  "type": "object",
  "properties": {
    "label": {"type": "string", "enum": ["positive", "neutral", "negative"]},
    "confidence": {"type": "number", "minimum": 0, "maximum": 1},
    "reason": {"type": "string", "maxLength": 200}
  },
  "required": ["label", "confidence", "reason"],
  "additionalProperties": false
})"},
    {"Key facts", "facts",
     R"({
  "type": "object",
  "properties": {
    "summary": {"type": "string"},
    "facts": {"type": "array", "items": {"type": "string"}, "minItems": 1},
    "open_questions": {"type": "array", "items": {"type": "string"}}
  },
  "required": ["summary", "facts"],
  "additionalProperties": false
})"},
};

std::string cellText(const Json& value) {
  if (value.is_string()) return value.get<std::string>();
  if (value.is_null()) return "";
  std::string s = value.dump();
  if (s.size() > 80) s = s.substr(0, 77) + "...";
  return s;
}

// A table reads better than a tree for a list of flat records, a list of
// scalars, or an object of scalars.
enum class Shape { Records, Scalars, KeyValue, Nested };

Shape shapeOf(const Json& value) {
  if (value.is_array()) {
    if (value.empty()) return Shape::Scalars;
    bool allObjects = true, allScalars = true;
    for (const auto& item : value) {
      if (!item.is_object()) allObjects = false;
      if (item.is_object() || item.is_array()) allScalars = false;
    }
    if (allObjects) return Shape::Records;
    if (allScalars) return Shape::Scalars;
    return Shape::Nested;
  }
  if (value.is_object()) {
    for (const auto& [key, member] : value.items()) {
      if (member.is_object() || member.is_array()) return Shape::Nested;
    }
    return Shape::KeyValue;
  }
  return Shape::Scalars;
}

void drawTree(const Json& value, const char* label, int depth) {
  ImGui::PushID(label);
  if (value.is_object() || value.is_array()) {
    const std::string header = std::string(label) + "  " +
                               (value.is_array() ? "[" + std::to_string(value.size()) + "]"
                                                 : "{" + std::to_string(value.size()) + "}");
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
    if (depth < 2) flags |= ImGuiTreeNodeFlags_DefaultOpen;
    if (ImGui::TreeNodeEx("##node", flags, "%s", header.c_str())) {
      if (value.is_object()) {
        for (const auto& [key, member] : value.items()) drawTree(member, key.c_str(), depth + 1);
      } else {
        for (size_t i = 0; i < value.size(); ++i) {
          drawTree(value[i], std::to_string(i).c_str(), depth + 1);
        }
      }
      ImGui::TreePop();
    }
  } else {
    ImGui::TreeNodeEx("##leaf", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                    ImGuiTreeNodeFlags_SpanAvailWidth,
                      "%s", label);
    ImGui::SameLine();
    widgets::dimText("%s", cellText(value).c_str());
  }
  ImGui::PopID();
}

void drawTable(const Json& value, Shape shape, float width) {
  const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings;
  if (shape == Shape::Records) {
    std::vector<std::string> columns;
    for (const auto& row : value) {
      for (const auto& [key, member] : row.items()) {
        if (std::find(columns.begin(), columns.end(), key) == columns.end()) columns.push_back(key);
      }
    }
    if (columns.empty()) return;
    const int shown = std::min(static_cast<int>(columns.size()), kTableColumnLimit);
    if (!ImGui::BeginTable("##records", shown, flags, ImVec2(width, 0))) return;
    for (int c = 0; c < shown; ++c) ImGui::TableSetupColumn(columns[static_cast<size_t>(c)].c_str());
    ImGui::TableHeadersRow();
    size_t rows = 0;
    for (const auto& row : value) {
      if (rows++ >= kTableRowLimit) break;
      ImGui::TableNextRow();
      for (int c = 0; c < shown; ++c) {
        ImGui::TableSetColumnIndex(c);
        const std::string& key = columns[static_cast<size_t>(c)];
        ImGui::TextWrapped("%s", row.contains(key) ? cellText(row[key]).c_str() : "");
      }
    }
    ImGui::EndTable();
    if (value.size() > kTableRowLimit) widgets::dimText("%zu more rows", value.size() - kTableRowLimit);
    if (columns.size() > static_cast<size_t>(shown)) widgets::dimText("%zu more columns in the JSON view", columns.size() - shown);
    return;
  }
  if (shape == Shape::KeyValue) {
    if (!ImGui::BeginTable("##kv", 2, flags, ImVec2(width, 0))) return;
    ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, width * 0.3f);
    ImGui::TableSetupColumn("Value");
    ImGui::TableHeadersRow();
    for (const auto& [key, member] : value.items()) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted(key.c_str());
      ImGui::TableSetColumnIndex(1);
      ImGui::TextWrapped("%s", cellText(member).c_str());
    }
    ImGui::EndTable();
    return;
  }
  // Scalars: one column, numbered.
  if (!ImGui::BeginTable("##list", 2, flags, ImVec2(width, 0))) return;
  ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 40.0f);
  ImGui::TableSetupColumn("Value");
  ImGui::TableHeadersRow();
  if (value.is_array()) {
    size_t rows = 0;
    for (const auto& item : value) {
      if (rows >= kTableRowLimit) break;
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::Text("%zu", rows + 1);
      ImGui::TableSetColumnIndex(1);
      ImGui::TextWrapped("%s", cellText(item).c_str());
      ++rows;
    }
  } else {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted("1");
    ImGui::TableSetColumnIndex(1);
    ImGui::TextWrapped("%s", cellText(value).c_str());
  }
  ImGui::EndTable();
}

}  // namespace

// ---------------------------------------------------------------- config

Json& App::structuredConfig() {
  if (!chat.raw.is_object()) chat.raw = Json::object();
  Json& cfg = chat.raw["structuredOutput"];
  if (!cfg.is_object()) cfg = Json::object();
  return cfg;
}

bool App::structuredEnabled() const {
  if (!chat.raw.is_object() || !chat.raw.contains("structuredOutput")) return false;
  const Json& cfg = chat.raw["structuredOutput"];
  return cfg.is_object() && cfg.value("enabled", false) && cfg.value("schema", "").size() > 1;
}

Json App::structuredSchema() const {
  if (!structuredEnabled()) return Json();
  Json parsed = Json::parse(chat.raw["structuredOutput"].value("schema", ""), nullptr, false);
  if (parsed.is_discarded() || !schema::check(parsed).empty()) return Json();
  return parsed;
}

std::string App::structuredName() const {
  if (!chat.raw.is_object() || !chat.raw.contains("structuredOutput")) return "reply";
  const std::string name = core::trim(chat.raw["structuredOutput"].value("name", ""));
  return name.empty() ? "reply" : name;
}

// ---------------------------------------------------------------- panel

void App::drawStructuredPanel() {
  if (!ImGui::BeginPopup("##structured")) return;
  const auto& palette = theme::palette();
  const float width = 560.0f;
  Json& cfg = structuredConfig();
  bool changed = false;

  theme::pushBold();
  ImGui::TextUnformatted("Structured output for this chat");
  theme::pop();
  ImGui::SameLine();
  widgets::dimText("replies must match a JSON schema");

  bool enabled = cfg.value("enabled", false);
  if (ImGui::Checkbox("On", &enabled)) {
    cfg["enabled"] = enabled;
    changed = true;
  }
  ImGui::SameLine(0, 16);
  ImGui::TextUnformatted("Name");
  ImGui::SameLine();
  std::string name = cfg.value("name", "");
  ImGui::SetNextItemWidth(160);
  if (ImGui::InputTextWithHint("##schemaname", "reply", &name)) {
    cfg["name"] = name;
    changed = true;
  }
  ImGui::SameLine(0, 16);
  ImGui::TextUnformatted("Re-ask up to");
  ImGui::SameLine();
  int retries = cfg.value("maxRetries", 2);
  ImGui::SetNextItemWidth(90);
  if (ImGui::SliderInt("##retries", &retries, 0, 3, "%d times")) {
    cfg["maxRetries"] = retries;
    changed = true;
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("A reply that breaks the schema is sent back to the model\nwith the violations, this many times, before it is shown as-is");
  }

  // Starting points.
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 3));
  for (const Example& example : kExamples) {
    if (ImGui::Button(example.label)) {
      cfg["schema"] = example.schema;
      cfg["name"] = example.name;
      cfg["enabled"] = true;
      changed = true;
    }
    ImGui::SameLine();
  }
  if (ImGui::Button("Infer from example")) {
    Json example;
    if (schema::extractJson(cfg.value("schema", ""), &example) && schema::check(example).empty() &&
        !(example.is_object() && example.contains("type"))) {
      cfg["schema"] = schema::infer(example).dump(2);
      cfg["enabled"] = true;
      changed = true;
    } else if (schema::extractJson(cfg.value("schema", ""), &example)) {
      cfg["schema"] = schema::infer(example).dump(2);
      cfg["enabled"] = true;
      changed = true;
    } else {
      widgets::toastError("Paste an example JSON value into the box first");
    }
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Paste an example of the JSON you want into the box, then\nturn it into a schema (every field required, types as seen)");
  }
  ImGui::PopStyleVar();

  std::string text = cfg.value("schema", "");
  theme::pushMono();
  if (ImGui::InputTextMultiline("##schema", &text, ImVec2(width, 220), ImGuiInputTextFlags_AllowTabInput)) {
    cfg["schema"] = text;
    changed = true;
  }
  theme::pop();

  // Live verdict on the schema itself.
  const std::string trimmed = core::trim(text);
  if (trimmed.empty()) {
    widgets::dimText("Paste a JSON schema or pick a starting point above");
  } else {
    Json parsed = Json::parse(trimmed, nullptr, false);
    std::string problem = parsed.is_discarded() ? "not valid JSON" : schema::check(parsed);
    if (!problem.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.danger));
      ImGui::TextWrapped("Schema problem: %s", problem.c_str());
      ImGui::PopStyleColor();
    } else {
      ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.success));
      ImGui::TextUnformatted(enabled ? "Schema OK - the next reply is asked for in this shape"
                                     : "Schema OK - switch On to use it");
      ImGui::PopStyleColor();
    }
  }

  ImGui::Separator();
  if (ImGui::Button("Clear")) {
    cfg = Json::object();
    changed = true;
  }
  ImGui::SameLine(width - 90.0f);
  if (widgets::primaryButton("Done", ImVec2(90, 0))) {
    saveActiveChat();
    ImGui::CloseCurrentPopup();
  }
  if (changed) {
    // Earlier replies are judged again against the new schema.
    for (auto& [id, message] : chat.messages) message.structuredChecked = false;
    structuredHints.clear();
    structuredRetries.clear();
    if (!chat.id.empty()) saveActiveChat();
  }
  ImGui::EndPopup();
}

// ---------------------------------------------------------------- checking

void App::checkStructured(Message& message) {
  message.structuredChecked = true;
  message.structuredValue = Json();
  message.structuredErrors.clear();
  if (message.role != "assistant" || message.streaming) return;
  const Json schema = structuredSchema();
  if (schema.is_null()) return;
  Json value;
  if (!schema::extractJson(message.content, &value)) {
    message.structuredErrors.push_back(core::trim(message.content).empty() ? "the reply was empty"
                                                                            : "no JSON value found in the reply");
    return;
  }
  message.structuredValue = value;
  message.structuredErrors = schema::validate(schema, value);
}

bool App::reaskStructured(const std::string& assistantId) {
  auto it = chat.messages.find(assistantId);
  if (it == chat.messages.end() || it->second.structuredErrors.empty()) return false;
  const std::string parentId = it->second.parentId;
  const int maxRetries = structuredConfig().value("maxRetries", 2);
  int& count = structuredRetries[parentId];
  if (count >= maxRetries) {
    if (maxRetries > 0) {
      widgets::toastError("Reply still breaks the schema after " + std::to_string(maxRetries) +
                          (maxRetries == 1 ? " re-ask" : " re-asks"));
    }
    return false;
  }
  ++count;
  std::string hint;
  for (const std::string& error : it->second.structuredErrors) {
    if (!hint.empty()) hint += "; ";
    hint += error;
    if (hint.size() > 600) break;
  }
  structuredHints[parentId] = hint;
  widgets::toastInfo("Reply breaks the schema - asking again (" + std::to_string(count) + "/" +
                     std::to_string(maxRetries) + ")");
  regenerate(assistantId);
  // Two replies under one prompt would otherwise show as an unresolved
  // fan-out; the retry is the branch, the rejected one stays as a sibling.
  keepFanoutAnswer(chat.currentId, false);
  return true;
}

// ---------------------------------------------------------------- rendering

bool App::drawStructuredValue(Message& message, float width) {
  if (!message.structuredChecked) checkStructured(message);
  const auto& palette = theme::palette();
  const bool valid = message.structuredErrors.empty() && !message.structuredValue.is_null();

  // Verdict line.
  icons::inlineIcon(valid ? icons::Icon::Check : icons::Icon::Warning, valid ? palette.success : palette.danger);
  ImGui::SameLine(0, 6);
  if (valid) {
    ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.success));
    ImGui::Text("Matches schema \"%s\"", structuredName().c_str());
    ImGui::PopStyleColor();
  } else {
    ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.danger));
    ImGui::Text("%zu schema violation%s", message.structuredErrors.size(),
                message.structuredErrors.size() == 1 ? "" : "s");
    ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.textDim));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
    size_t shown = 0;
    for (const std::string& error : message.structuredErrors) {
      if (shown++ >= 8) {
        ImGui::Text("... and %zu more", message.structuredErrors.size() - 8);
        break;
      }
      ImGui::BulletText("%s", error.c_str());
    }
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    if (streams_.empty()) {
      ImGui::SameLine();
      if (widgets::ghostButton("Ask again")) {
        structuredRetries[message.parentId] = 0;
        reaskStructured(message.id);
      }
    }
  }
  // Nothing parsed: the prose renders as usual underneath the verdict.
  if (message.structuredValue.is_null()) return false;

  // View switch.
  ImGui::SameLine(0, 14);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 2));
  const Shape shape = shapeOf(message.structuredValue);
  const char* views[] = {shape == Shape::Nested ? "Tree" : "Table", "Tree", "JSON"};
  for (int v = 0; v < 3; ++v) {
    if (v == 1 && shape == Shape::Nested) continue;
    ImGui::PushID(v);
    const bool active = message.structuredView == v;
    if (active) ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.accent));
    if (widgets::ghostButton(views[v])) message.structuredView = v;
    if (active) ImGui::PopStyleColor();
    ImGui::PopID();
    ImGui::SameLine();
  }
  if (widgets::ghostButton("Copy JSON")) {
    platform::setClipboardText(message.structuredValue.dump(2));
    widgets::toast(widgets::ToastLevel::Success, "Copied JSON");
  }
  ImGui::PopStyleVar();
  ImGui::Dummy(ImVec2(0, 2));

  if (message.structuredView == 2) {
    theme::pushMono();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
    ImGui::TextUnformatted(message.structuredValue.dump(2).c_str());
    ImGui::PopTextWrapPos();
    theme::pop();
  } else if (message.structuredView == 1 || shape == Shape::Nested) {
    drawTree(message.structuredValue, structuredName().c_str(), 0);
  } else {
    drawTable(message.structuredValue, shape, width);
  }
  return true;
}

}  // namespace app
