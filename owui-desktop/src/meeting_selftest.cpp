// `Open WebUI --meetingtest` (#115): resampling capture audio, cutting a live
// stream at pauses, the notes prompt and reading notes back, the saved document.
#include <cmath>
#include <cstdio>
#include <string>

#include "core/meeting.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }
std::vector<float> tone(double seconds, int rate, int channels, double hz, float amp = 0.3f) {
  std::vector<float> out(static_cast<size_t>(seconds * rate) * static_cast<size_t>(channels));
  for (size_t i = 0; i < out.size() / static_cast<size_t>(channels); ++i) {
    for (int c = 0; c < channels; ++c) out[i * static_cast<size_t>(channels) + static_cast<size_t>(c)] = amp * static_cast<float>(std::sin(2 * M_PI * hz * i / rate));
  }
  return out;
}
int crossings(const std::vector<float>& s) {
  int n = 0;
  for (size_t i = 1; i < s.size(); ++i) n += (s[i - 1] < 0) != (s[i] < 0);
  return n;
}
}  // namespace

int runMeetingSelfTest() {
  printf("Open WebUI desktop meeting notes self-test\n\n");
  using namespace meeting;
  // 1 s of a 440 Hz stereo tone at 48 kHz, fed whole and in odd pieces.
  const std::vector<float> stereo = tone(1.0, 48000, 2, 440.0);
  Resampler whole(48000, 16000);
  std::vector<float> a;
  whole.feed(stereo.data(), stereo.size() / 2, 2, &a);
  Resampler pieces(48000, 16000);
  std::vector<float> b;
  size_t at = 0;
  for (size_t step : {101u, 4096u, 7u, 9999u, 513u}) {
    while (at < stereo.size() / 2) {
      const size_t n = std::min(step, stereo.size() / 2 - at);
      pieces.feed(stereo.data() + at * 2, n, 2, &b);
      at += n;
      if (step != 513u) break;
    }
  }
  report("48 kHz stereo becomes 16 kHz mono: a second stays a second", std::abs(static_cast<int>(a.size()) - 16000) <= 2, std::to_string(a.size()) + " samples");
  report("the pitch is kept (440 Hz: about 880 zero crossings)", std::abs(crossings(a) - 880) <= 4, std::to_string(crossings(a)));
  report("feeding it in odd-sized pieces gives the same length", std::abs(static_cast<int>(b.size()) - static_cast<int>(a.size())) <= 2,
         std::to_string(b.size()));
  Resampler cd(44100, 16000);
  std::vector<float> c;
  const std::vector<float> mono441 = tone(2.0, 44100, 1, 300.0);
  cd.feed(mono441.data(), mono441.size(), 1, &c);
  report("44.1 kHz too", std::abs(static_cast<int>(c.size()) - 32000) <= 3, std::to_string(c.size()));

  // The chunker: speech-like tone with a pause.
  Chunker chunker(15.0, 30.0);
  Chunk chunk;
  chunker.feed(tone(12.0, 16000, 1, 200.0));
  report("under the minimum nothing is cut", !chunker.next(&chunk));
  chunker.feed(tone(6.0, 16000, 1, 200.0));
  std::vector<float> pause(16000, 0.0f);   // one second of quiet at 18 s
  chunker.feed(pause);
  chunker.feed(tone(5.0, 16000, 1, 200.0));
  const bool cut = chunker.next(&chunk);
  report("a chunk is cut inside the pause after the minimum", cut && chunk.start == 0.0 && chunk.pcm.duration() > 18.0 && chunk.pcm.duration() < 19.0,
         cut ? std::to_string(chunk.pcm.duration()) + " s" : "no cut");
  Chunker longTalk(15.0, 30.0);
  longTalk.feed(tone(41.0, 16000, 1, 200.0));
  const bool capped = longTalk.next(&chunk);
  report("talk with no pause is cut at the maximum", capped && std::abs(chunk.pcm.duration() - 30.0) < 0.01);
  report("the next chunk starts where that one ended", !longTalk.next(&chunk) && longTalk.flush(&chunk) && std::abs(chunk.start - 30.0) < 0.01 &&
                                                         std::abs(chunk.pcm.duration() - 11.0) < 0.01);
  Chunker quiet(15.0, 30.0);
  quiet.feed(std::vector<float>(16000 * 20, 0.0f));
  quiet.feed(tone(1.0, 16000, 1, 200.0, 0.0f));
  report("silence is dropped, not sent", !quiet.next(&chunk) && !quiet.flush(&chunk));
  transcript::Pcm hum;
  hum.samples = tone(1.0, 16000, 1, 50.0, 0.003f);
  report("a quiet room's hum counts as silence; speech does not", isSilent(hum) && !isSilent(transcript::Pcm{16000, tone(1.0, 16000, 1, 200.0)}));

  // Notes.
  Notes previous;
  previous.summary = "- Budget for Q1 reviewed";
  previous.actions = {"Ana: send the deck (Friday)"};
  const Json messages = notesMessages("Weekly sync", previous, std::string(30000, 'x') + " the vendor call is moved", false);
  const std::string user = messages[1]["content"].get<std::string>();
  report("the notes prompt carries the previous notes, the newest transcript, and the headings to use",
         has(messages[0]["content"].get<std::string>(), "## Action items") && has(user, "Ana: send the deck") && has(user, "vendor call is moved") &&
             has(user, "[earlier transcript summarised") && has(user, "Transcript so far") && user.size() < 26000);
  report("the last call says the meeting has ended", has(notesMessages("", Notes{}, "bye", true)[1]["content"].get<std::string>(), "The meeting has ended"));
  const Notes parsed = parseNotes(
      "<think>let me see</think>\n## Summary\n- Budget for Q1 reviewed\n* Hiring paused until March\n\n**Decisions:**\n1. Keep the vendor\n\n"
      "Action items\n- [ ] Ana: send the deck (Friday)\n- [x] Ben: book the room\n- (none yet)\n");
  report("notes read back: bullets, a bold heading, a numbered list, checkboxes, placeholders skipped",
         parsed.summary == "- Budget for Q1 reviewed\n- Hiring paused until March" && parsed.decisions == std::vector<std::string>{"Keep the vendor"} &&
             parsed.actions == std::vector<std::string>{"Ana: send the deck (Friday)", "Ben: book the room"},
         parsed.summary);
  const Notes empty = parseNotes("## Summary\n- (none yet)\n## Decisions\n- (none yet)\n## Action items\n- (none yet)");
  report("all placeholders is empty notes (the old notes stay)", empty.empty());
  report("the notes' Markdown reads back the same", parseNotes(notesMarkdown(parsed)).actions == parsed.actions && parseNotes(notesMarkdown(parsed)).summary == parsed.summary);
  std::vector<transcript::Line> lines{{0.0, 18.4, "Let's start with the budget.", ""}, {65.0, 80.0, "Ana will send the deck.", ""}, {80.0, 90.0, "", ""}};
  const std::string doc = document("Weekly sync", 1792852200, 905, parsed, lines);
  report("the saved document: title, when and how long, the notes, the timed transcript",
         has(doc, "# Weekly sync") && has(doc, "15:05") && has(doc, "## Action items\n- [ ] Ana: send the deck") && has(doc, "**1:05** Ana will send the deck.") &&
             !has(doc, "**1:20**"),
         doc.substr(0, 60));
  report("notes are due every two minutes with something new", notesDue(130, 500) && !notesDue(60, 5000) && !notesDue(300, 50));
  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
