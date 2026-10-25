// `Open WebUI --transcribetest`: segmentation on synthetic speech-like bursts,
// WAV cutting, the exports, and decoding real files made with `say` (WAV and
// AAC in an .m4a) through AVFoundation.
#include <spawn.h>
#include <sys/wait.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <random>
#include <string>

#include "core/transcript.h"

extern char** environ;
namespace fs = std::filesystem;

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
int sh(std::vector<std::string> args) {
  std::vector<char*> argv;
  for (auto& a : args) argv.push_back(a.data());
  argv.push_back(nullptr);
  pid_t pid;
  if (posix_spawn(&pid, args[0].c_str(), nullptr, nullptr, argv.data(), environ) != 0) return -1;
  int status = 0;
  waitpid(pid, &status, 0);
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
// Noise floor plus amplitude-modulated tone bursts at the given times.
transcript::Pcm bursts(const std::vector<std::pair<double, double>>& spans, double total) {
  transcript::Pcm p;
  p.rate = 16000;
  p.samples.resize(static_cast<size_t>(total * p.rate));
  std::mt19937 rng(7);
  std::normal_distribution<float> noise(0.0f, 0.003f);
  for (size_t i = 0; i < p.samples.size(); ++i) p.samples[i] = noise(rng);
  for (const auto& [a, b] : spans) {
    for (size_t i = static_cast<size_t>(a * p.rate); i < static_cast<size_t>(b * p.rate) && i < p.samples.size(); ++i) {
      const double t = static_cast<double>(i) / p.rate;
      p.samples[i] += static_cast<float>(0.3 * std::sin(2 * M_PI * 220 * t) * (0.6 + 0.4 * std::sin(2 * M_PI * 4 * t)));
    }
  }
  return p;
}
std::string spans(const std::vector<transcript::Span>& s) {
  std::string out;
  char buf[48];
  for (const auto& x : s) {
    snprintf(buf, sizeof(buf), "[%.2f-%.2f]", x.start, x.end);
    out += buf;
  }
  return out;
}
}  // namespace

int runTranscribeSelfTest() {
  using namespace transcript;
  printf("Open WebUI desktop transcription self-test\n\n");

  const Pcm three = bursts({{0.5, 3.0}, {4.0, 7.5}, {8.5, 11.0}}, 12.0);
  const auto s3 = segment(three);
  report("segment: three phrases with 1 s pauses become three spans around them", s3.size() == 3 && std::fabs(s3[0].start - 0.35) < 0.1 && std::fabs(s3[1].start - 3.85) < 0.1 && std::fabs(s3[2].end - 11.15) < 0.1, spans(s3));
  const Pcm breath = bursts({{0.5, 3.0}, {3.15, 6.0}}, 7.0);
  report("segment: a 0.15 s breath does not split a phrase", segment(breath).size() == 1, spans(segment(breath)));
  const Pcm tiny = bursts({{0.5, 3.0}, {3.6, 3.9}, {5.0, 8.0}}, 9.0);
  report("segment: a 0.3 s blip merges into a neighbour instead of standing alone", segment(tiny).size() == 2, spans(segment(tiny)));
  std::vector<std::pair<double, double>> longTalk;
  for (double t = 0.5; t < 59.0; t += 2.0) longTalk.push_back({t, t + 1.9});   // 0.1 s gaps: one run
  const auto sl = segment(bursts(longTalk, 60.0));
  bool capped = !sl.empty();
  for (const auto& x : sl) capped = capped && x.end - x.start <= 25.01;
  report("segment: a minute without pauses is cut into pieces of at most 25 s", capped && sl.size() >= 3, spans(sl));
  Pcm silent;
  silent.samples.assign(16000 * 3, 0.0f);
  report("segment: silence gives nothing; a constant tone gives one span", segment(silent).empty() && segment(bursts({{0.0, 5.0}}, 5.0)).size() == 1);

  const std::string wav = wav16(three, 4.0, 7.5);
  Pcm back;
  report("wav16: a 3.5 s cut is a 16-bit mono 16 kHz WAV that reads back", readWav16(wav, &back) && back.rate == 16000 && back.samples.size() == 56000 && wav.size() == 44 + 112000 && wav.compare(0, 4, "RIFF") == 0 &&
                                                                                 std::fabs(back.samples[1000] - three.samples[64000 + 1000]) < 0.001);

  std::vector<Line> lines{{0.0, 2.5, "Hello there.", ""}, {3.85, 7.65, "Second line, with a comma.", ""}, {65.25, 70.0, "", "timed out"}, {3725.5, 3727.0, "Late.", ""}};
  report("clock", clock(65.25) == "1:05" && clock(3725.5) == "1:02:05" && clock(65.25, true) == "00:01:05,250" && clock(0) == "0:00");
  report("lineAt", lineAt(lines, 0.0) == 0 && lineAt(lines, 5.0) == 1 && lineAt(lines, 1e6) == 3 && lineAt(lines, -1) == -1);
  report("toSrt numbers only lines with text", toSrt(lines) == "1\n00:00:00,000 --> 00:00:02,500\nHello there.\n\n2\n00:00:03,850 --> 00:00:07,650\nSecond line, with a comma.\n\n3\n01:02:05,500 --> 01:02:07,000\nLate.\n\n");
  report("toVtt uses dots and a header", toVtt(lines).rfind("WEBVTT\n\n00:00:00.000 --> 00:00:02.500\nHello there.", 0) == 0);
  report("toMarkdown / plainText / cleanText", toMarkdown(lines, "Talk").find("**[1:02:05]** Late.") != std::string::npos && plainText(lines) == "Hello there. Second line, with a comma. Late." &&
                                                  cleanText("  [BLANK_AUDIO]  so  it\n begins (music) ") == "so it begins");
  report("isAudioPath", isAudioPath("/a/b.M4A") && isAudioPath("x.wav") && !isAudioPath("x.txt") && !isAudioPath("noext"));

  // Real files from `say`.
  const fs::path dir = fs::temp_directory_path() / "owui-transcribetest";
  fs::remove_all(dir);
  fs::create_directories(dir);
  const std::string text = "The lens is polished every Monday. [[slnc 1200]] The foghorn is tested monthly. [[slnc 1200]] Visitors climb the tower on weekends.";
  const std::string wavPath = (dir / "say.wav").string(), m4aPath = (dir / "say.m4a").string();
  const bool made = sh({"/usr/bin/say", "-o", wavPath, "--data-format=LEI16@22050", text}) == 0 && sh({"/usr/bin/say", "-o", m4aPath, "--data-format=aac", text}) == 0;
  std::string err;
  const Pcm w = decodeAudio(wavPath, 16000, &err);
  const Pcm m = decodeAudio(m4aPath, 16000, &err);
  report("decodeAudio: say's 22 kHz WAV and AAC .m4a both decode to 16 kHz mono with matching lengths", made && w.rate == 16000 && w.duration() > 5.0 && m.duration() > 5.0 && std::fabs(w.duration() - m.duration()) < 0.2,
         std::to_string(w.duration()) + " s / " + std::to_string(m.duration()) + " s " + err);
  const auto sw = segment(w);
  report("segment on real speech: the two 1.2 s pauses give three sentences", sw.size() == 3, spans(sw));
  report("decodeAudio reports unreadable files", decodeAudio((dir / "missing.wav").string(), 16000, &err).samples.empty() && !err.empty());
  fs::remove_all(dir);

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
