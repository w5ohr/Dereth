// Focus-aware notifications (#125): what the system says about being busy -
// a Focus (INFocusStatusCenter, when allowed), the microphone or a camera in
// use by another app (CoreAudio / CoreMediaIO, no permission), a calendar
// event now (when Calendar access was given for #89) - and the policy that
// holds notifications meanwhile and delivers a digest afterwards.
#import <AppKit/AppKit.h>
#import <CoreAudio/CoreAudio.h>
#import <CoreMediaIO/CMIOHardware.h>
#import <Intents/Intents.h>
#import <UserNotifications/UserNotifications.h>

#include <cstdlib>
#include <ctime>

#include "app.h"
#include "core/async.h"
#include "core/common.h"
#include "core/notifypolicy.h"
#include "core/personaldata.h"
#include "core/settings.h"
#include "native.h"
#include "platform.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace busy {

// The default input device is running for some process (a call, a recorder).
bool microphoneInUse() {
  AudioObjectPropertyAddress address{kAudioHardwarePropertyDefaultInputDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
  AudioDeviceID device = 0;
  UInt32 size = sizeof(device);
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size, &device) != noErr || device == 0) return false;
  address.mSelector = kAudioDevicePropertyDeviceIsRunningSomewhere;
  UInt32 running = 0;
  size = sizeof(running);
  if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &running) != noErr) return false;
  return running != 0;
}

// Any camera is running for some process.
bool cameraInUse() {
  CMIOObjectPropertyAddress address{kCMIOHardwarePropertyDevices, kCMIOObjectPropertyScopeGlobal, kCMIOObjectPropertyElementMain};
  UInt32 size = 0;
  if (CMIOObjectGetPropertyDataSize(kCMIOObjectSystemObject, &address, 0, nullptr, &size) != noErr || size == 0) return false;
  std::vector<CMIODeviceID> devices(size / sizeof(CMIODeviceID));
  UInt32 used = 0;
  if (CMIOObjectGetPropertyData(kCMIOObjectSystemObject, &address, 0, nullptr, size, &used, devices.data()) != noErr) return false;
  for (CMIODeviceID d : devices) {
    CMIOObjectPropertyAddress running{kCMIODevicePropertyDeviceIsRunningSomewhere, kCMIOObjectPropertyScopeWildcard, kCMIOObjectPropertyElementWildcard};
    UInt32 value = 0, got = 0;
    if (CMIOObjectGetPropertyData(d, &running, 0, nullptr, sizeof(value), &got, &value) == noErr && value) return true;
  }
  return false;
}

// -1 unknown (not allowed / not available), 0 off, 1 on.
int focusOn() {
  if (@available(macOS 12.0, *)) {
    INFocusStatusCenter* center = INFocusStatusCenter.defaultCenter;
    if (center.authorizationStatus != INFocusStatusAuthorizationStatusAuthorized) return -1;
    NSNumber* focused = center.focusStatus.isFocused;
    return focused ? (focused.boolValue ? 1 : 0) : -1;
  }
  return -1;
}

const char* focusAccess() {
  if (@available(macOS 12.0, *)) {
    switch (INFocusStatusCenter.defaultCenter.authorizationStatus) {
      case INFocusStatusAuthorizationStatusAuthorized: return "allowed";
      case INFocusStatusAuthorizationStatusDenied: return "denied";
      case INFocusStatusAuthorizationStatusRestricted: return "restricted";
      default: return "not asked yet";
    }
  }
  return "not available";
}

void requestFocusAccess() {
  if (@available(macOS 12.0, *)) [INFocusStatusCenter.defaultCenter requestAuthorizationWithCompletionHandler:^(INFocusStatusAuthorizationStatus) {}];
}

}  // namespace busy

