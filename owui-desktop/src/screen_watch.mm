// Screen watch (#112), the system side and the Tools tab: list windows,
// capture one (ScreenCaptureKit when Screen Recording is allowed; the app's
// own windows can always be captured), read its text with Live Text, ask
// the model the standing question, and stop with a toast when it says YES.
#import <Cocoa/Cocoa.h>
#import <ImageIO/ImageIO.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <dlfcn.h>
#include <unistd.h>

#include <atomic>
#include <ctime>

#include "app.h"
#include "core/async.h"
#include "core/common.h"
#include "core/screenwatch.h"
#include "core/settings.h"
#include "imgui_stdlib.h"
#include "native.h"
#include "pane_common.h"
#include "ui/a11y_imgui.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace screenwatch {

struct WindowInfo {
  uint32_t id = 0;
  std::string owner, title;
  int pid = 0;
  bool own = false;
  float width = 0, height = 0;
};

bool screenRecordingAllowed() { return CGPreflightScreenCaptureAccess(); }
void requestScreenRecording() { CGRequestScreenCaptureAccess(); }   // the system prompt: from the button only

std::vector<WindowInfo> listWindows() {
  std::vector<WindowInfo> out;
  CFArrayRef list = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
  if (!list) return out;
  const int self = getpid();
  for (NSDictionary* w in (__bridge NSArray*)list) {
    if ([w[(id)kCGWindowLayer] intValue] != 0) continue;   // menus, the Dock, overlays
    WindowInfo info;
    info.id = [w[(id)kCGWindowNumber] unsignedIntValue];
    info.owner = [w[(id)kCGWindowOwnerName] UTF8String] ?: "";
    info.title = [w[(id)kCGWindowName] UTF8String] ?: "";   // empty for others' windows without Screen Recording
    info.pid = [w[(id)kCGWindowOwnerPID] intValue];
    info.own = info.pid == self;
    CGRect bounds{};
    CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)w[(id)kCGWindowBounds], &bounds);
    info.width = static_cast<float>(bounds.size.width);
    info.height = static_cast<float>(bounds.size.height);
    if (info.width < 80 || info.height < 60) continue;
    out.push_back(info);
  }
  CFRelease(list);
  return out;
}

namespace {

std::string pngOf(CGImageRef image) {
  NSMutableData* data = [NSMutableData data];
  CGImageDestinationRef dest = CGImageDestinationCreateWithData((__bridge CFMutableDataRef)data, (__bridge CFStringRef)UTTypePNG.identifier, 1, nullptr);
  if (!dest) return "";
  CGImageDestinationAddImage(dest, image, nullptr);
  const bool ok = CGImageDestinationFinalize(dest);
  CFRelease(dest);
  return ok ? std::string(static_cast<const char*>(data.bytes), data.length) : std::string();
}

std::vector<uint8_t> thumb9x8(CGImageRef image) {
  std::vector<uint8_t> gray(72, 0);
  CGColorSpaceRef space = CGColorSpaceCreateDeviceGray();
  CGContextRef ctx = CGBitmapContextCreate(gray.data(), 9, 8, 8, 9, space, kCGImageAlphaNone);
  CGColorSpaceRelease(space);
  if (!ctx) return gray;
  CGContextSetInterpolationQuality(ctx, kCGInterpolationMedium);
  CGContextDrawImage(ctx, CGRectMake(0, 0, 9, 8), image);
  CGContextRelease(ctx);
  return gray;
}

CGImageRef captureWithScreenCaptureKit(uint32_t windowId, std::string* error) API_AVAILABLE(macos(14.0)) {
  __block CGImageRef result = nullptr;
  __block std::string failure;
  dispatch_semaphore_t done = dispatch_semaphore_create(0);
  [SCShareableContent getShareableContentExcludingDesktopWindows:YES
                                             onScreenWindowsOnly:YES
                                               completionHandler:^(SCShareableContent* content, NSError* err) {
                                                 SCWindow* target = nil;
                                                 for (SCWindow* w in content.windows) {
                                                   if (w.windowID == windowId) target = w;
                                                 }
                                                 if (!target) {
                                                   failure = err ? err.localizedDescription.UTF8String : "that window is gone";
                                                   dispatch_semaphore_signal(done);
                                                   return;
                                                 }
                                                 SCContentFilter* filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:target];
                                                 SCStreamConfiguration* config = [[SCStreamConfiguration alloc] init];
                                                 const CGFloat scale = filter.pointPixelScale > 0 ? filter.pointPixelScale : 2.0;
                                                 config.width = static_cast<size_t>(target.frame.size.width * scale);
                                                 config.height = static_cast<size_t>(target.frame.size.height * scale);
                                                 config.showsCursor = NO;
                                                 [SCScreenshotManager captureImageWithFilter:filter
                                                                               configuration:config
                                                                           completionHandler:^(CGImageRef image, NSError* captureError) {
                                                                             if (image) result = CGImageRetain(image);
                                                                             else failure = captureError ? captureError.localizedDescription.UTF8String : "capture failed";
                                                                             dispatch_semaphore_signal(done);
                                                                           }];
                                               }];
  if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 15 * NSEC_PER_SEC)) != 0) failure = "the capture did not answer in time";
  if (!result) *error = failure;
  return result;
}

}  // namespace

