// Channels: the server's shared chat rooms, with live updates over socket.io.
#include "core/async.h"
#include "core/common.h"
#include "pane_common.h"
#include "ui/icons.h"
#include "ui/markdown.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

bool gLoaded = false;
bool gDisabled = false;
std::string gDisabledReason;
std::string gSelected;
Json gMessages = Json::array();
bool gMessagesLoading = false;
std::string gDraft;
int gSeenEventCounter = -1;

}  // namespace

void App::drawChannelsPane() {
  auto loadMessages = [this](const std::string& channelId) {
    gMessagesLoading = true;
    async::run([this, channelId] {
      api::Result res = api_.channelMessages(channelId, 100);
      async::post([res] {
        gMessagesLoading = false;
        if (res.ok && res.data.is_array()) gMessages = res.data;
      });
    });
  };

  if (!gLoaded) {
    gLoaded = true;
    async::run([this] {
      api::Result res = api_.channels();
      async::post([this, res] {
        if (res.ok && res.data.is_array()) {
          channelList = res.data;
          gDisabled = false;
        } else {
          gDisabled = true;
          gDisabledReason = api::errorMessage(res);
        }
      });
    });
  }

  // Live refresh when the socket reports channel activity.
  if (gSeenEventCounter != channelEventCounter && !gSelected.empty()) {
    gSeenEventCounter = channelEventCounter;
    loadMessages(gSelected);
  }

  paneHeader("Channels", "Shared rooms on this server", [&] {
    ImGui::SameLine(ImGui::GetWindowWidth() - 120.0f);
    if (ImGui::Button("Refresh", ImVec2(90, 30))) {
      gLoaded = false;
      if (!gSelected.empty()) loadMessages(gSelected);
    }
  });

  if (gDisabled) {
    paneBody("##channelsoff");
    card("off", [&] {
      icons::inlineIcon(icons::Icon::Info, theme::palette().accent);
      ImGui::SameLine(0, 6);
      ImGui::TextUnformatted("Channels are not enabled on this server.");
      if (!gDisabledReason.empty()) widgets::dimText("%s", gDisabledReason.c_str());
    }, ImGui::GetContentRegionAvail().x - 18.0f);
    paneEnd();
    return;
  }

  ImGui::BeginChild("##channellist", ImVec2(240, 0));
  ImGui::Dummy(ImVec2(0, 8));
  ImGui::Indent(12.0f);
  for (const auto& channel : channelList) {
    const std::string id = channel.value("id", "");
    const std::string name = channel.value("name", "channel");
    ImGui::PushID(id.c_str());
    if (ImGui::Selectable(("# " + name).c_str(), id == gSelected, 0, ImVec2(208, 26))) {
      gSelected = id;
      loadMessages(id);
    }
    ImGui::PopID();
  }
  if (channelList.empty()) emptyState("No channels");
  ImGui::Unindent(12.0f);
  ImGui::EndChild();

  ImGui::SameLine();

  ImGui::BeginChild("##channelbody", ImVec2(0, 0));
  if (gSelected.empty()) {
    emptyState("Pick a channel");
  } else {
    ImGui::BeginChild("##messages", ImVec2(0, -70.0f));
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::Indent(16.0f);
    if (gMessagesLoading) widgets::loadingRow("Loading messages...");
    // The API returns newest first; walk backwards so the latest sits at the bottom.
    for (auto it = gMessages.rbegin(); it != gMessages.rend(); ++it) {
      const Json& message = *it;
      const Json& user = message.value("user", Json::object());
      theme::pushBold();
      ImGui::TextUnformatted(user.value("name", "someone").c_str());
      theme::pop();
      ImGui::SameLine();
      widgets::dimText("%s", core::relativeTime(message.value("created_at",
                                                              static_cast<int64_t>(0)) /
                                                1000000)
                                 .c_str());
      md::Options options;
      options.wrapWidth = ImGui::GetContentRegionAvail().x - 30.0f;
      options.compact = true;
      md::render(message.value("id", "m").c_str(), message.value("content", ""), options);
      ImGui::Dummy(ImVec2(0, 6));
    }
    ImGui::Unindent(16.0f);
    ImGui::EndChild();

    ImGui::Indent(16.0f);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 130.0f);
    const bool submitted = ImGui::InputTextWithHint("##channeldraft", "Message the channel",
                                                    &gDraft, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((widgets::primaryButton("Send", ImVec2(100, 0)) || submitted) &&
        !core::trim(gDraft).empty()) {
      const std::string channelId = gSelected;
      const std::string content = core::trim(gDraft);
      gDraft.clear();
      async::run([this, channelId, content, loadMessages] {
        api::Result res = api_.postChannelMessage(channelId, content);
        async::post([res, channelId, loadMessages] {
          if (!res.ok) {
            widgets::toastError(api::errorMessage(res));
            return;
          }
          loadMessages(channelId);
        });
      });
    }
    ImGui::Unindent(16.0f);
  }
  ImGui::EndChild();
}

}  // namespace app
