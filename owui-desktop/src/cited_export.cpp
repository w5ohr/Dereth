// Cited export in the app: the "Copy with sources" menu under a reply and the
// palette commands - Markdown with footnotes, BibTeX, or a plain source list,
// built from the reply's server citations and its local-document citations.
#include "app.h"
#include "core/citeexport.h"
#include "core/common.h"
#include "platform.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

std::vector<std::string> localDocNames(const ChatDoc& chat) {
  std::vector<std::string> names;
  for (const std::string& id : chat.branch()) {
    auto it = chat.messages.find(id);
    if (it == chat.messages.end()) continue;
    for (const auto& f : it->second.files) {
      if (f.is_object() && f.value("type", "") == "local_document") names.push_back(f.value("name", ""));
    }
  }
  return names;
}

}  // namespace

bool App::replyHasSources(const Message& message) const {
  if (!message.sources.empty()) return true;
  return !citeexport::fromLocalCitations(message.content, localDocNames(chat)).empty();
}

std::string App::citedExport(const Message& message, int format) {
  const std::vector<citeexport::Source> server = citeexport::fromServerSources(message.sources);
  const std::vector<citeexport::Source> local = citeexport::fromLocalCitations(message.content, localDocNames(chat));
  std::vector<citeexport::Source> all = server;
  all.insert(all.end(), local.begin(), local.end());
  const std::string content = unredact(message.content);
  switch (format) {
    case 1: return citeexport::bibtex(all, core::formatTime(core::nowSeconds(), "%Y-%m-%d"));
    case 2: return citeexport::plainList(all);
    default: return citeexport::markdownWithFootnotes(content, server, local);
  }
}

void App::drawCitedCopyButton(const Message& message) {
  if (!replyHasSources(message)) return;
  ImGui::SameLine();
  if (icons::button("citecopy", icons::Icon::Book, "Copy with sources (Markdown footnotes, BibTeX, list)")) ImGui::OpenPopup("##citecopy");
  if (ImGui::BeginPopup("##citecopy")) {
    if (ImGui::MenuItem("Copy as Markdown with footnotes")) {
      platform::setClipboardText(citedExport(message, 0));
      widgets::toast(widgets::ToastLevel::Success, "Copied with footnoted sources");
    }
    if (ImGui::MenuItem("Copy sources as BibTeX")) {
      platform::setClipboardText(citedExport(message, 1));
      widgets::toast(widgets::ToastLevel::Success, "Copied BibTeX entries");
    }
    if (ImGui::MenuItem("Copy source list")) {
      platform::setClipboardText(citedExport(message, 2));
      widgets::toast(widgets::ToastLevel::Success, "Copied the source list");
    }
    ImGui::EndPopup();
  }
}

void App::copyLastReplyCited(int format) {
  std::string last;
  for (const std::string& id : chat.branch()) {
    if (chat.messages.at(id).role == "assistant") last = id;
  }
  if (last.empty()) {
    widgets::toastInfo("No reply in this chat yet");
    return;
  }
  const Message& message = chat.messages.at(last);
  if (!replyHasSources(message)) {
    widgets::toastInfo("The last reply has no sources to cite");
    return;
  }
  platform::setClipboardText(citedExport(message, format));
  widgets::toast(widgets::ToastLevel::Success, format == 1 ? "Copied BibTeX entries" : format == 2 ? "Copied the source list" : "Copied with footnoted sources");
}

}  // namespace app
