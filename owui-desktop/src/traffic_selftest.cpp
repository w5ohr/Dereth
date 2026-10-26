// `Open WebUI --traffictest`: offline checks of the traffic inspector.
#include <cstdio>
#include <string>

#include "core/traffic.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
traffic::Entry http(const std::string& method, const std::string& url, long status, double start, double ms, const std::string& req = "", const std::string& res = "") {
  traffic::Entry e;
  e.kind = "http";
  e.method = method;
  e.url = url;
  const size_t scheme = url.find("://");
  const size_t slash = url.find('/', scheme + 3);
  e.host = url.substr(scheme + 3, slash - scheme - 3);
  e.path = slash == std::string::npos ? "/" : url.substr(slash, url.find('?') == std::string::npos ? std::string::npos : url.find('?') - slash);
  e.status = status;
  e.startMs = start;
  e.ms = ms;
  e.requestBody = req;
  e.responseBody = res;
  e.bytesOut = static_cast<int64_t>(req.size());
  e.bytesIn = static_cast<int64_t>(res.size());
  e.contentType = "application/json";
  return e;
}
traffic::Entry ws(const std::string& packet, bool outgoing, double start) {
  traffic::Entry e;
  e.kind = "ws";
  e.method = outgoing ? "send" : "recv";
  e.event = traffic::sioEvent(packet);
  e.responseBody = packet;
  e.startMs = start;
  return e;
}
}  // namespace

