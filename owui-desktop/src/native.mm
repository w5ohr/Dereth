#include "core/i18n.h"
#include "native.h"

#import <Carbon/Carbon.h>
#import <Cocoa/Cocoa.h>
#import <ServiceManagement/ServiceManagement.h>
#import <CoreServices/CoreServices.h>
#import <CoreSpotlight/CoreSpotlight.h>
#import <Quartz/Quartz.h>
#import <UserNotifications/UserNotifications.h>
#import <Vision/Vision.h>
#import <WebKit/WebKit.h>

#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <type_traits>

#include <nlohmann/json.hpp>

#include "core/common.h"
#include "imgui.h"

namespace native {
namespace {

Callbacks gCallbacks;
__weak NSWindow* gCurrentWindow = nil;   // whose view is running (#121)
std::function<void(void*, const std::function<void()>&)> gWindowScope;
EventHotKeyRef gHotkeyRef = nullptr;
NSStatusItem* gStatusItem = nil;
NSUserActivity* gActivity = nil;
NSProgressIndicator* gDockProgress = nil;
int gNextWatchToken = 1;
std::map<int, FSEventStreamRef> gWatchers;
std::map<int, std::function<void(const std::vector<std::string>&)>> gWatchCallbacks;

OSStatus hotkeyHandler(EventHandlerCallRef, EventRef event, void*) {
  EventHotKeyID id{};
  GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr, sizeof(id), nullptr, &id);
  if (id.id == 2) {
    // Capture: leave whatever app is in front alone; the capture UI is the OS's.
    if (gCallbacks.captureScreen) gCallbacks.captureScreen();
    return noErr;
  }
  if (gCallbacks.quickAsk) {
    [NSApp activateIgnoringOtherApps:YES];
    gCallbacks.quickAsk();
  }
  return noErr;
}

std::string toStd(NSString* s) { return s ? std::string(s.UTF8String) : std::string(); }
NSString* toNs(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()]; }

}  // namespace
}  // namespace native

// ---------------------------------------------------------------- ObjC glue

@interface OWUINativeBridge : NSObject <NSUserNotificationCenterDelegate,
                                         UNUserNotificationCenterDelegate,
                                         QLPreviewPanelDataSource, QLPreviewPanelDelegate>
@property(nonatomic, strong) NSURL* quickLookUrl;
@end

static OWUINativeBridge* gBridge = nil;

@implementation OWUINativeBridge

// --- Services: "Ask Open WebUI" on any selected text.
- (void)askService:(NSPasteboard*)pboard userData:(NSString*)data error:(NSString**)error {
  (void)data;
  (void)error;
  NSString* text = [pboard stringForType:NSPasteboardTypeString];
  if (text && native::gCallbacks.askText) {
    [NSApp activateIgnoringOtherApps:YES];
    native::gCallbacks.askText(native::toStd(text));
  }
}

// --- URL scheme (owui://...).
- (void)handleGetURL:(NSAppleEventDescriptor*)event withReply:(NSAppleEventDescriptor*)reply {
  (void)reply;
  NSString* url = [[event paramDescriptorForKeyword:keyDirectObject] stringValue];
  if (url && native::gCallbacks.openUrl) {
    [NSApp activateIgnoringOtherApps:YES];
    native::gCallbacks.openUrl(native::toStd(url));
  }
}

// --- Status item actions.
- (void)menuQuickAsk:(id)sender { (void)sender; if (native::gCallbacks.quickAsk) { [NSApp activateIgnoringOtherApps:YES]; native::gCallbacks.quickAsk(); } }
- (void)menuNewChat:(id)sender { (void)sender; if (native::gCallbacks.newChat) { [NSApp activateIgnoringOtherApps:YES]; native::gCallbacks.newChat(); } }
- (void)menuShow:(id)sender { (void)sender; if (native::gCallbacks.showWindow) { [NSApp activateIgnoringOtherApps:YES]; native::gCallbacks.showWindow(); } }
- (void)menuOpenChat:(NSMenuItem*)item {
  if (native::gCallbacks.openChat) {
    [NSApp activateIgnoringOtherApps:YES];
    native::gCallbacks.openChat(native::toStd(item.representedObject));
  }
}
- (void)menuQuit:(id)sender { (void)sender; [NSApp terminate:nil]; }

// --- Notification responses, including the inline reply field.
- (void)userNotificationCenter:(UNUserNotificationCenter*)center
    didReceiveNotificationResponse:(UNNotificationResponse*)response
             withCompletionHandler:(void (^)(void))completionHandler {
  (void)center;
  if ([response isKindOfClass:[UNTextInputNotificationResponse class]]) {
    NSString* text = ((UNTextInputNotificationResponse*)response).userText;
    if (native::gCallbacks.notificationReply) native::gCallbacks.notificationReply(native::toStd(text));
  } else {
    NSString* chatId = response.notification.request.content.userInfo[@"chatId"];
    [NSApp activateIgnoringOtherApps:YES];
    if (chatId.length && native::gCallbacks.openChat) native::gCallbacks.openChat(native::toStd(chatId));
  }
  completionHandler();
}

- (void)userNotificationCenter:(UNUserNotificationCenter*)center
       willPresentNotification:(UNNotification*)notification
         withCompletionHandler:(void (^)(UNNotificationPresentationOptions))completionHandler {
  (void)center;
  (void)notification;
  completionHandler(UNNotificationPresentationOptionBanner | UNNotificationPresentationOptionSound);
}

// --- Quick Look.
- (NSInteger)numberOfPreviewItemsInPreviewPanel:(QLPreviewPanel*)panel { (void)panel; return self.quickLookUrl ? 1 : 0; }
- (id<QLPreviewItem>)previewPanel:(QLPreviewPanel*)panel previewItemAtIndex:(NSInteger)index { (void)panel; (void)index; return self.quickLookUrl; }

// --- Appearance.
- (void)appearanceChanged:(NSNotification*)note {
  (void)note;
  if (native::gCallbacks.appearanceChanged) native::gCallbacks.appearanceChanged(native::systemIsDark());
}

@end

// Drop target layered over the Metal view: the window's content view already
// belongs to the MTKView, so we subclass-less register on it directly through
// a transparent overlay view that forwards everything else.
@interface OWUIDropView : NSView
@end

@implementation OWUIDropView
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
  (void)sender;
  return NSDragOperationCopy;
}
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
  NSArray* urls = [sender.draggingPasteboard readObjectsForClasses:@[ [NSURL class] ]
                                                           options:@{NSPasteboardURLReadingFileURLsOnlyKey : @YES}];
  std::vector<std::string> paths;
  for (NSURL* url in urls) paths.push_back(native::toStd(url.path));
  if (!paths.empty() && native::gCallbacks.dropFiles) native::gCallbacks.dropFiles(paths);
  return paths.empty() ? NO : YES;
}
- (NSView*)hitTest:(NSPoint)point { (void)point; return nil; }   // mouse goes to the Metal view
@end

