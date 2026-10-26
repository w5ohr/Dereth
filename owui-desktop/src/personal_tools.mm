// Personal-data tools (#89), the system side: EventKit for Calendar and
// Reminders, the Contacts framework, and Mail's compose share service, plus
// the Settings card that switches each on and asks macOS for access. Nothing
// here asks for access on its own: only the card's buttons do.
#import <Cocoa/Cocoa.h>
#import <Contacts/Contacts.h>
#import <EventKit/EventKit.h>

#include "app.h"
#include "core/async.h"
#include "core/common.h"
#include "core/personaldata.h"
#include "core/settings.h"
#include "pane_common.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace personal {
namespace {

EKEventStore* store() {
  static EKEventStore* s = [[EKEventStore alloc] init];
  return s;
}

NSString* ns(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()] ?: @""; }
std::string sstr(NSString* s) { return s ? std::string(s.UTF8String ?: "") : std::string(); }

Access fromEventKit(EKAuthorizationStatus status) {
  if (@available(macOS 14.0, *)) {
    if (status == EKAuthorizationStatusFullAccess) return Access::Granted;
    if (status == EKAuthorizationStatusWriteOnly) return Access::WriteOnly;
  } else if (status == 3 /* EKAuthorizationStatusAuthorized */) {
    return Access::Granted;
  }
  if (status == EKAuthorizationStatusDenied) return Access::Denied;
  if (status == EKAuthorizationStatusRestricted) return Access::Restricted;
  return Access::NotDetermined;
}

Access accessNow(Kind kind) {
  switch (kind) {
    case Kind::Calendar: return fromEventKit([EKEventStore authorizationStatusForEntityType:EKEntityTypeEvent]);
    case Kind::Reminders: return fromEventKit([EKEventStore authorizationStatusForEntityType:EKEntityTypeReminder]);
    case Kind::Contacts: {
      const CNAuthorizationStatus status = [CNContactStore authorizationStatusForEntityType:CNEntityTypeContacts];
      if (status == CNAuthorizationStatusAuthorized) return Access::Granted;
      if (status == CNAuthorizationStatusDenied) return Access::Denied;
      if (status == CNAuthorizationStatusRestricted) return Access::Restricted;
      return Access::NotDetermined;
    }
    case Kind::Mail: return Access::Granted;
  }
  return Access::NotDetermined;
}

NSArray<EKCalendar*>* calendarsNamed(EKEntityType type, const std::string& name) {
  if (name.empty()) return nil;
  NSMutableArray* out = [NSMutableArray array];
  for (EKCalendar* c in [store() calendarsForEntityType:type]) {
    if ([c.title caseInsensitiveCompare:ns(name)] == NSOrderedSame) [out addObject:c];
  }
  return out;
}

NSDateComponents* components(double t, bool withTime) {
  NSDate* date = [NSDate dateWithTimeIntervalSince1970:t];
  NSCalendarUnit units = NSCalendarUnitYear | NSCalendarUnitMonth | NSCalendarUnitDay;
  if (withTime) units |= NSCalendarUnitHour | NSCalendarUnitMinute;
  return [[NSCalendar currentCalendar] components:units fromDate:date];
}

