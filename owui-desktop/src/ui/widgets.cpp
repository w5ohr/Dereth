#include "ui/widgets.h"

#include <cmath>
#include <cstdarg>
#include <deque>
#include <mutex>

#include "core/common.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include "ui/icons.h"
#include "ui/theme.h"

#include <nlohmann/json.hpp>

namespace widgets {
namespace {

struct ToastItem {
  ToastLevel level;
  std::string text;
  double born;
};

std::deque<ToastItem> gToasts;
std::mutex gToastMutex;
}  // namespace
bool toastsVisible() {
  std::lock_guard<std::mutex> lock(gToastMutex);
  return !gToasts.empty();
}
namespace {

ImU32 levelColor(ToastLevel level) {
  const auto& p = theme::palette();
  switch (level) {
    case ToastLevel::Success: return p.success;
    case ToastLevel::Warning: return p.warning;
    case ToastLevel::Error: return p.danger;
    default: return p.accent;
  }
}

icons::Icon levelIcon(ToastLevel level) {
  switch (level) {
    case ToastLevel::Success: return icons::Icon::Check;
    case ToastLevel::Warning: return icons::Icon::Warning;
    case ToastLevel::Error: return icons::Icon::Warning;
    default: return icons::Icon::Info;
  }
}

}  // namespace

void toast(ToastLevel level, const std::string& text) {
  std::lock_guard<std::mutex> lock(gToastMutex);
  gToasts.push_back({level, text, core::nowMillis()});
  if (gToasts.size() > 6) gToasts.pop_front();
}

void toastInfo(const std::string& text) { toast(ToastLevel::Info, text); }
void toastError(const std::string& text) { toast(ToastLevel::Error, text); }

void renderToasts() {
  std::deque<ToastItem> items;
  {
    std::lock_guard<std::mutex> lock(gToastMutex);
    const double now = core::nowMillis();
    while (!gToasts.empty() && now - gToasts.front().born > 6000.0) gToasts.pop_front();
    items = gToasts;
  }
  if (items.empty()) return;

  const ImGuiViewport* vp = ImGui::GetMainViewport();
  float y = vp->WorkPos.y + vp->WorkSize.y - 18.0f;
  int index = 0;
  for (auto it = items.rbegin(); it != items.rend(); ++it, ++index) {
    const double age = core::nowMillis() - it->born;
    float alpha = 1.0f;
    if (age > 5000.0) alpha = static_cast<float>(1.0 - (age - 5000.0) / 1000.0);
    alpha = ImClamp(alpha, 0.0f, 1.0f);

    ImGui::SetNextWindowBgAlpha(0.96f * alpha);
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - 18.0f, y), ImGuiCond_Always,
                            ImVec2(1.0f, 1.0f));
    char id[32];
    snprintf(id, sizeof(id), "##toast%d", index);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    if (ImGui::Begin(id, nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs)) {
      ImVec2 pos = ImGui::GetCursorScreenPos();
      icons::draw(ImGui::GetWindowDrawList(), levelIcon(it->level),
                  ImVec2(pos.x + 8, pos.y + ImGui::GetTextLineHeight() * 0.5f), 15.0f,
                  levelColor(it->level));
      ImGui::Dummy(ImVec2(18, ImGui::GetTextLineHeight()));
      ImGui::SameLine();
      ImGui::PushTextWrapPos(380.0f);
      ImGui::TextUnformatted(it->text.c_str());
      ImGui::PopTextWrapPos();
      y = ImGui::GetWindowPos().y - 8.0f;
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
  }
}

void spinner(const char* id, float radius, float thickness, ImU32 color) {
  ImGui::PushID(id);
  ImVec2 pos = ImGui::GetCursorScreenPos();
  ImVec2 size(radius * 2.0f, radius * 2.0f);
  ImGui::Dummy(size);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float t = static_cast<float>(ImGui::GetTime());
  const ImVec2 center(pos.x + radius, pos.y + radius);
  const int segments = 32;
  const float start = fmodf(t * 3.4f, 6.2831f);
  dl->PathClear();
  for (int i = 0; i <= segments; ++i) {
    const float a = start + (i / static_cast<float>(segments)) * 4.2f;
    dl->PathLineTo(ImVec2(center.x + cosf(a) * radius, center.y + sinf(a) * radius));
  }
  dl->PathStroke(color ? color : theme::palette().accent, thickness);
  ImGui::PopID();
}

void loadingRow(const char* text) {
  spinner("##loading", 7.0f, 2.0f);
  ImGui::SameLine();
  ImGui::AlignTextToFramePadding();
  ImGui::TextColored(theme::toVec4(theme::palette().textDim), "%s", text);
}

