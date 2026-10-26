#include "corpus.h"

#include <sqlite3.h>
#include <sys/stat.h>
#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include "core/common.h"
#include "core/settings.h"

namespace corpus {
namespace fs = std::filesystem;

const char* sourceName(Source source) {
  switch (source) {
    case Source::Notes: return "notes";
    case Source::Mail: return "mail";
    case Source::ReadingList: return "reading-list";
    case Source::Messages: return "messages";
  }
  return "?";
}

const char* sourceLabel(Source source) {
  switch (source) {
    case Source::Notes: return "Notes";
    case Source::Mail: return "Mail";
    case Source::ReadingList: return "Reading List";
    case Source::Messages: return "Messages";
  }
  return "?";
}

Options optionsFromSettings() {
  const Json extra = core::Settings::instance().extra.value("personalCorpus", Json::object());
  Options o;
  if (!extra.is_object()) return o;
  o.enabled = extra.value("enabled", false);
  o.notes = extra.value("notes", false);
  o.mail = extra.value("mail", false);
  o.readingList = extra.value("readingList", false);
  o.messages = extra.value("messages", false);
  o.mailDays = std::clamp(extra.value("mailDays", 365), 7, 3650);
  o.messagesDays = std::clamp(extra.value("messagesDays", 180), 7, 3650);
  o.maxMail = std::clamp(extra.value("maxMail", 3000), 50, 50000);
  o.maxMessages = std::clamp(extra.value("maxMessages", 20000), 100, 500000);
  return o;
}

std::string notesDbPath(const std::string& library) { return library + "/Group Containers/group.com.apple.notes/NoteStore.sqlite"; }
std::string mailRoot(const std::string& library) { return library + "/Mail"; }
std::string bookmarksPath(const std::string& library) { return library + "/Safari/Bookmarks.plist"; }
std::string messagesDbPath(const std::string& library) { return library + "/Messages/chat.db"; }

namespace {

// Why a protected file could not be opened, in words.
std::string accessError(const std::string& what, const std::string& path, int err, bool* permission) {
  if (err == EPERM || err == EACCES) {
    *permission = true;
    return what + " needs Full Disk Access: System Settings > Privacy & Security > Full Disk Access, then switch on Open WebUI.";
  }
  if (err == ENOENT) return what + ": nothing at " + path + " (not set up on this Mac?)";
  return what + ": " + std::strerror(err);
}

// Opens read-only and immutable, so a database another app has open is not touched.
sqlite3* openReadOnly(const std::string& path, std::string* error, bool* permission, const std::string& what) {
  struct stat st {};
  if (::stat(path.c_str(), &st) != 0) {
    *error = accessError(what, path, errno, permission);
    return nullptr;
  }
  FILE* probe = std::fopen(path.c_str(), "rb");
  if (!probe) {
    *error = accessError(what, path, errno, permission);
    return nullptr;
  }
  std::fclose(probe);
  sqlite3* db = nullptr;
  const std::string uri = "file:" + path + "?mode=ro&immutable=1";
  if (sqlite3_open_v2(uri.c_str(), &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, nullptr) != SQLITE_OK) {
    *error = what + ": " + (db ? sqlite3_errmsg(db) : "cannot open");
    sqlite3_close(db);
    return nullptr;
  }
  return db;
}

bool hasColumn(sqlite3* db, const std::string& table, const std::string& column) {
  sqlite3_stmt* st = nullptr;
  bool found = false;
  if (sqlite3_prepare_v2(db, ("PRAGMA table_info(" + table + ")").c_str(), -1, &st, nullptr) == SQLITE_OK) {
    while (sqlite3_step(st) == SQLITE_ROW) {
      const unsigned char* name = sqlite3_column_text(st, 1);
      if (name && column == reinterpret_cast<const char*>(name)) found = true;
    }
  }
  sqlite3_finalize(st);
  return found;
}

std::string col(sqlite3_stmt* st, int i) {
  const unsigned char* t = sqlite3_column_text(st, i);
  return t ? reinterpret_cast<const char*>(t) : "";
}

std::string blob(sqlite3_stmt* st, int i) {
  const void* b = sqlite3_column_blob(st, i);
  const int n = sqlite3_column_bytes(st, i);
  return b && n > 0 ? std::string(static_cast<const char*>(b), static_cast<size_t>(n)) : std::string();
}

// Protobuf varint.
bool varint(const std::string& s, size_t* pos, uint64_t* value) {
  uint64_t v = 0;
  int shift = 0;
  while (*pos < s.size() && shift < 64) {
    const uint8_t b = static_cast<uint8_t>(s[(*pos)++]);
    v |= static_cast<uint64_t>(b & 0x7F) << shift;
    if (!(b & 0x80)) {
      *value = v;
      return true;
    }
    shift += 7;
  }
  return false;
}

// The first length-delimited field `field` of a message.
bool protoField(const std::string& msg, uint32_t field, std::string* out) {
  size_t pos = 0;
  while (pos < msg.size()) {
    uint64_t key = 0;
    if (!varint(msg, &pos, &key)) return false;
    const uint32_t number = static_cast<uint32_t>(key >> 3), type = static_cast<uint32_t>(key & 7);
    uint64_t value = 0;
    switch (type) {
      case 0:
        if (!varint(msg, &pos, &value)) return false;
        break;
      case 1: pos += 8; break;
      case 5: pos += 4; break;
      case 2: {
        if (!varint(msg, &pos, &value) || pos + value > msg.size()) return false;
        if (number == field) {
          *out = msg.substr(pos, static_cast<size_t>(value));
          return true;
        }
        pos += static_cast<size_t>(value);
        break;
      }
      default: return false;   // groups are not used by Notes
    }
  }
  return false;
}

std::string lower(std::string s) { return core::lower(s); }

std::string readFile(const std::string& path, size_t max, int* err) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    *err = errno ? errno : ENOENT;
    return "";
  }
  std::string data;
  data.resize(max);
  in.read(data.data(), static_cast<std::streamsize>(max));
  data.resize(static_cast<size_t>(in.gcount()));
  *err = 0;
  return data;
}

