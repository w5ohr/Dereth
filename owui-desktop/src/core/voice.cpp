#include "voice.h"

#include <algorithm>
#include <cctype>
#include <vector>

#include "core/common.h"

namespace voice {

void Vad::reset(double nowMs) {
  start_ = last_ = nowMs;
  speaking_ = false;
  aboveSince_ = belowSince_ = -1;
  speechMs_ = 0;
  // The floor is kept: the room is the same room between turns.
}

Event Vad::update(double levelDb, double nowMs) {
  const double dt = std::max(0.0, nowMs - last_);
  last_ = nowMs;
  levelDb = std::clamp(levelDb, -100.0, 0.0);
  // The floor follows quiet quickly and loud slowly, so speech does not drag it up.
  if (!floorSet_) {
    floor_ = levelDb;
    floorSet_ = true;
  } else if (!speaking_) {
    const double rate = levelDb < floor_ ? 0.3 : 0.01;
    floor_ += (levelDb - floor_) * std::min(1.0, rate * dt / 50.0);
  }
  floor_ = std::clamp(floor_, -90.0, -20.0);
  const bool above = levelDb > floor_ + o_.marginDb;
  const bool below = levelDb < floor_ + o_.marginDb * 0.5;
  Event event = Event::None;
  if (above) {
    belowSince_ = -1;
    if (aboveSince_ < 0) aboveSince_ = nowMs;
    if (!speaking_ && nowMs - aboveSince_ >= o_.onsetMs) {
      speaking_ = true;
      event = Event::SpeechStarted;
    }
  } else {
    aboveSince_ = -1;
    if (below && belowSince_ < 0) belowSince_ = nowMs;
  }
  if (speaking_) speechMs_ += above ? dt : 0.0;
  if (speaking_ && belowSince_ >= 0 && nowMs - belowSince_ >= o_.hangoverMs) {
    speaking_ = false;
    if (heardSpeech()) return Event::TurnEnded;
    // A blip: keep listening.
    speechMs_ = 0;
  }
  if (nowMs - start_ >= o_.maxTurnMs) return heardSpeech() ? Event::TooLong : Event::NothingSaid;
  if (!heardSpeech() && !speaking_ && nowMs - start_ >= o_.giveUpMs) return Event::NothingSaid;
  return event;
}

bool PushToTalk::press(double nowMs) {
  if (down) return false;   // key repeat
  down = true;
  downAt = nowMs;
  return true;
}

bool PushToTalk::release(double nowMs) {
  if (!down) return false;
  down = false;
  return nowMs - downAt >= minHoldMs;
}

std::string normalize(const std::string& text) {
  std::string out;
  bool space = false;
  for (unsigned char c : text) {
    if (std::isalnum(c)) {
      if (space && !out.empty()) out += ' ';
      space = false;
      out += static_cast<char>(std::tolower(c));
    } else if (c >= 0x80) {
      if (space && !out.empty()) out += ' ';
      space = false;
      out += static_cast<char>(c);   // keep non-ASCII letters as they are
    } else {
      space = true;
    }
  }
  return out;
}

namespace {

void replaceAll(std::string* s, const std::string& from, const std::string& to) {
  size_t at = 0;
  while ((at = s->find(from, at)) != std::string::npos) {
    s->replace(at, from.size(), to);
    at += to.size() - 1;   // the shared space can start the next match
  }
}

// Speech-to-text spellings of the app's name ("web you eye") and a few
// greetings, made one word each so a phrase matches however it was heard.
std::string canonical(const std::string& text) {
  std::string s = " " + normalize(text) + " ";
  for (const char* ui : {" web you eye ", " web you i ", " web u i ", " web ui ", " webby "}) replaceAll(&s, ui, " webui ");
  replaceAll(&s, " open webui ", " openwebui ");
  replaceAll(&s, " open web ", " openwebui ");   // the "ui" is often lost
  replaceAll(&s, " okay ", " ok ");
  replaceAll(&s, " hay ", " hey ");
  return core::trim(s);
}

std::vector<std::string> words(const std::string& s) {
  std::vector<std::string> out;
  for (const std::string& w : core::split(s, ' ')) {
    if (!w.empty()) out.push_back(w);
  }
  return out;
}

// Where the phrase's words appear in order (one stray word allowed between); -1 when not.
int find(const std::vector<std::string>& heard, const std::vector<std::string>& phrase, size_t* endAt) {
  if (phrase.empty()) return -1;
  for (size_t i = 0; i < heard.size(); ++i) {
    if (heard[i] != phrase[0]) continue;
    size_t h = i + 1, p = 1;
    int strays = 0;
    while (p < phrase.size() && h < heard.size()) {
      if (heard[h] == phrase[p]) {
        ++p;
        ++h;
      } else if (strays == 0) {
        ++strays;
        ++h;
      } else {
        break;
      }
    }
    if (p == phrase.size()) {
      *endAt = h;
      return static_cast<int>(i);
    }
  }
  return -1;
}

}  // namespace

bool wakeMatch(const std::string& heard, const std::string& phrase) {
  size_t end = 0;
  return find(words(canonical(heard)), words(canonical(phrase)), &end) >= 0;
}

std::string afterWake(const std::string& heard, const std::string& phrase) {
  const std::vector<std::string> h = words(canonical(heard));
  size_t end = 0;
  if (find(h, words(canonical(phrase)), &end) < 0) return "";
  std::string out;
  for (size_t i = end; i < h.size(); ++i) out += (out.empty() ? "" : " ") + h[i];
  return out;
}

}  // namespace voice
