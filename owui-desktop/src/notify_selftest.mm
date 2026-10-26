// `Open WebUI --notifytest` (#125): which notifications are held during a
// Focus or a meeting, the settle time, the digest, and reading this Mac's
// busy signals (microphone, camera, Focus) without asking for anything.
#include <cstdio>
#include <string>

#include "core/notifypolicy.h"

namespace busy {
bool microphoneInUse();
bool cameraInUse();
int focusOn();
const char* focusAccess();
}

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
}  // namespace

int runNotifySelfTest() {
  printf("Open WebUI desktop focus-aware notifications self-test\n\n");
  using namespace notifypolicy;
  Options o;
  Busy free;
  Busy meeting;
  meeting.meeting = true;
  meeting.why = "microphone in use";
  Busy focus;
  focus.focus = true;
  focus.why = "a Focus is on";
  Item reply{100, Kind::Reply, "Trip plans", "Lisbon in May...", "chat-1"};
  Item watch{101, Kind::Watch, "Screen watch", "the build finished", ""};

  {
    Policy p;
    report("free: delivered at once", p.offer(reply, free, o) == Decision::Deliver && p.held().empty());
    report("in a meeting: a reply is held", p.offer(reply, meeting, o) == Decision::Hold && p.held().size() == 1);
    report("in a meeting: a screen-watch alert you asked for still comes", p.offer(watch, meeting, o) == Decision::Deliver);
    report("during a Focus: everything is held, alerts too", p.offer(watch, focus, o) == Decision::Hold && p.held().size() == 2);
    Options off = o;
    off.batchInMeetings = false;
    off.respectFocus = false;
    report("with both switched off nothing is held", p.offer(reply, meeting, off) == Decision::Deliver && p.offer(reply, focus, off) == Decision::Deliver);
  }
  {
    Policy p;
    p.offer(reply, meeting, o);
    p.offer(Item{120, Kind::Reply, "Budget", "Q1 numbers", "chat-2"}, meeting, o);
    p.offer(Item{130, Kind::Watch, "Screen watch", "done", ""}, focus, o);
    Digest d;
    std::vector<Item> each;
    const bool duringMeeting = p.tick(meeting, 200, o, &d, &each);
    const bool tooSoon = p.tick(free, 230, o, &d, &each);   // free since 230
    const bool stillTooSoon = p.tick(free, 280, o, &d, &each);
    const bool settled = p.tick(free, 291, o, &d, &each);
    report("nothing comes out while busy, or until free for a minute", !duringMeeting && !tooSoon && !stillTooSoon && settled);
    // Titled for the last busy state seen (the meeting, at 200).
    report("then one digest, titled for what it was, with the counts and chats",
           d.items.size() == 3 && each.empty() && d.title == "While you were in a meeting" && d.body == "2 replies finished, 1 screen watch answered: Trip plans, Budget, Screen watch",
           d.title + " | " + d.body);
    report("and nothing is held after it", p.held().empty() && !p.tick(free, 400, o, &d, &each));
  }
  {
    Policy p;
    p.offer(reply, meeting, o);
    Digest d;
    std::vector<Item> each;
    p.tick(free, 100, o, &d, &each);   // free since 100
    p.tick(meeting, 130, o, &d, &each);  // back on the call: the settle clock restarts
    const bool early = p.tick(free, 170, o, &d, &each);
    const bool later = p.tick(free, 231, o, &d, &each);
    report("a microphone that flickers off and on restarts the wait", !early && later);
    report("a single held one is delivered as itself, not as a digest", d.items.empty() && each.size() == 1 && each[0].title == "Trip plans");
  }
  {
    Policy p;
    Options noDigest = o;
    noDigest.digest = false;
    p.offer(reply, meeting, o);
    p.offer(Item{110, Kind::Plugin, "Plugin", "x", ""}, meeting, o);
    Digest d;
    std::vector<Item> each;
    p.tick(free, 0, noDigest, &d, &each);
    p.tick(free, 61, noDigest, &d, &each);
    report("with the digest off, held ones come one by one", d.items.empty() && each.size() == 2);
  }
  std::vector<Item> many;
  for (int i = 0; i < 6; ++i) many.push_back(Item{0, Kind::Reply, "Chat " + std::to_string(i), "", ""});
  many.push_back(Item{0, Kind::Reply, "Chat 0", "", ""});
  report("a Focus digest says so", makeDigest(many, "a Focus is on").title == "While your Focus was on");
  const Digest big = makeDigest(many, "microphone in use");
  report("a long digest names four and counts the rest; repeats are named once",
         big.title == "While you were in a meeting" && big.body == "7 replies finished: Chat 0, Chat 1, Chat 2, Chat 3 and 2 more", big.body);

  const bool mic = busy::microphoneInUse(), camera = busy::cameraInUse();
  const int focusState = busy::focusOn();
  report("reading the microphone, camera and Focus state asks for nothing", true,
         std::string("microphone ") + (mic ? "in use" : "free") + ", camera " + (camera ? "in use" : "free") + ", Focus " +
             (focusState < 0 ? std::string("unknown (") + busy::focusAccess() + ")" : focusState ? "on" : "off"));
  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
