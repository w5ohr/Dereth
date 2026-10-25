// The conversation view: header, message list, composer.
#include <algorithm>

#include "app.h"
#include "core/async.h"
#include "core/common.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include "native.h"
#include "platform.h"
#include "ui/icons.h"
#include "ui/images.h"
#include "ui/markdown.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

constexpr float kMaxContentWidth = 820.0f;

std::string gTitleEdit;
bool gEditingTitle = false;
bool gShowToolPicker = false;

std::string formatUsage(const Json& usage) {
  if (!usage.is_object() || usage.empty()) return {};
  const int prompt = usage.value("prompt_tokens", 0);
  const int completion = usage.value("completion_tokens", 0);
  const double rate = usage.value("response_token/s", 0.0);
  char buffer[160];
  if (rate > 0.0) {
    snprintf(buffer, sizeof(buffer), "%d in / %d out - %.1f tok/s", prompt, completion, rate);
  } else {
    snprintf(buffer, sizeof(buffer), "%d in / %d out", prompt, completion);
  }
  return buffer;
}

}  // namespace

void App::drawChatPane() {
  const auto& palette = theme::palette();
  const float width = ImGui::GetContentRegionAvail().x;

  // ---------------------------------------------------------------- header
  ImGui::BeginChild("##chatheader", ImVec2(0, 54), ImGuiChildFlags_None);
  {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetWindowPos();
    dl->AddLine(ImVec2(origin.x, origin.y + 53), ImVec2(origin.x + width, origin.y + 53),
                palette.border);
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::Indent(10.0f);

    if (icons::button("togglesidebar", icons::Icon::Menu, "Toggle sidebar")) {
      sidebarVisible = !sidebarVisible;
    }
    ImGui::SameLine();

    // Model chips open the picker.
    std::string label;
    for (const std::string& id : selectedModels) {
      std::string name = id;
      for (const ModelInfo& info : models) {
        if (info.id == id) name = info.name;
      }
      if (!label.empty()) label += " + ";
      label += name;
    }
    if (label.empty()) label = modelsLoading ? "Loading models..." : "Select a model";
    ImGui::AlignTextToFramePadding();
    if (ImGui::Button((label + "  v##modelbtn").c_str())) showModelPicker = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Choose one or more models");

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.textDim));
    if (gEditingTitle) {
      ImGui::SetNextItemWidth(320);
      if (ImGui::InputText("##title", &gTitleEdit,
                           ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
        chat.title = gTitleEdit;
        saveActiveChat();
        refreshChats();
        gEditingTitle = false;
      }
      if (ImGui::IsItemDeactivated()) gEditingTitle = false;
    } else {
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(chat.title.c_str());
      if (ImGui::IsItemClicked() && !chat.id.empty()) {
        gEditingTitle = true;
        gTitleEdit = chat.title;
      }
    }
    ImGui::PopStyleColor();

    // Right-hand actions.
    const float actionsWidth = 444.0f;
    ImGui::SameLine(ImGui::GetWindowWidth() - actionsWidth);
    if (!streams_.empty()) {
      widgets::spinner("##busy", 8.0f, 2.0f);
      ImGui::SameLine();
    }
    if (icons::button("newchat", icons::Icon::Plus, "New chat")) newChat();
    ImGui::SameLine();
    if (icons::button("findinchat", icons::Icon::Search, "Find in chat (Cmd+F)", 0.0f,
                      showChatSearch ? palette.accent : 0)) {
      showChatSearch = !showChatSearch;
    }
    ImGui::SameLine();
    if (icons::button("presets", icons::Icon::Layers, "Presets")) ImGui::OpenPopup("##presets");
    drawPresetMenu();
    ImGui::SameLine();
    if (icons::button("compare", icons::Icon::Branch, "Compare sibling answers side by side",
                      0.0f, compareMode ? palette.accent : 0)) {
      compareMode = !compareMode;
    }
    ImGui::SameLine();
    if (icons::button("treeview", icons::Icon::Tree, "Conversation tree", 0.0f,
                      showTree ? palette.accent : 0)) {
      showTree = !showTree;
    }
    ImGui::SameLine();
    if (icons::button("pinsrail", icons::Icon::Star, "Pinned messages", 0.0f,
                      showPins ? palette.accent : 0)) {
      showPins = !showPins;
    }
    ImGui::SameLine();
    if (icons::button("inspector", icons::Icon::Info, "What was sent")) showContextInspector = true;
    ImGui::SameLine();
    if (icons::button("pinchat", icons::Icon::Pin, "Pin") && !chat.id.empty()) {
      const std::string id = chat.id;
      async::run([this, id] {
        api_.pinChat(id);
        async::post([this] { refreshChats(false); });
      });
    }
    ImGui::SameLine();
    if (icons::button("sharechat", icons::Icon::Share, "Share") && !chat.id.empty()) {
      const std::string id = chat.id;
      async::run([this, id] {
        api::Result res = api_.shareChat(id);
        async::post([this, res] {
          if (res.ok && res.data.contains("share_id")) {
            const std::string link = api_.server() + "/s/" + res.data["share_id"].get<std::string>();
            platform::setClipboardText(link);
            widgets::toast(widgets::ToastLevel::Success, "Share link copied");
          }
        });
      });
    }
    ImGui::SameLine();
    if (icons::button("chatparams", icons::Icon::Gear, "Sampling and system prompt", 0.0f,
                      hasSamplingParams() ? palette.accent : 0)) {
      ImGui::OpenPopup("##chatparams");
    }
    drawSamplingPanel();
    ImGui::SameLine();
    if (icons::button("structured", icons::Icon::Code, "Structured output (JSON schema)", 0.0f,
                      structuredEnabled() ? palette.accent : 0)) {
      ImGui::OpenPopup("##structured");
    }
    drawStructuredPanel();
    ImGui::SameLine();
    if (icons::button("chatmenu", icons::Icon::Menu, "More")) ImGui::OpenPopup("##chatmore");
    if (ImGui::BeginPopup("##chatmore")) {
      if (ImGui::MenuItem("Open in new window", "Shift+Cmd+N")) openChatInNewWindow(chat.id, false);
      if (ImGui::MenuItem("Open as floating mini window")) openChatInNewWindow(chat.id, true);
      if (ImGui::MenuItem(floating ? "Stop floating on top" : "Float on top", "Shift+Cmd+T")) toggleFloating();
      if (ImGui::MenuItem("Conversation tree", nullptr, showTree)) showTree = !showTree;
      ImGui::Separator();
      if (ImGui::MenuItem("Download chat as JSON") && !chat.id.empty()) {
        const std::string path = platform::saveFile(chat.title + ".json");
        if (!path.empty()) {
          core::writeFile(path, chat.toJson().dump(2));
          widgets::toast(widgets::ToastLevel::Success, "Saved to " + path);
        }
      }
      if (ImGui::BeginMenu("Export")) {
        if (ImGui::MenuItem("Markdown...")) exportChat("md");
        if (ImGui::MenuItem("HTML...")) exportChat("html");
        if (ImGui::MenuItem("PDF...")) exportChat("pdf");
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("Open reference chat")) {
        for (const ChatSummary& summary : chats) {
          if (summary.id == chat.id) continue;
          if (ImGui::MenuItem(summary.title.c_str())) {
            const std::string refId = summary.id;
            referenceChatId = refId;
            async::run([this, refId] {
              api::Result res = api_.chat(refId);
              async::post([this, res, refId] {
                if (!res.ok || referenceChatId != refId) return;
                referenceChat = ChatDoc::fromJson(res.data.value("chat", Json::object()), refId);
              });
            });
          }
        }
        ImGui::EndMenu();
      }
      if (ImGui::MenuItem("Compact chat") && !chat.id.empty()) compactChat();
      if (ImGui::MenuItem(voiceMode ? "Stop voice conversation" : "Voice conversation")) {
        voiceMode = !voiceMode;
      }
      if (ImGui::MenuItem("Copy transcript")) {
        std::string transcript;
        for (const std::string& id : chat.branch()) {
          const Message& message = chat.messages.at(id);
          transcript += (message.role == "user" ? "### You\n" : "### Assistant\n");
          transcript += message.content + "\n\n";
        }
        platform::setClipboardText(transcript);
        widgets::toast(widgets::ToastLevel::Success, "Transcript copied");
      }
      if (ImGui::MenuItem("Archive") && !chat.id.empty()) {
        const std::string id = chat.id;
        async::run([this, id] {
          api_.archiveChat(id);
          async::post([this] {
            refreshChats(false);
            newChat();
          });
        });
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Sign out")) signOut();
      ImGui::EndPopup();
    }
    ImGui::Unindent(10.0f);

    // Context pressure: estimated tokens in play against the model's window.
    {
      const std::string model = selectedModels.empty() ? "" : selectedModels.front();
      const int used = currentContextTokens();
      const int window = std::max(1, contextWindowFor(model));
      const float fraction = std::min(1.0f, static_cast<float>(used) / window);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      const ImVec2 origin = ImGui::GetWindowPos();
      const float y = origin.y + 51.0f;
      const ImU32 color = fraction > 0.85f ? palette.danger
                          : fraction > 0.6f ? palette.warning
                                            : palette.accent;
      dl->AddRectFilled(ImVec2(origin.x, y), ImVec2(origin.x + width * fraction, y + 2.0f), color);
      ImGui::SetCursorScreenPos(ImVec2(origin.x + width - 150.0f, y - 16.0f));
      ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.textFaint));
      if (used < 1000) {
        ImGui::Text("~%d / %dk ctx", used, window / 1000);
      } else {
        ImGui::Text("~%.1fk / %dk ctx", used / 1000.0, window / 1000);
      }
      ImGui::PopStyleColor();
    }
  }
  ImGui::EndChild();

  if (showChatSearch) {
    ImGui::BeginChild("##searchbar", ImVec2(0, ImGui::GetFrameHeight() + 10.0f));
    ImGui::Dummy(ImVec2(0, 3));
    ImGui::Indent(16.0f);
    drawChatSearchBar(width);
    ImGui::Unindent(16.0f);
    ImGui::EndChild();
  }

  if (!serverReachable || !offlineQueue.empty()) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::toVec4(palette.panelAlt));
    ImGui::BeginChild("##offline", ImVec2(0, ImGui::GetTextLineHeight() + 12.0f));
    ImGui::Dummy(ImVec2(0, 3));
    ImGui::Indent(16.0f);
    icons::inlineIcon(icons::Icon::Warning, palette.warning);
    ImGui::SameLine(0, 6);
    ImGui::Text("%s - %d message(s) queued", serverReachable ? "Sending queue" : "Server unreachable",
                static_cast<int>(offlineQueue.size()));
    ImGui::Unindent(16.0f);
    ImGui::EndChild();
    ImGui::PopStyleColor();
  }

  // ------------------------------------------------------------- messages
  // The native composer grows with its text; the box height it needs was
  // measured when the composer was drawn last frame.
  const float composerHeight = 128.0f + (composerTextHeight - 46.0f) +
                              (attachments.empty() ? 0.0f : 74.0f) +
                              (projectFolder.empty() ? 0.0f : 24.0f);
  const float sideWidth = showArtifacts ? 540.0f
                          : showDocument ? 560.0f
                          : !referenceChatId.empty() ? 420.0f
                          : showTree ? 380.0f
                          : (showPins ? 260.0f : 0.0f);
  ImGui::BeginChild("##messagearea", ImVec2(0, -composerHeight), ImGuiChildFlags_None);
  ImGui::BeginChild("##messages", ImVec2(sideWidth > 0.0f ? -sideWidth - 8.0f : 0.0f, 0),
                    ImGuiChildFlags_None);
  {
    const float available = ImGui::GetContentRegionAvail().x;
    const float contentWidth = std::min(kMaxContentWidth, available - 40.0f);
    const float marginX = std::max(16.0f, (available - contentWidth) * 0.5f);

    const std::vector<std::string> path = chat.branch();
    if (path.empty()) {
      // Empty state.
      ImGui::Dummy(ImVec2(0, std::max(40.0f, ImGui::GetContentRegionAvail().y * 0.26f)));
      ImGui::Indent(marginX);
      theme::pushScaled(1.9f);
      theme::pushBold();
      const std::string name = user_.value("name", "there");
      ImGui::Text("Hello, %s", name.c_str());
      theme::pop();
      ImGui::PopFont();
      ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.textDim));
      ImGui::TextUnformatted("How can I help you today?");
      ImGui::PopStyleColor();
      ImGui::Dummy(ImVec2(0, 18));

      static const char* kStarters[] = {
          "Explain this error message and how to fix it",
          "Summarise the key points of a document I'll paste",
          "Write a Python script that watches a folder for new files",
          "Draft a short, friendly status update for my team",
      };
      for (const char* starter : kStarters) {
        ImGui::PushID(starter);
        ImVec2 pos = ImGui::GetCursorScreenPos();
        const float rowHeight = ImGui::GetTextLineHeight() + 18.0f;
        if (ImGui::InvisibleButton("##starter", ImVec2(contentWidth, rowHeight))) {
          composer = starter;
        }
        const bool hovered = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pos, ImVec2(pos.x + contentWidth, pos.y + rowHeight),
                          hovered ? palette.panelAlt : palette.panel, 10.0f);
        dl->AddText(ImVec2(pos.x + 14, pos.y + 9), palette.textDim, starter);
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0, 6));
      }
      ImGui::Unindent(marginX);
    }

    const std::string lastAssistantId = [&] {
      for (auto it = path.rbegin(); it != path.rend(); ++it) {
        if (chat.messages.at(*it).role == "assistant") return *it;
      }
      return std::string();
    }();

    for (const std::string& messageId : path) {
      Message& message = chat.messages[messageId];
      ImGui::PushID(messageId.c_str());
      ImGui::Indent(marginX);

      const bool isUser = message.role == "user";
      ImGui::Dummy(ImVec2(0, 10));
      if (pendingScrollTo == messageId) {
        ImGui::SetScrollHereY(0.15f);
        pendingScrollTo.clear();
      }
      if (focusedMessageId == messageId) {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x - 12, pos.y - 4),
                                                  ImVec2(pos.x - 9, pos.y + 40), palette.accent, 2.0f);
      }
      if (showChatSearch && !chatSearchQuery.empty() &&
          core::containsNoCase(message.content, chatSearchQuery)) {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x - 12, pos.y - 4),
                                                  ImVec2(pos.x - 9, pos.y + 40), palette.warning, 2.0f);
      }

      // Compare mode: two assistant answers to the same prompt, word-diffed.
      if (compareMode && !isUser) {
        const std::vector<std::string> siblings = chat.siblings(messageId);
        std::vector<std::string> answers;
        for (const std::string& sibling : siblings) {
          auto it = chat.messages.find(sibling);
          if (it != chat.messages.end() && it->second.role == "assistant" && !it->second.streaming) {
            answers.push_back(sibling);
          }
        }
        if (answers.size() >= 2) {
          drawCompareView(answers, contentWidth);
          ImGui::Unindent(marginX);
          ImGui::PopID();
          continue;
        }
      }

      // Fan-out: several answers to the same prompt sit side by side until
      // one of them is kept.
      if (!isUser && !compareMode && fanoutUnresolved(message.parentId)) {
        std::vector<std::string> answers;
        for (const std::string& sibling : chat.siblings(messageId)) {
          auto it = chat.messages.find(sibling);
          if (it != chat.messages.end() && it->second.role == "assistant") answers.push_back(sibling);
        }
        if (answers.size() >= 2) {
          drawFanoutColumns(answers, contentWidth);
          ImGui::Unindent(marginX);
          ImGui::PopID();
          continue;
        }
      }

      // --- header line: who + when + branch switcher
      {
        ImVec2 pos = ImGui::GetCursorScreenPos();
        icons::draw(ImGui::GetWindowDrawList(), isUser ? icons::Icon::User : icons::Icon::Sparkle,
                    ImVec2(pos.x + 9, pos.y + 9), 15.0f,
                    isUser ? palette.textDim : palette.accent);
        ImGui::Dummy(ImVec2(22, 18));
        ImGui::SameLine(0, 4);
        theme::pushBold();
        ImGui::TextUnformatted(isUser ? user_.value("name", "You").c_str()
                                      : (message.modelName.empty() ? "Assistant"
                                                                   : message.modelName.c_str()));
        theme::pop();
        if (settings().showTimestamps && message.timestamp > 0) {
          ImGui::SameLine();
          widgets::dimText("%s", core::relativeTime(message.timestamp).c_str());
        }
        auto timing = timings.find(messageId);
        if (!isUser && timing != timings.end() && timing->second.firstTokenMs > 0.0) {
          ImGui::SameLine();
          const double ttft = (timing->second.firstTokenMs - timing->second.sentMs) / 1000.0;
          if (timing->second.doneMs > 0.0) {
            widgets::dimText("first token %.1fs, total %.1fs", ttft,
                             (timing->second.doneMs - timing->second.sentMs) / 1000.0);
          } else {
            widgets::dimText("first token %.1fs", ttft);
          }
        }
        if (isPinned(messageId)) {
          ImGui::SameLine();
          icons::inlineIcon(icons::Icon::Star, palette.warning, 0.8f);
        }

        const std::vector<std::string> siblings = chat.siblings(messageId);
        if (siblings.size() > 1) {
          auto position = std::find(siblings.begin(), siblings.end(), messageId);
          const int index = static_cast<int>(std::distance(siblings.begin(), position));
          ImGui::SameLine();
          if (ImGui::SmallButton("<") && index > 0) {
            chat.currentId = siblings[index - 1];
          }
          ImGui::SameLine();
          ImGui::Text("%d/%d", index + 1, static_cast<int>(siblings.size()));
          ImGui::SameLine();
          if (ImGui::SmallButton(">") && index + 1 < static_cast<int>(siblings.size())) {
            chat.currentId = siblings[index + 1];
          }
        }
      }

      // --- attachments
      if (!message.files.empty()) {
        for (const auto& file : message.files) {
          const std::string type = file.value("type", "file");
          if (type == "image") {
            const std::string url = file.value("url", "");
            images::Entry entry = images::get(url, [this, url] { return fetchImageBytes(url); });
            if (entry.ready()) {
              const float imageWidth = std::min(contentWidth * 0.6f, 360.0f);
              const float scale = imageWidth / static_cast<float>(entry.width);
              ImGui::Image(entry.texture,
                           ImVec2(imageWidth, static_cast<float>(entry.height) * scale));
            } else if (entry.failed) {
              widgets::dimText("[image]");
            } else {
              widgets::loadingRow("loading image");
            }
          } else {
            icons::inlineIcon(icons::Icon::Paperclip);
            ImGui::SameLine(0, 4);
            widgets::dimText("%s", file.value("name", "attachment").c_str());
          }
        }
      }

      // --- reasoning
      if (!message.reasoning.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Header, theme::toVec4(palette.panel));
        if (ImGui::CollapsingHeader(message.streaming ? "Thinking..." : "Reasoning")) {
          ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.textFaint));
          ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentWidth);
          ImGui::TextUnformatted(message.reasoning.c_str());
          ImGui::PopTextWrapPos();
          ImGui::PopStyleColor();
        }
        ImGui::PopStyleColor();
      }

      // --- agent run: goal, steps and their outcomes
      if (message.agentRun.is_object()) drawAgentCard(message, contentWidth);

      // --- tool calls made on the way to this answer
      if (!message.toolCalls.empty()) drawToolCallCards(message, contentWidth);

      // --- body
      if (message.showRaw) {
        theme::pushMono();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentWidth);
        ImGui::TextUnformatted(message.content.c_str());
        ImGui::PopTextWrapPos();
        theme::pop();
      } else if (message.editing) {
        ImGui::SetNextItemWidth(contentWidth);
        const float editorHeight = isUser ? 120.0f : std::clamp(
            ImGui::GetTextLineHeight() * (static_cast<float>(std::count(message.editBuffer.begin(),
                                                                        message.editBuffer.end(), '\n')) + 3.0f),
            120.0f, 420.0f);
        ImGui::InputTextMultiline("##edit", &message.editBuffer, ImVec2(contentWidth, editorHeight));
        if (isUser) {
          if (widgets::primaryButton("Send", ImVec2(110, 0))) {
            const std::string text = message.editBuffer;
            const std::string id = messageId;
            message.editing = false;
            editAndResend(id, text);
            ImGui::Unindent(marginX);
            ImGui::PopID();
            continue;
          }
        } else {
          // An assistant reply is edited in place; "Save & continue" resumes
          // generation from the edited tail.
          bool saved = false, resume = false;
          if (widgets::primaryButton("Save", ImVec2(90, 0))) saved = true;
          ImGui::SameLine();
          if (ImGui::Button("Save & continue")) saved = resume = true;
          if (ImGui::IsItemHovered()) ImGui::SetTooltip("Keep this text and let the model carry on from its end");
          if (saved) {
            const std::string text = message.editBuffer;
            const std::string id = messageId;
            saveEditedReply(id, text, resume);
            ImGui::Unindent(marginX);
            ImGui::PopID();
            continue;
          }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) message.editing = false;
      } else if (!isUser && !message.streaming && structuredEnabled() &&
                 drawStructuredValue(message, contentWidth)) {
        // Drawn as a table, tree or JSON by the structured-output card.
      } else if (settings().renderMarkdown && !isUser) {
        md::Options options;
        options.wrapWidth = contentWidth;
        options.imageLoader = [this](const std::string& url) { return fetchImageBytes(url); };
        options.onRunCode = [this](const std::string& code, const std::string& language) {
          async::run([this, code, language] {
            api::Result res = api_.executeCode(code, language);
            async::post([res] {
              if (res.ok) {
                widgets::toast(widgets::ToastLevel::Success,
                               "Output: " + res.data.value("stdout", res.raw).substr(0, 300));
              } else {
                widgets::toastError("Execution failed: " + api::errorMessage(res));
              }
            });
          });
        };
        options.onCodeBlockActions = [this](const std::string& code, const std::string& language) {
          drawCodeBlockActions(code, language);
        };
        options.onApplyDiff = [this](const std::string& diff) { applyCodeBlockToFile(diff); };
        md::render("body", message.content, options);
      } else {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentWidth);
        ImGui::TextUnformatted(message.content.c_str());
        ImGui::PopTextWrapPos();
      }

      if (message.streaming && message.content.empty()) {
        widgets::loadingRow(message.status.empty() ? "Thinking..." : message.status.c_str());
      } else if (message.streaming && !message.status.empty()) {
        widgets::dimText("%s", message.status.c_str());
      }

      if (!message.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.danger));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentWidth);
        ImGui::TextUnformatted(message.error.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
      }

      // --- citations
      if (!message.sources.empty()) {
        ImGui::Dummy(ImVec2(0, 4));
        if (ImGui::CollapsingHeader("Sources")) {
          int index = 1;
          for (const auto& source : message.sources) {
            std::string label = std::to_string(index++) + ". ";
            if (source.contains("source") && source["source"].is_object()) {
              label += source["source"].value("name", source["source"].value("url", ""));
            } else if (source.is_string()) {
              label += source.get<std::string>();
            }
            if (widgets::linkText(label.c_str())) openCitation(source, 0);
          }
        }
      }

      // --- follow-up suggestions
      if (!message.streaming && message.followUps.is_array() && !message.followUps.empty()) {
        ImGui::Dummy(ImVec2(0, 4));
        for (const auto& suggestion : message.followUps) {
          if (!suggestion.is_string()) continue;
          const std::string text = suggestion.get<std::string>();
          ImGui::PushID(text.c_str());
          ImVec2 pos = ImGui::GetCursorScreenPos();
          const ImVec2 size(std::min(contentWidth, ImGui::CalcTextSize(text.c_str()).x + 24.0f),
                            ImGui::GetTextLineHeight() + 12.0f);
          if (ImGui::InvisibleButton("##followup", size)) {
            composer = text;
            focusComposer = true;
          }
          const bool hovered = ImGui::IsItemHovered();
          ImDrawList* dl = ImGui::GetWindowDrawList();
          dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                            hovered ? palette.panelAlt : palette.panel, size.y * 0.5f);
          dl->AddText(ImVec2(pos.x + 12.0f, pos.y + 6.0f), palette.textDim, text.c_str());
          ImGui::PopID();
        }
      }

      // --- steering chips under the latest answer: one click, no typing
      if (!isUser && !message.streaming && messageId == lastAssistantId && streams_.empty() &&
          message.error.empty()) {
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 3));
        struct Chip { const char* label; const char* prompt; };
        static const Chip kChips[] = {
            {"Shorter", "Rewrite your last answer much more briefly, keeping only what matters."},
            {"More detail", "Expand your last answer with more detail and concrete examples."},
            {"As a table", "Present your last answer as a markdown table."},
            {"As code", "Give your last answer as code only, in a single fenced block, with no prose."},
            {"Simpler", "Explain your last answer again in plain language, as if to a beginner."},
            {"Check it", "Review your last answer for mistakes or missing cases and correct it."},
        };
        for (const Chip& chip : kChips) {
          if (widgets::ghostButton(chip.label)) steer(chip.prompt);
          ImGui::SameLine();
        }
        ImGui::NewLine();
        ImGui::PopStyleVar();
      }

      // --- action row
      if (!message.streaming) {
        ImGui::Dummy(ImVec2(0, 2));
        if (icons::button("copy", icons::Icon::Copy, "Copy")) {
          platform::setClipboardText(message.content);
          widgets::toast(widgets::ToastLevel::Success, "Copied");
        }
        ImGui::SameLine();
        if (icons::button("quote", icons::Icon::Chat, "Quote in a reply")) quoteReply(message);
        ImGui::SameLine();
        if (icons::button("pin", icons::Icon::Star, isPinned(messageId) ? "Unpin" : "Pin", 0.0f,
                          isPinned(messageId) ? palette.warning : 0)) {
          togglePin(messageId);
        }
        ImGui::SameLine();
        if (isUser) {
          if (icons::button("edit", icons::Icon::Pencil, "Edit & resend")) {
            message.editing = true;
            message.editBuffer = message.content;
          }
        } else {
          if (icons::button("regen", icons::Icon::Refresh, "Regenerate")) {
            regenerate(messageId);
          }
          ImGui::SameLine();
          if (icons::button("retrywith", icons::Icon::Chevron, "Retry with another model")) {
            ImGui::OpenPopup("##retrywith");
          }
          if (ImGui::BeginPopup("##retrywith")) {
            for (const ModelInfo& info : models) {
              if (ImGui::MenuItem(info.name.c_str())) retryWithModel(messageId, info.id);
            }
            ImGui::EndPopup();
          }
          ImGui::SameLine();
          if (messageId == lastAssistantId) {
            if (icons::button("continue", icons::Icon::Play, "Continue this reply from where it stops")) {
              continueReply(messageId);
            }
            ImGui::SameLine();
            if (icons::button("editreply", icons::Icon::Pencil, "Edit this reply (then save, or save and continue)")) {
              message.editing = true;
              message.editBuffer = message.content;
            }
            ImGui::SameLine();
          }
          if (icons::button("speak", icons::Icon::Speaker, "Read aloud")) speak(message.content);
          ImGui::SameLine();
          if (icons::button("up", icons::Icon::ThumbUp, "Good response", 0.0f,
                            message.rating > 0 ? palette.success : 0)) {
            rateMessage(messageId, 1);
          }
          ImGui::SameLine();
          if (icons::button("down", icons::Icon::ThumbDown, "Bad response", 0.0f,
                            message.rating < 0 ? palette.danger : 0)) {
            rateMessage(messageId, -1);
          }
          ImGui::SameLine();
          if (icons::button("raw", icons::Icon::Code, "Toggle raw markdown")) {
            message.showRaw = !message.showRaw;
          }
          ImGui::SameLine();
          if (icons::button("fork", icons::Icon::Branch, "Branch into a new chat") &&
              !chat.id.empty()) {
            const std::string chatId = chat.id;
            const std::string id = messageId;
            async::run([this, chatId, id] {
              api::Result res = api_.forkChat(chatId, id);
              async::post([this, res] {
                if (res.ok && res.data.contains("id")) {
                  refreshChats(false);
                  openChat(res.data["id"]);
                }
              });
            });
          }
          const std::string usage = formatUsage(message.usage);
          if (!usage.empty()) {
            ImGui::SameLine();
            widgets::dimText("%s", usage.c_str());
          }
        }
      }

      ImGui::Unindent(marginX);
      ImGui::PopID();
      ImGui::Dummy(ImVec2(0, 6));
    }

    ImGui::Dummy(ImVec2(0, 20));
    if (scrollToBottom) {
      ImGui::SetScrollHereY(1.0f);
      if (streams_.empty()) scrollToBottom = false;
    }
    handleKeyboardNavigation(path);
  }
  ImGui::EndChild();
  if (showArtifacts) {
    ImGui::SameLine();
    drawArtifactColumn(sideWidth);
  } else if (showDocument) {
    ImGui::SameLine();
    drawDocumentColumn(sideWidth);
  } else if (!referenceChatId.empty()) {
    ImGui::SameLine();
    drawReferenceColumn(sideWidth);
  } else if (showTree) {
    ImGui::SameLine();
    drawTreeColumn(sideWidth);
  } else if (showPins) {
    ImGui::SameLine();
    drawPinnedRail(sideWidth);
  }
  ImGui::EndChild();

  // ------------------------------------------------------------- composer
  ImGui::BeginChild("##composer", ImVec2(0, 0), ImGuiChildFlags_None);
  {
    const float available = ImGui::GetContentRegionAvail().x;
    const float contentWidth = std::min(kMaxContentWidth, available - 40.0f);
    const float marginX = std::max(16.0f, (available - contentWidth) * 0.5f);
    ImGui::Indent(marginX);

    if (!attachments.empty()) {
      for (size_t i = 0; i < attachments.size(); ++i) {
        Attachment& attachment = attachments[i];
        ImGui::PushID(static_cast<int>(i));
        if (attachment.isImage) {
          const std::string key = "attach:" + std::to_string(i) + attachment.name;
          images::Entry entry = images::get(key, [&attachment] { return attachment.bytes; });
          if (entry.ready()) {
            const float h = 56.0f;
            ImGui::Image(entry.texture,
                         ImVec2(h * entry.width / std::max(1, entry.height), h));
          } else {
            ImGui::Dummy(ImVec2(56, 56));
          }
        } else {
          widgets::pill(attachment.name + (attachment.uploading ? " (uploading)" : ""),
                        palette.panelAlt);
        }
        ImGui::SameLine();
        if (icons::button("remove", icons::Icon::Close, "Remove")) {
          attachments.erase(attachments.begin() + static_cast<long>(i));
          ImGui::PopID();
          break;
        }
        ImGui::SameLine();
        ImGui::PopID();
      }
      ImGui::NewLine();
    }

    // The native NSTextView composer floats over this box (IME, dictation,
    // spellcheck, emoji picker); the ImGui field stays as the fallback when
    // settings "nativeComposer" is off.
    const bool nativeComposer = settings().extra.value("nativeComposer", true);
    static std::string syncedComposer;
    static int syncedGeneration = -1;
    static bool composerInstalled = false;
    float textHeight = 46.0f;
    if (nativeComposer) {
      if (!composerInstalled) {
        composerInstalled = true;
        native::ComposerCallbacks callbacks;
        callbacks.send = [this] {
          std::string text = native::composerText();
          while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
          if (core::trim(text).empty() && attachments.empty()) return;
          composer = text;
          sendMessage(core::trim(text));
          ++composerGeneration;
          focusComposer = true;
        };
        callbacks.tab = [this] {
          const std::string text = native::composerText();
          if (text.empty() || text[0] != '/' || text.find(' ') != std::string::npos ||
              text.find('\n') != std::string::npos) {
            return;
          }
          const Json matches = slashPromptMatches(text);
          if (matches.empty()) return;
          composer = matches[0].value("content", "");
          ++composerGeneration;
          focusComposer = true;
        };
        callbacks.navigate = [this](int direction) { return navigateFromComposer(direction); };
        callbacks.pasteImage = [this] { return pasteImageFromClipboard(); };
        callbacks.history = [this](int direction) {
          composer = native::composerText();
          return recallHistory(direction);
        };
        callbacks.pasteText = [this](const std::string& text) { return smartPaste(text); };
        native::composerInstall(callbacks);
      }
      textHeight = std::clamp(native::composerContentHeight(), 46.0f, 240.0f);
    }
    composerTextHeight = textHeight;

    ImVec2 boxStart = ImGui::GetCursorScreenPos();
    const float boxHeight = textHeight + 46.0f;
    // Reserve the whole composer area up front: everything below positions
    // itself with SetCursorScreenPos, which does not grow the parent.
    ImGui::Dummy(ImVec2(contentWidth, boxHeight + 22.0f));
    ImGui::SetCursorScreenPos(boxStart);
    ImGui::GetWindowDrawList()->AddRectFilled(
        boxStart, ImVec2(boxStart.x + contentWidth, boxStart.y + boxHeight), palette.panel, 14.0f);
    ImGui::GetWindowDrawList()->AddRect(
        boxStart, ImVec2(boxStart.x + contentWidth, boxStart.y + boxHeight), palette.border, 14.0f, 1.0f);

    bool composerFocused = false;
    if (nativeComposer) {
      // Three-way sync: a generation bump or a change made by app code pushes
      // `composer` into the view; otherwise the user's typing is pulled back.
      const std::string viewText = native::composerText();
      if (syncedGeneration != composerGeneration || composer != syncedComposer) {
        native::composerSetText(composer);
        syncedGeneration = composerGeneration;
      } else if (viewText != syncedComposer) {
        composer = viewText;
      }
      syncedComposer = composer;
      trackDraft();
      native::composerSetStyle(palette.text, palette.textFaint, palette.accent,
                               settings().fontSize * settings().uiScale, settings().sendOnEnter);
      native::composerSetFrame(boxStart.x + 12, boxStart.y + 8, contentWidth - 24, textHeight);
      composerShownThisFrame = true;
      if (focusComposer && native::composerFocus()) focusComposer = false;
      composerFocused = native::composerIsFocused();
      // The backend's event monitor also feeds ImGui the keys typed into the
      // native field; with keyboard navigation on, those keystrokes switch
      // ImGui into "keyboard mode", which ignores the mouse until it moves -
      // so the first click after typing would be lost.
      ImGuiIO& composerIo = ImGui::GetIO();
      if (composerFocused) composerIo.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
      else composerIo.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    } else {
      ImGui::SetCursorScreenPos(ImVec2(boxStart.x + 12, boxStart.y + 8));
      ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
      ImGui::SetNextItemWidth(contentWidth - 24);
      if (focusComposer) {
        ImGui::SetKeyboardFocusHere();
        focusComposer = false;
      }
      // The id carries a generation counter: bumping it after a send gives the
      // widget a fresh state instead of keeping the text it had while active.
      char composerId[32];
      snprintf(composerId, sizeof(composerId), "##composer%d", composerGeneration);
      ImGui::InputTextMultiline(composerId, &composer, ImVec2(contentWidth - 24, 46),
                                ImGuiInputTextFlags_NoHorizontalScroll);
      composerFocused = ImGui::IsItemFocused();
      ImGui::PopStyleColor();
      if (composer.empty() && !composerFocused) {
        ImGui::GetWindowDrawList()->AddText(ImVec2(boxStart.x + 17, boxStart.y + 14),
                                            palette.textFaint, "Send a message...");
      }
    }

    // "/" at the start of the composer offers the server's saved prompts.
    if (!composer.empty() && composer[0] == '/' && composer.find(' ') == std::string::npos &&
        composer.find('\n') == std::string::npos) {
      const Json matches = slashPromptMatches(composer);
      if (!matches.empty()) {
        ImGui::SetNextWindowPos(ImVec2(boxStart.x + 12, boxStart.y - 6), ImGuiCond_Always, ImVec2(0, 1));
        ImGui::BeginTooltip();
        widgets::dimText("Saved prompts - Tab inserts the first");
        for (const auto& prompt : matches) {
          ImGui::Text("%s  %s", prompt.value("command", "").c_str(),
                      prompt.value("title", prompt.value("name", "")).c_str());
        }
        ImGui::EndTooltip();
        if (!nativeComposer && composerFocused && ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
          composer = matches[0].value("content", "");
          ++composerGeneration;
          focusComposer = true;
        }
      }
    }

    // Toolbar inside the box.
    ImGui::SetCursorScreenPos(ImVec2(boxStart.x + 10, boxStart.y + textHeight + 10));
    if (icons::button("attach", icons::Icon::Paperclip, "Attach files")) {
      attachFiles(platform::openFiles(true));
    }
    ImGui::SameLine();
    if (icons::button("folderctx", icons::Icon::Folder,
                      projectFolder.empty() ? "Set project folder" : "Project files")) {
      if (projectFolder.empty()) {
        auto picked = native::chooseFolder();
        if (!picked.empty()) setProjectFolder(picked.front());
      } else {
        showProjectFiles = true;
      }
    }
    if (!attachments.empty() && attachments.back().isImage) {
      ImGui::SameLine();
      if (icons::button("livetext", icons::Icon::Note, "Extract text from the image")) {
        const std::string bytes = attachments.back().bytes;
        async::run([this, bytes] {
          const std::string text = native::recognizeText(bytes);
          async::post([this, text] {
            if (text.empty()) {
              widgets::toastInfo("No text found in the image");
              return;
            }
            composer += (composer.empty() ? "" : "\n") + text;
            ++composerGeneration;
            focusComposer = true;
          });
        });
      }
    }
    ImGui::SameLine();
    if (icons::button("mic", icons::Icon::Mic,
                      platform::recording() ? "Stop recording" : "Record a voice message", 0.0f,
                      platform::recording() ? palette.danger : 0)) {
      if (platform::recording()) {
        const std::string wav = platform::stopRecording();
        if (!wav.empty()) {
          async::run([this, wav] {
            api::Result res = api_.transcribe("recording.wav", "audio/wav", wav);
            async::post([this, res] {
              if (res.ok && res.data.contains("text")) {
                composer += res.data["text"].get<std::string>();
              } else {
                widgets::toastError("Transcription failed: " + api::errorMessage(res));
              }
            });
          });
        }
      } else {
        platform::startRecording();
      }
    }
    ImGui::SameLine();
    if (icons::button("websearch", icons::Icon::Globe, "Web search", 0.0f,
                      webSearchEnabled ? palette.accent : 0)) {
      webSearchEnabled = !webSearchEnabled;
    }
    ImGui::SameLine();
    if (icons::button("imagegen", icons::Icon::Image, "Image generation", 0.0f,
                      imageGenerationEnabled ? palette.accent : 0)) {
      imageGenerationEnabled = !imageGenerationEnabled;
    }
    ImGui::SameLine();
    if (icons::button("codeint", icons::Icon::Code, "Code interpreter", 0.0f,
                      codeInterpreterEnabled ? palette.accent : 0)) {
      codeInterpreterEnabled = !codeInterpreterEnabled;
    }
    ImGui::SameLine();
    if (icons::button("voice", icons::Icon::Speaker,
                      voiceMode ? "Stop voice conversation" : "Voice conversation", 0.0f,
                      voiceMode ? palette.danger : 0)) {
      voiceMode = !voiceMode;
    }
    ImGui::SameLine();
    if (icons::button("capture", icons::Icon::Camera,
                      "Capture part of the screen into this chat (Space for a whole window)")) {
      captureScreenToChat(true);
    }
    ImGui::SameLine();
    if (icons::button("fanout", icons::Icon::Branch,
                      fanoutModels.size() >= 2
                          ? ("Fan out to " + std::to_string(fanoutModels.size()) + " models").c_str()
                          : "Fan out: send this prompt to several models at once")) {
      showFanoutPicker = true;
    }
    drawFanoutPicker();
    ImGui::SameLine();
    if (icons::button("agent", icons::Icon::Brain,
                      agentMode ? "Agent mode on - the prompt is a goal: plan, act with tools, check, answer "
                                  "(right-click for options)"
                                : "Agent mode: plan, act with tools, check each step, answer (right-click for options)",
                      0.0f, agentMode ? palette.accent : 0)) {
      agentMode = !agentMode;
      settings().extra["agentMode"] = agentMode;
      settings().save();
      if (agentMode && mcpConnectedCount() == 0) {
        widgets::toastInfo("Agent mode without tools: the model plans and works in text only");
      }
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup("##agentopts");
    drawAgentPopover();
    ImGui::SameLine();
    if (icons::button("mcp", icons::Icon::Plug,
                      mcpEnabled ? "MCP tools on - replies can call your MCP servers"
                                 : "MCP tools (add servers under Tools > MCP)",
                      0.0f, mcpEnabled ? palette.accent : 0)) {
      mcpEnabled = !mcpEnabled;
      settings().extra["mcpEnabled"] = mcpEnabled;
      settings().save();
      if (mcpEnabled && mcpConnectedCount() == 0) {
        widgets::toastInfo("No MCP server is connected - add one under Tools > MCP");
      }
    }
    ImGui::SameLine();
    if (icons::button("tools", icons::Icon::Wrench, "Tools", 0.0f,
                      activeToolIds.empty() ? 0 : palette.accent)) {
      gShowToolPicker = true;
      if (toolList.empty()) {
        async::run([this] {
          api::Result res = api_.tools();
          async::post([this, res] {
            if (res.ok && res.data.is_array()) toolList = res.data;
          });
        });
      }
    }

    // Send / stop.
    ImGui::SetCursorScreenPos(ImVec2(boxStart.x + contentWidth - 44, boxStart.y + textHeight + 8));
    if (!streams_.empty()) {
      if (icons::button("stop", icons::Icon::Stop, "Stop generating", 20.0f, palette.danger)) {
        stopAllStreams();
      }
    } else {
      const bool canSend = !core::trim(composer).empty() || !attachments.empty();
      if (icons::button("send", icons::Icon::Send, "Send", 20.0f,
                        canSend ? palette.accent : palette.textFaint) &&
          canSend) {
        sendMessage(core::trim(composer));
        ++composerGeneration;
        focusComposer = true;
      }
    }

    // Enter sends; Shift+Enter makes a newline.
    ImGuiIO& io = ImGui::GetIO();
    if (!nativeComposer && composerFocused && settings().sendOnEnter && !io.KeyShift &&
        ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
      std::string text = composer;
      while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
      if (!core::trim(text).empty() || !attachments.empty()) {
        composer = text;
        sendMessage(core::trim(text));
        ++composerGeneration;
        focusComposer = true;
      }
    }

    ImGui::SetCursorScreenPos(ImVec2(boxStart.x, boxStart.y + boxHeight + 4));
    ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.textFaint));
    static const char* kVoiceStates[] = {"", "Listening...", "Transcribing...", "Thinking...", "Speaking..."};
    if (voiceMode && voiceState > 0) {
      ImGui::Text("Voice: %s %s", kVoiceStates[voiceState],
                  voiceState == 1 ? std::string(static_cast<size_t>(platform::recordingLevel() * 20), '|').c_str() : "");
    } else if (platform::recording()) {
      ImGui::Text("Recording... %.0f%%", platform::recordingLevel() * 100.0f);
    } else if (!routedModelNote.empty()) {
      ImGui::TextUnformatted(routedModelNote.c_str());
    } else if (!statusText_.empty()) {
      ImGui::TextUnformatted(statusText_.c_str());
    }
    if (!projectFolder.empty()) {
      ImGui::Text("Project: %s  (%d file%s pinned)", core::baseName(projectFolder).c_str(),
                  static_cast<int>(projectFiles.size()), projectFiles.size() == 1 ? "" : "s");
    }
    ImGui::PopStyleColor();
    ImGui::Unindent(marginX);
  }
  ImGui::EndChild();

  // ------------------------------------------------------------ tool picker
  if (gShowToolPicker) {
    ImGui::OpenPopup("Tools");
    ImGui::SetNextWindowSize(ImVec2(420, 400), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Tools", &gShowToolPicker)) {
      if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        gShowToolPicker = false;
        ImGui::CloseCurrentPopup();
      }
      if (toolList.empty()) {
        widgets::dimText("No tools installed on this server.");
      }
      for (const auto& tool : toolList) {
        const std::string id = tool.value("id", "");
        bool enabled =
            std::find(activeToolIds.begin(), activeToolIds.end(), id) != activeToolIds.end();
        if (ImGui::Checkbox(tool.value("name", id).c_str(), &enabled)) {
          if (enabled) {
            activeToolIds.push_back(id);
          } else {
            activeToolIds.erase(std::remove(activeToolIds.begin(), activeToolIds.end(), id),
                                activeToolIds.end());
          }
        }
        const std::string description =
            tool.value("meta", Json::object()).value("description", "");
        if (!description.empty()) widgets::dimText("    %s", description.c_str());
      }
      if (widgets::primaryButton("Done", ImVec2(-FLT_MIN, 30))) gShowToolPicker = false;
      ImGui::EndPopup();
    }
  }
}

// Option+Up/Down pressed inside the native composer: step through the
// transcript the way handleKeyboardNavigation does from the ImGui field.
bool App::navigateFromComposer(int direction) {
  const std::vector<std::string> path = chat.branch();
  if (path.empty()) return false;
  auto position = std::find(path.begin(), path.end(), focusedMessageId);
  int index = position == path.end() ? static_cast<int>(path.size())
                                     : static_cast<int>(std::distance(path.begin(), position));
  index = std::clamp(index + direction, 0, static_cast<int>(path.size()) - 1);
  focusedMessageId = path[static_cast<size_t>(index)];
  pendingScrollTo = focusedMessageId;
  return true;
}

}  // namespace app