Backend makeBackend() {
  Backend b;
  b.access = accessNow;
  b.events = [](double start, double end, const std::string& calendar, std::string* error) {
    std::vector<Event> out;
    NSArray<EKCalendar*>* calendars = calendarsNamed(EKEntityTypeEvent, calendar);
    if (calendars && calendars.count == 0) {
      *error = "no calendar named '" + calendar + "'";
      return out;
    }
    NSPredicate* predicate = [store() predicateForEventsWithStartDate:[NSDate dateWithTimeIntervalSince1970:start]
                                                              endDate:[NSDate dateWithTimeIntervalSince1970:end]
                                                            calendars:calendars];
    for (EKEvent* e in [store() eventsMatchingPredicate:predicate]) {
      Event ev;
      ev.title = sstr(e.title);
      ev.calendar = sstr(e.calendar.title);
      ev.location = sstr(e.location);
      ev.notes = sstr(e.notes);
      ev.start = e.startDate.timeIntervalSince1970;
      ev.end = e.endDate.timeIntervalSince1970;
      ev.allDay = e.allDay;
      out.push_back(ev);
    }
    return out;
  };
  b.createEvent = [](const Event& ev, std::string* error) {
    EKEvent* e = [EKEvent eventWithEventStore:store()];
    e.title = ns(ev.title);
    e.startDate = [NSDate dateWithTimeIntervalSince1970:ev.start];
    e.endDate = [NSDate dateWithTimeIntervalSince1970:ev.end];
    e.allDay = ev.allDay;
    if (!ev.location.empty()) e.location = ns(ev.location);
    if (!ev.notes.empty()) e.notes = ns(ev.notes);
    e.calendar = store().defaultCalendarForNewEvents;
    if (!e.calendar) {
      *error = "there is no default calendar to add to";
      return false;
    }
    NSError* err = nil;
    if (![store() saveEvent:e span:EKSpanThisEvent commit:YES error:&err]) {
      *error = sstr(err.localizedDescription);
      return false;
    }
    return true;
  };
  b.reminders = [](const std::string& list, bool includeCompleted, std::string* error) {
    std::vector<Reminder> out;
    NSArray<EKCalendar*>* lists = calendarsNamed(EKEntityTypeReminder, list);
    if (lists && lists.count == 0) {
      *error = "no reminders list named '" + list + "'";
      return out;
    }
    NSPredicate* predicate = includeCompleted ? [store() predicateForRemindersInCalendars:lists]
                                              : [store() predicateForIncompleteRemindersWithDueDateStarting:nil ending:nil calendars:lists];
    __block NSArray<EKReminder*>* found = nil;
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    [store() fetchRemindersMatchingPredicate:predicate
                                  completion:^(NSArray<EKReminder*>* reminders) {
                                    found = reminders;
                                    dispatch_semaphore_signal(done);
                                  }];
    if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_SEC)) != 0) {
      *error = "Reminders did not answer in time";
      return out;
    }
    for (EKReminder* r in found) {
      Reminder rm;
      rm.title = sstr(r.title);
      rm.list = sstr(r.calendar.title);
      rm.notes = sstr(r.notes);
      rm.completed = r.completed;
      rm.priority = static_cast<int>(r.priority);
      if (r.dueDateComponents) {
        NSDate* due = [[NSCalendar currentCalendar] dateFromComponents:r.dueDateComponents];
        if (due) rm.due = due.timeIntervalSince1970;
      }
      out.push_back(rm);
    }
    return out;
  };
  b.createReminder = [](const Reminder& rm, std::string* error) {
    EKReminder* r = [EKReminder reminderWithEventStore:store()];
    r.title = ns(rm.title);
    if (!rm.notes.empty()) r.notes = ns(rm.notes);
    if (rm.due > 0) r.dueDateComponents = components(rm.due, true);
    r.calendar = store().defaultCalendarForNewReminders;
    if (!r.calendar) {
      *error = "there is no default reminders list to add to";
      return false;
    }
    NSError* err = nil;
    if (![store() saveReminder:r commit:YES error:&err]) {
      *error = sstr(err.localizedDescription);
      return false;
    }
    return true;
  };
  b.contacts = [](const std::string& query, std::string* error) {
    std::vector<Contact> out;
    CNContactStore* contacts = [[CNContactStore alloc] init];
    NSArray* keys = @[ [CNContactFormatter descriptorForRequiredKeysForStyle:CNContactFormatterStyleFullName], CNContactOrganizationNameKey,
                       CNContactEmailAddressesKey, CNContactPhoneNumbersKey ];
    NSPredicate* predicate = query.find('@') != std::string::npos ? [CNContact predicateForContactsMatchingEmailAddress:ns(query)]
                                                                  : [CNContact predicateForContactsMatchingName:ns(query)];
    NSError* err = nil;
    NSArray<CNContact*>* found = [contacts unifiedContactsMatchingPredicate:predicate keysToFetch:keys error:&err];
    if (err) {
      *error = sstr(err.localizedDescription);
      return out;
    }
    for (CNContact* c in found) {
      Contact ct;
      ct.name = sstr([CNContactFormatter stringFromContact:c style:CNContactFormatterStyleFullName]);
      ct.organization = sstr(c.organizationName);
      for (CNLabeledValue<NSString*>* e in c.emailAddresses) ct.emails.push_back(sstr(e.value));
      for (CNLabeledValue<CNPhoneNumber*>* p in c.phoneNumbers) ct.phones.push_back(sstr(p.value.stringValue));
      out.push_back(ct);
    }
    return out;
  };
  b.composeMail = [](const std::vector<std::string>& to, const std::string& subject, const std::string& body, std::string* error) {
    __block bool ok = false;
    __block std::string failure;
    // The share service is AppKit: on the main thread.
    dispatch_sync(dispatch_get_main_queue(), ^{
      NSSharingService* mail = [NSSharingService sharingServiceNamed:NSSharingServiceNameComposeEmail];
      NSArray* items = @[ ns(body) ];
      if (!mail || ![mail canPerformWithItems:items]) {
        failure = "Mail has no account set up to write from";
        return;
      }
      NSMutableArray* recipients = [NSMutableArray array];
      for (const std::string& r : to) [recipients addObject:ns(r)];
      mail.recipients = recipients;
      mail.subject = ns(subject);
      [mail performWithItems:items];
      ok = true;
    });
    if (!ok) *error = failure;
    return ok;
  };
  return b;
}

}  // namespace

