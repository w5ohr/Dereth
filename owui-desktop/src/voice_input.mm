// Push-to-talk, voice-activity detection and the wake phrase (#119): hold
// Control-Option-Space from any app to talk, let go to send; voice mode now
// ends a turn with a detector that learns the room; and, when switched on,
// on-device speech recognition listens for a wake phrase (nothing leaves the
// Mac until it hears it). Microphone and speech-recognition access are asked
// for from the Settings card, or by the first hold of the hotkey.
#import <AVFoundation/AVFoundation.h>
#import <Speech/Speech.h>

#include <ctime>

#include "app.h"
#include "core/async.h"
#include "core/common.h"
#include "core/settings.h"
#include "core/voice.h"
#include "imgui_stdlib.h"
#include "native.h"
#include "platform.h"
#include "ui/theme.h"
#include "ui/widgets.h"

@interface OWUIWakeListener : NSObject
@property(nonatomic, strong) AVAudioEngine* engine;
@property(nonatomic, strong) SFSpeechRecognizer* recognizer;
@property(nonatomic, strong) SFSpeechAudioBufferRecognitionRequest* request;
@property(nonatomic, strong) SFSpeechRecognitionTask* task;
@property(nonatomic) double startedAt;
- (BOOL)startWithPhrase:(NSString*)phrase onHeard:(void (^)(NSString* after))heard error:(std::string*)error;
- (void)stop;
@end

@implementation OWUIWakeListener

- (BOOL)startWithPhrase:(NSString*)phrase onHeard:(void (^)(NSString*))heard error:(std::string*)error {
  [self stop];
  self.recognizer = [[SFSpeechRecognizer alloc] init];
  if (!self.recognizer || !self.recognizer.available) {
    *error = "speech recognition is not available";
    return NO;
  }
  if (!self.recognizer.supportsOnDeviceRecognition) {
    *error = "this Mac cannot recognise speech on the device for this language";
    return NO;
  }
  self.request = [[SFSpeechAudioBufferRecognitionRequest alloc] init];
  self.request.requiresOnDeviceRecognition = YES;   // the wake phrase never leaves the Mac
  self.request.shouldReportPartialResults = YES;
  self.engine = [[AVAudioEngine alloc] init];
  AVAudioInputNode* input = self.engine.inputNode;
  AVAudioFormat* format = [input outputFormatForBus:0];
  SFSpeechAudioBufferRecognitionRequest* request = self.request;
  [input installTapOnBus:0 bufferSize:2048 format:format block:^(AVAudioPCMBuffer* buffer, AVAudioTime*) { [request appendAudioPCMBuffer:buffer]; }];
  NSError* err = nil;
  if (![self.engine startAndReturnError:&err]) {
    *error = err.localizedDescription.UTF8String ?: "the microphone did not start";
    [input removeTapOnBus:0];
    return NO;
  }
  const std::string target = phrase.UTF8String ?: "";
  __block bool fired = false;
  self.task = [self.recognizer recognitionTaskWithRequest:request
                                            resultHandler:^(SFSpeechRecognitionResult* result, NSError*) {
                                              if (!result || fired) return;
                                              const std::string text = result.bestTranscription.formattedString.UTF8String ?: "";
                                              if (voice::wakeMatch(text, target)) {
                                                fired = true;
                                                const std::string after = voice::afterWake(text, target);
                                                heard([NSString stringWithUTF8String:after.c_str()]);
                                              }
                                            }];
  self.startedAt = [NSProcessInfo processInfo].systemUptime;
  return YES;
}

- (void)stop {
  [self.task cancel];
  self.task = nil;
  [self.request endAudio];
  self.request = nil;
  if (self.engine) {
    [self.engine.inputNode removeTapOnBus:0];
    [self.engine stop];
  }
  self.engine = nil;
}

@end

namespace app {
namespace {

voice::PushToTalk gPtt;
bool gPttRecording = false;
OWUIWakeListener* gWake = nil;
bool gWakeOneShot = false;      // a wake phrase started this voice turn
double gWakeRestartAt = 0;
std::string gWakeError;
constexpr int kSpaceKeyCode = 49;

const char* micAccess() {
  switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]) {
    case AVAuthorizationStatusAuthorized: return "allowed";
    case AVAuthorizationStatusDenied: return "denied";
    case AVAuthorizationStatusRestricted: return "restricted";
    default: return "not asked yet";
  }
}

