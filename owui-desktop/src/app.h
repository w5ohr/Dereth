// Application state and the per-frame UI entry point.
//
// The UI is split across several translation units that all operate on this
// one App object: app.cpp (shell, auth, layout), sidebar.cpp (chat list),
// chat_pane.cpp (conversation + composer + streaming), workspace_pane.cpp,
// extras_pane.cpp, admin_pane.cpp and console_pane.cpp.
#pragma once

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "agents/tool_loop.h"
#include "api/client.h"
#include "core/pty.h"
#include "core/settings.h"
#include "core/socketio.h"
#include "imgui.h"
#include "mcp/mcp_client.h"
#include "ui/terminal.h"

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
  Tools,
};

// A saved chat setup: models, parameters, system prompt and toggles.
struct Preset {
  std::string name;
  std::vector<std::string> models;
  Json params = Json::object();
  std::vector<std::string> tools;
  bool webSearch = false;
  bool imageGeneration = false;
  bool codeInterpreter = false;
  Json toJson() const;
  static Preset fromJson(const Json& j);
};

struct ScheduledPrompt {
  std::string id;
  std::string name;
  std::string prompt;
  std::string model;
  int hour = 8;
  int minute = 0;
  bool weekdaysOnly = true;
  bool enabled = true;
  int64_t lastRunDay = 0;     // days since epoch of the last firing
  Json toJson() const;
  static ScheduledPrompt fromJson(const Json& j);
};

struct ServerProfile {
  std::string name;
  std::string url;
  std::string email;
};

struct MessageTiming {
  double sentMs = 0.0;
  double firstTokenMs = 0.0;
  double doneMs = 0.0;
};

// Output of a locally run or linted code block, keyed by block hash.
struct CodeResult {
  std::string output;
  int exitCode = 0;
  bool running = false;
  bool timedOut = false;
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

// One MCP tool call made while answering, shown as a card above the reply and
// saved with the message (as `mcpToolCalls`) so it survives a reload.
struct ToolCall {
  std::string id;
  std::string server;           // server display name
  std::string tool;             // the server's tool name
  std::string qualifiedName;    // what the model called ("<server>__<tool>")
  Json args = Json::object();
  std::string result;
  std::string error;
  bool isError = false;
  int state = 0;                // agents::CallState
  int decision = 0;             // agents::Decision once the user has answered
  double durationMs = 0.0;
  double startedMs = 0.0;
  bool expanded = false;
  Json toJson() const;
  static ToolCall fromJson(const Json& j);
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
  std::vector<ToolCall> toolCalls;
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
  // Tool loop: the worker appends calls here; the UI answers approvals by
  // setting `decision` and signalling approvalCv.
  std::vector<ToolCall> toolCalls;
  std::condition_variable approvalCv;
};

// A renderable code block from a reply (HTML, SVG or Mermaid), shown live in
// the artifact column beside the chat.
struct Artifact {
  std::string messageId;      // empty for an ad-hoc preview
  std::string language;       // html | svg | mermaid
  std::string code;
  std::string title;
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
  void drawToolsPane();
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
  // Edit > Paste lands here first: an image on the pasteboard (and no text)
  // becomes a chat attachment. Returns false to let the text paste through.
  bool pasteImageFromClipboard();

  // --- chat workflow features (features.cpp) ----------------------------
  void drawPalette();
  void drawQuickAsk();
  void openQuickAsk(const std::string& prefill = "");
  void drawContextInspector();
  void drawChatSearchBar(float width);
  void drawPresetMenu();
  void applyPreset(const Preset& preset);
  void saveCurrentAsPreset(const std::string& name);
  void continueReply(const std::string& assistantId);
  void retryWithModel(const std::string& assistantId, const std::string& model);
  void compactChat();
  void togglePin(const std::string& messageId);
  bool isPinned(const std::string& messageId) const;
  void drawPinnedRail(float width);
  int estimateTokens(const std::string& text) const;
  int contextWindowFor(const std::string& modelId) const;
  int currentContextTokens() const;
  void tickScheduler();
  void tickVoiceMode();
  void tickOfflineQueue();
  void handleKeyboardNavigation(const std::vector<std::string>& path);
  void handleOpenUrl(const std::string& url);
  void updateHandoffAndSpotlight();
  void applyTheme();
  void switchProfile(const ServerProfile& profile);
  std::vector<ServerProfile> profiles() const;
  void saveProfiles(const std::vector<ServerProfile>& list);
  std::string routeModel(const std::string& text) const;
  Json slashPromptMatches(const std::string& prefix) const;
  void drawCompareView(const std::vector<std::string>& siblings, float width);
  void exportChat(const std::string& format);   // md | html | pdf
  void installNativeHooks();
  void drawReferenceColumn(float width);