int runTrafficSelfTest() {
  using namespace traffic;
  printf("Open WebUI desktop traffic-inspector self-test\n\n");

  Log log(3);
  const int a = log.add(http("GET", "http://localhost:3001/api/config", 200, 1000, 12));
  log.add(http("POST", "http://localhost:3001/api/chat/completions", 200, 1010, 900));
  log.add(ws("2", false, 1020));
  log.add(ws("3", true, 1021));
  report("Log: seq runs up, the ring keeps the newest, dropped counted", a == 1 && log.size() == 3 && log.snapshot()[0].seq == 2 && log.dropped() == 1);
  log.setPaused(true);
  report("Log: paused adds are dropped (seq 0), clear empties", log.add(ws("2", false, 1030)) == 0 && log.size() == 3 && [&] { log.setPaused(false); log.clear(); return log.size() == 0 && log.dropped() == 0; }());

  const std::string masked = redact(R"({"token":"abc.def","api_key":"sk-1234567890abcdef","user":"dave","nested":{"Password":"hunter2"}} Authorization: Bearer eyJhbGciOi.x.y ?key=zzz&name=ok)");
  report("redact masks JSON secrets (any case), bearer values, sk- keys and form fields, keeps the rest",
         masked.find("\"token\":\"***\"") != std::string::npos && masked.find("\"api_key\":\"***\"") != std::string::npos && masked.find("\"Password\":\"***\"") != std::string::npos &&
             masked.find("Bearer ***") != std::string::npos && masked.find("?key=***&name=ok") != std::string::npos && masked.find("\"user\":\"dave\"") != std::string::npos &&
             masked.find("hunter2") == std::string::npos && masked.find("eyJ") == std::string::npos,
         masked);
  report("sioEvent names packets", sioEvent("2") == "ping" && sioEvent("3") == "pong" && sioEvent("0{\"sid\":\"x\"}") == "open" && sioEvent("40{\"token\":\"t\"}") == "connect" &&
                                       sioEvent("42[\"events\",{\"a\":1}]") == "event events" && sioEvent("427[\"x\"]") == "event x" && sioEvent("43[1]") == "ack" && sioEvent("1") == "close" &&
                                       sioEvent("") == "?" && sioEvent("9") == "?");
  const std::string big(20000, 'x');
  const std::string capped = cap(big);
  report("cap keeps the first 16 KB and says how much went", capped.size() < 16500 && capped.find("more not kept") != std::string::npos && cap("short") == "short" &&
                                                               cap(std::string(16383, 'a') + "\xC3\xA9", 16384).rfind("\xC3") == std::string::npos);

  std::vector<Entry> all{http("GET", "http://localhost:3001/api/config", 200, 1000, 10, "", "{\"a\":1}"),
                         http("POST", "http://localhost:3001/api/chat/completions?x=1&y=2", 200, 1010, 1000, "{\"model\":\"m\",\"token\":\"t\"}", "data: {}"),
                         http("GET", "https://api.openai.com/v1/models", 401, 1020, 300, "", ""),
                         http("POST", "http://localhost:3001/api/v1/chats/new", 0, 1030, 50),
                         ws("2", false, 1040), ws("3", true, 1041), ws("42[\"events\",{\"type\":\"status\"}]", false, 1050)};
  all[3].error = "timeout";
  all[1].streamed = true;
  report("filter: kind, heartbeats, needle on path/host/status/event", filter(all, "", 0, false).size() == 5 && filter(all, "", 0, true).size() == 7 && filter(all, "", 1, true).size() == 4 &&
                                                                          filter(all, "", 2, true).size() == 3 && filter(all, "openai", 0, true).size() == 1 && filter(all, "401", 0, true).size() == 1 &&
                                                                          filter(all, "events", 2, true).size() == 1 && filter(all, "chats", 0, false)[0].path == "/api/v1/chats/new");
  const Summary s = summarize(all);
  report("summarize: requests, failures (4xx and errors), bytes, p50/p95, slowest, ws counts",
         s.requests == 4 && s.failures == 2 && s.bytesOut == 25 && s.bytesIn == 15 && s.p50 == 300.0 && s.p95 == 1000.0 && s.slowest == "POST /api/chat/completions" && s.wsIn == 2 && s.wsOut == 1,
         std::to_string(s.failures) + " failures, p50 " + std::to_string(s.p50) + ", out " + std::to_string(s.bytesOut) + ", in " + std::to_string(s.bytesIn));

  double lo = 0, hi = 0;
  const std::vector<Bar> bars = waterfall(all, &lo, &hi);
  report("waterfall: window spans first start to last end, bars are fractions, packets are points",
         lo == 1000.0 && hi == 2010.0 && bars.size() == 7 && bars[0].x0 == 0.0f && bars[1].x1 == 1.0f && bars[4].x0 == bars[4].x1 && bars[2].x0 > bars[0].x1 &&
             waterfall({}, &lo, &hi).empty() && lo == 0.0 && [&] { double l, h; waterfall({ws("2", false, 5)}, &l, &h); return h == 6.0; }());

  const std::string h = har(all, "1.0", "ws://localhost:3001/ws/socket.io/?EIO=4&transport=websocket");
  Json parsed = Json::parse(h, nullptr, false);
  bool harOk = !parsed.is_discarded() && parsed["log"]["version"] == "1.2" && parsed["log"]["entries"].size() == 5;
  if (harOk) {
    const Json& e1 = parsed["log"]["entries"][1];
    const Json& wsEntry = parsed["log"]["entries"][4];
    harOk = e1["request"]["method"] == "POST" && e1["request"]["queryString"].size() == 2 && e1["request"]["queryString"][1]["name"] == "y" &&
            e1["request"]["postData"]["text"] == "{\"model\":\"m\",\"token\":\"***\"}" && e1["timings"]["wait"] == 1000.0 && e1["startedDateTime"] == "1970-01-01T00:00:01.010Z" &&
            e1["comment"] == "streamed response" && parsed["log"]["entries"][3]["response"]["statusText"] == "timeout" && wsEntry["_resourceType"] == "websocket" &&
            wsEntry["_webSocketMessages"].size() == 3 && wsEntry["_webSocketMessages"][1]["type"] == "send" && wsEntry["response"]["status"] == 101;
  }
  report("har: 1.2 with one entry per request plus a websocket entry, query split, bodies redacted, timings, ws messages", harOk, harOk ? "" : h.substr(0, 200));
  report("har: no entries is still valid", Json::parse(har({}, "1.0", ""), nullptr, false)["log"]["entries"].empty());

  const std::string c = curlCommand(all[1]);
  report("curlCommand: method, quoted url, token placeholder, JSON body", c.find("curl -sS -X POST 'http://localhost:3001/api/chat/completions?x=1&y=2'") == 0 &&
                                                                              c.find("Bearer $OWUI_TOKEN") != std::string::npos && c.find("--data '{\"model\":\"m\",\"token\":\"t\"}'") != std::string::npos &&
                                                                              curlCommand(all[0]).find("--data") == std::string::npos &&
                                                                              curlCommand(http("POST", "http://x/y", 200, 0, 0, "it's")).find("'it'\\''s'") != std::string::npos);
  report("isoTime / pretty", isoTime(0) == "1970-01-01T00:00:00.000Z" && isoTime(1761400000123.0) == "2025-10-25T13:46:40.123Z" && pretty("{\"a\":1}") == "{\n  \"a\": 1\n}" && pretty("data: x") == "data: x" &&
                                 pretty("{broken") == "{broken");

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
