// API console: call any Open WebUI endpoint directly, with a browsable
// catalogue of what the server exposes.
#include <algorithm>

#include "core/async.h"
#include "core/common.h"
#include "pane_common.h"
#include "platform.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

struct Endpoint {
  const char* method;
  const char* path;
  const char* description;
};

// Grouped by the server's routers. Selecting one fills the request form.
struct Group {
  const char* name;
  std::vector<Endpoint> endpoints;
};

const std::vector<Group>& catalogue() {
  static const std::vector<Group> groups = {
      {"Session", {
          {"GET", "/api/config", "Server capabilities (no auth)"},
          {"GET", "/api/version", "Version"},
          {"GET", "/api/changelog", "Release notes"},
          {"GET", "/api/v1/auths/", "Current user"},
          {"POST", "/api/v1/auths/update/profile", "Change name / avatar"},
          {"POST", "/api/v1/auths/update/password", "Change password"},
          {"GET", "/api/v1/auths/api_key", "Read API key"},
          {"POST", "/api/v1/auths/api_key", "Create API key"},
          {"GET", "/api/v1/auths/admin/config", "Admin configuration"},
          {"GET", "/api/v1/auths/mfa/status", "Multi-factor status"},
      }},
      {"Chats", {
          {"GET", "/api/v1/chats/?page=1", "Recent chats"},
          {"GET", "/api/v1/chats/all", "Every chat"},
          {"GET", "/api/v1/chats/search?text=hello", "Search"},
          {"GET", "/api/v1/chats/pinned", "Pinned"},
          {"GET", "/api/v1/chats/all/archived", "Archived"},
          {"GET", "/api/v1/chats/all/tags", "Tags"},
          {"GET", "/api/v1/chats/stats/usage", "Usage per chat"},
          {"POST", "/api/v1/chats/new", "Create"},
          {"POST", "/api/v1/chats/archive/all", "Archive everything"},
      }},
      {"Completions", {
          {"POST", "/api/chat/completions", "Chat completion (SSE or background)"},
          {"POST", "/api/chat/completed", "Post-completion hook"},
          {"POST", "/api/message", "Anthropic-compatible messages"},
          {"POST", "/api/message/count_tokens", "Token count"},
          {"GET", "/api/tasks", "Running tasks"},
          {"POST", "/api/v1/tasks/title/completions", "Generate a title"},
          {"POST", "/api/v1/tasks/tags/completions", "Generate tags"},
          {"POST", "/api/v1/tasks/follow_up/completions", "Follow-up suggestions"},
          {"POST", "/api/v1/tasks/queries/completions", "Search queries"},
          {"POST", "/api/v1/tasks/emoji/completions", "Emoji reaction"},
          {"POST", "/api/v1/tasks/image_prompt/completions", "Image prompt"},
      }},
      {"Models", {
          {"GET", "/api/models", "Everything selectable"},
          {"GET", "/api/models/base", "Base models"},
          {"GET", "/api/v1/models/", "Custom models"},
          {"GET", "/api/v1/models/base/tags", "Base model tags"},
          {"POST", "/api/v1/models/create", "Create a custom model"},
          {"POST", "/api/v1/models/sync", "Sync"},
          {"GET", "/api/v1/models/export", "Export"},
      }},
      {"Knowledge & files", {
          {"GET", "/api/v1/files/", "Uploaded files"},
          {"GET", "/api/v1/files/search?filename=a", "Search files"},
          {"GET", "/api/v1/knowledge/", "Knowledge bases"},
          {"POST", "/api/v1/knowledge/reindex", "Reindex everything"},
          {"GET", "/api/v1/retrieval/config", "Retrieval settings"},
          {"POST", "/api/v1/retrieval/process/web", "Ingest a URL"},
          {"POST", "/api/v1/retrieval/process/youtube", "Ingest a video"},
          {"POST", "/api/v1/retrieval/process/web/search", "Web search"},
          {"POST", "/api/v1/retrieval/query/doc", "Query a collection"},
      }},
      {"Extensions", {
          {"GET", "/api/v1/tools/list", "Tools"},
          {"GET", "/api/v1/functions/list", "Functions"},
          {"GET", "/api/v1/skills/list", "Skills"},
          {"GET", "/api/v1/prompts/list", "Prompts"},
          {"GET", "/api/v1/pipelines/list", "Pipelines"},
          {"GET", "/api/v1/automations/list", "Automations"},
          {"GET", "/api/v1/apps/", "Apps"},
          {"GET", "/api/v1/terminals/", "Terminals"},
      }},
      {"Workspace", {
          {"GET", "/api/v1/notes/", "Notes"},
          {"GET", "/api/v1/channels/", "Channels"},
          {"GET", "/api/v1/folders/", "Folders"},
          {"GET", "/api/v1/memories/", "Memories"},
          {"GET", "/api/v1/calendars/", "Calendars"},
          {"GET", "/api/v1/calendars/events", "Calendar events"},
          {"GET", "/api/v1/notifications/events", "Notification events"},
      }},
      {"Media", {
          {"GET", "/api/v1/images/config", "Image backend"},
          {"GET", "/api/v1/images/models", "Image models"},
          {"POST", "/api/v1/images/generations", "Generate an image"},
          {"GET", "/api/v1/audio/config", "Audio backend"},
          {"GET", "/api/v1/audio/voices", "Voices"},
          {"GET", "/api/v1/audio/models", "Speech models"},
      }},
      {"Admin", {
          {"GET", "/api/v1/users/", "Users"},
          {"GET", "/api/v1/users/permissions", "Permissions"},
          {"GET", "/api/v1/groups/", "Groups"},
          {"GET", "/api/v1/configs/connections", "Model connections"},
          {"GET", "/api/v1/configs/banners", "Banners"},
          {"GET", "/api/v1/configs/export", "Export configuration"},
          {"GET", "/api/v1/analytics/summary", "Analytics summary"},
          {"GET", "/api/v1/analytics/daily", "Daily analytics"},
          {"GET", "/api/v1/evaluations/leaderboard", "Model leaderboard"},
          {"GET", "/api/v1/evaluations/feedbacks/list", "Feedback"},
          {"GET", "/api/usage", "Usage"},
      }},
      {"Ollama", {
          {"GET", "/ollama/api/tags", "Installed models"},
          {"GET", "/ollama/api/ps", "Resident models"},
          {"GET", "/ollama/api/version", "Ollama version"},
          {"POST", "/ollama/api/show", "Model details"},
          {"POST", "/ollama/api/pull", "Pull a model"},
          {"POST", "/ollama/api/unload", "Unload a model"},
          {"POST", "/ollama/api/embed", "Embeddings"},
      }},
      {"Utilities", {
          {"POST", "/api/v1/utils/code/execute", "Run code server-side"},
          {"POST", "/api/v1/utils/code/format", "Format code"},
          {"GET", "/api/v1/utils/gravatar?email=a@b.c", "Gravatar URL"},
      }},
  };
  return groups;
}

