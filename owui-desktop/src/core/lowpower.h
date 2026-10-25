// Low-power mode (#140): the frame rate drops when nothing is happening,
// drops further on battery, and heavy background work (indexing, syncs,
// model warmup) waits for mains power. The pure policy; the app measures
// idleness and power and applies the result. App-free; --lowpowertest.
#pragma once

#include <nlohmann/json.hpp>

namespace lowpower {

using Json = nlohmann::json;

struct Policy {
  bool enabled = true;
  int idleAfterSec = 8;        // no input for this long -> idle
  int idleFps = 8;             // frame rate while idle (toasts, spinners and streams keep it high)
  int batteryFps = 30;         // ceiling while on battery, even when active
  bool deferOnBattery = true;  // indexing, vault sync, mirror sync, warmup renewals wait for mains
  int activeFps = 60;
};
Policy fromJson(const Json& extra);   // extra["lowPower"]
Json toJson(const Policy& policy);

// `busy` = a reply streams, a toast shows, a download runs: never idle then.
int targetFps(const Policy& policy, double idleSeconds, bool onBattery, bool busy);
bool deferHeavyWork(const Policy& policy, bool onBattery);
const char* stateName(const Policy& policy, double idleSeconds, bool onBattery, bool busy);

}  // namespace lowpower
