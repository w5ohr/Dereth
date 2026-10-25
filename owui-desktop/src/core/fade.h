// Smooth streaming (#65): when each part of a streaming reply first appeared,
// so newly arrived words can fade in. Indices are positions in the rendered
// text (characters drawn so far, in drawing order), recorded once a frame.
// App-free; --fadetest covers it.
#pragma once

#include <cstddef>
#include <utility>
#include <vector>

namespace fade {

class Timeline {
 public:
  // After drawing at time `now`, `total` characters were on screen.
  void record(double now, size_t total);
  // When character `index` first appeared (-1 = before tracking began: fully shown).
  double arrival(size_t index) const;
  // 0 just arrived .. 1 fully shown, eased; `duration` in the same unit as `now`.
  float alpha(size_t index, double now, double duration) const;
  bool fading(double now, double duration) const;   // anything still below 1
  void forget(double now, double keep);              // drop history older than `keep`
  size_t total() const { return total_; }
  void reset();

 private:
  size_t settled_ = 0;                            // everything below this is fully shown
  std::vector<std::pair<double, size_t>> marks_;  // (time, total) rising
  size_t total_ = 0;
  bool started_ = false;
};

}  // namespace fade
