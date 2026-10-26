// Model cards: what Ollama knows about each model (family, parameters,
// quantisation, context length, layers, capabilities, licence, defaults),
// fetched once through the server's /ollama/api/show passthrough and shown
// as a tooltip in the picker and the chat header, plus a comparison table
// of every Ollama model (size on disk, bits per weight, context) so picking a
// quant is a look rather than a guess.
#include "app.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "core/async.h"
#include "core/common.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

struct Card {
  bool loading = false;
  bool loaded = false;
  bool failed = false;
  std::string family, parameters, quant, architecture, license, systemPrompt;
  std::vector<std::string> capabilities;
  int64_t contextLength = 0, embedding = 0, layers = 0, paramCount = 0, sizeBytes = 0;
  std::string modified;
  std::map<std::string, std::string> defaults;   // from the Modelfile PARAMETER lines
};

std::map<std::string, Card> gCards;
bool gShowCompare = false;

std::string gb(int64_t bytes) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%.1f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
  return buf;
}

std::string k(int64_t n) {
  if (n >= 1000000) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.0fk", static_cast<double>(n) / 1000.0);
    return buf;
  }
  if (n >= 1000) return std::to_string(n / 1000) + "k";
  return std::to_string(n);
}

std::string bitsPerWeight(const Card& c) {
  if (c.sizeBytes <= 0 || c.paramCount <= 0) return "";
  char buf[32];
  snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(c.sizeBytes) * 8.0 / static_cast<double>(c.paramCount));
  return buf;
}

// The size and modified date come with the model list already.
void seedFromList(const ModelInfo& info, Card* card) {
  if (info.raw.contains("ollama") && info.raw["ollama"].is_object()) {
    const Json& o = info.raw["ollama"];
    if (o.contains("size") && o["size"].is_number()) card->sizeBytes = o["size"].get<int64_t>();
    if (o.contains("modified_at") && o["modified_at"].is_string()) card->modified = o["modified_at"].get<std::string>().substr(0, 10);
    const Json details = o.value("details", Json::object());
    if (details.is_object()) {
      card->family = details.value("family", card->family);
      card->parameters = details.value("parameter_size", card->parameters);
      card->quant = details.value("quantization_level", card->quant);
    }
  }
}

void fill(const Json& show, Card* card) {
  const Json details = show.value("details", Json::object());
  if (details.is_object()) {
    card->family = details.value("family", card->family);
    card->parameters = details.value("parameter_size", card->parameters);
    card->quant = details.value("quantization_level", card->quant);
  }
  const Json info = show.value("model_info", Json::object());
  if (info.is_object()) {
    card->architecture = info.value("general.architecture", card->family);
    const std::string arch = card->architecture;
    auto num = [&](const std::string& key) -> int64_t {
      return info.contains(key) && info[key].is_number() ? info[key].get<int64_t>() : 0;
    };
    card->contextLength = num(arch + ".context_length");
    card->embedding = num(arch + ".embedding_length");
    card->layers = num(arch + ".block_count");
    card->paramCount = num("general.parameter_count");
    if (info.contains("general.license") && info["general.license"].is_string()) card->license = info["general.license"];
  }
  if (show.contains("capabilities") && show["capabilities"].is_array()) {
    card->capabilities.clear();
    for (const auto& c : show["capabilities"]) {
      if (c.is_string()) card->capabilities.push_back(c);
    }
  }
  if (card->license.empty() && show.contains("license") && show["license"].is_string()) {
    // First non-empty line of the licence text.
    for (const std::string& line : core::split(show["license"].get<std::string>(), '\n')) {
      const std::string t = core::trim(line);
      if (!t.empty()) {
        card->license = t.substr(0, 60);
        break;
      }
    }
  }
  if (show.contains("parameters") && show["parameters"].is_string()) {
    for (const std::string& line : core::split(show["parameters"].get<std::string>(), '\n')) {
      const std::string t = core::trim(line);
      const size_t space = t.find_first_of(" \t");
      if (space == std::string::npos) continue;
      const std::string key = t.substr(0, space);
      const std::string value = core::trim(t.substr(space));
      if (key == "stop") continue;   // many lines, little interest
      card->defaults[key] = value;
    }
  }
  if (show.contains("system") && show["system"].is_string()) card->systemPrompt = show["system"];
  card->loaded = true;
}

}  // namespace

