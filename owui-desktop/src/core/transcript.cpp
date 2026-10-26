#include "transcript.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <regex>

namespace transcript {
namespace {

void le16(std::string& s, uint16_t v) {
  s.push_back(static_cast<char>(v & 0xff));
  s.push_back(static_cast<char>(v >> 8));
}
void le32(std::string& s, uint32_t v) {
  for (int i = 0; i < 4; ++i) s.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
}
uint32_t rd32(const std::string& s, size_t at) {
  return static_cast<uint32_t>(static_cast<unsigned char>(s[at])) | static_cast<uint32_t>(static_cast<unsigned char>(s[at + 1])) << 8 |
         static_cast<uint32_t>(static_cast<unsigned char>(s[at + 2])) << 16 | static_cast<uint32_t>(static_cast<unsigned char>(s[at + 3])) << 24;
}

}  // namespace

std::vector<Span> segment(const Pcm& pcm, const Options& o) {
  std::vector<Span> out;
  const size_t hop = std::max<size_t>(1, static_cast<size_t>(o.frame * pcm.rate));
  const size_t frames = pcm.samples.size() / hop;
  if (frames == 0) return out;
  // RMS energy per frame, in dB.
  std::vector<double> db(frames);
  for (size_t f = 0; f < frames; ++f) {
    double sum = 0.0;
    for (size_t i = f * hop; i < (f + 1) * hop; ++i) sum += static_cast<double>(pcm.samples[i]) * pcm.samples[i];
    db[f] = 10.0 * std::log10(sum / hop + 1e-12);
  }
  // Threshold from the noise floor (10th percentile) and the loud level (95th).
  std::vector<double> sorted = db;
  std::sort(sorted.begin(), sorted.end());
  const double floor = sorted[sorted.size() / 10], loud = sorted[std::min(sorted.size() - 1, sorted.size() * 95 / 100)];
  if (loud - floor < 6.0) {   // no contrast: all speech or all silence
    if (loud > -55.0) out.push_back(Span{0.0, pcm.duration()});
    return out;
  }
  const double threshold = floor + (loud - floor) * 0.30;
  std::vector<bool> voiced(frames);
  for (size_t f = 0; f < frames; ++f) voiced[f] = db[f] > threshold;
  // Runs of voiced frames, joined across pauses shorter than minSilence.
  const size_t gap = static_cast<size_t>(o.minSilence / o.frame);
  std::vector<std::pair<size_t, size_t>> runs;   // [first, last] frames
  size_t f = 0;
  while (f < frames) {
    if (!voiced[f]) {
      ++f;
      continue;
    }
    size_t last = f, g = f;
    while (g < frames) {
      if (voiced[g]) {
        last = g;
        ++g;
      } else if (g - last <= gap) {
        ++g;
      } else {
        break;
      }
    }
    runs.push_back({f, last});
    f = last + 1;
  }
  // Long runs are cut at their quietest frame (a breath) until they fit.
  const size_t maxFrames = static_cast<size_t>(o.maxSegment / o.frame);
  std::vector<std::pair<size_t, size_t>> cut;
  for (auto r : runs) {
    while (r.second - r.first + 1 > maxFrames) {
      const size_t lo = r.first + maxFrames / 2, hi = r.first + maxFrames;
      size_t best = lo;
      for (size_t k = lo; k < hi; ++k) {
        if (db[k] < db[best]) best = k;
      }
      cut.push_back({r.first, best});
      r.first = best + 1;
    }
    cut.push_back(r);
  }
  // Short segments merge into the previous one when that stays under the cap.
  for (const auto& r : cut) {
    Span s{std::max(0.0, r.first * o.frame - o.pad), std::min(pcm.duration(), (r.second + 1) * o.frame + o.pad)};
    if (!out.empty() && ((s.end - s.start) < o.minSegment || (out.back().end - out.back().start) < o.minSegment) && s.end - out.back().start <= o.maxSegment) {
      out.back().end = s.end;
    } else if (!out.empty() && s.start < out.back().end) {
      const double mid = (s.start + out.back().end) / 2;   // padding overlaps: split the difference
      out.back().end = mid;
      s.start = mid;
      out.push_back(s);
    } else {
      out.push_back(s);
    }
  }
  return out;
}

std::string wav16(const Pcm& pcm, double start, double end) {
  const size_t a = static_cast<size_t>(std::max(0.0, start) * pcm.rate);
  const size_t b = std::min(pcm.samples.size(), static_cast<size_t>(std::max(start, end) * pcm.rate));
  const uint32_t n = static_cast<uint32_t>(b > a ? b - a : 0);
  std::string s = "RIFF";
  le32(s, 36 + n * 2);
  s += "WAVEfmt ";
  le32(s, 16);
  le16(s, 1);   // PCM
  le16(s, 1);   // mono
  le32(s, static_cast<uint32_t>(pcm.rate));
  le32(s, static_cast<uint32_t>(pcm.rate) * 2);
  le16(s, 2);
  le16(s, 16);
  s += "data";
  le32(s, n * 2);
  s.reserve(s.size() + n * 2);
  for (size_t i = a; i < b; ++i) le16(s, static_cast<uint16_t>(static_cast<int16_t>(std::lround(std::clamp(pcm.samples[i], -1.0f, 1.0f) * 32767.0f))));
  return s;
}

bool readWav16(const std::string& bytes, Pcm* out) {
  if (bytes.size() < 44 || bytes.compare(0, 4, "RIFF") != 0 || bytes.compare(8, 4, "WAVE") != 0) return false;
  out->rate = static_cast<int>(rd32(bytes, 24));
  const uint32_t n = rd32(bytes, 40) / 2;
  if (44 + static_cast<size_t>(n) * 2 > bytes.size()) return false;
  out->samples.resize(n);
  for (uint32_t i = 0; i < n; ++i) {
    const int16_t v = static_cast<int16_t>(static_cast<unsigned char>(bytes[44 + i * 2]) | static_cast<unsigned char>(bytes[45 + i * 2]) << 8);
    out->samples[i] = v / 32767.0f;
  }
  return true;
}

std::string clock(double seconds, bool millis) {
  if (seconds < 0) seconds = 0;
  const long total = static_cast<long>(seconds);
  const long h = total / 3600, m = (total / 60) % 60, s = total % 60;
  char buf[32];
  if (millis) {
    snprintf(buf, sizeof(buf), "%02ld:%02ld:%02ld,%03ld", h, m, s, static_cast<long>(std::lround((seconds - total) * 1000)) % 1000);
  } else if (h > 0) {
    snprintf(buf, sizeof(buf), "%ld:%02ld:%02ld", h, m, s);
  } else {
    snprintf(buf, sizeof(buf), "%ld:%02ld", m, s);
  }
  return buf;
}

int lineAt(const std::vector<Line>& lines, double t) {
  int at = -1;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (lines[i].start <= t + 1e-6) at = static_cast<int>(i);
    else break;
  }
  return at;
}

