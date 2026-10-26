#include "notifypolicy.h"

#include <algorithm>
#include <set>

#include "core/common.h"

namespace notifypolicy {

Decision Policy::offer(const Item& item, const Busy& busy, const Options& o) {
  bool hold = false;
  if (busy.focus && o.respectFocus) hold = true;   // a Focus holds everything we send
  if (busy.meeting && o.batchInMeetings && !(o.urgentThroughMeetings && item.kind == Kind::Watch)) hold = true;
  if (!hold) return Decision::Deliver;
  held_.push_back(item);
  if (held_.size() > 100) held_.erase(held_.begin());
  lastWhy_ = busy.why;
  return Decision::Hold;
}

bool Policy::tick(const Busy& busy, double now, const Options& o, Digest* digest, std::vector<Item>* oneByOne) {
  const bool busyNow = (busy.focus && o.respectFocus) || (busy.meeting && o.batchInMeetings);
  if (busyNow) {
    wasBusy_ = true;
    freeSince_ = -1;
    if (!busy.why.empty()) lastWhy_ = busy.why;
    return false;
  }
  if (freeSince_ < 0) freeSince_ = now;
  if (held_.empty()) {
    wasBusy_ = false;
    return false;
  }
  if (now - freeSince_ < o.settleSeconds) return false;
  if (o.digest && held_.size() > 1) {
    *digest = makeDigest(held_, lastWhy_);
  } else {
    *oneByOne = held_;
  }
  held_.clear();
  wasBusy_ = false;
  return true;
}

Digest makeDigest(const std::vector<Item>& items, const std::string& why) {
  Digest d;
  d.items = items;
  const std::string l = core::lower(why);
  d.title = l.find("focus") != std::string::npos ? "While your Focus was on" : !why.empty() ? "While you were in a meeting" : "While you were busy";
  int replies = 0, watches = 0, others = 0;
  std::vector<std::string> names;
  std::set<std::string> seen;
  for (const Item& it : items) {
    if (it.kind == Kind::Reply) ++replies;
    else if (it.kind == Kind::Watch) ++watches;
    else ++others;
    if (!it.title.empty() && seen.insert(it.title).second) names.push_back(it.title);
  }
  std::vector<std::string> parts;
  if (replies) parts.push_back(std::to_string(replies) + (replies == 1 ? " reply finished" : " replies finished"));
  if (watches) parts.push_back(std::to_string(watches) + (watches == 1 ? " screen watch answered" : " screen watches answered"));
  if (others) parts.push_back(std::to_string(others) + (others == 1 ? " other alert" : " other alerts"));
  d.body = core::join(parts, ", ");
  if (!names.empty()) {
    std::string list;
    for (size_t i = 0; i < names.size() && i < 4; ++i) list += (i ? ", " : "") + names[i];
    if (names.size() > 4) list += " and " + std::to_string(names.size() - 4) + " more";
    d.body += ": " + list;
  }
  return d;
}

}  // namespace notifypolicy
