// Typed client for the Open WebUI REST API (v0.12.x).
//
// Every call blocks, so run them on a worker (core/async.h). The generic
// request() at the bottom is the escape hatch the API Console pane uses to
// reach endpoints that have no named wrapper yet.
#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/http.h"

namespace api {

using Json = nlohmann::json;

struct Result {
  bool ok = false;
  long status = 0;
  std::string error;   // transport error or server "detail"
  Json data;           // parsed body when it was JSON
  std::string raw;     // body as received

  const Json& operator*() const { return data; }
  explicit operator bool() const { return ok; }
};

// One streamed chunk of a chat completion.
struct StreamDelta {
  std::string content;        // token text
  std::string reasoning;      // <think> / reasoning_content stream
  std::string statusText;     // tool/status events surfaced by the server
  Json toolCalls;             // raw tool_calls delta
  Json sources;               // citations attached to the message
  Json usage;                 // token accounting, sent on the final chunk
  std::string replaceContent; // authoritative full text (final socket event)
  bool hasReplace = false;
  std::string title;          // server-generated chat title
  Json tags;                  // server-generated chat tags
  Json followUps;
  Json logprobs;              // choice.logprobs.content entries, when the backend sends them
  bool done = false;
};

class Client {
 public:
  void setServer(const std::string& url);
  const std::string& server() const { return server_; }
  void setToken(const std::string& token) { token_ = token; }
  const std::string& token() const { return token_; }
  bool authenticated() const { return !token_.empty(); }
  std::string url(const std::string& path) const;

  // ---- generic -----------------------------------------------------------
  Result request(const std::string& method, const std::string& path, const Json& body = Json(),
                 int timeoutSec = 120);
  Result get(const std::string& path, int timeoutSec = 120);
  Result post(const std::string& path, const Json& body = Json::object(), int timeoutSec = 120);
  Result put(const std::string& path, const Json& body = Json::object());
  Result patch(const std::string& path, const Json& body = Json::object());
  Result del(const std::string& path, const Json& body = Json());
  Result upload(const std::string& path, const std::string& field, const std::string& filename,
                const std::string& mime, const std::string& bytes,
                const std::vector<std::pair<std::string, std::string>>& fields = {});
  // Fetches a binary body (images, audio, attachments).
  bool fetchBinary(const std::string& path, std::string* out, std::string* contentType = nullptr);

  // ---- auth / session ----------------------------------------------------
  Result config();                       // GET /api/config (no auth)
  Result version();
  Result changelog();
  Result signin(const std::string& email, const std::string& password);
  Result signup(const std::string& name, const std::string& email, const std::string& password);
  Result signout();
  Result sessionUser();                  // GET /api/v1/auths/
  Result updateProfile(const std::string& name, const std::string& profileImageUrl);
  Result updatePassword(const std::string& oldPassword, const std::string& newPassword);
  Result getApiKey();
  Result createApiKey();
  Result deleteApiKey();
  Result mfaStatus();
  Result mfaVerify(const std::string& code, const std::string& token);

  // ---- models ------------------------------------------------------------
  Result models();                       // GET /api/models — everything selectable
  Result baseModels();
  Result customModels();                 // GET /api/v1/models/
  Result modelById(const std::string& id);
  Result createModel(const Json& model);
  Result updateModel(const std::string& id, const Json& model);
  Result deleteModel(const std::string& id);
  Result toggleModel(const std::string& id);

