// Reading mode in the app (#63): the toggle (header button, Cmd+Shift+R,
// palette), the sidebar tuck, per-message hover tracking so controls show
// only under the pointer, and the Settings card.
#include <map>

#include "app.h"
#include "core/common.h"
#include "core/readingmode.h"
#include "core/settings.h"
#include "pane_common.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

struct Rect {
  ImVec2 min, max;
};
std::map<std::string, Rect> gRects;   // message id -> block rect from the last frame
bool gSidebarWasVisible = true;

}  // namespace

readingmode::Prefs App::readingPrefs() const { return readingmode::fromJson(core::Settings::instance().extra); }
bool App::readingMode() const { return readingPrefs().enabled; }

void App::setReadingMode(bool on) {
  readingmode::Prefs p = readingPrefs();
  if (p.enabled == on) return;
  p.enabled = on;
  if (on && p.hideSidebar) {
    gSidebarWasVisible = sidebarVisible;
    sidebarVisible = false;
  } else if (!on && p.hideSidebar) {
    sidebarVisible = gSidebarWasVisible;
  }
  settings().extra["readingMode"] = readingmode::toJson(p);
  settings().save();
  widgets::toastInfo(on ? "Reading mode on - controls show when you hover a message" : "Reading mode off");
}

void App::toggleReadingMode() { setReadingMode(!readingMode()); }

void App::noteMessageRect(const std::string& messageId, const ImVec2& min, const ImVec2& max) { gRects[messageId] = Rect{min, max}; }

bool App::messageControlsVisible(const std::string& messageId) const {
  const readingmode::Prefs p = readingPrefs();
  bool hovered = false;
  auto it = gRects.find(messageId);
  if (it != gRects.end()) hovered = ImGui::IsMouseHoveringRect(ImVec2(it->second.min.x - 20.0f, it->second.min.y - 8.0f), ImVec2(it->second.max.x + 20.0f, it->second.max.y + 40.0f), false);
  return readingmode::showControls(p, hovered, focusedMessageId == messageId);
}

void App::drawReadingModeSection(float width) {
  card("readingmode", [&] {
    readingmode::Prefs p = readingPrefs();
    bool changed = false;
    ImGui::PushTextWrapPos(width - 20.0f);
    ImGui::TextUnformatted("A narrower measure, larger text and more air between lines; the sidebar tucks away and the buttons under each message appear only when you hover it. Toggle with the book button in the chat header or Cmd+Shift+R.");
    ImGui::PopTextWrapPos();
    if (ImGui::Checkbox("Reading mode", &p.enabled)) {
      settings().extra["readingMode"] = readingmode::toJson(p);
      settings().save();
      setReadingMode(p.enabled);   // handles the sidebar and the toast
      p = readingPrefs();
    }
    ImGui::SetNextItemWidth(220);
    changed |= ImGui::SliderFloat("Measure (px)", &p.measure, 480.0f, 1000.0f, "%.0f");
    ImGui::SetNextItemWidth(220);
    changed |= ImGui::SliderFloat("Text size", &p.textScale, 1.0f, 1.6f, "%.2fx");
    ImGui::SetNextItemWidth(220);
    changed |= ImGui::SliderFloat("Line spacing", &p.lineSpacing, 1.0f, 2.0f, "%.1fx");
    changed |= ImGui::Checkbox("Hide the sidebar while reading", &p.hideSidebar);
    changed |= ImGui::Checkbox("Show message controls only on hover", &p.controlsOnHover);
    if (changed) {
      settings().extra["readingMode"] = readingmode::toJson(p);
      settings().save();
    }
    widgets::dimText("%s%s", readingmode::summary(p).c_str(), p.enabled ? " - on" : " - off");
  }, width);
}

}  // namespace app
