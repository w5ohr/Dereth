// Screen watch (#112): keep an eye on one window and tell the user when a
// standing question becomes true ("tell me when the build finishes"). Every
// so often the window is captured and either its text (read with Live Text,
// works with any model) or the screenshot itself (a vision model) goes to
// the model with the question; a YES stops the watch and says so.
//
// This part is the logic only: the prompt, reading the model's verdict, and
// deciding when a capture is worth a model call (an unchanged window is not,
// up to a limit). Capturing needs Screen Recording for other apps' windows;
// the app's own windows can always be captured.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace screenwatch {

using Json = nlohmann::json;

enum class Mode { Text, Image };

struct Config {
  uint32_t windowId = 0;
  std::string windowLabel;     // "Terminal - make", for the status line
  std::string question;        // "Has the build finished?"
  int intervalSec = 30;        // between captures; 10..3600
  Mode mode = Mode::Text;
  std::string model;
  bool stopWhenMet = true;
  int maxUnchangedSkips = 10;  // re-ask an unchanged window after this many skipped captures
};

struct Verdict {
  bool known = false;   // the reply said YES or NO
  bool met = false;
  std::string note;     // the model's one-line explanation
};

struct Check {
  double at = 0;
  Verdict verdict;
  bool skipped = false;   // unchanged since the last call: no model call made
  std::string error;
};

// The chat messages for one check. `screenText` is used in Text mode; in
// Image mode `pngBase64` is attached as an image.
Json buildMessages(const Config& config, const std::string& screenText, const std::string& pngBase64);
// "YES - the build log ends with BUILD SUCCEEDED" / "**No**. Still compiling." / <think>..</think> first.
Verdict parseVerdict(const std::string& reply);

// Fingerprints: an unchanged window gives the same one.
uint64_t textFingerprint(const std::string& text);   // whitespace and digits-in-clocks tolerant
// A 64-bit difference hash of an 9x8 grayscale thumbnail (row-major, 72 bytes).
uint64_t imageFingerprint(const std::vector<uint8_t>& gray9x8);
int hammingDistance(uint64_t a, uint64_t b);

// Should this capture go to the model? Changed, or unchanged for too long.
struct Gate {
  uint64_t lastSent = 0;
  bool hasLast = false;
  int skipsSinceSent = 0;
  bool shouldAsk(uint64_t fingerprint, Mode mode, int maxUnchangedSkips);
};

std::string describe(const Check& check);   // a line for the history list
int clampInterval(int seconds);

}  // namespace screenwatch
