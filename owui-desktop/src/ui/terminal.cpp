#include "ui/terminal.h"

#include <algorithm>

namespace term {
namespace {

constexpr size_t kMaxScrollback = 5000;

void appendUtf8(std::string* out, char32_t cp) {
  if (cp < 0x80) {
    out->push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out->push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out->push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out->push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

}  // namespace

Screen::Screen(int cols, int rows) : cols_(std::max(cols, 2)), rows_(std::max(rows, 1)) {
  scrollBottom_ = rows_ - 1;
  grid_.assign(static_cast<size_t>(rows_), blankLine());
}

Line Screen::blankLine() const { return Line(static_cast<size_t>(cols_)); }

void Screen::reset() {
  grid_.assign(static_cast<size_t>(rows_), blankLine());
  scrollback_.clear();
  x_ = y_ = 0;
  pendingWrap_ = false;
  attrs_ = Cell();
  scrollTop_ = 0;
  scrollBottom_ = rows_ - 1;
  alt_ = false;
  cursorVisible_ = true;
  appCursorKeys_ = false;
  state_ = State::Ground;
  dirty_ = true;
}

void Screen::resize(int cols, int rows) {
  cols = std::max(cols, 2);
  rows = std::max(rows, 1);
  if (cols == cols_ && rows == rows_) return;
  for (Line& line : grid_) line.resize(static_cast<size_t>(cols));
  for (Line& line : savedGrid_) line.resize(static_cast<size_t>(cols));
  // Fewer rows: the top lines move into the scrollback (main screen only).
  while (static_cast<int>(grid_.size()) > rows) {
    if (y_ > 0) {
      if (!alt_) scrollback_.push_back(grid_.front());
      grid_.erase(grid_.begin());
      --y_;
    } else {
      grid_.pop_back();
    }
  }
  while (static_cast<int>(grid_.size()) < rows) grid_.push_back(Line(static_cast<size_t>(cols)));
  while (scrollback_.size() > kMaxScrollback) scrollback_.pop_front();
  cols_ = cols;
  rows_ = rows;
  scrollTop_ = 0;
  scrollBottom_ = rows_ - 1;
  clampCursor();
  dirty_ = true;
}

void Screen::clampCursor() {
  x_ = std::clamp(x_, 0, cols_ - 1);
  y_ = std::clamp(y_, 0, rows_ - 1);
}

int Screen::param(size_t index, int fallback) const {
  if (index >= params_.size() || params_[index] <= 0) return fallback;
  return params_[index];
}

// ---------------------------------------------------------------- feeding

void Screen::feed(const std::string& bytes) {
  for (unsigned char byte : bytes) {
    // UTF-8 continuation bytes only matter in the ground state.
    if (state_ == State::Ground && utf8Left_ > 0) {
      if ((byte & 0xC0) == 0x80) {
        utf8Cp_ = (utf8Cp_ << 6) | (byte & 0x3F);
        if (--utf8Left_ == 0) put(utf8Cp_);
        continue;
      }
      utf8Left_ = 0;   // malformed; fall through and treat as a fresh byte
    }

    // C0 controls act in every state except OSC/DCS string collection.
    if (byte < 0x20 && state_ != State::Osc && state_ != State::Dcs) {
      switch (byte) {
        case 0x07: break;                       // BEL
        case 0x08: if (x_ > 0) --x_; pendingWrap_ = false; break;
        case 0x09: tab(); break;
        case 0x0A: case 0x0B: case 0x0C: lineFeed(); break;
        case 0x0D: carriageReturn(); break;
        case 0x1B: state_ = State::Escape; intermediate_.clear(); break;
        default: break;
      }
      continue;
    }

    switch (state_) {
      case State::Ground:
        if (byte < 0x80) {
          put(byte);
        } else if ((byte & 0xE0) == 0xC0) {
          utf8Cp_ = byte & 0x1F;
          utf8Left_ = 1;
        } else if ((byte & 0xF0) == 0xE0) {
          utf8Cp_ = byte & 0x0F;
          utf8Left_ = 2;
        } else if ((byte & 0xF8) == 0xF0) {
          utf8Cp_ = byte & 0x07;
          utf8Left_ = 3;
        }
        break;
      case State::Escape:
        if (byte == '[') {
          state_ = State::Csi;
          params_.clear();
          paramPending_ = false;
          privateCsi_ = false;
          intermediate_.clear();
        } else if (byte == ']') {
          state_ = State::Osc;
          osc_.clear();
        } else if (byte == 'P' || byte == 'X' || byte == '^' || byte == '_') {
          state_ = State::Dcs;
        } else if (byte >= 0x20 && byte <= 0x2F) {
          intermediate_.push_back(static_cast<char>(byte));
          state_ = State::EscapeIntermediate;
        } else {
          dispatchEscape(static_cast<char>(byte));
          state_ = State::Ground;
        }
        break;
      case State::EscapeIntermediate:
        // ESC ( B and friends: character-set selection, ignored.
        if (byte >= 0x30) state_ = State::Ground;
        break;
      case State::Csi:
        if (byte >= '0' && byte <= '9') {
          if (!paramPending_) {
            params_.push_back(0);
            paramPending_ = true;
          }
          params_.back() = params_.back() * 10 + (byte - '0');
        } else if (byte == ';') {
          if (!paramPending_) params_.push_back(0);
          paramPending_ = false;
        } else if (byte == '?' || byte == '>' || byte == '=' || byte == '!') {
          if (byte == '?') privateCsi_ = true;
        } else if (byte >= 0x20 && byte <= 0x2F) {
          intermediate_.push_back(static_cast<char>(byte));
        } else if (byte >= 0x40 && byte <= 0x7E) {
          dispatchCsi(static_cast<char>(byte));
          state_ = State::Ground;
        } else {
          state_ = State::Ground;
        }
        break;
      case State::Osc:
        if (byte == 0x07) {
          dispatchOsc();
          state_ = State::Ground;
        } else if (byte == 0x1B) {
          // ESC \ (ST) ends it; the backslash arrives as the next byte.
          dispatchOsc();
          state_ = State::Escape;
        } else {
          osc_.push_back(static_cast<char>(byte));
        }
        break;
      case State::Dcs:
        if (byte == 0x07) state_ = State::Ground;
        else if (byte == 0x1B) state_ = State::Escape;
        break;
    }
  }
}

void Screen::put(char32_t cp) {
  if (cp < 0x20) return;
  if (pendingWrap_) {
    if (autoWrap_) {
      x_ = 0;
      lineFeed();
    }
    pendingWrap_ = false;
  }
  Line& line = grid_[static_cast<size_t>(y_)];
  if (insertMode_ && x_ < cols_ - 1) {
    line.insert(line.begin() + x_, Cell());
    line.resize(static_cast<size_t>(cols_));
  }
  Cell& cell = line[static_cast<size_t>(x_)];
  cell = attrs_;
  cell.ch = cp;
  if (x_ < cols_ - 1) {
    ++x_;
  } else {
    pendingWrap_ = true;
  }
  dirty_ = true;
}

void Screen::tab() {
  pendingWrap_ = false;
  x_ = std::min(cols_ - 1, (x_ / 8 + 1) * 8);
}

void Screen::lineFeed() {
  pendingWrap_ = false;
  if (y_ == scrollBottom_) {
    scrollUp(1);
  } else if (y_ < rows_ - 1) {
    ++y_;
  }
  dirty_ = true;
}

void Screen::reverseIndex() {
  pendingWrap_ = false;
  if (y_ == scrollTop_) {
    scrollDown(1);
  } else if (y_ > 0) {
    --y_;
  }
  dirty_ = true;
}

void Screen::scrollUp(int n) {
  for (int i = 0; i < n; ++i) {
    if (scrollTop_ == 0 && !alt_) {
      scrollback_.push_back(grid_[0]);
      if (scrollback_.size() > kMaxScrollback) scrollback_.pop_front();
    }
    grid_.erase(grid_.begin() + scrollTop_);
    grid_.insert(grid_.begin() + scrollBottom_, blankLine());
  }
  dirty_ = true;
}

void Screen::scrollDown(int n) {
  for (int i = 0; i < n; ++i) {
    grid_.erase(grid_.begin() + scrollBottom_);
    grid_.insert(grid_.begin() + scrollTop_, blankLine());
  }
  dirty_ = true;
}

void Screen::eraseLine(int mode) {
  Line& line = grid_[static_cast<size_t>(y_)];
  Cell blank;
  blank.bg = attrs_.bg;
  const int from = mode == 0 ? x_ : 0;
  const int to = mode == 1 ? x_ + 1 : cols_;
  for (int i = from; i < to; ++i) line[static_cast<size_t>(i)] = blank;
  pendingWrap_ = false;
  dirty_ = true;
}

void Screen::eraseDisplay(int mode) {
  if (mode == 3) {
    scrollback_.clear();
    return;
  }
  if (mode == 2) {
    for (Line& line : grid_) line = blankLine();
  } else if (mode == 0) {
    eraseLine(0);
    for (int r = y_ + 1; r < rows_; ++r) grid_[static_cast<size_t>(r)] = blankLine();
  } else {
    eraseLine(1);
    for (int r = 0; r < y_; ++r) grid_[static_cast<size_t>(r)] = blankLine();
  }
  pendingWrap_ = false;
  dirty_ = true;
}

void Screen::insertLines(int n) {
  if (y_ < scrollTop_ || y_ > scrollBottom_) return;
  for (int i = 0; i < n; ++i) {
    grid_.erase(grid_.begin() + scrollBottom_);
    grid_.insert(grid_.begin() + y_, blankLine());
  }
  x_ = 0;
  dirty_ = true;
}

void Screen::deleteLines(int n) {
  if (y_ < scrollTop_ || y_ > scrollBottom_) return;
  for (int i = 0; i < n; ++i) {
    grid_.erase(grid_.begin() + y_);
    grid_.insert(grid_.begin() + scrollBottom_, blankLine());
  }
  x_ = 0;
  dirty_ = true;
}

void Screen::insertChars(int n) {
  Line& line = grid_[static_cast<size_t>(y_)];
  for (int i = 0; i < n && x_ < cols_; ++i) {
    line.insert(line.begin() + x_, Cell());
    line.pop_back();
  }
  dirty_ = true;
}

void Screen::deleteChars(int n) {
  Line& line = grid_[static_cast<size_t>(y_)];
  for (int i = 0; i < n && x_ < cols_; ++i) {
    line.erase(line.begin() + x_);
    line.push_back(Cell());
  }
  dirty_ = true;
}

void Screen::eraseChars(int n) {
  Line& line = grid_[static_cast<size_t>(y_)];
  for (int i = 0; i < n && x_ + i < cols_; ++i) line[static_cast<size_t>(x_ + i)] = Cell();
  dirty_ = true;
}

void Screen::enterAlt() {
  if (alt_) return;
  savedGrid_ = grid_;
  savedX_ = x_;
  savedY_ = y_;
  savedAttrs_ = attrs_;
  grid_.assign(static_cast<size_t>(rows_), blankLine());
  alt_ = true;
  dirty_ = true;
}

void Screen::leaveAlt() {
  if (!alt_) return;
  grid_ = savedGrid_;
  grid_.resize(static_cast<size_t>(rows_), blankLine());
  x_ = savedX_;
  y_ = savedY_;
  attrs_ = savedAttrs_;
  alt_ = false;
  clampCursor();
  dirty_ = true;
}

void Screen::setMode(bool set) {
  for (size_t i = 0; i < std::max<size_t>(params_.size(), 1); ++i) {
    const int mode = param(i, 0);
    if (privateCsi_) {
      switch (mode) {
        case 1: appCursorKeys_ = set; break;
        case 7: autoWrap_ = set; break;
        case 25: cursorVisible_ = set; dirty_ = true; break;
        case 47: case 1047: case 1049:
          if (set) {
            if (mode == 1049) { decX_ = x_; decY_ = y_; decAttrs_ = attrs_; }
            enterAlt();
            if (mode == 1049) { x_ = y_ = 0; }
          } else {
            leaveAlt();
            if (mode == 1049) { x_ = decX_; y_ = decY_; attrs_ = decAttrs_; clampCursor(); }
          }
          break;
        default: break;   // mouse reporting, bracketed paste, focus events: not supported
      }
    } else if (mode == 4) {
      insertMode_ = set;
    }
  }
}

void Screen::sgr() {
  if (params_.empty()) {
    attrs_ = Cell();
    return;
  }
  for (size_t i = 0; i < params_.size(); ++i) {
    const int p = params_[i];
    if (p == 0) attrs_ = Cell();
    else if (p == 1) attrs_.flags |= Bold;
    else if (p == 2) attrs_.flags |= Dim;
    else if (p == 4) attrs_.flags |= Underline;
    else if (p == 7) attrs_.flags |= Inverse;
    else if (p == 22) attrs_.flags &= static_cast<uint8_t>(~(Bold | Dim));
    else if (p == 24) attrs_.flags &= static_cast<uint8_t>(~Underline);
    else if (p == 27) attrs_.flags &= static_cast<uint8_t>(~Inverse);
    else if (p >= 30 && p <= 37) attrs_.fg = ansiColor(p - 30);
    else if (p == 39) attrs_.fg = kDefaultColor;
    else if (p >= 40 && p <= 47) attrs_.bg = ansiColor(p - 40);
    else if (p == 49) attrs_.bg = kDefaultColor;
    else if (p >= 90 && p <= 97) attrs_.fg = ansiColor(p - 90 + 8);
    else if (p >= 100 && p <= 107) attrs_.bg = ansiColor(p - 100 + 8);
    else if (p == 38 || p == 48) {
      uint32_t color = kDefaultColor;
      if (i + 1 < params_.size() && params_[i + 1] == 5 && i + 2 < params_.size()) {
        color = ansiColor(params_[i + 2]);
        i += 2;
      } else if (i + 1 < params_.size() && params_[i + 1] == 2 && i + 4 < params_.size()) {
        color = (static_cast<uint32_t>(params_[i + 2] & 0xFF) << 16) |
                (static_cast<uint32_t>(params_[i + 3] & 0xFF) << 8) |
                static_cast<uint32_t>(params_[i + 4] & 0xFF);
        i += 4;
      }
      (p == 38 ? attrs_.fg : attrs_.bg) = color;
    }
  }
}

void Screen::dispatchCsi(char final) {
  switch (final) {
    case 'A': y_ = std::max(scrollTop_ <= y_ ? scrollTop_ : 0, y_ - param(0, 1)); pendingWrap_ = false; break;
    case 'B': y_ = std::min(y_ <= scrollBottom_ ? scrollBottom_ : rows_ - 1, y_ + param(0, 1)); pendingWrap_ = false; break;
    case 'C': x_ = std::min(cols_ - 1, x_ + param(0, 1)); pendingWrap_ = false; break;
    case 'D': x_ = std::max(0, x_ - param(0, 1)); pendingWrap_ = false; break;
    case 'E': y_ = std::min(rows_ - 1, y_ + param(0, 1)); x_ = 0; pendingWrap_ = false; break;
    case 'F': y_ = std::max(0, y_ - param(0, 1)); x_ = 0; pendingWrap_ = false; break;
    case 'G': case '`': x_ = std::clamp(param(0, 1) - 1, 0, cols_ - 1); pendingWrap_ = false; break;
    case 'd': y_ = std::clamp(param(0, 1) - 1, 0, rows_ - 1); pendingWrap_ = false; break;
    case 'H': case 'f':
      y_ = std::clamp(param(0, 1) - 1, 0, rows_ - 1);
      x_ = std::clamp(param(1, 1) - 1, 0, cols_ - 1);
      pendingWrap_ = false;
      break;
    case 'J': eraseDisplay(param(0, 0)); break;
    case 'K': eraseLine(param(0, 0)); break;
    case 'L': insertLines(param(0, 1)); break;
    case 'M': deleteLines(param(0, 1)); break;
    case '@': insertChars(param(0, 1)); break;
    case 'P': deleteChars(param(0, 1)); break;
    case 'X': eraseChars(param(0, 1)); break;
    case 'S': scrollUp(param(0, 1)); break;
    case 'T': scrollDown(param(0, 1)); break;
    case 'r':
      scrollTop_ = std::clamp(param(0, 1) - 1, 0, rows_ - 1);
      scrollBottom_ = std::clamp(param(1, rows_) - 1, scrollTop_, rows_ - 1);
      x_ = y_ = 0;
      pendingWrap_ = false;
      break;
    case 's': decX_ = x_; decY_ = y_; decAttrs_ = attrs_; break;
    case 'u': x_ = decX_; y_ = decY_; attrs_ = decAttrs_; clampCursor(); break;
    case 'h': setMode(true); break;
    case 'l': setMode(false); break;
    case 'm': sgr(); break;
    default: break;   // DSR, DA, window ops, cursor style: not needed for display
  }
  dirty_ = true;
}

void Screen::dispatchEscape(char ch) {
  switch (ch) {
    case '7': decX_ = x_; decY_ = y_; decAttrs_ = attrs_; break;
    case '8': x_ = decX_; y_ = decY_; attrs_ = decAttrs_; clampCursor(); break;
    case 'D': lineFeed(); break;
    case 'E': carriageReturn(); lineFeed(); break;
    case 'M': reverseIndex(); break;
    case 'c': reset(); break;
    default: break;   // keypad modes, charset designations
  }
}

void Screen::dispatchOsc() {
  // "0;title" / "2;title"; everything else (colours, hyperlinks) is ignored.
  if (osc_.size() > 2 && (osc_[0] == '0' || osc_[0] == '2') && osc_[1] == ';') {
    title_ = osc_.substr(2);
  }
  osc_.clear();
}

// ---------------------------------------------------------------- helpers

std::string Screen::lineText(const Line& line) {
  std::string out;
  for (const Cell& cell : line) appendUtf8(&out, cell.ch ? cell.ch : U' ');
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

uint32_t Screen::ansiColor(int index) {
  static const uint32_t kBase[16] = {
      0x1e1e1e, 0xd2524d, 0x58c25c, 0xd7b448, 0x4a8fe0, 0xb66bd4, 0x3fb3c9, 0xd0d0d0,
      0x6a6a6a, 0xff6b66, 0x7ae07c, 0xf5d363, 0x6fa8ff, 0xd48ef0, 0x62d4e8, 0xffffff,
  };
  if (index < 0) return kDefaultColor;
  if (index < 16) return kBase[index];
  if (index < 232) {
    const int n = index - 16;
    const int r = n / 36, g = (n / 6) % 6, b = n % 6;
    auto level = [](int v) -> uint32_t { return v == 0 ? 0 : static_cast<uint32_t>(55 + v * 40); };
    return (level(r) << 16) | (level(g) << 8) | level(b);
  }
  if (index < 256) {
    const uint32_t grey = static_cast<uint32_t>(8 + (index - 232) * 10);
    return (grey << 16) | (grey << 8) | grey;
  }
  return kDefaultColor;
}

}  // namespace term