namespace native {

void install(const Callbacks& callbacks) {
  gCallbacks = callbacks;
  if (!gBridge) gBridge = [[OWUINativeBridge alloc] init];

  // Sleep and wake, for the model warmup (released before sleep, re-warmed after).
  static bool sleepObserved = false;
  if (!sleepObserved) {
    sleepObserved = true;
    NSNotificationCenter* center = [NSWorkspace sharedWorkspace].notificationCenter;
    [center addObserverForName:NSWorkspaceWillSleepNotification object:nil queue:[NSOperationQueue mainQueue] usingBlock:^(NSNotification*) {
      if (gCallbacks.sleepWake) gCallbacks.sleepWake(true);
    }];
    [center addObserverForName:NSWorkspaceDidWakeNotification object:nil queue:[NSOperationQueue mainQueue] usingBlock:^(NSNotification*) {
      if (gCallbacks.sleepWake) gCallbacks.sleepWake(false);
    }];
  }

  NSApp.servicesProvider = gBridge;
  NSUpdateDynamicServices();

  [[NSAppleEventManager sharedAppleEventManager]
      setEventHandler:gBridge
          andSelector:@selector(handleGetURL:withReply:)
        forEventClass:kInternetEventClass
           andEventID:kAEGetURL];

  UNUserNotificationCenter* center = [UNUserNotificationCenter currentNotificationCenter];
  center.delegate = gBridge;
  UNTextInputNotificationAction* reply =
      [UNTextInputNotificationAction actionWithIdentifier:@"reply"
                                                    title:@"Reply"
                                                  options:0
                                     textInputButtonTitle:@"Send"
                                     textInputPlaceholder:@"Follow up..."];
  UNNotificationCategory* category =
      [UNNotificationCategory categoryWithIdentifier:@"chat-reply"
                                             actions:@[ reply ]
                                   intentIdentifiers:@[]
                                             options:0];
  [center setNotificationCategories:[NSSet setWithObject:category]];

  [[NSDistributedNotificationCenter defaultCenter]
      addObserver:gBridge
         selector:@selector(appearanceChanged:)
             name:@"AppleInterfaceThemeChangedNotification"
           object:nil];
}

bool registerHotkey(int keyCode, bool cmd, bool alt, bool ctrl, bool shift) {
  unregisterHotkey();
  static bool handlerInstalled = false;
  if (!handlerInstalled) {
    EventTypeSpec spec = {kEventClassKeyboard, kEventHotKeyPressed};
    InstallApplicationEventHandler(&hotkeyHandler, 1, &spec, nullptr, nullptr);
    handlerInstalled = true;
  }
  UInt32 modifiers = 0;
  if (cmd) modifiers |= cmdKey;
  if (alt) modifiers |= optionKey;
  if (ctrl) modifiers |= controlKey;
  if (shift) modifiers |= shiftKey;
  EventHotKeyID id = {'OWUI', 1};
  OSStatus status = RegisterEventHotKey(static_cast<UInt32>(keyCode), modifiers, id,
                                        GetApplicationEventTarget(), 0, &gHotkeyRef);
  return status == noErr;
}

void unregisterHotkey() {
  if (gHotkeyRef) {
    UnregisterEventHotKey(gHotkeyRef);
    gHotkeyRef = nullptr;
  }
}

static EventHotKeyRef gCaptureHotkeyRef = nullptr;

bool registerCaptureHotkey(int keyCode, bool cmd, bool alt, bool ctrl, bool shift) {
  unregisterCaptureHotkey();
  static bool handlerInstalled = false;
  if (!handlerInstalled && !gHotkeyRef) {
    EventTypeSpec spec = {kEventClassKeyboard, kEventHotKeyPressed};
    InstallApplicationEventHandler(&hotkeyHandler, 1, &spec, nullptr, nullptr);
    handlerInstalled = true;
  }
  UInt32 modifiers = 0;
  if (cmd) modifiers |= cmdKey;
  if (alt) modifiers |= optionKey;
  if (ctrl) modifiers |= controlKey;
  if (shift) modifiers |= shiftKey;
  EventHotKeyID id = {'OWUI', 2};
  return RegisterEventHotKey(static_cast<UInt32>(keyCode), modifiers, id, GetApplicationEventTarget(), 0,
                             &gCaptureHotkeyRef) == noErr;
}

void unregisterCaptureHotkey() {
  if (gCaptureHotkeyRef) {
    UnregisterEventHotKey(gCaptureHotkeyRef);
    gCaptureHotkeyRef = nullptr;
  }
}

bool captureScreen(bool interactive, std::string* png) {
  NSString* path = [NSTemporaryDirectory() stringByAppendingPathComponent:
                    [NSString stringWithFormat:@"owui-capture-%d.png", static_cast<int>(arc4random())]];
  NSTask* task = [[NSTask alloc] init];
  task.executableURL = [NSURL fileURLWithPath:@"/usr/sbin/screencapture"];
  NSMutableArray* args = [NSMutableArray arrayWithObjects:@"-x", @"-t", @"png", nil];
  if (interactive) [args addObject:@"-i"];
  [args addObject:path];
  task.arguments = args;
  NSError* error = nil;
  if (![task launchAndReturnError:&error]) return false;
  [task waitUntilExit];
  NSData* data = [NSData dataWithContentsOfFile:path];
  [[NSFileManager defaultManager] removeItemAtPath:path error:nil];
  if (!data || data.length == 0) return false;   // cancelled with Escape
  png->assign(static_cast<const char*>(data.bytes), data.length);
  return true;
}

void showMainWindow() {
  [NSApp activateIgnoringOtherApps:YES];
  NSWindow* window = NSApp.mainWindow;
  if (!window) {
    // After a close in menu-bar-only mode there is no main window; find the
    // chat window, skipping the offscreen renderer and other helper windows.
    for (NSWindow* w in NSApp.windows) {
      if (w.frame.origin.x < -10000 || !w.contentViewController) continue;
      window = w;
      break;
    }
  }
  if (!window) window = NSApp.windows.firstObject;
  [window makeKeyAndOrderFront:nil];
}

static bool gMenuBarOnly = false;
static id gKeyEquivalentMonitor = nil;

void setMenuBarOnly(bool on) {
  gMenuBarOnly = on;
  // An accessory app has no menu bar on screen, and macOS stops routing
  // Cmd-key equivalents through the main menu; feed them to it by hand.
  if (!gKeyEquivalentMonitor) {
    gKeyEquivalentMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
                                                                  handler:^NSEvent*(NSEvent* event) {
                                                                    if (!gMenuBarOnly) return event;
                                                                    if (!(event.modifierFlags & NSEventModifierFlagCommand)) return event;
                                                                    if ([NSApp.mainMenu performKeyEquivalent:event]) return nil;
                                                                    return event;
                                                                  }];
  }
  // The closed window must survive so the extra can bring it back.
  for (NSWindow* w in NSApp.windows) w.releasedWhenClosed = NO;
  [NSApp setActivationPolicy:on ? NSApplicationActivationPolicyAccessory : NSApplicationActivationPolicyRegular];
  if (on) [NSApp activateIgnoringOtherApps:YES];
}

bool menuBarOnly() { return gMenuBarOnly; }

bool setLoginItem(bool on, std::string* error) {
  if (@available(macOS 13.0, *)) {
    NSError* err = nil;
    SMAppService* service = [SMAppService mainAppService];
    const BOOL ok = on ? [service registerAndReturnError:&err] : [service unregisterAndReturnError:&err];
    if (!ok && error) *error = err ? err.localizedDescription.UTF8String : "unknown error";
    return ok;
  }
  if (error) *error = "needs macOS 13 or later";
  return false;
}

int loginItemStatus() {
  if (@available(macOS 13.0, *)) {
    switch ([SMAppService mainAppService].status) {
      case SMAppServiceStatusEnabled: return 1;
      case SMAppServiceStatusRequiresApproval: return 2;
      case SMAppServiceStatusNotFound: return 3;
      default: return 0;
    }
  }
  return 0;
}

void refreshStatusMenu() {
  if (!gStatusItem) return;
  NSMenu* menu = [[NSMenu alloc] init];
  NSMenuItem* ask = [menu addItemWithTitle:@"Quick Ask..." action:@selector(menuQuickAsk:) keyEquivalent:@""];
  ask.target = gBridge;
  NSMenuItem* fresh = [menu addItemWithTitle:@"New Chat" action:@selector(menuNewChat:) keyEquivalent:@""];
  fresh.target = gBridge;
  [menu addItem:[NSMenuItem separatorItem]];
  if (gCallbacks.recentChats) {
    for (const auto& [id, title] : gCallbacks.recentChats()) {
      NSMenuItem* item = [menu addItemWithTitle:toNs(title) action:@selector(menuOpenChat:) keyEquivalent:@""];
      item.target = gBridge;
      item.representedObject = toNs(id);
    }
    [menu addItem:[NSMenuItem separatorItem]];
  }
  NSMenuItem* show = [menu addItemWithTitle:@"Show Open WebUI" action:@selector(menuShow:) keyEquivalent:@""];
  show.target = gBridge;
  NSMenuItem* quit = [menu addItemWithTitle:@"Quit" action:@selector(menuQuit:) keyEquivalent:@""];
  quit.target = gBridge;
  gStatusItem.menu = menu;
}

void setStatusItemVisible(bool visible) {
  if (visible && !gStatusItem) {
    gStatusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
    NSImage* image = [NSImage imageWithSystemSymbolName:@"bubble.left.and.bubble.right"
                                   accessibilityDescription:@"Open WebUI"];
    [image setTemplate:YES];
    gStatusItem.button.image = image;
    refreshStatusMenu();
  } else if (!visible && gStatusItem) {
    [[NSStatusBar systemStatusBar] removeStatusItem:gStatusItem];
    gStatusItem = nil;
  }
}

void enableDropOnMainWindow() {
  NSWindow* window = NSApp.mainWindow ?: NSApp.windows.firstObject;
  if (!window) return;
  NSView* content = window.contentView;
  for (NSView* sub in content.subviews) {
    if ([sub isKindOfClass:[OWUIDropView class]]) return;
  }
  OWUIDropView* overlay = [[OWUIDropView alloc] initWithFrame:content.bounds];
  overlay.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  [overlay registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];
  [content addSubview:overlay positioned:NSWindowAbove relativeTo:nil];
}

void indexChatForSpotlight(const std::string& chatId, const std::string& title,
                           const std::string& preview) {
  CSSearchableItemAttributeSet* attributes =
      [[CSSearchableItemAttributeSet alloc] initWithContentType:UTTypeText];
  attributes.title = toNs(title);
  attributes.contentDescription = toNs(preview);
  attributes.keywords = @[ @"Open WebUI", @"chat" ];
  CSSearchableItem* item = [[CSSearchableItem alloc] initWithUniqueIdentifier:toNs(chatId)
                                                            domainIdentifier:@"chats"
                                                                attributeSet:attributes];
  [[CSSearchableIndex defaultSearchableIndex] indexSearchableItems:@[ item ] completionHandler:nil];
}