  // ---- chats -------------------------------------------------------------
  Result chatList(int page = 1);
  Result chatListAll();
  Result chatSearch(const std::string& text, int page = 1);
  Result chat(const std::string& id);
  Result createChat(const Json& chat);
  Result updateChat(const std::string& id, const Json& chat);
  Result deleteChat(const std::string& id);
  // Removes one message server-side; a chat update alone leaves it in place.
  Result deleteChatMessage(const std::string& id, const std::string& messageId);
  Result archiveChat(const std::string& id);
  Result pinChat(const std::string& id);
  Result cloneChat(const std::string& id);
  Result forkChat(const std::string& id, const std::string& messageId);
  Result shareChat(const std::string& id);
  Result unshareChat(const std::string& id);
  Result archivedChats();
  Result pinnedChats();
  Result allTags();
  Result chatTags(const std::string& id);
  Result addChatTag(const std::string& id, const std::string& tag);
  Result removeChatTag(const std::string& id, const std::string& tag);
  Result moveChatToFolder(const std::string& id, const std::string& folderId);
  Result deleteAllChats();
  Result archiveAllChats();
  Result chatUsageStats();

  // ---- folders -----------------------------------------------------------
  Result folders();
  Result createFolder(const std::string& name);
  Result updateFolder(const std::string& id, const std::string& name);
  Result deleteFolder(const std::string& id);

  // ---- completions -------------------------------------------------------
  // POST /api/chat/completions with stream:true. onDelta runs on the calling
  // (worker) thread; return false from it to stop the generation.
  Result streamCompletion(const Json& payload,
                          const std::function<bool(const StreamDelta&)>& onDelta,
                          const std::atomic<bool>* cancel);
  Result completion(const Json& payload);           // non-streaming
  Result chatCompleted(const Json& payload);        // POST /api/chat/completed
  Result stopTask(const std::string& taskId);
  Result chatTasks(const std::string& chatId);      // GET /api/tasks/chat/{id} -> {task_ids}
  Result generateTitle(const Json& payload);
  Result generateTags(const Json& payload);
  Result generateFollowUps(const Json& payload);
  Result generateEmoji(const Json& payload);
  Result generateQueries(const Json& payload);
  Result generateImagePrompt(const Json& payload);
  Result countTokens(const Json& payload);

  // ---- files & knowledge -------------------------------------------------
  Result uploadFile(const std::string& filename, const std::string& mime,
                    const std::string& bytes, bool process = true);
  Result files();
  Result file(const std::string& id);
  Result deleteFile(const std::string& id);
  Result fileProcessStatus(const std::string& id);
  Result knowledgeList();
  Result knowledge(const std::string& id);
  Result createKnowledge(const std::string& name, const std::string& description);
  Result updateKnowledge(const std::string& id, const Json& body);
  Result deleteKnowledge(const std::string& id);
  Result addFileToKnowledge(const std::string& knowledgeId, const std::string& fileId);
  Result removeFileFromKnowledge(const std::string& knowledgeId, const std::string& fileId);
  Result reindexKnowledge();

  // ---- retrieval ---------------------------------------------------------
  Result retrievalConfig();
  Result processWeb(const std::string& url, const std::string& collectionName = "");
  Result processYoutube(const std::string& url);
  Result webSearch(const std::string& query);
  Result queryDoc(const std::string& collectionName, const std::string& query, int k = 4);

  // ---- prompts / tools / functions / skills ------------------------------
  Result prompts();
  Result createPrompt(const Json& prompt);
  Result updatePrompt(const std::string& command, const Json& prompt);
  Result deletePrompt(const std::string& command);
  Result tools();
  Result toolById(const std::string& id);
  Result functions();
  Result toggleFunction(const std::string& id);
  Result skills();
  Result toggleSkill(const std::string& id);

  // ---- memories ----------------------------------------------------------
  Result memories();
  Result addMemory(const std::string& content);
  Result updateMemory(const std::string& id, const std::string& content);
  Result deleteMemory(const std::string& id);
  Result queryMemory(const std::string& content, int k = 5);
  Result resetMemories();

  // ---- notes -------------------------------------------------------------
  Result notes();
  Result note(const std::string& id);
  Result createNote(const Json& note);
  Result updateNote(const std::string& id, const Json& note);
  Result deleteNote(const std::string& id);
  Result pinNote(const std::string& id);

