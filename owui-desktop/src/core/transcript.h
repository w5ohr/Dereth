// Audio transcription (#114): splitting audio into speech segments at pauses
// (energy-based, adaptive to the recording's noise floor), cutting each
// segment to a 16 kHz mono WAV for the server's speech-to-text, and the
// timestamped transcript - formatting, the line at a playback time, SRT /
// WebVTT / Markdown export. Decoding (AVFoundation) is in decodeAudio.
// --transcribetest covers it.
#pragma once

#include <string>
#include <vector>

namespace transcript {

struct Pcm {
  int rate = 16000;
  std::vector<float> samples;   // mono, -1..1
  double duration() const { return rate > 0 ? static_cast<double>(samples.size()) / rate : 0.0; }
};

// Any file AVFoundation reads (wav, aiff, m4a, mp3, caf, mp4 audio, ...),
// mixed to mono and resampled to `rate`. Empty with *error on failure.
Pcm decodeAudio(const std::string& path, int rate, std::string* error);

struct Span {
  double start = 0.0, end = 0.0;   // seconds
};
struct Options {
  double frame = 0.02;        // analysis frame, seconds
  double minSilence = 0.30;   // a pause this long can end a segment
  double minSegment = 1.5;    // shorter segments merge into a neighbour
  double maxSegment = 25.0;   // longer ones are cut at their quietest frame
  double pad = 0.15;          // kept either side of speech
};
std::vector<Span> segment(const Pcm& pcm, const Options& options = Options());

std::string wav16(const Pcm& pcm, double start, double end);   // 16-bit PCM WAV bytes of [start, end)
bool readWav16(const std::string& bytes, Pcm* out);              // our own WAVs back, for tests

struct Line {
  double start = 0.0, end = 0.0;
  std::string text;
  std::string error;   // this segment failed to transcribe
};
std::string clock(double seconds, bool millis = false);   // "1:05", "1:02:03"; millis: "00:01:05,250"
int lineAt(const std::vector<Line>& lines, double t);     // -1 before the first
std::string toSrt(const std::vector<Line>& lines);
std::string toVtt(const std::vector<Line>& lines);
std::string toMarkdown(const std::vector<Line>& lines, const std::string& title);
std::string plainText(const std::vector<Line>& lines);
std::string cleanText(const std::string& serverText);      // trims, collapses whitespace, drops "[BLANK_AUDIO]"-style tags
bool isAudioPath(const std::string& path);

}  // namespace transcript