std::string collapse(const std::string& text) {
  // Blank-line runs to one; trailing spaces off; \r gone.
  std::string out;
  int newlines = 0;
  for (char c : text) {
    if (c == '\r') continue;
    if (c == '\n') {
      while (!out.empty() && (out.back() == ' ' || out.back() == '\t')) out.pop_back();
      if (++newlines <= 2) out += c;
      continue;
    }
    newlines = 0;
    out += c;
  }
  return core::trim(out);
}

}  // namespace

// ---------------------------------------------------------------- Notes

std::string gunzip(const std::string& data) {
  std::string out;
  if (data.empty()) return out;
  z_stream zs{};
  if (inflateInit2(&zs, 15 + 32) != Z_OK) return out;   // gzip or zlib header, detected
  zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
  zs.avail_in = static_cast<uInt>(data.size());
  char buf[16384];
  int rc;
  do {
    zs.next_out = reinterpret_cast<Bytef*>(buf);
    zs.avail_out = sizeof(buf);
    rc = ::inflate(&zs, Z_NO_FLUSH);
    if (rc != Z_OK && rc != Z_STREAM_END) break;
    out.append(buf, sizeof(buf) - zs.avail_out);
  } while (rc != Z_STREAM_END && out.size() < (32u << 20));
  inflateEnd(&zs);
  return out;
}

std::string noteTextFromProto(const std::string& protobuf) {
  std::string document, note, text;
  if (!protoField(protobuf, 2, &document) || !protoField(document, 3, &note) || !protoField(note, 2, &text)) return "";
  // Attachments sit in the text as U+FFFC; drop them.
  std::string out;
  for (size_t i = 0; i < text.size(); ++i) {
    if (i + 2 < text.size() && static_cast<uint8_t>(text[i]) == 0xEF && static_cast<uint8_t>(text[i + 1]) == 0xBF &&
        static_cast<uint8_t>(text[i + 2]) == 0xBC) {
      i += 2;
      continue;
    }
    out += text[i];
  }
  return out;
}

