#include "core/socketio.h"

#include <chrono>
#include <vector>

#include "core/common.h"
#include "core/websocket.h"

namespace sio {
namespace {

// Engine.IO v4 packet types we care about: 0 open, 1 close, 2 ping, 3 pong,
// 4 message. Socket.IO rides inside "4": 0 connect, 1 disconnect, 2 event.
constexpr const char* kPath = "/ws/socket.io/?EIO=4&transport=websocket";

}  // namespace

Client::~Client() { stop(); }

std::string Client::sessionId() const {
  std::lock_guard<std::mutex> lock(sidMutex_);
  return sid_;
}

void Client::setHandler(EventHandler handler) {
  std::lock_guard<std::mutex> lock(handlerMutex_);
  handler_ = std::move(handler);
}

void Client::start(const std::string& baseUrl, const std::string& token) {
  stop();
  base_ = baseUrl;
  token_ = token;
  running_.store(true);
  socket_ = std::make_unique<ws::Socket>();
  socket_->setHandlers([this](const std::string& text) { onPacket(text); },
                       [this](bool open, const std::string& error) {
                         core::debugLog("sio transport %s %s", open ? "open" : "closed",
                                        error.c_str());
                         if (!open) {
                           connected_.store(false);
                           lastError_ = error;
                         }
                       });
  socket_->connect(ws::toWebSocketUrl(base_) + kPath);
}

void Client::stop() {
  running_.store(false);
  connected_.store(false);
  if (socket_) {
    socket_->close();
    socket_.reset();
  }
  std::lock_guard<std::mutex> lock(sidMutex_);
  sid_.clear();
}

void Client::reconnect() {
  if (!running_.load()) return;
  connected_.store(false);
  {
    std::lock_guard<std::mutex> lock(sidMutex_);
    sid_.clear();
  }
  if (!socket_) return;
  socket_->connect(ws::toWebSocketUrl(base_) + kPath);
}

void Client::send(const std::string& packet) {
  if (socket_) socket_->send(packet);
}

void Client::emit(const std::string& name, const Json& payload) {
  Json frame = Json::array({name, payload});
  send("42" + frame.dump());
}

void Client::onPacket(const std::string& packet) {
  core::debugLog("sio packet: %.120s", packet.c_str());
  if (packet.empty()) return;
  switch (packet[0]) {
    case '0': {  // engine.io open
      Json open = Json::parse(packet.substr(1), nullptr, false);
      if (!open.is_discarded() && open.contains("sid")) {
        std::lock_guard<std::mutex> lock(sidMutex_);
        sid_ = open["sid"].get<std::string>();
      }
      // socket.io namespace connect, carrying the bearer token as auth.
      send("40" + Json{{"token", token_}}.dump());
      break;
    }
    case '1':  // engine.io close
      connected_.store(false);
      break;
    case '2':  // ping
      send("3");
      break;
    case '4': {
      if (packet.size() < 2) break;
      if (packet[1] == '0') {
        // Socket.IO connect ack carries the per-namespace socket id, which is
        // what Open WebUI expects as `session_id`.
        Json ack = Json::parse(packet.substr(2), nullptr, false);
        if (!ack.is_discarded() && ack.is_object() && ack.contains("sid")) {
          std::lock_guard<std::mutex> lock(sidMutex_);
          sid_ = ack["sid"].get<std::string>();
        }
        connected_.store(true);
        emit("user-join", Json{{"auth", Json{{"token", token_}}}});
      } else if (packet[1] == '1') {
        connected_.store(false);
      } else if (packet[1] == '2') {
        Json frame = Json::parse(packet.substr(2), nullptr, false);
        if (frame.is_discarded() || !frame.is_array() || frame.empty()) break;
        const std::string name = frame[0].is_string() ? frame[0].get<std::string>() : "";
        Json payload = frame.size() > 1 ? frame[1] : Json::object();
        EventHandler handler;
        {
          std::lock_guard<std::mutex> lock(handlerMutex_);
          handler = handler_;
        }
        if (handler) handler(name, payload);
      }
      break;
    }
    default:
      break;
  }
}

}  // namespace sio
