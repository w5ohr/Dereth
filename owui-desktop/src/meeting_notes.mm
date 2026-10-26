// Meeting notes (#115), the system side and the Tools tab: system audio
// through ScreenCaptureKit (this app's own sound left out), the microphone
// through AVAudioEngine when asked, a live transcript from the server's
// speech-to-text, and notes the model keeps up to date. A recording file
// goes through the same transcript and notes. Capture starts only from Start.
#import <AVFoundation/AVFoundation.h>
#import <Cocoa/Cocoa.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <atomic>
#include <ctime>
#include <deque>
#include <mutex>

#include "app.h"
#include "core/async.h"
#include "core/common.h"
#include "core/meeting.h"
#include "core/settings.h"
#include "core/transcript.h"
#include "imgui_stdlib.h"
#include "ui/a11y_imgui.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace meetingaudio {

std::mutex gMutex;
std::vector<float> gSystem;   // 16 kHz mono, waiting for the main thread
std::vector<float> gMic;
std::string gError;

void push(std::vector<float>* into, const std::vector<float>& samples) {
  std::lock_guard<std::mutex> lock(gMutex);
  into->insert(into->end(), samples.begin(), samples.end());
}

// Mono floats from a sample buffer, planar or interleaved.
std::vector<float> monoOf(CMSampleBufferRef buffer, int* rate) {
  std::vector<float> out;
  CMFormatDescriptionRef format = CMSampleBufferGetFormatDescription(buffer);
  const AudioStreamBasicDescription* asbd = format ? CMAudioFormatDescriptionGetStreamBasicDescription(format) : nullptr;
  if (!asbd || !(asbd->mFormatFlags & kAudioFormatFlagIsFloat) || asbd->mBitsPerChannel != 32) return out;
  *rate = static_cast<int>(asbd->mSampleRate);
  AudioBufferList list;
  CMBlockBufferRef block = nullptr;
  size_t needed = 0;
  CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(buffer, &needed, nullptr, 0, nullptr, nullptr, 0, nullptr);
  std::vector<uint8_t> storage(needed);
  AudioBufferList* abl = reinterpret_cast<AudioBufferList*>(storage.data());
  if (CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(buffer, nullptr, abl, needed, nullptr, nullptr,
                                                              kCMSampleBufferFlag_AudioBufferList_Assure16ByteAlignment, &block) != noErr) {
    return out;
  }
  (void)list;
  const bool planar = asbd->mFormatFlags & kAudioFormatFlagIsNonInterleaved;
  const UInt32 channels = std::max<UInt32>(1, asbd->mChannelsPerFrame);
  if (planar) {
    const size_t frames = abl->mBuffers[0].mDataByteSize / sizeof(float);
    out.assign(frames, 0.0f);
    for (UInt32 b = 0; b < abl->mNumberBuffers; ++b) {
      const float* d = static_cast<const float*>(abl->mBuffers[b].mData);
      for (size_t i = 0; i < frames && d; ++i) out[i] += d[i] / static_cast<float>(abl->mNumberBuffers);
    }
  } else {
    const float* d = static_cast<const float*>(abl->mBuffers[0].mData);
    const size_t frames = abl->mBuffers[0].mDataByteSize / sizeof(float) / channels;
    out.assign(frames, 0.0f);
    for (size_t i = 0; i < frames; ++i) {
      for (UInt32 c = 0; c < channels; ++c) out[i] += d[i * channels + c] / static_cast<float>(channels);
    }
  }
  if (block) CFRelease(block);
  return out;
}

}  // namespace meetingaudio

API_AVAILABLE(macos(13.0))
@interface OWUISystemAudio : NSObject <SCStreamOutput, SCStreamDelegate>
@property(nonatomic, strong) SCStream* stream;
@property(nonatomic, strong) dispatch_queue_t queue;
- (void)startWithCompletion:(void (^)(NSString* error))done;
- (void)stop;
@end

