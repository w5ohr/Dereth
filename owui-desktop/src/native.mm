#include "native.h"

#import <Carbon/Carbon.h>
#import <Cocoa/Cocoa.h>
#import <CoreServices/CoreServices.h>
#import <CoreSpotlight/CoreSpotlight.h>
#import <Quartz/Quartz.h>
#import <UserNotifications/UserNotifications.h>
#import <Vision/Vision.h>

#include <map>
#include <mutex>

#include "core/common.h"

namespace native {
namespace {

Callbacks gCallbacks;
EventHotKeyRef gHotkeyRef = nullptr;
NSStatusItem* gStatusItem = nil;
NSUserActivity* gActivity = nil;
NSProgressIndicator* gDockProgress = nil;
int gNextWatchToken = 1;
std::map<int, FSEventStreamRef> gWatchers;
std::map<int, std::function<void(const std::vector<std::string>&)>> gWatchCallbacks;

OSStatus hotkeyHandler(EventHandlerCallRef, EventRef, void*) {
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
