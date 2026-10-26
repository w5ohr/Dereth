// macOS entry point: Cocoa window + MetalKit view + Dear ImGui, driving
// app::App once per frame.
#import <Cocoa/Cocoa.h>
#import <CoreSpotlight/CoreSpotlight.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

#include "core/crashreport.h"
#include "app.h"
#include "native.h"
#include "core/async.h"
#include "core/common.h"
#include "platform.h"
#include "core/http.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_metal.h"
#include "imgui_impl_osx.h"
#include "ui/a11y_bridge.h"
#include "ui/a11y_imgui.h"
#include "ui/images.h"
#include "ui/theme.h"

static std::string gIniPath;
// Shared by every window (#121): one Metal device, the main ImGui context
// (it owns the font atlas the chat windows share) and the main window.
static id<MTLDevice> gDevice = nil;
static ImGuiContext* gMainContext = nullptr;
static __weak NSWindow* gMainWindow = nil;
static void installWindowScopes();
static bool chatWindowsOpen();

@interface AppViewController : NSViewController <NSWindowDelegate, MTKViewDelegate>
@property(nonatomic, strong) id<MTLDevice> device;
@property(nonatomic, strong) id<MTLCommandQueue> commandQueue;
@end

@implementation AppViewController

- (instancetype)initWithNibName:(nullable NSString*)nibNameOrNil
                         bundle:(nullable NSBundle*)nibBundleOrNil {
  self = [super initWithNibName:nibNameOrNil bundle:nibBundleOrNil];
  _device = MTLCreateSystemDefaultDevice();
  _commandQueue = [_device newCommandQueue];
  if (!_device) {
    NSLog(@"Metal is not supported on this device");
    abort();
  }

  IMGUI_CHECKVERSION();
  gDevice = _device;
  gMainContext = ImGui::CreateContext();
  a11y::ui::install(ImGui::GetCurrentContext());   // VoiceOver reads the widgets ImGui reports
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigWindowsMoveFromTitleBarOnly = true;
  gIniPath = core::supportDir() + "/layout.ini";
  io.IniFilename = gIniPath.c_str();

  core::Settings::instance().load();
  theme::loadFonts(core::Settings::instance().fontSize);
  theme::apply(core::Settings::instance().theme);

  ImGui_ImplMetal_Init(_device);
  images::setDevice((__bridge void*)_device);
  return self;
}

- (MTKView*)mtkView {
  return (MTKView*)self.view;
}

- (void)loadView {
  self.view = [[OWUIMetalView alloc] initWithFrame:CGRectMake(0, 0, 1380, 900)];
}

static MTKView* gMainView = nil;
namespace native {
void setPreferredFrameRate(int fps) {
  if (!gMainView) return;
  gMainView.preferredFramesPerSecond = fps < 1 ? 1 : fps;
}
}  // namespace native

- (void)viewDidLoad {
  [super viewDidLoad];
  gMainView = self.mtkView;
  ((OWUIMetalView*)self.mtkView).imguiContext = ImGui::GetCurrentContext();
  installWindowScopes();
  self.mtkView.device = self.device;
  self.mtkView.delegate = self;
  ImGui_ImplOSX_Init(self.view);
  [NSApp activateIgnoringOtherApps:YES];
  app::App::instance().init();
}

- (void)viewWillAppear {
  [super viewWillAppear];
  self.view.window.delegate = self;
  gMainWindow = self.view.window;
  // Chat windows (#121) share this identifier, so they can be tabs of one another.
  self.view.window.tabbingIdentifier = @"OpenWebUIChat";
  self.view.window.title = @"Open WebUI";
  self.view.window.titlebarAppearsTransparent = YES;
  self.view.window.minSize = NSMakeSize(900, 600);
  // Secondary windows (a chat in its own window, the mini window) must not
  // write the remembered frame, or the next launch opens at their size.
  if (!app::App::instance().secondaryInstance) {
    [self.view.window setFrameAutosaveName:@"OpenWebUIMainWindow"];
  }
  // ImGui re-enables mouse hover only when it sees the mouse move; keep
  // mouse-moved events flowing even while a native subview is first responder.
  self.view.window.acceptsMouseMovedEvents = YES;
}