@implementation OWUISystemAudio {
  meeting::Resampler* _resampler;
}

- (void)startWithCompletion:(void (^)(NSString*))done {
  [SCShareableContent getShareableContentWithCompletionHandler:^(SCShareableContent* content, NSError* error) {
    SCDisplay* display = content.displays.firstObject;
    if (!display) {
      done(error ? error.localizedDescription : @"no display to capture audio from (Screen Recording off?)");
      return;
    }
    SCContentFilter* filter = [[SCContentFilter alloc] initWithDisplay:display excludingApplications:@[] exceptingWindows:@[]];
    SCStreamConfiguration* config = [[SCStreamConfiguration alloc] init];
    config.capturesAudio = YES;
    config.excludesCurrentProcessAudio = YES;   // not our own read-aloud
    config.sampleRate = 48000;
    config.channelCount = 2;
    config.width = 2;   // the video is not wanted; keep it tiny and slow
    config.height = 2;
    config.minimumFrameInterval = CMTimeMake(1, 1);
    self.queue = dispatch_queue_create("owui.meeting.audio", DISPATCH_QUEUE_SERIAL);
    delete self->_resampler;
    self->_resampler = new meeting::Resampler(48000, 16000);
    SCStream* stream = [[SCStream alloc] initWithFilter:filter configuration:config delegate:self];
    NSError* addError = nil;
    [stream addStreamOutput:self type:SCStreamOutputTypeAudio sampleHandlerQueue:self.queue error:&addError];
    [stream addStreamOutput:self type:SCStreamOutputTypeScreen sampleHandlerQueue:self.queue error:nil];
    if (addError) {
      done(addError.localizedDescription);
      return;
    }
    self.stream = stream;
    [stream startCaptureWithCompletionHandler:^(NSError* startError) {
      done(startError ? startError.localizedDescription : nil);
    }];
  }];
}

- (void)stream:(SCStream*)stream didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer ofType:(SCStreamOutputType)type {
  (void)stream;
  if (type != SCStreamOutputTypeAudio || !CMSampleBufferIsValid(sampleBuffer)) return;
  int rate = 48000;
  const std::vector<float> mono = meetingaudio::monoOf(sampleBuffer, &rate);
  if (mono.empty() || !_resampler) return;
  if (_resampler->inRate() != rate) {
    delete _resampler;
    _resampler = new meeting::Resampler(rate, 16000);
  }
  std::vector<float> out;
  _resampler->feed(mono.data(), mono.size(), 1, &out);
  meetingaudio::push(&meetingaudio::gSystem, out);
}

- (void)stream:(SCStream*)stream didStopWithError:(NSError*)error {
  (void)stream;
  std::lock_guard<std::mutex> lock(meetingaudio::gMutex);
  meetingaudio::gError = error.localizedDescription.UTF8String ?: "the audio capture stopped";
}

- (void)stop {
  [self.stream stopCaptureWithCompletionHandler:nil];
  self.stream = nil;
}

- (void)dealloc {
  delete _resampler;
}

@end

