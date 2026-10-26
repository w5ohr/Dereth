// Warmup and residency (#77): keep the model you use loaded so the first
// token comes at once - load it at launch, renew its keep-alive while the
// app is in front, let it go when the Mac sleeps. The pure part: payloads
// and the renewal schedule. App-free; --warmuptest covers it.
#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace warmup {

using Json = nlohmann::json;

struct Plan {
  bool enabled = false;          // off by default: a shared Ollama should not be pinned by a client
  int keepAliveMinutes = 30;     // what each renewal asks Ollama for
  bool onlyWhileFrontmost = true;
  bool releaseOnSleep = true;
  bool releaseOnQuit = false;
};
Plan fromJson(const Json& extra);   // reads extra["warmup"]
Json toJson(const Plan& plan);

std::string keepAliveText(int minutes);           // "30m", "0" for release
Json loadPayload(const std::string& model, int keepAliveMinutes);   // /api/generate with an empty prompt
Json releasePayload(const std::string& model);                      // keep_alive 0

// Renew at half the keep-alive so it never lapses; never while hidden when
// onlyWhileFrontmost; never more often than once a minute.
bool shouldRenew(double lastRenewMs, double nowMs, const Plan& plan, bool frontmost);
double renewIntervalMs(const Plan& plan);

}  // namespace warmup
