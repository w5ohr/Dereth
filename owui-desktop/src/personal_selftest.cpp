// `Open WebUI --personaltest` (#89): the Calendar / Reminders / Contacts /
// Mail tools against a fake backend - no permission asked, no real data read.
#include <cstdio>
#include <ctime>
#include <string>

#include "core/personaldata.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

struct Fake {
  personal::Access access = personal::Access::Granted;
  std::vector<personal::Event> events;
  std::vector<personal::Reminder> reminders;
  std::vector<personal::Contact> contacts;
  personal::Event created;
  personal::Reminder createdReminder;
  std::vector<std::string> mailTo;
  std::string mailSubject;
  double askedStart = 0, askedEnd = 0;
  personal::Backend backend() {
    personal::Backend b;
    b.access = [this](personal::Kind) { return access; };
    b.events = [this](double s, double e, const std::string&, std::string*) {
      askedStart = s;
      askedEnd = e;
      return events;
    };
    b.createEvent = [this](const personal::Event& ev, std::string*) {
      created = ev;
      return true;
    };
    b.reminders = [this](const std::string&, bool, std::string*) { return reminders; };
    b.createReminder = [this](const personal::Reminder& r, std::string*) {
      createdReminder = r;
      return true;
    };
    b.contacts = [this](const std::string&, std::string*) { return contacts; };
    b.composeMail = [this](const std::vector<std::string>& to, const std::string& subject, const std::string&, std::string*) {
      mailTo = to;
      mailSubject = subject;
      return true;
    };
    return b;
  }
};
}  // namespace