void removeChatFromSpotlight(const std::string& chatId) {
  [[CSSearchableIndex defaultSearchableIndex] deleteSearchableItemsWithIdentifiers:@[ toNs(chatId) ]
                                                                 completionHandler:nil];
}

void clearSpotlightIndex() {
  [[CSSearchableIndex defaultSearchableIndex] deleteAllSearchableItemsWithCompletionHandler:nil];
}

void notifyWithReply(const std::string& title, const std::string& body, const std::string& chatId) {
  UNUserNotificationCenter* center = [UNUserNotificationCenter currentNotificationCenter];
  [center requestAuthorizationWithOptions:UNAuthorizationOptionAlert | UNAuthorizationOptionSound
                        completionHandler:^(BOOL, NSError*) {}];
  UNMutableNotificationContent* content = [[UNMutableNotificationContent alloc] init];
  content.title = toNs(title);
  content.body = toNs(body);
  content.categoryIdentifier = @"chat-reply";
  content.userInfo = @{@"chatId" : toNs(chatId)};
  content.sound = [UNNotificationSound defaultSound];
  UNNotificationRequest* request =
      [UNNotificationRequest requestWithIdentifier:[[NSUUID UUID] UUIDString] content:content trigger:nil];
  [center addNotificationRequest:request withCompletionHandler:nil];
}

void setDockBadge(const std::string& text) {
  NSApp.dockTile.badgeLabel = text.empty() ? nil : toNs(text);
}

void setDockProgress(double fraction) {
  NSDockTile* tile = NSApp.dockTile;
  if (fraction < 0.0) {
    if (gDockProgress) {
      [gDockProgress removeFromSuperview];
      gDockProgress = nil;
      tile.contentView = nil;
      [tile display];
    }
    return;
  }
  if (!gDockProgress) {
    NSImageView* icon = [[NSImageView alloc] initWithFrame:NSMakeRect(0, 0, 128, 128)];
    icon.image = NSApp.applicationIconImage;
    gDockProgress = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(8, 10, 112, 14)];
    gDockProgress.style = NSProgressIndicatorStyleBar;
    gDockProgress.indeterminate = NO;
    gDockProgress.minValue = 0;
    gDockProgress.maxValue = 1;
    [icon addSubview:gDockProgress];
    tile.contentView = icon;
  }
  gDockProgress.doubleValue = fraction;
  [tile display];
}

void quickLook(const std::string& path) {
  if (!gBridge) return;
  gBridge.quickLookUrl = [NSURL fileURLWithPath:toNs(path)];
  QLPreviewPanel* panel = [QLPreviewPanel sharedPreviewPanel];
  panel.dataSource = gBridge;
  panel.delegate = gBridge;
  [panel makeKeyAndOrderFront:nil];
  [panel reloadData];
}

std::string recognizeText(const std::string& imageBytes) {
  NSData* data = [NSData dataWithBytes:imageBytes.data() length:imageBytes.size()];
  NSImage* image = [[NSImage alloc] initWithData:data];
  if (!image) return {};
  CGImageRef cg = [image CGImageForProposedRect:nil context:nil hints:nil];
  if (!cg) return {};
  __block NSMutableString* result = [NSMutableString string];
  VNRecognizeTextRequest* request = [[VNRecognizeTextRequest alloc] initWithCompletionHandler:^(VNRequest* req, NSError*) {
    for (VNRecognizedTextObservation* obs in req.results) {
      VNRecognizedText* best = [[obs topCandidates:1] firstObject];
      if (best) [result appendFormat:@"%@\n", best.string];
    }
  }];
  request.recognitionLevel = VNRequestTextRecognitionLevelAccurate;
  request.usesLanguageCorrection = NO;   // code: keep tokens as they are
  VNImageRequestHandler* handler = [[VNImageRequestHandler alloc] initWithCGImage:cg options:@{}];
  [handler performRequests:@[ request ] error:nil];
  return toStd(result);
}

void updateHandoff(const std::string& webUrl, const std::string& title) {
  if (gActivity) [gActivity invalidate];
  gActivity = [[NSUserActivity alloc] initWithActivityType:@"com.davidcochran.owui-desktop.chat"];
  gActivity.title = toNs(title);
  gActivity.webpageURL = [NSURL URLWithString:toNs(webUrl)];
  gActivity.eligibleForHandoff = YES;
  gActivity.eligibleForSearch = NO;
  [gActivity becomeCurrent];
}

void clearHandoff() {
  if (gActivity) {
    [gActivity invalidate];
    gActivity = nil;
  }
}

bool systemIsDark() {
  NSString* style = [[NSUserDefaults standardUserDefaults] stringForKey:@"AppleInterfaceStyle"];
  return style && [style isEqualToString:@"Dark"];
}

void setWindowTitle(const std::string& title) {
  NSWindow* window = NSApp.mainWindow ?: NSApp.windows.firstObject;
  if (window) window.title = toNs(title);
}

static void fsEventsCallback(ConstFSEventStreamRef, void* info, size_t count, void* paths,
                             const FSEventStreamEventFlags*, const FSEventStreamEventId*) {
  int token = static_cast<int>(reinterpret_cast<intptr_t>(info));
  auto it = gWatchCallbacks.find(token);
  if (it == gWatchCallbacks.end()) return;
  std::vector<std::string> changed;
  char** list = static_cast<char**>(paths);
  for (size_t i = 0; i < count; ++i) changed.emplace_back(list[i]);
  it->second(changed);
}

int watchFolder(const std::string& path, std::function<void(const std::vector<std::string>&)> onChange) {
  int token = gNextWatchToken++;
  gWatchCallbacks[token] = std::move(onChange);
  FSEventStreamContext context = {0, reinterpret_cast<void*>(static_cast<intptr_t>(token)), nullptr, nullptr, nullptr};
  NSArray* paths = @[ toNs(path) ];
  FSEventStreamRef stream = FSEventStreamCreate(nullptr, &fsEventsCallback, &context,
                                                (__bridge CFArrayRef)paths, kFSEventStreamEventIdSinceNow,
                                                1.0, kFSEventStreamCreateFlagFileEvents);
  FSEventStreamScheduleWithRunLoop(stream, CFRunLoopGetMain(), kCFRunLoopDefaultMode);
  FSEventStreamStart(stream);
  gWatchers[token] = stream;
  return token;
}

void stopWatching(int token) {
  auto it = gWatchers.find(token);
  if (it == gWatchers.end()) return;
  FSEventStreamStop(it->second);
  FSEventStreamInvalidate(it->second);
  FSEventStreamRelease(it->second);
  gWatchers.erase(it);
  gWatchCallbacks.erase(token);
}

ProcessResult runProcess(const std::vector<std::string>& argv, const std::string& workdir,
                         int timeoutSeconds, bool sandboxed) {
  ProcessResult result;
  if (argv.empty()) return result;
  NSTask* task = [[NSTask alloc] init];
  NSMutableArray* args = [NSMutableArray array];
  if (sandboxed) {
    // Deny network, allow reads everywhere, writes only inside the workdir.
    NSString* profile = [NSString stringWithFormat:
        @"(version 1)(allow default)(deny network*)"
         "(deny file-write* (subpath \"/\"))"
         "(allow file-write* (subpath \"%@\"))(allow file-write* (subpath \"/private/tmp\"))"
         "(allow file-write* (subpath \"/private/var/folders\"))",
        toNs(workdir)];
    task.executableURL = [NSURL fileURLWithPath:@"/usr/bin/sandbox-exec"];
    [args addObject:@"-p"];
    [args addObject:profile];
    for (const auto& a : argv) [args addObject:toNs(a)];
  } else {
    task.executableURL = [NSURL fileURLWithPath:@"/usr/bin/env"];
    for (const auto& a : argv) [args addObject:toNs(a)];
  }
  task.arguments = args;
  task.currentDirectoryURL = [NSURL fileURLWithPath:toNs(workdir)];
  NSPipe* pipe = [NSPipe pipe];
  task.standardOutput = pipe;
  task.standardError = pipe;
  NSError* error = nil;
  if (![task launchAndReturnError:&error]) {
    result.output = toStd(error.localizedDescription);
    return result;
  }
  NSFileHandle* handle = pipe.fileHandleForReading;
  __block NSMutableData* collected = [NSMutableData data];
  dispatch_semaphore_t done = dispatch_semaphore_create(0);
  dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
    [collected appendData:[handle readDataToEndOfFile]];
    dispatch_semaphore_signal(done);
  });
  dispatch_time_t deadline = dispatch_time(DISPATCH_TIME_NOW, (int64_t)timeoutSeconds * NSEC_PER_SEC);
  if (dispatch_semaphore_wait(done, deadline) != 0) {
    [task terminate];
    result.timedOut = true;
    dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
  }
  [task waitUntilExit];
  result.exitCode = task.terminationStatus;
  result.output = std::string(static_cast<const char*>(collected.bytes), collected.length);
  if (result.output.size() > 200000) result.output = result.output.substr(0, 200000) + "\n...[truncated]";
  return result;
}

