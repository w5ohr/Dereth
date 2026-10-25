// macOS entry point: Cocoa window + MetalKit view + Dear ImGui, driving
// app::App once per frame.
#import <Cocoa/Cocoa.h>
#import <CoreSpotlight/CoreSpotlight.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

#include "app.h"
#include "native.h"
#include "core/async.h"
#include "core/common.h"
#include "core/http.h"
#include "imgui.h"
#include "imgui_impl_metal.h"
#include "imgui_impl_osx.h"
#include "ui/images.h"
#include "ui/theme.h"

static std::string gIniPath;

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
  ImGui::CreateContext();
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
  self.view = [[MTKView alloc] initWithFrame:CGRectMake(0, 0, 1380, 900)];
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
  self.mtkView.device = self.device;
  self.mtkView.delegate = self;
  ImGui_ImplOSX_Init(self.view);
  [NSApp activateIgnoringOtherApps:YES];
  app::App::instance().init();
}

- (void)viewWillAppear {
  [super viewWillAppear];
  self.view.window.delegate = self;
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
  if (view.window.keyWindow) {
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
  ImGui::NewFrame();

  app::App::instance().frame();

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

- (void)windowWillClose:(NSNotification*)notification {
  (void)notification;
  app::App::instance().shutdown();
  ImGui_ImplMetal_Shutdown();
  ImGui_ImplOSX_Shutdown();
  ImGui::DestroyContext();
}

@end

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

- (void)newChat:(id)sender {
  (void)sender;
  app::App::instance().newChat();
}

- (void)selectPane:(NSMenuItem*)sender {
  app::App::instance().pane = static_cast<app::Pane>(sender.tag);
}

- (void)toggleSidebar:(id)sender {
  (void)sender;
  app::App& app = app::App::instance();
  app.sidebarVisible = !app.sidebarVisible;
}

- (void)stopGenerating:(id)sender {
  (void)sender;
  app::App::instance().stopAllStreams();
}

- (void)paste:(id)sender {
  (void)sender;
  if (app::App::instance().pasteImageFromClipboard()) return;
  injectChord(ImGuiKey_V);
}
- (void)copy:(id)sender { (void)sender; injectChord(ImGuiKey_C); }
- (void)cut:(id)sender { (void)sender; injectChord(ImGuiKey_X); }
- (void)selectAll:(id)sender { (void)sender; injectChord(ImGuiKey_A); }
- (void)undo:(id)sender { (void)sender; injectChord(ImGuiKey_Z); }
- (void)redo:(id)sender { (void)sender; injectChord(ImGuiKey_Z, true); }

- (void)openPalette:(id)sender {
  (void)sender;
  app::App& app = app::App::instance();
  app.showPalette = true;
  app.paletteQuery.clear();
}

- (void)quickAsk:(id)sender {
  (void)sender;
  app::App::instance().openQuickAsk();
}

- (void)captureScreen:(id)sender {
  (void)sender;
  app::App::instance().captureScreenToChat(true);
}

- (void)toggleFloating:(id)sender {
  (void)sender;
  app::App::instance().toggleFloating();
}

- (void)openChatInNewWindow:(id)sender {
  (void)sender;
  app::App& app = app::App::instance();
  app.openChatInNewWindow(app.chat.id, false);
}

- (void)openMiniWindow:(id)sender {
  (void)sender;
  app::App& app = app::App::instance();
  app.openChatInNewWindow(app.chat.id, true);
}

- (void)findInChat:(id)sender {
  (void)sender;
  app::App& app = app::App::instance();
  app.pane = app::Pane::Chat;
  app.showChatSearch = true;
}

- (void)exportMarkdown:(id)sender { (void)sender; app::App::instance().exportChat("md"); }
- (void)exportHtml:(id)sender { (void)sender; app::App::instance().exportChat("html"); }
- (void)exportPdf:(id)sender { (void)sender; app::App::instance().exportChat("pdf"); }

- (void)toggleCompare:(id)sender {
  (void)sender;
  app::App& app = app::App::instance();
  app.compareMode = !app.compareMode;
}

- (void)togglePins:(id)sender {
  (void)sender;
  app::App& app = app::App::instance();
  app.showPins = !app.showPins;
}

- (void)toggleVoice:(id)sender {
  (void)sender;
  app::App& app = app::App::instance();
  app.voiceMode = !app.voiceMode;
}

- (void)showInspector:(id)sender {
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
