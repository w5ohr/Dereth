// `Open WebUI --corpustest` (#106): the personal-corpus readers against a
// synthetic Library made here (Notes database, .emlx files, a Safari plist,
// a Messages chat.db) - no real data is read, no permission is involved.
#import <Foundation/Foundation.h>

#include <sqlite3.h>
#include <sys/stat.h>
#include <utime.h>
#include <zlib.h>

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>

#include "core/common.h"
#include "core/corpus.h"

namespace corpus {
ReadResult readReadingListFile(const std::string& library);
}

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }
namespace fs = std::filesystem;

std::string varint(uint64_t v) {
  std::string out;
  do {
    uint8_t b = v & 0x7F;
    v >>= 7;
    if (v) b |= 0x80;
    out += static_cast<char>(b);
  } while (v);
  return out;
}
std::string field(uint32_t number, const std::string& bytes) { return varint((number << 3) | 2) + varint(bytes.size()) + bytes; }
std::string gzip(const std::string& data) {
  z_stream zs{};
  deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
  std::string out(deflateBound(&zs, data.size()) + 32, '\0');
  zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
  zs.avail_in = static_cast<uInt>(data.size());
  zs.next_out = reinterpret_cast<Bytef*>(out.data());
  zs.avail_out = static_cast<uInt>(out.size());
  deflate(&zs, Z_FINISH);
  out.resize(zs.total_out);
  deflateEnd(&zs);
  return out;
}
// A NoteStoreProto with an attachment mark in the text.
std::string noteProto(const std::string& text) {
  const std::string note = varint((1 << 3) | 0) + varint(7) + field(2, text) + field(5, "attr-runs");
  return field(1, "version") + field(2, varint((1 << 3) | 0) + varint(0) + field(3, note));
}
void exec(sqlite3* db, const std::string& sql) {
  char* err = nullptr;
  if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) printf("    sql: %s\n", err ? err : "?");
  sqlite3_free(err);
}
void bindBlobInsert(sqlite3* db, const std::string& sql, const std::string& blob) {
  sqlite3_stmt* st = nullptr;
  sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr);
  sqlite3_bind_blob(st, 1, blob.data(), static_cast<int>(blob.size()), SQLITE_TRANSIENT);
  sqlite3_step(st);
  sqlite3_finalize(st);
}
void write(const std::string& path, const std::string& data, double mtime = 0) {
  fs::create_directories(fs::path(path).parent_path());
  std::ofstream(path, std::ios::binary) << data;
  if (mtime > 0) {
    struct utimbuf t {static_cast<time_t>(mtime), static_cast<time_t>(mtime)};
    utime(path.c_str(), &t);
  }
}
std::string emlx(const std::string& message) { return std::to_string(message.size()) + "\n" + message + "<?xml version=\"1.0\"?><plist><dict/></plist>"; }
}  // namespace

