// A small VT100/xterm screen emulator: feeds bytes from a pty into a grid of
// styled cells with a scrollback, the alternate screen, scroll regions and
// the SGR colours shells and TUIs use. It renders nothing itself; the
// terminal pane draws the grid with ImGui.
#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace term {

constexpr uint32_t kDefaultColor = 0xFFFFFFFFu;   // "the theme's default"

enum CellFlags : uint8_t { Bold = 1, Underline = 2, Inverse = 4, Dim = 8 };

struct Cell {
  char32_t ch = U' ';
  uint32_t fg = kDefaultColor;   // 0xRRGGBB
  uint32_t bg = kDefaultColor;
  uint8_t flags = 0;
};

using Line = std::vector<Cell>;

class Screen {
 public:
  Screen(int cols = 80, int rows = 24);

  void resize(int cols, int rows);
  void feed(const std::string& bytes);
  void reset();

  int cols() const { return cols_; }
  int rows() const { return rows_; }
  const Line& row(int y) const { return grid_[static_cast<size_t>(y)]; }
  const std::deque<Line>& scrollback() const { return scrollback_; }
  int cursorX() const { return x_; }
  int cursorY() const { return y_; }
  bool cursorVisible() const { return cursorVisible_; }
  bool appCursorKeys() const { return appCursorKeys_; }
  bool altScreen() const { return alt_; }
  const std::string& title() const { return title_; }
  // Set whenever the grid changed since the last clearDirty().
  bool dirty() const { return dirty_; }
  void clearDirty() { dirty_ = false; }

  // Plain text of one line (trailing blanks trimmed).
  static std::string lineText(const Line& line);
  // xterm's 256-colour palette entry as 0xRRGGBB.
  static uint32_t ansiColor(int index);

 private:
  enum class State { Ground, Escape, EscapeIntermediate, Csi, Osc, Dcs };

  void put(char32_t cp);
  void lineFeed();
  void reverseIndex();
  void carriageReturn() { x_ = 0; pendingWrap_ = false; }
  void tab();
  void scrollUp(int n);
  void scrollDown(int n);
  void eraseLine(int mode);
  void eraseDisplay(int mode);
  void insertLines(int n);
  void deleteLines(int n);
  void insertChars(int n);
  void deleteChars(int n);
  void eraseChars(int n);
  void setMode(bool set);
  void sgr();
  void dispatchCsi(char final);
  void dispatchEscape(char ch);
  void dispatchOsc();
  void enterAlt();
  void leaveAlt();
  int param(size_t index, int fallback) const;
  Line blankLine() const;
  void clampCursor();

  int cols_;
  int rows_;
  std::vector<Line> grid_;
  std::deque<Line> scrollback_;
  int x_ = 0;
  int y_ = 0;
  bool pendingWrap_ = false;
  bool autoWrap_ = true;
  bool cursorVisible_ = true;
  bool appCursorKeys_ = false;
  bool insertMode_ = false;
  int scrollTop_ = 0;
  int scrollBottom_;   // inclusive
  Cell attrs_;         // current SGR state (ch unused)
  std::string title_;
  bool dirty_ = true;

  // Alternate screen.
  bool alt_ = false;
  std::vector<Line> savedGrid_;
  int savedX_ = 0, savedY_ = 0;
  Cell savedAttrs_;
  // DECSC/DECRC.
  int decX_ = 0, decY_ = 0;
  Cell decAttrs_;

  // Parser.
  State state_ = State::Ground;
  std::vector<int> params_;
  bool paramPending_ = false;
  bool privateCsi_ = false;
  std::string intermediate_;
  std::string osc_;
  uint32_t utf8Cp_ = 0;
  int utf8Left_ = 0;
};

}  // namespace term
