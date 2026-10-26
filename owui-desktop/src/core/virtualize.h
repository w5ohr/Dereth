// Virtualised transcript (#64): messages far outside the view are not laid
// out at all - a spacer of their last measured height stands in - so a chat
// with thousands of messages scrolls like a short one. The pure parts: the
// height memory (per message, per content width) and the skip decision.
// App-free; --virtualtest covers it.
#pragma once

#include <map>
#include <string>

namespace virtualize {

struct Measured {
  float height = 0.0f;
  float width = 0.0f;   // the content width it was measured at
  int frame = 0;
};

class Heights {
 public:
  void remember(const std::string& id, float height, float width, int frame);
  float known(const std::string& id, float width) const;   // -1 when unknown or measured at another width
  void forget(const std::string& id);
  void clear();
  size_t size() const { return map_.size(); }
  // Drops entries not touched for `maxAge` frames (closed chats, deleted messages).
  void prune(int frame, int maxAge);

 private:
  std::map<std::string, Measured> map_;
};

// A block from `top` to `top + height` can be skipped when it lies wholly
// outside [viewTop - margin, viewBottom + margin].
bool shouldSkip(float top, float height, float viewTop, float viewBottom, float margin);

struct Stats {
  int drawn = 0, skipped = 0;
  double ms = 0.0;
};
std::string statsLine(const Stats& s);   // "drew 12 of 1500 messages in 1.8 ms"

}  // namespace virtualize