// Blocking (a worker). PNG for the model, a 9x8 grayscale thumbnail for change detection.
bool captureWindow(uint32_t windowId, bool own, std::string* png, std::vector<uint8_t>* gray, std::string* error) {
  CGImageRef image = nullptr;
  if (!own && !screenRecordingAllowed()) {
    *error = "watching another app's window needs Screen Recording (System Settings > Privacy & Security > Screen & System Audio Recording)";
    return false;
  }
  if (@available(macOS 14.0, *)) {
    if (screenRecordingAllowed()) image = captureWithScreenCaptureKit(windowId, error);
  }
  if (!image) {
    // This app's own windows (and older systems): the window server's copy.
    // The SDK no longer declares it for new code, so it is looked up at run time.
    using CreateImage = CGImageRef (*)(CGRect, CGWindowListOption, CGWindowID, CGWindowImageOption);
    static CreateImage create = reinterpret_cast<CreateImage>(dlsym(RTLD_DEFAULT, "CGWindowListCreateImage"));
    if (create) {
      image = create(CGRectNull, kCGWindowListOptionIncludingWindow, windowId, kCGWindowImageBoundsIgnoreFraming | kCGWindowImageNominalResolution);
    } else if (error->empty()) {
      *error = "this macOS has no way to capture a window without Screen Recording";
    }
  }
  if (!image) {
    if (error->empty()) *error = "the window could not be captured (closed, or on another Space?)";
    return false;
  }
  error->clear();
  *png = pngOf(image);
  *gray = thumb9x8(image);
  CGImageRelease(image);
  return !png->empty();
}

}  // namespace screenwatch