  // ---- channels ----------------------------------------------------------
  Result channels();
  Result channel(const std::string& id);
  Result channelMessages(const std::string& id, int limit = 50);
  Result postChannelMessage(const std::string& id, const std::string& content,
                            const Json& data = Json::object());
  Result addReaction(const std::string& channelId, const std::string& messageId,
                     const std::string& name);
  Result removeReaction(const std::string& channelId, const std::string& messageId,
                        const std::string& name);
  Result createChannel(const Json& body);

  // ---- images ------------------------------------------------------------
  Result imageConfig();
  Result imageModels();
  Result generateImages(const Json& form);   // {prompt, model, size, n, negative_prompt}

  // ---- audio -------------------------------------------------------------
  Result audioConfig();
  Result voices();
  bool speech(const std::string& text, const std::string& voice, const std::string& model,
              std::string* outAudio);
  Result transcribe(const std::string& filename, const std::string& mime,
                    const std::string& bytes);

  // ---- users / groups / admin -------------------------------------------
  Result users(int page = 1);
  Result userSettings();
  Result updateUserSettings(const Json& settings);
  Result updateUser(const std::string& id, const Json& body);
  Result deleteUser(const std::string& id);
  Result groups();
  Result permissions();
  Result adminConfig();
  Result updateAdminConfig(const Json& body);
  Result connections();
  Result updateConnections(const Json& body);
  Result banners();
  Result modelDefaults();
  Result codeExecutionConfig();
  Result exportConfig();
  Result importConfig(const Json& body);

  // ---- evaluations, analytics, usage ------------------------------------
  Result leaderboard();
  Result feedbacks();
  Result submitFeedback(const Json& body);
  Result analyticsSummary();
  Result analyticsDaily();
  Result analyticsModels();
  Result usage();

  // ---- automations / calendar / notifications / apps / terminals --------
  Result automations();
  Result runAutomation(const std::string& id);
  Result toggleAutomation(const std::string& id);
  Result calendars();
  Result calendarEvents();
  Result notificationEvents();
  Result apps();
  Result terminals();

  // ---- ollama passthrough (server-proxied) -------------------------------
  Result ollamaTags();
  Result ollamaPs();
  Result ollamaShow(const std::string& model);
  Result ollamaUnload(const std::string& model);
  Result ollamaGenerate(const Json& payload);   // POST /ollama/api/generate, non-streaming (warmup: empty prompt + keep_alive)
  Result ollamaDelete(const std::string& model);
  // Pull streams progress lines; onLine gets each JSON object.
  Result ollamaPull(const std::string& model,
                    const std::function<bool(const Json&)>& onLine,
                    const std::atomic<bool>* cancel);
  // POST /ollama/api/chat with stream:true; onLine gets each NDJSON chunk.
  // Used for native tool calling, which the chat pipeline doesn't pass through.
  Result ollamaChat(const Json& payload,
                    const std::function<bool(const Json&)>& onLine,
                    const std::atomic<bool>* cancel);

  // ---- utilities ---------------------------------------------------------
  Result executeCode(const std::string& code, const std::string& language = "python");
  Result formatCode(const std::string& code);

 private:
  net::Request makeRequest(const std::string& method, const std::string& path,
                           const std::string& body, int timeoutSec) const;
  static Result finish(const net::Response& res);

  std::string server_ = "http://localhost:3000";
  std::string token_;
};

// Parses one SSE `data:` payload from /api/chat/completions into a delta.
bool parseCompletionChunk(const std::string& data, StreamDelta* out);

// Parses an `events` payload pushed over socket.io into the same delta shape.
// Returns false for events that carry nothing for the message stream.
bool parseSocketEvent(const Json& payload, StreamDelta* out, std::string* messageId,
                      std::string* chatId);

// Pulls the human-readable text out of an error body.
std::string errorMessage(const Result& res);

}  // namespace api
