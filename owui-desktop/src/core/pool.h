// Multi-server pool (#75): several direct backends behind one virtual
// backend with health checks, least-loaded (or round-robin / first-alive)
// routing and failover. The pure parts: preferences, health records, the
// pick and the failover order. App-free; --pooltest covers it.
#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace pool {

using Json = nlohmann::json;

constexpr const char* kPoolId = "pool";

struct Prefs {
  bool enabled = false;
  std::vector<std::string> members;   // backend ids
  std::string strategy = "least-loaded";   // "least-loaded" | "round-robin" | "first-alive"
  int checkEverySec = 30;                  // 5 .. 600
  bool failover = true;                    // on an error before any output, try the next member
};
Prefs fromJson(const Json& extra);   // extra["backendPool"]
Json toJson(const Prefs& prefs);

struct Health {
  std::string id;
  bool alive = true;        // unknown counts as alive until a check says otherwise
  int running = 0;          // models loaded (Ollama /api/ps); 0 when the server can't say
  int inFlight = 0;         // requests this app has open against it
  double latencyMs = 0.0;   // the last check's round trip
  int64_t checkedAt = 0;    // unix seconds, 0 = never checked
  std::string error;
};
int runningFrom(const Json& response);   // Ollama /api/ps -> loaded model count; anything else 0

// The member to use for the next request ("" when none is alive), and the
// full order to try when failing over (the pick first).
std::string pick(const Prefs& prefs, const std::vector<Health>& health, int counter);
std::vector<std::string> order(const Prefs& prefs, const std::vector<Health>& health, int counter);
std::string describe(const Health& h);   // "alive - 2 models loaded, 1 in flight, 12 ms" / "down: ..."

}  // namespace pool
