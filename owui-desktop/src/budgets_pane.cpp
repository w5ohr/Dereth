// Token budgets in the app: the caps live in settings extra["budgets"]; every
// send is checked against today's and this chat's spend from the usage log
// (a warning toast near the cap, a modal with "Send anyway" past it); the
// Usage tab has the card to set them and a gauge; the chat header shows a
// small pill while a cap is set.
#include "app.h"
#include "core/budgets.h"
#include "core/common.h"
#include "core/usagelog.h"
#include "imgui_stdlib.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

std::string usagePath() { return core::supportDir() + "/usage.jsonl"; }

std::vector<usagelog::Record> gRecords;
double gLoadedAt = -1e9;
double gWarnedAt = -1e9;
std::string gWarnedFor;

budgets::Limits limits() { return budgets::fromJson(core::Settings::instance().extra.value("budgets", Json::object())); }

usagelog::Prices prices() {
  usagelog::Prices p;
  const Json table = core::Settings::instance().extra.value("usagePrices", Json::object());
  if (table.is_object()) {
    for (const auto& [model, v] : table.items()) {
      if (v.is_array() && v.size() == 2 && v[0].is_number() && v[1].is_number()) p.perMillion[model] = {v[0].get<double>(), v[1].get<double>()};
    }
  }
  return p;
}

const std::vector<usagelog::Record>& records(bool force) {
  if (force || core::nowMillis() - gLoadedAt > 10000.0) {
    gRecords = usagelog::load(usagePath(), 0);
    gLoadedAt = core::nowMillis();
  }
  return gRecords;
}

// Edit fields for the card.
std::string gChatCap, gDayCap, gUsdCap;
bool gFieldsLoaded = false;

}  // namespace

budgets::Spend App::budgetSpend(bool force) {
  return budgets::spend(records(force), chat.id, prices(), core::nowSeconds());
}

bool App::interceptBudget(const std::string& text) {
  const budgets::Limits l = limits();
  if (!l.any()) return false;
  const budgets::Spend s = budgetSpend(true);
  const int64_t about = static_cast<int64_t>(currentContextTokens()) + estimateTokens(text);
  const budgets::Verdict v = budgets::check(l, s, about);
  if (v.level == budgets::Level::Ok) return false;
  if (v.level == budgets::Level::Warn) {
    // One warning per cap per hour, not one per message.
    if (gWarnedFor != v.message && core::nowMillis() - gWarnedAt > 3600000.0) {
      gWarnedAt = core::nowMillis();
      gWarnedFor = v.message;
      widgets::toast(widgets::ToastLevel::Warning, "Budget: " + v.message);
    }
    return false;
  }
  budgetSheet.open = true;
  budgetSheet.justOpened = true;
  budgetSheet.text = text;
  budgetSheet.message = v.message;
  return true;
}

void App::drawBudgetSheet() {
  if (!budgetSheet.open) return;
  if (budgetSheet.justOpened) {
    ImGui::OpenPopup("Budget reached");
    budgetSheet.justOpened = false;
  }
  const auto& palette = theme::palette();
  bool keepOpen = true;
  ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_Appearing);
  if (!ImGui::BeginPopupModal("Budget reached", &keepOpen, ImGuiWindowFlags_NoSavedSettings)) {
    if (!keepOpen) budgetSheet.open = false;
    return;
  }
  ImGui::PushTextWrapPos(490.0f);
  ImGui::TextUnformatted(budgetSheet.message.c_str());
  ImGui::PopTextWrapPos();
  widgets::dimText("Caps are set under Tools > Usage > Budgets. Sending anyway goes past them for this message only.");
  ImGui::Dummy(ImVec2(0, 6));
  if (widgets::primaryButton("Send anyway", ImVec2(130, 0))) {
    const std::string text = budgetSheet.text;
    budgetSheet.open = false;
    ImGui::CloseCurrentPopup();
    sendingOverBudget_ = true;
    sendMessage(text);
    sendingOverBudget_ = false;
  }
  ImGui::SameLine();
  if (ImGui::Button("Hold it", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    composer = budgetSheet.text;   // back into the composer, nothing lost
    ++composerGeneration;
    budgetSheet.open = false;
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  if (ImGui::Button("Open budgets", ImVec2(120, 0))) {
    composer = budgetSheet.text;
    ++composerGeneration;
    budgetSheet.open = false;
    ImGui::CloseCurrentPopup();
    pane = Pane::Tools;
  }
  ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.textDim));
  ImGui::PopStyleColor();
  ImGui::EndPopup();
  if (!keepOpen) budgetSheet.open = false;
}