ReadResult readNotes(const std::string& library, double now) {
  (void)now;
  ReadResult r;
  const std::string path = notesDbPath(library);
  sqlite3* db = openReadOnly(path, &r.error, &r.permission, "Notes");
  if (!db) return r;
  // Column names move between macOS versions (ZTITLE1, ZMODIFICATIONDATE1...).
  const std::string t = "ZICCLOUDSYNCINGOBJECT";
  const std::string title = hasColumn(db, t, "ZTITLE1") ? "n.ZTITLE1" : hasColumn(db, t, "ZTITLE") ? "n.ZTITLE" : "''";
  const std::string modified = hasColumn(db, t, "ZMODIFICATIONDATE1") ? "n.ZMODIFICATIONDATE1" : hasColumn(db, t, "ZMODIFICATIONDATE") ? "n.ZMODIFICATIONDATE" : "0";
  std::string where = "1";
  if (hasColumn(db, t, "ZISPASSWORDPROTECTED")) where += " AND IFNULL(n.ZISPASSWORDPROTECTED, 0) = 0";
  if (hasColumn(db, t, "ZMARKEDFORDELETION")) where += " AND IFNULL(n.ZMARKEDFORDELETION, 0) = 0";
  const std::string sql = "SELECT n.Z_PK, " + title + ", " + modified + ", d.ZDATA FROM " + t + " n JOIN ZICNOTEDATA d ON d.ZNOTE = n.Z_PK WHERE " + where;
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) {
    r.error = std::string("Notes: this NoteStore layout is not one I know (") + sqlite3_errmsg(db) + ")";
    sqlite3_close(db);
    return r;
  }
  while (sqlite3_step(st) == SQLITE_ROW) {
    const std::string text = noteTextFromProto(gunzip(blob(st, 3)));
    if (core::trim(text).empty()) continue;
    Doc d;
    d.source = Source::Notes;
    d.id = "notes:" + std::to_string(sqlite3_column_int64(st, 0));
    d.title = col(st, 1);
    if (d.title.empty()) d.title = core::trim(text.substr(0, text.find('\n')));
    d.modified = appleTimeToUnix(static_cast<int64_t>(sqlite3_column_double(st, 2)));
    d.text = collapse(text);
    r.docs.push_back(std::move(d));
  }
  sqlite3_finalize(st);
  sqlite3_close(db);
  return r;
}

// ---------------------------------------------------------------- Mail

std::string decodeQuotedPrintable(const std::string& s, bool header) {
  std::string out;
  auto hex = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
  };
  for (size_t i = 0; i < s.size(); ++i) {
    const char c = s[i];
    if (header && c == '_') {
      out += ' ';
    } else if (c == '=' && i + 1 < s.size() && (s[i + 1] == '\n' || s[i + 1] == '\r')) {
      // A soft line break.
      ++i;
      if (s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n') ++i;
    } else if (c == '=' && i + 2 < s.size() && hex(s[i + 1]) >= 0 && hex(s[i + 2]) >= 0) {
      out += static_cast<char>(hex(s[i + 1]) * 16 + hex(s[i + 2]));
      i += 2;
    } else {
      out += c;
    }
  }
  return out;
}

