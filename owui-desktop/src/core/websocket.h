// WebSocket client backed by NSURLSessionWebSocketTask, so TLS, proxies and
// HTTP upgrade are handled by the system.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ws {

class Socket {
 public:
  using MessageHandler = std::function<void(const std::string& text)>;
  using StateHandler = std::function<void(bool open, const std::string& error)>;

  Socket();
  ~Socket();

  void setHandlers(MessageHandler onMessage, StateHandler onState);
  // `url` must use ws:// or wss://.
  void connect(const std::string& url,
               const std::vector<std::pair<std::string, std::string>>& headers = {});
  void send(const std::string& text);
  void close();
  bool open() const;

 private:
  struct Impl;
  // Shared, because the URLSession completion blocks outlive the Socket when
  // it is torn down mid-flight.
  std::shared_ptr<Impl> impl_;
};

// http(s):// -> ws(s)://
std::string toWebSocketUrl(const std::string& httpUrl);

}  // namespace ws