- (void)drawInMTKView:(MTKView*)view {
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize.x = view.bounds.size.width;
  io.DisplaySize.y = view.bounds.size.height;
  CGFloat scale = view.window.screen.backingScaleFactor ?: NSScreen.mainScreen.backingScaleFactor;
  io.DisplayFramebufferScale = ImVec2(scale, scale);

  id<MTLCommandBuffer> commandBuffer = [self.commandQueue commandBuffer];
  MTLRenderPassDescriptor* descriptor = view.currentRenderPassDescriptor;
  if (descriptor == nil) {
    [commandBuffer commit];
    return;
  }

  async::drain();
  images::uploadPending();

  // Keep the window title in step with the open conversation.
  {
    app::App& app = app::App::instance();
    NSString* title = app.signedIn()
                          ? [NSString stringWithUTF8String:(app.chat.title.empty()
                                                                ? "Open WebUI"
                                                                : app.chat.title.c_str())]
                          : @"Open WebUI";
    if (![view.window.title isEqualToString:title]) view.window.title = title;
  }

  // The ImGui backend learns the pointer position from mouse-moved events,
  // which stop arriving while a native subview (the composer, the artifact
  // view) is first responder; the first click after typing then lands on a
  // stale position. Refreshing from the window every frame keeps it right.
  // (Not while VoiceOver's press is being played in at a widget's centre.)
  if (view.window.keyWindow && !a11y::ui::pressInFlight()) {
    const NSPoint local = [view convertPoint:view.window.mouseLocationOutsideOfEventStream fromView:nil];
    const float x = static_cast<float>(local.x);
    const float y = static_cast<float>(view.bounds.size.height - local.y);
    if (x >= 0.0f && y >= 0.0f && x <= view.bounds.size.width && y <= view.bounds.size.height) {
      io.AddMousePosEvent(x, y);
    }
  }

  if (app::App::instance().fontsReloadRequested) {
    app::App::instance().fontsReloadRequested = false;
    theme::loadFonts(core::Settings::instance().fontSize);   // safe here: no frame is open
  }
  ImGui_ImplMetal_NewFrame(descriptor);
  ImGui_ImplOSX_NewFrame(view);
  a11y::ui::pumpInput(io);
  ImGui::NewFrame();
  a11y::ui::beginFrame();

  native::setCurrentWindow((__bridge void*)view.window);
  native::composerSelectWindow(nullptr);
  {
    // Toasts show in the key window; this one when no chat window is key.
    app::App& app = app::App::instance();
    NSWindow* key = NSApp.keyWindow;
    app.toastsHere = key == view.window || !key || ![key.contentView isKindOfClass:[OWUIMetalView class]];
    app.frame();
    app.toastsHere = true;
  }
  native::setCurrentWindow(nullptr);
  a11y::ui::endFrame();
  a11y::bridge::frameDone((__bridge void*)view);

  ImGui::Render();
  ImVec4 clear = theme::toVec4(theme::palette().bg);
  descriptor.colorAttachments[0].clearColor = MTLClearColorMake(clear.x, clear.y, clear.z, 1.0);
  id<MTLRenderCommandEncoder> encoder =
      [commandBuffer renderCommandEncoderWithDescriptor:descriptor];
  [encoder pushDebugGroup:@"ImGui"];
  ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), commandBuffer, encoder);
  [encoder popDebugGroup];
  [encoder endEncoding];
  [commandBuffer presentDrawable:view.currentDrawable];
  [commandBuffer commit];
}

