#include "meeting.h"

#include <algorithm>
#include <cmath>
#include <ctime>

#include "core/common.h"

namespace meeting {

void Resampler::feed(const float* in, size_t frames, int channels, std::vector<float>* out) {
  if (frames == 0 || channels <= 0) return;
  auto mono = [&](size_t i) {
    float sum = 0.0f;
    for (int c = 0; c < channels; ++c) sum += in[i * static_cast<size_t>(channels) + static_cast<size_t>(c)];
    return sum / static_cast<float>(channels);
  };
  const double step = static_cast<double>(inRate_) / outRate_;
  // Positions are in input frames; -1 means the last sample of the previous call.
  while (pos_ < static_cast<double>(frames) - 1.0) {
    const double p = pos_;
    const long i = static_cast<long>(std::floor(p));
    const double frac = p - static_cast<double>(i);
    float a = i < 0 ? (hasLast_ ? last_ : mono(0)) : mono(static_cast<size_t>(i));
    float b = mono(static_cast<size_t>(i + 1));
    // A low-pass of sorts for 3:1: average the span each output covers.
    if (step >= 2.0 && i >= 0) {
      float acc = 0.0f;
      int n = 0;
      for (long k = i; k < i + static_cast<long>(step) && k < static_cast<long>(frames); ++k, ++n) acc += mono(static_cast<size_t>(k));
      out->push_back(n > 0 ? acc / static_cast<float>(n) : a);
    } else {
      out->push_back(static_cast<float>(a + (b - a) * frac));
    }
    pos_ += step;
  }
  pos_ -= static_cast<double>(frames);
  last_ = mono(frames - 1);
  hasLast_ = true;
}

float rms(const float* s, size_t n) {
  if (n == 0) return 0.0f;
  double acc = 0.0;
  for (size_t i = 0; i < n; ++i) acc += static_cast<double>(s[i]) * s[i];
  return static_cast<float>(std::sqrt(acc / static_cast<double>(n)));
}

bool isSilent(const transcript::Pcm& pcm) {
  // Any 100 ms window above -45 dBFS counts as sound.
  const size_t window = static_cast<size_t>(pcm.rate / 10);
  for (size_t i = 0; i + window <= pcm.samples.size(); i += window) {
    if (rms(pcm.samples.data() + i, window) > 0.0056f) return false;
  }
  return true;
}

void Chunker::feed(const std::vector<float>& samples) { buffer_.insert(buffer_.end(), samples.begin(), samples.end()); }

bool Chunker::take(size_t n, Chunk* out) {
  n = std::min(n, buffer_.size());
  if (n == 0) return false;
  out->start = static_cast<double>(consumed_) / 16000.0;
  out->pcm.rate = 16000;
  out->pcm.samples.assign(buffer_.begin(), buffer_.begin() + static_cast<long>(n));
  buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<long>(n));
  consumed_ += n;
  return true;
}

bool Chunker::next(Chunk* out) {
  for (;;) {
    const size_t minN = static_cast<size_t>(min_ * 16000.0), maxN = static_cast<size_t>(max_ * 16000.0);
    if (buffer_.size() < minN) return false;
    // The quietest 300 ms window between min and what we have (up to max).
    const size_t window = 4800, hop = 800;
    const size_t limit = std::min(buffer_.size(), maxN);
    size_t bestAt = 0;
    float best = 1e9f;
    for (size_t at = minN; at + window <= limit; at += hop) {
      const float e = rms(buffer_.data() + at, window);
      if (e < best) {
        best = e;
        bestAt = at + window / 2;
      }
    }
    // Wait for a pause unless the chunk is at its longest already.
    const bool pause = best < 0.01f;
    if (!pause && buffer_.size() < maxN) return false;
    const size_t cut = pause ? bestAt : maxN;
    if (!take(cut, out)) return false;
    if (!isSilent(out->pcm)) return true;
    // A silent chunk: drop it and look again.
  }
}

bool Chunker::flush(Chunk* out) {
  while (next(out)) return true;
  if (buffer_.size() < 8000) {   // under half a second is not worth a request
    consumed_ += buffer_.size();
    buffer_.clear();
    return false;
  }
  if (!take(buffer_.size(), out)) return false;
  return !isSilent(out->pcm);
}

std::string notesMarkdown(const Notes& notes) {
  std::string out = "## Summary\n" + (notes.summary.empty() ? std::string("- (nothing yet)") : notes.summary) + "\n\n## Decisions\n";
  if (notes.decisions.empty()) out += "- (none yet)\n";
  for (const std::string& d : notes.decisions) out += "- " + d + "\n";
  out += "\n## Action items\n";
  if (notes.actions.empty()) out += "- (none yet)\n";
  for (const std::string& a : notes.actions) out += "- [ ] " + a + "\n";
  return out;
}

