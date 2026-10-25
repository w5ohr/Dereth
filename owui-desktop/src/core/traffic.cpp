#include "traffic.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <regex>

#include "auditlog.h"
#include "common.h"

namespace traffic {

bool Entry::ok() const {
  if (kind != "http") return true;
  return error.empty() && status > 0 && status < 400;
}

bool Entry::heartbeat() const { return kind == "ws" && (event == "ping" || event == "pong"); }

int Log::add(Entry entry) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (paused_) return 0;
  entry.seq = next_++;
  entries_.push_back(std::move(entry));
  while (entries_.size() > cap_) {
    entries_.pop_front();
    ++dropped_;
  }
  return entries_.back().seq;
}

std::vector<Entry> Log::snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return std::vector<Entry>(entries_.begin(), entries_.end());
}

void Log::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  entries_.clear();
  dropped_ = 0;
}

void Log::setPaused(bool paused) {
  std::lock_guard<std::mutex> lock(mutex_);
  paused_ = paused;
}

bool Log::paused() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return paused_;
}

size_t Log::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return entries_.size();
}

int Log::dropped() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return dropped_;
}

std::string redact(const std::string& body) {
  static const std::regex kJsonSecret(R"re(("(?:token|access_token|refresh_token|id_token|api_key|apikey|api_token|password|passwd|secret|client_secret|authorization|auth|key|cookie)"\s*:\s*")([^"]*)("))re",
                                      std::regex::icase);
  static const std::regex kBearer(R"((Bearer\s+)[A-Za-z0-9._~+/=-]+)");
  static const std::regex kSk(R"(\bsk-[A-Za-z0-9_-]{8,})");
  static const std::regex kFormSecret(R"re(((?:^|[&?])(?:token|password|api_key|key)=)[^&\s]+)re", std::regex::icase);
  std::string out = std::regex_replace(body, kJsonSecret, "$1***$3");
  out = std::regex_replace(out, kBearer, "$1***");
  out = std::regex_replace(out, kSk, "sk-***");
  out = std::regex_replace(out, kFormSecret, "$1***");
  return out;
}

std::string sioEvent(const std::string& packet) {
  if (packet.empty()) return "?";
  switch (packet[0]) {
    case '0': return "open";
    case '1': return "close";
    case '2': return "ping";
    case '3': return "pong";
    case '4': {
      if (packet.size() < 2) return "message";
      switch (packet[1]) {
        case '0': return "connect";
        case '1': return "disconnect";
        case '2': {
          // 42[ "name", ... ] - possibly with an ack id before the bracket.
          const size_t bracket = packet.find('[');
          if (bracket == std::string::npos) return "event";
          Json frame = Json::parse(packet.substr(bracket), nullptr, false);
          if (frame.is_array() && !frame.empty() && frame[0].is_string()) return "event " + frame[0].get<std::string>();
          return "event";
        }
        case '3': return "ack";
        case '4': return "error";
        default: return "message";
      }
    }
    default: return "?";
  }
}

std::string cap(const std::string& body, size_t limit) {
  if (body.size() <= limit) return body;
  size_t cut = limit;
  while (cut > 0 && (static_cast<unsigned char>(body[cut]) & 0xC0) == 0x80) --cut;   // don't split a UTF-8 sequence
  return body.substr(0, cut) + "\n... (" + core::humanBytes(static_cast<int64_t>(body.size() - cut)) + " more not kept)";
}

std::vector<Entry> filter(const std::vector<Entry>& entries, const std::string& needle, int kind, bool heartbeats) {
  std::vector<Entry> out;
  const std::string n = core::lower(needle);
  for (const Entry& e : entries) {
    if (kind == 1 && e.kind != "http") continue;
    if (kind == 2 && e.kind != "ws") continue;
    if (!heartbeats && e.heartbeat()) continue;
    if (!n.empty()) {
      const bool hit = core::lower(e.host).find(n) != std::string::npos || core::lower(e.path).find(n) != std::string::npos ||
                       core::lower(e.method).find(n) != std::string::npos || core::lower(e.event).find(n) != std::string::npos ||
                       (e.status > 0 && std::to_string(e.status).find(n) != std::string::npos) || core::lower(e.error).find(n) != std::string::npos;
      if (!hit) continue;
    }
    out.push_back(e);
  }
  return out;
}

