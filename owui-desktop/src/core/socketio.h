// Minimal Socket.IO v4 client over the Engine.IO HTTP long-polling transport
// (no WebSocket dependency). Open WebUI pushes chat completion tokens, status
// updates, channel messages and notifications over this connection.
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

#include "core/websocket.h"

namespace sio {

using Json = nlohmann::json;

class Client {
 public:
  using EventHandler = std::function<void(const std::string& name, const Json& payload)>;

  ~Client();

  // Connects in the background and keeps reconnecting until stop().
  void start(const std::string& baseUrl, const std::string& token);
  void stop();

  bool connected() const { return connected_.load(); }
  // The Engine.IO session id, which Open WebUI uses as the chat session_id.
  std::string sessionId() const;

  void setHandler(EventHandler handler);
  void emit(const std::string& name, const Json& payload);
  // Re-opens the socket after a drop; safe to call from the UI thread.
  void reconnect();
  std::string lastError() const { return lastError_; }

 private:
  void onPacket(const std::string& packet);
  void send(const std::string& packet);

  std::string base_;
  std::string token_;
  std::string sid_;
  mutable std::mutex sidMutex_;
  std::atomic<bool> running_{false};
  std::atomic<bool> connected_{false};
  std::unique_ptr<ws::Socket> socket_;
  EventHandler handler_;
  std::mutex handlerMutex_;
  std::string lastError_;
};

}  // namespace sio