int gMethodIndex = 0;
std::string gPath = "/api/config";
std::string gBody;
std::string gResponse;
long gStatus = 0;
double gElapsedMs = 0.0;
bool gBusy = false;
std::string gSearch;

const char* kMethods[] = {"GET", "POST", "PUT", "PATCH", "DELETE"};

}  // namespace

void App::drawConsolePane() {
  paneHeader("API Console", "Call any endpoint on this server with your session", [&] {
    ImGui::SameLine(ImGui::GetWindowWidth() - 150.0f);
    widgets::dimText("%s", api_.server().c_str());
  });

  ImGui::BeginChild("##catalogue", ImVec2(330, 0));
  ImGui::Dummy(ImVec2(0, 8));
  ImGui::Indent(12.0f);
  widgets::searchField("apisearch", &gSearch, "Find an endpoint", 294.0f);
  ImGui::Dummy(ImVec2(0, 6));
  for (const Group& group : catalogue()) {
    bool anyVisible = gSearch.empty();
    for (const Endpoint& endpoint : group.endpoints) {
      if (core::containsNoCase(endpoint.path, gSearch) ||
          core::containsNoCase(endpoint.description, gSearch)) {
        anyVisible = true;
      }
    }
    if (!anyVisible) continue;
    if (ImGui::TreeNodeEx(group.name, gSearch.empty() ? 0 : ImGuiTreeNodeFlags_DefaultOpen)) {
      for (const Endpoint& endpoint : group.endpoints) {
        if (!gSearch.empty() && !core::containsNoCase(endpoint.path, gSearch) &&
            !core::containsNoCase(endpoint.description, gSearch)) {
          continue;
        }
        ImGui::PushID(endpoint.path);
        const std::string label = std::string(endpoint.method) + "  " + endpoint.path;
        if (ImGui::Selectable(label.c_str(), gPath == endpoint.path)) {
          gPath = endpoint.path;
          for (int i = 0; i < IM_ARRAYSIZE(kMethods); ++i) {
            if (std::string(kMethods[i]) == endpoint.method) gMethodIndex = i;
          }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", endpoint.description);
        ImGui::PopID();
      }
      ImGui::TreePop();
    }
  }
  ImGui::Unindent(12.0f);
  ImGui::EndChild();

  ImGui::SameLine();

  ImGui::BeginChild("##request", ImVec2(0, 0));
  ImGui::Dummy(ImVec2(0, 8));
  ImGui::Indent(16.0f);
  const float width = ImGui::GetContentRegionAvail().x - 24.0f;

  ImGui::SetNextItemWidth(110);
  ImGui::Combo("##method", &gMethodIndex, kMethods, IM_ARRAYSIZE(kMethods));
  ImGui::SameLine();
  ImGui::SetNextItemWidth(width - 230.0f);
  const bool submitted =
      ImGui::InputText("##path", &gPath, ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::SameLine();
  widgets::beginDisabled(gBusy);
  const bool send = widgets::primaryButton(gBusy ? "..." : "Send", ImVec2(100, 0)) || submitted;
  widgets::endDisabled(gBusy);

  ImGui::Dummy(ImVec2(0, 6));
  ImGui::TextUnformatted("Request body (JSON)");
  theme::pushMono();
  ImGui::InputTextMultiline("##body", &gBody, ImVec2(width, 130.0f));
  theme::pop();

  if (send && !gBusy) {
    gBusy = true;
    gResponse.clear();
    const std::string method = kMethods[gMethodIndex];
    const std::string path = core::trim(gPath);
    Json body = Json();
    if (!core::trim(gBody).empty()) {
      body = Json::parse(gBody, nullptr, false);
      if (body.is_discarded()) {
        gBusy = false;
        widgets::toastError("Request body is not valid JSON");
      }
    }
    if (gBusy) {
      const double started = core::nowMillis();
      async::run([this, method, path, body, started] {
        api::Result res = api_.request(method, path, body, 300);
        async::post([res, started] {
          gBusy = false;
          gStatus = res.status;
          gElapsedMs = core::nowMillis() - started;
          gResponse = res.data.is_null() ? res.raw : res.data.dump(2);
          if (gResponse.empty() && !res.error.empty()) gResponse = res.error;
        });
      });
    }
  }

  ImGui::Dummy(ImVec2(0, 8));
  if (gStatus != 0) {
    const bool ok = gStatus >= 200 && gStatus < 300;
    widgets::pill("HTTP " + std::to_string(gStatus),
                  ok ? theme::palette().success : theme::palette().danger,
                  IM_COL32(255, 255, 255, 255));
    ImGui::SameLine();
    widgets::dimText("%.0f ms - %d bytes", gElapsedMs, static_cast<int>(gResponse.size()));
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy response")) platform::setClipboardText(gResponse);
  }

  ImGui::BeginChild("##response", ImVec2(width, ImGui::GetContentRegionAvail().y - 20.0f));
  theme::pushMono();
  ImGui::PushTextWrapPos(width - 20.0f);
  ImGui::TextUnformatted(gResponse.c_str());
  ImGui::PopTextWrapPos();
  theme::pop();
  ImGui::EndChild();

  ImGui::Unindent(16.0f);
  ImGui::EndChild();
}

}  // namespace app