Summary summarize(const std::vector<Entry>& entries) {
  Summary s;
  std::vector<double> times;
  for (const Entry& e : entries) {
    if (e.kind == "ws") {
      (e.method == "send" ? s.wsOut : s.wsIn)++;
      continue;
    }
    ++s.requests;
    if (!e.ok()) ++s.failures;
    s.bytesOut += e.bytesOut;
    s.bytesIn += e.bytesIn;
    times.push_back(e.ms);
    if (e.ms > s.slowestMs) {
      s.slowestMs = e.ms;
      s.slowest = e.method + " " + e.path;
    }
  }
  if (!times.empty()) {
    std::sort(times.begin(), times.end());
    auto at = [&](double q) { return times[std::min(times.size() - 1, static_cast<size_t>(std::floor(q * static_cast<double>(times.size() - 1) + 0.5)))]; };
    s.p50 = at(0.5);
    s.p95 = at(0.95);
  }
  return s;
}

std::vector<Bar> waterfall(const std::vector<Entry>& entries, double* windowStartMs, double* windowEndMs) {
  std::vector<Bar> bars;
  if (entries.empty()) {
    if (windowStartMs) *windowStartMs = 0.0;
    if (windowEndMs) *windowEndMs = 0.0;
    return bars;
  }
  double lo = entries[0].startMs, hi = entries[0].startMs + entries[0].ms;
  for (const Entry& e : entries) {
    lo = std::min(lo, e.startMs);
    hi = std::max(hi, e.startMs + e.ms);
  }
  if (hi <= lo) hi = lo + 1.0;
  const double span = hi - lo;
  for (const Entry& e : entries) {
    Bar b;
    b.seq = e.seq;
    b.x0 = static_cast<float>((e.startMs - lo) / span);
    b.x1 = static_cast<float>((e.startMs + e.ms - lo) / span);
    bars.push_back(b);
  }
  if (windowStartMs) *windowStartMs = lo;
  if (windowEndMs) *windowEndMs = hi;
  return bars;
}

double nowEpochMs() {
  return static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

std::string isoTime(double epochMs) {
  const std::time_t secs = static_cast<std::time_t>(std::floor(epochMs / 1000.0));
  const int millis = static_cast<int>(epochMs - static_cast<double>(secs) * 1000.0);
  std::tm utc{};
  gmtime_r(&secs, &utc);
  char buf[40];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &utc);
  char out[48];
  snprintf(out, sizeof(out), "%s.%03dZ", buf, std::max(0, std::min(999, millis)));
  return out;
}

std::string clockTime(double epochMs) {
  const std::time_t secs = static_cast<std::time_t>(std::floor(epochMs / 1000.0));
  const int millis = static_cast<int>(epochMs - static_cast<double>(secs) * 1000.0);
  std::tm local{};
  localtime_r(&secs, &local);
  char buf[16];
  std::strftime(buf, sizeof(buf), "%H:%M:%S", &local);
  char out[24];
  snprintf(out, sizeof(out), "%s.%03d", buf, std::max(0, std::min(999, millis)));
  return out;
}

std::string pretty(const std::string& body) {
  if (body.empty() || (body[0] != '{' && body[0] != '[')) return body;
  Json j = Json::parse(body, nullptr, false);
  if (j.is_discarded()) return body;
  return j.dump(2);
}

