// Focus-aware notifications (#125): hold notifications while a Focus is on
// or you are in a meeting, and when you are free again deliver them as one
// digest instead of a burst. This part is the decision logic, fed with what
// the system says (Focus on, microphone or camera in use by another app, a
// calendar event now); notifications.mm gathers that and posts.
#pragma once

#include <string>
#include <vector>

namespace notifypolicy {

struct Busy {
  bool focus = false;     // a Focus mode is on (when we may know)
  bool meeting = false;   // microphone / camera in use by another app, or a calendar event now
  std::string why;        // "microphone in use", "Work Focus", ...
  bool any() const { return focus || meeting; }
};

struct Options {
  bool respectFocus = true;
  bool batchInMeetings = true;
  bool digest = true;              // one notification afterwards; else each held one, then
  bool urgentThroughMeetings = true;   // screen-watch alerts you asked for still come during a meeting
  double settleSeconds = 60.0;     // free this long before the digest (meetings flicker the mic)
};

enum class Kind { Reply, Watch, Plugin };
struct Item {
  double at = 0;
  Kind kind = Kind::Reply;
  std::string title, body, chatId;
};

enum class Decision { Deliver, Hold };

struct Digest {
  std::string title, body;
  std::vector<Item> items;
};

class Policy {
 public:
  Decision offer(const Item& item, const Busy& busy, const Options& options);
  // Call often. When the user has been free for settleSeconds and something
  // is held, returns true with what to deliver (a digest, or the items one by one).
  bool tick(const Busy& busy, double now, const Options& options, Digest* digest, std::vector<Item>* oneByOne);
  const std::vector<Item>& held() const { return held_; }
  void clear() { held_.clear(); }
  std::string lastWhy() const { return lastWhy_; }

 private:
  std::vector<Item> held_;
  bool wasBusy_ = false;
  double freeSince_ = -1;
  std::string lastWhy_;
};

Digest makeDigest(const std::vector<Item>& items, const std::string& why);

}  // namespace notifypolicy
