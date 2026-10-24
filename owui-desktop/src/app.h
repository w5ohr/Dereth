// Application state and the per-frame UI entry point.
//
// The UI is split across several translation units that all operate on this
// one App object: app.cpp (shell, auth, layout), sidebar.cpp (chat list),
// chat_pane.cpp (conversation + composer + streaming), workspace_pane.cpp,
// extras_pane.cpp, admin_pane.cpp and console_pane.cpp.
#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "api/client.h"
#include "core/settings.h"
#include "core/socketio.h"
#include "imgui.h"

namespace app {

using Json = nlohmann::json;

enum class Pane {
  Chat,
  Notes,
  Channels,
  Workspace,
  Images,
  Files,
  Memories,
  Admin,
  Console,
  Settings,
};

struct Attachment {
  std::string id;          // server file id once uploaded
  std::string name;
  std::string mime;
  std::string localPath;
  std::string bytes;       // raw contents, kept for image previews
  std::string dataUrl;     // data: URI for images sent inline
  bool isImage = false;
  bool uploading = false;
  bool failed = false;
  Json toJson() const;
};

struct Message {
  std::string id;
  std::string parentId;
  std::string role;             // user | assistant | system
  std::string content;
  std::string reasoning;
  std::string status;           // transient status line while streaming
  std::string error;
  std::string model;
  std::string modelName;
  std::vector<std::string> childrenIds;
  int64_t timestamp = 0;
  Json files = Json::array();
  Json sources = Json::array();
  Json usage = Json::object();
  Json followUps = Json::array();
  bool streaming = false;
  int rating = 0;               // 1 thumbs up, -1 thumbs down
  bool showReasoning = false;
  bool showRaw = false;
  bool editing = false;
  std::string editBuffer;

  Json toJson() const;
  static Message fromJson(const Json& j);
};

struct ChatDoc {
  std::string id;               // empty until the chat is saved server-side
  std::string title = "New Chat";
  std::string folderId;
  std::vector<std::string> models;
  std::map<std::string, Message> messages;
  std::string currentId;        // leaf of the visible branch
  std::vector<std::string> tags;
  bool pinned = false;
  bool archived = false;
  int64_t updatedAt = 0;
  Json params = Json::object();
  Json raw = Json::object();    // server fields we round-trip untouched

  std::vector<std::string> branch() const;
  std::vector<std::string> siblings(const std::string& messageId) const;
  Json toJson() const;
  static ChatDoc fromJson(const Json& chat, const std::string& id);
};

struct ChatSummary {
  std::string id;
  std::string title;
  std::string folderId;
  int64_t updatedAt = 0;
  bool pinned = false;
  bool archived = false;
  std::vector<std::string> tags;
};

// One in-flight assistant response.
struct Stream {
  std::string chatId;
  std::string messageId;
  std::string sessionId;
  std::string model;
  std::vector<std::string> taskIds;   // server-side task handles, for Stop
  bool viaSocket = false;
  std::atomic<bool> cancel{false};
  std::mutex mutex;
  std::string content;
  std::string reasoning;
  std::string status;
  Json sources = Json::array();
  Json usage = Json::object();
  bool done = false;
  bool started = false;
  std::string error;
};

struct ModelInfo {
  std::string id;
  std::string name;
  std::string owner;
  std::string description;
  bool isCustom = false;
  Json raw;
};

class App {
 public:
  static App& instance();

  void init();
  void frame();
  void shutdown();

  api::Client& api() { return api_; }
  core::Settings& settings() { return core::Settings::instance(); }

  // --- shared helpers used across panes ---------------------------------
  void setStatus(const std::string& text) { statusText_ = text; }
  std::string imageUrlFor(const std::string& url) const;
  std::string fetchImageBytes(const std::string& url);
  bool signedIn() const { return signedIn_; }
  bool socketConnected() const { return socket_.connected(); }
  const Json& user() const { return user_; }
  const Json& serverConfig() const { return serverConfig_; }
  bool isAdmin() const;

