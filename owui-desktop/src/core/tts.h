// Local text-to-speech fallback (#118): when the server has no TTS, the Mac's
// own voices (AVSpeechSynthesizer) read replies aloud. The pure parts live
// here - preferences, the engine choice, markdown -> speakable text, sentence
// chunking, voice selection and the per-message read-aloud queue. The app
// drives the synthesizer and the server endpoint. App-free; --ttstest.
#pragma once

#include <deque>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace tts {

using Json = nlohmann::json;

enum class Mode { Auto, Server, Local };

struct Prefs {
  Mode mode = Mode::Auto;           // Auto: the server first, this Mac once the server proves to have no TTS
  std::string localVoice;           // AVSpeechSynthesisVoice identifier; "" = the system voice for your language
  float rate = 1.0f;                // 0.5 .. 2.0, relative to the voice's natural pace
  float pitch = 1.0f;               // 0.5 .. 2.0
  bool skipCode = true;             // fenced code blocks are announced ("Code block omitted"), not read out
  int serverChunkChars = 3000;      // server requests are cut at sentence ends into pieces this long
};
Prefs fromJson(const Json& extra);   // extra["tts"]
Json toJson(const Prefs& prefs);
const char* modeName(Mode mode);
Mode modeFromName(const std::string& name);

enum class Engine { Server, Local, None };
// Which engine reads the next item. Auto prefers the server until it has
// failed once this session, then stays on this Mac.
Engine choose(const Prefs& prefs, bool serverTtsMissing, bool localAvailable);

// Markdown -> text worth hearing: fenced code announced or unwrapped, links
// to their text, bare URLs to "link to <host>", headings/list/quote/table
// markup and emphasis dropped, citation markers [3] / [^3] removed.
std::string speakable(const std::string& markdown, bool skipCode);
// Sentences packed into pieces of at most `maxChars` (an oversize sentence is
// cut at a space). Never returns an empty piece.
std::vector<std::string> chunk(const std::string& text, size_t maxChars);
// AVSpeechUtterance.rate (0..1, the natural pace is 0.5) for a relative rate.
float appleRate(float relative);

struct Voice {
  std::string id, name, language;   // identifier, display name, BCP-47 ("en-US")
  int quality = 1;                  // 1 default, 2 enhanced, 3 premium
};
// `preferredId` when installed; otherwise the best enhanced/premium voice
// for `language` (exact tag first, then the same language); "" = the system
// voice, which beats an arbitrary plain one.
std::string pickVoice(const std::vector<Voice>& voices, const std::string& preferredId, const std::string& language);
std::string voiceLabel(const Voice& voice);   // "Samantha (en-US, enhanced)"

struct Item {
  std::string messageId;
  std::string text;
  int part = 1, parts = 1;          // piece n of the message's pieces
};

// The read-aloud queue: one entry per piece, several pieces per message.
struct Queue {
  std::deque<Item> items;
  Item current;
  bool active = false;              // `current` is being read

  // Queues the pieces of a message; false when it is already queued or playing.
  bool add(const std::string& messageId, const std::string& text, size_t chunkChars);
  bool next(Item* out);             // pops the next piece into `current`
  void finish();                    // `current` has been read
  void remove(const std::string& messageId);
  void clear();
  bool has(const std::string& messageId) const;   // queued or playing
  size_t messagesPending() const;   // distinct messages not yet started
  std::string status() const;       // "Reading aloud", "Reading aloud, 2 more queued", or ""
};

}  // namespace tts
