#include "api/client.h"

#include "core/common.h"

namespace api {
namespace {

Json parseOrNull(const std::string& raw) {
  if (raw.empty()) return Json();
  Json j = Json::parse(raw, nullptr, false);
  if (j.is_discarded()) return Json();
  return j;
}

std::string q(const std::string& s) { return core::urlEncode(s); }

}  // namespace

std::string errorMessage(const Result& res) {
  if (!res.error.empty()) return res.error;
  if (res.data.is_object()) {
    if (res.data.contains("detail")) {
      const Json& d = res.data["detail"];
      if (d.is_string()) return d.get<std::string>();
      return d.dump();
    }
    if (res.data.contains("error")) {
      const Json& d = res.data["error"];
      if (d.is_string()) return d.get<std::string>();
      if (d.is_object() && d.contains("message") && d["message"].is_string())
        return d["message"].get<std::string>();
      return d.dump();
    }
  }
  if (res.status) return "HTTP " + std::to_string(res.status);
  return "request failed";
}

void Client::setServer(const std::string& url) {
  server_ = core::trim(url);
  while (!server_.empty() && server_.back() == '/') server_.pop_back();
  if (!server_.empty() && server_.find("://") == std::string::npos) {
    server_ = "http://" + server_;
  }
}

std::string Client::url(const std::string& path) const {
  if (core::startsWith(path, "http://") || core::startsWith(path, "https://")) return path;
  if (path.empty() || path[0] != '/') return server_ + "/" + path;
  return server_ + path;
}

net::Request Client::makeRequest(const std::string& method, const std::string& path,
                                 const std::string& body, int timeoutSec) const {
  net::Request req;
  req.method = method;
  req.url = url(path);
  req.body = body;
  req.timeoutSec = timeoutSec;
  req.headers.emplace_back("Accept", "application/json");
  if (!token_.empty()) req.headers.emplace_back("Authorization", "Bearer " + token_);
  return req;
}

Result Client::finish(const net::Response& res) {
  Result out;
  out.status = res.status;
  out.raw = res.body;
  out.data = parseOrNull(res.body);
  if (!res.error.empty()) {
    out.error = res.error;
  } else if (res.status < 200 || res.status >= 300) {
    out.error = errorMessage(out);
    if (out.error.empty()) out.error = "HTTP " + std::to_string(res.status);
  } else {
    out.ok = true;
  }
  return out;
}

Result Client::request(const std::string& method, const std::string& path, const Json& body,
                       int timeoutSec) {
  net::Request req = makeRequest(method, path, body.is_null() ? "" : body.dump(), timeoutSec);
  return finish(net::perform(req));
}

Result Client::get(const std::string& path, int timeoutSec) {
  return request("GET", path, Json(), timeoutSec);
}
Result Client::post(const std::string& path, const Json& body, int timeoutSec) {
  return request("POST", path, body, timeoutSec);
}
Result Client::put(const std::string& path, const Json& body) { return request("PUT", path, body); }
Result Client::patch(const std::string& path, const Json& body) {
  return request("PATCH", path, body);
}
Result Client::del(const std::string& path, const Json& body) {
  return request("DELETE", path, body);
}

Result Client::upload(const std::string& path, const std::string& field,
                      const std::string& filename, const std::string& mime,
                      const std::string& bytes,
                      const std::vector<std::pair<std::string, std::string>>& fields) {
  net::Request req = makeRequest("POST", path, "", 600);
  req.files.push_back({field, filename, mime, bytes});
  req.fields = fields;
  return finish(net::perform(req));
}

bool Client::fetchBinary(const std::string& path, std::string* out, std::string* contentType) {
  net::Request req = makeRequest("GET", path, "", 120);
  net::Response res = net::perform(req);
  if (!res.ok()) return false;
  *out = res.body;
  if (contentType) *contentType = res.header("content-type");
  return true;
}

// ---- auth -----------------------------------------------------------------
Result Client::config() { return get("/api/config"); }
Result Client::version() { return get("/api/version"); }
Result Client::changelog() { return get("/api/changelog"); }

Result Client::signin(const std::string& email, const std::string& password) {
  return post("/api/v1/auths/signin", {{"email", email}, {"password", password}});
}
Result Client::signup(const std::string& name, const std::string& email,
                      const std::string& password) {
  return post("/api/v1/auths/signup",
              {{"name", name}, {"email", email}, {"password", password}});
}
Result Client::signout() { return post("/api/v1/auths/signout"); }
Result Client::sessionUser() { return get("/api/v1/auths/"); }
Result Client::updateProfile(const std::string& name, const std::string& profileImageUrl) {
  return post("/api/v1/auths/update/profile",
              {{"name", name}, {"profile_image_url", profileImageUrl}});
}
Result Client::updatePassword(const std::string& oldPassword, const std::string& newPassword) {
  return post("/api/v1/auths/update/password",
              {{"password", oldPassword}, {"new_password", newPassword}});
}
Result Client::getApiKey() { return get("/api/v1/auths/api_key"); }
Result Client::createApiKey() { return post("/api/v1/auths/api_key"); }
Result Client::deleteApiKey() { return del("/api/v1/auths/api_key"); }
Result Client::mfaStatus() { return get("/api/v1/auths/mfa/status"); }
Result Client::mfaVerify(const std::string& code, const std::string& token) {
  return post("/api/v1/auths/mfa/verify", {{"code", code}, {"token", token}});
}

// ---- models ---------------------------------------------------------------
Result Client::models() { return get("/api/models"); }
Result Client::baseModels() { return get("/api/models/base"); }
Result Client::customModels() { return get("/api/v1/models/"); }
Result Client::modelById(const std::string& id) { return get("/api/v1/models/model?id=" + q(id)); }
Result Client::createModel(const Json& model) { return post("/api/v1/models/create", model); }
Result Client::updateModel(const std::string& id, const Json& model) {
  return post("/api/v1/models/model/update?id=" + q(id), model);
}
Result Client::deleteModel(const std::string& id) {
  return post("/api/v1/models/model/delete?id=" + q(id));
}
Result Client::toggleModel(const std::string& id) {
  return post("/api/v1/models/model/toggle?id=" + q(id));
}

// ---- chats ----------------------------------------------------------------
Result Client::chatList(int page) {
  return get("/api/v1/chats/?page=" + std::to_string(page));
}
Result Client::chatListAll() { return get("/api/v1/chats/all"); }
Result Client::chatSearch(const std::string& text, int page) {
  return get("/api/v1/chats/search?text=" + q(text) + "&page=" + std::to_string(page));
}
Result Client::chat(const std::string& id) { return get("/api/v1/chats/" + id); }
Result Client::createChat(const Json& chat) { return post("/api/v1/chats/new", {{"chat", chat}}); }
Result Client::updateChat(const std::string& id, const Json& chat) {
  return post("/api/v1/chats/" + id, {{"chat", chat}});
}
Result Client::deleteChat(const std::string& id) { return del("/api/v1/chats/" + id); }
Result Client::archiveChat(const std::string& id) { return post("/api/v1/chats/" + id + "/archive"); }
Result Client::pinChat(const std::string& id) { return post("/api/v1/chats/" + id + "/pin"); }
Result Client::cloneChat(const std::string& id) { return post("/api/v1/chats/" + id + "/clone"); }
Result Client::forkChat(const std::string& id, const std::string& messageId) {
  return post("/api/v1/chats/" + id + "/fork", {{"message_id", messageId}});
}
Result Client::shareChat(const std::string& id) { return post("/api/v1/chats/" + id + "/share"); }
Result Client::unshareChat(const std::string& id) { return del("/api/v1/chats/" + id + "/share"); }
Result Client::archivedChats() { return get("/api/v1/chats/all/archived"); }
Result Client::pinnedChats() { return get("/api/v1/chats/pinned"); }
Result Client::allTags() { return get("/api/v1/chats/all/tags"); }
Result Client::chatTags(const std::string& id) { return get("/api/v1/chats/" + id + "/tags"); }
Result Client::addChatTag(const std::string& id, const std::string& tag) {
  return post("/api/v1/chats/" + id + "/tags", {{"name", tag}});
}
Result Client::removeChatTag(const std::string& id, const std::string& tag) {
  return del("/api/v1/chats/" + id + "/tags", {{"name", tag}});
}
Result Client::moveChatToFolder(const std::string& id, const std::string& folderId) {
  return post("/api/v1/chats/" + id + "/folder", {{"folder_id", folderId}});
}
Result Client::deleteAllChats() { return del("/api/v1/chats/"); }
Result Client::archiveAllChats() { return post("/api/v1/chats/archive/all"); }
Result Client::chatUsageStats() { return get("/api/v1/chats/stats/usage"); }

// ---- folders --------------------------------------------------------------
Result Client::folders() { return get("/api/v1/folders/"); }
Result Client::createFolder(const std::string& name) {
  return post("/api/v1/folders/", {{"name", name}});
}
Result Client::updateFolder(const std::string& id, const std::string& name) {
  return post("/api/v1/folders/" + id + "/update", {{"name", name}});
}
Result Client::deleteFolder(const std::string& id) { return del("/api/v1/folders/" + id); }

// ---- completions ----------------------------------------------------------
bool parseCompletionChunk(const std::string& data, StreamDelta* out) {
  if (data == "[DONE]") {
    out->done = true;
    return true;
  }
  Json j = Json::parse(data, nullptr, false);
  if (j.is_discarded()) return false;

  if (j.contains("usage") && j["usage"].is_object()) out->usage = j["usage"];
  if (j.contains("sources")) out->sources = j["sources"];
  if (j.contains("citations")) out->sources = j["citations"];

  // Server-side status/tool events arrive on the same stream.
  if (j.contains("event") && j["event"].is_object()) {
    const Json& ev = j["event"];
    if (ev.value("type", "") == "status" && ev.contains("data")) {
      out->statusText = ev["data"].value("description", "");
    }
  }

  if (j.contains("choices") && j["choices"].is_array() && !j["choices"].empty()) {
    const Json& choice = j["choices"][0];
    const Json& delta = choice.contains("delta") ? choice["delta"] : Json::object();
    if (delta.contains("content") && delta["content"].is_string()) {
      out->content = delta["content"].get<std::string>();
    }
    if (delta.contains("reasoning_content") && delta["reasoning_content"].is_string()) {
      out->reasoning = delta["reasoning_content"].get<std::string>();
    } else if (delta.contains("reasoning") && delta["reasoning"].is_string()) {
      out->reasoning = delta["reasoning"].get<std::string>();
    }
    if (delta.contains("tool_calls")) out->toolCalls = delta["tool_calls"];
    if (choice.contains("finish_reason") && !choice["finish_reason"].is_null()) {
      out->done = true;
    }
    // Non-streaming shape, used when the server collapses the response.
    if (choice.contains("message") && choice["message"].is_object()) {
      const Json& msg = choice["message"];
      if (msg.contains("content") && msg["content"].is_string()) {
        out->content = msg["content"].get<std::string>();
      }
    }
  }
  return true;
}

bool parseSocketEvent(const Json& payload, StreamDelta* out, std::string* messageId,
                      std::string* chatId) {
  if (messageId && payload.contains("message_id") && payload["message_id"].is_string()) {
    *messageId = payload["message_id"].get<std::string>();
  }
  if (chatId && payload.contains("chat_id") && payload["chat_id"].is_string()) {
    *chatId = payload["chat_id"].get<std::string>();
  }
  if (!payload.contains("data") || !payload["data"].is_object()) return false;
  const Json& outer = payload["data"];
  const std::string type = outer.value("type", "");
  const Json& data = outer.contains("data") && outer["data"].is_object() ? outer["data"]
                                                                        : Json::object();

  // Responses-API style streaming (Open WebUI 0.12+ over socket.io).
  if (type == "response:completion") {
    const std::string kind = data.value("type", "");
    if (kind == "response.output_text.delta") {
      out->content = data.value("delta", "");
    } else if (kind == "response.reasoning_text.delta") {
      out->reasoning = data.value("delta", "");
    } else if (kind == "response.output_item.done") {
      const Json& item = data.value("item", Json::object());
      if (item.value("type", "") == "message" && item.contains("content") &&
          item["content"].is_array() && !item["content"].empty()) {
        out->replaceContent = item["content"][0].value("text", "");
        out->hasReplace = !out->replaceContent.empty();
      }
    } else {
      return false;
    }
    return true;
  }

  if (type == "chat:completion") {
    if (data.contains("usage") && data["usage"].is_object()) out->usage = data["usage"];
    if (data.contains("sources")) out->sources = data["sources"];
    if (data.contains("content") && data["content"].is_string()) {
      out->replaceContent = data["content"].get<std::string>();
      out->hasReplace = true;
    }
    if (data.contains("choices") && data["choices"].is_array() && !data["choices"].empty()) {
      const Json& delta = data["choices"][0].value("delta", Json::object());
      if (delta.contains("content") && delta["content"].is_string()) {
        out->content = delta["content"].get<std::string>();
      }
    }
    if (data.value("done", false)) out->done = true;
    return true;
  }

  if (type == "status") {
    out->statusText = data.value("description", data.value("status", ""));
    return true;
  }
  if (type == "source" || type == "citation") {
    if (out->sources.is_null()) out->sources = Json::array();
    out->sources.push_back(data);
    return true;
  }
  if (type == "chat:title") {
    out->title = data.is_string() ? data.get<std::string>() : data.value("title", "");
    return !out->title.empty();
  }
  if (type == "chat:tags") {
    out->tags = data;
    return true;
  }
  if (type == "chat:message:follow_ups") {
    out->followUps = data.value("follow_ups", Json::array());
    return true;
  }
  return false;
}

Result Client::streamCompletion(const Json& payload,
                                const std::function<bool(const StreamDelta&)>& onDelta,
                                const std::atomic<bool>* cancel) {
  net::Request req = makeRequest("POST", "/api/chat/completions", payload.dump(), 0);
  req.headers.emplace_back("Accept", "text/event-stream");
  Result out;
  bool sawData = false;
  net::Response res = net::sse(
      req,
      [&](const std::string&, const std::string& data) {
        StreamDelta delta;
        if (!parseCompletionChunk(data, &delta)) return true;
        sawData = true;
        return onDelta(delta);
      },
      cancel);
  out.status = res.status;
  out.raw = res.body;
  if (!res.error.empty() && !sawData) {
    out.error = res.error;
  } else if ((res.status < 200 || res.status >= 300) && res.status != 0) {
    out.data = parseOrNull(res.body);
    out.error = errorMessage(out);
  } else {
    out.ok = true;
  }
  return out;
}

Result Client::completion(const Json& payload) {
  return post("/api/chat/completions", payload, 0);
}
Result Client::chatCompleted(const Json& payload) { return post("/api/chat/completed", payload); }
Result Client::stopTask(const std::string& taskId) {
  return post("/api/tasks/stop/" + taskId);
}
Result Client::generateTitle(const Json& payload) {
  return post("/api/v1/tasks/title/completions", payload);
}
Result Client::generateTags(const Json& payload) {
  return post("/api/v1/tasks/tags/completions", payload);
}
Result Client::generateFollowUps(const Json& payload) {
  return post("/api/v1/tasks/follow_up/completions", payload);
}
Result Client::generateEmoji(const Json& payload) {
  return post("/api/v1/tasks/emoji/completions", payload);
}
Result Client::generateQueries(const Json& payload) {
  return post("/api/v1/tasks/queries/completions", payload);
}
Result Client::generateImagePrompt(const Json& payload) {
  return post("/api/v1/tasks/image_prompt/completions", payload);
}
Result Client::countTokens(const Json& payload) {
  return post("/api/message/count_tokens", payload);
}

// ---- files & knowledge ----------------------------------------------------
Result Client::uploadFile(const std::string& filename, const std::string& mime,
                          const std::string& bytes, bool process) {
  return upload(std::string("/api/v1/files/?process=") + (process ? "true" : "false"), "file",
                filename, mime, bytes);
}
Result Client::files() { return get("/api/v1/files/"); }
Result Client::file(const std::string& id) { return get("/api/v1/files/" + id); }
Result Client::deleteFile(const std::string& id) { return del("/api/v1/files/" + id); }
Result Client::fileProcessStatus(const std::string& id) {
  return get("/api/v1/files/" + id + "/process/status");
}
Result Client::knowledgeList() { return get("/api/v1/knowledge/"); }
Result Client::knowledge(const std::string& id) { return get("/api/v1/knowledge/" + id); }
Result Client::createKnowledge(const std::string& name, const std::string& description) {
  return post("/api/v1/knowledge/create",
              {{"name", name}, {"description", description}, {"data", Json::object()}});
}
Result Client::updateKnowledge(const std::string& id, const Json& body) {
  return post("/api/v1/knowledge/" + id + "/update", body);
}
Result Client::deleteKnowledge(const std::string& id) {
  return del("/api/v1/knowledge/" + id + "/delete");
}
Result Client::addFileToKnowledge(const std::string& knowledgeId, const std::string& fileId) {
  return post("/api/v1/knowledge/" + knowledgeId + "/file/add", {{"file_id", fileId}});
}
Result Client::removeFileFromKnowledge(const std::string& knowledgeId, const std::string& fileId) {
  return post("/api/v1/knowledge/" + knowledgeId + "/file/remove", {{"file_id", fileId}});
}
Result Client::reindexKnowledge() { return post("/api/v1/knowledge/reindex"); }

// ---- retrieval ------------------------------------------------------------
Result Client::retrievalConfig() { return get("/api/v1/retrieval/config"); }
Result Client::processWeb(const std::string& url, const std::string& collectionName) {
  return post("/api/v1/retrieval/process/web",
              {{"url", url}, {"collection_name", collectionName}});
}
Result Client::processYoutube(const std::string& url) {
  return post("/api/v1/retrieval/process/youtube", {{"url", url}});
}
Result Client::webSearch(const std::string& query) {
  return post("/api/v1/retrieval/process/web/search", {{"query", query}}, 300);
}
Result Client::queryDoc(const std::string& collectionName, const std::string& query, int k) {
  return post("/api/v1/retrieval/query/doc",
              {{"collection_name", collectionName}, {"query", query}, {"k", k}});
}

// ---- prompts / tools / functions / skills ---------------------------------
Result Client::prompts() { return get("/api/v1/prompts/list"); }
Result Client::createPrompt(const Json& prompt) { return post("/api/v1/prompts/create", prompt); }
Result Client::updatePrompt(const std::string& id, const Json& prompt) {
  return post("/api/v1/prompts/id/" + q(id) + "/update", prompt);
}
Result Client::deletePrompt(const std::string& id) {
  return del("/api/v1/prompts/id/" + q(id) + "/delete");
}
Result Client::tools() { return get("/api/v1/tools/list"); }
Result Client::toolById(const std::string& id) { return get("/api/v1/tools/id/" + q(id)); }
Result Client::functions() { return get("/api/v1/functions/list"); }
Result Client::toggleFunction(const std::string& id) {
  return post("/api/v1/functions/id/" + q(id) + "/toggle");
}
Result Client::skills() { return get("/api/v1/skills/list"); }
Result Client::toggleSkill(const std::string& id) {
  return post("/api/v1/skills/id/" + q(id) + "/toggle");
}

// ---- memories -------------------------------------------------------------
Result Client::memories() { return get("/api/v1/memories/"); }
Result Client::addMemory(const std::string& content) {
  return post("/api/v1/memories/add", {{"content", content}});
}
Result Client::updateMemory(const std::string& id, const std::string& content) {
  return post("/api/v1/memories/" + id + "/update", {{"content", content}});
}
Result Client::deleteMemory(const std::string& id) { return del("/api/v1/memories/" + id); }
Result Client::queryMemory(const std::string& content, int k) {
  return post("/api/v1/memories/query", {{"content", content}, {"k", k}});
}
Result Client::resetMemories() { return post("/api/v1/memories/reset"); }

// ---- notes ----------------------------------------------------------------
Result Client::notes() { return get("/api/v1/notes/"); }
Result Client::note(const std::string& id) { return get("/api/v1/notes/" + id); }
Result Client::createNote(const Json& note) { return post("/api/v1/notes/create", note); }
Result Client::updateNote(const std::string& id, const Json& note) {
  return post("/api/v1/notes/" + id + "/update", note);
}
Result Client::deleteNote(const std::string& id) { return del("/api/v1/notes/" + id + "/delete"); }
Result Client::pinNote(const std::string& id) { return post("/api/v1/notes/" + id + "/pin"); }

// ---- channels -------------------------------------------------------------
Result Client::channels() { return get("/api/v1/channels/"); }
Result Client::channel(const std::string& id) { return get("/api/v1/channels/" + id); }
Result Client::channelMessages(const std::string& id, int limit) {
  return get("/api/v1/channels/" + id + "/messages?limit=" + std::to_string(limit));
}
Result Client::postChannelMessage(const std::string& id, const std::string& content,
                                  const Json& data) {
  return post("/api/v1/channels/" + id + "/messages/post",
              {{"content", content}, {"data", data}});
}
Result Client::addReaction(const std::string& channelId, const std::string& messageId,
                           const std::string& name) {
  return post("/api/v1/channels/" + channelId + "/messages/" + messageId + "/reactions/add",
              {{"name", name}});
}
Result Client::removeReaction(const std::string& channelId, const std::string& messageId,
                              const std::string& name) {
  return post("/api/v1/channels/" + channelId + "/messages/" + messageId + "/reactions/remove",
              {{"name", name}});
}
Result Client::createChannel(const Json& body) { return post("/api/v1/channels/create", body); }

// ---- images ---------------------------------------------------------------
Result Client::imageConfig() { return get("/api/v1/images/config"); }
Result Client::imageModels() { return get("/api/v1/images/models"); }
Result Client::generateImages(const Json& form) {
  return post("/api/v1/images/generations", form, 900);
}

// ---- audio ----------------------------------------------------------------
Result Client::audioConfig() { return get("/api/v1/audio/config"); }
Result Client::voices() { return get("/api/v1/audio/voices"); }
bool Client::speech(const std::string& text, const std::string& voice, const std::string& model,
                    std::string* outAudio) {
  Json body{{"input", text}};
  if (!voice.empty()) body["voice"] = voice;
  if (!model.empty()) body["model"] = model;
  net::Request req = makeRequest("POST", "/api/v1/audio/speech", body.dump(), 300);
  net::Response res = net::perform(req);
  if (!res.ok()) return false;
  *outAudio = res.body;
  return true;
}
Result Client::transcribe(const std::string& filename, const std::string& mime,
                          const std::string& bytes) {
  return upload("/api/v1/audio/transcriptions", "file", filename, mime, bytes);
}

// ---- users / admin --------------------------------------------------------
Result Client::users(int page) { return get("/api/v1/users/?page=" + std::to_string(page)); }
Result Client::userSettings() { return get("/api/v1/users/user/settings"); }
Result Client::updateUserSettings(const Json& settings) {
  return post("/api/v1/users/user/settings/update", settings);
}
Result Client::updateUser(const std::string& id, const Json& body) {
  return post("/api/v1/users/" + id + "/update", body);
}
Result Client::deleteUser(const std::string& id) { return del("/api/v1/users/" + id); }
Result Client::groups() { return get("/api/v1/groups/"); }
Result Client::permissions() { return get("/api/v1/users/permissions"); }
Result Client::adminConfig() { return get("/api/v1/auths/admin/config"); }
Result Client::updateAdminConfig(const Json& body) {
  return post("/api/v1/auths/admin/config", body);
}
Result Client::connections() { return get("/api/v1/configs/connections"); }
Result Client::updateConnections(const Json& body) {
  return post("/api/v1/configs/connections", body);
}
Result Client::banners() { return get("/api/v1/configs/banners"); }
Result Client::modelDefaults() { return get("/api/v1/configs/models/defaults"); }
Result Client::codeExecutionConfig() { return get("/api/v1/configs/code_execution"); }
Result Client::exportConfig() { return get("/api/v1/configs/export"); }
Result Client::importConfig(const Json& body) { return post("/api/v1/configs/import", body); }

// ---- evaluations / analytics ---------------------------------------------
Result Client::leaderboard() { return get("/api/v1/evaluations/leaderboard"); }
Result Client::feedbacks() { return get("/api/v1/evaluations/feedbacks/list"); }
Result Client::submitFeedback(const Json& body) {
  return post("/api/v1/evaluations/feedback", body);
}
Result Client::analyticsSummary() { return get("/api/v1/analytics/summary"); }
Result Client::analyticsDaily() { return get("/api/v1/analytics/daily"); }
Result Client::analyticsModels() { return get("/api/v1/analytics/models"); }
Result Client::usage() { return get("/api/usage"); }

// ---- automations / calendar / notifications / apps ------------------------
Result Client::automations() { return get("/api/v1/automations/list"); }
Result Client::runAutomation(const std::string& id) {
  return post("/api/v1/automations/" + id + "/run", Json::object(), 600);
}
Result Client::toggleAutomation(const std::string& id) {
  return post("/api/v1/automations/" + id + "/toggle");
}
Result Client::calendars() { return get("/api/v1/calendars/"); }
Result Client::calendarEvents() { return get("/api/v1/calendars/events"); }
Result Client::notificationEvents() { return get("/api/v1/notifications/events"); }
Result Client::apps() { return get("/api/v1/apps/"); }
Result Client::terminals() { return get("/api/v1/terminals/"); }

// ---- ollama passthrough ---------------------------------------------------
Result Client::ollamaTags() { return get("/ollama/api/tags"); }
Result Client::ollamaPs() { return get("/ollama/api/ps"); }
Result Client::ollamaShow(const std::string& model) {
  return post("/ollama/api/show", {{"model", model}});
}
Result Client::ollamaUnload(const std::string& model) {
  return post("/ollama/api/unload", {{"model", model}});
}
Result Client::ollamaDelete(const std::string& model) {
  return del("/ollama/api/delete", {{"model", model}});
}
Result Client::ollamaPull(const std::string& model,
                          const std::function<bool(const Json&)>& onLine,
                          const std::atomic<bool>* cancel) {
  net::Request req =
      makeRequest("POST", "/ollama/api/pull", Json{{"model", model}}.dump(), 0);
  std::string buffer;
  Result out;
  net::Response res = net::stream(
      req,
      [&](const char* data, size_t len) {
        buffer.append(data, len);
        size_t pos;
        while ((pos = buffer.find('\n')) != std::string::npos) {
          std::string line = core::trim(buffer.substr(0, pos));
          buffer.erase(0, pos + 1);
          if (line.empty()) continue;
          Json j = Json::parse(line, nullptr, false);
          if (!j.is_discarded() && !onLine(j)) return false;
        }
        return true;
      },
      cancel);
  out.status = res.status;
  out.ok = res.error.empty();
  out.error = res.error;
  return out;
}

Result Client::ollamaChat(const Json& payload,
                          const std::function<bool(const Json&)>& onLine,
                          const std::atomic<bool>* cancel) {
  Json body = payload;
  body["stream"] = true;
  net::Request req = makeRequest("POST", "/ollama/api/chat", body.dump(), 0);
  std::string buffer;
  Result out;
  net::Response res = net::stream(
      req,
      [&](const char* data, size_t len) {
        buffer.append(data, len);
        size_t pos;
        while ((pos = buffer.find('\n')) != std::string::npos) {
          std::string line = core::trim(buffer.substr(0, pos));
          buffer.erase(0, pos + 1);
          if (line.empty()) continue;
          Json j = Json::parse(line, nullptr, false);
          if (!j.is_discarded() && !onLine(j)) return false;
        }
        return true;
      },
      cancel);
  // An error body may arrive without a trailing newline.
  if (!core::trim(buffer).empty()) {
    Json j = Json::parse(buffer, nullptr, false);
    if (!j.is_discarded()) onLine(j);
  }
  out.status = res.status;
  out.raw = res.body;
  out.ok = res.error.empty() && res.status >= 200 && res.status < 300;
  if (!out.ok) {
    out.error = res.error.empty() ? "HTTP " + std::to_string(res.status) : res.error;
  }
  return out;
}

// ---- utilities ------------------------------------------------------------
Result Client::executeCode(const std::string& code, const std::string& language) {
  return post("/api/v1/utils/code/execute", {{"code", code}, {"language", language}}, 300);
}
Result Client::formatCode(const std::string& code) {
  return post("/api/v1/utils/code/format", {{"code", code}});
}

}  // namespace api