- (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size {
  (void)view;
  (void)size;
}

// With chat windows open, closing the main window hides it: it carries the
// app-wide work and the shared fonts, and comes back from the Dock or the
// Window menu.
- (BOOL)windowShouldClose:(NSWindow*)sender {
  if (!chatWindowsOpen()) return YES;
  [sender orderOut:nil];
  return NO;
}

- (void)windowWillClose:(NSNotification*)notification {
  (void)notification;
  app::App::instance().shutdown();
  ImGui_ImplMetal_Shutdown();
  ImGui_ImplOSX_Shutdown();
  ImGui::DestroyContext();
}

@end

// ---------------------------------------------------------------- chat windows (#121)
//
// A chat window is an NSWindow with its own MTKView and ImGui context (sharing
// the main context's font atlas) over a ViewState in App. Everything done for
// it - its frames, menu actions while it is key, its composer's keys, the
// network callbacks it started, VoiceOver's presses - runs inside
// asChatWindow(), which makes its context, native window and composer
// current and swaps its view into App.

@interface OWUIChatWindowController : NSObject <NSWindowDelegate, MTKViewDelegate>
@property(nonatomic, strong) NSWindow* window;
@property(nonatomic, strong) OWUIMetalView* view;
@property(nonatomic, strong) id<MTLCommandQueue> queue;
@property(nonatomic) uint64_t token;
@property(nonatomic) ImGuiContext* context;
@property(nonatomic) BOOL closing;
- (instancetype)initWithChat:(const std::string&)chatId mini:(BOOL)mini;
@end

static NSMutableArray<OWUIChatWindowController*>* gChatWindows = nil;

static bool chatWindowsOpen() { return gChatWindows.count > 0; }

static OWUIChatWindowController* controllerForWindow(NSWindow* window) {
  if (!window) return nil;
  for (OWUIChatWindowController* c in gChatWindows) {
    if (c.window == window && !c.closing) return c;
  }
  return nil;
}

static OWUIChatWindowController* controllerForToken(uint64_t token) {
  for (OWUIChatWindowController* c in gChatWindows) {
    if (c.token == token && !c.closing) return c;
  }
  return nil;
}

// Makes a chat window current (ImGui context, native window, composer, view)
// for the life of the object; restores what was current after.
struct ChatWindowScope {
  OWUIChatWindowController* controller = nil;
  ImGuiContext* previousContext = nullptr;
  void* previousWindow = nullptr;
  void* previousSlot = nullptr;
  uint64_t previousView = app::App::kNoView;
  explicit ChatWindowScope(OWUIChatWindowController* c) : controller(c) {
    if (!controller) return;
    previousContext = ImGui::GetCurrentContext();
    previousWindow = native::currentWindowSet();
    previousSlot = native::composerSelectedWindow();
    ImGui::SetCurrentContext(controller.context);
    native::setCurrentWindow((__bridge void*)controller.window);
    native::composerSelectWindow((__bridge void*)controller.window);
    previousView = app::App::instance().enterView(controller.token);
  }
  bool active() const { return controller && previousView != app::App::kNoView; }
  ~ChatWindowScope() {
    if (!controller) return;
    app::App::instance().leaveView(previousView);
    native::composerSelectWindow(previousSlot);
    native::setCurrentWindow(previousWindow);
    ImGui::SetCurrentContext(previousContext);
  }
};

// Menu actions act on the key window: a chat window's view when one is key.
struct KeyViewScope {
  ChatWindowScope scope;
  KeyViewScope() : scope(controllerForWindow(NSApp.keyWindow)) {}
};

static void installWindowScopes() {
  native::setWindowScope([](void* window, const std::function<void()>& fn) {
    OWUIChatWindowController* c = controllerForWindow((__bridge NSWindow*)window);
    if (!c) {
      fn();   // the main window
      return;
    }
    ChatWindowScope scope(c);
    if (scope.active()) fn();
  });
  async::setViewRunner([](uint64_t token, const std::function<void()>& fn) {
    OWUIChatWindowController* c = controllerForToken(token);
    if (!c) return false;   // closed: its callbacks are dropped
    ChatWindowScope scope(c);
    if (!scope.active()) return false;
    fn();
    return true;
  });
  // A chat dragged out of the sidebar past the window's edge opens where it was dropped.
  app::App::instance().onDragChatOut = [](const std::string& chatId, float x, float y) {
    NSWindow* window = (__bridge NSWindow*)native::currentWindowHandle();
    NSView* content = window.contentView;
    if (!content) return;
    const NSPoint local = NSMakePoint(x, content.bounds.size.height - y);
    const NSPoint screen = [window convertPointToScreen:[content convertPoint:local toView:nil]];
    native::openChatWindowAt(chatId, static_cast<float>(screen.x), static_cast<float>(screen.y));
  };
}

@implementation OWUIChatWindowController

- (instancetype)initWithChat:(const std::string&)chatId mini:(BOOL)mini {
  if (!(self = [super init])) return nil;
  app::App& app = app::App::instance();
  _token = app.createView(chatId, mini)->token;
  const NSSize mainSize = gMainWindow ? gMainWindow.contentLayoutRect.size : NSMakeSize(1180, 820);
  const NSRect frame = mini ? NSMakeRect(0, 0, 560, 760) : NSMakeRect(0, 0, std::max(900.0, mainSize.width - 80), std::max(600.0, mainSize.height - 60));
  _window = [[NSWindow alloc] initWithContentRect:frame
                                        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable |
                                                  NSWindowStyleMaskMiniaturizable
                                          backing:NSBackingStoreBuffered
                                            defer:NO];
  _window.releasedWhenClosed = NO;
  _window.title = @"Open WebUI";
  _window.titlebarAppearsTransparent = YES;
  _window.minSize = mini ? NSMakeSize(420, 480) : NSMakeSize(700, 500);
  _window.tabbingIdentifier = @"OpenWebUIChat";
  _window.acceptsMouseMovedEvents = YES;
  if (mini) {
    _window.tabbingMode = NSWindowTabbingModeDisallowed;   // a floating mini window stays on its own
    _window.level = NSFloatingWindowLevel;
    _window.collectionBehavior |= NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary;
  }
  _view = [[OWUIMetalView alloc] initWithFrame:frame];
  _view.device = gDevice;
  _view.delegate = self;
  _view.preferredFramesPerSecond = gMainView ? gMainView.preferredFramesPerSecond : 60;
  _window.contentView = _view;
  _window.delegate = self;
  _queue = [gDevice newCommandQueue];

  // Its own ImGui context over the main one's fonts; the backends bind to its view.
  ImGuiContext* previous = ImGui::GetCurrentContext();
  ImGui::SetCurrentContext(gMainContext);
  ImFontAtlas* atlas = ImGui::GetIO().Fonts;
  _context = ImGui::CreateContext(atlas);
  ImGui::SetCurrentContext(_context);
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
  io.ConfigWindowsMoveFromTitleBarOnly = true;
  io.IniFilename = nullptr;   // layout.ini belongs to the main window
  ImGui::GetStyle() = gMainContext->Style;
  a11y::ui::install(_context);
  ImGui_ImplMetal_Init(gDevice);
  ImGui_ImplOSX_Init(_view);
  _view.imguiContext = _context;
  ImGui::SetCurrentContext(previous);
  return self;
}

- (void)drawInMTKView:(MTKView*)view {
  if (self.closing) return;
  ImGuiContext* previous = ImGui::GetCurrentContext();
  // With the main window hidden its frames stop; the app's queue still drains here.
  if (!gMainWindow.visible) {
    ImGui::SetCurrentContext(gMainContext);
    async::drain();
    images::uploadPending();
    if (self.closing) {
      ImGui::SetCurrentContext(previous);
      return;
    }
  }
  ImGui::SetCurrentContext(self.context);
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(static_cast<float>(view.bounds.size.width), static_cast<float>(view.bounds.size.height));
  const CGFloat scale = view.window.screen.backingScaleFactor ?: NSScreen.mainScreen.backingScaleFactor;
  io.DisplayFramebufferScale = ImVec2(static_cast<float>(scale), static_cast<float>(scale));
  ImGui::GetStyle() = gMainContext->Style;   // theme changes reach every window
  if (gMainView) view.preferredFramesPerSecond = gMainView.preferredFramesPerSecond;

  id<MTLCommandBuffer> commandBuffer = [self.queue commandBuffer];
  MTLRenderPassDescriptor* descriptor = view.currentRenderPassDescriptor;
  if (descriptor == nil) {
    [commandBuffer commit];
    ImGui::SetCurrentContext(previous);
    return;
  }
  if (view.window.keyWindow && !a11y::ui::pressInFlight()) {
    const NSPoint local = [view convertPoint:view.window.mouseLocationOutsideOfEventStream fromView:nil];
    const float x = static_cast<float>(local.x);
    const float y = static_cast<float>(view.bounds.size.height - local.y);
    if (x >= 0.0f && y >= 0.0f && x <= view.bounds.size.width && y <= view.bounds.size.height) io.AddMousePosEvent(x, y);
  }
  ImGui_ImplMetal_NewFrame(descriptor);
  ImGui_ImplOSX_NewFrame(view);
  a11y::ui::pumpInput(io);
  ImGui::NewFrame();
  a11y::ui::beginFrame();
  std::string title;
  {
    native::setCurrentWindow((__bridge void*)view.window);
    native::composerSelectWindow((__bridge void*)view.window);
    app::App& app = app::App::instance();
    app.frameView(self.token, view.window.keyWindow);
    app.runInView(self.token, [&] { title = app.chat.title; });
    native::composerSelectWindow(nullptr);
    native::setCurrentWindow(nullptr);
  }
  a11y::ui::endFrame();
  a11y::bridge::frameDone((__bridge void*)view);
  NSString* nsTitle = [NSString stringWithUTF8String:(title.empty() ? "Open WebUI" : title.c_str())] ?: @"Open WebUI";
  if (![view.window.title isEqualToString:nsTitle]) view.window.title = nsTitle;

  ImGui::Render();
  ImVec4 clear = theme::toVec4(theme::palette().bg);
  descriptor.colorAttachments[0].clearColor = MTLClearColorMake(clear.x, clear.y, clear.z, 1.0);
  id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:descriptor];
  ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), commandBuffer, encoder);
  [encoder endEncoding];
  [commandBuffer presentDrawable:view.currentDrawable];
  [commandBuffer commit];
  ImGui::SetCurrentContext(previous);
}

