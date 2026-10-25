#include "app.h"

#include "plugins/plugin_host.h"

#include <algorithm>
#include <cstdlib>
#include <set>

#include "core/async.h"
#include "core/common.h"
#include "core/doctext.h"
#include "core/keychain.h"
#include "core/store.h"
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

constexpr const char* kTokenAccount = "session-token";

struct NavItem {
  Pane pane;
  icons::Icon icon;
  const char* label;
  bool adminOnly;
};

const NavItem kNav[] = {
    {Pane::Chat, icons::Icon::Chat, "Chats", false},
    {Pane::Notes, icons::Icon::Note, "Notes", false},
    {Pane::Channels, icons::Icon::Channel, "Channels", false},
    {Pane::Workspace, icons::Icon::Layers, "Workspace", false},
    {Pane::Images, icons::Icon::Image, "Images", false},
    {Pane::Files, icons::Icon::Folder, "Files", false},
    {Pane::Memories, icons::Icon::Brain, "Memories", false},
    {Pane::Library, icons::Icon::Archive, "Library", false},
    {Pane::Admin, icons::Icon::Wrench, "Admin", true},
    {Pane::Console, icons::Icon::Code, "API Console", false},
    {Pane::Tools, icons::Icon::Wrench, "Tools", false},
    {Pane::Settings, icons::Icon::Gear, "Settings", false},
};

}  // namespace

// ---------------------------------------------------------------- data model

Json Attachment::toJson() const {
  if (local) {
    return Json{{"type", "local_document"}, {"name", name}, {"hash", hash}, {"kind", kind}, {"pages", pages}, {"chars", chars}};
  }
  Json j{
      {"type", isImage ? "image" : "file"},
      {"name", name},
  };
  if (!id.empty()) {
    j["id"] = id;
    j["file"] = Json{{"id", id}, {"filename", name}, {"meta", Json{{"content_type", mime}}}};
    j["collection_name"] = "";
    j["status"] = "uploaded";
  }
  if (isImage && !dataUrl.empty()) j["url"] = dataUrl;
  return j;
}

Json Message::toJson() const {
  Json j{
      {"id", id},
      {"parentId", parentId.empty() ? Json(nullptr) : Json(parentId)},
      {"childrenIds", childrenIds},
      {"role", role},
      {"content", content},
      {"timestamp", timestamp},
      {"done", !streaming},
  };
  if (!model.empty()) {
    j["model"] = model;
    j["modelName"] = modelName.empty() ? model : modelName;
    j["modelIdx"] = 0;
  }
  if (!files.empty()) j["files"] = files;
  if (!sources.empty()) j["sources"] = sources;
  if (!usage.empty()) j["usage"] = usage;
  if (!reasoning.empty()) j["reasoning_content"] = reasoning;
  if (!error.empty()) j["error"] = Json{{"content", error}};
  if (rating != 0) j["annotation"] = Json{{"rating", rating}};
  if (!followUps.empty()) j["followUps"] = followUps;
  if (!toolCalls.empty()) {
    Json calls = Json::array();
    for (const ToolCall& call : toolCalls) calls.push_back(call.toJson());
    j["mcpToolCalls"] = calls;
  }
  if (agentRun.is_object()) j["agentRun"] = agentRun;
  return j;
}

Message Message::fromJson(const Json& j) {
  Message m;
  m.id = j.value("id", core::uuid());
  if (j.contains("parentId") && j["parentId"].is_string()) m.parentId = j["parentId"];
  if (j.contains("childrenIds") && j["childrenIds"].is_array()) {
    for (const auto& child : j["childrenIds"]) {
      if (child.is_string()) m.childrenIds.push_back(child);
    }
  }
  m.role = j.value("role", "user");
  if (j.contains("content")) {
    if (j["content"].is_string()) {
      m.content = j["content"];
    } else {
      m.content = j["content"].dump();
    }
  }
  m.timestamp = j.value("timestamp", static_cast<int64_t>(0));
  m.model = j.value("model", "");
  m.modelName = j.value("modelName", m.model);
  if (j.contains("files") && j["files"].is_array()) m.files = j["files"];
  if (j.contains("sources") && j["sources"].is_array()) m.sources = j["sources"];
  if (j.contains("usage") && j["usage"].is_object()) m.usage = j["usage"];
  if (j.contains("reasoning_content") && j["reasoning_content"].is_string()) {
    m.reasoning = j["reasoning_content"];
  }
  if (j.contains("error")) {
    m.error = j["error"].is_object() ? j["error"].value("content", "") : j["error"].dump();
  }
  if (j.contains("annotation") && j["annotation"].is_object()) {
    m.rating = j["annotation"].value("rating", 0);
  }
  if (j.contains("followUps") && j["followUps"].is_array()) m.followUps = j["followUps"];
  if (j.contains("agentRun") && j["agentRun"].is_object()) m.agentRun = j["agentRun"];
  if (j.contains("mcpToolCalls") && j["mcpToolCalls"].is_array()) {
    for (const auto& call : j["mcpToolCalls"]) m.toolCalls.push_back(ToolCall::fromJson(call));
  }
  return m;
}

std::vector<std::string> ChatDoc::branch() const {
  std::vector<std::string> path;
  std::string cursor = currentId;
  while (!cursor.empty()) {
    auto it = messages.find(cursor);
    if (it == messages.end()) break;
    path.push_back(cursor);
    cursor = it->second.parentId;
  }
  std::reverse(path.begin(), path.end());
  return path;
}

std::vector<std::string> ChatDoc::siblings(const std::string& messageId) const {
  auto it = messages.find(messageId);
  if (it == messages.end()) return {};
  const std::string& parentId = it->second.parentId;
  if (parentId.empty()) {
    std::vector<std::string> roots;
    for (const auto& [id, message] : messages) {
      if (message.parentId.empty()) roots.push_back(id);
    }
    std::sort(roots.begin(), roots.end(), [&](const std::string& a, const std::string& b) {
      return messages.at(a).timestamp < messages.at(b).timestamp;
    });
    return roots;
  }
  auto parent = messages.find(parentId);
  if (parent == messages.end()) return {};
  return parent->second.childrenIds;
}

Json ChatDoc::toJson() const {
  // Open WebUI 0.12 keeps whatever it already stores for an assistant message
  // marked done:false - it assumes its own pipeline is still streaming it - so
  // a placeholder saved mid-stream could never be updated afterwards. Messages
  // that are still streaming are therefore left out until they finish.
  std::set<std::string> omitted;
  for (const auto& [id, message] : messages) {
    if (message.streaming && !message.serverStream) omitted.insert(id);
  }
  std::string current = currentId;
  while (!current.empty() && omitted.count(current)) current = messages.at(current).parentId;

  Json history{{"currentId", current}, {"messages", Json::object()}};
  for (const auto& [id, message] : messages) {
    if (omitted.count(id)) continue;
    Json entry = message.toJson();
    if (!omitted.empty()) {
      Json children = Json::array();
      for (const std::string& child : message.childrenIds) {
        if (!omitted.count(child)) children.push_back(child);
      }
      entry["childrenIds"] = children;
    }
    history["messages"][id] = entry;
  }

  Json linear = Json::array();
  for (const std::string& id : branch()) {
    if (!omitted.count(id)) linear.push_back(history["messages"][id]);
  }

  Json out = raw.is_object() ? raw : Json::object();
  out["id"] = id;
  out["title"] = title;
  out["models"] = models;
  out["history"] = history;
  out["messages"] = linear;
  out["tags"] = tags;
  out["params"] = params;
  out["timestamp"] = static_cast<int64_t>(core::nowSeconds()) * 1000;
  if (!folderId.empty()) out["folder_id"] = folderId;
  if (!backendId.empty()) out["desktop_backend"] = backendId;
  return out;
}