namespace app {
namespace {

struct WatchState {
  bool running = false;
  bool busy = false;
  screenwatch::Config config;
  screenwatch::Gate gate;
  bool own = false;
  double nextAt = 0;
  std::vector<screenwatch::Check> history;   // newest last
  std::string status;
  int asked = 0, skipped = 0;
};
WatchState gWatch;
std::vector<screenwatch::WindowInfo> gWindows;
uint32_t gPicked = 0;
std::string gQuestion = "Has it finished?";
int gInterval = 30;
int gMode = 0;   // 0 text, 1 screenshot
std::string gModel;

std::string windowLabel(const screenwatch::WindowInfo& w) {
  std::string label = w.owner;
  if (!w.title.empty()) label += " - " + w.title;
  if (w.own) label += "  (this app)";
  return label;
}

void record(screenwatch::Check check) {
  gWatch.history.push_back(std::move(check));
  if (gWatch.history.size() > 200) gWatch.history.erase(gWatch.history.begin());
}

}  // namespace

void App::startScreenWatch(uint32_t windowId, const std::string& label, bool own, const std::string& question, int intervalSec,
                           bool image, const std::string& model) {
  if (windowId == 0 || core::trim(question).empty() || model.empty()) {
    widgets::toastError("Pick a window, a question and a model first");
    return;
  }
  gWatch = WatchState{};
  gWatch.running = true;
  gWatch.own = own;
  gWatch.config.windowId = windowId;
  gWatch.config.windowLabel = label;
  gWatch.config.question = question;
  gWatch.config.intervalSec = screenwatch::clampInterval(intervalSec);
  gWatch.config.mode = image ? screenwatch::Mode::Image : screenwatch::Mode::Text;
  gWatch.config.model = model;
  gWatch.nextAt = 0;   // first look straight away
  gWatch.status = "Watching " + label;
  widgets::toastInfo("Watching " + label);
}

void App::stopScreenWatch(const std::string& why) {
  if (!gWatch.running) return;
  gWatch.running = false;
  gWatch.status = why;
}

bool App::screenWatchRunning() const { return gWatch.running; }

void App::tickScreenWatch() {
  if (!gWatch.running || gWatch.busy || !signedIn_) return;
  const double now = static_cast<double>(std::time(nullptr));
  if (now < gWatch.nextAt) return;
  gWatch.busy = true;
  gWatch.nextAt = now + gWatch.config.intervalSec;
  const screenwatch::Config config = gWatch.config;
  const bool own = gWatch.own;
  async::run([this, config, own, now] {
    std::string png, error;
    std::vector<uint8_t> gray;
    if (!screenwatch::captureWindow(config.windowId, own, &png, &gray, &error)) {
      async::post([error, now] {
        gWatch.busy = false;
        screenwatch::Check check;
        check.at = now;
        check.error = error;
        record(check);
      });
      return;
    }
    std::string text;
    if (config.mode == screenwatch::Mode::Text) text = native::recognizeText(png);
    const uint64_t fingerprint = config.mode == screenwatch::Mode::Text ? screenwatch::textFingerprint(text) : screenwatch::imageFingerprint(gray);
    async::post([this, config, png, text, fingerprint, now] {
      if (!gWatch.running || gWatch.config.windowId != config.windowId) {
        gWatch.busy = false;
        return;
      }
      if (!gWatch.gate.shouldAsk(fingerprint, config.mode, config.maxUnchangedSkips)) {
        gWatch.busy = false;
        ++gWatch.skipped;
        screenwatch::Check check;
        check.at = now;
        check.skipped = true;
        record(check);
        return;
      }
      ++gWatch.asked;
      const Json payload{{"model", config.model},
                         {"stream", false},
                         {"messages", screenwatch::buildMessages(config, text, config.mode == screenwatch::Mode::Image ? core::base64Encode(png) : "")}};
      async::run([this, payload, config, now] {
        api::Result res = api_.completion(payload);
        std::string reply, error;
        if (res.ok && res.data.contains("choices") && res.data["choices"].is_array() && !res.data["choices"].empty()) {
          reply = res.data["choices"][0].value("message", Json::object()).value("content", "");
        } else {
          error = api::errorMessage(res);
        }
        async::post([this, reply, error, config, now] {
          gWatch.busy = false;
          screenwatch::Check check;
          check.at = now;
          check.error = error;
          if (error.empty()) check.verdict = screenwatch::parseVerdict(reply);
          record(check);
          if (!gWatch.running || !check.verdict.met) return;
          // The question came true.
          const std::string line = "Screen watch: " + config.question + " - yes. " + check.verdict.note;
          widgets::toastInfo(line);
          if (settings().extra.value("screenWatchNotify", false)) notify(1, "Screen watch", config.question + " - " + check.verdict.note, "");
          if (config.stopWhenMet) stopScreenWatch("Done: " + check.verdict.note);
        });
      });
    });
  });
}

void App::drawScreenWatchSection(float width) {
  widgets::sectionHeader("SCREEN WATCH");
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + std::max(200.0f, width - 24.0f));
  widgets::dimText("%s", "Pick a window and a question (\"Has the build finished?\"). Every so often the window is captured and either "
                         "its text (read with Live Text - any model) or the screenshot (a vision model) goes to the model; when it "
                         "answers yes you get a toast and the watch stops. An unchanged window is not asked about again.");
  ImGui::PopTextWrapPos();