Backend& nativeBackend() {
  static Backend backend = makeBackend();
  return backend;
}

// Shows the system prompt (only from the Settings card). done runs on the main thread.
void requestAccess(Kind kind, std::function<void(Access)> done) {
  auto finish = [kind, done](bool) {
    dispatch_async(dispatch_get_main_queue(), ^{
      if (done) done(accessNow(kind));
    });
  };
  if (kind == Kind::Contacts) {
    CNContactStore* contacts = [[CNContactStore alloc] init];
    [contacts requestAccessForEntityType:CNEntityTypeContacts completionHandler:^(BOOL granted, NSError*) { finish(granted); }];
    return;
  }
  if (kind == Kind::Calendar || kind == Kind::Reminders) {
    const EKEntityType type = kind == Kind::Calendar ? EKEntityTypeEvent : EKEntityTypeReminder;
    if (@available(macOS 14.0, *)) {
      auto handler = ^(BOOL granted, NSError*) { finish(granted); };
      if (type == EKEntityTypeEvent) [store() requestFullAccessToEventsWithCompletion:handler];
      else [store() requestFullAccessToRemindersWithCompletion:handler];
    } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
      [store() requestAccessToEntityType:type completion:^(BOOL granted, NSError*) { finish(granted); }];
#pragma clang diagnostic pop
    }
  }
}

void openPrivacySettings(Kind kind) {
  NSString* pane = kind == Kind::Calendar ? @"Privacy_Calendars" : kind == Kind::Reminders ? @"Privacy_Reminders" : @"Privacy_Contacts";
  NSURL* url = [NSURL URLWithString:[@"x-apple.systempreferences:com.apple.preference.security?" stringByAppendingString:pane]];
  [[NSWorkspace sharedWorkspace] openURL:url];
}

Access currentAccess(Kind kind) { return accessNow(kind); }

}  // namespace personal

// ---------------------------------------------------------------- settings card

namespace app {

void App::drawMacToolsCard(float width) {
  card("builtin-mac", [&] {
    theme::pushBold();
    ImGui::TextUnformatted("Mac data (built in)");
    theme::pop();
    ImGui::SameLine();
    widgets::dimText("mac__calendar_events, reminders_list, contacts_search, mail_draft ...");
    widgets::dimText("%s", "Tools the model can call to read and add to your Calendar and Reminders, look people up in Contacts, "
                           "and open a Mail draft for you to send. Each is off until you switch it on, and macOS asks once for "
                           "access - from here, never from a chat. Every call still shows an approval card.");
    Json tools = settings().extra.value("macTools", Json::object());
    if (!tools.is_object()) tools = Json::object();
    struct Row {
      personal::Kind kind;
      const char* key;
      const char* label;
    };
    const Row rows[] = {
        {personal::Kind::Calendar, "calendar", "Calendar: list events, add an event"},
        {personal::Kind::Reminders, "reminders", "Reminders: list, add a reminder"},
        {personal::Kind::Contacts, "contacts", "Contacts: search by name or email"},
        {personal::Kind::Mail, "mail", "Mail: open a draft (never sends)"},
    };
    for (const Row& row : rows) {
      ImGui::PushID(row.key);
      bool on = tools.value(row.key, false);
      if (ImGui::Checkbox(row.label, &on)) {
        tools[row.key] = on;
        settings().extra["macTools"] = tools;
        settings().save();
      }
      if (row.kind != personal::Kind::Mail) {
        const personal::Access access = personal::currentAccess(row.kind);
        ImGui::SameLine();
        widgets::dimText("access: %s", personal::accessName(access));
        if (access == personal::Access::NotDetermined) {
          ImGui::SameLine();
          const std::string button = std::string("Allow ") + personal::kindName(row.kind) + " access";
          if (ImGui::SmallButton(button.c_str())) {
            const personal::Kind kind = row.kind;
            personal::requestAccess(kind, [kind](personal::Access result) {
              widgets::toastInfo(std::string(personal::kindName(kind)) + " access: " + personal::accessName(result));
            });
          }
        } else if (access == personal::Access::Denied || access == personal::Access::WriteOnly) {
          ImGui::SameLine();
          if (ImGui::SmallButton("Open Privacy settings")) personal::openPrivacySettings(row.kind);
        }
      }
      ImGui::PopID();
    }
  }, width);
}

}  // namespace app