std::string har(const std::vector<Entry>& entries, const std::string& creatorVersion, const std::string& wsUrl) {
  Json out = Json{{"log", Json{{"version", "1.2"},
                               {"creator", Json{{"name", "Open WebUI Desktop"}, {"version", creatorVersion}}},
                               {"comment", "Request and response headers are not captured; bodies are capped and have secrets masked."},
                               {"entries", Json::array()}}}};
  Json& list = out["log"]["entries"];
  Json wsMessages = Json::array();
  double wsStart = 0.0;
  for (const Entry& e : entries) {
    if (e.kind == "ws") {
      if (wsStart == 0.0 || e.startMs < wsStart) wsStart = e.startMs;
      wsMessages.push_back(Json{{"type", e.method == "send" ? "send" : "receive"}, {"time", e.startMs / 1000.0}, {"opcode", 1}, {"data", redact(e.responseBody)}});
      continue;
    }
    Json request{{"method", e.method}, {"url", e.url}, {"httpVersion", "HTTP/1.1"}, {"headers", Json::array()}, {"queryString", Json::array()},
                 {"cookies", Json::array()}, {"headersSize", -1}, {"bodySize", e.bytesOut}};
    const size_t q = e.url.find('?');
    if (q != std::string::npos) {
      for (const std::string& pair : core::split(e.url.substr(q + 1), '&')) {
        const size_t eq = pair.find('=');
        request["queryString"].push_back(Json{{"name", pair.substr(0, eq)}, {"value", eq == std::string::npos ? "" : pair.substr(eq + 1)}});
      }
    }
    if (!e.requestBody.empty()) {
      request["postData"] = Json{{"mimeType", core::startsWith(e.requestBody, "<multipart") ? "multipart/form-data" : "application/json"}, {"text", redact(e.requestBody)}};
    }
    Json response{{"status", e.status}, {"statusText", e.error.empty() ? "" : e.error}, {"httpVersion", "HTTP/1.1"}, {"headers", Json::array()},
                  {"cookies", Json::array()},
                  {"content", Json{{"size", e.bytesIn}, {"mimeType", e.contentType.empty() ? "application/octet-stream" : e.contentType}, {"text", redact(e.responseBody)}}},
                  {"redirectURL", ""}, {"headersSize", -1}, {"bodySize", e.bytesIn}};
    Json entry{{"startedDateTime", isoTime(e.startMs)}, {"time", e.ms}, {"request", request}, {"response", response}, {"cache", Json::object()},
               {"timings", Json{{"blocked", -1}, {"dns", -1}, {"connect", -1}, {"send", 0}, {"wait", e.ms}, {"receive", 0}, {"ssl", -1}}}};
    if (e.streamed) entry["comment"] = "streamed response";
    list.push_back(entry);
  }
  if (!wsMessages.empty()) {
    list.push_back(Json{{"startedDateTime", isoTime(wsStart)}, {"time", 0}, {"_resourceType", "websocket"},
                        {"request", Json{{"method", "GET"}, {"url", wsUrl}, {"httpVersion", "HTTP/1.1"}, {"headers", Json::array()}, {"queryString", Json::array()}, {"cookies", Json::array()}, {"headersSize", -1}, {"bodySize", 0}}},
                        {"response", Json{{"status", 101}, {"statusText", "Switching Protocols"}, {"httpVersion", "HTTP/1.1"}, {"headers", Json::array()}, {"cookies", Json::array()},
                                          {"content", Json{{"size", 0}, {"mimeType", "x-unknown"}}}, {"redirectURL", ""}, {"headersSize", -1}, {"bodySize", 0}}},
                        {"cache", Json::object()}, {"timings", Json{{"send", 0}, {"wait", 0}, {"receive", 0}}}, {"_webSocketMessages", wsMessages}});
  }
  return out.dump(2);
}

std::string curlCommand(const Entry& e) {
  auto quote = [](const std::string& s) { return "'" + core::replaceAll(s, "'", "'\\''") + "'"; };
  std::string cmd = "curl -sS -X " + e.method + " " + quote(e.url) + " \\\n  -H 'Authorization: Bearer $OWUI_TOKEN'";
  if (!e.requestBody.empty() && !core::startsWith(e.requestBody, "<multipart")) {
    cmd += " \\\n  -H 'Content-Type: application/json' \\\n  --data " + quote(e.requestBody);
  }
  return cmd;
}

}  // namespace traffic