- (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size {
  (void)view;
  (void)size;
}

- (void)windowWillClose:(NSNotification*)notification {
  (void)notification;
  self.closing = YES;
  self.view.paused = YES;
  self.view.delegate = nil;
  // Tear down after this event, never inside a frame.
  dispatch_async(dispatch_get_main_queue(), ^{
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(self.context);
    ImGui_ImplMetal_Shutdown();
    ImGui_ImplOSX_Shutdown();
    a11y::ui::forget(self.context);
    ImGui::DestroyContext(self.context);
    ImGui::SetCurrentContext(previous == self.context ? gMainContext : previous);
    native::composerForgetWindow((__bridge void*)self.window);
    app::App::instance().destroyView(self.token);
    [gChatWindows removeObject:self];
    // The main window was only hidden: with nothing else open, quit as its close would.
    if (gChatWindows.count == 0 && gMainWindow && !gMainWindow.visible && !native::menuBarOnly()) [NSApp terminate:nil];
  });
}

@end

static OWUIChatWindowController* createChatWindow(const std::string& chatId, bool mini, bool asTab, bool placed, NSPoint topLeft) {
  if (!gChatWindows) gChatWindows = [NSMutableArray array];
  NSWindow* from = NSApp.keyWindow ?: gMainWindow;
  OWUIChatWindowController* c = [[OWUIChatWindowController alloc] initWithChat:chatId mini:mini];
  [gChatWindows addObject:c];
  if (asTab && !mini && from && from.tabbingMode != NSWindowTabbingModeDisallowed) {
    [from addTabbedWindow:c.window ordered:NSWindowAbove];
    [c.window makeKeyAndOrderFront:nil];
    return c;
  }
  if (placed) {
    [c.window setFrameTopLeftPoint:topLeft];
  } else if (from) {
    [c.window setFrameTopLeftPoint:NSMakePoint(NSMinX(from.frame) + 40, NSMaxY(from.frame) - 40)];
  } else {
    [c.window center];
  }
  // A window, not a tab: AppKit would otherwise add it to a tab bar that is
  // showing. Tabbing comes back once it is on screen, so it can be merged later.
  const NSWindowTabbingMode mode = c.window.tabbingMode;
  c.window.tabbingMode = NSWindowTabbingModeDisallowed;
  [c.window makeKeyAndOrderFront:nil];
  dispatch_async(dispatch_get_main_queue(), ^{
    c.window.tabbingMode = mode;
  });
  return c;
}

namespace native {
// Deferred to the next turn of the run loop: these are asked for from inside
// a frame (palette, menus drawn in ImGui), and a new ImGui context must not
// be made in the middle of one.
bool openChatWindow(const std::string& chatId, bool mini, bool asTab) {
  if (!gDevice || !gMainContext) return false;
  const std::string id = chatId;
  dispatch_async(dispatch_get_main_queue(), ^{
    createChatWindow(id, mini, asTab, false, NSZeroPoint);
  });
  return true;
}

bool openChatWindowAt(const std::string& chatId, float screenX, float screenY) {
  if (!gDevice || !gMainContext) return false;
  const std::string id = chatId;
  const NSPoint point = NSMakePoint(screenX, screenY);
  dispatch_async(dispatch_get_main_queue(), ^{
    createChatWindow(id, false, false, true, point);
  });
  return true;
}
}  // namespace native

// The Edit menu owns Cmd+C/V/X/A/Z as key equivalents, so ImGui never sees
// those keystrokes. Its actions arrive here instead and are replayed into
// ImGui's input queue as the same chords.
static void injectChord(ImGuiKey key, bool shift = false) {
  ImGuiIO& io = ImGui::GetIO();
  io.AddKeyEvent(ImGuiMod_Super, true);
  if (shift) io.AddKeyEvent(ImGuiMod_Shift, true);
  io.AddKeyEvent(key, true);
  io.AddKeyEvent(key, false);
  if (shift) io.AddKeyEvent(ImGuiMod_Shift, false);
  io.AddKeyEvent(ImGuiMod_Super, false);
}

@interface AppDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic, strong) NSWindow* window;
@end

@implementation AppDelegate

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
  (void)sender;
  return native::menuBarOnly() ? NO : YES;   // menu-bar-only mode lives on in the extra
}

- (instancetype)init {
  if (self = [super init]) {
    NSViewController* root = [[AppViewController alloc] initWithNibName:nil bundle:nil];
    self.window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, 1380, 900)
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                            NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    self.window.contentViewController = root;
    [self.window center];
    [self.window makeKeyAndOrderFront:self];
  }
  return self;
}

- (void)newChat:(id)sender { KeyViewScope keyView;
  (void)sender;
  app::App::instance().newChat();
}

- (void)selectPane:(NSMenuItem*)sender { KeyViewScope keyView;
  app::App::instance().pane = static_cast<app::Pane>(sender.tag);
}

- (void)toggleSidebar:(id)sender { KeyViewScope keyView;
  (void)sender;
  app::App& app = app::App::instance();
  app.sidebarVisible = !app.sidebarVisible;
}

- (void)stopGenerating:(id)sender { KeyViewScope keyView;
  (void)sender;
  app::App::instance().stopAllStreams();
}