ChatDoc ChatDoc::fromJson(const Json& chat, const std::string& chatId) {
  ChatDoc doc;
  doc.id = chatId;
  doc.raw = chat;
  doc.title = chat.value("title", "New Chat");
  if (chat.contains("models") && chat["models"].is_array()) {
    for (const auto& model : chat["models"]) {
      if (model.is_string()) doc.models.push_back(model);
    }
  }
  if (chat.contains("tags") && chat["tags"].is_array()) {
    for (const auto& tag : chat["tags"]) {
      if (tag.is_string()) doc.tags.push_back(tag);
    }
  }
  if (chat.contains("params") && chat["params"].is_object()) doc.params = chat["params"];
  doc.isPrivate = chat.value("desktop_private", false);
  doc.backendId = chat.value("desktop_backend", "");
  if (chat.contains("folder_id") && chat["folder_id"].is_string()) doc.folderId = chat["folder_id"];

  if (chat.contains("history") && chat["history"].is_object()) {
    const Json& history = chat["history"];
    doc.currentId = history.value("currentId", "");
    if (history.contains("messages") && history["messages"].is_object()) {
      for (const auto& [id, message] : history["messages"].items()) {
        Message m = Message::fromJson(message);
        m.id = id;
        doc.messages[id] = m;
      }
    }
  }
  // Older chats only carry the flat list.
  if (doc.messages.empty() && chat.contains("messages") && chat["messages"].is_array()) {
    std::string previous;
    for (const auto& message : chat["messages"]) {
      Message m = Message::fromJson(message);
      if (m.parentId.empty()) m.parentId = previous;
      if (!previous.empty()) doc.messages[previous].childrenIds.push_back(m.id);
      previous = m.id;
      doc.messages[m.id] = m;
    }
    doc.currentId = previous;
  }
  return doc;
}

// --------------------------------------------------------------------- app

App& App::instance() {
  static App app;
  return app;
}

void App::init() {
  core::Settings& cfg = settings();
  // OWUI_SERVER / OWUI_TOKEN let you point the app at another instance without
  // touching the saved settings — handy for testing against a scratch server.
  // The override is applied to the client only and never written into
  // cfg.serverUrl, so quitting a test run can't save the scratch server.
  const char* envServer = std::getenv("OWUI_SERVER");
  api_.setServer(envServer ? std::string(envServer) : cfg.serverUrl);
  serverInput_ = api_.server();
  store::open(api_.server());
  loginEmail_ = cfg.email;
  sessionId_ = core::uuid();
  selectedModels = cfg.selectedModels;

  // The keychain read is skipped when a token comes from the environment, and
  // when no session was ever saved, so neither case shows a keychain prompt.
  if (const char* envToken = std::getenv("OWUI_TOKEN")) {
    api_.setToken(envToken);
    loadSession();
  } else if (cfg.hasSavedSession) {
    // Reading can block on the system keychain dialog, so keep it off the UI
    // thread: the sign-in screen stays live while macOS asks.
    connecting_ = true;
    async::run([this] {
      const std::string token = keychain::load(kTokenAccount);
      async::post([this, token] {
        connecting_ = false;
        if (token.empty()) {
          settings().hasSavedSession = false;
          settings().save();
          return;
        }
        api_.setToken(token);
        loadSession();
      });
    });
  }

  connecting_ = true;
  async::run([this] {
    api::Result res = api_.config();
    async::post([this, res] {
      connecting_ = false;
      if (res.ok) {
        serverConfig_ = res.data;
      } else {
        authError_ = "Cannot reach " + api_.server() + ": " + res.error;
      }
    });
  });
  auditInit();
  trafficInit();
  applyTheme();   // lays the theme editor's overrides and the server accent over the base theme
  mcpInit();
  pluginsInit();
  refreshPrivateChats();
  triggersInit();
}

void App::shutdown() {
  warmupOnQuit();
  stopAllStreams();
  if (terminal) terminal->stop();
  stopFolderSync();
  triggersShutdown();
  pluginsShutdown();
  mcpShutdown();
  socket_.stop();
  if (!secondaryInstance) {
    // Only the first window owns the settings file.
    saveDraft();
    settings().selectedModels = selectedModels;
    settings().save();
  }
  // Join the pool here: letting the static thread vector run its destructor at
  // process exit with live threads aborts.
  async::shutdown();
}

void App::loadSession() {
  async::run([this] {
    api::Result res = api_.sessionUser();
    async::post([this, res] {
      if (res.ok && res.data.is_object()) {
        user_ = res.data;
        signedIn_ = true;
        authError_.clear();
        startSocket();
        refreshModels();
        refreshChats(false);
      } else {
        signedIn_ = false;
        api_.setToken("");
        keychain::erase(kTokenAccount);
        settings().hasSavedSession = false;
        settings().save();
      }
    });
  });
}

void App::signIn(const std::string& email, const std::string& password) {
  connecting_ = true;
  authError_.clear();
  api_.setServer(serverInput_);
  settings().serverUrl = api_.server();
  settings().email = email;
  settings().save();

  async::run([this, email, password] {
    api::Result res = api_.signin(email, password);
    async::post([this, res, email] {
      connecting_ = false;
      if (res.ok && res.data.contains("token")) {
        const std::string token = res.data["token"];
        api_.setToken(token);
        if (settings().rememberSession) {
          settings().hasSavedSession = keychain::save(kTokenAccount, token);
          // Profiles look tokens up per server; keep that copy too.
          keychain::save(std::string(kTokenAccount) + ":" + api_.server(), token);
          settings().save();
        }
        user_ = res.data;
        signedIn_ = true;
        loginPassword_.clear();
        startSocket();
        refreshModels();
        refreshChats(false);
        widgets::toast(widgets::ToastLevel::Success,
                       "Signed in as " + res.data.value("name", email));
      } else {
        authError_ = api::errorMessage(res);
      }
    });
  });
}

void App::signOut() {
  stopAllStreams();
  socket_.stop();
  async::run([this] { api_.signout(); });
  api_.setToken("");
  keychain::erase(kTokenAccount);
  settings().hasSavedSession = false;
  settings().save();
  signedIn_ = false;
  user_ = Json::object();
  chats.clear();
  chat = ChatDoc();
  models.clear();
}

bool App::isAdmin() const { return user_.value("role", "") == "admin"; }

std::string App::imageUrlFor(const std::string& url) const {
  if (core::startsWith(url, "http://") || core::startsWith(url, "https://") ||
      core::startsWith(url, "data:")) {
    return url;
  }
  return api_.server() + (core::startsWith(url, "/") ? url : "/" + url);
}

std::string App::fetchImageBytes(const std::string& url) {
  if (core::startsWith(url, "data:")) {
    size_t comma = url.find(',');
    if (comma == std::string::npos) return {};
    const std::string payload = url.substr(comma + 1);
    if (url.find(";base64") != std::string::npos) return core::base64Decode(payload);
    return payload;
  }
  std::string bytes;
  if (api_.fetchBinary(imageUrlFor(url), &bytes)) return bytes;
  return {};
}

// ------------------------------------------------------------------ chat ops

void App::newChat() {
  rememberScroll();
  stopAllStreams();
  saveDraft();
  followTail = true;
  newTextPending = false;
  pendingScrollRestore = -1.0f;
  chat = ChatDoc();
  chat.models = selectedModels;
  attachments.clear();
  composer.clear();
  ++composerGeneration;
  restoreDraft();
  scrollToBottom = true;
  followTail = true;
  focusComposer = true;
  pane = Pane::Chat;
}

void App::rememberScroll() {
  if (!chat.id.empty()) scrollMemory[chat.id] = lastScrollY;
}

// A remembered offset is applied a couple of frames in, once the messages
// have a size; otherwise the chat opens at its end.
void App::restoreScrollFor(const std::string& id) {
  auto it = scrollMemory.find(id);
  if (it != scrollMemory.end()) {
    pendingScrollRestore = it->second;
    scrollRestoreFrames = 2;
    followTail = false;
  } else {
    scrollToBottom = true;
    followTail = true;
  }
  newTextPending = false;
}

void App::openChat(const std::string& id) {
  if (id == chat.id) return;
  rememberScroll();
  if (isPrivateChat(id)) {
    openPrivateChat(id);
    return;
  }
  stopAllStreams();
  saveDraft();
  pane = Pane::Chat;
  async::run([this, id] {
    api::Result res = api_.chat(id);
    async::post([this, res, id] {
      Json fallback;
      if (!res.ok) {
        // Server away: open the local copy instead.
        if (auto cached = store::getChat(id)) {
          fallback = *cached;
          widgets::toastInfo("Opened the local copy - the server could not be reached");
        } else {
          widgets::toastError("Could not open chat: " + api::errorMessage(res));
          return;
        }
      } else {
        store::putChat(res.data);
      }
      const Json& body = res.ok ? res.data : fallback;
      ChatDoc doc = ChatDoc::fromJson(body.value("chat", Json::object()), id);
      doc.pinned = body.value("pinned", false);
      doc.archived = body.value("archived", false);
      doc.updatedAt = body.value("updated_at", static_cast<int64_t>(0));
      if (body.contains("folder_id") && body["folder_id"].is_string()) {
        doc.folderId = body["folder_id"];
      }
      chat = doc;
      if (!chat.models.empty()) selectedModels = chat.models;
      composer.clear();
      ++composerGeneration;
      restoreDraft();
      restoreScrollFor(id);
      focusComposer = true;
      focusedMessageId.clear();
      if (!pendingFocusMessage.empty()) {   // opened from a search hit
        focusMessageInChat(pendingFocusMessage);
        pendingFocusMessage.clear();
      }
      updateHandoffAndSpotlight();
    });
  });
}

