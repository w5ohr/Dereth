// Smart folders and tags in the app (#147): the Settings card with the rules,
// "Apply rules now" (folders created as needed, chats moved and tagged on the
// server), auto-tagging of untagged chats by the small model, and the
// optional automatic pass after chat list refreshes.
#include <algorithm>
#include <atomic>
#include <map>

#include "app.h"
#include "core/async.h"
#include "core/common.h"
#include "core/settings.h"
#include "core/smartfolders.h"
#include "imgui_stdlib.h"
#include "pane_common.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

std::atomic<bool> gBusy{false};
std::string gStatus;
double gLastAuto = 0.0;
smartfolders::Rule gDraft;
bool gShowAdd = false;

std::vector<smartfolders::Rule> rules() { return smartfolders::rulesFromJson(core::Settings::instance().extra.value("smartFolders", smartfolders::Json::array())); }
void saveRules(const std::vector<smartfolders::Rule>& list) {
  core::Settings::instance().extra["smartFolders"] = smartfolders::rulesToJson(list);
  core::Settings::instance().save();
}

std::string firstText(const Json& chatJson, const char* role) {
  const Json history = chatJson.value("history", Json::object());
  const Json messages = history.is_object() ? history.value("messages", Json::object()) : Json::object();
  if (!messages.is_object()) return "";
  std::string best;
  int64_t bestTs = INT64_MAX;
  for (const auto& [id, m] : messages.items()) {
    if (!m.is_object() || m.value("role", "") != role) continue;
    const int64_t ts = m.value("timestamp", static_cast<int64_t>(0));
    if (ts < bestTs || best.empty()) {
      bestTs = ts;
      best = m.value("content", "");
    }
  }
  return best;
}

}  // namespace

void App::applySmartFolders(bool manual) {
  if (gBusy.load()) return;
  const std::vector<smartfolders::Rule> list = rules();
  if (list.empty()) {
    if (manual) widgets::toastInfo("No smart folder rules yet");
    return;
  }
  std::vector<smartfolders::ChatFacts> facts;
  for (const ChatSummary& c : chats) {
    smartfolders::ChatFacts f;
    f.id = c.id;
    f.title = c.title;
    f.folderId = c.folderId;
    f.tags = c.tags;
    f.updatedAt = c.updatedAt;
    facts.push_back(f);
  }
  std::map<std::string, std::string> folderIds;
  for (const Json& f : folders) {
    if (f.is_object()) folderIds[core::lower(f.value("name", ""))] = f.value("id", "");
  }
  const bool fetch = smartfolders::needsChatBody(list);
  gBusy.store(true);
  gStatus = "Applying rules...";
  async::run([this, list, facts, folderIds, fetch, manual]() mutable {
    if (fetch) {
      int fetched = 0;
      for (smartfolders::ChatFacts& f : facts) {
        if (fetched++ >= 150) break;
        api::Result res = api_.chat(f.id);
        if (!res.ok || !res.data.is_object()) continue;
        const Json chatJson = res.data.value("chat", Json::object());
        if (chatJson.is_object()) {
          const Json models = chatJson.value("models", Json::array());
          if (models.is_array()) {
            for (const auto& m : models) {
              if (m.is_string()) f.models.push_back(m.get<std::string>());
            }
          }
          f.text = firstText(chatJson, "user");
        }
        const Json meta = res.data.value("meta", Json::object());
        if (meta.is_object() && meta.contains("tags") && meta["tags"].is_array()) {
          f.tags.clear();
          for (const auto& t : meta["tags"]) {
            if (t.is_string()) f.tags.push_back(t.get<std::string>());
          }
        }
      }
    }
    const std::vector<smartfolders::Action> actions = smartfolders::plan(list, facts, core::nowSeconds(), folderIds);
    int failures = 0;
    for (const smartfolders::Action& a : actions) {
      if (!a.folder.empty()) {
        std::string id;
        auto it = folderIds.find(core::lower(a.folder));
        if (it != folderIds.end()) id = it->second;
        if (id.empty()) {
          api::Result created = api_.createFolder(a.folder);
          if (created.ok && created.data.is_object()) id = created.data.value("id", "");
          if (id.empty()) {
            // Maybe it exists after all (a race or a differently cased name): re-read the list.
            api::Result all = api_.folders();
            if (all.ok && all.data.is_array()) {
              for (const auto& f : all.data) {
                if (f.is_object() && core::lower(f.value("name", "")) == core::lower(a.folder)) id = f.value("id", "");
              }
            }
          }
          if (!id.empty()) folderIds[core::lower(a.folder)] = id;
        }
        if (id.empty() || !api_.moveChatToFolder(a.chatId, id).ok) ++failures;
      }
      if (!a.tag.empty() && !api_.addChatTag(a.chatId, a.tag).ok) ++failures;
    }
    const std::string text = smartfolders::summary(actions) + (failures ? " - " + std::to_string(failures) + " failed" : "");
    async::post([this, text, actions, manual] {
      gBusy.store(false);
      gStatus = core::formatTime(core::nowSeconds(), "%H:%M") + ": " + text;
      if (manual || !actions.empty()) widgets::toast(actions.empty() ? widgets::ToastLevel::Info : widgets::ToastLevel::Success, "Smart folders: " + text);
      if (!actions.empty()) refreshChats();
    });
  });
}