int runCorpusSelfTest() {
  printf("Open WebUI desktop personal corpus self-test\n\n");
  using namespace corpus;
  const std::string root = (fs::temp_directory_path() / ("owui-corpus-" + std::to_string(getpid()))).string();
  const std::string library = root + "/Library";
  fs::remove_all(root);
  const double now = static_cast<double>(std::time(nullptr));

  // ---- Notes
  report("gzip and the NoteStore protobuf come apart into the note's text",
         noteTextFromProto(gunzip(gzip(noteProto("Shopping\nmilk, eggs \xEF\xBF\xBC bread")))) == "Shopping\nmilk, eggs  bread");
  fs::create_directories(fs::path(notesDbPath(library)).parent_path());
  sqlite3* db = nullptr;
  sqlite3_open(notesDbPath(library).c_str(), &db);
  exec(db, "CREATE TABLE ZICCLOUDSYNCINGOBJECT(Z_PK INTEGER PRIMARY KEY, ZTITLE1 TEXT, ZMODIFICATIONDATE1 REAL, ZISPASSWORDPROTECTED INTEGER, ZMARKEDFORDELETION INTEGER);"
           "CREATE TABLE ZICNOTEDATA(Z_PK INTEGER PRIMARY KEY, ZNOTE INTEGER, ZDATA BLOB);"
           "INSERT INTO ZICCLOUDSYNCINGOBJECT VALUES(1, 'Trip to Lisbon', 783000000, 0, 0), (2, 'Bank PIN', 783000000, 1, 0), (3, 'Old idea', 783000000, 0, 1);");
  bindBlobInsert(db, "INSERT INTO ZICNOTEDATA(ZNOTE, ZDATA) VALUES(1, ?)", gzip(noteProto("Trip to Lisbon\nBook the tram 28 tour. Pastéis de Belém on day two.")));
  bindBlobInsert(db, "INSERT INTO ZICNOTEDATA(ZNOTE, ZDATA) VALUES(2, ?)", gzip(noteProto("1234")));
  bindBlobInsert(db, "INSERT INTO ZICNOTEDATA(ZNOTE, ZDATA) VALUES(3, ?)", gzip(noteProto("gone")));
  sqlite3_close(db);
  ReadResult notes = readNotes(library, now);
  report("Notes: the note is read; locked and deleted ones are not",
         notes.error.empty() && notes.docs.size() == 1 && notes.docs[0].title == "Trip to Lisbon" && has(notes.docs[0].text, "tram 28") &&
             notes.docs[0].id == "notes:1",
         notes.error);
  report("Notes' dates are Apple-epoch seconds", notes.docs.size() == 1 && notes.docs[0].modified > 1.7e9 && notes.docs[0].modified < 1.8e9);

  // ---- Mail
  report("quoted-printable: soft breaks and =XX", decodeQuotedPrintable("caf=C3=A9 au=\n lait") == "caf\xC3\xA9 au lait");
  report("encoded-word headers (B and Q, adjacent words joined)",
         decodeEncodedWords("=?UTF-8?B?Q2Fmw6kgcGxhbnM=?= =?iso-8859-1?Q?=E0_7?= ok") == "Caf\xC3\xA9 plans\xC3\xA0 7 ok",
         decodeEncodedWords("=?UTF-8?B?Q2Fmw6kgcGxhbnM=?= =?iso-8859-1?Q?=E0_7?= ok"));
  report("windows-1252 quotes become UTF-8", toUtf8("\x93hi\x94", "windows-1252") == "\xE2\x80\x9Chi\xE2\x80\x9D");
  const double sent = parseRfc2822Date("Sat, 24 Oct 2026 09:30:00 -0500");
  report("RFC 2822 dates with a zone", static_cast<long long>(sent) == 1792852200LL, std::to_string(static_cast<long long>(sent)));
  report("HTML to text drops style and script, keeps paragraphs and entities",
         htmlToText("<html><head><style>p{}</style></head><body><p>Tom &amp; Jerry</p><script>x()</script><p>2 &lt; 3&nbsp;</p></body></html>") == "Tom & Jerry\n\n2 < 3",
         htmlToText("<html><head><style>p{}</style></head><body><p>Tom &amp; Jerry</p><script>x()</script><p>2 &lt; 3&nbsp;</p></body></html>"));
  const std::string inbox = mailRoot(library) + "/V10/ACCOUNT/INBOX.mbox/UUID/Data/Messages/";
  const std::string multipart =
      "From: Ann Lee <ann@example.com>\r\nTo: me@example.com\r\nSubject: =?UTF-8?B?Q2Fmw6kgcGxhbnM=?=\r\nDate: Sat, 24 Oct 2026 09:30:00 -0500\r\n"
      "Content-Type: multipart/alternative; boundary=\"b1\"\r\n\r\npreamble\r\n--b1\r\nContent-Type: text/plain; charset=utf-8\r\n"
      "Content-Transfer-Encoding: quoted-printable\r\n\r\nMeet at the caf=C3=A9 on Friday at 10.=\r\n Bring the tickets.\r\n--b1\r\n"
      "Content-Type: text/html\r\n\r\n<p>HTML version</p>\r\n--b1--\r\n";
  write(inbox + "1.emlx", emlx(multipart), now - 3600);
  const std::string htmlOnly = "From: shop@example.org\nSubject: Your order\nContent-Type: text/html; charset=utf-8\nContent-Transfer-Encoding: base64\n\n" +
                               core::base64Encode("<div>Order <b>A-1001</b> has shipped</div>") + "\n";
  write(inbox + "2.emlx", emlx(htmlOnly), now - 7200);
  write(inbox + "3.partial.emlx", emlx("From: x@y.z\nSubject: Ancient\n\nfrom years ago\n"), now - 800 * 86400.0);
  write(mailRoot(library) + "/V10/ACCOUNT/Junk.mbox/UUID/Data/Messages/4.emlx", emlx("From: spam@x.y\nSubject: WIN\n\nprize\n"), now - 60);
  const Email parsed = parseEmlx(emlx(multipart));
  report("a multipart message: the plain part, decoded; the subject decoded",
         parsed.subject == "Caf\xC3\xA9 plans" && parsed.body == "Meet at the caf\xC3\xA9 on Friday at 10. Bring the tickets." && parsed.from == "Ann Lee <ann@example.com>",
         parsed.body);
  ReadResult mail = readMail(library, now, 365, 100);
  bool sawHtml = false, sawJunk = false, sawOld = false;
  for (const auto& d : mail.docs) {
    sawHtml |= has(d.text, "Order A-1001 has shipped");
    sawJunk |= d.title == "WIN";
    sawOld |= d.title == "Ancient";
  }
  report("Mail: recent messages read (an HTML-only one as text); Junk and older than the window skipped",
         mail.error.empty() && mail.docs.size() == 2 && sawHtml && !sawJunk && !sawOld && mail.docs[0].title == "Caf\xC3\xA9 plans",
         std::to_string(mail.docs.size()) + " docs " + mail.error);
  report("Mail keeps sender and sent date", !mail.docs.empty() && mail.docs[0].who == "Ann Lee <ann@example.com>" &&
                                                static_cast<long long>(mail.docs[0].modified) == 1792852200LL);

  // ---- Reading List (a real binary plist, read the way the app reads Safari's)
  NSDictionary* bookmarks = @{
    @"Children" : @[
      @{@"Title" : @"BookmarksBar", @"Children" : @[]},
      @{@"Title" : @"com.apple.ReadingList",
        @"Children" : @[ @{@"URLString" : @"https://example.com/sourdough",
                           @"URIDictionary" : @{@"title" : @"Sourdough basics"},
                           @"ReadingList" : @{@"PreviewText" : @"Feed the starter twice a day.", @"DateAdded" : [NSDate dateWithTimeIntervalSince1970:1790000000]}} ]}
    ]
  };
  NSData* plist = [NSPropertyListSerialization dataWithPropertyList:bookmarks format:NSPropertyListBinaryFormat_v1_0 options:0 error:nil];
  fs::create_directories(fs::path(bookmarksPath(library)).parent_path());
  [plist writeToFile:[NSString stringWithUTF8String:bookmarksPath(library).c_str()] atomically:YES];
  ReadResult reading = readReadingListFile(library);
  report("Reading List: the item, its title, preview and date, from a binary plist",
         reading.error.empty() && reading.docs.size() == 1 && reading.docs[0].title == "Sourdough basics" && has(reading.docs[0].text, "starter twice") &&
             static_cast<long long>(reading.docs[0].modified) == 1790000000LL,
         reading.error);

  // ---- Messages
  report("Apple time: seconds and nanoseconds since 2001", appleTimeToUnix(0) == 978307200.0 && appleTimeToUnix(1000000000000000000LL) == 978307200.0 + 1e9);
  const std::string typed = std::string("streamtyped\x81\xe8\x03\x84\x01@\x84\x84\x84\x12NSAttributedString\x00\x84\x84\x08NSObject\x00\x85\x92\x84\x84\x84\x08NSString\x01\x94\x84\x01+", 71) +
                            std::string(1, '\x0d') + "See you at 7!" + "\x86\x84\x02iI";
  report("an NSAttributedString typedstream gives up its text", decodeAttributedBody(typed) == "See you at 7!", decodeAttributedBody(typed));
  std::string longText(300, 'x');
  const std::string typedLong = std::string("NSString\x01\x94\x84\x01+\x81", 14) + std::string(1, static_cast<char>(300 & 0xFF)) + std::string(1, static_cast<char>(300 >> 8)) + longText;
  report("a long typedstream string (two-byte length)", decodeAttributedBody(typedLong) == longText);
  fs::create_directories(fs::path(messagesDbPath(library)).parent_path());
  sqlite3_open(messagesDbPath(library).c_str(), &db);
  exec(db, "CREATE TABLE handle(ROWID INTEGER PRIMARY KEY, id TEXT); CREATE TABLE chat(ROWID INTEGER PRIMARY KEY, display_name TEXT, chat_identifier TEXT);"
           "CREATE TABLE message(ROWID INTEGER PRIMARY KEY, text TEXT, attributedBody BLOB, date INTEGER, is_from_me INTEGER, handle_id INTEGER);"
           "CREATE TABLE chat_message_join(chat_id INTEGER, message_id INTEGER);"
           "INSERT INTO handle VALUES(1, '+15550100'); INSERT INTO chat VALUES(1, '', '+15550100');");
  const auto ns = [](double unix) { return std::to_string(static_cast<long long>((unix - 978307200.0) * 1e9)); };
  exec(db, "INSERT INTO message VALUES(1, 'Are we still on for dinner?', NULL, " + ns(now - 7200) + ", 0, 1);"
           "INSERT INTO message VALUES(2, 'Yes - the Thai place', NULL, " + ns(now - 7100) + ", 1, 1);"
           "INSERT INTO message VALUES(4, 'way too old', NULL, " + ns(now - 400 * 86400.0) + ", 0, 1);"
           "INSERT INTO chat_message_join VALUES(1,1),(1,2),(1,3),(1,4);");
  bindBlobInsert(db, "INSERT INTO message VALUES(3, NULL, ?, " + ns(now - 7000) + ", 0, 1)", typed);
  sqlite3_close(db);
  ReadResult messages = readMessages(library, now, 180, 1000);
  const std::string convo = messages.docs.empty() ? "" : messages.docs[0].text;
  report("Messages: one conversation-day, in time order, me and them, the archived-text message included, old ones out",
         messages.error.empty() && messages.docs.size() == 1 && has(convo, "dinner") && has(convo, "me: Yes") && has(convo, "See you at 7!") &&
             convo.find("dinner") < convo.find("me: Yes") &&
             convo.find("me: Yes") < convo.find("See you at 7!") && !has(convo, "too old") && has(messages.docs[0].title, "+15550100"),
         messages.error + " " + convo.substr(0, 120));

  // ---- What the reader is told when it cannot look
  ReadResult missing = readNotes(root + "/NoLibrary", now);
  report("a source that is not there says so", !missing.permission && has(missing.error, "nothing at"));
  chmod(messagesDbPath(library).c_str(), 0000);
  ReadResult locked = readMessages(library, now, 180, 1000);
  chmod(messagesDbPath(library).c_str(), 0644);
  report("a file macOS will not open points at Full Disk Access", locked.permission && has(locked.error, "Full Disk Access"), locked.error);

  fs::remove_all(root);
  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