const char* speechAccess() {
  switch ([SFSpeechRecognizer authorizationStatus]) {
    case SFSpeechRecognizerAuthorizationStatusAuthorized: return "allowed";
    case SFSpeechRecognizerAuthorizationStatusDenied: return "denied";
    case SFSpeechRecognizerAuthorizationStatusRestricted: return "restricted";
    default: return "not asked yet";
  }
}

bool micAllowed() { return platform::fakeMicrophone() || [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio] == AVAuthorizationStatusAuthorized; }

}  // namespace

voice::VadOptions App::vadOptions() const {
  voice::VadOptions o;
  const Json& extra = core::Settings::instance().extra;
  o.marginDb = std::clamp(extra.value("vadMarginDb", 12.0), 6.0, 24.0);
  o.hangoverMs = std::clamp(extra.value("vadPauseMs", 1100.0), 500.0, 3000.0);
  return o;
}

void App::applyPushToTalk() {
  if (secondaryInstance) return;
  if (settings().extra.value("pushToTalk", false)) {
    if (!native::registerPushToTalkHotkey(kSpaceKeyCode, false, true, true, false)) {
      widgets::toastInfo("Control-Option-Space is taken by another app; push-to-talk is off");
    }
  } else {
    native::unregisterPushToTalkHotkey();
  }
}

void App::onPushToTalk(bool down) {
  const double now = core::nowMillis();
  if (down) {
    if (!gPtt.press(now)) return;   // key repeat while held
    if (voiceMode || platform::recording()) {
      gPtt.down = false;
      return;
    }
    if (gWake) [gWake stop];   // one user of the microphone at a time
    if (!platform::startRecording()) {
      gPtt.down = false;
      widgets::toastError("The microphone is not available");
      return;
    }
    gPttRecording = true;
    widgets::toastInfo("Listening - let go to send");
    return;
  }
  if (!gPttRecording) {
    gPtt.down = false;
    return;
  }
  gPttRecording = false;
  const std::string wav = platform::stopRecording();
  if (!gPtt.release(now) || wav.size() < 2000) {
    widgets::toastInfo("Too short - hold the keys while you talk");
    return;
  }
  const bool compose = settings().extra.value("pushToTalkCompose", false);
  async::run([this, wav, compose] {
    api::Result res = api_.transcribe("push-to-talk.wav", "audio/wav", wav);
    const std::string text = res.ok ? core::trim(res.data.value("text", "")) : "";
    const std::string error = res.ok ? "" : api::errorMessage(res);
    async::post([this, text, error, compose] {
      if (!error.empty()) {
        widgets::toastError("Speech-to-text: " + error);
        return;
      }
      if (text.empty()) {
        widgets::toastInfo("Nothing was heard");
        return;
      }
      pane = Pane::Chat;
      if (compose) {
        composer = composer.empty() ? text : composer + " " + text;
        focusComposer = true;
        widgets::toastInfo("In the message field: " + text);
      } else {
        sendMessage(text);
        widgets::toastInfo("Sent: " + text);
      }
    });
  });
}

void App::onWakeHeard(const std::string& after) {
  if (gWake) [gWake stop];
  gWakeRestartAt = core::nowMillis() + 1500.0;
  // "Hey Open WebUI, what's the weather" carries its question; else listen for one.
  if (core::split(after, ' ').size() >= 2) {
    pane = Pane::Chat;
    sendMessage(after);
    widgets::toastInfo("Heard: " + after);
    return;
  }
  widgets::toastInfo("Listening...");
  gWakeOneShot = true;
  voiceMode = true;
}

bool App::wakeTurnActive() const { return gWakeOneShot; }