- (void)paste:(id)sender { KeyViewScope keyView;
  (void)sender;
  if (app::App::instance().pasteImageFromClipboard()) return;
  injectChord(ImGuiKey_V);
}
- (void)copy:(id)sender { KeyViewScope keyView; (void)sender; injectChord(ImGuiKey_C); }
- (void)cut:(id)sender { KeyViewScope keyView; (void)sender; injectChord(ImGuiKey_X); }
- (void)selectAll:(id)sender { KeyViewScope keyView; (void)sender; injectChord(ImGuiKey_A); }
- (void)undo:(id)sender { KeyViewScope keyView; (void)sender; injectChord(ImGuiKey_Z); }
- (void)redo:(id)sender { KeyViewScope keyView; (void)sender; injectChord(ImGuiKey_Z, true); }

- (void)openPalette:(id)sender { KeyViewScope keyView;
  (void)sender;
  app::App& app = app::App::instance();
  app.showPalette = true;
  app.paletteQuery.clear();
}

- (void)quickAsk:(id)sender { KeyViewScope keyView;
  (void)sender;
  app::App::instance().openQuickAsk();
}

- (void)captureScreen:(id)sender { KeyViewScope keyView;
  (void)sender;
  app::App::instance().captureScreenToChat(true);
}

- (void)takePhoto:(id)sender { KeyViewScope keyView;
  (void)sender;
  app::App& app = app::App::instance();
  app.pane = app::Pane::Chat;
  app.openCamera();
}

- (void)toggleFloating:(id)sender { KeyViewScope keyView;
  (void)sender;
  app::App::instance().toggleFloating();
}

- (void)newTab:(id)sender { KeyViewScope keyView;
  (void)sender;
  app::App::instance().openChatInNewTab("");
}

// The tab bar's + button.
- (void)newWindowForTab:(id)sender {
  (void)sender;
  app::App::instance().openChatInNewTab("");
}

- (void)openChatInNewWindow:(id)sender { KeyViewScope keyView;
  (void)sender;
  app::App& app = app::App::instance();
  app.openChatInNewWindow(app.chat.id, false);
}

- (void)openMiniWindow:(id)sender { KeyViewScope keyView;
  (void)sender;
  app::App& app = app::App::instance();
  app.openChatInNewWindow(app.chat.id, true);
}

- (void)findInChat:(id)sender { KeyViewScope keyView;
  (void)sender;
  app::App& app = app::App::instance();
  app.pane = app::Pane::Chat;
  app.showChatSearch = true;
}

- (void)exportMarkdown:(id)sender { KeyViewScope keyView; (void)sender; app::App::instance().exportChat("md"); }
- (void)exportHtml:(id)sender { KeyViewScope keyView; (void)sender; app::App::instance().exportChat("html"); }
- (void)exportPdf:(id)sender { KeyViewScope keyView; (void)sender; app::App::instance().exportChat("pdf"); }

- (void)toggleCompare:(id)sender { KeyViewScope keyView;
  (void)sender;
  app::App& app = app::App::instance();
  app.compareMode = !app.compareMode;
}

- (void)togglePins:(id)sender { KeyViewScope keyView;
  (void)sender;
  app::App& app = app::App::instance();
  app.showPins = !app.showPins;
}

- (void)toggleVoice:(id)sender { KeyViewScope keyView;
  (void)sender;
  app::App& app = app::App::instance();
  app.voiceMode = !app.voiceMode;
}

- (void)showInspector:(id)sender { KeyViewScope keyView;
  (void)sender;
  app::App::instance().showContextInspector = true;
}

// Spotlight results and Handoff both arrive as user activities.
- (BOOL)application:(NSApplication*)application
    continueUserActivity:(NSUserActivity*)activity
      restorationHandler:(void (^)(NSArray<id<NSUserActivityRestoring>>*))restorationHandler {
  (void)application;
  (void)restorationHandler;
  std::string chatId;
  if ([activity.activityType isEqualToString:CSSearchableItemActionType]) {
    NSString* identifier = activity.userInfo[CSSearchableItemActivityIdentifier];
    if (identifier) chatId = identifier.UTF8String;
  } else if (activity.webpageURL) {
    NSString* path = activity.webpageURL.path;
    if ([path hasPrefix:@"/c/"]) chatId = [path substringFromIndex:3].UTF8String;
  }
  if (chatId.empty()) return NO;
  [self.window makeKeyAndOrderFront:nil];
  app::App::instance().openChat(chatId);
  return YES;
}

- (BOOL)applicationShouldHandleReopen:(NSApplication*)sender hasVisibleWindows:(BOOL)visible {
  (void)sender;
  if (!visible) [self.window makeKeyAndOrderFront:nil];
  return YES;
}

@end

static NSMutableDictionary<NSString*, NSMenuItem*>* gMenuItems = nil;
static void tagMenuItem(const char* id, NSMenuItem* item) {
  if (!gMenuItems) gMenuItems = [NSMutableDictionary dictionary];
  gMenuItems[[NSString stringWithUTF8String:id]] = item;
}

namespace native {
void setMenuShortcut(const std::string& id, const std::string& key, int mods) {
  NSMenuItem* item = gMenuItems ? gMenuItems[[NSString stringWithUTF8String:id.c_str()]] : nil;
  if (!item) return;
  item.keyEquivalent = [NSString stringWithUTF8String:key.c_str()];
  NSEventModifierFlags flags = 0;
  if (mods & 1) flags |= NSEventModifierFlagCommand;
  if (mods & 2) flags |= NSEventModifierFlagShift;
  if (mods & 4) flags |= NSEventModifierFlagOption;
  if (mods & 8) flags |= NSEventModifierFlagControl;
  item.keyEquivalentModifierMask = flags;
}
void suspendMenuShortcuts() {
  for (NSString* id in gMenuItems) gMenuItems[id].keyEquivalent = @"";
}
}  // namespace native