std::string toUtf8(const std::string& bytes, const std::string& charsetIn) {
  const std::string charset = lower(core::trim(charsetIn));
  if (charset.empty() || charset == "utf-8" || charset == "utf8" || charset == "us-ascii" || charset == "ascii") return bytes;
  if (charset == "iso-8859-1" || charset == "latin1" || charset == "iso-8859-15" || charset == "windows-1252" || charset == "cp1252") {
    // Latin-1 to UTF-8 (cp1252's quotes and dashes in 0x80-0x9F mapped too).
    static const uint16_t cp1252[32] = {0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                                        0x2039, 0x0152, 0x8D, 0x017D, 0x8F, 0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                                        0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178};
    std::string out;
    for (unsigned char c : bytes) {
      uint32_t cp = c;
      if (c >= 0x80 && c < 0xA0 && charset.find("1252") != std::string::npos) cp = cp1252[c - 0x80];
      if (cp < 0x80) out += static_cast<char>(cp);
      else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
      } else {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
      }
    }
    return out;
  }
  return bytes;   // anything else is kept as it came
}

std::string decodeEncodedWords(const std::string& header) {
  std::string out;
  size_t pos = 0;
  bool lastWasWord = false;
  while (pos < header.size()) {
    const size_t start = header.find("=?", pos);
    if (start == std::string::npos) {
      out += header.substr(pos);
      break;
    }
    const size_t q1 = header.find('?', start + 2);
    const size_t q2 = q1 == std::string::npos ? q1 : header.find('?', q1 + 1);
    const size_t end = q2 == std::string::npos ? q2 : header.find("?=", q2 + 1);
    if (end == std::string::npos) {
      out += header.substr(pos);
      break;
    }
    std::string between = header.substr(pos, start - pos);
    // Whitespace between two encoded words is dropped (RFC 2047 6.2).
    if (!(lastWasWord && core::trim(between).empty())) out += between;
    const std::string charset = header.substr(start + 2, q1 - start - 2);
    const char encoding = static_cast<char>(std::toupper(static_cast<unsigned char>(header[q1 + 1])));
    const std::string payload = header.substr(q2 + 1, end - q2 - 1);
    const std::string bytes = encoding == 'B' ? core::base64Decode(payload) : decodeQuotedPrintable(payload, true);
    out += toUtf8(bytes, charset.substr(0, charset.find('*')));
    pos = end + 2;
    lastWasWord = true;
  }
  return out;
}

std::string htmlToText(const std::string& html) {
  std::string s = html;
  // Drop script/style/head blocks whole.
  for (const char* tag : {"script", "style", "head"}) {
    for (;;) {
      std::string l = lower(s);
      const size_t a = l.find(std::string("<") + tag);
      if (a == std::string::npos) break;
      const size_t b = l.find(std::string("</") + tag, a);
      const size_t e = b == std::string::npos ? std::string::npos : l.find('>', b);
      s.erase(a, e == std::string::npos ? std::string::npos : e + 1 - a);
    }
  }
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '<') {
      const size_t e = s.find('>', i);
      if (e == std::string::npos) break;
      std::string tag = lower(s.substr(i + 1, e - i - 1));
      if (!tag.empty() && tag[0] == '/') tag.erase(0, 1);
      tag = tag.substr(0, tag.find_first_of(" /\t\n"));
      if (tag == "br" || tag == "p" || tag == "div" || tag == "tr" || tag == "li" || tag == "h1" || tag == "h2" || tag == "h3" || tag == "blockquote") out += '\n';
      i = e;
    } else if (s[i] == '&') {
      const size_t e = s.find(';', i);
      if (e != std::string::npos && e - i <= 8) {
        const std::string name = s.substr(i + 1, e - i - 1);
        static const std::map<std::string, std::string> named{{"amp", "&"}, {"lt", "<"}, {"gt", ">"}, {"quot", "\""}, {"apos", "'"}, {"nbsp", " "}, {"#39", "'"}};
        auto it = named.find(name);
        if (it != named.end()) {
          out += it->second;
          i = e;
          continue;
        }
        if (name.size() > 1 && name[0] == '#') {
          const long cp = name[1] == 'x' ? std::strtol(name.c_str() + 2, nullptr, 16) : std::strtol(name.c_str() + 1, nullptr, 10);
          if (cp > 0 && cp < 0x80) {
            out += static_cast<char>(cp);
            i = e;
            continue;
          }
        }
      }
      out += '&';
    } else {
      out += s[i];
    }
  }
  return collapse(out);
}

