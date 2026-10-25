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
  std::function<void()> captureScreen;                    // capture hotkey / menu bar extra
};

void install(const Callbacks& callbacks);

// Global hotkey (Carbon, no accessibility permission needed). Returns false if
// the combination is taken.
bool registerHotkey(int keyCode, bool cmd, bool alt, bool ctrl, bool shift);
void unregisterHotkey();
// Second global hotkey, for screen capture (fires Callbacks::captureScreen).
bool registerCaptureHotkey(int keyCode, bool cmd, bool alt, bool ctrl, bool shift);
void unregisterCaptureHotkey();

// Runs the system screen capture (the same UI as Shift+Cmd+4: drag a region,
// or press Space for a window) and returns the PNG. False when cancelled.
// Blocks until the user finishes, so call it from a worker. macOS asks for
// Screen Recording permission the first time.
bool captureScreen(bool interactive, std::string* png);
void showMainWindow();

// Windows. openNewInstance starts another copy of this app with `args`
// (`open -n`), so a chat can live in its own window.
bool openNewInstance(const std::vector<std::string>& args);
void setFloating(bool floating);       // float above other apps' windows
void setWindowSize(float width, float height);
void cascadeWindow();                   // offset from the window already on screen

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

// Native composer: an NSTextView floated over the ImGui composer box so the
// input gets IME/CJK, dictation, spellcheck, the emoji picker and trackpad
// selection. The chat pane positions it every frame in ImGui screen
// coordinates (points, y down); App::frame hides it when the pane isn't drawn.
struct ComposerCallbacks {
  std::function<void()> send;              // Enter (Shift+Enter is a newline), or Cmd+Enter
  std::function<void()> escape;            // Esc: keyboard goes back to ImGui
  std::function<void()> tab;               // Tab: slash-prompt completion
  std::function<bool(int direction)> navigate;   // Option+Up (-1) / Option+Down (+1); false = nothing to walk
  std::function<bool()> pasteImage;        // return true when the paste was an image
  std::function<bool(int direction)> history;          // plain Up/Down: -1 older, +1 newer; false = move the caret
  std::function<bool(const std::string&)> pasteText;   // return true when the text was taken elsewhere
};
void composerInstall(const ComposerCallbacks& callbacks);
void composerSetFrame(float x, float y, float width, float height);
void composerSetVisible(bool visible);
void composerSetText(const std::string& text);
std::string composerText();
bool composerFocus();                      // false when the view can't take focus yet
bool composerIsFocused();
float composerContentHeight();             // laid-out text height in points
void composerSetStyle(unsigned textColor, unsigned placeholderColor, unsigned accent,
                      float fontSize, bool sendOnEnter);

// Artifact pane: a WKWebView floated over the ImGui artifact column, the same
// way the composer is. Console output and page errors are collected through
// an injected script so the column can show them.
void artifactInstall();
void artifactSetFrame(float x, float y, float width, float height);
void artifactSetVisible(bool visible);
void artifactLoad(const std::string& html);     // replaces the document, clears the console
void artifactReload();
std::vector<std::string> artifactConsole();     // messages since the last load, oldest first

// Offscreen renderer: loads `html` in a hidden WKWebView `width` points
// wide, waits for the page to call owuiDone (height, or "error:...") and
// returns a PNG at the screen's scale. Blocks the calling worker up to
// `timeoutMs`; "" on error or timeout. Jobs run one at a time.
std::string renderHtmlToPng(const std::string& html, float width, int timeoutMs);

// Document pane: a PDFKit view floated over the ImGui document column, used
// to show the PDF behind a citation with the cited passage highlighted.
void pdfInstall();
void pdfSetFrame(float x, float y, float width, float height);
void pdfSetVisible(bool visible);
bool pdfLoad(const std::string& bytes);         // false when PDFKit can't parse it
int pdfPageCount();
int pdfCurrentPage();                           // 1-based
void pdfGoToPage(int page);                     // 1-based
// Finds `passage` (whitespace-insensitive, shortening from the front until
// something matches), highlights it and scrolls to it. Returns the page it
// was found on, or 0.
int pdfHighlight(const std::string& passage, int preferredPage);
void pdfClearHighlights();

}  // namespace native