void App::autoTagChats(bool manual) {
  if (gBusy.load()) return;
  std::string model = settings().extra.value("autoRouteSmallModel", std::string());
  if (model.empty() && !selectedModels.empty()) model = selectedModels.front();
  if (model.empty()) {
    if (manual) widgets::toastError("Pick a small model first (Settings > Auto-routing) or select a model");
    return;
  }
  std::vector<ChatSummary> untagged;
  for (const ChatSummary& c : chats) {
    if (c.tags.empty()) untagged.push_back(c);
  }
  std::sort(untagged.begin(), untagged.end(), [](const ChatSummary& a, const ChatSummary& b) { return a.updatedAt > b.updatedAt; });
  if (untagged.size() > 15) untagged.resize(15);
  if (untagged.empty()) {
    if (manual) widgets::toastInfo("Every chat already has tags");
    return;
  }
  gBusy.store(true);
  gStatus = "Tagging " + std::to_string(untagged.size()) + " chat(s) with " + model + "...";
  async::run([this, untagged, model, manual] {
    int tagged = 0, failures = 0;
    for (const ChatSummary& c : untagged) {
      api::Result res = api_.chat(c.id);
      if (!res.ok || !res.data.is_object()) {
        ++failures;
        continue;
      }
      const Json chatJson = res.data.value("chat", Json::object());
      const std::string userText = firstText(chatJson, "user");
      const std::string replyText = firstText(chatJson, "assistant");
      if (userText.empty()) continue;
      api::Result verdict = api_.completion(Json{{"model", model},
                                                 {"stream", false},
                                                 {"messages", Json::array({Json{{"role", "system"}, {"content", smartfolders::tagSystemPrompt()}},
                                                                           Json{{"role", "user"}, {"content", smartfolders::tagUserPrompt(c.title, userText, replyText)}}})}});
      std::string text;
      if (verdict.ok) {
        const Json& choices = verdict.data.value("choices", Json::array());
        if (choices.is_array() && !choices.empty()) text = choices[0].value("message", Json::object()).value("content", "");
      }
      const std::vector<std::string> tags = smartfolders::parseTags(text);
      if (tags.empty()) {
        ++failures;
        core::debugLog("auto-tag: no tags for %s: %.120s", c.title.c_str(), text.c_str());
        continue;
      }
      bool ok = true;
      for (const std::string& t : tags) ok = api_.addChatTag(c.id, t).ok && ok;
      if (ok) ++tagged;
      else ++failures;
    }
    async::post([this, tagged, failures, manual] {
      gBusy.store(false);
      gStatus = core::formatTime(core::nowSeconds(), "%H:%M") + ": tagged " + std::to_string(tagged) + " chat(s)" + (failures ? ", " + std::to_string(failures) + " skipped" : "");
      if (manual || tagged) widgets::toast(widgets::ToastLevel::Success, "Auto-tag: " + std::to_string(tagged) + " chat" + (tagged == 1 ? "" : "s") + " tagged" + (failures ? ", " + std::to_string(failures) + " skipped" : ""));
      if (tagged) refreshChats();
    });
  });
}

void App::tickSmartFolders() {
  if (!signedIn_ || chats.empty() || gBusy.load()) return;
  const double now = core::nowMillis();
  if (now - gLastAuto < 10 * 60 * 1000.0) return;
  gLastAuto = now;
  if (settings().extra.value("autoTagChats", false)) autoTagChats(false);
  else if (settings().extra.value("smartFoldersAuto", false)) applySmartFolders(false);
}