namespace {

struct Part {
  std::map<std::string, std::string> headers;   // lower-case names, unfolded values
  std::string body;
};

Part splitPart(const std::string& raw) {
  Part p;
  size_t split = raw.find("\r\n\r\n");
  size_t skip = 4;
  const size_t lf = raw.find("\n\n");
  if (split == std::string::npos || (lf != std::string::npos && lf < split)) {
    split = lf;
    skip = 2;
  }
  const std::string head = split == std::string::npos ? raw : raw.substr(0, split);
  p.body = split == std::string::npos ? "" : raw.substr(split + skip);
  std::string name, value;
  auto flush = [&] {
    if (!name.empty()) p.headers[lower(name)] = core::trim(value);
    name.clear();
    value.clear();
  };
  for (std::string line : core::split(head, '\n')) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty() && (line[0] == ' ' || line[0] == '\t')) {
      value += " " + core::trim(line);   // a folded continuation
      continue;
    }
    flush();
    const size_t colon = line.find(':');
    if (colon == std::string::npos) continue;
    name = line.substr(0, colon);
    value = line.substr(colon + 1);
  }
  flush();
  return p;
}

// A header parameter: boundary="x", charset=utf-8.
std::string param(const std::string& header, const std::string& key) {
  const std::string l = lower(header);
  size_t pos = 0;
  while ((pos = l.find(key + "=", pos)) != std::string::npos) {
    if (pos == 0 || l[pos - 1] == ';' || l[pos - 1] == ' ' || l[pos - 1] == '\t') break;
    pos += key.size();
  }
  if (pos == std::string::npos) return "";
  pos += key.size() + 1;
  if (pos < header.size() && header[pos] == '"') {
    const size_t end = header.find('"', pos + 1);
    return header.substr(pos + 1, end == std::string::npos ? std::string::npos : end - pos - 1);
  }
  const size_t end = header.find_first_of("; \t", pos);
  return header.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
}

std::string decodeBody(const Part& p) {
  const std::string encoding = lower(core::trim(p.headers.count("content-transfer-encoding") ? p.headers.at("content-transfer-encoding") : ""));
  std::string bytes = p.body;
  if (encoding == "base64") {
    std::string compact;
    for (char c : bytes) {
      if (!std::isspace(static_cast<unsigned char>(c))) compact += c;
    }
    bytes = core::base64Decode(compact);
  } else if (encoding == "quoted-printable") {
    bytes = decodeQuotedPrintable(bytes);
  }
  const std::string type = p.headers.count("content-type") ? p.headers.at("content-type") : "text/plain";
  return toUtf8(bytes, param(type, "charset"));
}

// The best text of a (possibly multipart) part: text/plain, else text/html.
void bestText(const Part& p, int depth, std::string* plain, std::string* html) {
  const std::string type = p.headers.count("content-type") ? p.headers.at("content-type") : "text/plain";
  const std::string mime = lower(core::trim(type.substr(0, type.find(';'))));
  const std::string disposition = lower(p.headers.count("content-disposition") ? p.headers.at("content-disposition") : "");
  if (disposition.rfind("attachment", 0) == 0) return;
  if (mime.rfind("multipart/", 0) == 0 && depth < 8) {
    const std::string boundary = param(type, "boundary");
    if (boundary.empty()) return;
    const std::string marker = "--" + boundary;
    size_t pos = p.body.find(marker);
    while (pos != std::string::npos) {
      pos += marker.size();
      if (p.body.compare(pos, 2, "--") == 0) break;   // the closing boundary
      const size_t next = p.body.find(marker, pos);
      std::string chunk = p.body.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
      if (!chunk.empty() && chunk[0] == '\r') chunk.erase(0, 1);
      if (!chunk.empty() && chunk[0] == '\n') chunk.erase(0, 1);
      bestText(splitPart(chunk), depth + 1, plain, html);
      pos = next;
    }
    return;
  }
  if (mime == "text/plain" && plain->empty()) *plain = decodeBody(p);
  else if (mime == "text/html" && html->empty()) *html = decodeBody(p);
}

}  // namespace

