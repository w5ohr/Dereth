// Private chats in the app: a conversation whose id starts with "local-"
// lives only in an encrypted file under the support folder, talks to Ollama
// directly (never through the Open WebUI server), gets its title from a
// local model call, and is listed in its own sidebar section. Everything
// else - redaction, prompt variables, plugins, MCP tools, continue, the tree -
// works as in any chat.
#include "app.h"

#include <sys/stat.h>

#include <cstdlib>

#include "agents/tool_loop.h"
#include "core/async.h"
#include "core/common.h"
#include "core/keychain.h"
#include "core/privatestore.h"
#include "native.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

constexpr const char* kKeyAccount = "private-chats-key";

std::string privateDir() { return core::supportDir() + "/private"; }

std::string hex(const std::string& bytes) {
  static const char* digits = "0123456789abcdef";
  std::string out;
  for (unsigned char c : bytes) {
    out += digits[c >> 4];
    out += digits[c & 15];
  }
  return out;
}

std::string unhex(const std::string& text) {
  std::string out;
  if (text.size() % 2) return out;
  for (size_t i = 0; i + 1 < text.size(); i += 2) {
    out += static_cast<char>(std::stoi(text.substr(i, 2), nullptr, 16));
  }
  return out;
}

// The 32-byte key lives in the keychain, made on first use. Where no keychain
// is available (a sandboxed or scratch home), it falls back to a 0600 file
// beside the chats - weaker, and said so once.
std::string privateKey() {
  static std::string cached;
  if (!cached.empty()) return cached;
  const std::string keyFile = privateDir() + "/.key";
  // OWUI_NO_KEYCHAIN=1 (test harnesses with a scratch HOME) goes straight to the file.
  const bool useKeychain = std::getenv("OWUI_NO_KEYCHAIN") == nullptr;
  std::string stored = useKeychain ? keychain::load(kKeyAccount) : std::string();
  if (stored.size() == 64) cached = unhex(stored);
  if (cached.size() != 32) {
    bool ok = false;
    const std::string fromFile = core::readFile(keyFile, &ok);
    if (ok && fromFile.size() == 64) cached = unhex(fromFile);
  }
  if (cached.size() != 32) {
    cached = privatestore::randomBytes(32);
    if (cached.size() == 32 && (!useKeychain || !keychain::save(kKeyAccount, hex(cached)))) {
      mkdir(privateDir().c_str(), 0700);
      core::writeFile(keyFile, hex(cached));
      chmod(keyFile.c_str(), 0600);
      widgets::toastInfo("No keychain available - the private-chat key is kept in a file only you can read");
    }
  }
  return cached;
}

// The folder only exists once a private chat was made.
bool anyPrivateFiles() {
  struct stat st {};
  return stat(privateDir().c_str(), &st) == 0;
}

}  // namespace

bool App::isPrivateChat(const std::string& id) const { return privatestore::isPrivateId(id); }

void App::refreshPrivateChats() {
  privateChats.clear();
  if (!anyPrivateFiles()) return;   // no folder yet: do not touch the keychain at launch
  for (const privatestore::Entry& entry : privatestore::list(privateDir(), privateKey())) {
    ChatSummary summary;
    summary.id = entry.id;
    summary.title = entry.title;
    summary.updatedAt = entry.updatedAt;
    privateChats.push_back(summary);
  }
}

void App::newPrivateChat() {
  if (settings().ollamaUrl.empty()) {
    widgets::toastError("Private chats talk to Ollama directly - set the Ollama URL in Settings first");
    pane = Pane::Settings;
    return;
  }
  newChat();
  chat.isPrivate = true;
  chat.id = privatestore::newId();
  chat.title = "Private chat";
  savePrivateChat();
  refreshPrivateChats();
  widgets::toastInfo("Private chat: stays on this Mac, encrypted, never sent to the server");
}

