#include "fade.h"

#include <algorithm>

namespace fade {

void Timeline::record(double now, size_t total) {
  if (!started_) {
    // What was already there when tracking began is shown at once.
    started_ = true;
    settled_ = total;
    total_ = total;
    return;
  }
  if (total < total_) {   // the text was replaced (a final authoritative copy): start over
    reset();
    record(now, total);
    return;
  }
  if (total > total_) marks_.push_back({now, total});
  total_ = total;
}

double Timeline::arrival(size_t index) const {
  if (index < settled_) return -1.0;
  const auto it = std::upper_bound(marks_.begin(), marks_.end(), index, [](size_t i, const std::pair<double, size_t>& m) { return i < m.second; });
  if (it == marks_.end()) return -2.0;   // not drawn before: arrives now
  return it->first;
}

float Timeline::alpha(size_t index, double now, double duration) const {
  const double t = arrival(index);
  if (t == -1.0) return 1.0f;
  if (t == -2.0) return 0.0f;
  if (duration <= 0) return 1.0f;
  const double x = std::clamp((now - t) / duration, 0.0, 1.0);
  return static_cast<float>(x * x * (3.0 - 2.0 * x));   // smoothstep
}

bool Timeline::fading(double now, double duration) const { return !marks_.empty() && now - marks_.back().first < duration; }

void Timeline::forget(double now, double keep) {
  size_t drop = 0;
  while (drop < marks_.size() && now - marks_[drop].first > keep) ++drop;
  if (drop == 0) return;
  settled_ = marks_[drop - 1].second;
  marks_.erase(marks_.begin(), marks_.begin() + static_cast<long>(drop));
}

void Timeline::reset() {
  settled_ = 0;
  marks_.clear();
  total_ = 0;
  started_ = false;
}

}  // namespace fade