  // --- pane entry points (defined in their own files) -------------------
  void drawSidebar(float width);
  void drawChatPane();
  void drawNotesPane();
  void drawChannelsPane();
  void drawWorkspacePane();
  void drawImagesPane();
  void drawFilesPane();
  void drawMemoriesPane();
  void drawAdminPane();
  void drawConsolePane();
  void drawSettingsPane();

  // --- chat operations ---------------------------------------------------
  void newChat();
  void openChat(const std::string& id);
  void refreshChats(bool silent = true);
  void refreshModels();
  void sendMessage(const std::string& text);
  void regenerate(const std::string& assistantMessageId);
  void editAndResend(const std::string& userMessageId, const std::string& newText);
  void stopAllStreams();
  void saveActiveChat();
  void deleteChat(const std::string& id);
  void attachFiles(const std::vector<std::string>& paths);
  void attachBytes(const std::string& name, const std::string& mime, const std::string& bytes);
  void speak(const std::string& text);
  void rateMessage(const std::string& messageId, int rating);

  // --- state (panes read and write this directly) -----------------------
  Pane pane = Pane::Chat;
  ChatDoc chat;
  std::vector<ChatSummary> chats;
  std::vector<ChatSummary> archived;
  std::vector<Json> folders;
  std::vector<ModelInfo> models;
  std::vector<std::string> selectedModels;
  std::vector<Attachment> attachments;
  std::string composer;
  std::string chatSearch;
  std::string pendingScrollTo;
  bool chatsLoading = false;
  bool modelsLoading = false;
  bool scrollToBottom = false;
  bool focusComposer = true;
  int composerGeneration = 0;   // bumped on send to reset the input widget
  bool showModelPicker = false;
  bool sidebarVisible = true;
  bool webSearchEnabled = false;
  bool imageGenerationEnabled = false;
  bool codeInterpreterEnabled = false;
  std::vector<std::string> activeToolIds;
  std::vector<Json> suggestions;
  int channelEventCounter = 0;   // bumped by socket channel events

  // Workspace/admin caches, filled lazily by their panes.
  Json knowledgeList = Json::array();
  Json promptList = Json::array();
  Json toolList = Json::array();
  Json functionList = Json::array();
  Json skillList = Json::array();
  Json noteList = Json::array();
  Json channelList = Json::array();
  Json memoryList = Json::array();
  Json fileList = Json::array();
  Json userList = Json::array();
  Json imageConfig = Json::object();
  Json imageModelList = Json::array();
  Json analytics = Json::object();
  Json leaderboard = Json::array();
  Json ollamaStatus = Json::object();
  Json customModelCache = Json::array();

 private:
  void drawLogin();
  void handleShortcuts();
  void drawTopBar();
  void drawStatusBar();
  void drawModelPicker();
  void pollStreams();
  void startStream(const std::string& assistantId, const std::string& model);
  void startSocket();
  void handleSocketEvent(const std::string& name, const Json& payload);
  std::shared_ptr<Stream> streamFor(const std::string& messageId);
  Json buildCompletionPayload(const std::string& assistantId, const std::string& model);
  void afterCompletion(const std::string& assistantId);
  void maybeGenerateTitle();
  void loadSession();
  void signIn(const std::string& email, const std::string& password);
  void signOut();

  api::Client api_;
  Json user_ = Json::object();
  Json serverConfig_ = Json::object();
  bool signedIn_ = false;
  bool connecting_ = false;
  std::string authError_;
  std::string statusText_;
  std::string loginEmail_;
  std::string loginPassword_;
  std::string serverInput_;
  std::vector<std::shared_ptr<Stream>> streams_;
  sio::Client socket_;
  std::string sessionId_;
  double lastChatRefresh_ = 0.0;
};

}  // namespace app