void App::savePrivateChat() {
  if (!chat.isPrivate || chat.id.empty()) return;
  Json doc = chat.toJson();
  doc["desktop_private"] = true;
  doc["updated_at"] = static_cast<int64_t>(core::nowSeconds());
  chat.updatedAt = doc["updated_at"];
  if (!privatestore::save(privateDir(), privateKey(), chat.id, doc)) {
    widgets::toastError("Could not write the private chat file");
  }
  for (ChatSummary& summary : privateChats) {
    if (summary.id == chat.id) {
      summary.title = chat.title;
      summary.updatedAt = chat.updatedAt;
    }
  }
}

void App::openPrivateChat(const std::string& id) {
  std::optional<Json> doc = privatestore::load(privateDir(), privateKey(), id);
  if (!doc) {
    widgets::toastError("Could not open the private chat (missing or not decryptable with this Mac's key)");
    return;
  }
  stopAllStreams();
  saveDraft();
  pane = Pane::Chat;
  ChatDoc loaded = ChatDoc::fromJson(*doc, id);
  loaded.isPrivate = true;
  loaded.updatedAt = doc->value("updated_at", static_cast<int64_t>(0));
  chat = loaded;
  if (!chat.models.empty()) selectedModels = chat.models;
  composer.clear();
  ++composerGeneration;
  restoreDraft();
  scrollToBottom = true;
  focusComposer = true;
  focusedMessageId.clear();
  native::clearHandoff();
}

void App::deletePrivateChat(const std::string& id) {
  privatestore::remove(privateDir(), id);
  if (chat.id == id) newChat();
  refreshPrivateChats();
}

// A private reply: the tool loop straight to Ollama (empty tool list unless
// MCP tools are on), never the server's completion endpoints.
void App::startPrivateStream(const std::string& assistantId, const std::string& model) {
  const std::string directUrl = settings().ollamaUrl;
  auto it = chat.messages.find(assistantId);
  if (directUrl.empty()) {
    if (it != chat.messages.end()) {
      it->second.streaming = false;
      it->second.error = "Private chats need a direct Ollama URL (Settings) - nothing was sent.";
    }
    return;
  }
  auto stream = std::make_shared<Stream>();
  stream->chatId = chat.id;
  stream->messageId = assistantId;
  stream->model = model;
  streams_.push_back(stream);
  timings[assistantId] = MessageTiming{core::nowMillis(), 0.0, 0.0};

  Json payload = buildCompletionPayload(assistantId, model);
  agents::LoopConfig config = baseToolConfig(model);
  config.messages = payload["messages"];
  if (!mcpEnabled) {
    config.tools.clear();
    config.systemPrefix.clear();
  }
  config.maxRounds = std::max(1, settings().extra.value("mcpMaxRounds", 8));
  lastPayload = payload;
  lastPayload["transport"] = "ollama /api/chat (direct, private chat)";
  lastPayload["model"] = config.model;

  std::map<std::string, std::shared_ptr<mcp::Client>> clients;
  {
    std::lock_guard<std::mutex> lock(mcpMutex_);
    clients = mcpClients_;
  }
  std::map<std::string, agents::ToolSpec> byName;
  for (const auto& spec : config.tools) byName[spec.name] = spec;
  const bool autoApprove = settings().extra.value("mcpAutoApprove", false);
  const Json approvals = settings().extra.value("mcpApprovals", Json::object());

  async::runDetached([this, stream, config, clients, byName, autoApprove, approvals, directUrl] {
    agents::LoopHooks hooks = makeToolHooks(stream, clients, byName, autoApprove, approvals);
    hooks.onDelta = [stream](const std::string& content, const std::string& reasoning) {
      std::lock_guard<std::mutex> lock(stream->mutex);
      stream->started = true;
      stream->content += content;
      stream->reasoning += reasoning;
      return !stream->cancel.load();
    };
    const agents::LoopResult result = agents::runToolLoop(api_, directUrl, config, hooks, &stream->cancel);
    std::lock_guard<std::mutex> lock(stream->mutex);
    if (!result.ok && !stream->cancel.load()) stream->error = result.error;
    stream->done = true;
  });
}