// The card for a model, fetched on first request (returns the partial one
// seeded from the list meanwhile).
const void* App::modelCard(const std::string& modelId) {
  Card& card = gCards[modelId];
  if (!card.loaded && !card.loading && !card.failed) {
    for (const ModelInfo& info : models) {
      if (info.id == modelId) seedFromList(info, &card);
    }
    // Only Ollama models have a show record; a workspace model unwraps to its base.
    std::string base = modelId;
    for (const ModelInfo& info : models) {
      if (info.id != modelId || !info.raw.contains("info") || !info.raw["info"].is_object()) continue;
      const Json& meta = info.raw["info"];
      if (meta.contains("base_model_id") && meta["base_model_id"].is_string() &&
          !meta["base_model_id"].get<std::string>().empty()) {
        base = meta["base_model_id"];
      }
    }
    card.loading = true;
    async::run([this, modelId, base] {
      api::Result res = api_.ollamaShow(base);
      async::post([modelId, res] {
        Card& c = gCards[modelId];
        c.loading = false;
        if (res.ok && res.data.is_object()) fill(res.data, &c);
        else c.failed = true;
      });
    });
  }
  return &card;
}

bool App::drawModelCardTooltip(const std::string& modelId) {
  const Card& card = *static_cast<const Card*>(modelCard(modelId));
  const auto& palette = theme::palette();
  ImGui::BeginTooltip();
  ImGui::PushTextWrapPos(380.0f);
  theme::pushBold();
  ImGui::TextUnformatted(modelId.c_str());
  theme::pop();
  if (card.loading && !card.loaded) {
    widgets::dimText("Reading the model's details...");
  } else if (card.failed && card.family.empty()) {
    widgets::dimText("No Ollama details for this model (not an Ollama model, or the passthrough is off).");
  } else {
    std::string line;
    if (!card.family.empty()) line += card.family;
    if (!card.parameters.empty()) line += (line.empty() ? "" : " - ") + card.parameters;
    if (!card.quant.empty()) line += (line.empty() ? "" : " - ") + card.quant;
    if (card.sizeBytes > 0) line += (line.empty() ? "" : " - ") + gb(card.sizeBytes);
    if (!line.empty()) ImGui::TextUnformatted(line.c_str());
    if (card.contextLength > 0) {
      ImGui::Text("Context %s tokens", k(card.contextLength).c_str());
      ImGui::SameLine();
      widgets::dimText("(trained; the server's num_ctx may be lower)");
    }
    if (card.layers > 0 || card.embedding > 0) {
      ImGui::Text("%lld layers, %lld-wide embeddings%s%s", static_cast<long long>(card.layers),
                  static_cast<long long>(card.embedding), bitsPerWeight(card).empty() ? "" : ", ",
                  bitsPerWeight(card).empty() ? "" : (bitsPerWeight(card) + " bits/weight").c_str());
    }
    if (!card.capabilities.empty()) {
      std::string caps;
      for (const std::string& c : card.capabilities) caps += (caps.empty() ? "" : ", ") + c;
      ImGui::Text("Capabilities: %s", caps.c_str());
    }
    if (!card.defaults.empty()) {
      std::string d;
      for (const auto& [key, value] : card.defaults) d += (d.empty() ? "" : "  ") + key + "=" + value;
      ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.textDim));
      ImGui::TextWrapped("Defaults: %s", d.c_str());
      ImGui::PopStyleColor();
    }
    if (!card.systemPrompt.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.textDim));
      ImGui::TextWrapped("System prompt: %s", card.systemPrompt.substr(0, 200).c_str());
      ImGui::PopStyleColor();
    }
    if (!card.license.empty()) widgets::dimText("Licence: %s", card.license.c_str());
    if (!card.modified.empty()) widgets::dimText("Pulled/modified %s", card.modified.c_str());
  }
  ImGui::PopTextWrapPos();
  ImGui::EndTooltip();
  return true;
}

void App::openModelCompare() {
  // One modal at a time: the picker closes, the table takes its place.
  showModelPicker = false;
  gShowCompare = true;
  for (const ModelInfo& info : models) {
    if (info.raw.contains("ollama")) modelCard(info.id);
  }
}