void App::tickWakeWord() {
  // A turn the wake phrase started ends voice mode again once it is spoken.
  if (gWakeOneShot && ((voiceMode && voiceState == 0 && voiceTurns_ > 0) || !voiceMode)) {
    voiceMode = false;
    gWakeOneShot = false;
    voiceTurns_ = 0;
  }
  const Json& extra = settings().extra;
  const bool want = extra.value("wakeWord", false) && !voiceMode && !gPttRecording && !platform::recording() && !ttsActive() && micAllowed() &&
                    [SFSpeechRecognizer authorizationStatus] == SFSpeechRecognizerAuthorizationStatusAuthorized && !platform::fakeMicrophone();
  const double now = core::nowMillis();
  if (!want) {
    if (gWake && gWake.engine) [gWake stop];
    return;
  }
  // Recognition tasks run about a minute: start one, and a fresh one every 50 s.
  const bool running = gWake && gWake.engine;
  if (running && [NSProcessInfo processInfo].systemUptime - gWake.startedAt < 50.0) return;
  if (now < gWakeRestartAt) return;
  if (!gWake) gWake = [[OWUIWakeListener alloc] init];
  std::string error;
  NSString* phrase = [NSString stringWithUTF8String:extra.value("wakePhrase", std::string("hey open webui")).c_str()];
  if (![gWake startWithPhrase:phrase
                      onHeard:^(NSString* after) {
                        const std::string text = after.UTF8String ?: "";
                        async::postMain([this, text] { onWakeHeard(text); });
                      }
                        error:&error]) {
    gWakeError = error;
    gWakeRestartAt = now + 30000.0;   // try again later
  } else {
    gWakeError.clear();
  }
}

void App::drawVoiceInputSection(float width) {
  (void)width;
  core::Settings& cfg = settings();
  widgets::dimText("Microphone: %s%s", micAccess(), platform::fakeMicrophone() ? "  (test microphone in use: OWUI_FAKE_MIC)" : "");
  if ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio] == AVAuthorizationStatusNotDetermined) {
    ImGui::SameLine();
    if (ImGui::SmallButton("Allow microphone access")) {
      [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio completionHandler:^(BOOL) {}];
    }
  }
  bool ptt = cfg.extra.value("pushToTalk", false);
  if (ImGui::Checkbox("Hold Control-Option-Space to talk, from any app; let go to send", &ptt)) {
    cfg.extra["pushToTalk"] = ptt;
    cfg.save();
    applyPushToTalk();
  }
  int target = cfg.extra.value("pushToTalkCompose", false) ? 1 : 0;
  ImGui::Indent(24.0f);
  if (ImGui::RadioButton("send what you said", target == 0)) target = 0;
  ImGui::SameLine();
  if (ImGui::RadioButton("put it in the message field", target == 1)) target = 1;
  if ((target == 1) != cfg.extra.value("pushToTalkCompose", false)) {
    cfg.extra["pushToTalkCompose"] = target == 1;
    cfg.save();
  }
  ImGui::Unindent(24.0f);

  widgets::dimText("%s", "Voice conversation and the wake phrase end your turn when you stop talking:");
  float pause = static_cast<float>(cfg.extra.value("vadPauseMs", 1100.0) / 1000.0);
  ImGui::SetNextItemWidth(220.0f);
  if (ImGui::SliderFloat("pause that ends a turn (s)", &pause, 0.5f, 3.0f, "%.1f")) {
    cfg.extra["vadPauseMs"] = pause * 1000.0;
    cfg.save();
  }
  float margin = static_cast<float>(cfg.extra.value("vadMarginDb", 12.0));
  ImGui::SetNextItemWidth(220.0f);
  if (ImGui::SliderFloat("how far above the room's noise speech is (dB)", &margin, 6.0f, 24.0f, "%.0f")) {
    cfg.extra["vadMarginDb"] = margin;
    cfg.save();
  }

  bool wake = cfg.extra.value("wakeWord", false);
  if (ImGui::Checkbox("Listen for a wake phrase (recognised on this Mac; nothing leaves it until the phrase is heard)", &wake)) {
    cfg.extra["wakeWord"] = wake;
    cfg.save();
  }
  std::string phrase = cfg.extra.value("wakePhrase", std::string("hey open webui"));
  ImGui::Indent(24.0f);
  ImGui::SetNextItemWidth(280.0f);
  if (ImGui::InputText("wake phrase", &phrase)) {
    cfg.extra["wakePhrase"] = phrase;
    cfg.save();
  }
  widgets::dimText("Speech recognition: %s", speechAccess());
  if ([SFSpeechRecognizer authorizationStatus] == SFSpeechRecognizerAuthorizationStatusNotDetermined) {
    ImGui::SameLine();
    if (ImGui::SmallButton("Allow speech recognition")) [SFSpeechRecognizer requestAuthorization:^(SFSpeechRecognizerAuthorizationStatus) {}];
  }
  if (wake) widgets::dimText("%s", !gWakeError.empty() ? gWakeError.c_str() : (gWake && gWake.engine) ? "Listening for the phrase" : "Waiting (access, or the microphone is busy)");
  ImGui::Unindent(24.0f);
}

}  // namespace app
