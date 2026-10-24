// macOS entry point: Cocoa window + MetalKit view + Dear ImGui, driving
// app::App once per frame.
#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

#include "app.h"
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

- (void)viewDidLoad {
  [super viewDidLoad];
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
  [self.view.window setFrameAutosaveName:@"OpenWebUIMainWindow"];
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

@interface AppDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic, strong) NSWindow* window;
@end

@implementation AppDelegate

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
  (void)sender;
  return YES;
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

@end

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
  [fileMenu addItemWithTitle:@"New Chat" action:@selector(newChat:) keyEquivalent:@"n"];
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
  editItem.submenu = editMenu;

  NSMenuItem* viewItem = [[NSMenuItem alloc] init];
  [menuBar addItem:viewItem];
  NSMenu* viewMenu = [[NSMenu alloc] initWithTitle:@"View"];
  struct PaneEntry {
    const char* title;
    const char* key;
    app::Pane pane;
  };
  const PaneEntry entries[] = {
      {"Chats", "1", app::Pane::Chat},       {"Notes", "2", app::Pane::Notes},
      {"Channels", "3", app::Pane::Channels}, {"Workspace", "4", app::Pane::Workspace},
      {"Images", "5", app::Pane::Images},    {"Files", "6", app::Pane::Files},
      {"Memories", "7", app::Pane::Memories}, {"Admin", "8", app::Pane::Admin},
      {"API Console", "9", app::Pane::Console}, {"Settings", "0", app::Pane::Settings},
  };
  for (const PaneEntry& entry : entries) {
    NSMenuItem* item =
        [[NSMenuItem alloc] initWithTitle:[NSString stringWithUTF8String:entry.title]
                                   action:@selector(selectPane:)
                            keyEquivalent:[NSString stringWithUTF8String:entry.key]];
    item.tag = static_cast<NSInteger>(entry.pane);
    [viewMenu addItem:item];
  }
  [viewMenu addItem:[NSMenuItem separatorItem]];
  [viewMenu addItemWithTitle:@"Toggle Sidebar" action:@selector(toggleSidebar:) keyEquivalent:@"b"];
  [viewMenu addItemWithTitle:@"Stop Generating" action:@selector(stopGenerating:) keyEquivalent:@"."];
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

int main(int argc, const char* argv[]) {
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--selftest") return runSelfTest();
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