int runPersonalSelfTest() {
  printf("Open WebUI desktop personal-data tools self-test\n\n");
  using namespace personal;
  const double t = parseLocalTime("2026-10-25 14:30");
  report("local date-times parse and print back", t > 0 && formatLocal(t) == "2026-10-25 14:30" &&
                                                  formatLocal(parseLocalTime("2026-10-25T09:05:00")) == "2026-10-25 09:05" &&
                                                  formatLocal(parseLocalTime("2026-10-25"), false) == "2026-10-25");
  report("bad dates are refused (Feb 31, 13th month, no minutes, words)",
         parseLocalTime("2026-02-31") == 0 && parseLocalTime("2026-13-01") == 0 && parseLocalTime("2026-10-25 14") == 0 &&
             parseLocalTime("tomorrow") == 0 && parseLocalTime("2028-02-29") > 0);

  double s = 0, e = 0;
  std::string err;
  const double now = t;   // a Sunday afternoon
  bool ok = parseRange(Json{{"when", "today"}}, now, &s, &e, &err);
  report("today is local midnight to midnight", ok && formatLocal(s) == "2026-10-25 00:00" && formatLocal(e) == "2026-10-26 00:00");
  ok = parseRange(Json{{"when", "this week"}}, now, &s, &e, &err);
  report("this week runs Monday to Monday", ok && formatLocal(s) == "2026-10-19 00:00" && formatLocal(e) == "2026-10-26 00:00",
         formatLocal(s) + " .. " + formatLocal(e));
  ok = parseRange(Json{{"when", "this month"}}, now, &s, &e, &err);
  report("this month runs from the first to the next first", ok && formatLocal(s) == "2026-10-01 00:00" && formatLocal(e) == "2026-11-01 00:00");
  ok = parseRange(Json{{"when", "this month"}}, parseLocalTime("2026-12-15 10:00"), &s, &e, &err);
  report("December's month ends in January", ok && formatLocal(e) == "2027-01-01 00:00");
  ok = parseRange(Json{{"start", "2026-10-27"}, {"end", "2026-10-28"}}, now, &s, &e, &err);
  report("a date-only end includes that whole day", ok && formatLocal(e) == "2026-10-29 00:00");
  ok = parseRange(Json{{"start", "2026-10-27 09:00"}}, now, &s, &e, &err);
  report("a start alone covers the rest of its day", ok && formatLocal(e) == "2026-10-28 00:00");
  std::string e1, e2, e3;
  const bool r1 = parseRange(Json{{"when", "someday"}}, now, &s, &e, &e1);
  const bool r2 = parseRange(Json{{"start", "2026-10-28 10:00"}, {"end", "2026-10-28 09:00"}}, now, &s, &e, &e2);
  const bool r3 = parseRange(Json{{"start", "2026-01-01"}, {"end", "2028-01-01"}}, now, &s, &e, &e3);
  report("unknown words, backwards and over-a-year ranges are refused with a reason",
         !r1 && !r2 && !r3 && has(e1, "unknown") && has(e2, "not after") && has(e3, "year"));

  Options none;
  Options all;
  all.calendar = all.reminders = all.contacts = all.mail = true;
  Options calendarOnly;
  calendarOnly.calendar = true;
  const auto allSpecs = specs(all);
  report("nothing is offered until switched on; each kind adds only its tools",
         specs(none).empty() && specs(calendarOnly).size() == 2 && allSpecs.size() == 6 &&
             allSpecs[0].name == "mac__calendar_events" && allSpecs[0].serverId == kServerId);
  bool schemasOk = true;
  for (const auto& sp : allSpecs) schemasOk &= sp.parameters.value("type", "") == "object" && sp.parameters.contains("properties");
  report("every tool has an object schema", schemasOk);

  Fake fake;
  Backend backend = fake.backend();
  std::string error;
  call("calendar_events", Json::object(), none, backend, now, &error);
  report("an off kind tells the model where to switch it on", has(error, "switched off") && has(error, "Settings > Tools > Mac data") && fake.askedStart == 0);
  error.clear();
  call("contacts_search", Json{{"query", "Ann"}}, calendarOnly, backend, now, &error);
  report("a tool of a kind that is off is refused", has(error, "Contacts tools are switched off"));

  fake.access = Access::NotDetermined;
  error.clear();
  call("calendar_events", Json{{"when", "today"}}, all, backend, now, &error);
  report("not asked yet: points at the Allow button, asks nothing", has(error, "Allow Calendar access") && fake.askedStart == 0);
  fake.access = Access::Denied;
  error.clear();
  call("reminders_list", Json::object(), all, backend, now, &error);
  report("denied: points at System Settings", has(error, "System Settings > Privacy & Security > Reminders"));
  fake.access = Access::WriteOnly;
  error.clear();
  call("calendar_events", Json::object(), all, backend, now, &error);
  std::string addError;
  const std::string added = call("calendar_create_event", Json{{"title", "Dentist"}, {"start", "2026-10-28 15:00"}}, all, backend, now, &addError);
  report("add-only calendar access lets the model add but not read", has(error, "only allows adding") && addError.empty() &&
                                                                       has(added, "Added \"Dentist\""));
  report("an event with a start time lasts an hour by default", formatLocal(fake.created.end) == "2026-10-28 16:00" && !fake.created.allDay);
  fake.access = Access::Granted;
  error.clear();
  call("calendar_create_event", Json{{"title", "Holiday"}, {"start", "2026-12-24"}}, all, backend, now, &error);
  report("a date-only start makes an all-day event", error.empty() && fake.created.allDay && formatLocal(fake.created.end, false) == "2026-12-25");

  Event late{"Standup", "Work", "Room 2", "", parseLocalTime("2026-10-25 15:00"), parseLocalTime("2026-10-25 15:15"), false};
  Event early{"Breakfast", "Home", "", "bring \\n coffee", parseLocalTime("2026-10-25 08:00"), parseLocalTime("2026-10-25 09:00"), false};
  Event allDay{"Birthday", "Family", "", "", parseLocalTime("2026-10-25"), parseLocalTime("2026-10-26"), true};
  fake.events = {late, early, allDay};
  error.clear();
  const std::string listed = call("calendar_events", Json{{"when", "today"}}, all, backend, now, &error);
  report("events come back sorted, with times, calendar, place and an all-day mark",
         error.empty() && listed.find("Birthday") < listed.find("Breakfast") && listed.find("Breakfast") < listed.find("Standup") &&
             has(listed, "2026-10-25 15:00-15:15  Standup  [Work]  @ Room 2") && has(listed, "(all day)") &&
             formatLocal(fake.askedStart) == "2026-10-25 00:00",
         listed.substr(0, 200));
  Options few = all;
  few.maxItems = 2;
  report("long lists say how many were left out", has(formatEvents(fake.events, 2), "(1 more not shown)"));

  Reminder done{"Old task", "Home", "", 0, true, 0};
  Reminder undated{"Someday", "Home", "", 0, false, 0};
  Reminder dated{"Pay rent", "Bills", "", parseLocalTime("2026-11-01 09:00"), false, 1};
  const std::string rem = formatReminders({done, undated, dated}, 10);
  report("reminders: open and dated first, done last, priority shown",
         rem.find("Pay rent") < rem.find("Someday") && rem.find("Someday") < rem.find("Old task") && has(rem, "[x] Old task") &&
             has(rem, "due 2026-11-01 09:00") && has(rem, "(high)"));
  error.clear();
  call("reminders_create", Json{{"title", "Call mum"}, {"due", "next friday"}}, all, backend, now, &error);
  report("an unreadable due date is refused", has(error, "could not read due"));

  error.clear();
  call("contacts_search", Json{{"query", "A"}}, all, backend, now, &error);
  fake.contacts = {{"Ann Lee", "Acme", {"ann@acme.com"}, {"+1 555 0100"}}, {"", "Plumbing Co", {}, {"555 0199"}}};
  std::string error2;
  const std::string people = call("contacts_search", Json{{"query", "Ann"}}, all, backend, now, &error2);
  report("contacts: two characters at least; name, organisation, email, phone",
         has(error, "two characters") && has(people, "Ann Lee (Acme)  email: ann@acme.com  phone: +1 555 0100") && has(people, "- Plumbing Co"));

  std::vector<std::string> invalid;
  const auto to = parseRecipients(Json("Ann Lee <ann@acme.com>; bob@example.org, not-an-address"), &invalid);
  report("recipients: names stripped, ; and , both split, bad ones reported",
         to == std::vector<std::string>{"ann@acme.com", "bob@example.org"} && invalid == std::vector<std::string>{"not-an-address"});
  error.clear();
  call("mail_draft", Json{{"to", "nobody"}, {"subject", "Hi"}, {"body", "x"}}, all, backend, now, &error);
  std::string mailError;
  const std::string drafted = call("mail_draft", Json{{"to", Json::array({"ann@acme.com"})}, {"subject", "Lunch"}, {"body", "Friday?"}}, all, backend, now, &mailError);
  report("mail: a bad address is refused; a draft opens and nothing is sent",
         has(error, "not an email address") && mailError.empty() && has(drafted, "Nothing was sent") && fake.mailTo.size() == 1 && fake.mailSubject == "Lunch");
  error.clear();
  call("mail_draft", Json{{"to", "ann@acme.com"}}, all, backend, now, &error);
  std::string unknown;
  call("calendar_delete_everything", Json::object(), all, backend, now, &unknown);
  report("an empty draft and unknown tools are refused", has(error, "subject or a body") && has(unknown, "unknown mac tool"));

  // The real status calls answer without a prompt (no request is made here).
  const Access c = currentAccess(Kind::Calendar), r = currentAccess(Kind::Reminders), k = currentAccess(Kind::Contacts);
  report("reading this Mac's access status asks nothing", true,
         std::string("calendar ") + accessName(c) + ", reminders " + accessName(r) + ", contacts " + accessName(k));
  (void)few;
  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
