// Local text-to-speech fallback in the app (#118): the read-aloud queue, the
// choice between the server's TTS and this Mac's voices, the per-message
// Read aloud / Stop button, and the Settings card.
#include "app.h"
#include "core/async.h"
#include "core/common.h"
#include "core/settings.h"
#include "core/tts.h"
#include "pane_common.h"
#include "platform.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

tts::Queue gQueue;
bool gBusy = false;              // a server speech request is in flight
int gGeneration = 0;             // bumps on stop so a late server answer is dropped
bool gServerMissing = false;     // the server failed a speech request this session
bool gToldFallback = false;
double gStartedAt = 0.0;
int gAdhoc = 0;
std::vector<tts::Voice> gLocalVoices;
bool gLocalVoicesLoaded = false;
Json gServerVoices = Json::array();
bool gServerVoicesLoaded = false;

tts::Prefs prefs() { return tts::fromJson(core::Settings::instance().extra); }

const std::vector<tts::Voice>& localVoices() {
  if (!gLocalVoicesLoaded) {
    gLocalVoicesLoaded = true;
    for (const platform::SpeechVoice& v : platform::speechVoices()) gLocalVoices.push_back(tts::Voice{v.id, v.name, v.language, v.quality});
  }
  return gLocalVoices;
}

void speakLocally(const tts::Item& item, const tts::Prefs& p) {
  const std::string voice = tts::pickVoice(localVoices(), p.localVoice, platform::speechLanguage());
  core::debugLog("tts: local voice '%s' part %d/%d (%zu chars)", voice.empty() ? "(system voice)" : voice.c_str(), item.part, item.parts, item.text.size());
  gStartedAt = core::nowMillis();
  platform::speakLocal(item.text, voice, tts::appleRate(p.rate), p.pitch);
}

}  // namespace

void App::speak(const std::string& text) { readAloud("", text); }

bool App::readAloud(const std::string& messageId, const std::string& markdown) {
  const tts::Prefs p = prefs();
  const std::string text = tts::speakable(markdown, p.skipCode);
  if (text.empty()) {
    widgets::toastInfo("Nothing to read aloud");
    return false;
  }
  const std::string id = messageId.empty() ? "adhoc-" + std::to_string(++gAdhoc) : messageId;
  const bool wasActive = ttsActive();
  if (!gQueue.add(id, text, static_cast<size_t>(p.serverChunkChars))) {
    widgets::toastInfo("Already reading that");
    return false;
  }
  if (wasActive) widgets::toastInfo("Queued to read next (" + std::to_string(gQueue.messagesPending()) + " waiting)");
  return true;
}

void App::readLastReplyAloud() {
  std::string last;
  for (const std::string& id : chat.branch()) {
    const Message& m = chat.messages.at(id);
    if (m.role == "assistant" && !m.content.empty()) last = id;
  }
  if (last.empty()) {
    widgets::toastInfo("No reply in this chat yet");
    return;
  }
  readAloud(last, chat.messages.at(last).content);
}

void App::readChatAloud() {
  int added = 0;
  for (const std::string& id : chat.branch()) {
    const Message& m = chat.messages.at(id);
    if (m.role != "assistant" || m.content.empty()) continue;
    const tts::Prefs p = prefs();
    if (gQueue.add(id, tts::speakable(m.content, p.skipCode), static_cast<size_t>(p.serverChunkChars))) ++added;
  }
  if (added == 0) widgets::toastInfo("Nothing new to read in this chat");
  else widgets::toastInfo("Reading " + std::to_string(added) + " repl" + (added == 1 ? "y" : "ies") + " aloud");
}

void App::stopReading() {
  gQueue.clear();
  gBusy = false;
  ++gGeneration;
  platform::stopAudio();
}

void App::stopReadingMessage(const std::string& messageId) {
  const bool playing = gQueue.active && gQueue.current.messageId == messageId;
  gQueue.remove(messageId);
  if (playing) {
    gBusy = false;
    ++gGeneration;
    platform::stopAudio();
  }
}

bool App::readingMessage(const std::string& messageId) const { return gQueue.has(messageId); }
bool App::ttsActive() const { return gBusy || gQueue.active || !gQueue.items.empty(); }
std::string App::readingStatus() const {
  std::string s = gQueue.status();
  if (!s.empty() && gBusy) s += " (asking the server)";
  return s;
}

void App::tickTts() {
  if (gBusy) return;
  if (gQueue.active) {
    // Playback reports itself a moment after it starts; give it that moment.
    if (platform::audioPlaying() || core::nowMillis() - gStartedAt < 400.0) return;
    gQueue.finish();
  }
  tts::Item item;
  if (!gQueue.next(&item)) return;
  const tts::Prefs p = prefs();
  const tts::Engine engine = tts::choose(p, gServerMissing, !localVoices().empty());
  gStartedAt = core::nowMillis();
  if (engine == tts::Engine::None) {
    widgets::toastError("No text-to-speech is available");
    gQueue.clear();
    return;
  }
  if (engine == tts::Engine::Local) {
    speakLocally(item, p);
    return;
  }
  gBusy = true;
  const int gen = gGeneration;
  const std::string voice = settings().ttsVoice;
  async::run([this, gen, item, voice, p] {
    std::string audio;
    const bool ok = api_.speech(item.text, voice, "", &audio) && !audio.empty();
    async::post([this, gen, ok, audio, item, p] {
      if (gen != gGeneration) return;   // stopped meanwhile
      gBusy = false;
      gStartedAt = core::nowMillis();
      if (ok) {
        platform::playAudio(audio, "mp3");
        return;
      }
      if (p.mode == tts::Mode::Server) {
        widgets::toastError("Text-to-speech is not configured on the server");
        gQueue.clear();
        return;
      }
      gServerMissing = true;
      if (!gToldFallback) {
        gToldFallback = true;
        widgets::toastInfo("The server has no text-to-speech - reading with this Mac's voice");
      }
      if (localVoices().empty()) {
        widgets::toastError("No text-to-speech is available");
        gQueue.clear();
        return;
      }
      speakLocally(item, p);
    });
  });
}