void App::refreshChats(bool silent) {
  if (!signedIn_) return;
  if (!silent) chatsLoading = true;
  async::run([this] {
    api::Result list = api_.chatList(1);
    api::Result folderRes = api_.folders();
    async::post([this, list, folderRes] {
      chatsLoading = false;
      if (!list.ok && list.status == 0) {
        // Unreachable: show what the mirror has and remember we're offline.
        loadChatsFromMirror(false);
        serverReachable = false;
        lastReachCheck = 0.0;
        return;
      }
      if (list.ok && list.data.is_array()) {
        offlineList = false;
        for (const auto& item : list.data) store::putListItem(item);
        chats.clear();
        for (const auto& item : list.data) {
          ChatSummary summary;
          summary.id = item.value("id", "");
          summary.title = item.value("title", "Untitled");
          summary.updatedAt = item.value("updated_at", static_cast<int64_t>(0));
          summary.pinned = item.value("pinned", false);
          summary.archived = item.value("archived", false);
          if (item.contains("folder_id") && item["folder_id"].is_string()) {
            summary.folderId = item["folder_id"];
          }
          chats.push_back(summary);
        }
      }
      if (folderRes.ok && folderRes.data.is_array()) {
        folders.clear();
        for (const auto& folder : folderRes.data) folders.push_back(folder);
      }
      native::refreshStatusMenu();
    });
  });
}

void App::refreshModels() {
  if (!signedIn_) return;
  modelsLoading = true;
  async::run([this] {
    api::Result res = api_.models();
    async::post([this, res] {
      modelsLoading = false;
      if (!res.ok) {
        widgets::toastError("Model list failed: " + api::errorMessage(res));
        return;
      }
      models.clear();
      const Json& data = res.data.contains("data") ? res.data["data"] : res.data;
      if (!data.is_array()) return;
      for (const auto& item : data) {
        ModelInfo info;
        info.id = item.value("id", "");
        info.name = item.value("name", info.id);
        info.owner = item.value("owned_by", "");
        if (item.contains("info") && item["info"].is_object()) {
          info.isCustom = true;
          const Json& meta = item["info"].value("meta", Json::object());
          info.description = meta.value("description", "");
        }
        info.raw = item;
        if (!info.id.empty()) models.push_back(info);
      }
      std::sort(models.begin(), models.end(),
                [](const ModelInfo& a, const ModelInfo& b) { return core::lower(a.name) < core::lower(b.name); });
      // Drop selections the server no longer offers.
      std::vector<std::string> keep;
      for (const std::string& id : selectedModels) {
        if (std::any_of(models.begin(), models.end(),
                        [&](const ModelInfo& m) { return m.id == id; })) {
          keep.push_back(id);
        }
      }
      selectedModels = keep;
      if (selectedModels.empty() && !models.empty()) selectedModels.push_back(models.front().id);
    });
  });
}

void App::attachFiles(const std::vector<std::string>& paths) {
  for (const std::string& path : paths) {
    bool ok = false;
    std::string bytes = core::readFile(path, &ok);
    if (!ok) {
      widgets::toastError("Could not read " + core::baseName(path));
      continue;
    }
    attachBytes(core::baseName(path), core::mimeForExtension(core::extension(path)), bytes);
  }
}

void App::attachBytes(const std::string& name, const std::string& mime, const std::string& bytes) {
  Attachment attachment;
  attachment.name = name;
  attachment.mime = mime;
  attachment.bytes = bytes;
  attachment.isImage = core::startsWith(mime, "image/");
  attachment.uploading = !attachment.isImage;
  if (attachment.isImage) {
    attachment.dataUrl = "data:" + mime + ";base64," + core::base64Encode(bytes);
  }
  attachments.push_back(attachment);
  if (attachment.isImage) return;

  // Documents are read on this Mac unless the setting says to upload them;
  // a private chat never uploads.
  const size_t index = attachments.size() - 1;
  const bool readLocally = doctext::supported(name, mime) && (chat.isPrivate || settings().extra.value("localDocuments", true));
  if (readLocally) attachDocument(index);
  else uploadAttachment(index);
}

void App::uploadAttachment(size_t index) {
  if (index >= attachments.size()) return;
  attachments[index].uploading = true;
  const std::string name = attachments[index].name, mime = attachments[index].mime, bytes = attachments[index].bytes;
  async::run([this, index, name, mime, bytes] {
    api::Result res = api_.uploadFile(name, mime, bytes);
    async::post([this, index, res, name] {
      if (index >= attachments.size()) return;
      attachments[index].uploading = false;
      if (res.ok && res.data.contains("id")) {
        attachments[index].id = res.data["id"];
      } else {
        attachments[index].failed = true;
        widgets::toastError("Upload failed for " + name + ": " + api::errorMessage(res));
      }
    });
  });
}

Json App::buildCompletionPayload(const std::string& assistantId, const std::string& model) {
  Json messages = Json::array();
  const Message& assistant = chat.messages.at(assistantId);

  // Walk up from the assistant's parent to the root, then replay forward.
  std::vector<std::string> path;
  std::string cursor = assistant.parentId;
  while (!cursor.empty()) {
    path.push_back(cursor);
    auto it = chat.messages.find(cursor);
    if (it == chat.messages.end()) break;
    cursor = it->second.parentId;
  }
  std::reverse(path.begin(), path.end());

  Json fileRefs = Json::array();
  std::vector<std::string> messageIds;
  for (const std::string& id : path) {
    const Message& message = chat.messages.at(id);
    if (message.role == "assistant" && message.content.empty()) continue;
    messageIds.push_back(id);

    Json entry{{"role", message.role}};
    bool hasImages = false;
    for (const auto& file : message.files) {
      if (file.value("type", "") == "image") hasImages = true;
      if (file.contains("id") && file.value("type", "") != "image") fileRefs.push_back(file);
    }
    if (hasImages && message.role == "user") {
      Json parts = Json::array();
      parts.push_back(Json{{"type", "text"}, {"text", message.content}});
      for (const auto& file : message.files) {
        if (file.value("type", "") != "image") continue;
        parts.push_back(Json{{"type", "image_url"},
                             {"image_url", Json{{"url", file.value("url", "")}}}});
      }
      entry["content"] = parts;
    } else {
      entry["content"] = message.content;
    }
    messages.push_back(entry);
  }
  // Documents read on this Mac ride along as a system message, fitted to the
  // window and chosen for the latest question when they are too big.
  {
    std::string lastUser;
    for (auto it = messageIds.rbegin(); it != messageIds.rend(); ++it) {
      const Message& m = chat.messages.at(*it);
      if (m.role == "user") {
        lastUser = m.content;
        break;
      }
    }
    const Json docs = localDocumentMessage(messageIds, model, lastUser);
    if (!docs.is_null()) messages.insert(messages.begin(), docs);
  }
  // Continuing a reply: the text so far closes the list as the assistant's
  // open turn, so the model carries on from it instead of answering afresh.
  if (continueFromId == assistantId && !assistant.content.empty()) {
    // A thinking model needs a closed think block in front of the prefix,
    // or it takes the prefix for its own thoughts and "closes" them with
    // </think> before answering again; Ollama renders the `thinking` field
    // as that block (Open WebUI passes it through).
    Json prefill{{"role", "assistant"}, {"content", assistant.content}};
    prefill["thinking"] = assistant.reasoning.empty() ? " " : assistant.reasoning;
    prefill["reasoning_content"] = prefill["thinking"];
    messages.push_back(prefill);
  }

  // Context strategy: once the branch outgrows the model's window, drop,
  // window or summarise the oldest turns (the continuation prefix, if any,
  // rides along as the newest message and is never cut).
  if (continueFromId == assistantId && !assistant.content.empty()) messageIds.push_back(assistantId);
  const std::string trimmed = applyContextPlan(&messages, messageIds);
  if (!trimmed.empty()) contextTrimNote_ = trimmed;

  // Project mode: the pinned folder's tree and ticked files ride along as a
  // system message so every turn sees the code being worked on.
  const std::string projectBlock = projectContextBlock();
  if (!projectBlock.empty()) {
    messages.insert(messages.begin(), Json{{"role", "system"}, {"content", projectBlock}});
  }

  // Structured output: the schema goes to the backend as `response_format`
  // (Open WebUI hands it to OpenAI-style servers as is and turns it into
  // Ollama's `format`) and is spelled out in a system message for models that
  // only follow the prompt. After a violation the errors ride along too.
  Json responseFormat;
  if (structuredEnabled()) {
    const Json schema = structuredSchema();
    if (!schema.is_null()) {
      responseFormat = Json{{"type", "json_schema"},
                            {"json_schema", Json{{"name", structuredName()}, {"schema", schema}}}};
      std::string note =
          "Reply with a single JSON value that matches this JSON schema, and nothing else - "
          "no prose before or after it, no code fence:\n" + schema.dump(2);
      auto hint = structuredHints.find(assistant.parentId);
      if (hint != structuredHints.end()) {
        note += "\n\nYour previous reply was rejected because: " + hint->second +
                "\nFix every point and reply with JSON only.";
      }
      messages.insert(messages.begin(), Json{{"role", "system"}, {"content", note}});
    }
  }

  Json payload{
      {"stream", settings().streamResponses},
      {"model", model},
      {"messages", messages},
      {"id", assistantId},
      // No session_id: with one the server hands the stream to socket.io and
      // answers this request with a task id instead of the tokens.
      {"background_tasks",
       Json{{"title_generation", false}, {"tags_generation", false}, {"follow_up_generation", false}}},
      {"features", Json{{"web_search", webSearchEnabled},
                        {"image_generation", imageGenerationEnabled},
                        {"code_interpreter", codeInterpreterEnabled},
                        {"memory", false}}},
      {"stream_options", Json{{"include_usage", true}}},
  };
  if (!chat.id.empty()) payload["chat_id"] = chat.id;
  if (!fileRefs.empty() && !retrievalBypass_.count(assistantId)) payload["files"] = fileRefs;   // a re-ask carries its passages inline
  if (!activeToolIds.empty()) payload["tool_ids"] = activeToolIds;
  if (!chat.params.empty()) payload["params"] = chat.params;
  if (!responseFormat.is_null()) payload["response_format"] = responseFormat;
  return payload;
}

