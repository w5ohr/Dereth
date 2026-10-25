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
#include "core/promptvars.h"
#include "core/contextfit.h"
#include "core/redact.h"
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
  Json agentRun;                // agent mode: goal, steps, outcome (saved as `agentRun`)
  bool streaming = false;
  // Streaming through the server (socket path): the placeholder is saved
  // with its parentId up front, since the server fills that record itself.
  bool serverStream = false;
  int rating = 0;               // 1 thumbs up, -1 thumbs down
  bool showReasoning = false;
  bool showRaw = false;
  bool editing = false;
  std::string editBuffer;
  // Structured output: the JSON pulled out of the reply and its verdict
  // against the chat's schema (filled lazily; not saved - re-checked on load).
  Json structuredValue;
  std::vector<std::string> structuredErrors;
  bool structuredChecked = false;
  int structuredView = 0;       // 0 table (or tree when nested), 1 tree, 2 JSON

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
  bool isPrivate = false;       // lives in an encrypted local file, talks to Ollama directly

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
  // Agent loop: the run's goal / steps / status snapshot, and the pause flag
  // the loop polls between steps.
  Json agentRun;
  std::atomic<bool> paused{false};
  bool resumed = false;         // re-attached after a relaunch: polled from the server
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
  void saveActiveChat(std::function<void()> then = {});   // `then` runs on the UI thread afterwards
  bool serverDrivenStream() const;       // will the next reply stream through the server?
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
  // Stop, edit, continue: a stopped (or finished) reply can be edited in
  // place and resumed from its tail - the reply so far goes to the model as
  // the open assistant turn (prefill) and the new tokens extend it.
  void continueReply(const std::string& assistantId);
  void saveEditedReply(const std::string& assistantId, const std::string& text, bool andContinue);
  std::string continueFromId;            // set while startStream builds a continuation
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
  // Multi-file apply: every code block in a reply that names a file, as one
  // transaction with per-file backups; the last apply can be undone.
  int countFileBlocks(const std::string& content) const;
  void applyFileBlocks(const std::string& content);
  void undoLastApply();
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

  // --- composer conveniences (compose_tools.cpp) -----------------------------
  std::string draftKey() const;
  bool hasDraft(const std::string& chatId) const;
  void saveDraft();
  void restoreDraft();
  void clearDraft(const std::string& key);
  void trackDraft();                      // per frame; saves once typing pauses
  void pushHistory(const std::string& text);
  bool recallHistory(int direction);      // -1 older, +1 newer; false = not handled
  bool smartPaste(const std::string& text);   // true when attached as a file instead
  void quoteReply(const Message& message);
  void steer(const std::string& instruction);
  std::vector<std::string> promptHistory;
  int historyIndex = -1;
  std::string historyStash;
  std::string lastSavedDraft;
  double draftChangedAt = 0.0;

  // --- windows (windows.cpp) -------------------------------------------------
  // A second window is a second instance started with --chat (and --mini for
  // a compact window that floats above other apps).
  void openChatInNewWindow(const std::string& chatId, bool mini);
  void toggleFloating();
  void applyWindowMode();                // once the window exists: mini size, float, cascade
  std::string startChatId;               // from --chat; opened once signed in
  bool miniWindow = false;               // --mini
  bool secondaryInstance = false;        // --chat/--mini: no hotkeys, no settings writes
  bool floating = false;

  // --- screen capture (capture.cpp) -----------------------------------------
  // Runs the system capture UI (interactive: drag a region or pick a window;
  // otherwise the whole screen), attaches the shot and its recognised text.
  void captureScreenToChat(bool interactive);

  // --- document column (document.cpp) ---------------------------------------
  // Shows the PDF behind a citation beside the chat with the cited passage
  // highlighted. `page` is 1-based (0 = unknown).
  void openDocument(const std::string& fileId, const std::string& name, int page, const std::string& passage);
  void drawDocumentColumn(float width);

  // --- sampling panel (sampling.cpp) ----------------------------------------
  void drawSamplingPanel();              // the "##chatparams" popup body
  bool hasSamplingParams() const;        // anything set besides the system prompt

  // --- plugins (plugins/plugin_integration.cpp) -----------------------------
  // JavaScript plugins: palette commands, model tools, before-send hooks,
  // code-block renderers; hot-reloaded from the plugins folder.
  void pluginsInit();
  void pluginsShutdown();
  void pluginsReload();
  void pluginsTick();
  Json pluginSnapshot() const;               // the `ctx` a plugin receives
  void drawPluginsSection(float width);
  int pluginWatchToken = -1;
  double pluginReloadAt = 0.0;

  // --- context strategy (context_strategy.cpp) ------------------------------
  // What goes to the model once the chat outgrows its window: drop oldest,
  // sliding window, or a rolling summary; a popover previews the next send.
  Json& contextConfig();                       // chat.raw["desktop_context"]
  std::string contextStrategyKey() const;      // "drop" | "window" | "summary"
  int contextWindowTurns() const;
  int contextBudget() const;
  Json contextSummary() const;                 // {upto, text, tokens} or null
  contextfit::Plan planContext(const Json& messages, const std::vector<std::string>& ids) const;
  std::string applyContextPlan(Json* messages, const std::vector<std::string>& ids);
  void maybeSummariseContext();
  void drawContextPopover();                   // the "##contextplan" popup body
  Json lastContextPlan;
  std::string contextTrimNote_;              // shown as a toast once the send is out
  bool contextSummaryPending_ = false;
  bool contextDefaultTouched_ = false;

  // --- private chats (private_chats.cpp) ------------------------------------
  // "local-" chats never reach the server: encrypted files in support/private,
  // replies straight from Ollama, titles from a local call.
  bool isPrivateChat(const std::string& id) const;
  void newPrivateChat();
  void savePrivateChat();
  void openPrivateChat(const std::string& id);
  void deletePrivateChat(const std::string& id);
  void startPrivateStream(const std::string& assistantId, const std::string& model);
  void maybeGeneratePrivateTitle();
  void refreshPrivateChats();
  void drawPrivateSection();
  std::vector<ChatSummary> privateChats;

  // --- stream recovery (recovery.cpp) ---------------------------------------
  // Replies in flight are snapshotted to support/inflight.json; on launch a
  // server-driven one is re-attached, a client-driven one keeps its partial.
  void saveInflight();
  void clearInflight();
  void recoverInflight();
  void tickRecovery();
  struct PendingResume {
    std::string chatId;
    std::string messageId;
    Json record;
    bool resume = false;
  } pendingResume_;
  double inflightSavedAt = 0.0;
  bool inflightChecked_ = false;

  // --- redaction (redaction.cpp) --------------------------------------------
  // Secrets and personal data in an outgoing prompt are reviewed and masked
  // with placeholders; the mapping stays local so the chat reads normally.
  struct RedactionSheet {
    bool open = false;
    bool justOpened = false;
    bool reveal = false;
    bool enterArmed = false;      // Enter counts only once it was seen released (the send key opened us)
    std::string text;
    std::vector<redact::Finding> findings;
    std::vector<bool> mask;
  } redactionSheet;
  bool interceptRedaction(const std::string& text);   // true when it took the send over
  void sendRedacted(const std::string& text, const std::vector<redact::Finding>& findings,
                    const std::vector<bool>& mask);
  void drawRedactionSheet();
  std::string unredact(const std::string& text) const;   // display form of a message
  void adoptRedactionsForNewChat();
  std::string redactionPolicy(int kind) const;          // "ask" | "always" | "never"
  void setRedactionPolicy(int kind, const std::string& policy);
  bool sendingRedacted_ = false;

  // --- prompt variables (prompt_vars.cpp) -----------------------------------
  // {{clipboard}}, {{selection}}, {{date}}, {{file:path}}, {{input:Label}}...
  // are filled in when a prompt is sent; inputs are asked for in a form first.
  struct PromptForm {
    bool open = false;
    bool justOpened = false;
    bool submit = false;
    std::string text;                         // the template as typed
    std::vector<promptvars::Var> fields;      // one per distinct input label
    std::vector<std::string> values;
  } promptForm;
  std::string lastSelectionText;              // text handed in via Services / Quick Ask
  bool interceptPromptVariables(const std::string& text);   // true when it took the send over
  void sendExpandedPrompt(const promptvars::Expansion& expansion, const std::string& original);
  void drawPromptForm();
  promptvars::Context promptContext() const;
  bool sendingExpanded_ = false;
  std::string historyOverride_;               // what pushHistory records instead of the sent text

  // --- structured output (structured.cpp) -----------------------------------
  // A JSON schema for this chat: the request asks for it, the reply is checked
  // against it, shown as a table or tree, and re-asked on a violation. The
  // configuration is chat.raw["structuredOutput"], saved with the chat.
  Json& structuredConfig();
  bool structuredEnabled() const;
  Json structuredSchema() const;               // parsed; null when off or broken
  std::string structuredName() const;
  void drawStructuredPanel();                  // the "##structured" popup body
  void checkStructured(Message& message);      // extract + validate a finished reply
  bool reaskStructured(const std::string& assistantId);   // true when a retry started
  bool drawStructuredValue(Message& message, float width); // true when it drew the body
  std::map<std::string, int> structuredRetries;           // per user-message id
  std::map<std::string, std::string> structuredHints;     // violations for the retry prompt

  // --- conversation tree (tree_view.cpp) ------------------------------------
  // A map of every branch beside the transcript: click a node to jump, prune
  // the branches the conversation left behind.
  void drawTreeColumn(float width);
  void jumpToNode(const std::string& id);
  void pruneSubtree(const std::string& id, bool quiet = false);   // the node and everything under it
  void pruneDeadBranches();                     // every subtree off the current path

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
  // Smooth streaming: the transcript follows new text only while the reader
  // is at the bottom; scrolling up parks it and a "New text" pill brings it
  // back. Scroll offsets are remembered per chat.
  bool followTail = true;
  bool newTextPending = false;          // text arrived while parked
  float lastScrollY = 0.0f;
  float lastScrollMaxY = 0.0f;
  float pendingScrollRestore = -1.0f;   // scroll offset to apply once the chat has laid out
  int scrollRestoreFrames = 0;
  std::map<std::string, float> scrollMemory;
  void rememberScroll();                // before leaving the open chat
  void restoreScrollFor(const std::string& id);   // after opening one
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
  bool showTree = false;                 // conversation tree column
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
  bool agentMode = false;                     // composer toggle, persisted as extra["agentMode"]
  // --- agent loop (agents/agent_integration.cpp) ---------------------------
  // "Do this until done": plan, act with tools, verify each step, answer.
  void startAgentStream(const std::string& assistantId, const std::string& model);
  void drawAgentCard(Message& message, float width);
  void drawAgentPopover();                    // the "##agentopts" popup body
  agents::LoopConfig baseToolConfig(const std::string& model);
  agents::LoopHooks makeToolHooks(const std::shared_ptr<Stream>& stream,
                                  const std::map<std::string, std::shared_ptr<mcp::Client>>& clients,
                                  const std::map<std::string, agents::ToolSpec>& byName,
                                  bool autoApprove, const Json& approvals);
  bool composerShownThisFrame = false;        // set by the chat pane when the native composer is placed
  float composerTextHeight = 46.0f;           // text area height the composer used last frame
  bool showDocument = false;
  bool documentShownThisFrame = false;
  bool documentLoading = false;
  std::string documentFileId;
  std::string documentName;
  std::string documentPath;                   // cached copy on disk, for "Open in Preview"
  std::string documentError;
  std::string documentPassage;
  int documentPage = 0;                       // page the citation named (1-based), 0 = unknown
  int documentFoundPage = 0;                  // where the passage was actually found
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