void App::drawModelCompare() {
  if (!gShowCompare) return;
  ImGui::OpenPopup("Compare models");
  ImGui::SetNextWindowSize(ImVec2(900, 520), ImGuiCond_Appearing);
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (!ImGui::BeginPopupModal("Compare models", &gShowCompare)) return;
  if (ImGui::IsKeyPressed(ImGuiKey_Escape)) gShowCompare = false;
  widgets::dimText("Every Ollama model the server lists. Bits per weight is file size over parameter count - the "
                   "quantisation's real cost. Click a row to select that model.");
  ImGui::Dummy(ImVec2(0, 4));
  std::vector<const ModelInfo*> rows;
  for (const ModelInfo& info : models) {
    if (info.raw.contains("ollama")) rows.push_back(&info);
  }
  const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Sortable |
                                ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
  if (ImGui::BeginTable("##compare", 9, flags, ImVec2(0, -40))) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Model", ImGuiTableColumnFlags_DefaultSort);
    ImGui::TableSetupColumn("Family");
    ImGui::TableSetupColumn("Params");
    ImGui::TableSetupColumn("Quant");
    ImGui::TableSetupColumn("Size");
    ImGui::TableSetupColumn("Bits/wt");
    ImGui::TableSetupColumn("Context");
    ImGui::TableSetupColumn("Layers");
    ImGui::TableSetupColumn("Capabilities");
    ImGui::TableHeadersRow();
    if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs()) {
      if (specs->SpecsCount > 0) {
        const int column = specs->Specs[0].ColumnIndex;
        const bool asc = specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
        std::stable_sort(rows.begin(), rows.end(), [&](const ModelInfo* a, const ModelInfo* b) {
          const Card& ca = *static_cast<const Card*>(modelCard(a->id));
          const Card& cb = *static_cast<const Card*>(modelCard(b->id));
          auto cmp = [&](auto x, auto y) { return asc ? x < y : y < x; };
          switch (column) {
            case 1: return cmp(ca.family, cb.family);
            case 2: return cmp(ca.paramCount, cb.paramCount);
            case 3: return cmp(ca.quant, cb.quant);
            case 4: return cmp(ca.sizeBytes, cb.sizeBytes);
            case 5: return cmp(ca.sizeBytes * 8.0 / std::max<int64_t>(1, ca.paramCount), cb.sizeBytes * 8.0 / std::max<int64_t>(1, cb.paramCount));
            case 6: return cmp(ca.contextLength, cb.contextLength);
            case 7: return cmp(ca.layers, cb.layers);
            default: return cmp(a->name, b->name);
          }
        });
      }
    }
    for (const ModelInfo* info : rows) {
      const Card& c = *static_cast<const Card*>(modelCard(info->id));
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      const bool selected = std::find(selectedModels.begin(), selectedModels.end(), info->id) != selectedModels.end();
      if (ImGui::Selectable(info->name.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
        selectedModels = {info->id};
        settings().selectedModels = selectedModels;
        settings().save();
      }
      if (ImGui::IsItemHovered()) drawModelCardTooltip(info->id);
      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted(c.family.c_str());
      ImGui::TableSetColumnIndex(2);
      ImGui::TextUnformatted(c.parameters.c_str());
      ImGui::TableSetColumnIndex(3);
      ImGui::TextUnformatted(c.quant.c_str());
      ImGui::TableSetColumnIndex(4);
      ImGui::TextUnformatted(c.sizeBytes > 0 ? gb(c.sizeBytes).c_str() : "");
      ImGui::TableSetColumnIndex(5);
      ImGui::TextUnformatted(bitsPerWeight(c).c_str());
      ImGui::TableSetColumnIndex(6);
      ImGui::TextUnformatted(c.contextLength > 0 ? k(c.contextLength).c_str() : (c.loading ? "..." : ""));
      ImGui::TableSetColumnIndex(7);
      ImGui::TextUnformatted(c.layers > 0 ? std::to_string(c.layers).c_str() : "");
      ImGui::TableSetColumnIndex(8);
      std::string caps;
      for (const std::string& cap : c.capabilities) {
        if (cap == "completion") continue;
        caps += (caps.empty() ? "" : ", ") + cap;
      }
      ImGui::TextUnformatted(caps.c_str());
    }
    ImGui::EndTable();
  }
  if (widgets::primaryButton("Done", ImVec2(120, 30))) gShowCompare = false;
  ImGui::EndPopup();
}

}  // namespace app