void App::startSocket() {
  socket_.setHandler([this](const std::string& name, const Json& payload) {
    // Runs on a URLSession queue; hand it to the UI thread.
    async::post([this, name, payload] { handleSocketEvent(name, payload); });
  });
  socket_.start(api_.server(), api_.token());
  core::debugLog("socket starting against %s", api_.server().c_str());
}

std::shared_ptr<Stream> App::streamFor(const std::string& messageId) {
  for (auto& stream : streams_) {
    if (stream->messageId == messageId) return stream;
  }
  return nullptr;
}

void App::handleSocketEvent(const std::string& name, const Json& payload) {
  core::debugLog("socket event %s: %.200s", name.c_str(), payload.dump().c_str());
  if (name == "events:channel" || name == "channel-events") {
    // Channels pane refreshes itself from this.
    channelEventCounter++;
    return;
  }
  if (name != "events") return;

  api::StreamDelta delta;
  std::string messageId;
  std::string eventChatId;
  if (!api::parseSocketEvent(payload, &delta, &messageId, &eventChatId)) return;

  if (!delta.title.empty() && eventChatId == chat.id) {
    chat.title = delta.title;
    refreshChats();
  }
  if (!delta.tags.is_null() && delta.tags.is_array() && eventChatId == chat.id) {
    chat.tags.clear();
    for (const auto& tag : delta.tags) {
      if (tag.is_string()) chat.tags.push_back(tag);
    }
  }

  std::shared_ptr<Stream> stream = streamFor(messageId);
  if (!stream) return;
  std::lock_guard<std::mutex> lock(stream->mutex);
  stream->started = true;
  stream->content += delta.content;
  stream->reasoning += delta.reasoning;
  if (delta.hasReplace) stream->content = delta.replaceContent;
  if (!delta.statusText.empty()) stream->status = delta.statusText;
  if (!delta.sources.is_null() && !delta.sources.empty()) stream->sources = delta.sources;
  if (!delta.usage.is_null() && !delta.usage.empty()) stream->usage = delta.usage;
  if (delta.done) stream->done = true;
}

// Local index retrieval: searches the on-disk index for the latest user turn
// and puts the hits in front as a system message. Blocks (it embeds the
// query), so it only ever runs on the worker that sends the request.
static void addLocalIndexHits(App* app, Json* payload, const std::string& query) {
  if (query.empty()) return;
  const auto hits = app->localSearch(query, 4);
  if (hits.empty()) return;
  std::string block = "Relevant excerpts from the user's local files:\n\n";
  for (const auto& [path, text] : hits) block += "### " + path + "\n" + text + "\n\n";
  Json& messages = (*payload)["messages"];
  if (messages.is_array()) {
    messages.insert(messages.begin(), Json{{"role", "system"}, {"content", block}});
  }
}

bool App::serverDrivenStream() const {
  if (chat.isPrivate) return false;
  if (mcpEnabled && !mcpToolSpecs().empty()) return false;   // tool loop talks to Ollama directly
  return socket_.connected() && !chat.id.empty();
}

void App::startStream(const std::string& assistantId, const std::string& model) {
  // With MCP tools switched on the reply runs through the tool loop instead,
  // which talks to Ollama directly so the model can call our tools.
  const bool continuing = continueFromId == assistantId;
  if (chat.isPrivate) {
    startPrivateStream(assistantId, model);
    return;
  }
  if (agentMode && !continuing) {
    startAgentStream(assistantId, model);
    return;
  }
  if (mcpEnabled && !continuing) {
    if (!mcpToolSpecs().empty()) {
      startToolStream(assistantId, model);
      return;
    }
    widgets::toastInfo("MCP tools are on but no server is connected - replying without tools");
  }
  auto stream = std::make_shared<Stream>();
  stream->chatId = chat.id;
  stream->messageId = assistantId;
  stream->model = model;
  stream->sessionId = sessionId_;
  streams_.push_back(stream);
  if (continuing) {
    // The reply so far is the prefix every delta extends.
    const Message& existing = chat.messages.at(assistantId);
    stream->content = existing.content;
    stream->reasoning = existing.reasoning;
  }

  Json payload = buildCompletionPayload(assistantId, model);
  if (!contextTrimNote_.empty()) {
    widgets::toastInfo(contextTrimNote_ + " (context)");
    contextTrimNote_.clear();
  }
  const bool useStreaming = settings().streamResponses;
  lastPayload = payload;
  const bool useLocalIndex = localIndexEnabled;
  std::string indexQuery;
  if (useLocalIndex) {
    auto assistant = chat.messages.find(assistantId);
    if (assistant != chat.messages.end()) {
      auto parent = chat.messages.find(assistant->second.parentId);
      if (parent != chat.messages.end()) indexQuery = parent->second.content;
    }
  }
  timings[assistantId] = MessageTiming{core::nowMillis(), 0.0, 0.0};

  // When the socket is up, let the server run the completion as a background
  // task and push tokens over it: that is the path the web UI takes, so tool
  // calls, status events and server-side persistence all behave the same.
  core::debugLog("startStream %s model=%s socket=%d sid=%s", assistantId.c_str(), model.c_str(),
                 socket_.connected() ? 1 : 0, socket_.sessionId().c_str());
  // A continuation goes over HTTP: as a server task the message would be
  // restarted from its new tokens and the prefix lost.
  if (socket_.connected() && !chat.id.empty() && !continuing) {
    stream->viaSocket = true;
    payload["session_id"] = socket_.sessionId();
    const std::shared_ptr<Stream> handle = stream;
    async::run([this, handle, payload, useLocalIndex, indexQuery] {
      Json body = payload;
      if (useLocalIndex) addLocalIndexHits(this, &body, indexQuery);
      api::Result res = api_.completion(body);
      async::post([this, handle, res] {
        if (!res.ok) {
          std::lock_guard<std::mutex> lock(handle->mutex);
          handle->error = api::errorMessage(res);
          handle->done = true;
          return;
        }
        if (res.data.is_object() && res.data.contains("task_ids") &&
            res.data["task_ids"].is_array()) {
          for (const auto& taskId : res.data["task_ids"]) {
            if (taskId.is_string()) handle->taskIds.push_back(taskId);
          }
        }
      });
    });
    return;
  }

  // Without a live socket the reply has to come back over HTTP, and a chat_id
  // in the payload makes the server run it as a background task whose tokens
  // go to a socket that isn't there - so drop it and save the result ourselves.
  payload.erase("chat_id");

  async::runDetached([this, stream, payload, useStreaming, useLocalIndex, indexQuery] {
    Json body = payload;
    if (useLocalIndex) addLocalIndexHits(this, &body, indexQuery);
    if (useStreaming) {
      api::Result res = api_.streamCompletion(
          body,
          [stream](const api::StreamDelta& delta) {
            std::lock_guard<std::mutex> lock(stream->mutex);
            stream->started = true;
            stream->content += delta.content;
            stream->reasoning += delta.reasoning;
            if (!delta.statusText.empty()) stream->status = delta.statusText;
            if (!delta.sources.is_null() && !delta.sources.empty()) stream->sources = delta.sources;
            if (!delta.usage.is_null() && !delta.usage.empty()) stream->usage = delta.usage;
            return !stream->cancel.load();
          },
          &stream->cancel);
      std::lock_guard<std::mutex> lock(stream->mutex);
      if (!res.ok && !stream->cancel.load()) stream->error = api::errorMessage(res);
      stream->done = true;
    } else {
      api::Result res = api_.completion(body);
      std::lock_guard<std::mutex> lock(stream->mutex);
      if (res.ok) {
        const Json& choices = res.data.value("choices", Json::array());
        if (!choices.empty()) {
          stream->content += choices[0].value("message", Json::object()).value("content", "");
        }
        if (res.data.contains("usage")) stream->usage = res.data["usage"];
      } else {
        stream->error = api::errorMessage(res);
      }
      stream->done = true;
    }
  });
}

