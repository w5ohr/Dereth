// Admin: users, server configuration, model management, analytics, feedback.
#include <algorithm>

#include "core/async.h"
#include "core/common.h"
#include "pane_common.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

enum class Tab { Users, Models, Server, Analytics, Feedback, Connections };

Tab gTab = Tab::Users;
bool gLoaded[6] = {false, false, false, false, false, false};
std::string gFilter;
Json gAdminConfig = Json::object();
Json gConnections = Json::object();
Json gOllamaTags = Json::object();
Json gOllamaPs = Json::object();
std::string gPullModel;
std::string gPullStatus;
bool gPulling = false;
std::atomic<bool> gPullCancel{false};

const char* tabName(Tab tab) {
  switch (tab) {
    case Tab::Users: return "Users";
    case Tab::Models: return "Models";
    case Tab::Server: return "Server";
    case Tab::Analytics: return "Analytics";
    case Tab::Feedback: return "Feedback";
    case Tab::Connections: return "Connections";
  }
  return "";
}

}  // namespace

void App::drawAdminPane() {
  auto invalidate = [](Tab tab) { gLoaded[static_cast<int>(tab)] = false; };

  auto ensureLoaded = [&](Tab tab) {
    const int index = static_cast<int>(tab);
    if (gLoaded[index]) return;
    gLoaded[index] = true;
    switch (tab) {
      case Tab::Users:
        async::run([this] {
          api::Result res = api_.users(1);
          async::post([this, res] {
            if (!res.ok) return;
            userList = res.data.is_array() ? res.data : res.data.value("users", Json::array());
          });
        });
        break;
      case Tab::Models:
        async::run([this] {
          api::Result tags = api_.ollamaTags();
          api::Result ps = api_.ollamaPs();
          async::post([tags, ps] {
            if (tags.ok) gOllamaTags = tags.data;
            if (ps.ok) gOllamaPs = ps.data;
          });
        });
        break;
      case Tab::Server:
        async::run([this] {
          api::Result config = api_.adminConfig();
          async::post([config] {
            if (config.ok) gAdminConfig = config.data;
          });
        });
        break;
      case Tab::Analytics:
        async::run([this] {
          api::Result summary = api_.analyticsSummary();
          api::Result daily = api_.analyticsDaily();
          async::post([this, summary, daily] {
            if (summary.ok) analytics = summary.data;
            if (daily.ok) analytics["daily"] = daily.data;
          });
        });
        break;
      case Tab::Feedback:
        async::run([this] {
          api::Result board = api_.leaderboard();
          async::post([this, board] {
            if (board.ok) leaderboard = board.data;
          });
        });
        break;
      case Tab::Connections:
        async::run([this] {
          api::Result res = api_.connections();
          async::post([res] {
            if (res.ok) gConnections = res.data;
          });
        });
        break;
    }
  };

  paneHeader("Admin", "Server-wide settings and usage", [&] {
    ImGui::SameLine(ImGui::GetWindowWidth() - 120.0f);
    if (ImGui::Button("Refresh", ImVec2(90, 30))) {
      for (bool& loaded : gLoaded) loaded = false;
    }
  });

  paneBody("##admin");
  const float width = ImGui::GetContentRegionAvail().x - 18.0f;

  if (ImGui::BeginTabBar("##admintabs")) {
    for (Tab tab : {Tab::Users, Tab::Models, Tab::Server, Tab::Analytics, Tab::Feedback,
                    Tab::Connections}) {
      if (ImGui::BeginTabItem(tabName(tab))) {
        gTab = tab;
        ImGui::EndTabItem();
      }
    }
    ImGui::EndTabBar();
  }
  ensureLoaded(gTab);
  ImGui::Dummy(ImVec2(0, 8));

  switch (gTab) {
    case Tab::Users: {
      widgets::searchField("userfilter", &gFilter, "Filter users", 300.0f);
      ImGui::Dummy(ImVec2(0, 8));
      if (ImGui::BeginTable("##users", 5,
                            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                ImGuiTableFlags_SizingStretchProp,
                            ImVec2(width, 0))) {
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Email");
        ImGui::TableSetupColumn("Role");
        ImGui::TableSetupColumn("Last active");
        ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        for (const auto& user : userList) {
          const std::string name = user.value("name", "");
          const std::string email = user.value("email", "");
          if (!gFilter.empty() && !core::containsNoCase(name, gFilter) &&
              !core::containsNoCase(email, gFilter)) {
            continue;
          }
          const std::string id = user.value("id", "");
          ImGui::PushID(id.c_str());
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          ImGui::TextUnformatted(name.c_str());
          ImGui::TableSetColumnIndex(1);
          widgets::dimText("%s", email.c_str());
          ImGui::TableSetColumnIndex(2);
          const std::string role = user.value("role", "user");
          widgets::pill(role, role == "admin" ? theme::palette().accent
                                              : theme::palette().panelAlt);
          ImGui::TableSetColumnIndex(3);
          widgets::dimText("%s", core::relativeTime(user.value("last_active_at",
                                                               static_cast<int64_t>(0)))
                                     .c_str());
          ImGui::TableSetColumnIndex(4);
          if (ImGui::SmallButton(role == "admin" ? "Make user" : "Make admin")) {
            Json body{{"role", role == "admin" ? "user" : "admin"},
                      {"name", name},
                      {"email", email}};
            async::run([this, id, body, invalidate] {
              api_.updateUser(id, body);
              async::post([invalidate] { invalidate(Tab::Users); });
            });
          }
          if (user.value("id", "") != user_.value("id", "")) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete")) {
              async::run([this, id, invalidate] {
                api_.deleteUser(id);
                async::post([invalidate] { invalidate(Tab::Users); });
              });
            }
          }
          ImGui::PopID();
        }
        ImGui::EndTable();
      }
      if (userList.empty()) emptyState("No users returned");
      break;
    }

    case Tab::Models: {
      card("pull", [&] {
        ImGui::TextUnformatted("Pull an Ollama model");
        ImGui::SetNextItemWidth(300);
        ImGui::InputTextWithHint("##pullname", "e.g. qwen3:8b", &gPullModel);
        ImGui::SameLine();
        widgets::beginDisabled(gPulling || core::trim(gPullModel).empty());
        if (widgets::primaryButton("Pull", ImVec2(100, 0))) {
          gPulling = true;
          gPullCancel.store(false);
          gPullStatus = "starting...";
          const std::string model = core::trim(gPullModel);
          async::runDetached([this, model] {
            api_.ollamaPull(
                model,
                [](const Json& line) {
                  std::string status = line.value("status", "");
                  if (line.contains("completed") && line.contains("total")) {
                    const double done = line.value("completed", 0.0);
                    const double total = line.value("total", 1.0);
                    char buffer[128];
                    snprintf(buffer, sizeof(buffer), "%s %.0f%%", status.c_str(),
                             total > 0 ? (done / total) * 100.0 : 0.0);
                    status = buffer;
                  }
                  async::post([status] { gPullStatus = status; });
                  return !gPullCancel.load();
                },
                &gPullCancel);
            async::post([] {
              gPulling = false;
              gPullStatus = "done";
              gLoaded[static_cast<int>(Tab::Models)] = false;
            });
          });
        }
        widgets::endDisabled(gPulling || core::trim(gPullModel).empty());
        if (gPulling) {
          ImGui::SameLine();
          if (ImGui::Button("Cancel", ImVec2(90, 0))) gPullCancel.store(true);
        }
        if (!gPullStatus.empty()) widgets::dimText("%s", gPullStatus.c_str());
      }, width);

      widgets::sectionHeader("LOADED IN MEMORY");
      const Json& loaded = gOllamaPs.value("models", Json::array());
      if (loaded.empty()) widgets::dimText("Nothing resident right now");
      for (const auto& model : loaded) {
        const std::string name = model.value("name", "");
        ImGui::PushID(name.c_str());
        ImGui::BulletText("%s  %s", name.c_str(),
                          core::humanBytes(model.value("size", static_cast<int64_t>(0))).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Unload")) {
          async::run([this, name, invalidate] {
            api_.ollamaUnload(name);
            async::post([invalidate] { invalidate(Tab::Models); });
          });
        }
        ImGui::PopID();
      }

      widgets::sectionHeader("INSTALLED");
      const Json& tags = gOllamaTags.value("models", Json::array());
      if (ImGui::BeginTable("##ollamamodels", 4,
                            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                ImGuiTableFlags_SizingStretchProp,
                            ImVec2(width, 0))) {
        ImGui::TableSetupColumn("Model");
        ImGui::TableSetupColumn("Parameters");
        ImGui::TableSetupColumn("Size");
        ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        for (const auto& model : tags) {
          const std::string name = model.value("name", "");
          ImGui::PushID(name.c_str());
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          ImGui::TextUnformatted(name.c_str());
          ImGui::TableSetColumnIndex(1);
          widgets::dimText("%s", model.value("details", Json::object())
                                     .value("parameter_size", "")
                                     .c_str());
          ImGui::TableSetColumnIndex(2);
          widgets::dimText("%s",
                           core::humanBytes(model.value("size", static_cast<int64_t>(0))).c_str());
          ImGui::TableSetColumnIndex(3);
          if (ImGui::SmallButton("Delete")) {
            async::run([this, name, invalidate] {
              api_.ollamaDelete(name);
              async::post([invalidate] { invalidate(Tab::Models); });
            });
          }
          ImGui::PopID();
        }
        ImGui::EndTable();
      }
      break;
    }

    case Tab::Server: {
      card("version", [&] {
        field("Server", serverConfig_.value("name", ""));
        field("Version", serverConfig_.value("version", ""));
        field("Signup enabled",
              serverConfig_.value("features", Json::object()).value("enable_signup", false)
                  ? "yes"
                  : "no");
        field("WebSocket",
              serverConfig_.value("features", Json::object()).value("enable_websocket", false)
                  ? "yes"
                  : "no");
        field("Live socket", socketConnected() ? "connected" : "offline");
      }, width);
      if (!gAdminConfig.empty()) {
        widgets::sectionHeader("ADMIN CONFIG");
        card("adminconfig", [&] {
          ImGui::BeginChild("##adminjson", ImVec2(width - 40.0f, 320.0f));
          theme::pushMono();
          ImGui::PushTextWrapPos(width - 60.0f);
          ImGui::TextUnformatted(gAdminConfig.dump(2).c_str());
          ImGui::PopTextWrapPos();
          theme::pop();
          ImGui::EndChild();
        }, width);
      }
      break;
    }

    case Tab::Analytics: {
      if (analytics.empty()) {
        emptyState("No analytics available");
      } else {
        card("summary", [&] {
          for (auto it = analytics.begin(); it != analytics.end(); ++it) {
            if (it.value().is_number() || it.value().is_string()) {
              field(it.key().c_str(), it.value().is_string()
                                          ? it.value().get<std::string>()
                                          : it.value().dump());
            }
          }
        }, width);
        card("raw", [&] {
          ImGui::BeginChild("##analyticsjson", ImVec2(width - 40.0f, 300.0f));
          theme::pushMono();
          ImGui::PushTextWrapPos(width - 60.0f);
          ImGui::TextUnformatted(analytics.dump(2).c_str());
          ImGui::PopTextWrapPos();
          theme::pop();
          ImGui::EndChild();
        }, width);
      }
      break;
    }

    case Tab::Feedback: {
      if (leaderboard.empty()) {
        emptyState("No feedback collected yet");
      } else if (ImGui::BeginTable("##leaderboard", 3,
                                   ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH,
                                   ImVec2(width, 0))) {
        ImGui::TableSetupColumn("Model");
        ImGui::TableSetupColumn("Rating");
        ImGui::TableSetupColumn("Votes");
        ImGui::TableHeadersRow();
        for (const auto& row : leaderboard) {
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          ImGui::TextUnformatted(row.value("name", row.value("id", "")).c_str());
          ImGui::TableSetColumnIndex(1);
          ImGui::Text("%.0f", row.value("rating", 0.0));
          ImGui::TableSetColumnIndex(2);
          ImGui::Text("%d", row.value("won", 0) + row.value("lost", 0));
        }
        ImGui::EndTable();
      }
      break;
    }

    case Tab::Connections: {
      card("connections", [&] {
        ImGui::BeginChild("##connjson", ImVec2(width - 40.0f, 420.0f));
        theme::pushMono();
        ImGui::PushTextWrapPos(width - 60.0f);
        ImGui::TextUnformatted(gConnections.dump(2).c_str());
        ImGui::PopTextWrapPos();
        theme::pop();
        ImGui::EndChild();
      }, width);
      break;
    }
  }

  paneEnd();
}

}  // namespace app