std::string homeDirectory() { return toStd(NSHomeDirectory()); }

std::vector<std::string> chooseFolder() {
  NSOpenPanel* panel = [NSOpenPanel openPanel];
  panel.canChooseDirectories = YES;
  panel.canChooseFiles = NO;
  panel.allowsMultipleSelection = NO;
  std::vector<std::string> out;
  if ([panel runModal] == NSModalResponseOK) {
    for (NSURL* url in panel.URLs) out.push_back(toStd(url.path));
  }
  return out;
}

}  // namespace native

// ---------------------------------------------------------------- composer

@class OWUIComposerView;

namespace native {
// One native composer per window (#121); the chat pane's calls go to the slot
// of the window being drawn (composerSelectWindow).
struct ComposerSlot {
  NSScrollView* scroll = nil;
  OWUIComposerView* view = nil;
  NSRect frame = NSZeroRect;
  unsigned styleKey[3] = {0, 0, 0};
  float fontSize = 0.0f;
  ComposerCallbacks callbacks;
};
}  // namespace native

namespace {
std::map<void*, native::ComposerSlot> gSlots;   // by NSWindow (nullptr: the main window)
native::ComposerSlot* gSlot = &gSlots[nullptr];

// Runs a composer callback with that window's view current, and returns its result.
template <typename F>
auto inComposerWindow(NSView* view, F&& f) -> decltype(f()) {
  using R = decltype(f());
  if constexpr (std::is_void_v<R>) {
    native::runInWindow((__bridge void*)view.window, [&] { f(); });
  } else {
    R result{};
    native::runInWindow((__bridge void*)view.window, [&] { result = f(); });
    return result;
  }
}

NSColor* colorFromImU32(unsigned c) {
  // ImGui packs ABGR.
  return [NSColor colorWithSRGBRed:((c >> 0) & 0xFF) / 255.0
                             green:((c >> 8) & 0xFF) / 255.0
                              blue:((c >> 16) & 0xFF) / 255.0
                             alpha:((c >> 24) & 0xFF) / 255.0];
}
}  // namespace

@interface OWUIComposerView : NSTextView
@property(nonatomic) native::ComposerSlot* slot;
@property(nonatomic, copy) NSString* placeholder;
@property(nonatomic, strong) NSColor* placeholderColor;
@property(nonatomic) BOOL sendOnEnter;
@end

@implementation OWUIComposerView

- (BOOL)becomeFirstResponder {
  const BOOL ok = [super becomeFirstResponder];
  if (ok) self.needsDisplay = YES;
  return ok;
}

- (BOOL)resignFirstResponder {
  const BOOL ok = [super resignFirstResponder];
  if (ok) self.needsDisplay = YES;
  return ok;
}

// Files dropped on the window go to the drop overlay, not into the text.
- (NSArray<NSPasteboardType>*)acceptableDragTypes { return @[]; }

- (void)drawRect:(NSRect)rect {
  [super drawRect:rect];
  // The placeholder follows the app's language (looked up each time it is drawn).
  if (self.placeholder.length > 0) self.placeholder = [NSString stringWithUTF8String:i18n::tr("Send a message...")];
  if (self.string.length == 0 && self.placeholder.length > 0 && !self.hasMarkedText) {
    NSDictionary* attributes = @{
      NSFontAttributeName : self.font ?: [NSFont systemFontOfSize:13],
      NSForegroundColorAttributeName : self.placeholderColor ?: NSColor.placeholderTextColor,
    };
    const NSPoint origin = NSMakePoint(self.textContainerOrigin.x + self.textContainer.lineFragmentPadding,
                                       self.textContainerOrigin.y);
    [self.placeholder drawAtPoint:origin withAttributes:attributes];
  }
}

// Hands the keyboard back to ImGui's own responder so its shortcuts and the
// transcript navigation work again.
- (void)handOffToImGui {
  NSView* host = self.window.contentView;
  Class responder = NSClassFromString(@"KeyEventResponder");
  for (NSView* sub in host.subviews) {
    if (responder && [sub isKindOfClass:responder]) {
      [self.window makeFirstResponder:sub];
      return;
    }
  }
  [self.window makeFirstResponder:host];
}

// VoiceOver: a named text area in the app's language (the placeholder alone
// is not read as a name).
- (NSString*)accessibilityLabel {
  return [NSString stringWithUTF8String:i18n::tr("Message")];
}

- (BOOL)isAccessibilityEnabled {
  return YES;
}

- (NSString*)accessibilityPlaceholderValue {
  return [NSString stringWithUTF8String:i18n::tr("Send a message...")];
}

- (void)keyDown:(NSEvent*)event {
  const NSEventModifierFlags mods = event.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask;
  const BOOL isReturn = event.keyCode == 36 || event.keyCode == 76;
  if (isReturn && (mods & NSEventModifierFlagCommand)) {
    if (self.slot->callbacks.send) inComposerWindow(self, [&] { return self.slot->callbacks.send(); });
    return;
  }
  if (event.keyCode == 53) {   // Escape
    if (self.slot->callbacks.escape) inComposerWindow(self, [&] { return self.slot->callbacks.escape(); });
    [self handOffToImGui];
    return;
  }
  [super keyDown:event];
}

- (void)doCommandBySelector:(SEL)selector {
  if (selector == @selector(insertNewline:)) {
    const NSEventModifierFlags mods = NSApp.currentEvent.modifierFlags;
    const BOOL shift = (mods & NSEventModifierFlagShift) != 0;
    if (self.sendOnEnter && !shift && !self.hasMarkedText) {
      if (self.slot->callbacks.send) inComposerWindow(self, [&] { return self.slot->callbacks.send(); });
      return;
    }
    [self insertText:@"\n" replacementRange:self.selectedRange];
    return;
  }
  if (selector == @selector(cancelOperation:)) {
    if (self.slot->callbacks.escape) inComposerWindow(self, [&] { return self.slot->callbacks.escape(); });
    [self handOffToImGui];
    return;
  }
  if (selector == @selector(insertTab:)) {
    if (self.slot->callbacks.tab) inComposerWindow(self, [&] { return self.slot->callbacks.tab(); });
    return;
  }
  // Plain Up/Down on the first/last line recall earlier prompts (shell-style);
  // in the middle of a text they move the caret as usual.
  if (selector == @selector(moveUp:) || selector == @selector(moveDown:)) {
    const BOOL up = selector == @selector(moveUp:);
    const NSRange sel = self.selectedRange;
    const NSString* text = self.string;
    const BOOL firstLine = [text rangeOfString:@"\n" options:0 range:NSMakeRange(0, sel.location)].location == NSNotFound;
    const BOOL lastLine = [text rangeOfString:@"\n" options:0 range:NSMakeRange(NSMaxRange(sel), text.length - NSMaxRange(sel))].location == NSNotFound;
    if (((up && firstLine) || (!up && lastLine)) && self.slot->callbacks.history && inComposerWindow(self, [&] { return self.slot->callbacks.history(up ? -1 : 1); })) return;
  }
  // Option+Up / Option+Down walk the transcript, as they do from the ImGui field.
  if (selector == @selector(moveToBeginningOfParagraph:) || selector == @selector(moveToEndOfParagraph:)) {
    const bool walked =
        self.slot->callbacks.navigate && inComposerWindow(self, [&] { return self.slot->callbacks.navigate(selector == @selector(moveToBeginningOfParagraph:) ? -1 : 1); });
    if (walked) [self handOffToImGui];
    return;
  }
  [super doCommandBySelector:selector];
}

// NSTextView handles a click with its own event loop (for drag selection),
// which swallows the mouse-up. ImGui saw the mouse-down through its event
// monitor, so without this it believes the button is still held and the next
// click anywhere in the UI is lost.
- (void)mouseDown:(NSEvent*)event {
  [super mouseDown:event];
  if (ImGui::GetCurrentContext()) ImGui::GetIO().AddMouseButtonEvent(0, false);
}

- (void)paste:(id)sender {
  if (self.slot->callbacks.pasteImage && inComposerWindow(self, [&] { return self.slot->callbacks.pasteImage(); })) return;
  if (self.slot->callbacks.pasteText) {
    NSString* text = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
    if (text && inComposerWindow(self, [&] { return self.slot->callbacks.pasteText(native::toStd(text)); })) return;
  }
  [super paste:sender];
}

@end