namespace app {
namespace {

enum class Phase { Idle, Live, Finishing, File };

struct State {
  Phase phase = Phase::Idle;
  std::string title;
  double startedAt = 0;
  meeting::Chunker chunker{15.0, 30.0};
  std::deque<meeting::Chunk> pending;     // waiting for speech-to-text, in order
  bool transcribing = false;
  std::vector<transcript::Line> lines;
  meeting::Notes notes;
  bool notesBusy = false;
  double notesAt = 0;
  size_t charsAtNotes = 0;
  std::string status;
  bool mic = false;
  double fileSeconds = 0;
};
State gM;
id gSystemAudio = nil;
AVAudioEngine* gEngine = nil;
meeting::Resampler* gMicResampler = nullptr;
std::string gModel;
int gSource = 0;   // 0 system audio, 1 a recording

size_t transcriptChars() {
  size_t n = 0;
  for (const transcript::Line& l : gM.lines) n += l.text.size();
  return n;
}

void stopCapture() {
  if (@available(macOS 13.0, *)) {
    if (gSystemAudio) [(OWUISystemAudio*)gSystemAudio stop];
  }
  gSystemAudio = nil;
  if (gEngine) {
    [gEngine.inputNode removeTapOnBus:0];
    [gEngine stop];
    gEngine = nil;
  }
}

bool startMic(std::string* error) {
  gEngine = [[AVAudioEngine alloc] init];
  AVAudioInputNode* input = gEngine.inputNode;
  AVAudioFormat* format = [input outputFormatForBus:0];
  if (format.sampleRate <= 0) {
    *error = "no microphone";
    gEngine = nil;
    return false;
  }
  delete gMicResampler;
  gMicResampler = new meeting::Resampler(static_cast<int>(format.sampleRate), 16000);
  [input installTapOnBus:0
              bufferSize:4096
                  format:format
                   block:^(AVAudioPCMBuffer* buffer, AVAudioTime*) {
                     const AVAudioFrameCount frames = buffer.frameLength;
                     const AVAudioChannelCount channels = buffer.format.channelCount;
                     if (!buffer.floatChannelData || frames == 0) return;
                     std::vector<float> mono(frames, 0.0f);
                     for (AVAudioChannelCount c = 0; c < channels; ++c) {
                       for (AVAudioFrameCount i = 0; i < frames; ++i) mono[i] += buffer.floatChannelData[c][i] / static_cast<float>(channels);
                     }
                     std::vector<float> out;
                     gMicResampler->feed(mono.data(), mono.size(), 1, &out);
                     meetingaudio::push(&meetingaudio::gMic, out);
                   }];
  NSError* err = nil;
  if (![gEngine startAndReturnError:&err]) {
    *error = err.localizedDescription.UTF8String ?: "the microphone did not start";
    [input removeTapOnBus:0];
    gEngine = nil;
    return false;
  }
  return true;
}

}  // namespace

void App::startMeetingCapture(const std::string& title, bool withMic) {
  if (gM.phase != Phase::Idle) return;
  if (@available(macOS 13.0, *)) {
  } else {
    widgets::toastError("System audio capture needs macOS 13");
    return;
  }
  gM = State{};
  gM.title = title;
  gM.startedAt = static_cast<double>(std::time(nullptr));
  gM.phase = Phase::Live;
  gM.mic = withMic;
  gM.status = "Starting...";
  {
    std::lock_guard<std::mutex> lock(meetingaudio::gMutex);
    meetingaudio::gSystem.clear();
    meetingaudio::gMic.clear();
    meetingaudio::gError.clear();
  }
  if (withMic) {
    std::string error;
    if (!startMic(&error)) widgets::toastError("Microphone: " + error);
  }
  if (@available(macOS 13.0, *)) {
    OWUISystemAudio* audio = [[OWUISystemAudio alloc] init];
    gSystemAudio = audio;
    [audio startWithCompletion:^(NSString* error) {
      std::string message = error ? std::string(error.UTF8String) : "";
      async::postMain([message] {
        if (!message.empty()) {
          gM.status = "System audio: " + message;
          widgets::toastError(gM.status);
          if (!gEngine) {
            stopCapture();
            gM.phase = Phase::Idle;
          }
        } else {
          gM.status = "Listening";
        }
      });
    }];
  }
}

void App::stopMeetingCapture() {
  if (gM.phase != Phase::Live) return;
  stopCapture();
  gM.phase = Phase::Finishing;
  gM.status = "Finishing the transcript...";
}

void App::transcribeMeetingFile(const std::string& path, const std::string& title) {
  if (gM.phase != Phase::Idle) return;
  gM = State{};
  gM.title = title.empty() ? path.substr(path.rfind('/') + 1) : title;
  gM.startedAt = static_cast<double>(std::time(nullptr));
  gM.phase = Phase::File;
  gM.status = "Reading the recording...";
  async::run([path] {
    std::string error;
    transcript::Pcm pcm = transcript::decodeAudio(path, 16000, &error);
    const std::vector<transcript::Span> spans = error.empty() ? transcript::segment(pcm) : std::vector<transcript::Span>{};
    async::post([pcm, spans, error] {
      if (!error.empty()) {
        gM.status = "Could not read it: " + error;
        gM.phase = Phase::Idle;
        return;
      }
      gM.fileSeconds = pcm.duration();
      for (const transcript::Span& s : spans) {
        meeting::Chunk c;
        c.start = s.start;
        c.pcm.rate = pcm.rate;
        const size_t a = static_cast<size_t>(s.start * pcm.rate), b = std::min(pcm.samples.size(), static_cast<size_t>(s.end * pcm.rate));
        if (b > a) c.pcm.samples.assign(pcm.samples.begin() + static_cast<long>(a), pcm.samples.begin() + static_cast<long>(b));
        gM.pending.push_back(std::move(c));
      }
      gM.status = std::to_string(spans.size()) + " parts to transcribe";
      gM.phase = Phase::Finishing;   // no more audio is coming: transcribe, then the final notes
    });
  });
}

void App::updateMeetingNotes(bool final) {
  if (gM.notesBusy || gM.lines.empty()) return;
  const std::string model = !gModel.empty() ? gModel : (!selectedModels.empty() ? selectedModels.front() : "");
  if (model.empty()) {
    gM.status = "Choose a model for the notes";
    return;
  }
  gM.notesBusy = true;
  gM.notesAt = core::nowMillis() / 1000.0;
  gM.charsAtNotes = transcriptChars();
  const Json payload{{"model", model}, {"stream", false},
                     {"messages", meeting::notesMessages(gM.title, gM.notes, transcript::plainText(gM.lines), final)}};
  async::run([this, payload, final] {
    api::Result res = api_.completion(payload);
    std::string reply, error;
    if (res.ok && res.data.contains("choices") && res.data["choices"].is_array() && !res.data["choices"].empty()) {
      reply = res.data["choices"][0].value("message", Json::object()).value("content", "");
    } else {
      error = api::errorMessage(res);
    }
    async::post([reply, error, final] {
      gM.notesBusy = false;
      if (!error.empty()) {
        gM.status = "Notes: " + error;
        return;
      }
      meeting::Notes notes = meeting::parseNotes(reply);
      if (!notes.empty()) gM.notes = notes;
      gM.status = final ? "Done - notes are final" : "Notes updated";
      if (final) widgets::toastInfo("Meeting notes are ready");
    });
  });
}

void App::tickMeeting() {
  if (gM.phase == Phase::Idle) return;
  // Live audio into the chunker: the system mix plus the microphone, aligned by arrival.
  if (gM.phase == Phase::Live) {
    std::vector<float> system, mic;
    std::string error;
    {
      std::lock_guard<std::mutex> lock(meetingaudio::gMutex);
      system.swap(meetingaudio::gSystem);
      mic.swap(meetingaudio::gMic);
      error.swap(meetingaudio::gError);
    }
    if (!error.empty()) gM.status = "System audio: " + error;
    if (gM.mic) {
      const size_t n = std::max(system.size(), mic.size());
      system.resize(n, 0.0f);
      for (size_t i = 0; i < mic.size(); ++i) system[i] = std::clamp(system[i] + mic[i], -1.0f, 1.0f);
    }
    gM.chunker.feed(system);
    meeting::Chunk chunk;
    while (gM.chunker.next(&chunk)) gM.pending.push_back(std::move(chunk));
  } else if (gM.phase == Phase::Finishing) {
    meeting::Chunk chunk;   // after Stop: what the chunker still holds
    while (gM.chunker.flush(&chunk)) gM.pending.push_back(std::move(chunk));
  }
  // One transcription at a time, so lines arrive in order.
  if (!gM.transcribing && !gM.pending.empty()) {
    gM.transcribing = true;
    meeting::Chunk chunk = std::move(gM.pending.front());
    gM.pending.pop_front();
    const size_t index = gM.lines.size();
    transcript::Line line;
    line.start = chunk.start;
    line.end = chunk.start + chunk.pcm.duration();
    gM.lines.push_back(line);
    const std::string wav = transcript::wav16(chunk.pcm, 0.0, chunk.pcm.duration());
    async::run([this, wav, index] {
      api::Result r = api_.transcribe("meeting-" + std::to_string(index) + ".wav", "audio/wav", wav);
      const std::string text = r.ok ? transcript::cleanText(r.data.value("text", "")) : "";
      const std::string error = r.ok ? "" : api::errorMessage(r);
      async::post([index, text, error] {
        gM.transcribing = false;
        if (index < gM.lines.size()) {
          gM.lines[index].text = text;
          gM.lines[index].error = error;
        }
      });
    });
  }
  const double now = core::nowMillis() / 1000.0;
  if (gM.phase == Phase::Live && meeting::notesDue(now - gM.notesAt, transcriptChars() - gM.charsAtNotes)) updateMeetingNotes(false);
  // All audio transcribed after Stop (or a file): the final notes, then idle.
  if (gM.phase == Phase::Finishing && gM.pending.empty() && !gM.transcribing) {
    if (!gM.notesBusy) {
      gM.phase = Phase::Idle;
      if (!gM.lines.empty()) updateMeetingNotes(true);
      else gM.status = "Nothing was said";
    }
  }
}

void App::drawMeetingSection(float width) {
  widgets::sectionHeader("MEETING NOTES");
  const float wrap = std::max(200.0f, width - 24.0f);
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
  widgets::dimText("%s", "Live notes from what your Mac plays - a call, a webinar, a video - with your microphone too if you like. "
                         "Speech goes to your Open WebUI server's speech-to-text in short pieces; every couple of minutes the model "
                         "updates a summary, the decisions and the action items. Or choose a recording to get the same notes from it.");
  ImGui::PopTextWrapPos();
  const bool busy = gM.phase != Phase::Idle;
  static std::string title;
  ImGui::SetNextItemWidth(std::max(200.0f, width - 140.0f));
  ImGui::InputTextWithHint("Title", "Weekly sync", &title);
  ImGui::RadioButton("System audio (what you hear)", &gSource, 0);
  ImGui::SameLine();
  ImGui::RadioButton("A recording", &gSource, 1);
  if (gModel.empty() && !selectedModels.empty()) gModel = selectedModels.front();
  ImGui::SetNextItemWidth(320.0f);
  if (ImGui::BeginCombo("Notes model", gModel.empty() ? "(choose)" : gModel.c_str())) {
    for (const ModelInfo& m : models) {
      if (ImGui::Selectable(m.id.c_str(), m.id == gModel)) gModel = m.id;
    }
    ImGui::EndCombo();
  }
  static bool withMic = false;
  if (gSource == 0) {
    const bool screen = CGPreflightScreenCaptureAccess();
    widgets::dimText("System audio needs Screen Recording: %s", screen ? "allowed" : "not allowed yet");
    if (!screen) {
      ImGui::SameLine();
      if (ImGui::SmallButton("Allow Screen Recording")) CGRequestScreenCaptureAccess();
    }
    ImGui::Checkbox("Also my microphone (macOS asks once)", &withMic);
    if (gM.phase == Phase::Live) {
      if (widgets::dangerButton("Stop")) stopMeetingCapture();
      ImGui::SameLine();
      widgets::dimText("%s  %s", transcript::clock(gM.chunker.elapsed()).c_str(), gM.mic ? "(system audio + microphone)" : "(system audio)");
    } else {
      widgets::beginDisabled(busy);
      if (widgets::primaryButton("Start")) startMeetingCapture(title, withMic);
      widgets::endDisabled(busy);
    }
  } else {
    widgets::beginDisabled(busy);
    if (widgets::primaryButton("Choose a recording...")) {
      NSOpenPanel* panel = [NSOpenPanel openPanel];
      panel.allowedContentTypes = @[ UTTypeAudio, UTTypeMovie ];
      if ([panel runModal] == NSModalResponseOK && panel.URL) transcribeMeetingFile(panel.URL.path.UTF8String, title);
    }
    widgets::endDisabled(busy);
  }
  if (!gM.status.empty()) {
    const size_t done = std::count_if(gM.lines.begin(), gM.lines.end(), [](const transcript::Line& l) { return !l.text.empty() || !l.error.empty(); });
    widgets::dimText("%s  -  %zu of %zu parts transcribed%s", gM.status.c_str(), done, gM.lines.size() + gM.pending.size(),
                     gM.notesBusy ? ", writing notes..." : "");
  }

  // The notes.
  ImGui::Dummy(ImVec2(0, 6));
  theme::pushBold();
  ImGui::TextUnformatted("Notes");
  theme::pop();
  a11y::ui::headingItem("Notes");
  ImGui::SameLine();
  widgets::beginDisabled(gM.notesBusy || gM.lines.empty());
  if (ImGui::SmallButton("Update notes now")) updateMeetingNotes(gM.phase == Phase::Idle);
  widgets::endDisabled(gM.notesBusy || gM.lines.empty());
  ImGui::SameLine();
  widgets::beginDisabled(gM.lines.empty());
  const std::string doc = meeting::document(gM.title.empty() ? title : gM.title, gM.startedAt, gM.fileSeconds > 0 ? gM.fileSeconds : gM.chunker.elapsed(), gM.notes, gM.lines);
  if (ImGui::SmallButton("Copy as Markdown")) {
    ImGui::SetClipboardText(doc.c_str());
    widgets::toastInfo("Copied the notes and transcript");
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Save as a note")) {
    const Json body{{"title", gM.title.empty() ? std::string("Meeting notes") : gM.title}, {"data", Json{{"content", Json{{"md", doc}}}}}, {"meta", Json::object()}};
    async::run([this, body] {
      api::Result r = api_.createNote(body);
      async::post([r] { r.ok ? widgets::toastInfo("Saved to Notes") : widgets::toastError("Could not save: " + api::errorMessage(r)); });
    });
  }
  widgets::endDisabled(gM.lines.empty());
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
  const std::string notes = gM.notes.empty() ? std::string("(The notes appear after a couple of minutes of speech.)") : meeting::notesMarkdown(gM.notes);
  ImGui::TextUnformatted(notes.c_str());
  a11y::ui::textItem(notes);
  ImGui::PopTextWrapPos();

  // The transcript, newest at the bottom.
  ImGui::Dummy(ImVec2(0, 6));
  theme::pushBold();
  ImGui::TextUnformatted("Transcript");
  theme::pop();
  a11y::ui::headingItem("Transcript");
  ImGui::BeginChild("##meetingtranscript", ImVec2(wrap, 240), ImGuiChildFlags_Borders);
  for (const transcript::Line& l : gM.lines) {
    const std::string text = !l.error.empty() ? "(" + l.error + ")" : l.text.empty() ? "..." : l.text;
    widgets::dimText("%s", transcript::clock(l.start).c_str());
    ImGui::SameLine();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap - 80);
    ImGui::TextUnformatted(text.c_str());
    a11y::ui::textItem(transcript::clock(l.start) + " " + text);
    ImGui::PopTextWrapPos();
  }
  if (gM.phase == Phase::Live && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20) ImGui::SetScrollHereY(1.0f);
  ImGui::EndChild();
}

}  // namespace app
