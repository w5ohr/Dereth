// Windows: a chat in its own window, a floating mini window, float-on-top.
//
// A second window is a second instance of the app (`open -n` with --chat),
// so every window has the full client - composer, tools, streaming - without
// sharing one ImGui context between NSWindows. The server keeps the windows in
// step (chats are reloaded from it; the local mirror is per server and shared);
// secondary instances leave the global hotkeys, the menu bar extra and the
// settings file to the first one.
#include "app.h"

#include "core/common.h"
#include "native.h"
#include "ui/widgets.h"

namespace app {

void App::openChatInNewWindow(const std::string& chatId, bool mini) {
  std::vector<std::string> args;
  if (!chatId.empty()) {
    args.push_back("--chat");
    args.push_back(chatId);
  }
  if (mini) args.push_back("--mini");
  if (!native::openNewInstance(args)) {
    widgets::toastError("Could not open a new window");
    return;
  }
  widgets::toastInfo(mini ? "Opening a floating mini window" : "Opening in a new window");
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
