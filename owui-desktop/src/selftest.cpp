// `Open WebUI --selftest` drives the API layer end to end against a server and
// prints a pass/fail line per capability. Credentials come from the
// environment: OWUI_SERVER, plus OWUI_TOKEN or OWUI_EMAIL/OWUI_PASSWORD.
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <map>

#include "api/client.h"
#include "core/async.h"
#include "core/common.h"
#include "core/http.h"
#include "core/socketio.h"
#include "core/store.h"
#include <unistd.h>
#include <chrono>
#include <thread>

namespace {

using Json = nlohmann::json;

int gPassed = 0;
int gFailed = 0;

void report(const std::string& name, bool ok, const std::string& detail = "") {
  if (ok) {
    ++gPassed;
    printf("  PASS  %-34s %s\n", name.c_str(), detail.c_str());
  } else {
    ++gFailed;
    printf("  FAIL  %-34s %s\n", name.c_str(), detail.c_str());
  }
  fflush(stdout);
}

std::string env(const char* key, const char* fallback = "") {
  const char* value = std::getenv(key);
  return value ? value : fallback;
}

}  // namespace

int runSelfTest() {
  net::globalInit();
  async::start(4);

  api::Client api;
  api.setServer(env("OWUI_SERVER", "http://localhost:3000"));
  printf("Open WebUI desktop self-test against %s\n\n", api.server().c_str());

  api::Result config = api.config();
  report("GET /api/config", config.ok,
         config.ok ? config.data.value("name", "") + " " + config.data.value("version", "")
                   : config.error);

  std::string token = env("OWUI_TOKEN");
  if (token.empty()) {
    api::Result signin = api.signin(env("OWUI_EMAIL"), env("OWUI_PASSWORD"));
    report("POST auths/signin", signin.ok && signin.data.contains("token"),
           signin.ok ? signin.data.value("email", "") : signin.error);
    if (!signin.ok) return 1;
    token = signin.data["token"];
  }
  api.setToken(token);

  api::Result me = api.sessionUser();
  report("GET auths/ (session)", me.ok,
         me.ok ? me.data.value("name", "") + " / " + me.data.value("role", "") : me.error);

  // ---- models
  api::Result models = api.models();
  std::vector<std::string> modelIds;
  if (models.ok) {
    const api::Json& data = models.data.contains("data") ? models.data["data"] : models.data;
    for (const auto& item : data) modelIds.push_back(item.value("id", ""));
  }
  report("GET /api/models", models.ok && !modelIds.empty(),
         std::to_string(modelIds.size()) + " models");
  if (modelIds.empty()) return 1;

  std::string model = modelIds.front();
  for (const std::string& candidate : modelIds) {
    if (candidate.find("qwen3:8b") != std::string::npos) model = candidate;
  }
  printf("  ....  using model %s\n", model.c_str());

  // ---- chat lifecycle
  api::Json chatBody{{"title", "Self test"},
                     {"models", api::Json::array({model})},
                     {"messages", api::Json::array()},
                     {"history", api::Json{{"currentId", nullptr},
                                           {"messages", api::Json::object()}}}};
  api::Result created = api.createChat(chatBody);
  const std::string chatId = created.ok ? created.data.value("id", "") : "";
  report("POST chats/new", created.ok && !chatId.empty(), chatId);

  // ---- streaming completion
  std::string streamed;
  std::string reasoned;
  int chunks = 0;
  std::atomic<bool> cancel{false};
  api::Json payload{
      {"model", model},
      {"stream", true},
      {"id", core::uuid()},
      {"messages", api::Json::array({api::Json{
                       {"role", "user"},
                       {"content", "Reply with the single word: pong. /no_think"}}})},
  };
  const double started = core::nowMillis();
  api::Result stream = api.streamCompletion(
      payload,
      [&](const api::StreamDelta& delta) {
        streamed += delta.content;
        reasoned += delta.reasoning;
        if (!delta.content.empty() || !delta.reasoning.empty()) ++chunks;
        return true;
      },
      &cancel);
  const double elapsed = (core::nowMillis() - started) / 1000.0;
  char detail[256];
  snprintf(detail, sizeof(detail), "%d chunks in %.1fs: \"%.50s\" %s%.80s", chunks, elapsed,
           core::trim(streamed).c_str(), ("reasoning=" + std::to_string(reasoned.size()) + "B err=").c_str(), stream.error.c_str());
  report("POST /api/chat/completions (SSE)", stream.ok && chunks > 0, detail);

  // ---- socket.io transport (how the web UI receives background completions)
  {
    sio::Client socket;
    std::string socketContent;
    std::atomic<bool> socketDone{false};
    std::atomic<int> socketEvents{0};
    socket.setHandler([&](const std::string& name, const api::Json& payload) {
      if (name != "events") return;
      ++socketEvents;
      api::StreamDelta delta;
      std::string messageId;
      std::string eventChatId;
      if (!api::parseSocketEvent(payload, &delta, &messageId, &eventChatId)) return;
      socketContent += delta.content;
      if (delta.hasReplace) socketContent = delta.replaceContent;
      if (delta.done) socketDone = true;
    });
    socket.start(api.server(), token);
    for (int i = 0; i < 150 && !socket.connected(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    report("socket.io connect", socket.connected(), "sid=" + socket.sessionId());

    if (socket.connected()) {
      const std::string messageId = core::uuid();
      api::Json backgroundPayload{
          {"model", model},
          {"stream", true},
          {"chat_id", chatId},
          {"id", messageId},
          {"session_id", socket.sessionId()},
          {"messages", api::Json::array({api::Json{{"role", "user"},
                                                   {"content", "Reply with one word: pong"}}})},
      };
      api::Result background = api.completion(backgroundPayload);
      report("POST completions (background task)", background.ok,
             background.ok ? "task queued" : background.error);
      const double waitStarted = core::nowMillis();
      while (!socketDone.load() && core::nowMillis() - waitStarted < 120000.0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      char socketDetail[256];
      snprintf(socketDetail, sizeof(socketDetail), "%d events, %.1fs: \"%.40s\"",
               socketEvents.load(), (core::nowMillis() - waitStarted) / 1000.0,
               core::trim(socketContent).c_str());
      report("socket.io chat:completion stream", socketDone.load() && !socketContent.empty(),
             socketDetail);
    }
    socket.stop();
  }

  // ---- persistence round trip
  api::Json history{{"currentId", "m2"}, {"messages", api::Json::object()}};
  history["messages"]["m1"] = api::Json{{"id", "m1"},
                                        {"parentId", nullptr},
                                        {"childrenIds", api::Json::array({"m2"})},
                                        {"role", "user"},
                                        {"content", "ping"},
                                        {"timestamp", core::nowSeconds()}};
  history["messages"]["m2"] = api::Json{{"id", "m2"},
                                        {"parentId", "m1"},
                                        {"childrenIds", api::Json::array()},
                                        {"role", "assistant"},
                                        {"content", streamed},
                                        {"model", model},
                                        {"timestamp", core::nowSeconds()}};
  api::Json saved = chatBody;
  saved["history"] = history;
  saved["messages"] = api::Json::array({history["messages"]["m1"], history["messages"]["m2"]});
  api::Result updated = api.updateChat(chatId, saved);
  report("POST chats/{id} (save)", updated.ok, updated.ok ? "" : updated.error);

  api::Result fetched = api.chat(chatId);
  bool roundTripped = false;
  if (fetched.ok) {
    const api::Json& chat = fetched.data.value("chat", api::Json::object());
    roundTripped = chat["history"]["messages"].contains("m2") &&
                   chat["history"]["messages"]["m2"].value("content", "") == streamed;
  }
  report("GET chats/{id} (round trip)", roundTripped);

  report("GET chats/ (list)", api.chatList(1).ok);
  report("GET chats/search", api.chatSearch("Self test").ok);
  report("POST chats/{id}/pin", api.pinChat(chatId).ok);
  report("POST chats/{id}/archive", api.archiveChat(chatId).ok);
  report("POST chats/{id}/clone", api.cloneChat(chatId).ok);
  report("POST chats/{id}/share", api.shareChat(chatId).ok);

  // ---- task models
  api::Json titlePayload{{"model", model},
                         {"chat_id", chatId},
                         {"messages", api::Json::array({api::Json{
                                          {"role", "user"},
                                          {"content", "How do I sort a list in Python?"}}})}};
  api::Result title = api.generateTitle(titlePayload);
  report("POST tasks/title/completions", title.ok, title.ok ? "" : title.error);

  // ---- folders
  api::Result folder = api.createFolder("Self test folder");
  const std::string folderId = folder.ok && folder.data.is_object()
                                   ? folder.data.value("id", "")
                                   : "";
  report("POST folders/", folder.ok);
  report("GET folders/", api.folders().ok);
  if (!folderId.empty()) report("DELETE folders/{id}", api.deleteFolder(folderId).ok);

  // ---- files & knowledge
  api::Result upload = api.uploadFile("selftest.txt", "text/plain",
                                      "Dereth is a world in Asheron's Call.\n");
  const std::string fileId = upload.ok ? upload.data.value("id", "") : "";
  report("POST files/ (upload)", upload.ok && !fileId.empty(), fileId);
  report("GET files/", api.files().ok);

  api::Result knowledge = api.createKnowledge("Self test KB", "temporary");
  const std::string knowledgeId = knowledge.ok ? knowledge.data.value("id", "") : "";
  report("POST knowledge/create", knowledge.ok && !knowledgeId.empty());
  if (!knowledgeId.empty() && !fileId.empty()) {
    report("POST knowledge/{id}/file/add", api.addFileToKnowledge(knowledgeId, fileId).ok);
    report("DELETE knowledge/{id}", api.deleteKnowledge(knowledgeId).ok);
  }
  if (!fileId.empty()) report("DELETE files/{id}", api.deleteFile(fileId).ok);

  // ---- prompts
  api::Json prompt{{"command", "/selftest"},
                   {"name", "Self test"},
                   {"title", "Self test"},
                   {"content", "Say hi"}};
  api::Result createdPrompt = api.createPrompt(prompt);
  report("POST prompts/create", createdPrompt.ok, createdPrompt.ok ? "" : createdPrompt.error);
  report("GET prompts/list", api.prompts().ok);
  if (createdPrompt.ok) {
    const std::string promptId = createdPrompt.data.value("id", createdPrompt.data.value("command", ""));
    report("DELETE prompts/{id}", api.deletePrompt(promptId).ok);
  }

  // ---- memories
  api::Result memory = api.addMemory("The self test ran on this machine.");
  const std::string memoryId = memory.ok ? memory.data.value("id", "") : "";
  report("POST memories/add", memory.ok);
  report("GET memories/", api.memories().ok);
  if (!memoryId.empty()) report("DELETE memories/{id}", api.deleteMemory(memoryId).ok);

  // ---- notes
  api::Json note{{"title", "Self test note"},
                 {"data", api::Json{{"content", api::Json{{"md", "hello"}}}}},
                 {"meta", api::Json::object()},
                 {"access_control", nullptr}};
  api::Result createdNote = api.createNote(note);
  const std::string noteId = createdNote.ok ? createdNote.data.value("id", "") : "";
  report("POST notes/create", createdNote.ok, createdNote.ok ? "" : createdNote.error);
  report("GET notes/", api.notes().ok);
  if (!noteId.empty()) report("DELETE notes/{id}", api.deleteNote(noteId).ok);

  // ---- read-only surfaces
  {
    api::Result channels = api.channels();
    // Channels are an opt-in server feature; 403 means "turned off", not broken.
    report("GET channels/", channels.ok || channels.status == 403,
           channels.status == 403 ? "(disabled on this server)" : "");
  }
  report("GET tools/list", api.tools().ok);
  report("GET functions/list", api.functions().ok);
  report("GET skills/list", api.skills().ok);
  report("GET models/ (custom)", api.customModels().ok);
  report("GET images/config", api.imageConfig().ok);
  report("GET audio/config", api.audioConfig().ok);
  report("GET users/ (admin)", api.users().ok);
  report("GET configs/connections", api.connections().ok);
  report("GET evaluations/leaderboard", api.leaderboard().ok);
  report("GET analytics/summary", api.analyticsSummary().ok);
  report("GET ollama/api/tags", api.ollamaTags().ok);
  report("GET ollama/api/ps", api.ollamaPs().ok);
  report("GET automations/list", api.automations().ok);
  report("GET calendars/", api.calendars().ok);
  {
    api::Result notifications = api.notificationEvents();
    report("GET notifications/events", notifications.ok || notifications.status == 404,
           notifications.status == 404 ? "(not enabled on this server)" : "");
  }

  // ---- local-first mirror (SQLite, no server involved)
  {
    const std::string tmpUrl = "http://selftest-mirror.local:" + std::to_string(getpid());
    store::open(tmpUrl);
    store::clear();
    report("mirror open", store::ready(), store::stats().path);
    const Json one = Json::parse(R"({"id":"m1","title":"Poodle grooming","updated_at":100,"pinned":true,
      "chat":{"history":{"messages":{"a":{"role":"user","content":"How do I groom a parti poodle?"}}}}})");
    const Json two = Json::parse(R"({"id":"m2","title":"Roman history","updated_at":200,"archived":true,
      "chat":{"history":{"messages":{"b":{"role":"assistant","content":"The empire fell in 476."}}}}})");
    store::putChat(one);
    store::putChat(two);
    report("mirror list", store::listChats(false).size() == 1 && store::listChats(true).size() == 1);
    const auto hits = store::search("poodle");
    report("mirror search (body)", hits.size() == 1 && hits.front().id == "m1");
    report("mirror search (prefix)", store::search("rom").size() == 1);
    report("mirror search (no hit)", store::search("zebra").empty());
    report("mirror getChat", store::getChat("m1").has_value() && (*store::getChat("m1")).value("title", "") == "Poodle grooming");
    store::putListItem(Json{{"id", "m1"}, {"title", "Poodle care"}, {"updated_at", 150}, {"pinned", false}});
    report("mirror list item update", store::listChats(false).front().title == "Poodle care" &&
                                          store::listChats(false).front().updatedAt == 150);
    store::putDirty("m1", Json{{"title", "Poodle care (offline)"}, {"history", Json::object()}}, 150);
    const auto dirty = store::dirtyChats();
    report("mirror dirty round-trip", dirty.size() == 1 && dirty.front().first == "m1" &&
                                          store::dirtyBase("m1") == 150 && store::stats().dirty == 1);
    store::putListItem(Json{{"id", "m1"}, {"title", "server title"}, {"updated_at", 999}});
    report("mirror dirty row ignores list updates", store::listChats(false).front().title == "Poodle care (offline)");
    store::putChat(Json{{"id", "m1"}, {"title", "synced"}, {"updated_at", 300}, {"chat", Json::object()}});
    report("mirror putChat clears dirty", store::dirtyChats().empty());
    store::removeChat("m2");
    report("mirror remove", store::chatIds().size() == 1 && store::search("empire").empty());
    store::clear();
    store::close();
    unlink(store::stats().path.c_str());
  }

  // ---- cleanup
  report("DELETE chats/{id}", api.deleteChat(chatId).ok);

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  async::shutdown();
  return gFailed == 0 ? 0 : 1;
}
