#include "virtualize.h"

#include <cmath>

namespace virtualize {

void Heights::remember(const std::string& id, float height, float width, int frame) {
  Measured& m = map_[id];
  m.height = height;
  m.width = width;
  m.frame = frame;
}

float Heights::known(const std::string& id, float width) const {
  auto it = map_.find(id);
  if (it == map_.end()) return -1.0f;
  if (std::fabs(it->second.width - width) > 0.5f) return -1.0f;
  return it->second.height;
}

void Heights::forget(const std::string& id) { map_.erase(id); }
void Heights::clear() { map_.clear(); }

void Heights::prune(int frame, int maxAge) {
  for (auto it = map_.begin(); it != map_.end();) {
    if (frame - it->second.frame > maxAge) it = map_.erase(it);
    else ++it;
  }
}

bool shouldSkip(float top, float height, float viewTop, float viewBottom, float margin) {
  if (height <= 0.0f) return false;
  const float bottom = top + height;
  return bottom < viewTop - margin || top > viewBottom + margin;
}

std::string statsLine(const Stats& s) {
  char buf[96];
  snprintf(buf, sizeof(buf), "drew %d of %d messages in %.1f ms", s.drawn, s.drawn + s.skipped, s.ms);
  return buf;
}

}  // namespace virtualize
