#include "app.h"

#include <algorithm>
#include <cstdlib>

#include "core/async.h"
#include "core/common.h"
#include "core/keychain.h"
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
    {Pane::Admin, icons::Icon::Wrench, "Admin", true},
    {Pane::Console, icons::Icon::Code, "API Console", false},
    {Pane::Settings, icons::Icon::Gear, "Settings", false},
};

}  // namespace

// ---------------------------------------------------------------- data model

Json Attachment::toJson() const {
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
  Json history{{"currentId", currentId}, {"messages", Json::object()}};
  for (const auto& [id, message] : messages) history["messages"][id] = message.toJson();

  Json linear = Json::array();
  for (const std::string& id : branch()) linear.push_back(messages.at(id).toJson());

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
  if (const char* envServer = std::getenv("OWUI_SERVER")) cfg.serverUrl = envServer;
  api_.setServer(cfg.serverUrl);
  serverInput_ = cfg.serverUrl;
  loginEmail_ = cfg.email;
  sessionId_ = core::uuid();
  selectedModels = cfg.selectedModels;

  // The keychain read is skipped entirely when a token is supplied in the
  // environment, so test runs never trigger a keychain prompt.
  const char* envToken = std::getenv("OWUI_TOKEN");
  std::string token = envToken ? std::string(envToken) : keychain::load(kTokenAccount);
  if (!token.empty()) {
    api_.setToken(token);
    loadSession();
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
}

void App::shutdown() {
  stopAllStreams();
  socket_.stop();
  settings().selectedModels = selectedModels;
  settings().save();
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
        if (settings().rememberSession) keychain::save(kTokenAccount, token);
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
  stopAllStreams();
  chat = ChatDoc();
  chat.models = selectedModels;
  attachments.clear();
  composer.clear();
  scrollToBottom = true;
  focusComposer = true;
  pane = Pane::Chat;
}

void App::openChat(const std::string& id) {
  if (id == chat.id) return;
  stopAllStreams();
  pane = Pane::Chat;
  async::run([this, id] {
    api::Result res = api_.chat(id);
    async::post([this, res, id] {
      if (!res.ok) {
        widgets::toastError("Could not open chat: " + api::errorMessage(res));
        return;
      }
      const Json& body = res.data;
      ChatDoc doc = ChatDoc::fromJson(body.value("chat", Json::object()), id);
      doc.pinned = body.value("pinned", false);
      doc.archived = body.value("archived", false);
      doc.updatedAt = body.value("updated_at", static_cast<int64_t>(0));
      if (body.contains("folder_id") && body["folder_id"].is_string()) {
        doc.folderId = body["folder_id"];
      }
      chat = doc;
      if (!chat.models.empty()) selectedModels = chat.models;
      scrollToBottom = true;
      focusComposer = true;
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
      if (list.ok && list.data.is_array()) {
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

  if (!attachment.isImage) {
    const size_t index = attachments.size() - 1;
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
  for (const std::string& id : path) {
    const Message& message = chat.messages.at(id);
    if (message.role == "assistant" && message.content.empty()) continue;

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
  if (!fileRefs.empty()) payload["files"] = fileRefs;
  if (!activeToolIds.empty()) payload["tool_ids"] = activeToolIds;
  if (!chat.params.empty()) payload["params"] = chat.params;
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

void App::startStream(const std::string& assistantId, const std::string& model) {
  auto stream = std::make_shared<Stream>();
  stream->chatId = chat.id;
  stream->messageId = assistantId;
  stream->model = model;
  stream->sessionId = sessionId_;
  streams_.push_back(stream);

  Json payload = buildCompletionPayload(assistantId, model);
  const bool useStreaming = settings().streamResponses;

  // When the socket is up, let the server run the completion as a background
  // task and push tokens over it: that is the path the web UI takes, so tool
  // calls, status events and server-side persistence all behave the same.
  core::debugLog("startStream %s model=%s socket=%d sid=%s", assistantId.c_str(), model.c_str(),
                 socket_.connected() ? 1 : 0, socket_.sessionId().c_str());
  if (socket_.connected() && !chat.id.empty()) {
    stream->viaSocket = true;
    payload["session_id"] = socket_.sessionId();
    const std::shared_ptr<Stream> handle = stream;
    async::run([this, handle, payload] {
      api::Result res = api_.completion(payload);
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

  async::runDetached([this, stream, payload, useStreaming] {
    if (useStreaming) {
      api::Result res = api_.streamCompletion(
          payload,
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
      api::Result res = api_.completion(payload);
      std::lock_guard<std::mutex> lock(stream->mutex);
      if (res.ok) {
        const Json& choices = res.data.value("choices", Json::array());
        if (!choices.empty()) {
          stream->content = choices[0].value("message", Json::object()).value("content", "");
        }
        if (res.data.contains("usage")) stream->usage = res.data["usage"];
      } else {
        stream->error = api::errorMessage(res);
      }
      stream->done = true;
    }
  });
}

void App::sendMessage(const std::string& text) {
  if (core::trim(text).empty() && attachments.empty()) return;
  if (selectedModels.empty()) {
    widgets::toastError("Pick a model first");
    return;
  }
  for (const Attachment& attachment : attachments) {
    if (attachment.uploading) {
      widgets::toastInfo("Still uploading an attachment...");
      return;
    }
  }

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
  chat.models = selectedModels;

  std::vector<std::pair<std::string, std::string>> pending;
  for (const std::string& model : selectedModels) {
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
    chat.messages[user.id].childrenIds.push_back(assistant.id);
    chat.messages[assistant.id] = assistant;
    pending.emplace_back(assistant.id, model);
  }
  chat.currentId = pending.front().first;

  attachments.clear();
  composer.clear();
  scrollToBottom = true;

  if (chat.id.empty()) {
    // Create the chat server-side first so the id is attached to the run.
    const Json body = chat.toJson();
    async::run([this, body, pending] {
      api::Result res = api_.createChat(body);
      async::post([this, res, pending] {
        if (res.ok && res.data.contains("id")) {
          chat.id = res.data["id"];
          refreshChats();
        } else {
          widgets::toastError("Could not create chat: " + api::errorMessage(res));
        }
        for (const auto& [assistantId, model] : pending) startStream(assistantId, model);
      });
    });
  } else {
    saveActiveChat();
    for (const auto& [assistantId, model] : pending) startStream(assistantId, model);
  }
}

void App::regenerate(const std::string& assistantMessageId) {
  auto it = chat.messages.find(assistantMessageId);
  if (it == chat.messages.end()) return;
  const std::string parentId = it->second.parentId;
  const std::string model = it->second.model.empty()
                                ? (selectedModels.empty() ? "" : selectedModels.front())
                                : it->second.model;
  if (parentId.empty() || model.empty()) return;

  Message assistant;
  assistant.id = core::uuid();
  assistant.parentId = parentId;
  assistant.role = "assistant";
  assistant.model = model;
  assistant.modelName = it->second.modelName;
  assistant.timestamp = core::nowSeconds();
  assistant.streaming = true;
  chat.messages[parentId].childrenIds.push_back(assistant.id);
  chat.messages[assistant.id] = assistant;
  chat.currentId = assistant.id;
  scrollToBottom = true;
  startStream(assistant.id, model);
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
}

void App::pollStreams() {
  if (streams_.empty()) return;
  std::vector<std::shared_ptr<Stream>> keep;
  for (auto& stream : streams_) {
    bool finished = false;
    {
      std::lock_guard<std::mutex> lock(stream->mutex);
      auto it = chat.messages.find(stream->messageId);
      if (it != chat.messages.end()) {
        Message& message = it->second;
        if (message.content != stream->content) {
          message.content = stream->content;
          if (scrollToBottom == false) scrollToBottom = true;
        }
        message.reasoning = stream->reasoning;
        message.status = stream->status;
        if (!stream->sources.empty()) message.sources = stream->sources;
        if (!stream->usage.empty()) message.usage = stream->usage;
        if (stream->done) {
          message.streaming = false;
          message.status.clear();
          if (!stream->error.empty()) message.error = stream->error;
        }
      }
      finished = stream->done;
    }
    if (finished) {
      afterCompletion(stream->messageId);
    } else {
      keep.push_back(stream);
    }
  }
  streams_.swap(keep);
}

void App::afterCompletion(const std::string& assistantId) {
  auto it = chat.messages.find(assistantId);
  if (it == chat.messages.end()) return;
  const Message& message = it->second;

  if (!message.error.empty()) {
    widgets::toastError(message.error);
  } else if (settings().notifyOnCompletion && !ImGui::GetIO().AppFocusLost) {
    // Only notify when the window is in the background.
  }
  if (settings().speakReplies && !message.content.empty()) speak(message.content);

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
}

void App::saveActiveChat() {
  if (chat.id.empty()) return;
  const std::string id = chat.id;
  const Json body = chat.toJson();
  async::run([this, id, body] {
    api::Result res = api_.updateChat(id, body);
    if (!res.ok) {
      async::post([res] {
        widgets::toastError("Chat not saved: " + api::errorMessage(res));
      });
    }
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
  async::run([this, id] {
    api_.deleteChat(id);
    async::post([this, id] {
      if (chat.id == id) newChat();
      refreshChats();
    });
  });
}

void App::speak(const std::string& text) {
  const std::string plain = md::toPlainText(text);
  const std::string voice = settings().ttsVoice;
  async::run([this, plain, voice] {
    std::string audio;
    if (api_.speech(plain, voice, "", &audio) && !audio.empty()) {
      async::post([audio] { platform::playAudio(audio, "mp3"); });
    } else {
      async::post([] { widgets::toastError("Text-to-speech is not configured on the server"); });
    }
  });
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
  pollStreams();
  handleShortcuts();

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
      case Pane::Admin: drawAdminPane(); break;
      case Pane::Console: drawConsolePane(); break;
      case Pane::Settings: drawSettingsPane(); break;
    }
    ImGui::EndChild();
  }

  ImGui::End();

  if (showModelPicker) drawModelPicker();
  widgets::renderToasts();

  // Periodic refresh of the chat list while idle.
  const double now = core::nowMillis();
  if (signedIn_ && now - lastChatRefresh_ > 60000.0) {
    lastChatRefresh_ = now;
    refreshChats();
  }
}

void App::handleShortcuts() {
  if (!signedIn_) return;
  ImGuiIO& io = ImGui::GetIO();
  if (!io.KeySuper) return;

  // Cmd+1..0 jump between panes, matching the order of the rail.
  const std::pair<ImGuiKey, Pane> jumps[] = {
      {ImGuiKey_1, Pane::Chat},     {ImGuiKey_2, Pane::Notes},
      {ImGuiKey_3, Pane::Channels}, {ImGuiKey_4, Pane::Workspace},
      {ImGuiKey_5, Pane::Images},   {ImGuiKey_6, Pane::Files},
      {ImGuiKey_7, Pane::Memories}, {ImGuiKey_8, Pane::Admin},
      {ImGuiKey_9, Pane::Console},  {ImGuiKey_0, Pane::Settings},
  };
  for (const auto& [key, target] : jumps) {
    if (ImGui::IsKeyPressed(key, false)) {
      if (target == Pane::Admin && !isAdmin()) continue;
      pane = target;
    }
  }

  if (ImGui::IsKeyPressed(ImGuiKey_N, false)) newChat();
  if (ImGui::IsKeyPressed(ImGuiKey_B, false)) sidebarVisible = !sidebarVisible;
  if (ImGui::IsKeyPressed(ImGuiKey_R, false) && pane == Pane::Chat) refreshChats(false);
  if (ImGui::IsKeyPressed(ImGuiKey_Period, false)) stopAllStreams();
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
      if (!info.description.empty()) {
        ImGui::SameLine();
        widgets::dimText("- %s", info.description.c_str());
      }
      ImGui::PopID();
    }
    ImGui::EndChild();
    if (widgets::primaryButton("Done", ImVec2(-FLT_MIN, 32))) showModelPicker = false;
    ImGui::EndPopup();
  }
  if (!showModelPicker) ImGui::CloseCurrentPopup();
}

}  // namespace app
