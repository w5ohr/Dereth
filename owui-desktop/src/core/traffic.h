// Traffic inspector (#134): a live, in-memory log of every HTTP transfer and
// socket.io packet - latency waterfall, bodies (capped, secrets masked), HAR
// export, curl and replay. The pure parts live here: the ring log, secret
// masking, socket.io packet naming, filtering, summaries, the waterfall
// geometry and the HAR writer. App-free; --traffictest covers it.
#pragma once

#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace traffic {

using Json = nlohmann::json;

constexpr size_t kBodyCap = 16384;   // bytes of each body kept

struct Entry {
  int seq = 0;
  std::string kind;           // "http" | "ws"
  double startMs = 0.0;       // epoch milliseconds
  double ms = 0.0;            // duration (http)
  std::string method;         // GET/POST... | "send"/"recv" for ws
  std::string url, host, path;
  std::string event;          // ws: "ping", "pong", "open", "connect", "event <name>", "ack", ...
  long status = 0;
  int64_t bytesOut = 0, bytesIn = 0;
  std::string error;
  std::string contentType;    // response content-type (http)
  std::string requestBody;    // capped; "<multipart: ...>" for uploads
  std::string responseBody;   // capped; ws: the packet
  bool streamed = false;

  bool ok() const;            // http: no error and status < 400; ws: always
  bool heartbeat() const;     // ws ping/pong
};

// Thread-safe ring of the newest `capacity` entries.
class Log {
 public:
  explicit Log(size_t capacity = 1000) : cap_(capacity) {}
  int add(Entry entry);       // assigns seq (0 = dropped while paused)
  std::vector<Entry> snapshot() const;
  void clear();
  void setPaused(bool paused);
  bool paused() const;
  size_t size() const;
  int dropped() const;        // entries that fell off the ring

 private:
  mutable std::mutex mutex_;
  std::deque<Entry> entries_;
  size_t cap_;
  int next_ = 1;
  bool paused_ = false;
  int dropped_ = 0;
};

// Tokens, keys, passwords and bearer values masked in a body (JSON or text).
std::string redact(const std::string& body);
// Human name of an Engine.IO/Socket.IO packet: "ping", "pong", "open",
// "close", "connect", "disconnect", "event <name>", "ack", "error", "?".
std::string sioEvent(const std::string& packet);
std::string cap(const std::string& body, size_t limit = kBodyCap);   // cut on a UTF-8 boundary, marked

// `kind`: 0 all, 1 http, 2 ws. Heartbeats (ws ping/pong) only when asked.
std::vector<Entry> filter(const std::vector<Entry>& entries, const std::string& needle, int kind, bool heartbeats);

struct Summary {
  int requests = 0, failures = 0, wsIn = 0, wsOut = 0;
  int64_t bytesOut = 0, bytesIn = 0;
  double p50 = 0.0, p95 = 0.0, slowestMs = 0.0;
  std::string slowest;        // "POST /api/chat/completions"
};
Summary summarize(const std::vector<Entry>& entries);

// Bars as fractions of the window spanned by `entries` (ws packets are points).
struct Bar {
  int seq = 0;
  float x0 = 0.0f, x1 = 0.0f;
};
std::vector<Bar> waterfall(const std::vector<Entry>& entries, double* windowStartMs, double* windowEndMs);

// HAR 1.2. Headers are not captured and say so; bodies are redacted; ws
// packets become one websocket entry with Chrome's _webSocketMessages.
std::string har(const std::vector<Entry>& entries, const std::string& creatorVersion, const std::string& wsUrl);
std::string curlCommand(const Entry& entry);   // the bearer token as $OWUI_TOKEN
double nowEpochMs();                           // wall-clock milliseconds
std::string isoTime(double epochMs);           // "2026-10-25T14:03:05.123Z"
std::string clockTime(double epochMs);         // local "14:03:05.123"
std::string pretty(const std::string& body);   // JSON reindented when it parses, else as is

}  // namespace traffic