#define gComposerScroll (gSlot->scroll)
#define gComposerView (gSlot->view)
#define gComposerFrame (gSlot->frame)
#define gComposerStyleKey (gSlot->styleKey)
#define gComposerFontSize (gSlot->fontSize)

namespace native {

void runInWindow(void* window, const std::function<void()>& fn) {
  if (gWindowScope) gWindowScope(window, fn);
  else fn();
}

void setWindowScope(std::function<void(void* window, const std::function<void()>& fn)> scope) {
  gWindowScope = std::move(scope);
}

void composerSelectWindow(void* window) { gSlot = &gSlots[window]; }

void* composerSelectedWindow() {
  for (auto& [key, slot] : gSlots) {
    if (&slot == gSlot) return key;
  }
  return nullptr;
}

void composerForgetWindow(void* window) {
  auto it = gSlots.find(window);
  if (it == gSlots.end() || window == nullptr) return;
  [it->second.scroll removeFromSuperview];
  if (gSlot == &it->second) gSlot = &gSlots[nullptr];
  gSlots.erase(it);
}

bool composerInstalled() { return gSlot->view != nil; }

void composerInstall(const ComposerCallbacks& callbacks) {
  gSlot->callbacks = callbacks;
  if (gComposerView) return;
  NSWindow* window = gCurrentWindow ?: (NSApp.mainWindow ?: NSApp.windows.firstObject);
  if (!window) return;
  NSView* host = window.contentView;

  NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 10, 10)];
  scroll.drawsBackground = NO;
  scroll.borderType = NSNoBorder;
  scroll.hasVerticalScroller = YES;
  scroll.hasHorizontalScroller = NO;
  scroll.autohidesScrollers = YES;
  scroll.scrollerStyle = NSScrollerStyleOverlay;
  scroll.hidden = YES;

  OWUIComposerView* text = [[OWUIComposerView alloc] initWithFrame:NSMakeRect(0, 0, 10, 10)];
  text.minSize = NSMakeSize(0, 0);
  text.maxSize = NSMakeSize(FLT_MAX, FLT_MAX);
  text.verticallyResizable = YES;
  text.horizontallyResizable = NO;
  text.autoresizingMask = NSViewWidthSizable;
  text.textContainer.widthTracksTextView = YES;
  text.textContainer.containerSize = NSMakeSize(10, FLT_MAX);
  text.drawsBackground = NO;
  text.richText = NO;
  text.importsGraphics = NO;
  text.usesFontPanel = NO;
  text.allowsUndo = YES;
  text.continuousSpellCheckingEnabled = YES;
  text.grammarCheckingEnabled = NO;
  // Prompts carry code; smart quotes and dashes would corrupt it.
  text.automaticQuoteSubstitutionEnabled = NO;
  text.automaticDashSubstitutionEnabled = NO;
  text.automaticTextReplacementEnabled = NO;
  text.automaticSpellingCorrectionEnabled = NO;
  text.automaticLinkDetectionEnabled = NO;
  text.smartInsertDeleteEnabled = NO;
  text.textContainerInset = NSMakeSize(2, 4);
  text.placeholder = @"Send a message...";
  [text unregisterDraggedTypes];

  scroll.documentView = text;
  [host addSubview:scroll positioned:NSWindowAbove relativeTo:nil];
  text.slot = gSlot;
  gComposerScroll = scroll;
  gComposerView = text;
}

void composerSetFrame(float x, float y, float width, float height) {
  if (!gComposerScroll) return;
  NSView* host = gComposerScroll.superview;
  const NSRect frame = NSMakeRect(x, host.bounds.size.height - y - height, width, height);
  if (NSEqualRects(frame, gComposerFrame)) return;
  gComposerFrame = frame;
  gComposerScroll.frame = frame;
}

void composerSetVisible(bool visible) {
  if (!gComposerScroll) return;
  if (gComposerScroll.hidden == !visible) return;
  if (!visible && composerIsFocused()) [gComposerView handOffToImGui];
  gComposerScroll.hidden = !visible;
}

void composerSetText(const std::string& text) {
  if (!gComposerView) return;
  NSString* value = toNs(text);
  if ([gComposerView.string isEqualToString:value]) return;
  [gComposerView setString:value];
  [gComposerView setSelectedRange:NSMakeRange(value.length, 0)];
  gComposerView.needsDisplay = YES;
}

std::string composerText() { return gComposerView ? toStd(gComposerView.string) : std::string(); }

bool composerFocus() {
  if (!gComposerView || gComposerScroll.hidden || !gComposerView.window) return false;
  if (composerIsFocused()) return true;
  [gComposerView.window makeFirstResponder:gComposerView];
  // Programmatic focus (after a send, Esc out of the transcript, a follow-up
  // chip) always lands the caret at the end.
  if (composerIsFocused()) {
    [gComposerView setSelectedRange:NSMakeRange(gComposerView.string.length, 0)];
  }
  return composerIsFocused();
}

bool composerIsFocused() {
  return gComposerView && gComposerView.window && gComposerView.window.firstResponder == gComposerView;
}

void composerHandOff() {
  if (composerIsFocused()) [gComposerView handOffToImGui];
}

float composerContentHeight() {
  if (!gComposerView) return 46.0f;
  NSLayoutManager* layout = gComposerView.layoutManager;
  [layout ensureLayoutForTextContainer:gComposerView.textContainer];
  const NSRect used = [layout usedRectForTextContainer:gComposerView.textContainer];
  return static_cast<float>(used.size.height + gComposerView.textContainerInset.height * 2 + 2);
}

void composerSetStyle(unsigned textColor, unsigned placeholderColor, unsigned accent, float fontSize,
                      bool sendOnEnter) {
  if (!gComposerView) return;
  gComposerView.sendOnEnter = sendOnEnter;
  if (gComposerStyleKey[0] == textColor && gComposerStyleKey[1] == placeholderColor &&
      gComposerStyleKey[2] == accent && gComposerFontSize == fontSize) {
    return;
  }
  gComposerStyleKey[0] = textColor;
  gComposerStyleKey[1] = placeholderColor;
  gComposerStyleKey[2] = accent;
  gComposerFontSize = fontSize;
  NSFont* font = [NSFont systemFontOfSize:fontSize];
  gComposerView.font = font;
  gComposerView.textColor = colorFromImU32(textColor);
  gComposerView.insertionPointColor = colorFromImU32(accent);
  gComposerView.placeholderColor = colorFromImU32(placeholderColor);
  gComposerView.typingAttributes = @{
    NSFontAttributeName : font,
    NSForegroundColorAttributeName : colorFromImU32(textColor),
  };
  gComposerView.needsDisplay = YES;
}

}  // namespace native

// ---------------------------------------------------------------- artifacts

namespace {
std::mutex gArtifactMutex;
std::vector<std::string> gArtifactConsole;
std::string gArtifactHtml;

void artifactLog(const std::string& line) {
  std::lock_guard<std::mutex> lock(gArtifactMutex);
  gArtifactConsole.push_back(line.size() > 1000 ? line.substr(0, 1000) + "..." : line);
  if (gArtifactConsole.size() > 200) gArtifactConsole.erase(gArtifactConsole.begin());
}
}  // namespace

@interface OWUIArtifactBridge : NSObject <WKScriptMessageHandler, WKNavigationDelegate>
@end

@implementation OWUIArtifactBridge
- (void)userContentController:(WKUserContentController*)controller
      didReceiveScriptMessage:(WKScriptMessage*)message {
  (void)controller;
  if ([message.body isKindOfClass:[NSString class]]) artifactLog(native::toStd((NSString*)message.body));
}
- (void)webView:(WKWebView*)webView didFailProvisionalNavigation:(WKNavigation*)navigation
      withError:(NSError*)error {
  (void)webView;
  (void)navigation;
  artifactLog("load failed: " + native::toStd(error.localizedDescription));
}
- (void)webView:(WKWebView*)webView didFailNavigation:(WKNavigation*)navigation withError:(NSError*)error {
  (void)webView;
  (void)navigation;
  artifactLog("load failed: " + native::toStd(error.localizedDescription));
}
@end

namespace native {
namespace {
}  // namespace
}  // namespace native

// ------------------------------------------------------- offscreen render

@interface OWUIOffscreenRenderer : NSObject <WKNavigationDelegate, WKScriptMessageHandler>
@property(nonatomic, strong) WKWebView* view;
@property(nonatomic, strong) NSWindow* window;
@property(nonatomic, copy) void (^done)(NSData* png);
@property(nonatomic) BOOL busy;
@property(nonatomic) int generation;
// URL jobs (browseUrl): extract text + links after the load settles.
@property(nonatomic) BOOL browsing;
@property(nonatomic) BOOL wantPng;
@property(nonatomic) int settleMs;
@property(nonatomic, copy) void (^doneBrowse)(NSData* png, NSString* json, NSString* error);
- (void)finish:(NSData*)png generation:(int)generation;
- (void)extractForGeneration:(int)generation note:(NSString*)note;
- (void)finishBrowse:(NSData*)png json:(NSString*)json error:(NSString*)error generation:(int)generation;
@end

