// The conversation view: header, message list, composer.
#include <algorithm>

#include "app.h"
#include "core/async.h"
#include "core/common.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"
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
    const float actionsWidth = 190.0f;
    ImGui::SameLine(ImGui::GetWindowWidth() - actionsWidth);
    if (!streams_.empty()) {
      widgets::spinner("##busy", 8.0f, 2.0f);
      ImGui::SameLine();
    }
    if (icons::button("newchat", icons::Icon::Plus, "New chat")) newChat();
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
    if (icons::button("chatmenu", icons::Icon::Menu, "More")) ImGui::OpenPopup("##chatmore");
    if (ImGui::BeginPopup("##chatmore")) {
      if (ImGui::MenuItem("Download chat as JSON") && !chat.id.empty()) {
        const std::string path = platform::saveFile(chat.title + ".json");
        if (!path.empty()) {
          core::writeFile(path, chat.toJson().dump(2));
          widgets::toast(widgets::ToastLevel::Success, "Saved to " + path);
        }
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
  }
  ImGui::EndChild();

  // ------------------------------------------------------------- messages
  const float composerHeight = 128.0f + (attachments.empty() ? 0.0f : 74.0f);
  ImGui::BeginChild("##messages", ImVec2(0, -composerHeight), ImGuiChildFlags_None);
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

    for (const std::string& messageId : path) {
      Message& message = chat.messages[messageId];
      ImGui::PushID(messageId.c_str());
      ImGui::Indent(marginX);

      const bool isUser = message.role == "user";
      ImGui::Dummy(ImVec2(0, 10));

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

      // --- body
      if (message.showRaw) {
        theme::pushMono();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentWidth);
        ImGui::TextUnformatted(message.content.c_str());
        ImGui::PopTextWrapPos();
        theme::pop();
      } else if (message.editing) {
        ImGui::SetNextItemWidth(contentWidth);
        ImGui::InputTextMultiline("##edit", &message.editBuffer, ImVec2(contentWidth, 120));
        if (widgets::primaryButton("Send", ImVec2(110, 0))) {
          const std::string text = message.editBuffer;
          const std::string id = messageId;
          message.editing = false;
          editAndResend(id, text);
          ImGui::Unindent(marginX);
          ImGui::PopID();
          continue;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) message.editing = false;
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
            if (widgets::linkText(label.c_str())) {
              std::string url;
              if (source.contains("source") && source["source"].is_object()) {
                url = source["source"].value("url", "");
              }
              if (!url.empty()) platform::openUrl(url);
            }
          }
        }
      }

      // --- action row
      if (!message.streaming) {
        ImGui::Dummy(ImVec2(0, 2));
        if (icons::button("copy", icons::Icon::Copy, "Copy")) {
          platform::setClipboardText(message.content);
          widgets::toast(widgets::ToastLevel::Success, "Copied");
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

    ImVec2 boxStart = ImGui::GetCursorScreenPos();
    const float boxHeight = 92.0f;
    // Reserve the whole composer area up front: everything below positions
    // itself with SetCursorScreenPos, which does not grow the parent.
    ImGui::Dummy(ImVec2(contentWidth, boxHeight + 22.0f));
    ImGui::SetCursorScreenPos(boxStart);
    ImGui::GetWindowDrawList()->AddRectFilled(
        boxStart, ImVec2(boxStart.x + contentWidth, boxStart.y + boxHeight), palette.panel, 14.0f);
    ImGui::GetWindowDrawList()->AddRect(
        boxStart, ImVec2(boxStart.x + contentWidth, boxStart.y + boxHeight), palette.border, 14.0f, 1.0f);

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
    const bool composerFocused = ImGui::IsItemFocused();
    ImGui::PopStyleColor();
    if (composer.empty() && !composerFocused) {
      ImGui::GetWindowDrawList()->AddText(ImVec2(boxStart.x + 17, boxStart.y + 14),
                                          palette.textFaint, "Send a message...");
    }

    // Toolbar inside the box.
    ImGui::SetCursorScreenPos(ImVec2(boxStart.x + 10, boxStart.y + 56));
    if (icons::button("attach", icons::Icon::Paperclip, "Attach files")) {
      attachFiles(platform::openFiles(true));
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
    ImGui::SetCursorScreenPos(ImVec2(boxStart.x + contentWidth - 44, boxStart.y + 54));
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
    if (composerFocused && settings().sendOnEnter && !io.KeyShift &&
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
    // Cmd+V with an image on the pasteboard attaches it.
    if (composerFocused && io.KeySuper && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
      const std::string png = platform::clipboardImage();
      if (!png.empty()) attachBytes("pasted.png", "image/png", png);
    }

    ImGui::SetCursorScreenPos(ImVec2(boxStart.x, boxStart.y + boxHeight + 4));
    ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.textFaint));
    if (platform::recording()) {
      ImGui::Text("Recording... %.0f%%", platform::recordingLevel() * 100.0f);
    } else if (!statusText_.empty()) {
      ImGui::TextUnformatted(statusText_.c_str());
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

}  // namespace app
