#include "platform.h"

#import <AVFoundation/AVFoundation.h>
#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <UserNotifications/UserNotifications.h>

#include "core/common.h"

namespace platform {
namespace {

AVAudioPlayer* gPlayer = nil;
AVAudioRecorder* gRecorder = nil;
NSURL* gRecordingUrl = nil;

}  // namespace

std::vector<std::string> openFiles(bool allowMultiple) {
  std::vector<std::string> out;
  NSOpenPanel* panel = [NSOpenPanel openPanel];
  panel.allowsMultipleSelection = allowMultiple;
  panel.canChooseDirectories = NO;
  panel.canChooseFiles = YES;
  if ([panel runModal] == NSModalResponseOK) {
    for (NSURL* url in panel.URLs) out.push_back(url.path.UTF8String);
  }
  return out;
}

std::string saveFile(const std::string& suggestedName) {
  NSSavePanel* panel = [NSSavePanel savePanel];
  panel.nameFieldStringValue = [NSString stringWithUTF8String:suggestedName.c_str()];
  if ([panel runModal] == NSModalResponseOK && panel.URL) return panel.URL.path.UTF8String;
  return {};
}

bool revealInFinder(const std::string& path) {
  NSString* p = [NSString stringWithUTF8String:path.c_str()];
  return [[NSWorkspace sharedWorkspace] selectFile:p inFileViewerRootedAtPath:@""];
}

void openUrl(const std::string& url) {
  NSString* s = [NSString stringWithUTF8String:url.c_str()];
  NSURL* u = [NSURL URLWithString:s];
  if (u) [[NSWorkspace sharedWorkspace] openURL:u];
}

void notify(const std::string& title, const std::string& body) {
  UNUserNotificationCenter* center = [UNUserNotificationCenter currentNotificationCenter];
  [center requestAuthorizationWithOptions:UNAuthorizationOptionAlert | UNAuthorizationOptionSound
                        completionHandler:^(BOOL granted, NSError* error) {
                          (void)granted;
                          (void)error;
                        }];
  UNMutableNotificationContent* content = [[UNMutableNotificationContent alloc] init];
  content.title = [NSString stringWithUTF8String:title.c_str()];
  content.body = [NSString stringWithUTF8String:body.c_str()];
  UNNotificationRequest* request =
      [UNNotificationRequest requestWithIdentifier:[[NSUUID UUID] UUIDString]
                                           content:content
                                           trigger:nil];
  [center addNotificationRequest:request withCompletionHandler:nil];
}

void playAudio(const std::string& bytes, const std::string& extensionHint) {
  (void)extensionHint;
  stopAudio();
  NSData* data = [NSData dataWithBytes:bytes.data() length:bytes.size()];
  NSError* error = nil;
  gPlayer = [[AVAudioPlayer alloc] initWithData:data error:&error];
  if (!gPlayer) {
    NSLog(@"audio playback failed: %@", error);
    return;
  }
  [gPlayer play];
}

void stopAudio() {
  if (gPlayer) {
    [gPlayer stop];
    gPlayer = nil;
  }
}

bool audioPlaying() { return gPlayer != nil && gPlayer.isPlaying; }

bool startRecording() {
  if (gRecorder) return false;
  NSString* tmp = [NSTemporaryDirectory()
      stringByAppendingPathComponent:[NSString stringWithFormat:@"owui-%@.wav",
                                                                [[NSUUID UUID] UUIDString]]];
  gRecordingUrl = [NSURL fileURLWithPath:tmp];
  NSDictionary* settings = @{
    AVFormatIDKey : @(kAudioFormatLinearPCM),
    AVSampleRateKey : @16000.0,
    AVNumberOfChannelsKey : @1,
    AVLinearPCMBitDepthKey : @16,
    AVLinearPCMIsFloatKey : @NO,
    AVLinearPCMIsBigEndianKey : @NO,
  };
  NSError* error = nil;
  gRecorder = [[AVAudioRecorder alloc] initWithURL:gRecordingUrl settings:settings error:&error];
  if (!gRecorder) {
    NSLog(@"recorder init failed: %@", error);
    gRecordingUrl = nil;
    return false;
  }
  gRecorder.meteringEnabled = YES;
  [gRecorder prepareToRecord];
  return [gRecorder record];
}

std::string stopRecording() {
  if (!gRecorder) return {};
  [gRecorder stop];
  std::string path = gRecordingUrl.path.UTF8String;
  gRecorder = nil;
  gRecordingUrl = nil;
  std::string bytes = core::readFile(path);
  remove(path.c_str());
  return bytes;
}

bool recording() { return gRecorder != nil && gRecorder.isRecording; }

float recordingLevel() {
  if (!gRecorder) return 0.0f;
  [gRecorder updateMeters];
  float db = [gRecorder averagePowerForChannel:0];
  float level = (db + 60.0f) / 60.0f;
  return level < 0.0f ? 0.0f : (level > 1.0f ? 1.0f : level);
}

std::string clipboardImage() {
  NSPasteboard* pb = [NSPasteboard generalPasteboard];
  NSArray* classes = @[ [NSImage class] ];
  if (![pb canReadObjectForClasses:classes options:nil]) return {};
  NSArray* objects = [pb readObjectsForClasses:classes options:nil];
  if (objects.count == 0) return {};
  NSImage* image = objects.firstObject;
  CGImageRef cgImage = [image CGImageForProposedRect:nil context:nil hints:nil];
  if (!cgImage) return {};
  NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithCGImage:cgImage];
  NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
  return std::string(static_cast<const char*>(png.bytes), png.length);
}

std::string clipboardText() {
  NSString* s = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
  return s ? std::string(s.UTF8String) : std::string();
}

bool clipboardHasText() {
  NSPasteboard* pb = [NSPasteboard generalPasteboard];
  return [pb availableTypeFromArray:@[ NSPasteboardTypeString ]] != nil;
}

void setClipboardText(const std::string& text) {
  NSPasteboard* pb = [NSPasteboard generalPasteboard];
  [pb clearContents];
  [pb setString:[NSString stringWithUTF8String:text.c_str()] forType:NSPasteboardTypeString];
}

std::string appVersion() {
  NSString* v = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
  return v ? v.UTF8String : "1.0";
}

}  // namespace platform