static NSString* const kBrowseExtractScript =
    @"(function(){function clean(s){return (s||'').replace(/[ \\t]+/g,' ').replace(/ *\\n */g,'\\n').replace(/\\n{3,}/g,'\\n\\n').trim();}"
     "var links=[],seen={},as=document.querySelectorAll('a[href]');"
     "for(var i=0;i<as.length&&links.length<150;i++){var a=as[i],h=a.href;if(!h||!/^https?:/.test(h)||seen[h])continue;"
     "var t=(a.innerText||a.getAttribute('aria-label')||a.title||'').trim().replace(/\\s+/g,' ');if(!t)continue;seen[h]=1;links.push([t.slice(0,120),h]);}"
     "var main=document.querySelector('main,article,[role=main]')||document.body;"
     "return JSON.stringify({title:document.title||'',url:location.href,text:clean(main?main.innerText:'').slice(0,400000),links:links});})()";

@implementation OWUIOffscreenRenderer
- (void)userContentController:(WKUserContentController*)controller didReceiveScriptMessage:(WKScriptMessage*)message {
  if (![message.name isEqualToString:@"owuiDone"] || !self.busy) return;
  const int generation = self.generation;
  if ([message.body isKindOfClass:[NSString class]]) {
    core::debugLog("offscreen: page reported %s", [[message.body description] UTF8String]);
    [self finish:nil generation:generation];
    return;
  }
  core::debugLog("offscreen: page height %.0f", [message.body doubleValue]);
  CGFloat height = [message.body isKindOfClass:[NSNumber class]] ? [message.body doubleValue] : 0;
  height = MAX(24.0, MIN(4000.0, height));
  NSRect frame = self.view.frame;
  frame.size.height = height;
  self.view.frame = frame;
  // Let the layout settle at the new height before the snapshot.
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.15 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
    if (self.generation != generation || !self.busy) return;
    WKSnapshotConfiguration* config = [[WKSnapshotConfiguration alloc] init];
    config.rect = self.view.bounds;
    [self.view takeSnapshotWithConfiguration:config completionHandler:^(NSImage* image, NSError* error) {
      NSData* png = nil;
      if (error) core::debugLog("offscreen: snapshot failed: %s", [[error localizedDescription] UTF8String]);
      if (image && !error) {
        CGImageRef cg = [image CGImageForProposedRect:nil context:nil hints:nil];
        if (cg) {
          NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithCGImage:cg];
          png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
        }
      }
      [self finish:png generation:generation];
    }];
  });
}
- (void)webView:(WKWebView*)webView didFailProvisionalNavigation:(WKNavigation*)navigation withError:(NSError*)error {
  core::debugLog("offscreen: navigation failed: %s", [[error localizedDescription] UTF8String]);
  if (self.browsing) {
    [self finishBrowse:nil json:nil error:[error localizedDescription] generation:self.generation];
    return;
  }
  [self finish:nil generation:self.generation];
}
- (void)webView:(WKWebView*)webView didFailNavigation:(WKNavigation*)navigation withError:(NSError*)error {
  if (!self.browsing) return;
  // A page that loaded something before failing still gets read.
  [self extractForGeneration:self.generation note:[error localizedDescription]];
}
- (void)webView:(WKWebView*)webView didFinishNavigation:(WKNavigation*)navigation {
  if (!self.browsing) {
    core::debugLog("offscreen: page loaded, waiting for done()");
    return;
  }
  core::debugLog("offscreen: url loaded, settling %d ms", self.settleMs);
  const int generation = self.generation;
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)self.settleMs * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
    [self extractForGeneration:generation note:nil];
  });
}
- (void)extractForGeneration:(int)generation note:(NSString*)note {
  if (!self.busy || !self.browsing || generation != self.generation) return;
  [self.view evaluateJavaScript:kBrowseExtractScript completionHandler:^(id result, NSError* jsError) {
    if (!self.busy || generation != self.generation) return;
    NSString* json = [result isKindOfClass:[NSString class]] ? result : nil;
    NSString* err = note;
    if (!json && jsError) err = [jsError localizedDescription];
    if (!self.wantPng) {
      [self finishBrowse:nil json:json error:err generation:generation];
      return;
    }
    WKSnapshotConfiguration* config = [[WKSnapshotConfiguration alloc] init];
    config.rect = self.view.bounds;
    [self.view takeSnapshotWithConfiguration:config completionHandler:^(NSImage* image, NSError* snapError) {
      NSData* png = nil;
      if (image && !snapError) {
        CGImageRef cg = [image CGImageForProposedRect:nil context:nil hints:nil];
        if (cg) png = [[[NSBitmapImageRep alloc] initWithCGImage:cg] representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
      }
      [self finishBrowse:png json:json error:err generation:generation];
    }];
  }];
}
- (void)finishBrowse:(NSData*)png json:(NSString*)json error:(NSString*)error generation:(int)generation {
  if (!self.busy || generation != self.generation) return;
  core::debugLog("offscreen: browse finished %s%s", json ? "with text" : "without text", error ? " (error)" : "");
  self.busy = NO;
  self.browsing = NO;
  [self.view stopLoading];
  void (^cb)(NSData*, NSString*, NSString*) = self.doneBrowse;
  self.doneBrowse = nil;
  if (cb) cb(png, json, error);
}
- (void)finish:(NSData*)png generation:(int)generation {
  core::debugLog("offscreen: finish %s (%zu bytes)", png ? "ok" : "empty", png ? (size_t)png.length : (size_t)0);
  if (!self.busy || generation != self.generation) return;
  self.busy = NO;
  void (^cb)(NSData*) = self.done;
  self.done = nil;
  if (cb) cb(png);
}
@end

namespace native {
namespace {

static OWUIOffscreenRenderer* gOffscreen = nil;

static OWUIOffscreenRenderer* offscreenRenderer() {
  if (gOffscreen) return gOffscreen;
  OWUIOffscreenRenderer* r = [[OWUIOffscreenRenderer alloc] init];
  WKWebViewConfiguration* config = [[WKWebViewConfiguration alloc] init];
  WKUserContentController* content = [[WKUserContentController alloc] init];
  [content addScriptMessageHandler:r name:@"owuiDone"];
  config.userContentController = content;
  r.view = [[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 600, 200) configuration:config];
  r.view.navigationDelegate = r;
  // A window far off any screen: WebKit only paints views that belong to one.
  r.window = [[NSWindow alloc] initWithContentRect:NSMakeRect(-30000, -30000, 600, 200)
                                         styleMask:NSWindowStyleMaskBorderless
                                           backing:NSBackingStoreBuffered
                                             defer:NO];
  r.window.releasedWhenClosed = NO;
  // Opaque but far off every screen: a transparent window reads as occluded
  // and WebKit stops painting it.
  r.window.ignoresMouseEvents = YES;
  r.window.excludedFromWindowsMenu = YES;
  r.window.collectionBehavior = NSWindowCollectionBehaviorStationary | NSWindowCollectionBehaviorIgnoresCycle;
  r.window.contentView = r.view;
  [r.window orderBack:nil];
  gOffscreen = r;
  return r;
}

// Serial queue of render jobs, all driven from the main thread. A job is
// either HTML to snapshot (done) or a URL to read (browse).
struct OffscreenJob {
  std::string html;
  float width;
  int timeoutMs;
  std::function<void(std::string)> done;
  // URL jobs:
  std::string url;
  float height = 200;
  int settleMs = 0;
  bool wantPng = false;
  std::function<void(std::string png, std::string json, std::string error)> browse;
};
static std::deque<OffscreenJob> gOffscreenJobs;

static void pumpOffscreen() {
  OWUIOffscreenRenderer* r = offscreenRenderer();
  if (r.busy || gOffscreenJobs.empty()) return;
  OffscreenJob job = gOffscreenJobs.front();
  gOffscreenJobs.pop_front();
  r.busy = YES;
  const int generation = ++r.generation;
  if (job.browse) {
    const auto browse = job.browse;
    r.browsing = YES;
    r.wantPng = job.wantPng;
    r.settleMs = job.settleMs;
    r.doneBrowse = ^(NSData* png, NSString* json, NSString* error) {
      std::string pngBytes, jsonText, errorText;
      if (png) pngBytes.assign(static_cast<const char*>(png.bytes), png.length);
      if (json) jsonText = json.UTF8String ? json.UTF8String : "";
      if (error) errorText = error.UTF8String ? error.UTF8String : "";
      browse(pngBytes, jsonText, errorText);
      dispatch_async(dispatch_get_main_queue(), ^{ pumpOffscreen(); });
    };
    NSRect frame = NSMakeRect(0, 0, job.width, job.height);
    [r.window setContentSize:frame.size];
    r.view.frame = frame;
    NSURL* nsUrl = [NSURL URLWithString:toNs(job.url)];
    if (!nsUrl) {
      [r finishBrowse:nil json:nil error:@"bad URL" generation:generation];
      return;
    }
    NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:nsUrl];
    request.timeoutInterval = job.timeoutMs / 1000.0;
    [r.view loadRequest:request];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)job.timeoutMs * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
      if (r.busy && r.generation == generation) {
        core::debugLog("offscreen: browse timed out after %d ms - reading what is there", job.timeoutMs);
        [r extractForGeneration:generation note:@"timed out while loading; this is what the page showed by then"];
        // The extraction itself can hang on a wedged page: hard stop after another 3 s.
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)3 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
          if (r.busy && r.generation == generation) [r finishBrowse:nil json:nil error:@"timed out" generation:generation];
        });
      }
    });
    return;
  }
  const auto done = job.done;
  r.browsing = NO;
  r.done = ^(NSData* png) {
    std::string bytes;
    if (png) bytes.assign(static_cast<const char*>(png.bytes), png.length);
    done(bytes);
    dispatch_async(dispatch_get_main_queue(), ^{ pumpOffscreen(); });
  };
  NSRect frame = NSMakeRect(0, 0, job.width, 200);
  [r.window setContentSize:frame.size];
  r.view.frame = frame;
  [r.view loadHTMLString:toNs(job.html) baseURL:nil];
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)job.timeoutMs * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
    if (r.busy && r.generation == generation) {
      core::debugLog("offscreen: timed out after %d ms", job.timeoutMs);
      [r.view stopLoading];
      [r finish:nil generation:generation];
    }
  });
}

}  // namespace