void App::sendMessage(const std::string& textIn) {
  if (!sendingExpanded_ && interceptPromptVariables(textIn)) return;
  // Plugins' before-send hooks see the final text (variables already filled).
  const std::string text = plugins::Host::instance().beforeSend(textIn, pluginSnapshot());
  if (core::trim(text).empty() && attachments.empty()) return;
  if (!sendingRedacted_ && interceptRedaction(text)) return;
  if (!sendingOverBudget_ && !sendingRedacted_ && interceptBudget(text)) return;
  if (!serverReachable && attachments.empty()) {
    offlineQueue.push_back(text);
    composer.clear();
    widgets::toastInfo("Offline - queued (" + std::to_string(offlineQueue.size()) + " waiting)");
    return;
  }
  // Auto-routing: short, code-free questions go to the small model for this
  // one turn; the user's selection is left alone.
  std::vector<std::string> savedSelection;
  const std::string routed = routeModel(text);
  if (!routed.empty() && selectedModels.size() == 1 && selectedModels.front() != routed) {
    savedSelection = selectedModels;
    selectedModels = {routed};
    routedModelNote = "Routed to " + routed + " (short question)";
  } else {
    routedModelNote.clear();
  }
  struct RestoreSelection {
    App* app;
    std::vector<std::string> saved;
    ~RestoreSelection() {
      if (!saved.empty()) app->selectedModels = saved;
    }
  } restore{this, savedSelection};
  if (selectedModels.empty()) {
    widgets::toastError("Pick a model first");
    return;
  }
  for (const Attachment& attachment : attachments) {
    if (attachment.uploading) {
      widgets::toastInfo(attachment.reading ? "Still reading an attachment..." : "Still uploading an attachment...");
      return;
    }
  }

  // A private chat on a direct backend answers with the model picked for that
  // backend, not the server's selection.
  const bool directPrivate = chat.isPrivate && !chat.backendId.empty();
  if (directPrivate && chat.models.empty()) {
    widgets::toastError("Pick a model for this chat's backend first (the button in the header)");
    return;
  }
  const std::vector<std::string> targets = directPrivate ? chat.models : selectedModels;

  Message user;
  user.id = core::uuid();
  user.parentId = chat.currentId;
  user.role = "user";
  user.content = text;
  user.timestamp = core::nowSeconds();
  for (const Attachment& attachment : attachments) user.files.push_back(attachment.toJson());

  if (!user.parentId.empty() && chat.messages.count(user.parentId)) {
    chat.messages[user.parentId].childrenIds.push_back(user.id);
  }
  chat.messages[user.id] = user;
  chat.currentId = user.id;
  if (!directPrivate) chat.models = selectedModels;

  std::vector<std::pair<std::string, std::string>> pending;
  for (const std::string& model : targets) {
    Message assistant;
    assistant.id = core::uuid();
    assistant.parentId = user.id;
    assistant.role = "assistant";
    assistant.model = model;
    assistant.modelName = model;
    for (const ModelInfo& info : models) {
      if (info.id == model) assistant.modelName = info.name;
    }
    assistant.timestamp = core::nowSeconds();
    assistant.streaming = true;
    assistant.serverStream = !chat.id.empty() && serverDrivenStream();
    chat.messages[user.id].childrenIds.push_back(assistant.id);
    chat.messages[assistant.id] = assistant;
    pending.emplace_back(assistant.id, model);
  }
  if (retrievalBypassNext_) {
    for (const auto& [assistantId, model] : pending) retrievalBypass_.insert(assistantId);
    retrievalBypassNext_ = false;
  }
  chat.currentId = pending.front().first;

  attachments.clear();
  composer.clear();
  pushHistory(historyOverride_.empty() ? text : historyOverride_);
  clearDraft(draftKey());
  scrollToBottom = true;
  followTail = true;

  if (chat.id.empty()) {
    // Create the chat server-side first so the id is attached to the run.
    const Json body = chat.toJson();
    async::run([this, body, pending] {
      api::Result res = api_.createChat(body);
      async::post([this, res, pending] {
        if (res.ok && res.data.contains("id")) {
          chat.id = res.data["id"];
          adoptRedactionsForNewChat();
          store::putChat(res.data);
          refreshChats();
        } else {
          widgets::toastError("Could not create chat: " + api::errorMessage(res));
        }
        for (const auto& [assistantId, model] : pending) startStream(assistantId, model);
      });
    });
  } else {
    saveActiveChat([this, pending] {
      for (const auto& [assistantId, model] : pending) {
        if (chat.messages.count(assistantId)) startStream(assistantId, model);
      }
    });
  }
}

void App::regenerate(const std::string& assistantMessageId) {
  auto it = chat.messages.find(assistantMessageId);
  if (it == chat.messages.end()) return;
  const std::string parentId = it->second.parentId;
  std::string model = it->second.model.empty()
                          ? (selectedModels.empty() ? "" : selectedModels.front())
                          : it->second.model;
  const bool overridden = !activeStreamModelOverride_.empty();
  if (overridden) model = activeStreamModelOverride_;
  if (parentId.empty() || model.empty()) return;

  Message assistant;
  assistant.id = core::uuid();
  assistant.parentId = parentId;
  assistant.role = "assistant";
  assistant.model = model;
  assistant.modelName = it->second.modelName;
  if (overridden) {
    assistant.modelName = model;
    for (const ModelInfo& info : models) {
      if (info.id == model) assistant.modelName = info.name;
    }
  }
  assistant.timestamp = core::nowSeconds();
  assistant.streaming = true;
  assistant.serverStream = serverDrivenStream();
  chat.messages[parentId].childrenIds.push_back(assistant.id);
  chat.messages[assistant.id] = assistant;
  chat.currentId = assistant.id;
  scrollToBottom = true;
  followTail = true;
  // The server creates the reply's record itself when the task starts, with
  // no parent; saving the linked placeholder first keeps the thread intact.
  const std::string assistantId = assistant.id;
  saveActiveChat([this, assistantId, model] {
    if (chat.messages.count(assistantId)) startStream(assistantId, model);
  });
}

