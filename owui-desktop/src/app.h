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
#include <set>

#include "core/accessibility.h"
#include "core/logprobs.h"
#include "core/outputctl.h"
#include "core/pool.h"
#include "core/readingmode.h"
#include "core/rerank.h"
#include "core/retrieval.h"
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "agents/tool_loop.h"
#include "core/backends.h"
#include "core/budgets.h"
#include "api/client.h"
#include "core/pty.h"
#include "core/promptvars.h"
#include "core/contextfit.h"
#include "core/recipes.h"
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
  Library,
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
  // Documents read on this Mac (local_documents.cpp): never uploaded.
  bool reading = false;
  bool local = false;
  std::string hash, kind;
  int pages = 0;
  size_t chars = 0;
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
  Json logprobs = Json::array();   // token log-probabilities, compact (saved as `desktop_logprobs`)
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
  std::string backendId;        // private chats: the direct backend that answers (empty = the Ollama URL)

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
  Json logprobs = Json::array();   // compact token entries as they stream in
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
  void uploadAttachment(size_t index);

  // --- local documents (local_documents.cpp) --------------------------------
  // PDF / Word / PowerPoint / Excel / text attachments extracted on this Mac
  // into pages, sent with the prompt, cited as [name p.N].
  void attachDocument(size_t index);
  Json localDocumentMessage(const std::vector<std::string>& messageIds, const std::string& model, const std::string& query);
  void drawLocalDocumentFile(const Json& file);
  void drawLocalDocumentRefs(const Message& message);
  void openLocalDocument(const std::string& hash, const std::string& name, int page);
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
  void drawBrowserToolCard(float width);   // browser_tool.cpp: the built-in browser tool's card in the MCP tab
  // script_tools.cpp: the user's scripts in support/scripts as a built-in tool server.
  std::vector<agents::ToolSpec> scriptToolSpecs() const;
  static bool isScriptToolServer(const std::string& serverId);
  agents::ToolOutcome callScriptTool(const std::string& name, const Json& args, const std::string& projectFolder);
  void drawScriptToolsCard(float width);

  // --- semantic chat search (chat_index.cpp) --------------------------------
  // Every mirrored chat embedded locally; the sidebar's "By meaning" search.
  void tickChatIndex();
  void indexChatsNow(bool full);
  void drawChatIndexCard(float width);
  void drawSemanticSearchResults(float width);
  void focusMessageInChat(const std::string& messageId);
  bool chatSearchSemantic = false;
  bool semanticSearchDirty = false;
  double semanticSearchAt = 0.0;
  std::string chatIndexStatus;
  std::string pendingFocusMessage;   // focus this message once its chat has opened

  // --- memory proposals (memory_proposals.cpp) ------------------------------
  // Facts about the user a model picked out of a turn; saved only on Save.
  struct MemoryProposal {
    std::string text, edit;
    bool editing = false;
    int state = 0;   // 0 pending, 1 saved, 2 skipped, 3 saving
  };
  std::map<std::string, std::vector<MemoryProposal>> memoryProposals;   // assistant message id -> proposals
  void maybeProposeMemories(const std::string& assistantId);
  void acceptMemoryProposal(const std::string& assistantId, size_t index, const std::string& text);
  void skipMemoryProposal(const std::string& assistantId, size_t index);
  void drawMemoryProposals(const std::string& assistantId);

  // --- vault sync (vault_sync.cpp) ------------------------------------------
  // Chats and notes as Markdown in a folder (an Obsidian vault); notes both ways.
  void tickVaultSync();
  void syncVault(bool force);
  void drawVaultSection(float width);
  std::string vaultStatus;

  // --- health dashboard (health_pane.cpp) -----------------------------------
  void tickHealth();
  void drawHealthSection(float width);
  void healthTabHidden();

  // --- model library (model_library.cpp) ------------------------------------
  // Search the Ollama library / Hugging Face GGUF, queue pulls with progress.
  void drawModelLibrarySection(float width);
  void modelQueuePull(const std::string& name, double bytes);
  void modelPullNext();
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

  // --- recipes (recipes_pane.cpp) -------------------------------------------
  // Saved prompt chains, run step by step in a chat; inputs asked once.
  std::vector<recipes::Recipe> recipeList() const;
  void saveRecipe(const recipes::Recipe& recipe);
  void deleteRecipe(const std::string& id);
  void runRecipe(const std::string& id);
  void runRecipeWith(const std::string& id, const std::map<std::string, std::string>& inputs,
                     const std::vector<std::string>& attachPaths);   // no inputs form; used by triggers
  void startRecipeRun();
  void recipeSendNext();
  void tickRecipe();
  void stopRecipe();
  bool recipeRunning() const;
  void drawRecipeInputs();
  void drawRecipesSection(float width);

  // --- triggers (triggers.cpp) ----------------------------------------------
  // Watched folders and the clipboard watcher run recipes on their own.
  void triggersInit();
  void triggersShutdown();
  void tickTriggers();
  void drawTriggersSection(float width);

  // --- diagrams and math (diagrams.cpp) -------------------------------------
  std::string renderBlockImage(const std::string& kind, const std::string& code, float width);   // worker; PNG or ""

  // --- audit log (audit_pane.cpp) -------------------------------------------
  void auditInit();                           // net observer + log trim
  void auditTool(const std::string& server, const std::string& tool, const std::string& qualified,
                 const std::string& decision);
  void drawAuditSection(float width);

  // --- traffic inspector (traffic_pane.cpp) --------------------------------
  void trafficInit();                         // HTTP + socket observers into the in-memory log
  void drawTrafficSection(float width);
  void consoleLoad(const std::string& method, const std::string& path, const std::string& body);   // console_pane.cpp

  // --- repo map (code_tools.cpp) --------------------------------------------
  // A symbol index of the project folder, sent with the project context in
  // place of whole files (extra["projectRepoMap"], default on).
  void buildRepoMap();                        // (re)indexes on a worker
  std::string repoMapBlock() const;           // rendered, or "" when off / not built
  std::string repoMapStatus;                  // "412 symbols in 38 files (~3.1k tokens)"

  // --- test loop (test_loop.cpp) --------------------------------------------
  // Run the project's tests, have the model fix failures, apply, repeat.
  void testLoopDetect();
  void testLoopRun(bool startLoop);
  void testLoopAskForFix();
  void testLoopApply(const std::string& reply);
  void testLoopStop();
  void drawTestLoopSection(float width);
  void recordTestLoopApply(const std::vector<std::string>& relPaths);   // journal + undo, from code_tools

  // --- backup and import (backup.cpp) ---------------------------------------
  void exportEverything();
  void importChats();
  void drawBackupSection(float width);

  // --- usage dashboard (usage_pane.cpp) -------------------------------------
  void recordUsage(const Message& message);   // one log line per finished reply
  void drawUsageSection(float width);

  // --- git pane (git_pane.cpp) ----------------------------------------------
  // The project folder's repository: status, stage, diff, model-drafted
  // commit messages and PR descriptions, explain-a-diff, blame.
  std::string gitRepo() const;
  void drawGitSection(float width);
  void gitRefresh();
  void gitLoadDiff();
  void gitStage(const std::string& path, bool stage);
  void gitStageAll(bool stage);
  void gitAsk(const std::string& system, const std::string& user, std::function<void(std::string)> done,
              bool keepFences = false);   // keepFences: the reply's code fences carry meaning (file blocks)
  void gitDraftCommitMessage();
  void gitCommit();
  void gitExplainDiff();
  void gitBlame();
  void gitDraftPr();

  // --- model cards (model_cards.cpp) ----------------------------------------
  // Ollama `show` details per model, as tooltips and a comparison table.
  const void* modelCard(const std::string& modelId);      // opaque; fetches on first use
  bool drawModelCardTooltip(const std::string& modelId);
  void openModelCompare();
  void drawModelCompare();

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

  // --- direct backends (backends_pane.cpp) ----------------------------------
  // Ollama, OpenAI-compatible and Anthropic endpoints the app talks to with no
  // server in between; keys in the keychain. Private chats pick one.
  std::vector<backends::Backend> backendList();                 // the Ollama URL first, then the stored ones
  bool findBackend(const std::string& id, backends::Backend* out);
  std::string backendKey(const std::string& id);
  agents::Transport backendTransport(const backends::Backend& backend);
  void refreshBackendModels(const std::string& id, bool force);
  void drawBackendsSection(float width);                        // Settings card
  void drawPrivateBackendButton();                              // header button of a private chat

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

  // --- token budgets (budgets_pane.cpp) -------------------------------------
  struct BudgetSheet {
    bool open = false, justOpened = false;
    std::string text, message;
  } budgetSheet;
  bool sendingOverBudget_ = false;
  bool interceptBudget(const std::string& text);   // true when the send is held behind the modal
  void drawBudgetSheet();
  void drawBudgetPill();
  void drawBudgetsCard(float width);
  budgets::Spend budgetSpend(bool force);

  // --- warmup and residency (warmup_pane.cpp) -------------------------------
  void tickWarmup();
  void warmModelNow(bool release);
  void onSleepWake(bool sleeping);
  void warmupOnQuit();
  void drawWarmupSection(float width);
  std::string warmupModel() const;
  std::string warmupStatus;

  // --- message selection (message_selection.cpp) ----------------------------
  // Shift-click headers (or selection mode's boxes) -> copy / export / move / delete.
  std::set<std::string> selectedMessages;
  std::string selectionAnchor;
  bool selectionMode = false;
  void selectMessage(const std::string& id, bool range);
  void clearSelection();
  std::vector<std::string> selectedInOrder() const;
  void drawSelectionHandle(const std::string& messageId, const ImVec2& pos, float contentWidth);
  void drawSelectionBar(const ImVec2& transcriptMin, const ImVec2& transcriptMax);
  void deleteSelection();
  void moveSelectionToNewChat();

  // --- low-power mode (low_power.cpp) ---------------------------------------
  void tickLowPower();
  bool deferHeavyWork() const;   // on battery with the policy on: indexing, syncs and warmup wait
  bool onBatteryPower() const;
  void drawLowPowerSection(float width);

  // --- editor bridge (editor_bridge.cpp) ------------------------------------
  void openInEditor(const std::string& link);   // "editor://path:line[:col]"
  void drawEditorSection(float width);

  // --- cited export (cited_export.cpp) --------------------------------------
  bool replyHasSources(const Message& message) const;
  std::string citedExport(const Message& message, int format);   // 0 Markdown + footnotes, 1 BibTeX, 2 plain list
  void drawCitedCopyButton(const Message& message);
  void copyLastReplyCited(int format);

  // --- local text-to-speech fallback (local_tts.cpp) -----------------------
  // Replies are read by the server's TTS or, when it has none, this Mac's voices;
  // read-aloud requests queue per message.
  bool readAloud(const std::string& messageId, const std::string& markdown);   // "" id = ad hoc text
  void readLastReplyAloud();
  void readChatAloud();
  void stopReading();
  void stopReadingMessage(const std::string& messageId);
  bool readingMessage(const std::string& messageId) const;   // queued or playing
  bool ttsActive() const;
  std::string readingStatus() const;
  void tickTts();
  void drawReadAloudButton(const std::string& messageId, const std::string& content);
  void drawTtsSection(float width);

  // --- retrieval inspector (retrieval_inspector.cpp) -----------------------
  bool showRetrieval = false;            // side column for retrievalMessageId's chunks
  std::string retrievalMessageId;
  std::set<std::string> retrievalBypass_;   // assistant ids whose request carries no file refs (re-ask)
  bool retrievalBypassNext_ = false;
  void toggleRetrievalColumn(const std::string& messageId);
  retrieval::Marks retrievalMarks(const std::string& messageId) const;   // chat.raw["desktop_retrieval"][id]
  void saveRetrievalMarks(const std::string& messageId, const retrieval::Marks& marks);
  void drawRetrievalButton(const Message& message);
  void drawRetrievalColumn(float width);
  void reaskWithPassages(const std::string& messageId);

  // --- playgrounds (playground_pane.cpp) -----------------------------------
  bool showPlayground = false;
  void openPlayground(const std::string& code, const std::string& language);   // "" code = the sample for that kind
  void runPlayground();
  void drawPlayground();

  // --- model evaluation (eval_pane.cpp) ------------------------------------
  void runEval();
  void drawEvalSection(float width);

  // --- saved answers library (library_pane.cpp) ----------------------------
  bool isInLibrary(const std::string& messageId) const;
  void saveToLibrary(const std::string& messageId);   // toggles
  void saveLastReplyToLibrary();
  void drawLibraryButton(const std::string& messageId);
  void drawLibraryPane();

  // --- smart folders and tags (smart_folders.cpp) --------------------------
  void applySmartFolders(bool manual);   // rules -> folder moves and tags on the server
  void autoTagChats(bool manual);        // the small model tags chats that have none
  void tickSmartFolders();
  void drawSmartFoldersSection(float width);

  // --- shortcut editor (shortcut_editor.cpp) -------------------------------
  bool showCheatSheet = false;
  void shortcutsApplyMenus();                  // native menu key equivalents follow the keymap
  void shortcutsApplyMenusOnce();
  bool shortcutCapturing() const;              // the editor is waiting for keys: no shortcut fires
  bool shortcutPressed(const std::string& id) const;
  std::string shortcutLabel(const std::string& id) const;   // "⌘K", "" when unbound
  bool handleVimKeys(const std::vector<std::string>& path, int index);   // true when a Vim key acted
  void drawShortcutsSection(float width);
  void drawCheatSheet();

  // --- theme editor (theme_editor.cpp) -------------------------------------
  std::string effectiveThemeName() const;               // "system" resolved to dark / light
  std::map<std::string, ImU32> themeOverridesForCurrent() const;
  void drawThemeEditorSection(float width);

  // --- onboarding (onboarding_pane.cpp) ------------------------------------
  bool showTour = false;
  bool showWhatsNew = false;
  void tickOnboarding();                 // first sign-in: the tour, then what's new once per update
  void startTour();
  void showWhatsNewSheet(bool everything);
  void drawTour();
  void drawWhatsNew();
  void drawOnboardingSection(float width);

  // --- reading mode (reading_mode.cpp) -------------------------------------
  readingmode::Prefs readingPrefs() const;
  bool readingMode() const;
  void setReadingMode(bool on);
  void toggleReadingMode();
  void noteMessageRect(const std::string& messageId, const ImVec2& min, const ImVec2& max);
  bool messageControlsVisible(const std::string& messageId) const;   // hover / focus gate in reading mode
  void drawReadingModeSection(float width);

  // --- menu-bar-only mode (menu_bar_only.cpp) -------------------------------
  void setMenuBarOnly(bool on);
  void drawMenuBarOnlySection(float width);

  // --- local reranker (local_rerank.cpp) ------------------------------------
  rerank::Prefs rerankPrefs() const;
  std::string rerankModel() const;
  std::vector<std::pair<std::string, std::string>> rerankLocalHits(const std::string& query, const std::vector<std::pair<std::string, std::string>>& hits);   // blocking
  int rerankScoreFor(const std::string& path, const std::string& text) const;   // -1 when not scored
  std::string rerankSummary() const;
  void drawRerankControls();

  // --- accessibility (accessibility_pane.cpp) -------------------------------
  bool fontsReloadRequested = false;   // main.mm rebuilds the atlas before the next frame
  accessibility::Prefs accessibilityPrefs() const;
  void applyAccessibility();           // reduced motion, font override, scale, then the theme
  void drawAccessibilitySection(float width);

  // --- logprob view (logprob_view.cpp) --------------------------------------
  logprobs::Prefs logprobPrefs() const;
  void applyLogprobRequest(Json& fields);          // logprobs + top_logprobs when enabled
  bool confidenceShown(const std::string& messageId) const;
  void drawConfidenceButton(const Message& message);
  void drawTokenFlow(const Message& message, float width);
  void drawLogprobSettings();

  // --- grammar / tool-choice controls (output_controls.cpp) -----------------
  outputctl::Controls outputControls() const;            // chat.raw["outputControls"]
  void setOutputControls(const outputctl::Controls& c);
  void applyOutputControls(agents::LoopConfig& config);   // format, grammar, tool_choice, nudge
  void drawOutputControlsSection(float width, bool* changed);
  bool openSamplingPanel = false;   // the palette asks the chat header to open ##chatparams

  // --- multi-server pool (backend_pool.cpp) ---------------------------------
  pool::Prefs poolPrefs() const;
  std::vector<pool::Health> poolHealth() const;
  std::string poolLastRoute() const;
  void poolCheckNow();
  void tickBackendPool();
  agents::Transport poolTransport();          // picks a member per request, fails over
  std::vector<std::string> poolModelUnion();  // blocking: every member's models
  void drawBackendPoolSection(float width);

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
