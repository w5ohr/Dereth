// Windows: chats in windows (and tabs) of their own, a floating mini window,
// float-on-top (#121).
//
// Every window is in this one process. The main window is App's own state; a
// chat window keeps the per-window members (OWUI_VIEW_MEMBERS) in a ViewState
// that is swapped into App around everything done for that window: its
// frames (frameView), its menu actions and composer keys (main.mm and
// native.mm go through runInView), and the network callbacks it started
// (async carries the window's token). Shared state - the chat list, models,
// settings, the socket, the mirror - is never swapped, so all windows see
// one list and one connection. Secondary *instances* (--chat / --mini from
// an older build or a script) still work.
#include "app.h"

#include "core/async.h"
#include "core/common.h"
#include "native.h"
#include "ui/widgets.h"

namespace app {

void App::swapView(ViewState& view) {
#define OWUI_VIEW_SWAP(name) std::swap(name, view.name);
  OWUI_VIEW_MEMBERS(OWUI_VIEW_SWAP)
#undef OWUI_VIEW_SWAP
}

ViewState* App::createView(const std::string& chatId, bool mini) {
  auto view = std::make_unique<ViewState>();
  view->token = nextViewToken_++;
  // A fresh view, as App starts: defaults that are not zero.
  view->pane = Pane::Chat;
  view->followTail = true;
  view->focusComposer = true;
  view->pendingScrollRestore = -1.0f;
  view->composerTextHeight = 46.0f;
  view->composerSyncedGeneration_ = -1;
  view->citationPopup = Json();
  view->lastPayload = Json::object();
  // What carries over from the window it came from.
  view->selectedModels = selectedModels;
  view->webSearchEnabled = webSearchEnabled;
  view->imageGenerationEnabled = imageGenerationEnabled;
  view->codeInterpreterEnabled = codeInterpreterEnabled;
  view->activeToolIds = activeToolIds;
  view->sidebarVisible = !mini && sidebarVisible;
  view->miniWindow = mini;
  view->floating = mini;
  view->startChatId = chatId;
  view->wantsNewChat = chatId.empty();
  ViewState* raw = view.get();
  views_[raw->token] = std::move(view);
  return raw;
}

ViewState* App::findView(uint64_t token) {
  auto it = views_.find(token);
  return it == views_.end() ? nullptr : it->second.get();
}

std::vector<uint64_t> App::viewTokens() const {
  std::vector<uint64_t> out;
  for (const auto& [token, view] : views_) out.push_back(token);
  return out;
}

size_t App::streamCountAllViews() const {
  size_t n = streams_.size();
  for (const auto& [token, view] : views_) n += view->streams_.size();
  return n;
}

uint64_t App::enterView(uint64_t token) {
  const uint64_t previous = activeView_;
  if (token == previous) return previous;
  ViewState* target = token ? findView(token) : nullptr;
  if (token && !target) return kNoView;   // that window has closed
  // Put back whichever window is in, then bring the target in.
  if (ViewState* current = previous ? findView(previous) : nullptr) swapView(*current);
  if (target) swapView(*target);
  activeView_ = token;
  async::setCurrentView(token);
  return previous;
}

void App::leaveView(uint64_t previous) {
  if (previous == kNoView || previous == activeView_) return;
  if (ViewState* current = activeView_ ? findView(activeView_) : nullptr) swapView(*current);
  if (ViewState* back = previous ? findView(previous) : nullptr) swapView(*back);
  activeView_ = previous;
  async::setCurrentView(previous);
}

bool App::runInView(uint64_t token, const std::function<void()>& fn) {
  const uint64_t previous = enterView(token);
  if (previous == kNoView) return false;
  fn();
  leaveView(previous);
  return true;
}

void App::frameView(uint64_t token, bool keyWindow) {
  runInView(token, [&] {
    ViewState* view = findView(token);
    if (!view) return;
    // Its first frame: open the chat it was made for, or a new one. (The
    // view's own fields are in App now; `view` holds what was swapped out.)
    if (!view->opened && signedIn_) {
      view->opened = true;
      if (view->wantsNewChat) newChat();
      else focusComposer = true;   // frame() opens startChatId
    }
    toastsHere = keyWindow;
    frame();
    toastsHere = true;
  });
}

void App::destroyView(uint64_t token) {
  ViewState* view = findView(token);
  if (!view) return;
  if (activeView_ == token) return;   // never from inside its own frame
  for (auto& stream : view->streams_) stream->cancel = true;
  views_.erase(token);
}

void App::openChatInNewWindow(const std::string& chatId, bool mini) {
  // In this process: a window of its own (or, with the Window menu's tab
  // preference, a tab). native::openChatWindow lives in main.mm.
  if (!native::openChatWindow(chatId, mini, false)) {
    widgets::toastError("Could not open a new window");
    return;
  }
}

void App::openChatInNewTab(const std::string& chatId) {
  if (!native::openChatWindow(chatId, false, true)) widgets::toastError("Could not open a new tab");
}

void App::toggleFloating() {
  floating = !floating;
  native::setFloating(floating);
  widgets::toastInfo(floating ? "This window now floats above others" : "This window no longer floats");
}

void App::applyWindowMode() {
  if (miniWindow) {
    sidebarVisible = false;
    floating = true;
    native::setFloating(true);
    native::setWindowSize(560, 760);
    native::cascadeWindow();
  } else if (secondaryInstance) {
    native::cascadeWindow();
  }
}

}  // namespace app
