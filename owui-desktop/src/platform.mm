#include "platform.h"

#include <cstdlib>

#import <AVFoundation/AVFoundation.h>
#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <UserNotifications/UserNotifications.h>

#include <algorithm>

#include "core/common.h"

namespace platform {
namespace {

AVAudioPlayer* gPlayer = nil;
AVAudioRecorder* gRecorder = nil;
NSURL* gRecordingUrl = nil;
AVSpeechSynthesizer* gSynth = nil;
id gSpeechDelegate = nil;
bool gSpeechPending = false;   // set on speak, cleared when the utterance finishes or is cancelled

}  // namespace
}  // namespace platform

@interface OwuiSpeechDelegate : NSObject <AVSpeechSynthesizerDelegate>
@end
@implementation OwuiSpeechDelegate
- (void)speechSynthesizer:(AVSpeechSynthesizer*)synthesizer didFinishSpeechUtterance:(AVSpeechUtterance*)utterance {
  (void)synthesizer;
  (void)utterance;
  platform::gSpeechPending = false;
}
- (void)speechSynthesizer:(AVSpeechSynthesizer*)synthesizer didCancelSpeechUtterance:(AVSpeechUtterance*)utterance {
  (void)synthesizer;
  (void)utterance;
  platform::gSpeechPending = false;
}
@end

namespace platform {

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
  stopSpeaking();
}

bool audioPlaying() { return (gPlayer != nil && gPlayer.isPlaying) || speaking(); }

std::vector<SpeechVoice> speechVoices() {
  std::vector<SpeechVoice> out;
  for (AVSpeechSynthesisVoice* v in [AVSpeechSynthesisVoice speechVoices]) {
    SpeechVoice sv;
    sv.id = v.identifier ? v.identifier.UTF8String : "";
    sv.name = v.name ? v.name.UTF8String : "";
    sv.language = v.language ? v.language.UTF8String : "";
    sv.quality = v.quality == AVSpeechSynthesisVoiceQualityPremium ? 3 : v.quality == AVSpeechSynthesisVoiceQualityEnhanced ? 2 : 1;
    if (!sv.id.empty()) out.push_back(sv);
  }
  std::sort(out.begin(), out.end(), [](const SpeechVoice& a, const SpeechVoice& b) {
    if (a.language != b.language) return a.language < b.language;
    if (a.name != b.name) return a.name < b.name;
    return a.quality > b.quality;
  });
  return out;
}

std::string speechLanguage() {
  NSString* code = [AVSpeechSynthesisVoice currentLanguageCode];
  return code ? code.UTF8String : "en-US";
}

void speakLocal(const std::string& text, const std::string& voiceId, float rate, float pitch) {
  if (!gSynth) {
    gSynth = [[AVSpeechSynthesizer alloc] init];
    gSpeechDelegate = [[OwuiSpeechDelegate alloc] init];
    gSynth.delegate = gSpeechDelegate;
  }
  if (gSynth.isSpeaking) [gSynth stopSpeakingAtBoundary:AVSpeechBoundaryImmediate];
  AVSpeechUtterance* u = [AVSpeechUtterance speechUtteranceWithString:[NSString stringWithUTF8String:text.c_str()]];
  AVSpeechSynthesisVoice* voice = nil;
  if (!voiceId.empty()) voice = [AVSpeechSynthesisVoice voiceWithIdentifier:[NSString stringWithUTF8String:voiceId.c_str()]];
  if (!voice) voice = [AVSpeechSynthesisVoice voiceWithLanguage:[AVSpeechSynthesisVoice currentLanguageCode]];
  if (voice) u.voice = voice;
  u.rate = std::max(0.05f, std::min(0.95f, rate));
  u.pitchMultiplier = std::max(0.5f, std::min(2.0f, pitch));
  gSpeechPending = true;
  [gSynth speakUtterance:u];
}

void stopSpeaking() {
  gSpeechPending = false;
  if (gSynth && gSynth.isSpeaking) [gSynth stopSpeakingAtBoundary:AVSpeechBoundaryImmediate];
}

bool speaking() { return gSpeechPending || (gSynth != nil && gSynth.isSpeaking); }

namespace {
bool gFakeRecording = false;
double gFakeStarted = 0;
const char* fakeMicPath() {
  const char* p = std::getenv("OWUI_FAKE_MIC");
  return p && *p ? p : nullptr;
}
}  // namespace

bool fakeMicrophone() { return fakeMicPath() != nullptr; }

bool startRecording() {
  if (fakeMicPath()) {   // the test hook: no microphone
    if (gFakeRecording) return false;
    gFakeRecording = true;
    gFakeStarted = [NSProcessInfo processInfo].systemUptime;
    return true;
  }
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
  if (fakeMicPath()) {
    if (!gFakeRecording) return {};
    gFakeRecording = false;
    return core::readFile(fakeMicPath());
  }
  if (!gRecorder) return {};
  [gRecorder stop];
  std::string path = gRecordingUrl.path.UTF8String;
  gRecorder = nil;
  gRecordingUrl = nil;
  std::string bytes = core::readFile(path);
  remove(path.c_str());
  return bytes;
}

bool recording() { return fakeMicPath() ? gFakeRecording : (gRecorder != nil && gRecorder.isRecording); }

float recordingLevelDb() {
  if (fakeMicPath()) {
    // A quiet room, three seconds of speech, then quiet: what the detector needs to end a turn.
    const double t = [NSProcessInfo processInfo].systemUptime - gFakeStarted;
    return gFakeRecording && t >= 0.5 && t < 3.5 ? -18.0f : -58.0f;
  }
  if (!gRecorder) return -160.0f;
  [gRecorder updateMeters];
  return [gRecorder averagePowerForChannel:0];
}

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