double parseRfc2822Date(const std::string& raw) {
  // "Sat, 25 Oct 2026 14:30:00 -0500" (the weekday optional).
  std::string s = core::trim(raw);
  const size_t comma = s.find(',');
  if (comma != std::string::npos && comma < 5) s = core::trim(s.substr(comma + 1));
  int day = 0, year = 0, h = 0, m = 0, sec = 0;
  char mon[4] = {0};
  char zone[8] = {0};
  const int n = std::sscanf(s.c_str(), "%d %3s %d %d:%d:%d %7s", &day, mon, &year, &h, &m, &sec, zone);
  if (n < 5) return 0;
  static const char* months = "JanFebMarAprMayJunJulAugSepOctNovDec";
  const char* found = std::strstr(months, mon);
  if (!found || mon[0] == 0) return 0;
  std::tm t{};
  t.tm_year = (year < 100 ? year + 2000 : year) - 1900;
  t.tm_mon = static_cast<int>((found - months) / 3);
  t.tm_mday = day;
  t.tm_hour = h;
  t.tm_min = m;
  t.tm_sec = sec;
  double utc = static_cast<double>(timegm(&t));
  if ((zone[0] == '+' || zone[0] == '-') && std::strlen(zone) >= 5) {
    const int hh = (zone[1] - '0') * 10 + (zone[2] - '0'), mm = (zone[3] - '0') * 10 + (zone[4] - '0');
    const int offset = (hh * 3600 + mm * 60) * (zone[0] == '-' ? -1 : 1);
    utc -= offset;
  }
  return utc;
}

Email parseRfc822(const std::string& message) {
  const Part top = splitPart(message);
  Email e;
  auto header = [&](const char* name) { return top.headers.count(name) ? decodeEncodedWords(top.headers.at(name)) : std::string(); };
  e.subject = header("subject");
  e.from = header("from");
  e.to = header("to");
  e.date = header("date");
  e.sent = parseRfc2822Date(e.date);
  std::string plain, html;
  bestText(top, 0, &plain, &html);
  e.body = !core::trim(plain).empty() ? collapse(plain) : htmlToText(html);
  return e;
}

