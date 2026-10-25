// Usage dashboard (Tools > Usage): what the last N days of replies cost in
// tokens, time, money (for models with a price) and energy (local models),
// per day and per model, with a stacked bar chart of output tokens per day,
// a tok/s trend, and a CSV export. Every finished reply is appended to the
// log from afterCompletion.
#include "app.h"

#include <algorithm>
#include <cmath>

#include "core/common.h"
#include "core/usagelog.h"
#include "imgui_stdlib.h"
#include "platform.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

std::string logPath() { return core::supportDir() + "/usage.jsonl"; }

int gRangeDays = 30;
std::vector<usagelog::Record> gRecords;
double gLoadedAt = 0.0;
std::string gPriceModel, gPriceIn, gPriceOut;

usagelog::Prices pricesFromSettings() {
  usagelog::Prices p;
  const Json& extra = core::Settings::instance().extra;
  const Json table = extra.value("usagePrices", Json::object());
  if (table.is_object()) {
    for (const auto& [model, v] : table.items()) {
      if (v.is_array() && v.size() == 2 && v[0].is_number() && v[1].is_number()) {
        p.perMillion[model] = {v[0].get<double>(), v[1].get<double>()};
      }
    }
  }
  p.joulesPerToken = extra.value("usageJoulesPerToken", 0.5);
  return p;
}

std::string fmtTokens(int64_t n) {
  char buf[32];
  if (n >= 1000000) snprintf(buf, sizeof(buf), "%.2fM", n / 1e6);
  else if (n >= 10000) snprintf(buf, sizeof(buf), "%.1fk", n / 1e3);
  else snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(n));
  return buf;
}

std::string fmtMs(double ms) {
  char buf[32];
  if (ms >= 3600000) snprintf(buf, sizeof(buf), "%.1f h", ms / 3600000.0);
  else if (ms >= 60000) snprintf(buf, sizeof(buf), "%.1f min", ms / 60000.0);
  else snprintf(buf, sizeof(buf), "%.0f s", ms / 1000.0);
  return buf;
}

ImU32 seriesColor(size_t index, const theme::Palette& palette) {
  static const ImU32 extra[] = {IM_COL32(120, 170, 255, 255), IM_COL32(110, 200, 150, 255), IM_COL32(240, 180, 90, 255),
                                IM_COL32(220, 120, 160, 255), IM_COL32(160, 140, 230, 255), IM_COL32(90, 200, 210, 255)};
  if (index == 0) return palette.accent;
  return extra[(index - 1) % (sizeof(extra) / sizeof(extra[0]))];
}

}  // namespace

// Called from afterCompletion for every reply that finished without an error.
void App::recordUsage(const Message& message) {
  if (!settings().extra.value("usageLog", true)) return;
  usagelog::Record r;
  r.at = core::nowSeconds();
  r.model = message.model.empty() ? (selectedModels.empty() ? "" : selectedModels.front()) : message.model;
  r.chatId = chat.id;
  r.isPrivate = chat.isPrivate;
  r.promptTokens = message.usage.value("prompt_tokens", 0);
  r.completionTokens = message.usage.value("completion_tokens", 0);
  if (r.promptTokens == 0 && r.completionTokens == 0) {
    // No usage from the backend: estimate from the text so the day still counts.
    r.completionTokens = estimateTokens(message.content);
  }
  auto timing = timings.find(message.id);
  if (timing != timings.end()) {
    if (timing->second.firstTokenMs > 0) r.firstTokenMs = timing->second.firstTokenMs - timing->second.sentMs;
    if (timing->second.doneMs > 0) r.totalMs = timing->second.doneMs - timing->second.sentMs;
  }
  r.tokensPerSec = message.usage.value("response_token/s", 0.0);
  if (r.tokensPerSec <= 0 && r.completionTokens > 0 && r.totalMs > r.firstTokenMs && r.totalMs > 0) {
    r.tokensPerSec = r.completionTokens / ((r.totalMs - r.firstTokenMs) / 1000.0);
  }
  // Ollama-backed models are local; everything else (OpenAI-style, custom) counts as an API.
  r.isLocal = true;
  for (const ModelInfo& info : models) {
    if (info.id == r.model) r.isLocal = info.raw.contains("ollama") || info.owner == "ollama";
  }
  usagelog::append(logPath(), r);
  gLoadedAt = 0.0;   // the dashboard reloads next time it is drawn
}