void App::drawBudgetPill() {
  const budgets::Limits l = limits();
  if (!l.any()) return;
  const budgets::Spend s = budgetSpend(false);
  const auto& palette = theme::palette();
  double worst = 0.0;
  if (l.dayTokens > 0) worst = std::max(worst, static_cast<double>(s.dayTokens) / static_cast<double>(l.dayTokens));
  if (l.dayUsd > 0) worst = std::max(worst, s.dayUsd / l.dayUsd);
  if (l.chatTokens > 0) worst = std::max(worst, static_cast<double>(s.chatTokens) / static_cast<double>(l.chatTokens));
  char buf[32];
  snprintf(buf, sizeof buf, "budget %.0f%%", worst * 100.0);
  widgets::pill(buf, palette.panelAlt, worst >= 1.0 ? palette.danger : worst >= l.warnAt ? palette.warning : palette.textDim);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", budgets::summary(l, s).c_str());
}

void App::drawBudgetsCard(float width) {
  const auto& palette = theme::palette();
  budgets::Limits l = limits();
  if (!gFieldsLoaded) {
    gFieldsLoaded = true;
    gChatCap = l.chatTokens ? std::to_string(l.chatTokens) : "";
    gDayCap = l.dayTokens ? std::to_string(l.dayTokens) : "";
    gUsdCap = l.dayUsd > 0 ? std::to_string(l.dayUsd).substr(0, std::to_string(l.dayUsd).find('.') + 3) : "";
  }
  ImGui::Dummy(ImVec2(0, 6));
  widgets::sectionHeader("BUDGETS");
  widgets::dimText("Caps on tokens per chat, tokens per day and dollars per day (from the prices below). A warning comes near a cap; past it, each send asks first. Leave a field empty for no cap.");
  ImGui::SetNextItemWidth(130);
  ImGui::InputTextWithHint("##bchat", "tokens / chat", &gChatCap, ImGuiInputTextFlags_CharsDecimal);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(130);
  ImGui::InputTextWithHint("##bday", "tokens / day", &gDayCap, ImGuiInputTextFlags_CharsDecimal);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(110);
  ImGui::InputTextWithHint("##busd", "$ / day", &gUsdCap, ImGuiInputTextFlags_CharsDecimal);
  ImGui::SameLine();
  float warn = static_cast<float>(l.warnAt * 100.0);
  ImGui::SetNextItemWidth(140);
  if (ImGui::SliderFloat("warn at", &warn, 50.0f, 100.0f, "%.0f%%", ImGuiSliderFlags_None)) {
    l.warnAt = warn / 100.0;
    settings().extra["budgets"] = budgets::toJson(l);
    settings().save();
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Save caps")) {
    l.chatTokens = std::atoll(gChatCap.c_str());
    l.dayTokens = std::atoll(gDayCap.c_str());
    l.dayUsd = std::atof(gUsdCap.c_str());
    settings().extra["budgets"] = budgets::toJson(l);
    settings().save();
    gWarnedFor.clear();
    widgets::toast(widgets::ToastLevel::Success, l.any() ? "Budgets saved" : "No caps - budgets off");
  }
  if (l.any()) {
    const budgets::Spend s = budgetSpend(false);
    auto gauge = [&](const char* label, double used, double cap, const std::string& text) {
      if (cap <= 0) return;
      const float f = static_cast<float>(std::min(1.0, used / cap));
      ImGui::PushStyleColor(ImGuiCol_PlotHistogram, theme::toVec4(f >= 1.0f ? palette.danger : f >= l.warnAt ? palette.warning : palette.accent));
      ImGui::ProgressBar(f, ImVec2(std::min(420.0f, width - 200.0f), 0), text.c_str());
      ImGui::PopStyleColor();
      ImGui::SameLine();
      widgets::dimText("%s", label);
    };
    char buf[64];
    gauge("today, tokens", static_cast<double>(s.dayTokens), static_cast<double>(l.dayTokens), budgets::tokens(s.dayTokens) + " / " + budgets::tokens(l.dayTokens));
    snprintf(buf, sizeof buf, "$%.2f / $%.2f", s.dayUsd, l.dayUsd);
    gauge("today, cost", s.dayUsd, l.dayUsd, buf);
    gauge("this chat, tokens", static_cast<double>(s.chatTokens), static_cast<double>(l.chatTokens), budgets::tokens(s.chatTokens) + " / " + budgets::tokens(l.chatTokens));
  }
}

}  // namespace app
