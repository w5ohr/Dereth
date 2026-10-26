// Meeting notes (#115): system audio (and optionally the microphone) in, a
// live transcript, and running notes - summary, decisions, action items -
// kept up to date by the model every couple of minutes. This part is the
// logic: resampling capture audio to 16 kHz mono as it arrives, cutting it
// into chunks at pauses for the server's speech-to-text, the notes prompt,
// reading the notes back, and the Markdown that is saved. Transcript lines,
// WAV encoding and timestamps come from core/transcript (#114).
#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/transcript.h"

namespace meeting {

using Json = nlohmann::json;

// Interleaved float frames at any rate and channel count to 16 kHz mono,
// streaming: the fractional position carries across calls.
class Resampler {
 public:
  explicit Resampler(int inRate = 48000, int outRate = 16000) : inRate_(inRate), outRate_(outRate) {}
  void feed(const float* interleaved, size_t frames, int channels, std::vector<float>* out);
  int inRate() const { return inRate_; }

 private:
  int inRate_, outRate_;
  double pos_ = 0.0;      // next output sample's position in input frames, relative to the next feed
  float last_ = 0.0f;     // the previous call's last mono sample
  bool hasLast_ = false;
};

// Cuts a live 16 kHz stream into chunks: at the quietest pause after
// minSeconds, never longer than maxSeconds. Silence-only chunks are dropped.
struct Chunk {
  double start = 0.0;     // seconds since the stream began
  transcript::Pcm pcm;
};
class Chunker {
 public:
  Chunker(double minSeconds = 15.0, double maxSeconds = 30.0) : min_(minSeconds), max_(maxSeconds) {}
  void feed(const std::vector<float>& samples16k);
  bool next(Chunk* out);       // a chunk ready to transcribe
  bool flush(Chunk* out);      // whatever is left (at stop)
  double elapsed() const { return static_cast<double>(consumed_ + buffer_.size()) / 16000.0; }

 private:
  bool take(size_t n, Chunk* out);
  double min_, max_;
  std::vector<float> buffer_;
  size_t consumed_ = 0;        // samples already cut off the front
};
float rms(const float* samples, size_t n);
bool isSilent(const transcript::Pcm& pcm);   // nothing above the noise a quiet room makes

struct Notes {
  std::string summary;                     // markdown bullets
  std::vector<std::string> decisions;
  std::vector<std::string> actions;        // "Ana: send the deck (Friday)"
  bool empty() const { return summary.empty() && decisions.empty() && actions.empty(); }
};
// The chat messages that update the notes with the transcript so far.
Json notesMessages(const std::string& title, const Notes& previous, const std::string& transcriptSoFar, bool final);
Notes parseNotes(const std::string& markdown);
std::string notesMarkdown(const Notes& notes);
// The saved document: title, when, how long, the notes, then the transcript.
std::string document(const std::string& title, double startedAt, double seconds, const Notes& notes,
                     const std::vector<transcript::Line>& lines);
// Every two minutes with something new, or at the end.
bool notesDue(double secondsSinceLast, size_t newChars);

}  // namespace meeting