static void buildMenuBar() {
  NSMenu* menuBar = [[NSMenu alloc] init];
  NSMenuItem* appItem = [[NSMenuItem alloc] init];
  [menuBar addItem:appItem];

  NSMenu* appMenu = [[NSMenu alloc] initWithTitle:@"Open WebUI"];
  [appMenu addItemWithTitle:@"About Open WebUI" action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];
  [appMenu addItem:[NSMenuItem separatorItem]];
  [appMenu addItemWithTitle:@"Hide Open WebUI" action:@selector(hide:) keyEquivalent:@"h"];
  [appMenu addItemWithTitle:@"Quit Open WebUI" action:@selector(terminate:) keyEquivalent:@"q"];
  appItem.submenu = appMenu;

  NSMenuItem* fileItem = [[NSMenuItem alloc] init];
  [menuBar addItem:fileItem];
  NSMenu* fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
  tagMenuItem("chat.new", [fileMenu addItemWithTitle:@"New Chat" action:@selector(newChat:) keyEquivalent:@"n"]);
  tagMenuItem("quickAsk", [fileMenu addItemWithTitle:@"Quick Ask" action:@selector(quickAsk:) keyEquivalent:@"j"]);
  {
    NSMenuItem* item = [fileMenu addItemWithTitle:@"Open Chat in New Window" action:@selector(openChatInNewWindow:) keyEquivalent:@"n"];
    item.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagShift;
    tagMenuItem("chat.newWindow", item);
    tagMenuItem("chat.newTab", [fileMenu addItemWithTitle:@"New Tab" action:@selector(newTab:) keyEquivalent:@"t"]);
    [fileMenu addItemWithTitle:@"Take Photo..." action:@selector(takePhoto:) keyEquivalent:@""];
    [fileMenu addItemWithTitle:@"Open Chat as Floating Mini Window" action:@selector(openMiniWindow:) keyEquivalent:@""];
  }
  {
    NSMenuItem* capture = [fileMenu addItemWithTitle:@"Capture Screen to Chat"
                                              action:@selector(captureScreen:)
                                       keyEquivalent:@"2"];
    capture.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagShift;
    tagMenuItem("chat.capture", capture);
  }
  NSMenuItem* exportItem = [fileMenu addItemWithTitle:@"Export Chat" action:nil keyEquivalent:@""];
  NSMenu* exportMenu = [[NSMenu alloc] initWithTitle:@"Export Chat"];
  [exportMenu addItemWithTitle:@"Markdown..." action:@selector(exportMarkdown:) keyEquivalent:@""];
  [exportMenu addItemWithTitle:@"HTML..." action:@selector(exportHtml:) keyEquivalent:@""];
  tagMenuItem("chat.export", [exportMenu addItemWithTitle:@"PDF..." action:@selector(exportPdf:) keyEquivalent:@"p"]);
  exportItem.submenu = exportMenu;
  [fileMenu addItemWithTitle:@"Close Window" action:@selector(performClose:) keyEquivalent:@"w"];
  fileItem.submenu = fileMenu;

  NSMenuItem* editItem = [[NSMenuItem alloc] init];
  [menuBar addItem:editItem];
  NSMenu* editMenu = [[NSMenu alloc] initWithTitle:@"Edit"];
  [editMenu addItemWithTitle:@"Undo" action:@selector(undo:) keyEquivalent:@"z"];
  [editMenu addItemWithTitle:@"Redo" action:@selector(redo:) keyEquivalent:@"Z"];
  [editMenu addItem:[NSMenuItem separatorItem]];
  [editMenu addItemWithTitle:@"Cut" action:@selector(cut:) keyEquivalent:@"x"];
  [editMenu addItemWithTitle:@"Copy" action:@selector(copy:) keyEquivalent:@"c"];
  [editMenu addItemWithTitle:@"Paste" action:@selector(paste:) keyEquivalent:@"v"];
  [editMenu addItemWithTitle:@"Select All" action:@selector(selectAll:) keyEquivalent:@"a"];
  [editMenu addItem:[NSMenuItem separatorItem]];
  tagMenuItem("chat.find", [editMenu addItemWithTitle:@"Find in Chat" action:@selector(findInChat:) keyEquivalent:@"f"]);
  editItem.submenu = editMenu;

  NSMenuItem* viewItem = [[NSMenuItem alloc] init];
  [menuBar addItem:viewItem];
  NSMenu* viewMenu = [[NSMenu alloc] initWithTitle:@"View"];
  tagMenuItem("palette", [viewMenu addItemWithTitle:@"Command Palette" action:@selector(openPalette:) keyEquivalent:@"k"]);
  [viewMenu addItem:[NSMenuItem separatorItem]];
  struct PaneEntry {
    const char* title;
    const char* key;
    app::Pane pane;
    const char* id;
  };
  const PaneEntry entries[] = {
      {"Chats", "1", app::Pane::Chat, "pane.chat"},       {"Notes", "2", app::Pane::Notes, "pane.notes"},
      {"Channels", "3", app::Pane::Channels, "pane.channels"}, {"Workspace", "4", app::Pane::Workspace, "pane.workspace"},
      {"Images", "5", app::Pane::Images, "pane.images"},    {"Files", "6", app::Pane::Files, "pane.files"},
      {"Memories", "7", app::Pane::Memories, "pane.memories"}, {"Admin", "8", app::Pane::Admin, "pane.admin"},
      {"API Console", "9", app::Pane::Console, "pane.console"}, {"Settings", "0", app::Pane::Settings, "pane.settings"},
      {"Tools", "-", app::Pane::Tools, "pane.tools"},
  };
  for (const PaneEntry& entry : entries) {
    NSMenuItem* item =
        [[NSMenuItem alloc] initWithTitle:[NSString stringWithUTF8String:entry.title]
                                   action:@selector(selectPane:)
                            keyEquivalent:[NSString stringWithUTF8String:entry.key]];
    item.tag = static_cast<NSInteger>(entry.pane);
    [viewMenu addItem:item];
    tagMenuItem(entry.id, item);
  }
  [viewMenu addItem:[NSMenuItem separatorItem]];
  tagMenuItem("view.sidebar", [viewMenu addItemWithTitle:@"Toggle Sidebar" action:@selector(toggleSidebar:) keyEquivalent:@"b"]);
  {
    NSMenuItem* item = [viewMenu addItemWithTitle:@"Float on Top" action:@selector(toggleFloating:) keyEquivalent:@"t"];
    item.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagShift;
    tagMenuItem("view.float", item);
  }
  tagMenuItem("chat.compare", [viewMenu addItemWithTitle:@"Compare Answers" action:@selector(toggleCompare:) keyEquivalent:@"d"]);
  tagMenuItem("chat.pins", [viewMenu addItemWithTitle:@"Pinned Messages" action:@selector(togglePins:) keyEquivalent:@"i"]);
  tagMenuItem("chat.inspector", [viewMenu addItemWithTitle:@"What Was Sent" action:@selector(showInspector:) keyEquivalent:@"e"]);
  tagMenuItem("chat.voice", [viewMenu addItemWithTitle:@"Voice Conversation" action:@selector(toggleVoice:) keyEquivalent:@"u"]);
  tagMenuItem("chat.stop", [viewMenu addItemWithTitle:@"Stop Generating" action:@selector(stopGenerating:) keyEquivalent:@"."]);
  viewItem.submenu = viewMenu;

  NSMenuItem* windowItem = [[NSMenuItem alloc] init];
  [menuBar addItem:windowItem];
  NSMenu* windowMenu = [[NSMenu alloc] initWithTitle:@"Window"];
  [windowMenu addItemWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
  [windowMenu addItemWithTitle:@"Zoom" action:@selector(performZoom:) keyEquivalent:@""];
  windowItem.submenu = windowMenu;
  NSApp.windowsMenu = windowMenu;

  NSApp.mainMenu = menuBar;
}

int runSelfTest();
int runMcpSelfTest(int argc, const char* argv[]);
int runSchemaSelfTest();
int runPromptVarsSelfTest();
int runAgentSelfTest();
int runPluginSelfTest();
int runRedactSelfTest();
int runPrivateSelfTest();
int runContextSelfTest();
int runFileBlocksSelfTest();
int runUsageSelfTest();
int runImportSelfTest();
int runRecipeSelfTest();
int runTestLoopSelfTest();
int runRepoMapSelfTest();
int runAuditSelfTest();
int runBackendsSelfTest();
int runDocTextSelfTest();
int runBrowserSelfTest();
int runScriptToolsSelfTest();
int runChatIndexSelfTest();
int runMemoryPropSelfTest();
int runVaultSelfTest();
int runHealthSelfTest();
int runModelLibSelfTest();
int runBudgetSelfTest();
int runWarmupSelfTest();
int runSelectionSelfTest();
int runLowPowerSelfTest();
int runEditorSelfTest();
int runCiteSelfTest();
int runTtsSelfTest();
int runTrafficSelfTest();
int runRetrievalSelfTest();
int runPlaygroundSelfTest();
int runEvalSelfTest();
int runLibrarySelfTest();
int runSmartFoldersSelfTest();
int runHybridSelfTest();
int runKeymapSelfTest();
int runThemeSelfTest();
int runOnboardingSelfTest();
int runReadingModeSelfTest();
int runVirtualizeSelfTest();
int runMenuBarSelfTest();
int runRerankSelfTest();
int runAccessibilitySelfTest();
int runPoolSelfTest();
int runOutputSelfTest();
int runLogprobSelfTest();
int runFileEditorSelfTest();
int runLayoutSelfTest();
int runImageMetaSelfTest();
int runGraphSelfTest();
int runSketchSelfTest();
int runLspSelfTest();
int runNotebookSelfTest();
int runEmojiSelfTest();
int runCrashSelfTest();
int runUpdateSelfTest();
int runTranscribeSelfTest();
int runI18nSelfTest();
int runImgEditSelfTest();
int runFadeSelfTest();
int runVoiceOverSelfTest();
int runWindowsSelfTest();
int runPersonalSelfTest();
int runCorpusSelfTest();
int runScreenWatchSelfTest();
int runCameraSelfTest();
int runMeetingSelfTest();
int runVoiceSelfTest();
int runNotifySelfTest();
int runAutoModeSelfTest();
int runCrashChild(int argc, const char* argv[]);

int main(int argc, const char* argv[]) {
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--selftest") return runSelfTest();
    if (std::string(argv[i]) == "--mcptest") return runMcpSelfTest(argc, argv);
    if (std::string(argv[i]) == "--schematest") return runSchemaSelfTest();
    if (std::string(argv[i]) == "--varstest") return runPromptVarsSelfTest();
    if (std::string(argv[i]) == "--agenttest") return runAgentSelfTest();
    if (std::string(argv[i]) == "--plugintest") return runPluginSelfTest();
    if (std::string(argv[i]) == "--redacttest") return runRedactSelfTest();
    if (std::string(argv[i]) == "--privatetest") return runPrivateSelfTest();
    if (std::string(argv[i]) == "--contexttest") return runContextSelfTest();
    if (std::string(argv[i]) == "--applytest") return runFileBlocksSelfTest();
    if (std::string(argv[i]) == "--usagetest") return runUsageSelfTest();
    if (std::string(argv[i]) == "--importtest") return runImportSelfTest();
    if (std::string(argv[i]) == "--recipetest") return runRecipeSelfTest();
    if (std::string(argv[i]) == "--testlooptest") return runTestLoopSelfTest();
    if (std::string(argv[i]) == "--repomaptest") return runRepoMapSelfTest();
    if (std::string(argv[i]) == "--audittest") return runAuditSelfTest();
    if (std::string(argv[i]) == "--backendstest") return runBackendsSelfTest();
    if (std::string(argv[i]) == "--doctest") return runDocTextSelfTest();
    if (std::string(argv[i]) == "--browsertest") return runBrowserSelfTest();
    if (std::string(argv[i]) == "--scripttest") return runScriptToolsSelfTest();
    if (std::string(argv[i]) == "--chatindextest") return runChatIndexSelfTest();
    if (std::string(argv[i]) == "--memorytest") return runMemoryPropSelfTest();
    if (std::string(argv[i]) == "--vaulttest") return runVaultSelfTest();
    if (std::string(argv[i]) == "--healthtest") return runHealthSelfTest();
    if (std::string(argv[i]) == "--modellibtest") return runModelLibSelfTest();
    if (std::string(argv[i]) == "--budgettest") return runBudgetSelfTest();
    if (std::string(argv[i]) == "--warmuptest") return runWarmupSelfTest();
    if (std::string(argv[i]) == "--selectiontest") return runSelectionSelfTest();
    if (std::string(argv[i]) == "--lowpowertest") return runLowPowerSelfTest();
    if (std::string(argv[i]) == "--editortest") return runEditorSelfTest();
    if (std::string(argv[i]) == "--citetest") return runCiteSelfTest();
    if (std::string(argv[i]) == "--ttstest") return runTtsSelfTest();
    if (std::string(argv[i]) == "--traffictest") return runTrafficSelfTest();
    if (std::string(argv[i]) == "--retrievaltest") return runRetrievalSelfTest();
    if (std::string(argv[i]) == "--playgroundtest") return runPlaygroundSelfTest();
    if (std::string(argv[i]) == "--evaltest") return runEvalSelfTest();
    if (std::string(argv[i]) == "--librarytest") return runLibrarySelfTest();
    if (std::string(argv[i]) == "--foldertest") return runSmartFoldersSelfTest();
    if (std::string(argv[i]) == "--hybridtest") return runHybridSelfTest();
    if (std::string(argv[i]) == "--keymaptest") return runKeymapSelfTest();
    if (std::string(argv[i]) == "--themetest") return runThemeSelfTest();
    if (std::string(argv[i]) == "--onboardtest") return runOnboardingSelfTest();
    if (std::string(argv[i]) == "--readingtest") return runReadingModeSelfTest();
    if (std::string(argv[i]) == "--virtualtest") return runVirtualizeSelfTest();
    if (std::string(argv[i]) == "--menubartest") return runMenuBarSelfTest();
    if (std::string(argv[i]) == "--reranktest") return runRerankSelfTest();
    if (std::string(argv[i]) == "--a11ytest") return runAccessibilitySelfTest();
    if (std::string(argv[i]) == "--pooltest") return runPoolSelfTest();
    if (std::string(argv[i]) == "--outputtest") return runOutputSelfTest();
    if (std::string(argv[i]) == "--logprobtest") return runLogprobSelfTest();
    if (std::string(argv[i]) == "--fileeditortest") return runFileEditorSelfTest();
    if (std::string(argv[i]) == "--layouttest") return runLayoutSelfTest();
    if (std::string(argv[i]) == "--imagemetatest") return runImageMetaSelfTest();
    if (std::string(argv[i]) == "--graphtest") return runGraphSelfTest();
    if (std::string(argv[i]) == "--sketchtest") return runSketchSelfTest();
    if (std::string(argv[i]) == "--lsptest") return runLspSelfTest();
    if (std::string(argv[i]) == "--notebooktest") return runNotebookSelfTest();
    if (std::string(argv[i]) == "--emojitest") return runEmojiSelfTest();
    if (std::string(argv[i]) == "--crashtest") return runCrashSelfTest();
    if (std::string(argv[i]) == "--updatetest") return runUpdateSelfTest();
    if (std::string(argv[i]) == "--transcribetest") return runTranscribeSelfTest();
    if (std::string(argv[i]) == "--i18ntest") return runI18nSelfTest();
    if (std::string(argv[i]) == "--imgedittest") return runImgEditSelfTest();
    if (std::string(argv[i]) == "--fadetest") return runFadeSelfTest();
    if (std::string(argv[i]) == "--voiceovertest") return runVoiceOverSelfTest();
    if (std::string(argv[i]) == "--windowstest") return runWindowsSelfTest();
    if (std::string(argv[i]) == "--personaltest") return runPersonalSelfTest();
    if (std::string(argv[i]) == "--corpustest") return runCorpusSelfTest();
    if (std::string(argv[i]) == "--screenwatchtest") return runScreenWatchSelfTest();
    if (std::string(argv[i]) == "--cameratest") return runCameraSelfTest();
    if (std::string(argv[i]) == "--meetingtest") return runMeetingSelfTest();
    if (std::string(argv[i]) == "--voicetest") return runVoiceSelfTest();
    if (std::string(argv[i]) == "--notifytest") return runNotifySelfTest();
    if (std::string(argv[i]) == "--automodetest") return runAutoModeSelfTest();
    if (std::string(argv[i]) == "--crashchild") return runCrashChild(argc, argv);
  }
  // A chat in its own window is another instance started with these flags.
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--chat" && i + 1 < argc) {
      app::App::instance().startChatId = argv[++i];
      app::App::instance().secondaryInstance = true;
    } else if (arg == "--mini") {
      app::App::instance().miniWindow = true;
      app::App::instance().secondaryInstance = true;
    }
  }
  @autoreleasepool {
    // Crash reports go to the support folder; OWUI_CRASH_QUIET makes a crash
    // exit quietly after writing (tests: no macOS report, no dialog).
    crashreport::install(core::supportDir() + "/crashes", platform::appVersion(), getenv("OWUI_CRASH_QUIET") != nullptr);
    net::globalInit();
    async::start(6);
    NSApp = [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    buildMenuBar();
    AppDelegate* delegate = [[AppDelegate alloc] init];
    NSApp.delegate = delegate;
    [NSApp run];
  }
  return 0;
}
