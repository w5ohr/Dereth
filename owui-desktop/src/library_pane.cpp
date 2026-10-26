// Saved answers library in the app (#148): the archive button under a reply
// saves it to a global, local shelf (support/library.json); the Library pane
// searches, tags, annotates, reopens and exports what was saved.
#include <set>

#include "app.h"
#include "core/common.h"
#include "core/library.h"
#include "imgui_stdlib.h"
#include "pane_common.h"
#include "platform.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

std::vector<library::Entry> gLib;
bool gLoaded = false;
std::string gQuery, gTagFilter;
std::string gEditing, gEditNote, gEditTags;

std::string libraryPath() { return core::supportDir() + "/library.json"; }

void loadLibrary() {
  if (gLoaded) return;
  gLoaded = true;
  bool ok = false;
  const std::string text = core::readFile(libraryPath(), &ok);
  if (ok) gLib = library::fromJson(library::Json::parse(text, nullptr, false));
}

void saveLibrary() { core::writeFile(libraryPath(), library::toJson(gLib).dump(2)); }

}  // namespace

bool App::isInLibrary(const std::string& messageId) const {
  loadLibrary();
  return library::find(gLib, chat.id, messageId) != nullptr;
}

void App::saveToLibrary(const std::string& messageId) {
  loadLibrary();
  auto it = chat.messages.find(messageId);
  if (it == chat.messages.end()) return;
  if (const library::Entry* existing = library::find(gLib, chat.id, messageId)) {
    library::remove(gLib, existing->id);
    saveLibrary();
    widgets::toastInfo("Removed from the library");
    return;
  }
  library::Entry e;
  e.chatId = chat.id;
  e.chatTitle = chat.title;
  e.messageId = messageId;
  e.role = it->second.role;
  e.model = it->second.modelName.empty() ? it->second.model : it->second.modelName;
  e.text = unredact(it->second.content);
  library::add(gLib, e);
  saveLibrary();
  widgets::toast(widgets::ToastLevel::Success, "Saved to the library (" + std::to_string(gLib.size()) + " answer" + (gLib.size() == 1 ? "" : "s") + ")");
}

void App::saveLastReplyToLibrary() {
  std::string last;
  for (const std::string& id : chat.branch()) {
    const Message& m = chat.messages.at(id);
    if (m.role == "assistant" && !m.content.empty()) last = id;
  }
  if (last.empty()) widgets::toastInfo("No reply in this chat yet");
  else saveToLibrary(last);
}

void App::drawLibraryButton(const std::string& messageId) {
  ImGui::SameLine();
  const bool saved = isInLibrary(messageId);
  if (icons::button("library", icons::Icon::Archive, saved ? "Remove from the library" : "Save to the library (bookmarks across every chat)", 0.0f,
                    saved ? theme::palette().accent : 0)) {
    saveToLibrary(messageId);
  }
}

void App::drawLibraryPane() {
  loadLibrary();
  const auto& palette = theme::palette();
  const std::string query = gQuery + (gTagFilter.empty() ? "" : " #" + gTagFilter);
  const std::vector<library::Entry> shown = library::search(gLib, query);
  paneHeader("Library", "Saved answers from every chat, with your notes and tags - kept on this Mac", [&] {
    ImGui::SameLine(ImGui::GetWindowWidth() - 330.0f);
    if (ImGui::Button("Export Markdown...", ImVec2(150, 30))) {
      const std::string path = platform::saveFile("saved-answers.md");
      if (!path.empty()) {
        core::writeFile(path, library::markdown(shown));
        widgets::toast(widgets::ToastLevel::Success, "Saved " + std::to_string(shown.size()) + " answers to " + path);
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Copy Markdown", ImVec2(130, 30))) {
      platform::setClipboardText(library::markdown(shown));
      widgets::toast(widgets::ToastLevel::Success, "Copied " + std::to_string(shown.size()) + " answers as Markdown");
    }
  });
  paneBody("##library");
  const float width = ImGui::GetContentRegionAvail().x - 18.0f;

  card("search", [&] {
    ImGui::SetNextItemWidth(width - 30.0f);
    ImGui::InputTextWithHint("##libquery", "Search text, notes, chat titles and models; #tag narrows to a tag", &gQuery);
    const auto tags = library::tagCounts(gLib);
    if (!tags.empty()) {
      widgets::dimText("Tags:");
      for (const auto& [tag, count] : tags) {
        ImGui::SameLine();
        const bool on = gTagFilter == tag;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, theme::toVec4(palette.accent));
        if (ImGui::SmallButton(("#" + tag + " (" + std::to_string(count) + ")").c_str())) gTagFilter = on ? "" : tag;
        if (on) ImGui::PopStyleColor();
      }
    }
    std::set<std::string> chats;
    for (const library::Entry& e : gLib) chats.insert(e.chatId);
    widgets::dimText("%zu saved answer%s across %zu chat%s%s", gLib.size(), gLib.size() == 1 ? "" : "s", chats.size(), chats.size() == 1 ? "" : "s",
                     shown.size() != gLib.size() ? (" - " + std::to_string(shown.size()) + " shown").c_str() : "");
  }, width);

  if (shown.empty()) {
    widgets::dimText(gLib.empty() ? "Nothing saved yet. The archive button under a reply saves it here; the palette has \"Save the last reply to the library\"." : "Nothing matches that search.");
  }
  for (const library::Entry& e : shown) {
    ImGui::PushID(e.id.c_str());
    card("entry", [&] {
      theme::pushBold();
      ImGui::TextUnformatted(e.chatTitle.empty() ? "Untitled chat" : e.chatTitle.c_str());
      theme::pop();
      ImGui::SameLine();
      widgets::dimText("%s%s%s", core::relativeTime(e.at).c_str(), e.model.empty() ? "" : " - ", e.model.c_str());
      ImGui::PushTextWrapPos(width - 30.0f);
      ImGui::TextUnformatted(library::snippet(e.text, 600).c_str());
      ImGui::PopTextWrapPos();
      if (gEditing == e.id) {
        ImGui::SetNextItemWidth(width - 30.0f);
        ImGui::InputTextWithHint("##note", "note", &gEditNote);
        ImGui::SetNextItemWidth(width - 30.0f);
        ImGui::InputTextWithHint("##tags", "tags, comma or space separated", &gEditTags);
        if (ImGui::SmallButton("Save")) {
          for (library::Entry& stored : gLib) {
            if (stored.id == e.id) {
              stored.note = core::trim(gEditNote);
              stored.tags = library::parseTags(gEditTags);
            }
          }
          saveLibrary();
          gEditing.clear();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Cancel")) gEditing.clear();
      } else {
        if (!e.note.empty()) {
          ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.accent));
          ImGui::PushTextWrapPos(width - 30.0f);
          ImGui::Text("Note: %s", e.note.c_str());
          ImGui::PopTextWrapPos();
          ImGui::PopStyleColor();
        }
        if (!e.tags.empty()) widgets::dimText("#%s", core::join(e.tags, "  #").c_str());
        if (ImGui::SmallButton("Open chat")) {
          pendingFocusMessage = e.messageId;
          openChat(e.chatId);
          pane = Pane::Chat;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy")) {
          platform::setClipboardText(e.text);
          widgets::toastInfo("Copied");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(e.note.empty() && e.tags.empty() ? "Add note or tags" : "Edit note or tags")) {
          gEditing = e.id;
          gEditNote = e.note;
          gEditTags = core::join(e.tags, ", ");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) {
          library::remove(gLib, e.id);
          saveLibrary();
        }
      }
    }, width);
    ImGui::PopID();
  }
  paneEnd();
}

}  // namespace app
