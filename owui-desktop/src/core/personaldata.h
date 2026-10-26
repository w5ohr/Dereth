// Personal-data tools (#89): Calendar, Reminders, Contacts and a Mail draft,
// offered to the model as the built-in "mac" tool server. This is the part
// that needs no permission: the tool definitions, argument checks, date
// ranges and the text the model gets back. The system calls (EventKit,
// Contacts, the Mail share service) sit behind a Backend, so the self-test
// runs on a fake one and never touches anyone's real data.
//
// Tool calls never ask for access themselves: each kind is switched on in
// Settings, and access is requested there, by the person, with a button.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "agents/tool_loop.h"

namespace personal {

using Json = nlohmann::json;

constexpr const char* kServerId = "builtin:mac";
constexpr const char* kServerName = "mac";

enum class Kind { Calendar, Reminders, Contacts, Mail };
enum class Access { NotDetermined, Granted, Denied, Restricted, WriteOnly };
const char* kindName(Kind kind);
const char* accessName(Access access);

struct Options {
  bool calendar = false, reminders = false, contacts = false, mail = false;   // all off until switched on
  int maxItems = 50;
  bool any() const { return calendar || reminders || contacts || mail; }
  bool enabled(Kind kind) const;
};
Options optionsFromSettings();

struct Event {
  std::string title, calendar, location, notes;
  double start = 0, end = 0;   // seconds since the epoch
  bool allDay = false;
};
struct Reminder {
  std::string title, list, notes;
  double due = 0;              // 0: no due date
  bool completed = false;
  int priority = 0;            // 0 none, 1 high .. 9 low (EventKit's scale)
};
struct Contact {
  std::string name, organization;
  std::vector<std::string> emails, phones;
};

// The system side. Every function runs on a tool worker thread.
struct Backend {
  std::function<Access(Kind)> access;
  std::function<std::vector<Event>(double start, double end, const std::string& calendar, std::string* error)> events;
  std::function<bool(const Event& event, std::string* error)> createEvent;
  std::function<std::vector<Reminder>(const std::string& list, bool includeCompleted, std::string* error)> reminders;
  std::function<bool(const Reminder& reminder, std::string* error)> createReminder;
  std::function<std::vector<Contact>(const std::string& query, std::string* error)> contacts;
  std::function<bool(const std::vector<std::string>& to, const std::string& subject, const std::string& body, std::string* error)> composeMail;
};
Backend& nativeBackend();   // personal_tools.mm
Access currentAccess(Kind kind);                                     // no prompt
void requestAccess(Kind kind, std::function<void(Access)> done);     // the system prompt; Settings only
void openPrivacySettings(Kind kind);

std::vector<agents::ToolSpec> specs(const Options& options);
// Blocking. Returns the text for the model, or sets *error (then shown to the model as a failed call).
std::string call(const std::string& tool, const Json& args, const Options& options, Backend& backend, double now, std::string* error);

// ---- pieces, exposed for the self-test ------------------------------------
// "2026-10-25", "2026-10-25 14:30", "2026-10-25T14:30:00" in local time; 0 when unreadable.
double parseLocalTime(const std::string& text);
std::string formatLocal(double t, bool withTime = true);   // "2026-10-25 14:30" / "2026-10-25"
// A range from {"start","end"} or {"when": "today|tomorrow|this week|next 7 days|..."}; false + reason when bad.
bool parseRange(const Json& args, double now, double* start, double* end, std::string* error);
std::string formatEvents(const std::vector<Event>& events, int maxItems);
std::string formatReminders(const std::vector<Reminder>& reminders, int maxItems);
std::string formatContacts(const std::vector<Contact>& contacts, int maxItems);
// Recipients from a string ("a@x.com, b@y.org") or an array; bad ones listed in *invalid.
std::vector<std::string> parseRecipients(const Json& value, std::vector<std::string>* invalid);
std::string accessMessage(Kind kind, Access access);   // what the model is told when it may not look

}  // namespace personal