Email parseEmlx(const std::string& bytes) {
  // First line: the message's length in bytes.
  const size_t nl = bytes.find('\n');
  if (nl == std::string::npos) return parseRfc822(bytes);
  const std::string first = core::trim(bytes.substr(0, nl));
  const bool numeric = !first.empty() && std::all_of(first.begin(), first.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
  if (!numeric) return parseRfc822(bytes);
  const size_t length = static_cast<size_t>(std::strtoull(first.c_str(), nullptr, 10));
  return parseRfc822(bytes.substr(nl + 1, length));
}

ReadResult readMail(const std::string& library, double now, int days, int max) {
  ReadResult r;
  const std::string root = mailRoot(library);
  std::error_code ec;
  fs::directory_iterator probe(root, ec);
  if (ec) {
    r.error = accessError("Mail", root, ec.value(), &r.permission);
    return r;
  }
  // Newest messages first, within the window.
  struct File {
    std::string path;
    double mtime;
  };
  std::vector<File> files;
  const double since = now - days * 86400.0;
  for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
       !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    const fs::path& p = it->path();
    const std::string name = p.filename().string();
    if (it->is_directory(ec)) {
      // Junk and trash are not worth knowing.
      const std::string l = lower(name);
      if (l == "junk.mbox" || l == "trash.mbox" || l == "deleted messages.mbox" || l == "spam.mbox") it.disable_recursion_pending();
      continue;
    }
    if (name.size() < 6 || name.compare(name.size() - 5, 5, ".emlx") != 0) continue;
    struct stat st {};
    if (::stat(p.c_str(), &st) != 0) continue;
    const double mtime = static_cast<double>(st.st_mtimespec.tv_sec);
    if (mtime < since) continue;
    files.push_back({p.string(), mtime});
  }
  std::sort(files.begin(), files.end(), [](const File& a, const File& b) { return a.mtime > b.mtime; });
  if (static_cast<int>(files.size()) > max) files.resize(static_cast<size_t>(max));
  for (const File& f : files) {
    int err = 0;
    const std::string bytes = readFile(f.path, 4u << 20, &err);
    if (err) continue;
    const Email e = parseEmlx(bytes);
    if (core::trim(e.body).empty() && e.subject.empty()) continue;
    Doc d;
    d.source = Source::Mail;
    d.id = "mail:" + f.path;
    d.title = e.subject.empty() ? "(no subject)" : e.subject;
    d.who = e.from;
    d.modified = e.sent > 0 ? e.sent : f.mtime;
    d.text = "From: " + e.from + "\nTo: " + e.to + "\nDate: " + e.date + "\nSubject: " + e.subject + "\n\n" + e.body;
    r.docs.push_back(std::move(d));
  }
  return r;
}

// ---------------------------------------------------------------- Reading List

ReadResult readReadingList(const Json& bookmarks) {
  ReadResult r;
  if (!bookmarks.is_object() || !bookmarks.contains("Children") || !bookmarks["Children"].is_array()) {
    r.error = "Reading List: Bookmarks.plist has no folders";
    return r;
  }
  for (const auto& folder : bookmarks["Children"]) {
    if (!folder.is_object() || folder.value("Title", "") != "com.apple.ReadingList") continue;
    for (const auto& item : folder.value("Children", Json::array())) {
      if (!item.is_object()) continue;
      const std::string url = item.value("URLString", "");
      if (url.empty()) continue;
      Doc d;
      d.source = Source::ReadingList;
      d.id = "reading-list:" + url;
      d.title = item.contains("URIDictionary") && item["URIDictionary"].is_object() ? item["URIDictionary"].value("title", "") : "";
      if (d.title.empty()) d.title = url;
      std::string preview;
      double added = 0;
      if (item.contains("ReadingList") && item["ReadingList"].is_object()) {
        const Json& rl = item["ReadingList"];
        preview = rl.value("PreviewText", "");
        if (rl.contains("DateAdded") && rl["DateAdded"].is_number()) added = rl["DateAdded"].get<double>();
      }
      d.modified = added;
      d.text = d.title + "\n" + url + (preview.empty() ? "" : "\n\n" + preview);
      r.docs.push_back(std::move(d));
    }
  }
  return r;
}

// ---------------------------------------------------------------- Messages

double appleTimeToUnix(int64_t t) {
  constexpr double kAppleEpoch = 978307200.0;   // 2001-01-01 UTC
  if (t > 100000000000LL) return kAppleEpoch + static_cast<double>(t) / 1e9;   // nanoseconds (High Sierra on)
  return kAppleEpoch + static_cast<double>(t);
}

std::string decodeAttributedBody(const std::string& b) {
  // A typedstream: the archived NSString's text follows "NSString", a few
  // class bytes, then '+' and a length (one byte, or 0x81 + two bytes LE,
  // or 0x82 + four bytes LE).
  size_t pos = b.find("NSString");
  if (pos == std::string::npos) return "";
  pos = b.find('+', pos + 8);
  if (pos == std::string::npos || pos + 1 >= b.size()) return "";
  ++pos;
  size_t length = static_cast<uint8_t>(b[pos++]);
  if (length == 0x81 && pos + 2 <= b.size()) {
    length = static_cast<uint8_t>(b[pos]) | (static_cast<size_t>(static_cast<uint8_t>(b[pos + 1])) << 8);
    pos += 2;
  } else if (length == 0x82 && pos + 4 <= b.size()) {
    length = static_cast<uint8_t>(b[pos]) | (static_cast<size_t>(static_cast<uint8_t>(b[pos + 1])) << 8) |
             (static_cast<size_t>(static_cast<uint8_t>(b[pos + 2])) << 16) | (static_cast<size_t>(static_cast<uint8_t>(b[pos + 3])) << 24);
    pos += 4;
  }
  if (pos + length > b.size()) return "";
  return b.substr(pos, length);
}

