// macOS integration beyond the window: global hotkey, menu bar extra,
// Services, URL scheme, drag and drop, Spotlight, notifications with reply,
// Dock badge/progress, Quick Look, Live Text, Handoff, appearance, file
// watching and subprocesses. Everything here is callable from the UI thread;
// callbacks into the app are marshalled back through async::post by the caller.
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace native {

struct Callbacks {
  std::function<void()> quickAsk;                         // global hotkey / menu
  std::function<void()> newChat;
  std::function<void()> showWindow;
  std::function<void(const std::string& chatId)> openChat;
  std::function<void(const std::string& text)> askText;   // Services, URL scheme
  std::function<void(const std::vector<std::string>& paths)> dropFiles;
  std::function<void(const std::string& url)> openUrl;    // owui:// links
  std::function<void(const std::string& reply)> notificationReply;
  std::function<void(bool dark)> appearanceChanged;
  std::function<std::vector<std::pair<std::string, std::string>>()> recentChats;
};

void install(const Callbacks& callbacks);

// Global hotkey (Carbon, no accessibility permission needed). Returns false if
// the combination is taken.
bool registerHotkey(int keyCode, bool cmd, bool alt, bool ctrl, bool shift);
void unregisterHotkey();

// Menu bar extra.
void setStatusItemVisible(bool visible);
void refreshStatusMenu();

// Attach drag-and-drop to the main content view.
void enableDropOnMainWindow();

// Spotlight.
void indexChatForSpotlight(const std::string& chatId, const std::string& title,
                           const std::string& preview);
void removeChatFromSpotlight(const std::string& chatId);
void clearSpotlightIndex();

// Notifications. The "reply" category offers an inline text field.
void notifyWithReply(const std::string& title, const std::string& body,
                     const std::string& chatId);

// Dock.
void setDockBadge(const std::string& text);
void setDockProgress(double fraction);   // < 0 hides it

// Quick Look a local file.
void quickLook(const std::string& path);

// Live Text: recognise text in an encoded image (PNG/JPEG bytes).
std::string recognizeText(const std::string& imageBytes);

// Handoff: advertise the current chat so another device can continue it in
// the web UI.
void updateHandoff(const std::string& webUrl, const std::string& title);
void clearHandoff();

bool systemIsDark();
void setWindowTitle(const std::string& title);

// Folder watching (FSEvents). Returns a token for stop().
int watchFolder(const std::string& path, std::function<void(const std::vector<std::string>&)> onChange);
void stopWatching(int token);

// Run a command with a timeout, capturing both streams. Optionally sandboxed
// (no network, write access limited to `workdir`).
struct ProcessResult {
  int exitCode = -1;
  std::string output;
  bool timedOut = false;
};
ProcessResult runProcess(const std::vector<std::string>& argv, const std::string& workdir,
                         int timeoutSeconds, bool sandboxed);

std::string homeDirectory();
std::vector<std::string> chooseFolder();

}  // namespace native