Json notesMessages(const std::string& title, const Notes& previous, const std::string& transcriptSoFar, bool final) {
  const std::string system =
      "You take notes during a meeting from its live transcript (speech-to-text: expect slips and no speaker names). "
      "Answer in exactly three sections with these headings:\n## Summary\n## Decisions\n## Action items\n"
      "Summary: up to 8 short bullets of what was discussed. Decisions: bullets of what was agreed. "
      "Action items: one per line as '- [ ] who: what (when)', leaving out who or when if nobody said. "
      "Keep everything from the previous notes that still holds; never invent names, dates or decisions. "
      "Write '- (none yet)' under a heading with nothing.";
  std::string user = "Meeting: " + (title.empty() ? std::string("(untitled)") : title) + "\n\n";
  if (!previous.empty()) user += "Previous notes:\n" + notesMarkdown(previous) + "\n";
  std::string text = transcriptSoFar;
  constexpr size_t kMax = 24000;   // the notes carry the early part forward; keep the newest words
  if (text.size() > kMax) text = "[earlier transcript summarised in the previous notes]\n" + text.substr(text.size() - kMax);
  user += std::string(final ? "The meeting has ended. Final transcript" : "Transcript so far") + ":\n<<<\n" + text + "\n>>>";
  return Json::array({Json{{"role", "system"}, {"content", system}}, Json{{"role", "user"}, {"content", user}}});
}

Notes parseNotes(const std::string& markdownIn) {
  std::string markdown = markdownIn;
  for (;;) {
    const size_t a = markdown.find("<think>");
    if (a == std::string::npos) break;
    const size_t b = markdown.find("</think>", a);
    markdown.erase(a, b == std::string::npos ? std::string::npos : b + 8 - a);
  }
  Notes n;
  enum { None, Summary, Decisions, Actions } section = None;
  std::string summary;
  for (std::string line : core::split(markdown, '\n')) {
    std::string t = core::trim(line);
    if (t.empty()) continue;
    // A heading: "## Summary", "**Action items:**", "Decisions:".
    std::string h = core::lower(t);
    while (!h.empty() && (h[0] == '#' || h[0] == '*' || h[0] == ' ')) h.erase(0, 1);
    while (!h.empty() && (h.back() == '*' || h.back() == ':' || h.back() == ' ')) h.pop_back();
    if (h == "summary" || h == "decisions" || h == "decisions made" || h == "action items" || h == "actions" || h == "next steps" || h == "to-dos" || h == "todos") {
      section = h == "summary" ? Summary : h.rfind("decision", 0) == 0 ? Decisions : Actions;
      continue;
    }
    if (section == None) continue;
    // A bullet's text, without its marker or checkbox.
    std::string item = t;
    if (item.rfind("- ", 0) == 0 || item.rfind("* ", 0) == 0 || item.rfind("• ", 0) == 0) item = core::trim(item.substr(item[0] == '-' || item[0] == '*' ? 2 : 4));
    else if (item.size() > 2 && std::isdigit(static_cast<unsigned char>(item[0])) && (item[1] == '.' || item[1] == ')')) item = core::trim(item.substr(2));
    if (item.rfind("[ ]", 0) == 0 || item.rfind("[x]", 0) == 0 || item.rfind("[X]", 0) == 0) item = core::trim(item.substr(3));
    const std::string l = core::lower(item);
    if (item.empty() || l == "(none yet)" || l == "none" || l == "none yet" || l == "(none)" || l == "(nothing yet)") continue;
    if (section == Summary) summary += "- " + item + "\n";
    else if (section == Decisions) n.decisions.push_back(item);
    else n.actions.push_back(item);
  }
  if (!summary.empty()) summary.pop_back();
  n.summary = summary;
  return n;
}

std::string document(const std::string& title, double startedAt, double seconds, const Notes& notes, const std::vector<transcript::Line>& lines) {
  std::time_t t = static_cast<std::time_t>(startedAt);
  std::tm local{};
  localtime_r(&t, &local);
  char when[40];
  std::strftime(when, sizeof when, "%Y-%m-%d %H:%M", &local);
  std::string out = "# " + (title.empty() ? std::string("Meeting notes") : title) + "\n\n" + when + " - " + transcript::clock(seconds) + "\n\n";
  out += notesMarkdown(notes) + "\n## Transcript\n\n";
  for (const transcript::Line& l : lines) {
    if (l.text.empty()) continue;
    out += "**" + transcript::clock(l.start) + "** " + l.text + "\n\n";
  }
  return out;
}

bool notesDue(double secondsSinceLast, size_t newChars) { return newChars >= 300 && secondsSinceLast >= 120.0; }

}  // namespace meeting