ReadResult readMessages(const std::string& library, double now, int days, int max) {
  ReadResult r;
  const std::string path = messagesDbPath(library);
  sqlite3* db = openReadOnly(path, &r.error, &r.permission, "Messages");
  if (!db) return r;
  const bool hasBody = hasColumn(db, "message", "attributedBody");
  const std::string sql = std::string("SELECT m.text, ") + (hasBody ? "m.attributedBody" : "NULL") +
                          ", m.date, m.is_from_me, IFNULL(h.id, ''), IFNULL(c.display_name, ''), IFNULL(c.chat_identifier, '')"
                          " FROM message m LEFT JOIN handle h ON h.ROWID = m.handle_id"
                          " LEFT JOIN chat_message_join j ON j.message_id = m.ROWID LEFT JOIN chat c ON c.ROWID = j.chat_id"
                          " ORDER BY m.date DESC LIMIT ?";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) {
    r.error = std::string("Messages: this chat.db layout is not one I know (") + sqlite3_errmsg(db) + ")";
    sqlite3_close(db);
    return r;
  }
  sqlite3_bind_int(st, 1, max);
  // One document per conversation per day, lines in time order.
  struct Line {
    double when;
    std::string text;
  };
  struct Day {
    std::string who;
    std::vector<Line> lines;
    double last = 0;
  };
  std::map<std::string, Day> days_;
  const double since = now - days * 86400.0;
  while (sqlite3_step(st) == SQLITE_ROW) {
    std::string text = col(st, 0);
    if (core::trim(text).empty()) text = decodeAttributedBody(blob(st, 1));
    text = core::trim(text);
    if (text.empty()) continue;
    const double when = appleTimeToUnix(sqlite3_column_int64(st, 2));
    if (when < since) break;   // newest first: the rest are older
    const bool fromMe = sqlite3_column_int(st, 3) != 0;
    const std::string handle = col(st, 4), name = col(st, 5), chatId = col(st, 6);
    const std::string who = !name.empty() ? name : !chatId.empty() ? chatId : handle;
    std::time_t tt = static_cast<std::time_t>(when);
    std::tm local{};
    localtime_r(&tt, &local);
    char day[16], hm[8];
    std::strftime(day, sizeof day, "%Y-%m-%d", &local);
    std::strftime(hm, sizeof hm, "%H:%M", &local);
    Day& d = days_[who + "\n" + day];
    d.who = who;
    d.last = std::max(d.last, when);
    d.lines.push_back({when, std::string("[") + hm + "] " + (fromMe ? "me" : (handle.empty() ? who : handle)) + ": " + text});
  }
  sqlite3_finalize(st);
  sqlite3_close(db);
  for (auto& [key, d] : days_) {
    std::sort(d.lines.begin(), d.lines.end(), [](const Line& a, const Line& b) { return a.when < b.when; });
    const std::string day = key.substr(key.find('\n') + 1);
    Doc doc;
    doc.source = Source::Messages;
    doc.id = "messages:" + d.who + ":" + day;
    doc.title = "Messages with " + d.who + ", " + day;
    doc.who = d.who;
    doc.modified = d.last;
    for (const Line& l : d.lines) doc.text += l.text + "\n";
    r.docs.push_back(std::move(doc));
  }
  return r;
}

}  // namespace corpus