void App::drawSmartFoldersSection(float width) {
  card("smartfolders", [&] {
    std::vector<smartfolders::Rule> list = rules();
    ImGui::PushTextWrapPos(width - 20.0f);
    ImGui::TextUnformatted("Rules file chats into folders and tag them by title, model, first message, tag or age; the small model can tag chats that have none.");
    ImGui::PopTextWrapPos();
    bool changed = false;
    int remove = -1;
    for (size_t i = 0; i < list.size(); ++i) {
      ImGui::PushID(static_cast<int>(i));
      if (ImGui::Checkbox("##on", &list[i].enabled)) changed = true;
      ImGui::SameLine();
      theme::pushBold();
      ImGui::TextUnformatted(list[i].name.c_str());
      theme::pop();
      ImGui::SameLine();
      widgets::dimText("%s", smartfolders::describe(list[i]).c_str());
      ImGui::SameLine();
      if (ImGui::SmallButton("Delete")) remove = static_cast<int>(i);
      ImGui::PopID();
    }
    if (list.empty()) widgets::dimText("No rules yet.");
    if (remove >= 0) {
      list.erase(list.begin() + remove);
      changed = true;
    }
    if (ImGui::SmallButton(gShowAdd ? "Hide" : "Add a rule...")) gShowAdd = !gShowAdd;
    if (gShowAdd) {
      const float w = std::min(300.0f, width * 0.4f);
      ImGui::SetNextItemWidth(w);
      ImGui::InputTextWithHint("##rname", "rule name", &gDraft.name);
      ImGui::SetNextItemWidth(w);
      ImGui::InputTextWithHint("##rfolder", "file into folder (created if missing)", &gDraft.folder);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(w);
      ImGui::InputTextWithHint("##rtag", "add tag", &gDraft.tag);
      ImGui::SetNextItemWidth(w);
      ImGui::InputTextWithHint("##rtitle", "when the title contains...", &gDraft.titleContains);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(w);
      ImGui::InputTextWithHint("##rmodel", "when a model name contains...", &gDraft.modelContains);
      ImGui::SetNextItemWidth(w);
      ImGui::InputTextWithHint("##rtext", "when the first message contains...", &gDraft.textContains);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(w);
      ImGui::InputTextWithHint("##rhastag", "when tagged with...", &gDraft.hasTag);
      ImGui::SetNextItemWidth(140);
      ImGui::InputInt("Older than (days)", &gDraft.olderThanDays);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(140);
      ImGui::InputInt("Newer than (days)", &gDraft.newerThanDays);
      gDraft.olderThanDays = std::max(0, gDraft.olderThanDays);
      gDraft.newerThanDays = std::max(0, gDraft.newerThanDays);
      const bool valid = !core::trim(gDraft.folder).empty() || !smartfolders::normalizeTag(gDraft.tag).empty();
      widgets::beginDisabled(!valid);
      if (ImGui::SmallButton("Save rule")) {
        smartfolders::Rule r = gDraft;
        r.id = core::uuid();
        r.enabled = true;
        r.folder = core::trim(r.folder);
        r.tag = smartfolders::normalizeTag(r.tag);
        r.hasTag = smartfolders::normalizeTag(r.hasTag);
        if (r.name.empty()) r.name = r.folder.empty() ? "tag " + r.tag : "file into " + r.folder;
        list.push_back(r);
        gDraft = smartfolders::Rule{};
        gShowAdd = false;
        changed = true;
      }
      widgets::endDisabled(!valid);
      if (!valid) {
        ImGui::SameLine();
        widgets::dimText("a rule needs a folder or a tag");
      }
    }
    if (changed) saveRules(list);

    ImGui::Dummy(ImVec2(0, 4));
    bool autoRules = settings().extra.value("smartFoldersAuto", false);
    if (ImGui::Checkbox("Apply the rules automatically (every 10 minutes while signed in)", &autoRules)) {
      settings().extra["smartFoldersAuto"] = autoRules;
      settings().save();
    }
    bool autoTag = settings().extra.value("autoTagChats", false);
    if (ImGui::Checkbox("Auto-tag chats that have no tags with the small model (every 10 minutes, 15 at a time)", &autoTag)) {
      settings().extra["autoTagChats"] = autoTag;
      settings().save();
    }
    widgets::beginDisabled(gBusy.load());
    if (ImGui::Button("Apply rules now")) applySmartFolders(true);
    ImGui::SameLine();
    if (ImGui::Button("Auto-tag untagged chats now")) autoTagChats(true);
    widgets::endDisabled(gBusy.load());
    if (!gStatus.empty()) {
      ImGui::SameLine();
      widgets::dimText("%s", gStatus.c_str());
    }
    widgets::dimText("The small model is the auto-routing one (%s). Folders and tags are the server's, so the web UI sees them too.",
                     settings().extra.value("autoRouteSmallModel", std::string()).empty() ? "none set - the selected model is used" : settings().extra.value("autoRouteSmallModel", std::string()).c_str());
  }, width);
}

}  // namespace app