  // --- code tools (code_tools.cpp) --------------------------------------
  void runCodeBlock(const std::string& code, const std::string& language);
  void lintCodeBlock(const std::string& code, const std::string& language);
  void applyCodeBlockToFile(const std::string& code);
  void attachFolderAsContext(const std::string& folder);
  void attachGitContext(const std::string& folder, const std::string& mode);  // diff|staged|log
  void explainBuildError(const std::string& log);
  void setProjectFolder(const std::string& folder);
  std::string projectContextBlock() const;
  void drawProjectFilesPopup();
  void runCommand(const std::string& command);
  CodeResult* codeResultFor(const std::string& key);
  void drawCodeBlockActions(const std::string& code, const std::string& language);

  // --- knowledge tools (knowledge_tools.cpp) ----------------------------
  void syncFolderToKnowledge(const std::string& folder, const std::string& knowledgeId);
  void stopFolderSync();
  void captureUrlToKnowledge(const std::string& url, const std::string& knowledgeId);
  void drawCitationPopup();
  void openCitation(const Json& source, int chunkIndex);
  void indexFolderLocally(const std::string& folder);
  std::vector<std::pair<std::string, std::string>> localSearch(const std::string& query, int k);
  void drawLocalIndexSection(float width);

  // --- MCP servers and the tool loop (agents/mcp_integration.cpp) ----------
  void mcpInit();                 // load servers, connect the enabled ones
  void mcpShutdown();
  std::vector<mcp::ServerConfig> mcpServers() const;
  void saveMcpServers(const std::vector<mcp::ServerConfig>& list);
  void mcpConnect(const std::string& serverId);
  void mcpDisconnect(const std::string& serverId);
  std::shared_ptr<mcp::Client> mcpClient(const std::string& serverId) const;
  int mcpConnectedCount() const;
  std::vector<agents::ToolSpec> mcpToolSpecs() const;
  std::string mcpApprovalFor(const std::string& qualifiedName) const;   // ask | always | never
  void setMcpApproval(const std::string& qualifiedName, const std::string& policy);
  void mcpImportConfig(const std::string& path);
  void drawToolCallCards(Message& message, float width);
  void drawMcpSection(float width);

  // --- local-first mirror (mirror.cpp) -------------------------------------
  // Pushes chats edited offline, then pulls everything newer than the mirror.
  // Throttled to every ten minutes unless `force`.
  void syncMirror(bool force);
  void drawMirrorSection(float width);   // settings: stats, sync, rebuild
  // Fills `chats` from the mirror when the server can't be reached.
  void loadChatsFromMirror(bool archived);

  // --- fan-out (fanout.cpp) -------------------------------------------------
  // One prompt to several models at once; the answers sit side by side until
  // one is kept as the branch the conversation continues from.
  void fanoutSend(const std::string& text);         // uses fanoutModels for this turn only
  void drawFanoutPicker();                           // popup opened from the composer
  bool fanoutUnresolved(const std::string& parentId) const;
  void drawFanoutColumns(const std::vector<std::string>& siblings, float width);
  void keepFanoutAnswer(const std::string& messageId, bool dropOthers);

  // --- terminal pane (terminal_pane.cpp) ------------------------------------
  void drawTerminalSection(float width);
  void startTerminal();                  // (re)spawns the login shell
  std::string terminalSelectionText() const;
  std::string terminalRecentText(int lines) const;

  // --- artifact pane (artifacts.cpp) ---------------------------------------
  static bool isArtifactLanguage(const std::string& language, const std::string& code);
  // First HTML/SVG block of a reply, for auto-opening after a completion.
  static bool firstArtifactIn(const std::string& content, std::string* code, std::string* language);
  void scanArtifacts();                  // refresh `artifacts` from the open branch
  void openArtifact(const std::string& code, const std::string& language);
  void drawArtifactColumn(float width);
  std::string artifactDocument(const Artifact& artifact) const;   // full HTML for the web view

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