std::string toSrt(const std::vector<Line>& lines) {
  std::string out;
  int n = 0;
  for (const Line& l : lines) {
    if (l.text.empty()) continue;
    out += std::to_string(++n) + "\n" + clock(l.start, true) + " --> " + clock(l.end, true) + "\n" + l.text + "\n\n";
  }
  return out;
}

std::string toVtt(const std::vector<Line>& lines) {
  std::string out = "WEBVTT\n\n";
  for (const Line& l : lines) {
    if (l.text.empty()) continue;
    std::string a = clock(l.start, true), b = clock(l.end, true);
    std::replace(a.begin(), a.end(), ',', '.');
    std::replace(b.begin(), b.end(), ',', '.');
    out += a + " --> " + b + "\n" + l.text + "\n\n";
  }
  return out;
}

std::string toMarkdown(const std::vector<Line>& lines, const std::string& title) {
  std::string out = "# " + title + "\n\n";
  for (const Line& l : lines) {
    if (!l.text.empty()) out += "**[" + clock(l.start) + "]** " + l.text + "\n\n";
  }
  return out;
}

std::string plainText(const std::vector<Line>& lines) {
  std::string out;
  for (const Line& l : lines) {
    if (l.text.empty()) continue;
    if (!out.empty()) out += " ";
    out += l.text;
  }
  return out;
}

std::string cleanText(const std::string& serverText) {
  static const std::regex tags(R"(\[(BLANK_AUDIO|MUSIC|NOISE|SILENCE|INAUDIBLE)\]|\((silence|music|noise)\))", std::regex::icase);
  std::string t = std::regex_replace(serverText, tags, " ");
  std::string out;
  bool space = false;
  for (unsigned char c : t) {
    if (std::isspace(c)) {
      space = !out.empty();
      continue;
    }
    if (space) out += ' ';
    space = false;
    out += static_cast<char>(c);
  }
  return out;
}

bool isAudioPath(const std::string& path) {
  std::string ext = path.substr(path.find_last_of('.') == std::string::npos ? path.size() : path.find_last_of('.'));
  for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  static const char* kinds[] = {".wav", ".aif", ".aiff", ".m4a", ".mp3", ".caf", ".aac", ".flac", ".mp4", ".mov", ".m4b"};
  for (const char* k : kinds) {
    if (ext == k) return true;
  }
  return false;
}

}  // namespace transcript