void App::drawReadAloudButton(const std::string& messageId, const std::string& content) {
  if (readingMessage(messageId)) {
    if (icons::button("speak", icons::Icon::Close, "Stop reading this aloud")) stopReadingMessage(messageId);
    return;
  }
  if (icons::button("speak", icons::Icon::Speaker, ttsActive() ? "Read aloud after what is playing" : "Read aloud")) readAloud(messageId, content);
}

void App::drawTtsSection(float width) {
  card("voice", [&] {
    tts::Prefs p = prefs();
    bool changed = false;
    ImGui::TextUnformatted("Replies are read by the server's text-to-speech when it has one, otherwise by this Mac's own voices.");
    static const char* kModes[] = {"Server, then this Mac", "Server only", "This Mac only"};
    int mode = static_cast<int>(p.mode);
    ImGui::SetNextItemWidth(260);
    if (ImGui::Combo("Engine", &mode, kModes, 3)) {
      p.mode = static_cast<tts::Mode>(mode);
      changed = true;
    }
    if (p.mode != tts::Mode::Local) {
      if (!gServerVoicesLoaded) {
        gServerVoicesLoaded = true;
        async::run([this] {
          api::Result res = api_.voices();
          async::post([res] {
            if (res.ok) gServerVoices = res.data.is_array() ? res.data : res.data.value("voices", Json::array());
          });
        });
      }
      core::Settings& cfg = settings();
      ImGui::SetNextItemWidth(260);
      if (ImGui::BeginCombo("Server voice", cfg.ttsVoice.empty() ? "(server default)" : cfg.ttsVoice.c_str())) {
        if (ImGui::Selectable("(server default)", cfg.ttsVoice.empty())) {
          cfg.ttsVoice.clear();
          cfg.save();
        }
        for (const auto& voice : gServerVoices) {
          const std::string id = voice.is_string() ? voice.get<std::string>() : voice.value("id", voice.value("name", ""));
          if (id.empty()) continue;
          if (ImGui::Selectable(id.c_str(), id == cfg.ttsVoice)) {
            cfg.ttsVoice = id;
            cfg.save();
          }
        }
        ImGui::EndCombo();
      }
    }
    if (p.mode != tts::Mode::Server) {
      const std::vector<tts::Voice>& voices = localVoices();
      std::string current = "(system voice, " + platform::speechLanguage() + ")";
      for (const tts::Voice& v : voices) {
        if (v.id == p.localVoice) current = tts::voiceLabel(v);
      }
      ImGui::SetNextItemWidth(260);
      if (ImGui::BeginCombo("Mac voice", current.c_str())) {
        if (ImGui::Selectable(("(system voice, " + platform::speechLanguage() + ")").c_str(), p.localVoice.empty())) {
          p.localVoice.clear();
          changed = true;
        }
        for (const tts::Voice& v : voices) {
          if (ImGui::Selectable(tts::voiceLabel(v).c_str(), v.id == p.localVoice)) {
            p.localVoice = v.id;
            changed = true;
          }
        }
        ImGui::EndCombo();
      }
      ImGui::SetNextItemWidth(200);
      changed |= ImGui::SliderFloat("Rate", &p.rate, 0.5f, 2.0f, "%.2fx");
      ImGui::SetNextItemWidth(200);
      changed |= ImGui::SliderFloat("Pitch", &p.pitch, 0.5f, 2.0f, "%.2f");
    }
    changed |= ImGui::Checkbox("Skip code blocks (announce them instead of reading the code)", &p.skipCode);
    if (changed) {
      settings().extra["tts"] = tts::toJson(p);
      settings().save();
    }
    if (ImGui::Button("Test", ImVec2(90, 0))) {
      const tts::Engine engine = tts::choose(p, gServerMissing, !localVoices().empty());
      readAloud("", std::string("This is the Open WebUI desktop client, reading with ") + (engine == tts::Engine::Local ? "this Mac's voice." : "the server's voice."));
    }
    if (ttsActive()) {
      ImGui::SameLine();
      if (ImGui::Button("Stop", ImVec2(80, 0))) stopReading();
      ImGui::SameLine();
      widgets::dimText("%s", readingStatus().c_str());
    }
    widgets::dimText("%zu voices on this Mac (more under System Settings > Accessibility > Spoken Content). %s", localVoices().size(),
                     gServerMissing ? "The server had no text-to-speech this session, so replies are read here." : "The Read aloud button under a reply queues it; a second click stops it.");
  }, width);
}

}  // namespace app