  // Feature state.
  bool showPalette = false;
  std::string paletteQuery;
  bool showQuickAsk = false;
  std::string quickAskInput;
  std::string quickAskChatId;
  bool showContextInspector = false;
  Json lastPayload = Json::object();
  bool showChatSearch = false;
  std::string chatSearchQuery;
  int chatSearchIndex = 0;
  bool compareMode = false;
  bool showPins = false;
  std::string focusedMessageId;
  std::map<std::string, MessageTiming> timings;
  bool voiceMode = false;
  double voiceSilenceSince = 0.0;
  bool voiceHeardSpeech = false;
  int voiceState = 0;                     // 0 idle, 1 listening, 2 transcribing, 3 waiting, 4 speaking
  std::vector<std::string> offlineQueue;
  bool serverReachable = true;
  double lastReachCheck = 0.0;
  std::string projectFolder;
  std::vector<std::string> projectFiles;      // ticked files sent with each message
  std::vector<std::string> projectTree;       // cached listing
  bool showProjectFiles = false;
  std::map<std::string, CodeResult> codeResults;
  std::string commandInput;
  std::string commandOutput;
  bool commandRunning = false;
  std::string buildErrorInput;
  std::string referenceChatId;               // read-only chat shown beside the active one
  ChatDoc referenceChat;
  Json citationPopup = Json();
  int citationChunk = 0;
  bool showCitation = false;
  int folderSyncToken = -1;
  std::string folderSyncPath;
  std::string folderSyncKnowledgeId;
  std::string folderSyncStatus;
  std::string localIndexStatus;
  std::string localIndexFolder;
  bool localIndexEnabled = false;
  std::string benchOutput;
  bool benchRunning = false;
  Json benchResults = Json::object();
  std::string routedModelNote;
  std::string lastRoutedModel;
  bool mcpEnabled = false;                    // composer toggle, persisted as extra["mcpEnabled"]
  bool composerShownThisFrame = false;        // set by the chat pane when the native composer is placed
  float composerTextHeight = 46.0f;           // text area height the composer used last frame
  std::vector<std::string> fanoutModels;      // persisted as extra["fanoutModels"]
  bool showFanoutPicker = false;
  // Terminal pane state. Selection endpoints are absolute line numbers
  // (scrollback lines first, then the live screen) and columns.
  std::unique_ptr<core::Pty> terminal;
  term::Screen terminalScreen{80, 24};
  bool terminalFocused = false;
  int terminalScroll = 0;                     // lines scrolled back from the bottom
  bool terminalSelecting = false;
  bool terminalHasSelection = false;
  int terminalSelAnchorLine = 0, terminalSelAnchorCol = 0;
  int terminalSelEndLine = 0, terminalSelEndCol = 0;
  std::string terminalCwd;
  std::string terminalError;
  bool offlineList = false;                   // the sidebar shows the mirror, not the server
  std::string mirrorStatus;
  bool mirrorSyncing = false;
  double lastMirrorSync = 0.0;
  std::vector<Artifact> artifacts;
  bool showArtifacts = false;
  int artifactIndex = 0;
  std::string artifactEdit;                   // editor buffer for the open artifact
  bool artifactEditing = false;
  bool artifactShownThisFrame = false;
  std::string artifactsKey_;                  // branch fingerprint behind `artifacts`
  std::string artifactLoadedKey_;             // what the web view currently shows
  bool navigateFromComposer(int direction);   // Option+Up/Down out of the native composer; false if no messages

 private:
  void drawLogin();
  void handleShortcuts();
  void drawTopBar();
  void drawStatusBar();
  void drawModelPicker();
  void pollStreams();
  void startStream(const std::string& assistantId, const std::string& model);
  void startToolStream(const std::string& assistantId, const std::string& model);
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
  std::string activeStreamModelOverride_;
  std::string sessionId_;
  double lastChatRefresh_ = 0.0;
  // Connected (or connecting) MCP servers by config id. Workers hold their own
  // shared_ptr copies, so a server can be dropped here while a call finishes.
  mutable std::mutex mcpMutex_;
  std::map<std::string, std::shared_ptr<mcp::Client>> mcpClients_;
};

}  // namespace app
