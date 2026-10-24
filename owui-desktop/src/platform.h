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
void stopAudio();
bool audioPlaying();

// Microphone capture to a 16 kHz mono WAV in memory.
bool startRecording();
std::string stopRecording();   // returns WAV bytes ("" on failure)
bool recording();
float recordingLevel();

// Returns PNG bytes of an image on the pasteboard, or "" if there is none.
std::string clipboardImage();
void setClipboardText(const std::string& text);

std::string appVersion();

}  // namespace platform
