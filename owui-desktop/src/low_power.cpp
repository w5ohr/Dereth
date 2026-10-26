// Low-power mode in the app: watch for input, streams and toasts to tell
// idle from active, ask the power source every half minute, and set the
// Metal view's frame rate from the policy; heavy background ticks ask
// deferHeavyWork() and wait for mains. Settings card with a live readout.
#include "app.h"
#include "core/common.h"
#include "core/lowpower.h"
#include "core/sysmetrics.h"
#include "native.h"
#include "pane_common.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

double gLastInputMs = 0.0;
double gLastPowerCheckMs = -1e9;
bool gOnBattery = false;
int gCurrentFps = 0;
double gIdleSeconds = 0.0;
bool gBusy = false;

lowpower::Policy policy() { return lowpower::fromJson(core::Settings::instance().extra); }

}  // namespace

bool App::deferHeavyWork() const { return lowpower::deferHeavyWork(policy(), gOnBattery); }

bool App::onBatteryPower() const { return gOnBattery; }

void App::tickLowPower() {
  const double now = core::nowMillis();
  const ImGuiIO& io = ImGui::GetIO();
  const bool input = io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f || io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f ||
                     ImGui::IsAnyMouseDown() || io.InputQueueCharacters.Size > 0 || ImGui::IsAnyItemActive() || io.WantTextInput;
  bool keyDown = false;
  for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END && !keyDown; ++key) {
    keyDown = ImGui::IsKeyDown(static_cast<ImGuiKey>(key));
  }
  if (input || keyDown || gLastInputMs == 0.0) gLastInputMs = now;
  if (now - gLastPowerCheckMs > 30000.0) {
    gLastPowerCheckMs = now;
    gOnBattery = sysmetrics::onBatteryNow();
  }
  gBusy = !streams_.empty() || widgets::toastsVisible();
  gIdleSeconds = (now - gLastInputMs) / 1000.0;
  const int fps = lowpower::targetFps(policy(), gIdleSeconds, gOnBattery, gBusy);
  if (fps != gCurrentFps) {
    gCurrentFps = fps;
    native::setPreferredFrameRate(fps);
  }
}

void App::drawLowPowerSection(float width) {
  card("lowpower", [&] {
    lowpower::Policy p = policy();
    bool changed = false;
    ImGui::TextUnformatted("Draw fewer frames when nothing is happening, fewer still on battery, and keep heavy background work for mains power.");
    changed |= ImGui::Checkbox("Low-power mode", &p.enabled);
    ImGui::SetNextItemWidth(200);
    changed |= ImGui::SliderInt("Idle after (seconds)", &p.idleAfterSec, 1, 120);
    ImGui::SetNextItemWidth(200);
    changed |= ImGui::SliderInt("Idle frame rate", &p.idleFps, 1, 60);
    ImGui::SetNextItemWidth(200);
    changed |= ImGui::SliderInt("On battery, at most", &p.batteryFps, 1, 60);
    changed |= ImGui::Checkbox("On battery, wait with chat indexing, vault and chat syncs and model warmup until plugged in", &p.deferOnBattery);
    if (changed) {
      settings().extra["lowPower"] = lowpower::toJson(p);
      settings().save();
    }
    widgets::dimText("Now: %s - drawing at %d fps - last input %.0f s ago - %s", lowpower::stateName(p, gIdleSeconds, gOnBattery, gBusy), gCurrentFps,
                     gIdleSeconds, gOnBattery ? "on battery" : "on mains power");
    widgets::dimText("Streams, toasts and downloads count as busy. The frame rate is a ceiling for the Metal view, so the app also runs cooler while you read.");
  }, width);
}

}  // namespace app