void App::drawUsageSection(float width) {
  const auto& palette = theme::palette();
  widgets::sectionHeader("USAGE");
  bool enabled = settings().extra.value("usageLog", true);
  if (ImGui::Checkbox("Record every reply (model, tokens, timing) to a local log", &enabled)) {
    settings().extra["usageLog"] = enabled;
    settings().save();
  }
  ImGui::SameLine(0, 20);
  ImGui::SetNextItemWidth(120);
  const char* ranges[] = {"7 days", "30 days", "90 days", "All"};
  int rangeIndex = gRangeDays == 7 ? 0 : gRangeDays == 30 ? 1 : gRangeDays == 90 ? 2 : 3;
  if (ImGui::Combo("##range", &rangeIndex, ranges, 4)) {
    gRangeDays = rangeIndex == 0 ? 7 : rangeIndex == 1 ? 30 : rangeIndex == 2 ? 90 : 0;
    gLoadedAt = 0.0;
  }
  ImGui::SameLine();
  if (ImGui::Button("Export CSV...")) {
    const std::string path = platform::saveFile("usage.csv");
    if (!path.empty()) {
      core::writeFile(path, usagelog::csv(gRecords));
      widgets::toast(widgets::ToastLevel::Success, "Saved " + std::to_string(gRecords.size()) + " rows to " + path);
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Reveal log")) platform::revealInFinder(logPath());

  if (core::nowMillis() - gLoadedAt > 5000.0) {
    gRecords = usagelog::load(logPath(), gRangeDays);
    gLoadedAt = core::nowMillis();
  }
  const usagelog::Prices prices = pricesFromSettings();
  const usagelog::Row total = usagelog::totals(gRecords, prices);

  // Headline tiles.
  ImGui::Dummy(ImVec2(0, 6));
  auto tile = [&](const char* label, const std::string& value, const char* tip) {
    ImGui::BeginGroup();
    theme::pushScaled(1.5f);
    theme::pushBold();
    ImGui::TextUnformatted(value.c_str());
    theme::pop();
    theme::pop();
    widgets::dimText("%s", label);
    ImGui::EndGroup();
    if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    ImGui::SameLine(0, 36);
  };
  tile("replies", std::to_string(total.replies), nullptr);
  tile("tokens in", fmtTokens(total.promptTokens), "prompt tokens the backend reported (estimated when it did not)");
  tile("tokens out", fmtTokens(total.completionTokens), nullptr);
  tile("generating", fmtMs(total.totalMs), "wall time from send to the last token");
  {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f", total.tokensPerSecMean);
    tile("tok/s", buf, "mean output speed, weighted by tokens");
  }
  {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f Wh", total.energyWh);
    tile("energy", buf, "local tokens x joules-per-token (set below); a rough figure, not a measurement");
  }
  if (total.costUsd > 0 || !prices.perMillion.empty()) {
    char buf[32];
    snprintf(buf, sizeof(buf), "$%.3f", total.costUsd);
    tile("cost", buf, "tokens x the per-million prices entered below, for models that have one");
  }
  ImGui::NewLine();
  ImGui::Dummy(ImVec2(0, 8));

  if (gRecords.empty()) {
    widgets::dimText("No replies recorded in this range yet.");
    return;
  }

  // Chart: output tokens per day, stacked by model; tok/s as a line.
  std::vector<std::string> modelsSeen;
  const auto matrix = usagelog::dayModelMatrix(gRecords, &modelsSeen);
  const std::vector<usagelog::Row> days = usagelog::byDay(gRecords, prices);
  const float chartH = 180.0f;
  const float chartW = width - 40.0f;
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(origin, ImVec2(origin.x + chartW, origin.y + chartH), palette.panel, 6.0f);
  int64_t maxTokens = 1;
  double maxTps = 1.0;
  for (const usagelog::Row& d : days) {
    maxTokens = std::max<int64_t>(maxTokens, d.completionTokens);
    maxTps = std::max(maxTps, d.tokensPerSecMean);
  }
  const size_t n = days.size();
  const float slot = chartW / static_cast<float>(std::max<size_t>(n, 1));
  const float barW = std::max(4.0f, std::min(36.0f, slot * 0.6f));
  ImVec2 prevPoint(0, 0);
  bool havePrev = false;
  for (size_t i = 0; i < n; ++i) {
    const usagelog::Row& d = days[n - 1 - i];   // oldest left
    const float x = origin.x + slot * (i + 0.5f);
    float y = origin.y + chartH - 22.0f;
    const auto dayIt = matrix.find(d.key);
    for (size_t m = 0; m < modelsSeen.size(); ++m) {
      const int64_t tokens = dayIt != matrix.end() ? (dayIt->second.count(modelsSeen[m]) ? dayIt->second.at(modelsSeen[m]) : 0) : 0;
      if (tokens <= 0) continue;
      const float h = (chartH - 40.0f) * static_cast<float>(tokens) / static_cast<float>(maxTokens);
      dl->AddRectFilled(ImVec2(x - barW * 0.5f, y - h), ImVec2(x + barW * 0.5f, y), seriesColor(m, palette));
      y -= h;
    }
    if (n <= 16 || i % std::max<size_t>(1, n / 8) == 0) {
      dl->AddText(ImVec2(x - 18.0f, origin.y + chartH - 18.0f), palette.textFaint, d.key.substr(5).c_str());
    }
    if (d.tokensPerSecMean > 0) {
      const ImVec2 point(x, origin.y + 12.0f + (chartH - 52.0f) * static_cast<float>(1.0 - d.tokensPerSecMean / maxTps));
      if (havePrev) dl->AddLine(prevPoint, point, palette.warning, 2.0f);
      dl->AddCircleFilled(point, 3.0f, palette.warning);
      prevPoint = point;
      havePrev = true;
    }
    // Hover: the day's numbers.
    ImGui::SetCursorScreenPos(ImVec2(x - slot * 0.5f, origin.y));
    ImGui::InvisibleButton(("##day" + d.key).c_str(), ImVec2(slot, chartH));
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s\n%d replies, %s in / %s out\n%.1f tok/s, %s generating%s", d.key.c_str(), d.replies,
                        fmtTokens(d.promptTokens).c_str(), fmtTokens(d.completionTokens).c_str(), d.tokensPerSecMean,
                        fmtMs(d.totalMs).c_str(), d.costUsd > 0 ? (", $" + std::to_string(d.costUsd).substr(0, 6)).c_str() : "");
    }
  }
  ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + chartH + 6.0f));
  // Legend.
  for (size_t m = 0; m < modelsSeen.size() && m < 8; ++m) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(p, ImVec2(p.x + 10, p.y + 10), seriesColor(m, palette), 2.0f);
    ImGui::Dummy(ImVec2(14, 10));
    ImGui::SameLine();
    widgets::dimText("%s", modelsSeen[m].c_str());
    ImGui::SameLine(0, 14);
  }
  {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddLine(ImVec2(p.x, p.y + 5), ImVec2(p.x + 14, p.y + 5), palette.warning, 2.0f);
    ImGui::Dummy(ImVec2(18, 10));
    ImGui::SameLine();
    widgets::dimText("tok/s (output tokens per day as bars)");
  }
  ImGui::Dummy(ImVec2(0, 8));

  // Per-model table.
  widgets::sectionHeader("BY MODEL");
  const std::vector<usagelog::Row> perModel = usagelog::byModel(gRecords, prices);
  if (ImGui::BeginTable("##bymodel", 8, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp)) {
    for (const char* h : {"Model", "Replies", "In", "Out", "tok/s", "Generating", "Energy", "Cost"}) ImGui::TableSetupColumn(h);
    ImGui::TableHeadersRow();
    for (const usagelog::Row& r : perModel) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted(r.key.c_str());
      ImGui::TableSetColumnIndex(1);
      ImGui::Text("%d", r.replies);
      ImGui::TableSetColumnIndex(2);
      ImGui::TextUnformatted(fmtTokens(r.promptTokens).c_str());
      ImGui::TableSetColumnIndex(3);
      ImGui::TextUnformatted(fmtTokens(r.completionTokens).c_str());
      ImGui::TableSetColumnIndex(4);
      ImGui::Text("%.1f", r.tokensPerSecMean);
      ImGui::TableSetColumnIndex(5);
      ImGui::TextUnformatted(fmtMs(r.totalMs).c_str());
      ImGui::TableSetColumnIndex(6);
      ImGui::Text("%.2f Wh", r.energyWh);
      ImGui::TableSetColumnIndex(7);
      auto price = prices.perMillion.find(r.key);
      if (price != prices.perMillion.end()) ImGui::Text("$%.3f", r.costUsd);
      else widgets::dimText("-");
    }
    ImGui::EndTable();
  }

  // Prices and the energy figure.
  ImGui::Dummy(ImVec2(0, 6));
  widgets::sectionHeader("PRICES AND ENERGY");
  widgets::dimText("Per-million-token prices for paid models (local models cost nothing but power).");
  ImGui::SetNextItemWidth(200);
  ImGui::InputTextWithHint("##pm", "model id", &gPriceModel);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(80);
  ImGui::InputTextWithHint("##pin", "$ in", &gPriceIn);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(80);
  ImGui::InputTextWithHint("##pout", "$ out", &gPriceOut);
  ImGui::SameLine();
  if (ImGui::SmallButton("Set price")) {
    const std::string model = core::trim(gPriceModel);
    if (!model.empty()) {
      Json& table = settings().extra["usagePrices"];
      if (!table.is_object()) table = Json::object();
      table[model] = Json::array({std::atof(gPriceIn.c_str()), std::atof(gPriceOut.c_str())});
      settings().save();
      gPriceModel.clear();
      gPriceIn.clear();
      gPriceOut.clear();
    }
  }
  for (const auto& [model, p] : prices.perMillion) {
    ImGui::PushID(model.c_str());
    widgets::dimText("%s: $%.2f in / $%.2f out per 1M", model.c_str(), p.first, p.second);
    ImGui::SameLine();
    if (ImGui::SmallButton("x")) {
      settings().extra["usagePrices"].erase(model);
      settings().save();
    }
    ImGui::PopID();
  }
  float joules = static_cast<float>(prices.joulesPerToken);
  ImGui::SetNextItemWidth(220);
  if (ImGui::SliderFloat("joules per local token", &joules, 0.05f, 3.0f, "%.2f J")) {
    settings().extra["usageJoulesPerToken"] = joules;
    settings().save();
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("An M-series Mac drawing ~30 W at ~50 tok/s is about 0.6 J per token;\nset it from your own power readings for a better estimate.");
  }
}

}  // namespace app