void pill(const std::string& text, ImU32 bg, ImU32 fg) {
  ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
  ImVec2 pos = ImGui::GetCursorScreenPos();
  ImVec2 size(textSize.x + 14.0f, textSize.y + 6.0f);
  ImGui::Dummy(size);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bg, size.y * 0.5f);
  dl->AddText(ImVec2(pos.x + 7.0f, pos.y + 3.0f), fg ? fg : theme::palette().text, text.c_str());
}

void sectionHeader(const char* text) {
  ImGui::Spacing();
  theme::pushBold();
  ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(theme::palette().textFaint));
  ImGui::TextUnformatted(text);
  ImGui::PopStyleColor();
  theme::pop();
  ImGui::Spacing();
}

void dimText(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(theme::palette().textDim));
  ImGui::TextV(fmt, args);
  ImGui::PopStyleColor();
  va_end(args);
}

void centeredText(const char* text) {
  const float width = ImGui::GetContentRegionAvail().x;
  const float textWidth = ImGui::CalcTextSize(text).x;
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (width - textWidth) * 0.5f);
  ImGui::TextUnformatted(text);
}

bool linkText(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(theme::palette().accent));
  ImGui::TextUnformatted(text);
  ImGui::PopStyleColor();
  const bool hovered = ImGui::IsItemHovered();
  if (hovered) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImVec2 min = ImGui::GetItemRectMin();
    ImVec2 max = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(min.x, max.y - 1), ImVec2(max.x, max.y - 1),
                                        theme::palette().accent, 1.0f);
  }
  return hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
}

bool primaryButton(const char* label, ImVec2 size) {
  const auto& p = theme::palette();
  ImGui::PushStyleColor(ImGuiCol_Button, theme::toVec4(p.accent));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::toVec4(p.accentHover));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::toVec4(p.accentHover));
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
  bool clicked = ImGui::Button(label, size);
  ImGui::PopStyleColor(4);
  return clicked;
}

bool ghostButton(const char* label, ImVec2 size) {
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
  bool clicked = ImGui::Button(label, size);
  ImGui::PopStyleColor();
  return clicked;
}

bool dangerButton(const char* label, ImVec2 size) {
  const auto& p = theme::palette();
  ImGui::PushStyleColor(ImGuiCol_Button, theme::toVec4(p.danger));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::toVec4(p.danger));
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
  bool clicked = ImGui::Button(label, size);
  ImGui::PopStyleColor(3);
  return clicked;
}

bool searchField(const char* id, std::string* value, const char* placeholder, float width) {
  ImGui::PushID(id);
  const float w = width > 0.0f ? width : ImGui::GetContentRegionAvail().x;
  ImVec2 pos = ImGui::GetCursorScreenPos();
  icons::draw(ImGui::GetWindowDrawList(), icons::Icon::Search,
              ImVec2(pos.x + 15.0f, pos.y + ImGui::GetFrameHeight() * 0.5f), 13.0f,
              theme::palette().textFaint);
  ImGui::SetCursorScreenPos(ImVec2(pos.x + 26.0f, pos.y));
  ImGui::SetNextItemWidth(w - 26.0f);
  bool changed = ImGui::InputTextWithHint("##field", placeholder, value);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRect(pos, ImVec2(pos.x + w, pos.y + ImGui::GetFrameHeight()), theme::palette().border,
              8.0f, 1.0f);
  ImGui::PopID();
  return changed;
}

bool confirmModal(const char* id, const char* message, const char* confirmLabel) {
  bool confirmed = false;
  if (ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::PushTextWrapPos(380.0f);
    ImGui::TextUnformatted(message);
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
    ImGui::SameLine();
    if (dangerButton(confirmLabel, ImVec2(120, 0))) {
      confirmed = true;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  return confirmed;
}

void Card::begin(ImU32 bg, float r) {
  start = ImGui::GetCursorScreenPos();
  color = bg ? bg : theme::palette().panel;
  rounding = r;
  ImGui::GetWindowDrawList()->ChannelsSplit(2);
  ImGui::GetWindowDrawList()->ChannelsSetCurrent(1);
  ImGui::BeginGroup();
}

void Card::end(float padding) {
  ImGui::EndGroup();
  ImVec2 min(start.x - padding, start.y - padding);
  ImVec2 max(start.x + ImGui::GetItemRectSize().x + padding,
             start.y + ImGui::GetItemRectSize().y + padding);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->ChannelsSetCurrent(0);
  dl->AddRectFilled(min, max, color, rounding);
  dl->ChannelsMerge();
}

void beginDisabled(bool disabled) {
  if (disabled) ImGui::BeginDisabled();
}
void endDisabled(bool disabled) {
  if (disabled) ImGui::EndDisabled();
}

std::string humanJson(const std::string& raw) {
  nlohmann::json j = nlohmann::json::parse(raw, nullptr, false);
  if (j.is_discarded()) return raw;
  return j.dump(2);
}

}  // namespace widgets
