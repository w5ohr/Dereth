// Personal corpus (#106): readers for Apple Notes, Mail, the Safari Reading
// List and Messages that turn what is on this Mac into documents for a local
// index (embedded on Ollama, stored in the app's support folder, never sent
// to a server). Everything here reads files under a Library folder it is
// given, so the self-test runs on a synthetic one. macOS guards the real
// files with Full Disk Access; without it a read fails and the reason says so.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace corpus {

using Json = nlohmann::json;

enum class Source { Notes, Mail, ReadingList, Messages };
const char* sourceName(Source source);   // "notes", "mail", "reading-list", "messages"
const char* sourceLabel(Source source);  // "Notes", "Mail", ...

struct Doc {
  Source source = Source::Notes;
  std::string id;          // stable key, e.g. "notes:1234", "mail:/path.emlx", "messages:+1555...:2026-10-25"
  std::string title;
  std::string who;         // sender / conversation, when there is one
  double modified = 0;     // seconds since the epoch
  std::string text;        // plain text, ready to chunk
};

struct Options {
  bool enabled = false;
  bool notes = false, mail = false, readingList = false, messages = false;
  int mailDays = 365, messagesDays = 180;
  int maxMail = 3000, maxMessages = 20000;
  bool any() const { return notes || mail || readingList || messages; }
};
Options optionsFromSettings();

struct ReadResult {
  std::vector<Doc> docs;
  std::string error;       // "" when the source read; else why not (permission, missing)
  bool permission = false; // the error is macOS refusing access
};

// Paths under `library` (normally ~/Library).
std::string notesDbPath(const std::string& library);
std::string mailRoot(const std::string& library);
std::string bookmarksPath(const std::string& library);
std::string messagesDbPath(const std::string& library);

ReadResult readNotes(const std::string& library, double now);
ReadResult readMail(const std::string& library, double now, int days, int max);
// The Reading List comes from Bookmarks.plist, which the native side turns into JSON first.
ReadResult readReadingList(const Json& bookmarks);
ReadResult readMessages(const std::string& library, double now, int days, int max);

// ---- pieces, exposed for the self-test ------------------------------------
std::string gunzip(const std::string& data);
std::string noteTextFromProto(const std::string& protobuf);   // NoteStoreProto: document(2) > note(3) > text(2)
struct Email {
  std::string subject, from, to, date, body;
  double sent = 0;
};
Email parseEmlx(const std::string& bytes);            // .emlx: a length line, the RFC 822 message, a plist
Email parseRfc822(const std::string& message);
std::string decodeQuotedPrintable(const std::string& s, bool header = false);
std::string decodeEncodedWords(const std::string& header);   // =?UTF-8?B?...?= and =?...?Q?...?=
std::string toUtf8(const std::string& bytes, const std::string& charset);
std::string htmlToText(const std::string& html);
double parseRfc2822Date(const std::string& date);
std::string decodeAttributedBody(const std::string& blob);  // the text inside Messages' NSAttributedString typedstream
double appleTimeToUnix(int64_t t);                           // seconds or nanoseconds since 2001-01-01

}  // namespace corpus
