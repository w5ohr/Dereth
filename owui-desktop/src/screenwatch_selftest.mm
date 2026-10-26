// `Open WebUI --screenwatchtest` (#112): the screen-watch logic (prompt,
// verdicts, change detection, the unchanged-window gate) and a real capture
// of a window this test opens itself - an app's own windows need no Screen
// Recording permission.
#import <Cocoa/Cocoa.h>

#include <cstdio>
#include <string>

#include "core/screenwatch.h"

namespace screenwatch {
struct WindowInfo {
  uint32_t id = 0;
  std::string owner, title;
  int pid = 0;
  bool own = false;
  float width = 0, height = 0;
};
std::vector<WindowInfo> listWindows();
bool captureWindow(uint32_t windowId, bool own, std::string* png, std::vector<uint8_t>* gray, std::string* error);
}

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }
}  // namespace

int runScreenWatchSelfTest() {
  printf("Open WebUI desktop screen watch self-test\n\n");
  using namespace screenwatch;
  Config c;
  c.question = "Has the build finished?";
  Json text = buildMessages(c, "Compiling foo.cpp\n[42/97]", "");
  const std::string user = text[1]["content"].get<std::string>();
  report("text mode: a system rule for YES/NO, then the question and the screen's text",
         text.size() == 2 && has(text[0]["content"].get<std::string>(), "YES or NO") && has(user, "Has the build finished?") && has(user, "[42/97]"));
  std::string longLog(20000, 'a');
  longLog += "BUILD SUCCEEDED";
  const std::string tail = buildMessages(c, longLog, "")[1]["content"].get<std::string>();
  report("a long screen keeps its end (where logs change)", has(tail, "BUILD SUCCEEDED") && has(tail, "[...]") && tail.size() < 13000);
  report("an empty screen is said to be empty", has(buildMessages(c, "  ", "")[1]["content"].get<std::string>(), "(no readable text)"));
  c.mode = Mode::Image;
  Json image = buildMessages(c, "", "iVBORw0KGgo=");
  report("image mode attaches the screenshot as a data URL",
         image[1]["content"].is_array() && image[1]["content"][1]["image_url"]["url"].get<std::string>() == "data:image/png;base64,iVBORw0KGgo=");

  Verdict v1 = parseVerdict("YES\nThe log ends with BUILD SUCCEEDED.");
  Verdict v2 = parseVerdict("**No**. Still compiling (42 of 97).");
  Verdict v3 = parseVerdict("<think>The user wants... it says done</think>\nYes - the terminal shows the prompt again");
  Verdict v4 = parseVerdict("I cannot tell from this.");
  Verdict v5 = parseVerdict("Yesterday's build is shown.");
  report("verdicts: plain, bold, after thinking", v1.known && v1.met && v1.note == "The log ends with BUILD SUCCEEDED." && v2.known && !v2.met &&
                                                     v2.note == "Still compiling (42 of 97)." && v3.known && v3.met && has(v3.note, "prompt again"),
         v1.note + " | " + v2.note + " | " + v3.note);
  report("no YES/NO is unclear, and 'Yesterday' is not a yes", !v4.known && !v4.met && !v5.known && !v5.met);

  report("text fingerprints ignore ticking clocks and spacing",
         textFingerprint("Uptime 12:04:31  waiting") == textFingerprint("Uptime 12:04:59 waiting") &&
             textFingerprint("waiting") != textFingerprint("finished"));
  std::vector<uint8_t> left(72), right(72);
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 9; ++x) {
      left[static_cast<size_t>(y * 9 + x)] = static_cast<uint8_t>(x * 20);
      right[static_cast<size_t>(y * 9 + x)] = static_cast<uint8_t>(200 - x * 20);
    }
  }
  report("image fingerprints tell a left-to-right ramp from its mirror", hammingDistance(imageFingerprint(left), imageFingerprint(right)) == 64 &&
                                                                         imageFingerprint(left) == imageFingerprint(left));

  Gate gate;
  bool first = gate.shouldAsk(7, Mode::Text, 3);
  bool second = gate.shouldAsk(7, Mode::Text, 3), third = gate.shouldAsk(7, Mode::Text, 3), fourth = gate.shouldAsk(7, Mode::Text, 3);
  bool fifth = gate.shouldAsk(7, Mode::Text, 3);
  bool changed = gate.shouldAsk(8, Mode::Text, 3);
  report("the gate asks first, skips an unchanged window, re-asks after the limit, and on any change",
         first && !second && !third && !fourth && fifth && changed);
  Gate pictures;
  pictures.shouldAsk(0b1111, Mode::Image, 5);
  report("images within two bits count as unchanged", !pictures.shouldAsk(0b1100, Mode::Image, 5) && pictures.shouldAsk(0xFF00, Mode::Image, 5));

  Check yes;
  yes.at = 1790000000;
  yes.verdict = v1;
  Check skipped;
  skipped.skipped = true;
  Check failed;
  failed.error = "the window is gone";
  report("history lines", has(describe(yes), "YES - The log ends") && has(describe(skipped), "unchanged") && has(describe(failed), "error: the window is gone"));
  report("intervals stay between 10 s and an hour", clampInterval(1) == 10 && clampInterval(99999) == 3600 && clampInterval(30) == 30);

  // A real capture of a window of our own.
  [NSApplication sharedApplication];
  NSWindow* window = [[NSWindow alloc] initWithContentRect:NSMakeRect(40, 40, 240, 160)
                                                 styleMask:NSWindowStyleMaskBorderless
                                                   backing:NSBackingStoreBuffered
                                                     defer:NO];
  window.backgroundColor = [NSColor colorWithSRGBRed:0.9 green:0.1 blue:0.1 alpha:1.0];
  [window orderFrontRegardless];
  [window display];
  [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.3]];
  bool listed = false;
  for (const WindowInfo& w : listWindows()) listed |= w.id == static_cast<uint32_t>(window.windowNumber) && w.own;
  std::string png, error;
  std::vector<uint8_t> gray;
  const bool captured = captureWindow(static_cast<uint32_t>(window.windowNumber), true, &png, &gray, &error);
  [window orderOut:nil];
  report("our own window is listed as ours", listed);
  report("our own window captures without Screen Recording (PNG and a 9x8 thumbnail)",
         captured && png.size() > 100 && png.compare(1, 3, "PNG") == 0 && gray.size() == 72, error);
  std::string otherError;
  std::string otherPng;
  std::vector<uint8_t> otherGray;
  const bool other = captureWindow(1, false, &otherPng, &otherGray, &otherError);
  report("another app's window without Screen Recording is refused with the settings path",
         CGPreflightScreenCaptureAccess() ? true : (!other && has(otherError, "Screen Recording")), otherError);
  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
