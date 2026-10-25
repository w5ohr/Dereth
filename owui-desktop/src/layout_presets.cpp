// Layout presets in the app (#69): capture the current arrangement, apply a
// layout (sidebar, side column beside or below, reading mode, float, window
// size), save / delete named layouts, the Settings card and the shortcuts.
#include "app.h"
#include "core/async.h"
#include "core/common.h"
#include "core/layouts.h"
#include "core/settings.h"
#include "imgui_stdlib.h"
#include "native.h"
#include "pane_common.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {
std::string gNewName;
std::string gLastApplied;
}  // namespace

layouts::Layout App::captureLayout(const std::string& name) const {
  layouts::Layout l;
  l.name = name;
  l.sidebar = sidebarVisible;
  l.sidebarWidth = core::Settings::instance().sidebarWidth;
  l.column = showArtifacts ? "artifacts" : !referenceChatId.empty() ? "reference" : showTree ? "tree" : showPins ? "pins" : "none";
  l.columnBelow = sideColumnBelow;
  l.reading = readingMode();
  l.floating = floating;
  const ImVec2 size = ImGui::GetMainViewport()->Size;
  l.windowW = size.x;
  l.windowH = size.y;
  l.referenceChatId = referenceChatId;
  return l;
}

void App::openReferenceChat(const std::string& id) {
  if (id.empty() || id == chat.id) return;
  referenceChatId = id;
  async::run([this, id] {
    api::Result res = api_.chat(id);
    async::post([this, res, id] {
      if (!res.ok || referenceChatId != id) return;
      referenceChat = ChatDoc::fromJson(res.data.value("chat", Json::object()), id);
    });
  });
}

void App::applyLayout(const layouts::Layout& l) {
  pane = Pane::Chat;
  sidebarVisible = l.sidebar;
  settings().sidebarWidth = l.sidebarWidth;
  showArtifacts = l.column == "artifacts";
  showTree = l.column == "tree";
  showPins = l.column == "pins";
  showRetrieval = false;
  showDocument = false;
  if (l.column == "reference") {
    std::string id = l.referenceChatId;
    if (id.empty() || id == chat.id) {
      // No chat remembered for the column: the most recent other chat.
      for (const ChatSummary& summary : chats) {
        if (summary.id != chat.id) {
          id = summary.id;
          break;
        }
      }
    }
    if (!id.empty() && id != referenceChatId) openReferenceChat(id);
    if (id.empty()) widgets::toastInfo("No other chat to show as the reference yet");
  } else {
    referenceChatId.clear();
    referenceChat = ChatDoc();
  }
  sideColumnBelow = l.columnBelow;
  if (readingMode() != l.reading) setReadingMode(l.reading);
  if (floating != l.floating) toggleFloating();
  if (l.windowW > 0.0f && l.windowH > 0.0f) native::setWindowSize(l.windowW, l.windowH);
  settings().save();
  gLastApplied = l.name;
  widgets::toastInfo("Layout: " + l.name + " (" + layouts::summary(l) + ")");
}

bool App::applyLayoutByIndex(int index) {
  const std::vector<layouts::Layout> list = layouts::all(settings().extra);
  if (index < 0 || index >= static_cast<int>(list.size())) return false;
  applyLayout(list[static_cast<size_t>(index)]);
  return true;
}

void App::saveLayoutAs(const std::string& nameIn) {
  const std::string name = core::trim(nameIn);
  if (name.empty()) return;
  std::vector<layouts::Layout> list = layouts::saved(settings().extra);
  const layouts::Layout l = captureLayout(name);
  const int at = layouts::indexOf(list, name);
  if (at >= 0) list[static_cast<size_t>(at)] = l;
  else list.push_back(l);
  settings().extra["layouts"] = layouts::toSaved(list);
  settings().save();
  widgets::toast(widgets::ToastLevel::Success, "Saved layout \"" + name + "\" - " + layouts::summary(l));
}

void App::deleteLayout(const std::string& name) {
  std::vector<layouts::Layout> list = layouts::saved(settings().extra);
  const int at = layouts::indexOf(list, name);
  if (at < 0) return;
  list.erase(list.begin() + at);
  settings().extra["layouts"] = layouts::toSaved(list);
  settings().save();
}

void App::drawLayoutsSection(float width) {
  const auto& palette = theme::palette();
  ImGui::PushTextWrapPos(width - 20.0f);
  ImGui::TextUnformatted("A layout is the chat screen's arrangement: the sidebar and its width, which side column shows (reference chat, pins, conversation tree, artifacts) and whether it sits beside or below the transcript, reading mode, float-on-top and the window size. Apply one here, from the palette (\"Layout: ...\") or with Cmd+Option+1 to 4 for the first four.");
  ImGui::PopTextWrapPos();
  const std::vector<layouts::Layout> list = layouts::all(settings().extra);
  if (ImGui::BeginTable("##layouts", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp, ImVec2(width - 20.0f, 0))) {
    ImGui::TableSetupColumn("Layout", ImGuiTableColumnFlags_WidthFixed, 150.0f);
    ImGui::TableSetupColumn("What it does");
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 130.0f);
    for (size_t i = 0; i < list.size(); ++i) {
      const layouts::Layout& l = list[i];
      ImGui::PushID(static_cast<int>(i));
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      if (l.name == gLastApplied) ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.accent));
      ImGui::TextUnformatted(l.name.c_str());
      if (l.name == gLastApplied) ImGui::PopStyleColor();
      if (i < 4) {
        ImGui::SameLine();
        widgets::dimText("\xe2\x8c\x98\xe2\x8c\xa5%d", static_cast<int>(i) + 1);
      }
      ImGui::TableSetColumnIndex(1);
      widgets::dimText("%s%s", layouts::summary(l).c_str(), l.builtin ? "  (built in)" : "");
      ImGui::TableSetColumnIndex(2);
      if (ImGui::SmallButton("Apply")) applyLayout(l);
      if (!l.builtin) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete")) deleteLayout(l.name);
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  ImGui::SetNextItemWidth(220);
  const bool enter = ImGui::InputTextWithHint("##layoutname", "Name for the current arrangement", &gNewName, ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::SameLine();
  widgets::beginDisabled(core::trim(gNewName).empty());
  if (ImGui::Button("Save current layout") || (enter && !core::trim(gNewName).empty())) {
    saveLayoutAs(gNewName);
    gNewName.clear();
  }
  widgets::endDisabled(core::trim(gNewName).empty());
  ImGui::SameLine();
  widgets::dimText("now: %s", layouts::summary(captureLayout("")).c_str());
  ImGui::Checkbox("Side column below the transcript (horizontal split)", &sideColumnBelow);
}

}  // namespace app