namespace app {
namespace {

notifypolicy::Policy gPolicy;
notifypolicy::Busy gBusy;
double gBusyCheckedAt = 0;
bool gCalendarBusy = false;
double gCalendarCheckedAt = 0;
struct Recent {
  double at;
  std::string line;
};
std::vector<Recent> gRecent;
notifypolicy::Digest gLastDigest;
bool gDigestUnseen = false;

notifypolicy::Options optionsFromSettings() {
  const Json j = core::Settings::instance().extra.value("notifyPolicy", Json::object());
  notifypolicy::Options o;
  if (j.is_object()) {
    o.respectFocus = j.value("respectFocus", true);
    o.batchInMeetings = j.value("batchInMeetings", true);
    o.digest = j.value("digest", true);
    o.urgentThroughMeetings = j.value("urgentThroughMeetings", true);
  }
  return o;
}

void remember(const std::string& line) {
  gRecent.push_back({static_cast<double>(std::time(nullptr)), line});
  if (gRecent.size() > 12) gRecent.erase(gRecent.begin());
}

std::string hm(double t) {
  std::time_t tt = static_cast<std::time_t>(t);
  std::tm local{};
  localtime_r(&tt, &local);
  char buf[8];
  std::strftime(buf, sizeof buf, "%H:%M", &local);
  return buf;
}

bool notificationsAllowed() {
  const int a = native::notificationAuthorization();
  return a == UNAuthorizationStatusAuthorized || a == UNAuthorizationStatusProvisional;
}

}  // namespace

void App::notify(int kind, const std::string& title, const std::string& body, const std::string& chatId) {
  notifypolicy::Item item;
  item.at = static_cast<double>(std::time(nullptr));
  item.kind = static_cast<notifypolicy::Kind>(kind);
  item.title = title;
  item.body = body;
  item.chatId = chatId;
  if (gPolicy.offer(item, gBusy, optionsFromSettings()) == notifypolicy::Decision::Hold) {
    remember("held \"" + title + "\" (" + gBusy.why + ")");
    return;
  }
  if (notificationsAllowed()) {
    native::notifyWithReply(title, body, chatId);
    remember("sent \"" + title + "\"");
  } else {
    remember("not sent \"" + title + "\" (notifications are not allowed)");
  }
}

void App::tickNotifications() {
  const double now = core::nowMillis() / 1000.0;
  if (now - gBusyCheckedAt < 2.0) return;
  if (gBusyCheckedAt == 0) native::refreshNotificationAuthorization();
  gBusyCheckedAt = now;
  notifypolicy::Busy b;
  const char* fake = std::getenv("OWUI_FAKE_BUSY");   // test hook: "meeting", "focus", or a file whose first word is one
  std::string fakeState = fake ? fake : "";
  if (!fakeState.empty() && fakeState[0] == '/') fakeState = core::trim(core::readFile(fakeState));
  if (!fakeState.empty()) {
    b.meeting = fakeState == "meeting";
    b.focus = fakeState == "focus";
    b.why = b.focus ? "Focus (test)" : b.meeting ? "meeting (test)" : "";
  } else {
    const int focus = busy::focusOn();
    b.focus = focus == 1;
    if (b.focus) b.why = "a Focus is on";
    // Our own recording (voice, push-to-talk, meeting notes' microphone) is not a meeting.
    if (!platform::recording() && busy::microphoneInUse()) {
      b.meeting = true;
      b.why = b.why.empty() ? "microphone in use" : b.why;
    }
    if (!showCamera && busy::cameraInUse()) {
      b.meeting = true;
      b.why = b.why.empty() ? "camera in use" : b.why;
    }
    // A calendar event now, when Calendar access was given (#89); checked each minute.
    if (personal::currentAccess(personal::Kind::Calendar) == personal::Access::Granted) {
      if (now - gCalendarCheckedAt > 60.0) {
        gCalendarCheckedAt = now;
        std::string error;
        const double t = static_cast<double>(std::time(nullptr));
        gCalendarBusy = false;
        for (const personal::Event& e : personal::nativeBackend().events(t - 1, t + 1, "", &error)) {
          if (!e.allDay && e.start <= t && e.end > t) gCalendarBusy = true;
        }
      }
      if (gCalendarBusy) {
        b.meeting = true;
        b.why = b.why.empty() ? "a calendar event now" : b.why;
      }
    }
  }
  gBusy = b;
  notifypolicy::Digest digest;
  std::vector<notifypolicy::Item> each;
  if (!gPolicy.tick(gBusy, static_cast<double>(std::time(nullptr)), optionsFromSettings(), &digest, &each)) return;
  if (!digest.items.empty()) {
    gLastDigest = digest;
    gDigestUnseen = true;
    if (notificationsAllowed()) native::notifyWithReply(digest.title, digest.body, digest.items.front().chatId);
    remember("digest of " + std::to_string(digest.items.size()) + ": " + digest.body);
    widgets::toastInfo(digest.title + ": " + digest.body);
  }
  for (const notifypolicy::Item& it : each) {
    if (notificationsAllowed()) native::notifyWithReply(it.title, it.body, it.chatId);
    remember("delivered held \"" + it.title + "\"");
  }
}

void App::drawNotificationsSection(float width) {
  (void)width;
  core::Settings& cfg = settings();
  const int auth = native::notificationAuthorization();
  widgets::dimText("Notifications: %s", auth == UNAuthorizationStatusAuthorized ? "allowed"
                                        : auth == UNAuthorizationStatusProvisional ? "allowed quietly"
                                        : auth == UNAuthorizationStatusDenied ? "off in System Settings"
                                        : auth == UNAuthorizationStatusNotDetermined ? "not asked yet" : "checking...");
  if (auth == UNAuthorizationStatusNotDetermined) {
    ImGui::SameLine();
    if (ImGui::SmallButton("Allow notifications")) native::requestNotificationAuthorization();
  } else if (auth == UNAuthorizationStatusDenied) {
    ImGui::SameLine();
    if (ImGui::SmallButton("Open Notifications settings")) {
      [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"x-apple.systempreferences:com.apple.Notifications-Settings.extension"]];
    }
  }
  Json j = cfg.extra.value("notifyPolicy", Json::object());
  if (!j.is_object()) j = Json::object();
  notifypolicy::Options o = optionsFromSettings();
  bool changed = false;
  changed |= ImGui::Checkbox("Hold notifications while a Focus is on", &o.respectFocus);
  changed |= ImGui::Checkbox("Hold them while you are in a meeting (microphone or camera in use, or a calendar event now)", &o.batchInMeetings);
  changed |= ImGui::Checkbox("Afterwards, one digest instead of each one", &o.digest);
  changed |= ImGui::Checkbox("Let screen-watch alerts through during meetings", &o.urgentThroughMeetings);
  if (changed) {
    j["respectFocus"] = o.respectFocus;
    j["batchInMeetings"] = o.batchInMeetings;
    j["digest"] = o.digest;
    j["urgentThroughMeetings"] = o.urgentThroughMeetings;
    cfg.extra["notifyPolicy"] = j;
    cfg.save();
  }
  widgets::dimText("Focus status: %s", busy::focusAccess());
  if (std::string(busy::focusAccess()) == "not asked yet") {
    ImGui::SameLine();
    if (ImGui::SmallButton("Allow Focus status")) busy::requestFocusAccess();
  }
  widgets::dimText("Now: %s%s", gBusy.any() ? "busy - " : "free", gBusy.why.c_str());
  if (!gPolicy.held().empty()) {
    widgets::dimText("%zu held", gPolicy.held().size());
    ImGui::SameLine();
    if (ImGui::SmallButton("Show what is held")) {
      gLastDigest = notifypolicy::makeDigest(gPolicy.held(), gBusy.why);
      gDigestUnseen = true;
    }
  }
  if (gDigestUnseen && !gLastDigest.items.empty()) {
    theme::pushBold();
    ImGui::TextUnformatted(gLastDigest.title.c_str());
    theme::pop();
    for (size_t i = 0; i < gLastDigest.items.size(); ++i) {
      const notifypolicy::Item& it = gLastDigest.items[i];
      ImGui::PushID(static_cast<int>(i));
      widgets::dimText("%s  %s - %s", hm(it.at).c_str(), it.title.c_str(), it.body.substr(0, 90).c_str());
      if (!it.chatId.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Open")) {
          pane = Pane::Chat;
          openChat(it.chatId);
        }
      }
      ImGui::PopID();
    }
    if (ImGui::SmallButton("Dismiss")) gDigestUnseen = false;
  }
  for (auto it = gRecent.rbegin(); it != gRecent.rend(); ++it) widgets::dimText("%s  %s", hm(it->at).c_str(), it->line.c_str());
}

}  // namespace app