void App::editAndResend(const std::string& userMessageId, const std::string& newText) {
  auto it = chat.messages.find(userMessageId);
  if (it == chat.messages.end()) return;

  Message edited;
  edited.id = core::uuid();
  edited.parentId = it->second.parentId;
  edited.role = "user";
  edited.content = newText;
  edited.files = it->second.files;
  edited.timestamp = core::nowSeconds();

  if (!edited.parentId.empty() && chat.messages.count(edited.parentId)) {
    chat.messages[edited.parentId].childrenIds.push_back(edited.id);
  }
  chat.messages[edited.id] = edited;
  chat.currentId = edited.id;

  std::vector<std::pair<std::string, std::string>> pending;
  for (const std::string& model : (chat.models.empty() ? selectedModels : chat.models)) {
    Message assistant;
    assistant.id = core::uuid();
    assistant.parentId = edited.id;
    assistant.role = "assistant";
    assistant.model = model;
    assistant.modelName = model;
    assistant.timestamp = core::nowSeconds();
    assistant.streaming = true;
    chat.messages[edited.id].childrenIds.push_back(assistant.id);
    chat.messages[assistant.id] = assistant;
    pending.emplace_back(assistant.id, model);
  }
  if (!pending.empty()) chat.currentId = pending.front().first;
  scrollToBottom = true;
  followTail = true;
  for (const auto& [assistantId, model] : pending) startStream(assistantId, model);
}

void App::stopAllStreams() {
  for (auto& stream : streams_) {
    stream->cancel.store(true);
    std::vector<std::string> taskIds = stream->taskIds;
    if (taskIds.empty()) taskIds.push_back(stream->messageId);
    async::run([this, taskIds] {
      for (const std::string& taskId : taskIds) api_.stopTask(taskId);
    });
  }
  for (auto& [id, message] : chat.messages) message.streaming = false;
  streams_.clear();
  clearInflight();
}

void App::pollStreams() {
  if (streams_.empty()) return;
  saveInflight();
  std::vector<std::shared_ptr<Stream>> keep;
  for (auto& stream : streams_) {
    bool finished = false;
    {
      std::lock_guard<std::mutex> lock(stream->mutex);
      auto it = chat.messages.find(stream->messageId);
      if (it != chat.messages.end()) {
        Message& message = it->second;
        if (message.content != stream->content) {
          if (message.content.empty() && timings.count(stream->messageId) &&
              timings[stream->messageId].firstTokenMs == 0.0) {
            timings[stream->messageId].firstTokenMs = core::nowMillis();
          }
          message.content = stream->content;
          if (followTail) scrollToBottom = true;
          else newTextPending = true;
        }
        message.reasoning = stream->reasoning;
        message.status = stream->status;
        if (!stream->sources.empty()) message.sources = stream->sources;
        if (!stream->usage.empty()) message.usage = stream->usage;
        if (stream->agentRun.is_object()) message.agentRun = stream->agentRun;
        // Tool calls: mirror the worker's list, keeping the cards' expanded state.
        for (size_t i = 0; i < stream->toolCalls.size(); ++i) {
          if (i < message.toolCalls.size()) {
            const bool expanded = message.toolCalls[i].expanded;
            message.toolCalls[i] = stream->toolCalls[i];
            message.toolCalls[i].expanded = expanded;
          } else {
            message.toolCalls.push_back(stream->toolCalls[i]);
          }
        }
        if (stream->done) {
          message.streaming = false;
          message.status.clear();
          if (!stream->error.empty()) message.error = stream->error;
          if (stream->error.empty() && stream->content.empty() && stream->reasoning.empty() &&
              !stream->cancel.load()) {
            message.error = "No response came back from the server. Try regenerating.";
          }
          if (timings.count(stream->messageId)) timings[stream->messageId].doneMs = core::nowMillis();
        }
      }
      finished = stream->done;
    }
    if (finished) {
      const std::string err = core::lower(stream->error);
      const bool transport = !err.empty() && stream->content.empty() &&
                             (err.find("connect") != std::string::npos ||
                              err.find("resolve") != std::string::npos ||
                              err.find("timeout") != std::string::npos ||
                              err.find("timed out") != std::string::npos ||
                              err.find("network") != std::string::npos);
      auto assistant = chat.messages.find(stream->messageId);
      if (transport && assistant != chat.messages.end()) {
        // The server is unreachable: take the turn back out of the chat and
        // queue its text, so it goes out once the server returns instead of
        // leaving a dead exchange behind.
        const std::string userId = assistant->second.parentId;
        auto user = chat.messages.find(userId);
        if (user != chat.messages.end()) {
          offlineQueue.push_back(user->second.content);
          const std::string grandparent = user->second.parentId;
          if (!grandparent.empty() && chat.messages.count(grandparent)) {
            auto& siblings = chat.messages[grandparent].childrenIds;
            siblings.erase(std::remove(siblings.begin(), siblings.end(), userId), siblings.end());
          }
          chat.messages.erase(userId);
          chat.currentId = grandparent;
        }
        chat.messages.erase(stream->messageId);
        serverReachable = false;
        lastReachCheck = 0.0;
        widgets::toastInfo("Server unreachable - message queued and will send when it is back");
        continue;
      }
      afterCompletion(stream->messageId);
    } else {
      keep.push_back(stream);
    }
  }
  streams_.swap(keep);
  if (streams_.size() != keep.size() || streams_.empty()) clearInflight();
}

void App::afterCompletion(const std::string& assistantId) {
  auto it = chat.messages.find(assistantId);
  if (it == chat.messages.end()) return;

  // Structured output: judge the reply now; a violation goes straight back
  // to the model (a sibling reply, so the rejected one stays visible).
  if (structuredEnabled() && it->second.error.empty()) {
    checkStructured(it->second);
    if (it->second.structuredErrors.empty()) {
      structuredHints.erase(it->second.parentId);
      structuredRetries.erase(it->second.parentId);
    } else if (reaskStructured(assistantId)) {
      return;
    }
  }
  const Message& message = it->second;
  if (message.error.empty()) recordUsage(message);

  if (!message.error.empty()) {
    widgets::toastError(message.error);
  } else if (settings().notifyOnCompletion && ImGui::GetIO().AppFocusLost) {
    // Only notify when the window is in the background; the notification's
    // reply field sends straight back into this chat.
    std::string preview = md::toPlainText(message.content);
    if (preview.size() > 180) preview = preview.substr(0, 180) + "...";
    native::notifyWithReply(chat.title.empty() ? "Reply ready" : chat.title, preview, chat.id);
  }
  if (settings().speakReplies && !message.content.empty()) readAloud(assistantId, message.content);

  // A reply with an HTML or SVG block opens the artifact pane on it.
  if (message.error.empty() && settings().extra.value("artifactAutoOpen", true)) {
    std::string code, language;
    if (firstArtifactIn(message.content, &code, &language)) openArtifact(code, language);
  }

  if (chat.isPrivate) {
    // Nothing about this chat goes to the server: save locally, title locally.
    saveActiveChat();
    maybeGeneratePrivateTitle();
    refreshPrivateChats();
    return;
  }
  maybeSummariseContext();
  const std::string chatId = chat.id;
  const std::string model = message.model;
  const Json payload{{"model", model},
                     {"chat_id", chatId},
                     {"id", assistantId},
                     {"session_id", sessionId_},
                     {"messages", Json::array()}};
  async::run([this, payload] { api_.chatCompleted(payload); });

  saveActiveChat();
  maybeGenerateTitle();
  maybeProposeMemories(assistantId);

  if (settings().autoFollowUps && message.error.empty() && !message.content.empty()) {
    Json messages = Json::array();
    for (const std::string& id : chat.branch()) {
      const Message& entry = chat.messages.at(id);
      messages.push_back(Json{{"role", entry.role}, {"content", entry.content}});
    }
    const Json payload{{"model", model}, {"messages", messages}, {"chat_id", chatId}};
    const std::string targetId = assistantId;
    async::run([this, payload, targetId] {
      api::Result res = api_.generateFollowUps(payload);
      if (!res.ok) return;
      std::string text;
      if (res.data.contains("choices") && res.data["choices"].is_array() &&
          !res.data["choices"].empty()) {
        text = res.data["choices"][0].value("message", Json::object()).value("content", "");
      }
      Json parsed = Json::parse(text, nullptr, false);
      Json suggestionList = Json::array();
      if (!parsed.is_discarded() && parsed.is_object() && parsed.contains("follow_ups")) {
        suggestionList = parsed["follow_ups"];
      } else if (!parsed.is_discarded() && parsed.is_array()) {
        suggestionList = parsed;
      }
      if (suggestionList.empty()) return;
      async::post([this, targetId, suggestionList] {
        auto entry = chat.messages.find(targetId);
        if (entry != chat.messages.end()) entry->second.followUps = suggestionList;
      });
    });
  }
}

