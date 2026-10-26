// Composer conveniences: drafts that survive a relaunch, a shell-style prompt
// history ring, smart paste of long text, quote-reply and steering chips.
// Drafts and history live in settings().extra ("drafts", "promptHistory").
#include "app.h"

#include <algorithm>

#include "core/common.h"
#include "ui/widgets.h"

namespace app {
namespace {
constexpr size_t kHistoryLimit = 50;
constexpr size_t kLongPasteLines = 40;
constexpr size_t kLongPasteChars = 4000;
}  // namespace

// ---------------------------------------------------------------- drafts

std::string App::draftKey() const { return chat.id.empty() ? "new" : chat.id; }

bool App::hasDraft(const std::string& chatId) const {
  const Json& drafts = core::Settings::instance().extra.value("drafts", Json::object());
  return drafts.is_object() && drafts.contains(chatId) && drafts[chatId].is_string() &&
         !drafts[chatId].get<std::string>().empty();
}

void App::saveDraft() {
  Json& drafts = settings().extra["drafts"];
  if (!drafts.is_object()) drafts = Json::object();
  const std::string key = draftKey();
  if (core::trim(composer).empty()) {
    drafts.erase(key);
  } else {
    drafts[key] = composer;
  }
  settings().save();
  lastSavedDraft = composer;
}

void App::clearDraft(const std::string& key) {
  Json& drafts = settings().extra["drafts"];
  if (drafts.is_object() && drafts.contains(key)) {
    drafts.erase(key);
    settings().save();
  }
  lastSavedDraft.clear();
}

void App::restoreDraft() {
  const Json& drafts = settings().extra.value("drafts", Json::object());
  const std::string key = draftKey();
  if (drafts.is_object() && drafts.contains(key) && drafts[key].is_string()) {
    composer = drafts[key];
    ++composerGeneration;
    lastSavedDraft = composer;
  }
}

// Called every frame from the composer: writes the draft once typing pauses.
void App::trackDraft() {
  if (composer == lastSavedDraft) {
    draftChangedAt = 0.0;
    return;
  }
  const double now = core::nowMillis();
  if (draftChangedAt == 0.0) draftChangedAt = now;
  if (now - draftChangedAt > 1200.0) saveDraft();
}

// ---------------------------------------------------------------- history

void App::pushHistory(const std::string& text) {
  const std::string entry = core::trim(text);
  if (entry.empty()) return;
  promptHistory.erase(std::remove(promptHistory.begin(), promptHistory.end(), entry), promptHistory.end());
  promptHistory.push_back(entry);
  if (promptHistory.size() > kHistoryLimit) promptHistory.erase(promptHistory.begin());
  settings().extra["promptHistory"] = promptHistory;
  settings().save();
  historyIndex = -1;
  historyStash.clear();
}

bool App::recallHistory(int direction) {
  if (promptHistory.empty()) return false;
  const int count = static_cast<int>(promptHistory.size());
  if (direction < 0) {
    // Up: only from an empty composer or while already browsing.
    if (historyIndex == -1) {
      if (!core::trim(composer).empty()) return false;
      historyStash = composer;
      historyIndex = count - 1;
    } else if (historyIndex > 0) {
      --historyIndex;
    }
  } else {
    if (historyIndex == -1) return false;
    if (historyIndex + 1 >= count) {
      historyIndex = -1;
      composer = historyStash;
      ++composerGeneration;
      return true;
    }
    ++historyIndex;
  }
  composer = promptHistory[static_cast<size_t>(historyIndex)];
  ++composerGeneration;
  return true;
}

// ---------------------------------------------------------------- paste

bool App::smartPaste(const std::string& text) {
  const size_t lines = static_cast<size_t>(std::count(text.begin(), text.end(), '\n'));
  if (lines < kLongPasteLines && text.size() < kLongPasteChars) return false;
  // Guess a name from the content so the server's text extraction is sane.
  const std::string trimmed = core::trim(text);
  std::string name = "pasted.txt";
  if (core::startsWith(trimmed, "{") || core::startsWith(trimmed, "[")) name = "pasted.json";
  else if (core::startsWith(trimmed, "<")) name = "pasted.html";
  else if (trimmed.find("\n#include") != std::string::npos || core::startsWith(trimmed, "#include")) name = "pasted.cpp";
  else if (trimmed.find("\ndef ") != std::string::npos || core::startsWith(trimmed, "import ")) name = "pasted.py";
  attachBytes(name, "text/plain", text);
  widgets::toastInfo("Long paste attached as " + name + " (" + std::to_string(lines + 1) + " lines)");
  return true;
}

// ---------------------------------------------------------------- quote, steer

void App::quoteReply(const Message& message) {
  std::string quoted = core::trim(message.content);
  if (quoted.size() > 600) quoted = quoted.substr(0, 600) + "...";
  std::string block;
  for (const std::string& line : core::split(quoted, '\n')) block += "> " + line + "\n";
  if (!composer.empty() && composer.back() != '\n') composer += "\n";
  composer += block + "\n";
  ++composerGeneration;
  focusComposer = true;
}

void App::steer(const std::string& instruction) {
  if (!streams_.empty()) return;
  sendMessage(instruction);
  ++composerGeneration;
  focusComposer = true;
}

}  // namespace app
