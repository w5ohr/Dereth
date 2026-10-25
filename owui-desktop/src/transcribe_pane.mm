// Transcribe tab (#114): open or drop an audio file; it is decoded on this
// Mac, split at pauses, and each piece is sent to the server's speech-to-text
// in turn, so every line has a time. Click a line to play from there; the
// line being heard is highlighted. Copy, export SRT / VTT / Markdown, or send
// the transcript to a chat. Transcripts are kept per file (by content hash).
#import <AVFoundation/AVFoundation.h>

#include <atomic>
#include <filesystem>

#include "app.h"
#include "core/async.h"
#include "core/common.h"
#include "core/transcript.h"
#include "core/updater.h"
#include "imgui_stdlib.h"
#include "pane_common.h"
#include "platform.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace fs = std::filesystem;

namespace app {
namespace {

struct State {
  std::string path, name, hash;
  double duration = 0.0;
  std::vector<transcript::Line> lines;
  int total = 0, done = 0;
  bool decoding = false, running = false;
  std::string error;
  std::atomic<bool> cancel{false};
};
State gS;
AVAudioPlayer* gPlayer = nil;
int gLastDrawFrame = -100;
int gFollow = -1;   // the line the view last scrolled to

std::string storeDir() { return core::supportDir() + "/transcripts"; }

Json linesToJson(const std::vector<transcript::Line>& lines) {
  Json a = Json::array();
  for (const auto& l : lines) a.push_back(Json{{"start", l.start}, {"end", l.end}, {"text", l.text}, {"error", l.error}});
  return a;
}

void save() {
  if (gS.hash.empty() || gS.lines.empty()) return;
  std::error_code ec;
  fs::create_directories(storeDir(), ec);
  core::writeFile(storeDir() + "/" + gS.hash + ".json", Json{{"name", gS.name}, {"duration", gS.duration}, {"lines", linesToJson(gS.lines)}}.dump());
}

bool loadSaved() {
  bool ok = false;
  const Json j = Json::parse(core::readFile(storeDir() + "/" + gS.hash + ".json", &ok), nullptr, false);
  if (!ok || !j.is_object()) return false;
  gS.lines.clear();
  for (const auto& l : j.value("lines", Json::array())) gS.lines.push_back(transcript::Line{l.value("start", 0.0), l.value("end", 0.0), l.value("text", ""), l.value("error", "")});
  gS.total = gS.done = static_cast<int>(gS.lines.size());
  return !gS.lines.empty();
}

void openPlayer(const std::string& path) {
  NSError* err = nil;
  gPlayer = [[AVAudioPlayer alloc] initWithContentsOfURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]] error:&err];
  [gPlayer prepareToPlay];
}

}  // namespace

bool App::transcribeTabVisible() const { return ImGui::GetFrameCount() - gLastDrawFrame < 3; }

void App::openAudioForTranscript(const std::string& path, bool force) {
  if (gS.running || gS.decoding) {
    widgets::toastInfo("A transcription is already running");
    return;
  }
  [gPlayer stop];
  gPlayer = nil;
  gS.path = path;
  gS.name = fs::path(path).filename().string();
  gS.lines.clear();
  gS.error.clear();
  gS.total = gS.done = 0;
  gS.duration = 0;
  gS.cancel = false;
  gS.hash = updater::sha256File(path);
  openPlayer(path);
  gS.duration = gPlayer ? gPlayer.duration : 0.0;
  if (!force && loadSaved()) {
    widgets::toastInfo("Opened the saved transcript of " + gS.name);
    return;
  }
  gS.decoding = true;
  gS.running = true;
  async::run([this, path] {
    std::string error;
    const transcript::Pcm pcm = transcript::decodeAudio(path, 16000, &error);
    const auto spans = error.empty() ? transcript::segment(pcm) : std::vector<transcript::Span>{};
    async::post([spans, error, dur = pcm.duration()] {
      gS.decoding = false;
      if (!error.empty() || spans.empty()) {
        gS.running = false;
        gS.error = !error.empty() ? error : "no speech found in the file";
        return;
      }
      gS.duration = dur;
      gS.total = static_cast<int>(spans.size());
      for (const auto& sp : spans) gS.lines.push_back(transcript::Line{sp.start, sp.end, "", ""});
    });
    if (!error.empty()) return;
    for (size_t i = 0; i < spans.size(); ++i) {
      if (gS.cancel) break;
      const std::string wav = transcript::wav16(pcm, spans[i].start, spans[i].end);
      api::Result r = api_.transcribe("segment-" + std::to_string(i) + ".wav", "audio/wav", wav);
      std::string text, err;
      if (r.ok) text = transcript::cleanText(r.data.value("text", ""));
      else err = api::errorMessage(r);
      async::post([i, text, err] {
        if (i < gS.lines.size()) {
          gS.lines[i].text = text;
          gS.lines[i].error = err;
        }
        ++gS.done;
      });
    }
    async::post([] {
      gS.running = false;
      save();
    });
  });
}

void App::openTranscribeTab() {
  pane = Pane::Tools;
  toolsTabRequest = "transcribe";
}

