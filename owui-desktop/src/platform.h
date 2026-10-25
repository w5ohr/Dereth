// Native macOS services the UI needs: file pickers, notifications, audio
// playback and capture, clipboard images, opening links.
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace platform {

std::vector<std::string> openFiles(bool allowMultiple = true);
std::string saveFile(const std::string& suggestedName);
bool revealInFinder(const std::string& path);
void openUrl(const std::string& url);

void notify(const std::string& title, const std::string& body);

// Plays an in-memory audio clip (wav/mp3/flac as returned by the TTS endpoint).
void playAudio(const std::string& bytes, const std::string& extensionHint);
void stopAudio();              // also stops local speech
bool audioPlaying();           // a clip plays or local speech is under way

// Local speech (AVSpeechSynthesizer): the Mac's own voices.
struct SpeechVoice {
  std::string id, name, language;   // identifier, display name, BCP-47
  int quality = 1;                  // 1 default, 2 enhanced, 3 premium
};
std::vector<SpeechVoice> speechVoices();
std::string speechLanguage();  // the system's current speech language ("en-US")
// Speaks `text` with `voiceId` ("" = the system voice for speechLanguage())
// at an AVSpeechUtterance rate (0..1, 0.5 natural) and pitch multiplier.
// Replaces whatever is being spoken.
void speakLocal(const std::string& text, const std::string& voiceId, float rate, float pitch);
void stopSpeaking();
bool speaking();

// Microphone capture to a 16 kHz mono WAV in memory.
bool startRecording();
std::string stopRecording();   // returns WAV bytes ("" on failure)
bool recording();
float recordingLevel();

// Returns PNG bytes of an image on the pasteboard, or "" if there is none.
std::string clipboardImage();
bool clipboardHasText();
std::string clipboardText();   // plain text on the pasteboard, or ""
void setClipboardText(const std::string& text);

std::string appVersion();

}  // namespace platform
