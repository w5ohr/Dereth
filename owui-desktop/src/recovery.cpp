// Stream recovery: every reply in flight is written to support/inflight.json
// as it arrives (throttled), so a crash or a quit mid-stream loses nothing.
// On the next launch each record is checked against the server: a reply the
// server finished meanwhile is just reported; one its background task is
// still writing is re-attached by polling the message until it is done; one
// that only this client was driving (HTTP path, tool or agent loop) keeps the
// text that had arrived, with a note and the Continue button to finish it.
#include "app.h"

#include <chrono>
#include <thread>
#include <unistd.h>

#include "core/async.h"
#include "core/common.h"
#include "ui/widgets.h"

namespace app {
namespace {

std::string inflightPath() { return core::supportDir() + "/inflight.json"; }

Json readInflight() {
  bool ok = false;
  const std::string raw = core::readFile(inflightPath(), &ok);
  Json parsed = ok ? Json::parse(raw, nullptr, false) : Json::object();
  return parsed.is_discarded() || !parsed.is_object() ? Json::object() : parsed;
}

// The message as stored on the server, or null.
Json serverMessage(const Json& chatBody, const std::string& messageId) {
  const Json& chat = chatBody.value("chat", Json::object());
  if (!chat.contains("history") || !chat["history"].is_object()) return Json();
  const Json& messages = chat["history"].value("messages", Json::object());
  if (!messages.is_object() || !messages.contains(messageId)) return Json();
  return messages[messageId];
}

}  // namespace

// Called from pollStreams: a snapshot of every live stream, at most once a second.
void App::saveInflight() {
  const double now = core::nowMillis();
  if (now - inflightSavedAt < 1000.0) return;
  inflightSavedAt = now;
  Json all = Json::object();
  for (const auto& stream : streams_) {
    std::lock_guard<std::mutex> lock(stream->mutex);
    if (stream->done) continue;
    all[stream->messageId] = Json{{"chatId", stream->chatId},
                                  {"model", stream->model},
                                  {"viaSocket", stream->viaSocket},
                                  {"taskIds", stream->taskIds},
                                  {"content", stream->content},
                                  {"reasoning", stream->reasoning},
                                  {"updatedAt", static_cast<int64_t>(core::nowSeconds())}};
  }
  if (all.empty()) {
    unlink(inflightPath().c_str());
  } else {
    const bool ok = core::writeFile(inflightPath(), all.dump());
    core::debugLog("inflight: %zu stream(s) -> %s (%s)", all.size(), inflightPath().c_str(), ok ? "ok" : "FAILED");
  }
}

void App::clearInflight() {
  inflightSavedAt = 0.0;
  if (streams_.empty()) {
    unlink(inflightPath().c_str());
  } else {
    saveInflight();
  }
}

// Once per launch, after sign-in: look at what was in flight when the app
// last ran.
void App::recoverInflight() {
  const Json records = readInflight();
  unlink(inflightPath().c_str());   // live streams re-create it
  if (records.empty()) return;
  const int64_t now = core::nowSeconds();
  for (const auto& [messageId, record] : records.items()) {
    if (!record.is_object()) continue;
    const std::string chatId = record.value("chatId", "");
    if (chatId.empty()) continue;
    if (now - record.value("updatedAt", static_cast<int64_t>(0)) > 24 * 3600) continue;
    const std::string mid = messageId;
    const Json rec = record;
    async::run([this, chatId, mid, rec] {
      api::Result res = api_.chat(chatId);
      async::post([this, chatId, mid, rec, res] {
        if (!res.ok) return;   // server away: the local copy will show whatever was saved
        const std::string title = res.data.value("title", "the chat");
        const Json message = serverMessage(res.data, mid);
        const bool serverDone = message.is_object() && message.value("done", false) &&
                                !message.value("content", "").empty();
        if (serverDone) {
          widgets::toastInfo("A reply in \"" + title + "\" finished while the app was closed");
          if (chat.id.empty()) openChat(chatId);
          return;
        }
        PendingResume pending;
        pending.chatId = chatId;
        pending.messageId = mid;
        pending.record = rec;
        pending.resume = rec.value("viaSocket", false);   // only a server task keeps writing
        pendingResume_ = pending;
        if (chat.id != chatId) openChat(chatId);
      });
    });
  }
}

// Called every frame: attaches once the chat with the pending reply is open.
void App::tickRecovery() {
  if (pendingResume_.chatId.empty() || chat.id != pendingResume_.chatId) return;
  auto it = chat.messages.find(pendingResume_.messageId);
  const PendingResume pending = pendingResume_;
  pendingResume_ = PendingResume{};
  if (it == chat.messages.end()) return;
  Message& message = it->second;
  const std::string saved = pending.record.value("content", "");
  if (saved.size() > message.content.size()) message.content = saved;
  const std::string savedReasoning = pending.record.value("reasoning", "");
  if (savedReasoning.size() > message.reasoning.size()) message.reasoning = savedReasoning;

  if (!pending.resume) {
    // Nothing on the server will finish this one: keep what arrived.
    message.streaming = false;
    message.error = "The app closed while this reply was streaming - the text so far was kept. "
                    "Use Continue to let the model finish it.";
    saveActiveChat();
    widgets::toastInfo("Kept the partial reply in \"" + chat.title + "\"");
    return;
  }

  // The server's task may still be writing: follow the message until done.
  auto stream = std::make_shared<Stream>();
  stream->chatId = chat.id;
  stream->messageId = message.id;
  stream->model = message.model.empty() ? pending.record.value("model", "") : message.model;
  stream->viaSocket = true;
  stream->resumed = true;
  stream->started = true;
  stream->content = message.content;
  stream->reasoning = message.reasoning;
  if (pending.record.contains("taskIds") && pending.record["taskIds"].is_array()) {
    for (const auto& id : pending.record["taskIds"]) {
      if (id.is_string()) stream->taskIds.push_back(id);
    }
  }
  stream->status = "Catching up with the reply the server was still writing...";
  streams_.push_back(stream);
  message.streaming = true;
  message.error.clear();
  timings[message.id] = MessageTiming{core::nowMillis(), core::nowMillis(), 0.0};
  widgets::toastInfo("Resuming the reply in \"" + chat.title + "\"");

  const std::string chatId = chat.id;
  const std::string messageId = message.id;
  async::runDetached([this, stream, chatId, messageId] {
    int failures = 0, quietPolls = 0;
    std::string last;
    for (int i = 0; i < 600 && !stream->cancel.load(); ++i) {   // ~15 minutes at most
      std::this_thread::sleep_for(std::chrono::milliseconds(1500));
      if (stream->cancel.load()) break;
      api::Result res = api_.chat(chatId);
      if (!res.ok) {
        if (++failures >= 5) break;
        continue;
      }
      failures = 0;
      const Json message = serverMessage(res.data, messageId);
      if (!message.is_object()) break;
      const std::string content = message.value("content", "");
      const std::string reasoning = message.value("reasoning_content", "");
      bool done = message.value("done", false);
      {
        std::lock_guard<std::mutex> lock(stream->mutex);
        if (content.size() >= stream->content.size()) stream->content = content;
        if (reasoning.size() >= stream->reasoning.size()) stream->reasoning = reasoning;
        if (message.contains("usage") && message["usage"].is_object()) stream->usage = message["usage"];
        if (done) stream->status.clear();
      }
      if (done) break;
      // A task that is no longer listed and a message that stopped growing
      // means the server gave up on it: keep what there is.
      quietPolls = (content == last) ? quietPolls + 1 : 0;
      last = content;
      if (quietPolls >= 8) {
        api::Result tasks = api_.chatTasks(chatId);
        const bool running = tasks.ok && tasks.data.is_object() && tasks.data.contains("task_ids") &&
                             tasks.data["task_ids"].is_array() && !tasks.data["task_ids"].empty();
        if (!running) break;
        quietPolls = 0;
      }
    }
    std::lock_guard<std::mutex> lock(stream->mutex);
    stream->status.clear();
    stream->done = true;
  });
}

}  // namespace app
