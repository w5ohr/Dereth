// `Open WebUI --windowstest` (#121): the parts of in-process chat windows that
// run without a screen - views swapped in and out of App, async callbacks
// carried back to the window that asked, and two ImGui contexts sharing one
// font atlas with an accessibility tree each.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "app.h"
#include "core/async.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "ui/a11y_imgui.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}

void testViews() {
  app::App& app = app::App::instance();
  app.chat.id = "main-chat";
  app.composer = "typed in the main window";
  app.selectedModels = {"qwen3:8b"};
  app.sidebarVisible = true;
  app.followTail = false;   // the main window is parked mid-transcript

  app::ViewState* one = app.createView("chat-one", false);
  app::ViewState* two = app.createView("", true);
  report("a new view starts fresh but inherits the models", one->token != two->token && one->startChatId == "chat-one" && one->followTail &&
                                                             one->selectedModels == std::vector<std::string>{"qwen3:8b"} && one->composer.empty() &&
                                                             !one->wantsNewChat && two->wantsNewChat);
  report("a mini view has no sidebar and floats", two->miniWindow && two->floating && !two->sidebarVisible);

  bool sawFresh = false;
  app.runInView(one->token, [&] {
    sawFresh = app.chat.id.empty() && app.composer.empty() && app.followTail && app.startChatId == "chat-one" && app.inSecondaryView();
    app.chat.id = "chat-one";
    app.composer = "typed in window one";
  });
  report("inside a view, App's members are that window's", sawFresh);
  report("after it, the main window's are back untouched",
         app.chat.id == "main-chat" && app.composer == "typed in the main window" && !app.followTail && !app.inSecondaryView() && app.activeView() == 0);
  report("and the view kept what it changed", one->chat.id == "chat-one" && one->composer == "typed in window one");

  // Nested: from window one, act for window two, then carry on in one.
  std::string inner, afterInner;
  app.runInView(one->token, [&] {
    app.runInView(two->token, [&] {
      app.composer = "typed in window two";
      inner = app.composer;
    });
    afterInner = app.composer;
  });
  report("views nest: two inside one, then back to one", inner == "typed in window two" && afterInner == "typed in window one" &&
                                                           two->composer == "typed in window two" && app.composer == "typed in the main window");
  bool mainFromView = false;
  app.runInView(one->token, [&] { app.runInView(0, [&] { mainFromView = app.chat.id == "main-chat"; }); });
  report("token 0 is the main window, also from inside another view", mainFromView && app.chat.id == "main-chat");

  const uint64_t left = app.enterView(one->token);
  const std::string during = app.composer;
  app.leaveView(left);
  report("enterView / leaveView pair as runInView does", left == 0 && during == "typed in window one" && app.composer == "typed in the main window");

  auto stream = std::make_shared<app::Stream>();
  stream->messageId = "reply-1";
  one->streams_.push_back(stream);
  report("streams are counted across windows", app.streamCountAllViews() >= 1);
  const uint64_t gone = one->token;
  app.destroyView(gone);
  report("closing a window stops its streams and forgets it", stream->cancel.load() && app.findView(gone) == nullptr &&
                                                                 app.enterView(gone) == app::App::kNoView && !app.runInView(gone, [] {}));
  app.destroyView(two->token);
}

void testAsync() {
  async::start(2);
  std::atomic<int> ranIn{-1};
  std::vector<uint64_t> routed;
  async::setViewRunner([&](uint64_t token, const std::function<void()>& fn) {
    routed.push_back(token);
    if (token == 99) return false;   // a closed window
    fn();
    return true;
  });
  // A job queued from window 7 posts back; window 99's has closed; main posts plain.
  async::setCurrentView(7);
  async::run([&] {
    const uint64_t onWorker = async::currentView();
    async::post([&, onWorker] { ranIn = static_cast<int>(onWorker); });
  });
  async::setCurrentView(99);
  bool closedRan = false;
  async::post([&] { closedRan = true; });
  async::setCurrentView(0);
  bool mainRan = false;
  async::post([&] { mainRan = true; });
  async::setCurrentView(7);
  bool toMain = false;
  async::postMain([&] { toMain = true; });
  async::setCurrentView(0);
  for (int i = 0; i < 100 && ranIn.load() < 0; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    async::drain();
  }
  async::drain();
  const bool sevenRouted = std::count(routed.begin(), routed.end(), 7) == 1;
  report("a job keeps its window's token on the worker and its post runs in that window", ranIn.load() == 7 && sevenRouted,
         "ran in " + std::to_string(ranIn.load()));
  report("a post for a closed window is dropped", !closedRan && std::count(routed.begin(), routed.end(), 99) == 1);
  report("main-window posts and postMain skip the runner", mainRan && toMain && routed.size() == 2);
  async::setViewRunner(nullptr);
}

void frame(ImGuiContext* ctx, const char* button) {
  ImGui::SetCurrentContext(ctx);
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(400, 300);
  io.DeltaTime = 1.0f / 60.0f;
  ImGui::NewFrame();
  a11y::ui::beginFrame();
  ImGui::Begin("##root");
  ImGui::Button(button);
  ImGui::End();
  a11y::ui::endFrame();
  ImGui::Render();
}

void testContexts() {
  ImGuiContext* mainCtx = ImGui::CreateContext();
  ImGui::SetCurrentContext(mainCtx);
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
  io.Fonts->AddFontDefault();
  a11y::ui::install(mainCtx);
  ImFontAtlas* atlas = io.Fonts;
  ImGuiContext* chatCtx = ImGui::CreateContext(atlas);
  ImGui::SetCurrentContext(chatCtx);
  ImGui::GetIO().IniFilename = nullptr;
  ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
  a11y::ui::install(chatCtx);
  for (int i = 0; i < 3; ++i) {
    frame(mainCtx, "Main button");
    frame(chatCtx, "Chat window button");
  }
  report("two contexts share one font atlas", ImGui::GetIO().Fonts == atlas && mainCtx->IO.Fonts == atlas);
  const a11y::Snapshot& a = a11y::ui::snapshotFor(mainCtx);
  const a11y::Snapshot& b = a11y::ui::snapshotFor(chatCtx);
  auto has = [](const a11y::Snapshot& s, const std::string& label) {
    for (const auto& n : s.nodes) {
      if (n.label == label) return true;
    }
    return false;
  };
  report("each context has its own accessibility tree", has(a, "Main button") && !has(a, "Chat window button") &&
                                                          has(b, "Chat window button") && !has(b, "Main button"));
  ImGui::SetCurrentContext(chatCtx);
  a11y::ui::forget(chatCtx);
  ImGui::DestroyContext(chatCtx);
  ImGui::SetCurrentContext(mainCtx);
  frame(mainCtx, "Main button");
  report("closing one context leaves the other drawing with the shared fonts",
         ImGui::GetIO().Fonts == atlas && atlas->Fonts.Size > 0 && has(a11y::ui::snapshotFor(mainCtx), "Main button"));
  ImGui::DestroyContext(mainCtx);
}
}  // namespace

int runWindowsSelfTest() {
  printf("Open WebUI desktop windows (in-process chat windows) self-test\n\n");
  testViews();
  testAsync();
  testContexts();
  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
