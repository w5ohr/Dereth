// `Open WebUI --voicetest` (#119): the turn detector on simulated meter
// readings, push-to-talk's hold rules, wake-phrase matching, and the global
// hotkey registering (no microphone is opened).
#include <cstdio>
#include <string>

#include "core/voice.h"
#include "native.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}

struct Run {
  double started = -1, ended = -1;
  voice::Event last = voice::Event::None;
};
// Feeds `level` dB for `ms` at 50 ms a reading; records the first start and end.
void feed(voice::Vad& vad, double& t, double level, double ms, Run& run) {
  for (double end = t + ms; t < end; t += 50.0) {
    const voice::Event e = vad.update(level, t);
    if (e == voice::Event::SpeechStarted && run.started < 0) run.started = t;
    if ((e == voice::Event::TurnEnded || e == voice::Event::TooLong || e == voice::Event::NothingSaid) && run.ended < 0) {
      run.ended = t;
      run.last = e;
    }
  }
}
}  // namespace

int runVoiceSelfTest() {
  printf("Open WebUI desktop push-to-talk and voice detection self-test\n\n");
  using namespace voice;
  {
    Vad vad;
    double t = 0;
    vad.reset(t);
    Run run;
    feed(vad, t, -52, 1000, run);    // a quiet room
    feed(vad, t, -20, 1500, run);    // talking
    const double floorWhileTalking = vad.floorDb();
    feed(vad, t, -52, 2000, run);    // stops
    report("speech starts about 150 ms in and the turn ends about 1.1 s after it stops",
           run.started >= 1100 && run.started <= 1250 && run.last == Event::TurnEnded && run.ended >= 3550 && run.ended <= 3750,
           "start " + std::to_string(static_cast<int>(run.started)) + " ms, end " + std::to_string(static_cast<int>(run.ended)) + " ms");
    report("talking does not drag the noise floor up", floorWhileTalking < -45.0, std::to_string(floorWhileTalking));
  }
  {
    Vad vad;
    double t = 0;
    vad.reset(t);
    Run run;
    feed(vad, t, -52, 800, run);
    feed(vad, t, -18, 200, run);     // a cough
    feed(vad, t, -52, 2500, run);
    report("a cough is not a turn: still listening", run.ended < 0, run.ended >= 0 ? "ended at " + std::to_string(run.ended) : "");
    feed(vad, t, -52, 6000, run);
    report("nothing said for 8 s gives up", run.last == Event::NothingSaid);
  }
  {
    Vad vad;
    double t = 0;
    vad.reset(t);
    Run run;
    feed(vad, t, -50, 500, run);
    feed(vad, t, -15, 31000, run);
    report("talk without a pause is cut at 30 s", run.last == Event::TooLong && run.ended >= 29900 && run.ended <= 30100);
  }
  {
    Vad vad;
    double t = 0;
    vad.reset(t);
    Run run;
    feed(vad, t, -34, 2000, run);    // a noisy room: fan, traffic
    const double floor = vad.floorDb();
    feed(vad, t, -30, 1500, run);    // a bit louder than the room is not speech
    const bool ignored = run.started < 0;
    feed(vad, t, -14, 1500, run);    // speech over it
    feed(vad, t, -34, 2000, run);
    report("in a noisy room the floor moves up and only real speech counts",
           floor > -36 && floor < -32 && ignored && run.started > 0 && run.last == Event::TurnEnded, std::to_string(floor) + " dB floor");
  }
  {
    VadOptions patient;
    patient.hangoverMs = 2500;
    Vad vad(patient);
    double t = 0;
    vad.reset(t);
    Run run;
    feed(vad, t, -50, 600, run);
    feed(vad, t, -20, 1000, run);
    feed(vad, t, -50, 1800, run);    // a thinking pause, shorter than 2.5 s
    const bool waited = run.ended < 0;
    feed(vad, t, -50, 1200, run);
    report("a longer pause setting waits through a thinking pause", waited && run.last == Event::TurnEnded);
  }

  PushToTalk ptt;
  const bool started = ptt.press(1000);
  const bool repeat = ptt.press(1100);
  const bool sentShort = ptt.release(1200);
  ptt.press(2000);
  const bool sentLong = ptt.release(3500);
  report("push-to-talk: a press starts, key repeat is ignored, a tap is discarded, a hold is sent",
         started && !repeat && !sentShort && sentLong && !ptt.release(4000));

  report("normalising: lower case, punctuation gone", normalize("Hey, Open-WebUI!  What's up?") == "hey open webui what s up");
  report("the wake phrase as speech-to-text spells it",
         wakeMatch("Hey Open Web UI, what's the weather in Lisbon?", "hey open webui") && wakeMatch("hey open web you eye", "Hey Open WebUI") &&
             wakeMatch("Okay, open web.", "ok open webui") && wakeMatch("hay open webui", "hey open webui"));
  report("one stray word is allowed, unrelated speech is not",
         wakeMatch("hey there open webui", "hey open webui") && !wakeMatch("open the window please", "hey open webui") &&
             !wakeMatch("hey you", "hey open webui"));
  report("what follows the phrase is the question",
         afterWake("Hey Open Web UI, what's the weather in Lisbon?", "hey open webui") == "what s the weather in lisbon" &&
             afterWake("hey open webui", "hey open webui").empty() && afterWake("Computer, lights on", "computer") == "lights on");

  // F19 with every modifier: nobody uses it, so registering it proves the call works.
  const bool registered = native::registerPushToTalkHotkey(80, true, true, true, true);
  native::unregisterPushToTalkHotkey();
  report("the hold-to-talk hotkey registers (Carbon, no Accessibility needed) and unregisters", registered);
  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