void App::saveActiveChat(std::function<void()> then) {
  if (chat.isPrivate) {
    savePrivateChat();
    if (then) then();
    return;
  }
  if (chat.id.empty()) {
    if (then) then();
    return;
  }
  const std::string id = chat.id;
  const Json body = chat.toJson();
  for (const auto& [mid, message] : chat.messages) {
    core::debugLog("save %s: %s %s len=%zu streaming=%d tools=%zu", id.substr(0, 8).c_str(),
                   message.role.c_str(), mid.substr(0, 8).c_str(), message.content.size(),
                   message.streaming ? 1 : 0, message.toolCalls.size());
  }
  const int64_t base = chat.updatedAt;
  async::run([this, id, body, base, then] {
    api::Result res = api_.updateChat(id, body);
    if (then) async::post(then);
    if (res.ok) {
      store::putChat(res.data);
      return;
    }
    if (res.status == 0) {
      // Keep the edit locally; syncMirror pushes it once the server is back.
      store::putDirty(id, body, base);
      async::post([] { widgets::toastInfo("Server unreachable - change kept locally and will sync"); });
      return;
    }
    async::post([res] { widgets::toastError("Chat not saved: " + api::errorMessage(res)); });
  });
}

void App::maybeGenerateTitle() {
  if (!settings().autoTitle) return;
  if (chat.id.empty()) return;
  if (chat.title != "New Chat" && !chat.title.empty()) return;

  Json messages = Json::array();
  for (const std::string& id : chat.branch()) {
    const Message& message = chat.messages.at(id);
    messages.push_back(Json{{"role", message.role}, {"content", message.content}});
  }
  if (messages.empty()) return;

  const std::string model = chat.models.empty() ? "" : chat.models.front();
  const Json payload{{"model", model}, {"messages", messages}, {"chat_id", chat.id}};
  const std::string chatId = chat.id;
  async::run([this, payload, chatId] {
    api::Result res = api_.generateTitle(payload);
    if (!res.ok) return;
    std::string title;
    if (res.data.contains("choices") && res.data["choices"].is_array() &&
        !res.data["choices"].empty()) {
      title = res.data["choices"][0].value("message", Json::object()).value("content", "");
    }
    // The task model often answers with a JSON blob.
    Json parsed = Json::parse(title, nullptr, false);
    if (!parsed.is_discarded() && parsed.is_object() && parsed.contains("title")) {
      title = parsed["title"].get<std::string>();
    }
    title = core::trim(title);
    if (title.size() > 80) title = title.substr(0, 80);
    if (title.empty()) return;
    async::post([this, title, chatId] {
      if (chat.id == chatId) {
        chat.title = title;
        saveActiveChat();
      }
      refreshChats();
    });
  });
}

void App::deleteChat(const std::string& id) {
  if (isPrivateChat(id)) {
    deletePrivateChat(id);
    return;
  }
  native::removeChatFromSpotlight(id);
  store::removeChat(id);
  async::run([this, id] {
    api_.deleteChat(id);
    async::post([this, id] {
      if (chat.id == id) newChat();
      refreshChats();
    });
  });
}

bool App::pasteImageFromClipboard() {
  if (!signedIn_ || pane != Pane::Chat || platform::clipboardHasText()) return false;
  const std::string png = platform::clipboardImage();
  core::debugLog("paste image: %zu bytes", png.size());
  if (png.empty()) return false;
  attachBytes("pasted.png", "image/png", png);
  widgets::toast(widgets::ToastLevel::Success, "Image attached");
  return true;
}

void App::rateMessage(const std::string& messageId, int rating) {
  auto it = chat.messages.find(messageId);
  if (it == chat.messages.end()) return;
  it->second.rating = it->second.rating == rating ? 0 : rating;
  const int value = it->second.rating;
  const std::string model = it->second.model;
  const std::string chatId = chat.id;
  saveActiveChat();
  if (value == 0) return;
  Json body{{"type", "rating"},
            {"data", Json{{"rating", value}, {"model_id", model}}},
            {"meta", Json{{"chat_id", chatId}, {"message_id", messageId}}}};
  async::run([this, body] { api_.submitFeedback(body); });
}

// --------------------------------------------------------------------- frame

void App::frame() {
  tickLowPower();
  pollStreams();
  tickTts();
  tickSmartFolders();
  tickOnboarding();
  handleShortcuts();

  // The native composer is a Cocoa view above everything ImGui draws, so it
  // only shows while the chat pane placed it last frame and nothing modal is
  // open over the window.
  {
    const bool overlay = showPalette || showQuickAsk || showContextInspector || showCitation || showPlayground || showCheatSheet || showTour || showWhatsNew ||
                         showProjectFiles ||
                         ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    native::composerSetVisible(composerShownThisFrame && !overlay);
    composerShownThisFrame = false;
    native::artifactSetVisible(artifactShownThisFrame && !overlay);
    artifactShownThisFrame = false;
    native::pdfSetVisible(documentShownThisFrame && !overlay);
    documentShownThisFrame = false;
  }

  // Native hooks need the window to exist, so they go in on the first frame
  // rather than in init().
  static bool nativeInstalled = false;
  if (!nativeInstalled) {
    nativeInstalled = true;
    installNativeHooks();
    projectFolder = settings().extra.value("projectFolder", "");
    if (settings().extra.contains("projectFiles") && settings().extra["projectFiles"].is_array()) {
      projectFiles = settings().extra["projectFiles"].get<std::vector<std::string>>();
    }
    if (settings().theme == "system") applyTheme();
    localIndexEnabled = settings().extra.value("localIndexEnabled", false);
    if (settings().extra.contains("promptHistory") && settings().extra["promptHistory"].is_array()) {
      for (const auto& entry : settings().extra["promptHistory"]) {
        if (entry.is_string()) promptHistory.push_back(entry);
      }
    }
    if (settings().extra.contains("fanoutModels") && settings().extra["fanoutModels"].is_array()) {
      for (const auto& id : settings().extra["fanoutModels"]) {
        if (id.is_string()) fanoutModels.push_back(id);
      }
    }
  }
  if (signedIn_ && !chatsLoading && !inflightChecked_) {
    inflightChecked_ = true;
    recoverInflight();
  }
  tickRecovery();
  if (signedIn_ && !startChatId.empty() && !chatsLoading) {
    // --chat: this window was opened for one conversation.
    const std::string id = startChatId;
    startChatId.clear();
    openChat(id);
  }
  if (signedIn_) {
    tickScheduler();
    tickVoiceMode();
    tickOfflineQueue();
    tickChatIndex();
    tickVaultSync();
    tickHealth();
    tickWarmup();
    if (serverReachable && !deferHeavyWork()) syncMirror(false);

    // The socket drops when the server restarts or the network blips; bring
    // it back with a widening backoff rather than staying on the HTTP path.
    static double nextReconnect = 0.0;
    static double backoff = 2000.0;
    const double now = core::nowMillis();
    if (socket_.connected()) {
      backoff = 2000.0;
      nextReconnect = 0.0;
    } else if (serverReachable) {
      if (nextReconnect == 0.0) {
        nextReconnect = now + backoff;
      } else if (now >= nextReconnect) {
        core::debugLog("socket down, reconnecting (backoff %.0f ms)", backoff);
        socket_.reconnect();
        backoff = std::min(backoff * 2.0, 30000.0);
        nextReconnect = now + backoff;
      }
    }
  }
  {
    static size_t lastBadge = static_cast<size_t>(-1);
    if (streams_.size() != lastBadge) {
      lastBadge = streams_.size();
      native::setDockBadge(lastBadge ? std::to_string(lastBadge) : "");
    }
  }

  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::SetNextWindowViewport(viewport->ID);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::Begin("##root", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                   ImGuiWindowFlags_NoSavedSettings);
  ImGui::PopStyleVar(2);

  if (!signedIn_) {
    drawLogin();
  } else {
    const float railWidth = 56.0f;
    ImGui::BeginChild("##rail", ImVec2(railWidth, 0), ImGuiChildFlags_None);
    {
      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImVec2 min = ImGui::GetWindowPos();
      dl->AddRectFilled(min, ImVec2(min.x + railWidth, min.y + ImGui::GetWindowHeight()),
                        theme::palette().panel);
      ImGui::Dummy(ImVec2(0, 34));
      for (const NavItem& item : kNav) {
        if (item.adminOnly && !isAdmin()) continue;
        ImGui::PushID(item.label);
        const bool active = pane == item.pane;
        ImVec2 pos = ImGui::GetCursorScreenPos();
        const float size = 40.0f;
        ImGui::SetCursorPosX((railWidth - size) * 0.5f);
        pos = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##nav", ImVec2(size, size))) pane = item.pane;
        const bool hovered = ImGui::IsItemHovered();
        if (active || hovered) {
          dl->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size),
                            active ? theme::palette().panelAlt : theme::palette().border, 9.0f);
        }
        icons::draw(dl, item.icon, ImVec2(pos.x + size * 0.5f, pos.y + size * 0.5f), 19.0f,
                    active ? theme::palette().accent : theme::palette().textDim);
        if (hovered) ImGui::SetTooltip("%s", item.label);
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0, 4));
      }
      ImGui::EndChild();
    }
    ImGui::SameLine(0, 0);

    if (pane == Pane::Chat && sidebarVisible) {
      drawSidebar(settings().sidebarWidth);
      ImGui::SameLine(0, 0);
    }

    ImGui::BeginChild("##content", ImVec2(0, 0), ImGuiChildFlags_None);
    switch (pane) {
      case Pane::Chat: drawChatPane(); break;
      case Pane::Notes: drawNotesPane(); break;
      case Pane::Channels: drawChannelsPane(); break;
      case Pane::Workspace: drawWorkspacePane(); break;
      case Pane::Images: drawImagesPane(); break;
      case Pane::Files: drawFilesPane(); break;
      case Pane::Memories: drawMemoriesPane(); break;
      case Pane::Library: drawLibraryPane(); break;
      case Pane::Admin: drawAdminPane(); break;
      case Pane::Console: drawConsolePane(); break;
      case Pane::Settings: drawSettingsPane(); break;
      case Pane::Tools: drawToolsPane(); break;
    }
    ImGui::EndChild();
  }

  ImGui::End();

  if (showModelPicker) drawModelPicker();
  drawModelCompare();
  if (signedIn_) {
    drawPalette();
    drawQuickAsk();
    drawContextInspector();
    drawPlayground();
    drawCheatSheet();
    drawWhatsNew();
    drawTour();
    drawPromptForm();
    drawRedactionSheet();
  drawBudgetSheet();
    drawRecipeInputs();
    tickRecipe();
    tickTriggers();
    pluginsTick();
    drawCitationPopup();
    drawProjectFilesPopup();
  }
  widgets::renderToasts();

  // Periodic refresh of the chat list while idle.
  const double now = core::nowMillis();
  if (signedIn_ && now - lastChatRefresh_ > 60000.0) {
    lastChatRefresh_ = now;
    refreshChats();
  }
}

