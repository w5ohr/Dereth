// Warmup and residency in the app: load the selected Ollama model at launch,
// renew its keep-alive while the app is in front, release it when the Mac
// sleeps (and on quit when asked); the card under Settings; Warm now /
// Release now.
#include "app.h"
#include "core/async.h"
#include "core/common.h"
#include "core/http.h"
#include "core/warmup.h"
#include "pane_common.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

double gLastRenew = -1.0;
bool gBusy = false;
bool gSleeping = false;
bool gHeld = false;   // released by hand: stay released until Warm now or a wake
std::string gWarmModel;

warmup::Plan plan() { return warmup::fromJson(core::Settings::instance().extra); }

}  // namespace

std::string App::warmupModel() const {
  const std::string model = selectedModels.empty() ? "" : selectedModels.front();
  if (model.empty()) return "";
  // Only an Ollama-backed model can be pinned; a workspace model maps to its base.
  for (const ModelInfo& info : models) {
    if (info.id != model) continue;
    const bool ollama = info.raw.contains("ollama") || info.owner == "ollama";
    if (!ollama) return "";
    if (info.raw.contains("info") && info.raw["info"].is_object()) {
      const Json meta = info.raw["info"].value("meta", Json::object());
      if (meta.contains("base_model_id") && meta["base_model_id"].is_string() && !meta["base_model_id"].get<std::string>().empty()) {
        return meta["base_model_id"];
      }
    }
    return model;
  }
  return model;   // not in the list yet: assume Ollama
}

void App::warmModelNow(bool release) {
  const std::string model = release ? (gWarmModel.empty() ? warmupModel() : gWarmModel) : warmupModel();
  if (model.empty() || gBusy) {
    if (model.empty() && !release) warmupStatus = "No Ollama model selected - pick one in the chat header";
    return;
  }
  gBusy = true;
  const warmup::Plan p = plan();
  const Json payload = release ? warmup::releasePayload(model) : warmup::loadPayload(model, p.keepAliveMinutes);
  const std::string direct = settings().ollamaUrl;
  async::run([this, payload, model, release, direct, p] {
    api::Result res = api_.ollamaGenerate(payload);
    std::string error = res.ok ? "" : api::errorMessage(res);
    if (!res.ok && !direct.empty()) {
      net::Request req;
      req.method = "POST";
      req.url = direct + "/api/generate";
      req.body = payload.dump();
      req.timeoutSec = 300;
      net::Response r = net::perform(req);
      if (r.ok()) error.clear();
    }
    async::post([this, model, release, error, p] {
      gBusy = false;
      if (!error.empty()) {
        warmupStatus = (release ? "Release failed: " : "Warmup failed: ") + error;
        return;
      }
      if (release) {
        gWarmModel.clear();
        gLastRenew = -1.0;
        warmupStatus = model + " released at " + core::formatTime(core::nowSeconds(), "%H:%M") + (gHeld ? " - Warm now loads it again" : "");
      } else {
        gHeld = false;
        gWarmModel = model;
        gLastRenew = core::nowMillis();
        warmupStatus = model + " warm - renewed " + core::formatTime(core::nowSeconds(), "%H:%M") + ", keep-alive " + warmup::keepAliveText(p.keepAliveMinutes);
      }
    });
  });
}

void App::tickWarmup() {
  const warmup::Plan p = plan();
  if (!p.enabled || !signedIn_ || gBusy || gSleeping || gHeld || deferHeavyWork()) return;
  const bool frontmost = !ImGui::GetIO().AppFocusLost;
  const std::string model = warmupModel();
  if (model.empty()) return;
  static double firstTick = -1.0;
  const double now = core::nowMillis();
  if (firstTick < 0) {
    firstTick = now;
    return;
  }
  // The launch preload waits a few seconds for the model list, then runs once.
  if (gLastRenew < 0 && now - firstTick > 5000.0) {
    warmModelNow(false);
    return;
  }
  if (gLastRenew >= 0 && (model != gWarmModel || warmup::shouldRenew(gLastRenew, now, p, frontmost))) warmModelNow(false);
}

void App::onSleepWake(bool sleeping) {
  gSleeping = sleeping;
  const warmup::Plan p = plan();
  if (sleeping) {
    if (p.enabled && p.releaseOnSleep && !gWarmModel.empty()) warmModelNow(true);
  } else {
    gHeld = false;
    gLastRenew = -1.0;   // the next tick warms again
  }
}

void App::warmupOnQuit() {
  if (plan().enabled && plan().releaseOnQuit && !gWarmModel.empty()) {
    api_.ollamaGenerate(warmup::releasePayload(gWarmModel));   // blocking, once, on the way out
  }
}

void App::drawWarmupSection(float width) {
  card("warmup", [&] {
    warmup::Plan p = plan();
    bool changed = false;
    ImGui::TextUnformatted("Keep the model you use loaded so the first token comes at once.");
    changed |= ImGui::Checkbox("Load the selected Ollama model at launch and keep it resident", &p.enabled);
    ImGui::SetNextItemWidth(220);
    changed |= ImGui::SliderInt("Keep-alive (minutes)", &p.keepAliveMinutes, 1, 240);
    changed |= ImGui::Checkbox("Renew only while this app is in front", &p.onlyWhileFrontmost);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Release when the Mac sleeps", &p.releaseOnSleep);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Release on quit", &p.releaseOnQuit);
    if (changed) {
      settings().extra["warmup"] = warmup::toJson(p);
      settings().save();
      gHeld = false;
      if (!p.enabled) warmupStatus = "Off";
    }
    const std::string model = warmupModel();
    if (!warmupStatus.empty()) {
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - 40.0f);
      widgets::dimText("%s", warmupStatus.c_str());
      ImGui::PopTextWrapPos();
    }
    widgets::beginDisabled(gBusy || model.empty());
    if (widgets::primaryButton(gBusy ? "Working..." : "Warm now", ImVec2(110, 0))) warmModelNow(false);
    ImGui::SameLine();
    if (ImGui::Button("Release now", ImVec2(110, 0))) {
      gHeld = true;
      warmModelNow(true);
    }
    widgets::endDisabled(gBusy || model.empty());
    ImGui::SameLine();
    widgets::dimText(model.empty() ? "no Ollama model selected" : ("model: " + model).c_str());
    widgets::dimText("Off by default: pinning a model on a shared Ollama holds its memory for everyone. The renewal runs at half the keep-alive. Tools > Health shows what is resident.");
  }, width);
}

}  // namespace app