  const bool allowed = screenwatch::screenRecordingAllowed();
  widgets::dimText("Screen Recording: %s", allowed ? "allowed - any window can be watched" : "not allowed - only this app's own windows can be watched");
  if (!allowed) {
    ImGui::SameLine();
    if (ImGui::SmallButton("Allow Screen Recording")) screenwatch::requestScreenRecording();
    ImGui::SameLine();
    if (ImGui::SmallButton("Open its settings")) {
      [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture"]];
    }
  }

  if (gWindows.empty() || ImGui::Button("Refresh the window list")) gWindows = screenwatch::listWindows();
  ImGui::BeginChild("##watchwindows", ImVec2(std::max(200.0f, width - 24.0f), 150), ImGuiChildFlags_Borders);
  for (const screenwatch::WindowInfo& w : gWindows) {
    const std::string label = windowLabel(w) + "##w" + std::to_string(w.id);
    const bool usable = w.own || allowed;
    widgets::beginDisabled(!usable);
    if (ImGui::Selectable(label.c_str(), gPicked == w.id)) gPicked = w.id;
    widgets::endDisabled(!usable);
  }
  ImGui::EndChild();

  ImGui::SetNextItemWidth(std::max(200.0f, width - 140.0f));
  ImGui::InputText("Question", &gQuestion);
  ImGui::SetNextItemWidth(220.0f);
  ImGui::SliderInt("seconds between looks", &gInterval, 10, 600);
  ImGui::RadioButton("Send the text on screen (any model)", &gMode, 0);
  ImGui::SameLine();
  ImGui::RadioButton("Send the screenshot (a vision model)", &gMode, 1);
  if (gModel.empty() && !selectedModels.empty()) gModel = selectedModels.front();
  ImGui::SetNextItemWidth(320.0f);
  if (ImGui::BeginCombo("Model", gModel.empty() ? "(choose)" : gModel.c_str())) {
    for (const ModelInfo& m : models) {
      if (ImGui::Selectable(m.id.c_str(), m.id == gModel)) gModel = m.id;
    }
    ImGui::EndCombo();
  }
  bool notify = settings().extra.value("screenWatchNotify", false);
  if (ImGui::Checkbox("Also show a macOS notification (macOS asks once)", &notify)) {
    settings().extra["screenWatchNotify"] = notify;
    settings().save();
  }
  if (gWatch.running) {
    if (widgets::dangerButton("Stop watching")) stopScreenWatch("Stopped");
  } else {
    widgets::beginDisabled(gPicked == 0);
    if (widgets::primaryButton("Start watching")) {
      for (const screenwatch::WindowInfo& w : gWindows) {
        if (w.id == gPicked) startScreenWatch(w.id, windowLabel(w), w.own, gQuestion, gInterval, gMode == 1, gModel);
      }
    }
    widgets::endDisabled(gPicked == 0);
  }
  if (!gWatch.status.empty()) {
    widgets::dimText("%s  (%d asked, %d unchanged)", gWatch.status.c_str(), gWatch.asked, gWatch.skipped);
  }
  for (auto it = gWatch.history.rbegin(); it != gWatch.history.rend() && it - gWatch.history.rbegin() < 30; ++it) {
    widgets::dimText("%s", screenwatch::describe(*it).c_str());
  }
}

}  // namespace app