// A title from the local model, once, after the first reply.
void App::maybeGeneratePrivateTitle() {
  if (!chat.isPrivate || chat.title != "Private chat" || settings().ollamaUrl.empty()) return;
  std::string firstUser, firstReply;
  for (const std::string& id : chat.branch()) {
    const Message& m = chat.messages.at(id);
    if (m.role == "user" && firstUser.empty()) firstUser = m.content.substr(0, 1500);
    if (m.role == "assistant" && firstReply.empty()) firstReply = m.content.substr(0, 1500);
  }
  if (firstUser.empty()) return;
  const std::string model = chat.models.empty() ? (selectedModels.empty() ? "" : selectedModels.front())
                                                : chat.models.front();
  if (model.empty()) return;
  agents::LoopConfig config = baseToolConfig(model);
  config.tools.clear();
  config.systemPrefix = "Reply with a title of three to six words for this conversation. No quotes, no punctuation, "
                        "no explanation - only the title.";
  config.messages = Json::array({Json{{"role", "user"}, {"content", "User: " + firstUser + "\n\nAssistant: " + firstReply}}});
  config.maxRounds = 1;
  const std::string chatId = chat.id;
  const std::string directUrl = settings().ollamaUrl;
  async::run([this, config, chatId, directUrl] {
    std::string text;
    agents::LoopHooks hooks;
    hooks.onDelta = [&](const std::string& content, const std::string&) {
      text += content;
      return true;
    };
    const agents::LoopResult result = agents::runToolLoop(api_, directUrl, config, hooks, nullptr);
    if (!result.ok) return;
    std::string title = core::trim(text);
    if (!title.empty() && (title.front() == '"' || title.front() == '\'')) title.erase(0, 1);
    if (!title.empty() && (title.back() == '"' || title.back() == '\'' || title.back() == '.')) title.pop_back();
    if (title.size() > 60) title = title.substr(0, 60);
    if (title.empty()) return;
    async::post([this, chatId, title] {
      if (chat.id != chatId || !chat.isPrivate) return;
      chat.title = title;
      savePrivateChat();
      native::setWindowTitle(title);
    });
  });
}

void App::drawPrivateSection() {
  if (privateChats.empty()) return;
  const auto& palette = theme::palette();
  widgets::sectionHeader("PRIVATE");
  for (const ChatSummary& summary : privateChats) {
    ImGui::PushID(summary.id.c_str());
    const bool active = summary.id == chat.id;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float rowHeight = ImGui::GetTextLineHeight() + 12.0f;
    const float width = ImGui::GetContentRegionAvail().x;
    if (ImGui::InvisibleButton("##row", ImVec2(width, rowHeight))) openChat(summary.id);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (active || hovered) {
      dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + rowHeight), active ? palette.panelAlt : palette.panel, 6.0f);
    }
    icons::draw(dl, icons::Icon::Lock, ImVec2(pos.x + 14.0f, pos.y + rowHeight * 0.5f), 12.0f,
                active ? palette.text : palette.textDim);
    dl->PushClipRect(pos, ImVec2(pos.x + width - 6.0f, pos.y + rowHeight), true);
    dl->AddText(ImVec2(pos.x + 28.0f, pos.y + 6.0f), active ? palette.text : palette.textDim, summary.title.c_str());
    dl->PopClipRect();
    if (ImGui::BeginPopupContextItem("##privmenu")) {
      widgets::dimText("Encrypted on this Mac; never on the server");
      if (ImGui::MenuItem("Delete")) deletePrivateChat(summary.id);
      ImGui::EndPopup();
    }
    ImGui::PopID();
  }
}

}  // namespace app