void App::drawTranscribeSection(float width) {
  const auto& palette = theme::palette();
  gLastDrawFrame = ImGui::GetFrameCount();
  ImGui::PushTextWrapPos(width - 20.0f);
  widgets::dimText("Drop an audio file here (wav, m4a, mp3, aiff, flac, mp4 audio) or open one. It is decoded on this Mac, split at pauses, and each piece goes to the server's speech-to-text, so every line has its time. Click a line to play from there.");
  ImGui::PopTextWrapPos();
  widgets::beginDisabled(gS.running);
  if (ImGui::Button("Open audio...")) {
    const auto picked = platform::openFiles(false);
    if (!picked.empty()) openAudioForTranscript(picked.front(), false);
  }
  widgets::endDisabled(gS.running);
  if (!gS.path.empty()) {
    ImGui::SameLine();
    if (gS.running) {
      if (ImGui::Button("Stop")) gS.cancel = true;
    } else if (ImGui::Button("Transcribe again")) {
      openAudioForTranscript(gS.path, true);
    }
  }
  if (gS.path.empty()) {
    if (!gS.error.empty()) ImGui::TextColored(theme::toVec4(palette.danger), "%s", gS.error.c_str());
    return;
  }
  ImGui::SameLine();
  theme::pushBold();
  ImGui::TextUnformatted(gS.name.c_str());
  theme::pop();
  ImGui::SameLine();
  widgets::dimText("%s", transcript::clock(gS.duration).c_str());
  if (!gS.error.empty()) ImGui::TextColored(theme::toVec4(palette.danger), "%s", gS.error.c_str());
  if (gS.decoding) widgets::loadingRow("decoding and finding the pauses...");
  else if (gS.running) widgets::loadingRow(("transcribing piece " + std::to_string(std::min(gS.done + 1, gS.total)) + " of " + std::to_string(gS.total) + "...").c_str());

  // Player.
  const double now = gPlayer ? gPlayer.currentTime : 0.0;
  const bool playing = gPlayer && gPlayer.isPlaying;
  if (gPlayer) {
    if (ImGui::Button(playing ? "Pause" : "Play", ImVec2(70, 0))) {
      if (playing) [gPlayer pause];
      else [gPlayer play];
    }
    ImGui::SameLine();
    float pos = static_cast<float>(now);
    ImGui::SetNextItemWidth(width - 260.0f);
    if (ImGui::SliderFloat("##seek", &pos, 0.0f, static_cast<float>(std::max(0.1, gPlayer.duration)), transcript::clock(now).c_str())) gPlayer.currentTime = pos;
    ImGui::SameLine();
    widgets::dimText("%s / %s", transcript::clock(now).c_str(), transcript::clock(gPlayer.duration).c_str());
  }

  // Transcript.
  const int current = playing || now > 0.01 ? transcript::lineAt(gS.lines, now) : -1;
  ImGui::BeginChild("##transcript", ImVec2(width - 20.0f, std::max(220.0f, ImGui::GetContentRegionAvail().y - 44.0f)), ImGuiChildFlags_Borders);
  for (size_t i = 0; i < gS.lines.size(); ++i) {
    const transcript::Line& l = gS.lines[i];
    ImGui::PushID(static_cast<int>(i));
    const bool here = static_cast<int>(i) == current;
    theme::pushMono();
    ImGui::TextColored(theme::toVec4(here ? palette.accent : palette.textFaint), "%7s", transcript::clock(l.start).c_str());
    theme::pop();
    ImGui::SameLine();
    const std::string text = !l.error.empty() ? "(could not transcribe: " + l.error + ")" : l.text.empty() ? (gS.running ? "..." : "(no words)") : l.text;
    // A full-width selectable for the click, with the wrapped text drawn over it.
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float textW = ImGui::GetContentRegionAvail().x - 8.0f;
    const float h = ImGui::CalcTextSize(text.c_str(), nullptr, false, textW).y;
    if (here) ImGui::PushStyleColor(ImGuiCol_Header, theme::toVec4(palette.panelAlt));
    if (ImGui::Selectable(("##line" + std::to_string(i)).c_str(), here, ImGuiSelectableFlags_AllowOverlap, ImVec2(textW, h))) {
      if (gPlayer) {
        gPlayer.currentTime = l.start;
        [gPlayer play];
      }
    }
    if (here) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play from %s", transcript::clock(l.start).c_str());
    ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(), at, !l.error.empty() ? palette.danger : l.text.empty() ? palette.textDim : palette.text, text.c_str(), nullptr, textW);
    if (here && playing && gFollow != current) {
      ImGui::SetScrollHereY(0.35f);
      gFollow = current;
    }
    ImGui::PopID();
  }
  ImGui::EndChild();

  // Exports.
  const bool any = !transcript::plainText(gS.lines).empty();
  widgets::beginDisabled(!any || gS.running);
  if (ImGui::Button("Copy text")) {
    platform::setClipboardText(transcript::toMarkdown(gS.lines, gS.name));
    widgets::toast(widgets::ToastLevel::Success, "Transcript copied");
  }
  ImGui::SameLine();
  const std::string stem = fs::path(gS.name).stem().string();
  if (ImGui::Button("Save .srt...")) {
    const std::string p = platform::saveFile(stem + ".srt");
    if (!p.empty() && core::writeFile(p, transcript::toSrt(gS.lines))) widgets::toast(widgets::ToastLevel::Success, "Saved " + p);
  }
  ImGui::SameLine();
  if (ImGui::Button("Save .vtt...")) {
    const std::string p = platform::saveFile(stem + ".vtt");
    if (!p.empty() && core::writeFile(p, transcript::toVtt(gS.lines))) widgets::toast(widgets::ToastLevel::Success, "Saved " + p);
  }
  ImGui::SameLine();
  if (ImGui::Button("Save .md...")) {
    const std::string p = platform::saveFile(stem + ".md");
    if (!p.empty() && core::writeFile(p, transcript::toMarkdown(gS.lines, gS.name))) widgets::toast(widgets::ToastLevel::Success, "Saved " + p);
  }
  ImGui::SameLine();
  if (widgets::primaryButton("Send to chat", ImVec2(120, 0))) {
    composer = "Transcript of " + gS.name + ":\n\n" + transcript::toMarkdown(gS.lines, gS.name);
    pane = Pane::Chat;
    focusComposer = true;
  }
  widgets::endDisabled(!any || gS.running);
}

}  // namespace app