void App::handleShortcuts() {
  if (!signedIn_ || shortcutCapturing()) return;
  shortcutsApplyMenusOnce();
  ImGuiIO& io = ImGui::GetIO();
  if (shortcutPressed("cheatSheet")) showCheatSheet = !showCheatSheet;
  if (!(io.KeySuper || io.KeyCtrl || io.KeyAlt)) return;   // plain keys belong to the panes

  // Pane jumps and the rest follow the keymap (Settings > Keyboard shortcuts);
  // menu-owned chords usually fire through the menu first, this is the fallback.
  const std::pair<const char*, Pane> jumps[] = {
      {"pane.chat", Pane::Chat},         {"pane.notes", Pane::Notes},     {"pane.channels", Pane::Channels}, {"pane.workspace", Pane::Workspace},
      {"pane.images", Pane::Images},     {"pane.files", Pane::Files},     {"pane.memories", Pane::Memories}, {"pane.library", Pane::Library},
      {"pane.admin", Pane::Admin},       {"pane.console", Pane::Console}, {"pane.settings", Pane::Settings}, {"pane.tools", Pane::Tools},
  };
  for (const auto& [id, target] : jumps) {
    if (shortcutPressed(id)) {
      if (target == Pane::Admin && !isAdmin()) continue;
      pane = target;
    }
  }
  if (shortcutPressed("chat.new")) newChat();
  if (shortcutPressed("view.sidebar")) sidebarVisible = !sidebarVisible;
  if (shortcutPressed("view.reading")) toggleReadingMode();
  if (shortcutPressed("chat.refresh") && pane == Pane::Chat) refreshChats(false);
  if (shortcutPressed("chat.stop")) stopAllStreams();
}

void App::shortcutsApplyMenusOnce() {
  static bool done = false;
  if (done) return;
  done = true;
  shortcutsApplyMenus();
}

void App::drawLogin() {
  const ImVec2 size = ImGui::GetContentRegionAvail();
  const float cardWidth = 420.0f;
  ImGui::SetCursorPos(ImVec2((size.x - cardWidth) * 0.5f, std::max(60.0f, size.y * 0.18f)));
  ImGui::BeginChild("##login", ImVec2(cardWidth, 0), ImGuiChildFlags_AutoResizeY);

  theme::pushScaled(1.9f);
  theme::pushBold();
  widgets::centeredText("Open WebUI");
  theme::pop();
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0, 2));
  ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(theme::palette().textDim));
  widgets::centeredText("Native desktop client");
  ImGui::PopStyleColor();
  ImGui::Dummy(ImVec2(0, 20));

  ImGui::TextUnformatted("Server");
  ImGui::SetNextItemWidth(-FLT_MIN);
  if (ImGui::InputText("##server", &serverInput_)) authError_.clear();

  ImGui::Dummy(ImVec2(0, 6));
  ImGui::TextUnformatted("Email");
  ImGui::SetNextItemWidth(-FLT_MIN);
  ImGui::InputText("##email", &loginEmail_);

  ImGui::Dummy(ImVec2(0, 6));
  ImGui::TextUnformatted("Password");
  ImGui::SetNextItemWidth(-FLT_MIN);
  const bool submitted =
      ImGui::InputText("##password", &loginPassword_,
                       ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);

  ImGui::Dummy(ImVec2(0, 8));
  ImGui::Checkbox("Stay signed in", &settings().rememberSession);

  ImGui::Dummy(ImVec2(0, 12));
  widgets::beginDisabled(connecting_);
  if (widgets::primaryButton(connecting_ ? "Connecting..." : "Sign in", ImVec2(-FLT_MIN, 38)) ||
      submitted) {
    signIn(core::trim(loginEmail_), loginPassword_);
  }
  widgets::endDisabled(connecting_);

  if (!authError_.empty()) {
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(theme::palette().danger));
    ImGui::PushTextWrapPos(cardWidth);
    ImGui::TextUnformatted(authError_.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
  }

  ImGui::Dummy(ImVec2(0, 14));
  if (serverConfig_.contains("name")) {
    widgets::dimText("Connected to %s %s", serverConfig_.value("name", "server").c_str(),
                     serverConfig_.value("version", "").c_str());
  }
  ImGui::EndChild();
}

void App::drawModelPicker() {
  ImGui::OpenPopup("Select models");
  ImGui::SetNextWindowSize(ImVec2(520, 520), ImGuiCond_Appearing);
  if (ImGui::BeginPopupModal("Select models", &showModelPicker)) {
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) showModelPicker = false;
    static std::string filter;
    widgets::searchField("modelfilter", &filter, "Filter models");
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::BeginChild("##modellist", ImVec2(0, -40));
    for (const ModelInfo& info : models) {
      if (!filter.empty() && !core::containsNoCase(info.name, filter) &&
          !core::containsNoCase(info.id, filter)) {
        continue;
      }
      bool selected = std::find(selectedModels.begin(), selectedModels.end(), info.id) !=
                      selectedModels.end();
      ImGui::PushID(info.id.c_str());
      if (ImGui::Checkbox("##sel", &selected)) {
        if (selected) {
          selectedModels.push_back(info.id);
        } else {
          selectedModels.erase(
              std::remove(selectedModels.begin(), selectedModels.end(), info.id),
              selectedModels.end());
        }
        settings().selectedModels = selectedModels;
        settings().save();
      }
      ImGui::SameLine();
      ImGui::TextUnformatted(info.name.c_str());
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) drawModelCardTooltip(info.id);
      if (!info.description.empty()) {
        ImGui::SameLine();
        widgets::dimText("- %s", info.description.c_str());
      }
      ImGui::PopID();
    }
    ImGui::EndChild();
    if (widgets::ghostButton("Compare models...")) openModelCompare();
    ImGui::SameLine();
    if (widgets::primaryButton("Done", ImVec2(-FLT_MIN, 32))) showModelPicker = false;
    ImGui::EndPopup();
  }
  if (!showModelPicker) ImGui::CloseCurrentPopup();
}

}  // namespace app