std::string renderHtmlToPng(const std::string& html, float width, int timeoutMs) {
  if ([NSThread isMainThread]) return "";   // would deadlock; callers run on workers
  dispatch_semaphore_t sem = dispatch_semaphore_create(0);
  auto result = std::make_shared<std::string>();
  OffscreenJob job{html, width, timeoutMs, [result, sem](std::string bytes) {
                     *result = std::move(bytes);
                     dispatch_semaphore_signal(sem);
                   }};
  dispatch_async(dispatch_get_main_queue(), ^{
    gOffscreenJobs.push_back(job);
    pumpOffscreen();
  });
  // A job that never settles (the timeout fires on the main thread) still
  // signals; this wait is only a last resort.
  if (dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(timeoutMs + 5000) * NSEC_PER_MSEC)) != 0) return "";
  return *result;
}

BrowseResult browseUrl(const std::string& url, float width, float height, int timeoutMs, int settleMs, bool wantPng) {
  BrowseResult out;
  out.url = url;
  if ([NSThread isMainThread]) {
    out.error = "browseUrl cannot run on the main thread";
    return out;
  }
  dispatch_semaphore_t sem = dispatch_semaphore_create(0);
  auto result = std::make_shared<BrowseResult>();
  result->url = url;
  OffscreenJob job;
  job.url = url;
  job.width = width;
  job.height = height;
  job.timeoutMs = timeoutMs;
  job.settleMs = settleMs;
  job.wantPng = wantPng;
  job.browse = [result, sem](std::string png, std::string json, std::string error) {
    result->png = std::move(png);
    result->error = std::move(error);
    if (!json.empty()) {
      nlohmann::json j = nlohmann::json::parse(json, nullptr, false);
      if (j.is_object()) {
        result->title = j.value("title", "");
        if (!j.value("url", "").empty()) result->url = j["url"];
        result->text = j.value("text", "");
        for (const auto& link : j.value("links", nlohmann::json::array())) {
          if (link.is_array() && link.size() == 2 && link[0].is_string() && link[1].is_string()) result->links.push_back({link[0], link[1]});
        }
      }
    }
    dispatch_semaphore_signal(sem);
  };
  dispatch_async(dispatch_get_main_queue(), ^{
    gOffscreenJobs.push_back(job);
    pumpOffscreen();
  });
  if (dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(timeoutMs + 8000) * NSEC_PER_MSEC)) != 0) {
    out.error = "timed out";
    return out;
  }
  return *result;
}

namespace {

WKWebView* gArtifactView = nil;
OWUIArtifactBridge* gArtifactBridge = nil;
NSRect gArtifactFrame = NSZeroRect;

// Forwards console output and errors to the app; installed before any page
// script runs so even early failures are caught.
NSString* const kArtifactBridgeScript =
    @"(function(){const post=(t)=>{try{window.webkit.messageHandlers.owui.postMessage(String(t));}catch(e){}};"
     "window.addEventListener('error',e=>post('error: '+e.message+(e.lineno?' (line '+e.lineno+')':'')));"
     "window.addEventListener('unhandledrejection',e=>post('unhandled rejection: '+(e.reason&&e.reason.message||e.reason)));"
     "['log','info','warn','error'].forEach(k=>{const o=console[k];console[k]=function(){"
     "post(k+': '+Array.from(arguments).map(a=>{try{return typeof a==='object'?JSON.stringify(a):String(a)}catch(_){return String(a)}}).join(' '));"
     "o.apply(console,arguments);};});})();";
}  // namespace

void artifactInstall() {
  if (gArtifactView) return;
  NSWindow* window = NSApp.mainWindow ?: NSApp.windows.firstObject;
  if (!window) return;
  NSView* host = window.contentView;

  WKWebViewConfiguration* config = [[WKWebViewConfiguration alloc] init];
  WKUserContentController* content = [[WKUserContentController alloc] init];
  gArtifactBridge = [[OWUIArtifactBridge alloc] init];
  [content addScriptMessageHandler:gArtifactBridge name:@"owui"];
  [content addUserScript:[[WKUserScript alloc] initWithSource:kArtifactBridgeScript
                                                injectionTime:WKUserScriptInjectionTimeAtDocumentStart
                                             forMainFrameOnly:NO]];
  config.userContentController = content;

  WKWebView* view = [[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 10, 10) configuration:config];
  view.navigationDelegate = gArtifactBridge;
  view.hidden = YES;
  [host addSubview:view positioned:NSWindowAbove relativeTo:nil];
  gArtifactView = view;
}

void artifactSetFrame(float x, float y, float width, float height) {
  if (!gArtifactView) return;
  NSView* host = gArtifactView.superview;
  const NSRect frame = NSMakeRect(x, host.bounds.size.height - y - height, width, height);
  if (NSEqualRects(frame, gArtifactFrame)) return;
  gArtifactFrame = frame;
  gArtifactView.frame = frame;
}

void artifactSetVisible(bool visible) {
  if (!gArtifactView || gArtifactView.hidden == !visible) return;
  if (!visible && gArtifactView.window.firstResponder &&
      [gArtifactView.window.firstResponder isKindOfClass:[NSView class]] &&
      [(NSView*)gArtifactView.window.firstResponder isDescendantOf:gArtifactView]) {
    [gArtifactView.window makeFirstResponder:gArtifactView.window.contentView];
  }
  gArtifactView.hidden = !visible;
}

void artifactLoad(const std::string& html) {
  if (!gArtifactView) return;
  {
    std::lock_guard<std::mutex> lock(gArtifactMutex);
    gArtifactConsole.clear();
    gArtifactHtml = html;
  }
  [gArtifactView loadHTMLString:toNs(html) baseURL:nil];
}

void artifactReload() {
  std::string html;
  {
    std::lock_guard<std::mutex> lock(gArtifactMutex);
    html = gArtifactHtml;
  }
  artifactLoad(html);
}

std::vector<std::string> artifactConsole() {
  std::lock_guard<std::mutex> lock(gArtifactMutex);
  return gArtifactConsole;
}

}  // namespace native

// ---------------------------------------------------------------- pdf pane

