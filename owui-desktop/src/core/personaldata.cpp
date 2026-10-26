#include "personaldata.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>

#include "core/common.h"
#include "core/settings.h"

namespace personal {

const char* kindName(Kind kind) {
  switch (kind) {
    case Kind::Calendar: return "Calendar";
    case Kind::Reminders: return "Reminders";
    case Kind::Contacts: return "Contacts";
    case Kind::Mail: return "Mail";
  }
  return "?";
}

const char* accessName(Access access) {
  switch (access) {
    case Access::NotDetermined: return "not asked yet";
    case Access::Granted: return "allowed";
    case Access::Denied: return "denied";
    case Access::Restricted: return "restricted";
    case Access::WriteOnly: return "add only";
  }
  return "?";
}

bool Options::enabled(Kind kind) const {
  switch (kind) {
    case Kind::Calendar: return calendar;
    case Kind::Reminders: return reminders;
    case Kind::Contacts: return contacts;
    case Kind::Mail: return mail;
  }
  return false;
}

Options optionsFromSettings() {
  const Json& extra = core::Settings::instance().extra;
  const Json tools = extra.value("macTools", Json::object());
  Options o;
  if (tools.is_object()) {
    o.calendar = tools.value("calendar", false);
    o.reminders = tools.value("reminders", false);
    o.contacts = tools.value("contacts", false);
    o.mail = tools.value("mail", false);
    o.maxItems = std::clamp(tools.value("maxItems", 50), 5, 500);
  }
  return o;
}

namespace {

agents::ToolSpec spec(const char* tool, const char* description, Json parameters) {
  agents::ToolSpec s;
  s.tool = tool;
  s.name = std::string(kServerName) + "__" + tool;
  s.description = description;
  s.parameters = std::move(parameters);
  s.serverId = kServerId;
  s.serverName = kServerName;
  return s;
}

Json object(Json properties, std::vector<std::string> required = {}) {
  Json schema{{"type", "object"}, {"properties", std::move(properties)}};
  if (!required.empty()) schema["required"] = required;
  return schema;
}

Json str(const char* description) { return Json{{"type", "string"}, {"description", description}}; }

std::string lowerTrim(const std::string& s) { return core::lower(core::trim(s)); }

// Midnight (local) of the day holding t, plus `days`.
double dayStart(double t, int days) {
  std::time_t tt = static_cast<std::time_t>(t);
  std::tm local{};
  localtime_r(&tt, &local);
  local.tm_hour = 0;
  local.tm_min = 0;
  local.tm_sec = 0;
  local.tm_mday += days;
  local.tm_isdst = -1;
  return static_cast<double>(std::mktime(&local));
}

std::string text(const Json& args, const char* key) {
  auto it = args.find(key);
  if (it == args.end() || it->is_null()) return "";
  if (it->is_string()) return core::trim(it->get<std::string>());
  return it->dump();
}

std::string oneLine(std::string s, size_t max) {
  for (char& c : s) {
    if (c == '\n' || c == '\r' || c == '\t') c = ' ';
  }
  s = core::trim(s);
  if (s.size() > max) s = s.substr(0, max) + "...";
  return s;
}

bool ensure(Kind kind, const Options& options, Backend& backend, bool write, std::string* error) {
  if (!options.enabled(kind)) {
    *error = std::string(kindName(kind)) + " tools are switched off (Settings > Tools > Mac data).";
    return false;
  }
  if (kind == Kind::Mail) return true;   // a draft through the share sheet needs no access
  const Access access = backend.access ? backend.access(kind) : Access::NotDetermined;
  if (access == Access::Granted || (write && access == Access::WriteOnly && kind == Kind::Calendar)) return true;
  *error = accessMessage(kind, access);
  return false;
}

}  // namespace

std::string accessMessage(Kind kind, Access access) {
  const std::string what = kindName(kind);
  switch (access) {
    case Access::NotDetermined:
      return what + " access has not been granted yet. Ask the user to open Settings > Tools > Mac data and press \"Allow " + what + " access\".";
    case Access::Denied:
      return what + " access was denied. The user can allow it in System Settings > Privacy & Security > " + what + ".";
    case Access::Restricted:
      return what + " access is restricted on this Mac (for example by a device profile).";
    case Access::WriteOnly:
      return what + " access only allows adding, not reading. The user can allow full access in System Settings > Privacy & Security > " + what + ".";
    case Access::Granted: break;
  }
  return "";
}

std::vector<agents::ToolSpec> specs(const Options& options) {
  std::vector<agents::ToolSpec> out;
  const Json range = object({{"when", str("today, tomorrow, yesterday, this week, next week, next 7 days, this month (instead of start/end)")},
                             {"start", str("local date or date-time, e.g. 2026-10-25 or 2026-10-25 09:00")},
                             {"end", str("local date or date-time (exclusive); defaults to the end of the start day")},
                             {"calendar", str("only this calendar (its name)")}});
  if (options.calendar) {
    out.push_back(spec("calendar_events", "List the user's calendar events in a time range (local time).", range));
    out.push_back(spec("calendar_create_event", "Add an event to the user's default calendar. Times are local.",
                       object({{"title", str("event title")},
                               {"start", str("local date-time, e.g. 2026-10-27 15:00")},
                               {"end", str("local date-time; defaults to one hour after start")},
                               {"all_day", Json{{"type", "boolean"}}},
                               {"location", str("optional")},
                               {"notes", str("optional")}},
                              {"title", "start"})));
  }
  if (options.reminders) {
    out.push_back(spec("reminders_list", "List the user's reminders (open ones unless include_completed).",
                       object({{"list", str("only this list (its name)")}, {"include_completed", Json{{"type", "boolean"}}}})));
    out.push_back(spec("reminders_create", "Add a reminder to the user's default list.",
                       object({{"title", str("what to be reminded of")},
                               {"due", str("optional local date or date-time")},
                               {"notes", str("optional")}},
                              {"title"})));
  }
  if (options.contacts) {
    out.push_back(spec("contacts_search", "Find people in the user's contacts by name, organisation or email address.",
                       object({{"query", str("a name, part of one, or an email address")}}, {"query"})));
  }
  if (options.mail) {
    out.push_back(spec("mail_draft",
                       "Open a new Mail message for the user to review and send. Nothing is sent by this tool.",
                       object({{"to", Json{{"description", "recipient address(es): a string or a list"}}},
                               {"subject", str("subject line")},
                               {"body", str("plain-text body")}},
                              {"subject", "body"})));
  }
  return out;
}

double parseLocalTime(const std::string& raw) {
  const std::string s = core::trim(raw);
  int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
  char sep = 0;
  const int n = std::sscanf(s.c_str(), "%4d-%2d-%2d%c%2d:%2d:%2d", &y, &mo, &d, &sep, &h, &mi, &se);
  if (n < 3 || y < 1900 || mo < 1 || mo > 12 || d < 1 || d > 31) return 0;
  if (n >= 4 && sep != 'T' && sep != ' ') return 0;
  if (n >= 4 && n < 6) return 0;   // a separator but no hh:mm
  if (h < 0 || h > 23 || mi < 0 || mi > 59 || se < 0 || se > 60) return 0;
  std::tm local{};
  local.tm_year = y - 1900;
  local.tm_mon = mo - 1;
  local.tm_mday = d;
  local.tm_hour = h;
  local.tm_min = mi;
  local.tm_sec = se;
  local.tm_isdst = -1;
  const std::time_t t = std::mktime(&local);
  if (t == static_cast<std::time_t>(-1)) return 0;
  // mktime normalises 2026-02-31 to March: refuse it instead.
  std::tm check{};
  localtime_r(&t, &check);
  if (check.tm_mday != d || check.tm_mon != mo - 1) return 0;
  return static_cast<double>(t);
}

std::string formatLocal(double t, bool withTime) {
  std::time_t tt = static_cast<std::time_t>(t);
  std::tm local{};
  localtime_r(&tt, &local);
  char buf[32];
  std::strftime(buf, sizeof buf, withTime ? "%Y-%m-%d %H:%M" : "%Y-%m-%d", &local);
  return buf;
}

bool parseRange(const Json& args, double now, double* start, double* end, std::string* error) {
  const std::string when = lowerTrim(text(args, "when"));
  const std::string from = text(args, "start"), to = text(args, "end");
  if (!when.empty()) {
    std::time_t tt = static_cast<std::time_t>(now);
    std::tm local{};
    localtime_r(&tt, &local);
    const int weekday = (local.tm_wday + 6) % 7;   // Monday 0
    if (when == "today") *start = dayStart(now, 0), *end = dayStart(now, 1);
    else if (when == "tomorrow") *start = dayStart(now, 1), *end = dayStart(now, 2);
    else if (when == "yesterday") *start = dayStart(now, -1), *end = dayStart(now, 0);
    else if (when == "this week") *start = dayStart(now, -weekday), *end = dayStart(now, 7 - weekday);
    else if (when == "next week") *start = dayStart(now, 7 - weekday), *end = dayStart(now, 14 - weekday);
    else if (when == "next 7 days" || when == "week") *start = dayStart(now, 0), *end = dayStart(now, 7);
    else if (when == "next 30 days") *start = dayStart(now, 0), *end = dayStart(now, 30);
    else if (when == "this month") {
      std::tm first = local;
      first.tm_mday = 1;
      first.tm_hour = first.tm_min = first.tm_sec = 0;
      first.tm_isdst = -1;
      std::tm next = first;
      next.tm_mon += 1;   // mktime carries December into January
      *start = static_cast<double>(std::mktime(&first));
      *end = static_cast<double>(std::mktime(&next));
    } else {
      *error = "unknown 'when': " + when + " (try today, tomorrow, this week, next 7 days)";
      return false;
    }
    return true;
  }
  if (from.empty()) {
    *start = dayStart(now, 0);
    *end = dayStart(now, 1);
    return true;
  }
  *start = parseLocalTime(from);
  if (*start == 0) {
    *error = "could not read start '" + from + "' (use 2026-10-25 or 2026-10-25 14:30)";
    return false;
  }
  if (to.empty()) {
    *end = dayStart(*start, 1);
  } else {
    *end = parseLocalTime(to);
    if (*end == 0) {
      *error = "could not read end '" + to + "'";
      return false;
    }
    if (to.find(':') == std::string::npos) *end = dayStart(*end, 1);   // a whole end day is included
  }
  if (*end <= *start) {
    *error = "end is not after start";
    return false;
  }
  if (*end - *start > 400.0 * 86400.0) {
    *error = "that range is over a year; ask for a shorter one";
    return false;
  }
  return true;
}

std::string formatEvents(const std::vector<Event>& events, int maxItems) {
  if (events.empty()) return "No events in that range.";
  std::vector<Event> sorted = events;
  std::sort(sorted.begin(), sorted.end(), [](const Event& a, const Event& b) { return a.start < b.start; });
  std::string out;
  int shown = 0;
  for (const Event& e : sorted) {
    if (shown++ >= maxItems) break;
    std::string when;
    if (e.allDay) {
      when = formatLocal(e.start, false) + " (all day)";
    } else {
      const std::string from = formatLocal(e.start), to = formatLocal(e.end);
      // Same day: "2026-10-25 14:00-15:00"; else the end date too.
      when = from + "-" + (to.compare(0, 10, from, 0, 10) == 0 ? to.substr(11) : to);
    }
    out += "- " + when + "  " + oneLine(e.title.empty() ? "(no title)" : e.title, 160);
    if (!e.calendar.empty()) out += "  [" + oneLine(e.calendar, 60) + "]";
    if (!e.location.empty()) out += "  @ " + oneLine(e.location, 120);
    if (!e.notes.empty()) out += "\n  notes: " + oneLine(e.notes, 300);
    out += "\n";
  }
  if (static_cast<int>(sorted.size()) > maxItems) out += "(" + std::to_string(sorted.size() - maxItems) + " more not shown)\n";
  return out;
}

std::string formatReminders(const std::vector<Reminder>& reminders, int maxItems) {
  if (reminders.empty()) return "No reminders.";
  std::vector<Reminder> sorted = reminders;
  // Open before done; dated by due date, undated after.
  std::stable_sort(sorted.begin(), sorted.end(), [](const Reminder& a, const Reminder& b) {
    if (a.completed != b.completed) return !a.completed;
    if ((a.due > 0) != (b.due > 0)) return a.due > 0;
    return a.due < b.due;
  });
  std::string out;
  int shown = 0;
  for (const Reminder& r : sorted) {
    if (shown++ >= maxItems) break;
    out += std::string("- [") + (r.completed ? "x" : " ") + "] " + oneLine(r.title.empty() ? "(no title)" : r.title, 160);
    if (r.due > 0) out += "  due " + formatLocal(r.due);
    if (r.priority > 0) out += r.priority <= 4 ? "  (high)" : r.priority == 5 ? "  (medium)" : "  (low)";
    if (!r.list.empty()) out += "  [" + oneLine(r.list, 60) + "]";
    if (!r.notes.empty()) out += "\n  notes: " + oneLine(r.notes, 300);
    out += "\n";
  }
  if (static_cast<int>(sorted.size()) > maxItems) out += "(" + std::to_string(sorted.size() - maxItems) + " more not shown)\n";
  return out;
}

std::string formatContacts(const std::vector<Contact>& contacts, int maxItems) {
  if (contacts.empty()) return "No matching contacts.";
  std::string out;
  int shown = 0;
  for (const Contact& c : contacts) {
    if (shown++ >= maxItems) break;
    out += "- " + oneLine(c.name.empty() ? c.organization : c.name, 120);
    if (!c.name.empty() && !c.organization.empty()) out += " (" + oneLine(c.organization, 80) + ")";
    if (!c.emails.empty()) out += "  email: " + core::join(c.emails, ", ");
    if (!c.phones.empty()) out += "  phone: " + core::join(c.phones, ", ");
    out += "\n";
  }
  if (static_cast<int>(contacts.size()) > maxItems) out += "(" + std::to_string(contacts.size() - maxItems) + " more not shown)\n";
  return out;
}

std::vector<std::string> parseRecipients(const Json& value, std::vector<std::string>* invalid) {
  std::vector<std::string> raw;
  if (value.is_string()) {
    std::string s = value.get<std::string>();
    std::replace(s.begin(), s.end(), ';', ',');
    for (const std::string& part : core::split(s, ',')) raw.push_back(part);
  } else if (value.is_array()) {
    for (const auto& v : value) {
      if (v.is_string()) raw.push_back(v.get<std::string>());
    }
  }
  std::vector<std::string> out;
  for (std::string r : raw) {
    r = core::trim(r);
    if (r.empty()) continue;
    // "Name <a@b.c>" keeps the address.
    const size_t lt = r.find('<'), gt = r.rfind('>');
    if (lt != std::string::npos && gt != std::string::npos && gt > lt) r = core::trim(r.substr(lt + 1, gt - lt - 1));
    const size_t at = r.find('@');
    const bool ok = at != std::string::npos && at > 0 && r.find('@', at + 1) == std::string::npos &&
                    r.find('.', at) != std::string::npos && r.back() != '.' && r.find(' ') == std::string::npos;
    if (ok) out.push_back(r);
    else if (invalid) invalid->push_back(r);
  }
  return out;
}

std::string call(const std::string& tool, const Json& argsIn, const Options& options, Backend& backend, double now, std::string* error) {
  const Json args = argsIn.is_object() ? argsIn : Json::object();
  if (tool == "calendar_events") {
    if (!ensure(Kind::Calendar, options, backend, false, error)) return "";
    double start = 0, end = 0;
    if (!parseRange(args, now, &start, &end, error)) return "";
    std::string err;
    const std::vector<Event> events = backend.events(start, end, text(args, "calendar"), &err);
    if (!err.empty()) {
      *error = err;
      return "";
    }
    return "Events " + formatLocal(start) + " to " + formatLocal(end) + ":\n" + formatEvents(events, options.maxItems);
  }
  if (tool == "calendar_create_event") {
    if (!ensure(Kind::Calendar, options, backend, true, error)) return "";
    Event e;
    e.title = text(args, "title");
    if (e.title.empty()) {
      *error = "an event needs a title";
      return "";
    }
    const std::string startText = text(args, "start");
    e.start = parseLocalTime(startText);
    if (e.start == 0) {
      *error = "could not read start '" + startText + "'";
      return "";
    }
    e.allDay = args.value("all_day", false) || startText.find(':') == std::string::npos;
    const std::string endText = text(args, "end");
    e.end = endText.empty() ? (e.allDay ? dayStart(e.start, 1) : e.start + 3600) : parseLocalTime(endText);
    if (e.end <= e.start) {
      *error = "end is not after start";
      return "";
    }
    e.location = text(args, "location");
    e.notes = text(args, "notes");
    std::string err;
    if (!backend.createEvent(e, &err)) {
      *error = err.empty() ? "the event could not be saved" : err;
      return "";
    }
    return "Added \"" + e.title + "\" on " + (e.allDay ? formatLocal(e.start, false) + " (all day)" : formatLocal(e.start) + " to " + formatLocal(e.end)) + ".";
  }
  if (tool == "reminders_list") {
    if (!ensure(Kind::Reminders, options, backend, false, error)) return "";
    std::string err;
    const std::vector<Reminder> list = backend.reminders(text(args, "list"), args.value("include_completed", false), &err);
    if (!err.empty()) {
      *error = err;
      return "";
    }
    return formatReminders(list, options.maxItems);
  }
  if (tool == "reminders_create") {
    if (!ensure(Kind::Reminders, options, backend, true, error)) return "";
    Reminder r;
    r.title = text(args, "title");
    if (r.title.empty()) {
      *error = "a reminder needs a title";
      return "";
    }
    const std::string due = text(args, "due");
    if (!due.empty()) {
      r.due = parseLocalTime(due);
      if (r.due == 0) {
        *error = "could not read due '" + due + "'";
        return "";
      }
    }
    r.notes = text(args, "notes");
    std::string err;
    if (!backend.createReminder(r, &err)) {
      *error = err.empty() ? "the reminder could not be saved" : err;
      return "";
    }
    return "Added the reminder \"" + r.title + "\"" + (r.due > 0 ? " due " + formatLocal(r.due) : "") + ".";
  }
  if (tool == "contacts_search") {
    if (!ensure(Kind::Contacts, options, backend, false, error)) return "";
    const std::string query = text(args, "query");
    if (query.size() < 2) {
      *error = "search with at least two characters";
      return "";
    }
    std::string err;
    const std::vector<Contact> found = backend.contacts(query, &err);
    if (!err.empty()) {
      *error = err;
      return "";
    }
    return formatContacts(found, std::min(options.maxItems, 25));
  }
  if (tool == "mail_draft") {
    if (!ensure(Kind::Mail, options, backend, true, error)) return "";
    std::vector<std::string> invalid;
    const std::vector<std::string> to = parseRecipients(args.contains("to") ? args["to"] : Json(), &invalid);
    if (!invalid.empty()) {
      *error = "not an email address: " + core::join(invalid, ", ");
      return "";
    }
    const std::string subject = text(args, "subject"), body = args.contains("body") && args["body"].is_string() ? args["body"].get<std::string>() : "";
    if (subject.empty() && body.empty()) {
      *error = "a draft needs a subject or a body";
      return "";
    }
    std::string err;
    if (!backend.composeMail(to, subject, body, &err)) {
      *error = err.empty() ? "Mail could not open a draft" : err;
      return "";
    }
    return "Opened a Mail draft" + (to.empty() ? std::string() : " to " + core::join(to, ", ")) + " for the user to review and send. Nothing was sent.";
  }
  *error = "unknown mac tool '" + tool + "'";
  return "";
}

}  // namespace personal
