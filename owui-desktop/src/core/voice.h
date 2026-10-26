// Push-to-talk and voice-activity detection (#119). A hold-to-talk hotkey
// records while held; voice mode and a wake phrase end a turn when the
// speaker stops. This part is the logic only: a detector that learns the
// room's noise floor and calls a turn over after a pause, the push-to-talk
// hold rules, and matching a wake phrase in what on-device recognition heard.
#pragma once

#include <string>

namespace voice {

struct VadOptions {
  double marginDb = 12.0;      // speech is this far above the noise floor
  double onsetMs = 150.0;      // ... for this long before it counts
  double hangoverMs = 1100.0;  // a pause this long ends the turn
  double minSpeechMs = 300.0;  // shorter is a cough, not a turn
  double maxTurnMs = 30000.0;  // a turn never runs longer
  double giveUpMs = 8000.0;    // nothing said for this long: stop listening
};

enum class Event { None, SpeechStarted, TurnEnded, TooLong, NothingSaid };

class Vad {
 public:
  explicit Vad(VadOptions options = VadOptions()) : o_(options) {}
  void reset(double nowMs);
  // One meter reading (dBFS, -160..0) at nowMs.
  Event update(double levelDb, double nowMs);
  double floorDb() const { return floor_; }
  bool speaking() const { return speaking_; }
  bool heardSpeech() const { return speechMs_ >= o_.minSpeechMs; }

 private:
  VadOptions o_;
  double start_ = 0, last_ = 0;
  double floor_ = -60.0;
  bool floorSet_ = false;
  bool speaking_ = false;
  double aboveSince_ = -1, belowSince_ = -1;
  double speechMs_ = 0;
};

// Push-to-talk: a press starts recording; the release sends unless the hold
// was too short to be speech (a tap by mistake).
struct PushToTalk {
  double minHoldMs = 300.0;
  bool down = false;
  double downAt = 0;
  bool press(double nowMs);          // true: start recording
  bool release(double nowMs);        // true: send what was recorded; false: discard
};

// Wake phrases: "hey open webui" in "hey open web you eye, what's the weather".
std::string normalize(const std::string& text);   // lower case, letters and digits, single spaces
bool wakeMatch(const std::string& heard, const std::string& phrase);
std::string afterWake(const std::string& heard, const std::string& phrase);   // what was said after it, "" when nothing

}  // namespace voice