namespace native {
namespace {
PDFView* gPdfView = nil;
NSRect gPdfFrame = NSZeroRect;
NSMutableArray<PDFAnnotation*>* gPdfHighlights = nil;

NSString* collapseWhitespace(NSString* text) {
  NSArray* parts = [text componentsSeparatedByCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
  NSMutableArray* words = [NSMutableArray array];
  for (NSString* w in parts) {
    if (w.length) [words addObject:w];
  }
  return [words componentsJoinedByString:@" "];
}
}  // namespace

void pdfInstall() {
  if (gPdfView) return;
  NSWindow* window = NSApp.mainWindow ?: NSApp.windows.firstObject;
  if (!window) return;
  PDFView* view = [[PDFView alloc] initWithFrame:NSMakeRect(0, 0, 10, 10)];
  view.autoScales = YES;
  view.displayMode = kPDFDisplaySinglePageContinuous;
  view.displaysPageBreaks = YES;
  view.hidden = YES;
  [window.contentView addSubview:view positioned:NSWindowAbove relativeTo:nil];
  gPdfView = view;
  gPdfHighlights = [NSMutableArray array];
}

void pdfSetFrame(float x, float y, float width, float height) {
  if (!gPdfView) return;
  NSView* host = gPdfView.superview;
  const NSRect frame = NSMakeRect(x, host.bounds.size.height - y - height, width, height);
  if (NSEqualRects(frame, gPdfFrame)) return;
  gPdfFrame = frame;
  gPdfView.frame = frame;
}

void pdfSetVisible(bool visible) {
  if (!gPdfView || gPdfView.hidden == !visible) return;
  if (!visible && gPdfView.window.firstResponder && [gPdfView.window.firstResponder isKindOfClass:[NSView class]] &&
      [(NSView*)gPdfView.window.firstResponder isDescendantOf:gPdfView]) {
    [gPdfView.window makeFirstResponder:gPdfView.window.contentView];
  }
  gPdfView.hidden = !visible;
}

bool pdfLoad(const std::string& bytes) {
  if (!gPdfView) return false;
  NSData* data = [NSData dataWithBytes:bytes.data() length:bytes.size()];
  PDFDocument* document = [[PDFDocument alloc] initWithData:data];
  if (!document) return false;
  [gPdfHighlights removeAllObjects];
  gPdfView.document = document;
  return true;
}

int pdfPageCount() { return gPdfView && gPdfView.document ? static_cast<int>(gPdfView.document.pageCount) : 0; }

int pdfCurrentPage() {
  if (!gPdfView || !gPdfView.document || !gPdfView.currentPage) return 0;
  return static_cast<int>([gPdfView.document indexForPage:gPdfView.currentPage]) + 1;
}

void pdfGoToPage(int page) {
  if (!gPdfView || !gPdfView.document) return;
  const NSInteger index = std::clamp<NSInteger>(page - 1, 0, static_cast<NSInteger>(gPdfView.document.pageCount) - 1);
  PDFPage* target = [gPdfView.document pageAtIndex:static_cast<NSUInteger>(index)];
  if (!target) return;
  [gPdfView goToPage:target];
  // Right after a load the view has not laid out yet and the jump is lost
  // (it lands wherever the layout settles); apply it again once it has.
  PDFView* view = gPdfView;
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.15 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
    if (gPdfView == view && view.document == target.document) [view goToPage:target];
  });
}

void pdfClearHighlights() {
  for (PDFAnnotation* annotation in gPdfHighlights) [annotation.page removeAnnotation:annotation];
  [gPdfHighlights removeAllObjects];
}

int pdfHighlight(const std::string& passage, int preferredPage) {
  if (!gPdfView || !gPdfView.document) return 0;
  pdfClearHighlights();
  PDFDocument* document = gPdfView.document;
  NSString* text = collapseWhitespace([NSString stringWithUTF8String:passage.c_str()]);
  NSArray<NSString*>* words = [text componentsSeparatedByString:@" "];

  // The server's text extraction and PDFKit's rarely agree on every
  // character, so search for a run of leading words, shortening it until it
  // matches; prefer a hit on the page the citation names.
  // Longest first so the whole chunk lights up when the texts agree.
  PDFSelection* found = nil;
  const NSUInteger lengths[] = {60, 40, 28, 20, 14, 10, 7, 5, 4};
  for (NSUInteger length : lengths) {
    if (found) break;
    const NSUInteger count = MIN(length, words.count);
    if (count < 4) break;
    NSString* needle = [[words subarrayWithRange:NSMakeRange(0, count)] componentsJoinedByString:@" "];
    if (needle.length < 12) break;
    NSArray<PDFSelection*>* hits = [document findString:needle withOptions:NSCaseInsensitiveSearch];
    if (hits.count == 0) continue;
    found = hits.firstObject;
    if (preferredPage > 0) {
      for (PDFSelection* hit : hits) {
        PDFPage* page = hit.pages.firstObject;
        if (page && [document indexForPage:page] + 1 == static_cast<NSUInteger>(preferredPage)) {
          found = hit;
          break;
        }
      }
    }
  }
  if (!found) {
    if (preferredPage > 0) pdfGoToPage(preferredPage);
    return 0;
  }
  // findString: won't match across a line break, so the hit covers at most
  // one line. Keep searching for the following words from the hit onwards and
  // union the pieces while they stay on the same page and close by.
  {
    PDFPage* page = found.pages.firstObject;
    NSUInteger consumed = 0;
    NSString* foundText = collapseWhitespace(found.string);
    consumed = [foundText componentsSeparatedByString:@" "].count;
    PDFSelection* cursor = found;
    int misses = 0;
    while (consumed + 3 <= words.count && misses < 2) {
      const NSUInteger take = MIN((NSUInteger)6, words.count - consumed);
      NSString* needle = [[words subarrayWithRange:NSMakeRange(consumed, take)] componentsJoinedByString:@" "];
      PDFSelection* next = [document findString:needle fromSelection:cursor withOptions:NSCaseInsensitiveSearch];
      if (!next || next.pages.firstObject != page) {
        ++misses;
        consumed += take;   // skip a run the extractions disagree on and try the next
        continue;
      }
      const NSRect a = [cursor boundsForPage:page];
      const NSRect b = [next boundsForPage:page];
      if (fabs(NSMinY(b) - NSMinY(a)) > 120.0) break;   // jumped elsewhere on the page
      [found addSelection:next];
      cursor = next;
      consumed += take;
      misses = 0;
    }
  }
  for (PDFPage* page in found.pages) {
    for (NSValue* rectValue in @[ [NSValue valueWithRect:[found boundsForPage:page]] ]) {
      PDFAnnotation* highlight = [[PDFAnnotation alloc] initWithBounds:rectValue.rectValue
                                                               forType:PDFAnnotationSubtypeHighlight
                                                        withProperties:nil];
      highlight.color = [NSColor colorWithSRGBRed:1.0 green:0.85 blue:0.2 alpha:0.9];
      [page addAnnotation:highlight];
      [gPdfHighlights addObject:highlight];
    }
  }
  // A document assigned moments ago may not be laid out yet, in which case
  // scrollSelectionToVisible lands anywhere; lay it out and go to the rect.
  [gPdfView layoutDocumentView];
  [gPdfView setCurrentSelection:found animate:NO];
  PDFPage* page = found.pages.firstObject;
  if (page) {
    NSRect bounds = [found boundsForPage:page];
    bounds = NSInsetRect(bounds, 0, -80);   // some context above and below
    [gPdfView goToRect:bounds onPage:page];
  }
  return page ? static_cast<int>([document indexForPage:page]) + 1 : 0;
}

}  // namespace native

// ---------------------------------------------------------------- windows

namespace native {

// The window whose view is being drawn or acted for (#121), else the key one.
NSWindow* currentWindow() {
  if (gCurrentWindow) return gCurrentWindow;
  return NSApp.keyWindow ?: (NSApp.mainWindow ?: NSApp.windows.firstObject);
}

void setCurrentWindow(void* window) { gCurrentWindow = (__bridge NSWindow*)window; }
void* currentWindowHandle() { return (__bridge void*)currentWindow(); }
void* currentWindowSet() { return (__bridge void*)gCurrentWindow; }

bool openNewInstance(const std::vector<std::string>& args) {
  // NSWorkspace rather than `open -n`: the new instance inherits this one's
  // environment (OWUI_SERVER / OWUI_TOKEN overrides included) and arguments.
  NSWorkspaceOpenConfiguration* config = [NSWorkspaceOpenConfiguration configuration];
  config.createsNewApplicationInstance = YES;
  config.activates = YES;
  config.environment = NSProcessInfo.processInfo.environment;
  NSMutableArray* arguments = [NSMutableArray array];
  for (const std::string& a : args) [arguments addObject:toNs(a)];
  config.arguments = arguments;
  [[NSWorkspace sharedWorkspace] openApplicationAtURL:NSBundle.mainBundle.bundleURL
                                        configuration:config
                                    completionHandler:^(NSRunningApplication* app, NSError* error) {
                                      if (error) core::debugLog("new window: %s", error.localizedDescription.UTF8String);
                                      (void)app;
                                    }];
  return true;
}

void setFloating(bool floating) {
  NSWindow* window = currentWindow();
  if (!window) return;
  window.level = floating ? NSFloatingWindowLevel : NSNormalWindowLevel;
  if (floating) {
    window.collectionBehavior |= NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary;
  } else {
    window.collectionBehavior &= ~(NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary);
  }
}

void setWindowSize(float width, float height) {
  NSWindow* window = currentWindow();
  if (!window) return;
  NSRect frame = window.frame;
  frame.origin.y += frame.size.height - height;
  frame.size = NSMakeSize(width, height);
  [window setFrame:frame display:YES animate:NO];
}

void cascadeWindow() {
  NSWindow* window = currentWindow();
  if (!window) return;
  NSPoint topLeft = NSMakePoint(NSMinX(window.frame) + 40, NSMaxY(window.frame) - 40);
  [window cascadeTopLeftFromPoint:topLeft];
}

}  // namespace native
