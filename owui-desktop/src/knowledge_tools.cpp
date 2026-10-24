// Knowledge tools: folder -> knowledge base sync, clickable citations, URL
// capture, local embedding index.
//
// Owned by work package D in docs/FEATURE-PLAN.md. Everything that touches the
// network or the disk runs on an async worker; the statics below are only
// read and written on the UI thread (workers hand results back via
// async::post), except the atomics, which the index worker updates directly.
#include <CommonCrypto/CommonDigest.h>
#include <sqlite3.h>
#include <sys/stat.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <thread>

#include "app.h"
#include "core/async.h"
#include "core/common.h"
#include "core/http.h"
#include "imgui_stdlib.h"
#include "native.h"
#include "platform.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

namespace fs = std::filesystem;

// ---- eligible-file walk ---------------------------------------------------
// The same filter package C uses for "attach folder": honour .gitignore
// (name, dir/, *.ext, leading /, **; negations are ignored), skip dotfiles,
// symlinks, binaries (NUL in the first 1 KB) and anything over 200 KB.

constexpr size_t kMaxFileBytes = 200 * 1024;
constexpr size_t kMaxFiles = 5000;

struct IgnoreRule {
  std::string base;      // directory the .gitignore lives in, relative, "" or "dir/"
  std::string pattern;
  bool dirOnly = false;
  bool anchored = false; // contains a slash: match against the path, not the name
};

struct FileFilter {
  std::string root;
  std::vector<IgnoreRule> rules;
};

// Glob with gitignore's flavour: `*` and `?` stop at '/', `**/` spans whole
// directories, a trailing `**` swallows the rest.
bool globMatch(const char* p, const char* s) {
  while (*p) {
    if (p[0] == '*' && p[1] == '*') {
      const bool slash = p[2] == '/';
      p += slash ? 3 : 2;
      if (!slash) {
        for (const char* q = s;; ++q) {
          if (globMatch(p, q)) return true;
          if (!*q) return false;
        }
      }
      for (const char* q = s;;) {
        if (globMatch(p, q)) return true;
        q = std::strchr(q, '/');
        if (!q) return false;
        ++q;
      }
    }
    if (*p == '*') {
      ++p;
      for (const char* q = s;; ++q) {
        if (globMatch(p, q)) return true;
        if (!*q || *q == '/') return false;
      }
    }
    if (!*s) return false;
    if (*p == '?') {
      if (*s == '/') return false;
    } else if (*p != *s) {
      return false;
    }
    ++p;
    ++s;
  }
  return !*s;
}

void loadGitignore(const std::string& dir, const std::string& relBase, FileFilter* filter) {
  bool ok = false;
  const std::string raw = core::readFile(dir + "/.gitignore", &ok);
  if (!ok) return;
  for (std::string line : core::split(raw, '\n')) {
    line = core::trim(line);
    if (line.empty() || line[0] == '#' || line[0] == '!') continue;
    IgnoreRule rule;
    rule.base = relBase;
    if (line.back() == '/') {
      rule.dirOnly = true;
      line.pop_back();
    }
    if (line.empty()) continue;
    if (line[0] == '/') {
      rule.anchored = true;
      line.erase(0, 1);
    } else if (line.find('/') != std::string::npos) {
      rule.anchored = true;
    }
    rule.pattern = line;
    filter->rules.push_back(rule);
  }
}

bool ignored(const FileFilter& filter, const std::string& rel, bool isDir) {
  for (const IgnoreRule& rule : filter.rules) {
    if (rule.dirOnly && !isDir) continue;
    if (!core::startsWith(rel, rule.base)) continue;
    const std::string sub = rel.substr(rule.base.size());
    const std::string subject = rule.anchored ? sub : core::baseName(sub);
    if (globMatch(rule.pattern.c_str(), subject.c_str())) return true;
  }
  return false;
}

// Reads a file if it passes the size and binary checks.
bool readTextFile(const std::string& path, std::string* out) {
  struct stat st {};
  if (::stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return false;
  if (st.st_size <= 0 || static_cast<size_t>(st.st_size) > kMaxFileBytes) return false;
  bool ok = false;
  std::string bytes = core::readFile(path, &ok);
  if (!ok) return false;
  const size_t probe = std::min<size_t>(bytes.size(), 1024);
  if (std::memchr(bytes.data(), '\0', probe) != nullptr) return false;
  *out = std::move(bytes);
  return true;
}

bool looksBinary(const std::string& path) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return true;
  char buf[1024];
  const size_t n = std::fread(buf, 1, sizeof(buf), f);
  std::fclose(f);
  return std::memchr(buf, '\0', n) != nullptr;
}

void walkDir(const std::string& dir, const std::string& rel, FileFilter* filter,
             std::vector<std::string>* out) {
  loadGitignore(dir, rel, filter);
  std::error_code ec;
  std::vector<fs::directory_entry> entries;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    entries.push_back(*it);
  }
  std::sort(entries.begin(), entries.end(),
            [](const auto& a, const auto& b) { return a.path() < b.path(); });
  for (const fs::directory_entry& entry : entries) {
    if (out->size() >= kMaxFiles) return;
    const std::string name = entry.path().filename().string();
    if (name.empty() || name[0] == '.') continue;
    const fs::file_status status = entry.symlink_status(ec);
    if (ec || fs::is_symlink(status)) continue;
    const std::string childRel = rel + name;
    if (fs::is_directory(status)) {
      if (ignored(*filter, childRel, true)) continue;
      walkDir(entry.path().string(), childRel + "/", filter, out);
    } else if (fs::is_regular_file(status)) {
      if (ignored(*filter, childRel, false)) continue;
      const auto size = entry.file_size(ec);
      if (ec || size == 0 || size > kMaxFileBytes) continue;
      if (looksBinary(entry.path().string())) continue;
      out->push_back(entry.path().string());
    }
  }
}

// Absolute paths of every eligible file under `root` (which should be canonical,
// because FSEvents reports canonical paths and the sync map is keyed by them).
std::vector<std::string> eligibleFiles(const std::string& root, FileFilter* filter) {
  filter->root = root;
  filter->rules.clear();
  std::vector<std::string> out;
  walkDir(root, "", filter, &out);
  return out;
}

std::string relativeTo(const std::string& root, const std::string& path) {
  if (core::startsWith(path, root + "/")) return path.substr(root.size() + 1);
  return path;
}

// Re-checks one path from a change event against the walk's rules, including
// every directory on the way down.
bool eligiblePath(const FileFilter& filter, const std::string& path) {
  if (!core::startsWith(path, filter.root + "/")) return false;
  const std::string rel = path.substr(filter.root.size() + 1);
  std::string prefix;
  const std::vector<std::string> parts = core::split(rel, '/');
  for (size_t i = 0; i < parts.size(); ++i) {
    if (parts[i].empty() || parts[i][0] == '.') return false;
    prefix += parts[i];
    if (ignored(filter, prefix, i + 1 < parts.size())) return false;
    prefix += "/";
  }
  return true;
}

std::string canonicalFolder(const std::string& folder) {
  std::error_code ec;
  fs::path canonical = fs::weakly_canonical(fs::path(folder), ec);
  std::string out = ec ? folder : canonical.string();
  while (out.size() > 1 && out.back() == '/') out.pop_back();
  return out;
}

// SHA-256, the same digest the server reports as meta.file_hash, so files
// already in a knowledge base can be recognised without downloading them.
std::string contentHash(const std::string& bytes) {
  unsigned char digest[CC_SHA256_DIGEST_LENGTH];
  CC_SHA256(bytes.data(), static_cast<CC_LONG>(bytes.size()), digest);
  static const char* kHex = "0123456789abcdef";
  std::string out;
  for (unsigned char b : digest) {
    out.push_back(kHex[b >> 4]);
    out.push_back(kHex[b & 15]);
  }
  return out;
}

// Code and config files go up as text/plain so the server's text loader takes
// them; anything mimeForExtension knows to be text keeps its own type.
std::string uploadMime(const std::string& path) {
  const std::string mime = core::mimeForExtension(core::extension(path));
  if (core::startsWith(mime, "text/") || mime == "application/json") return mime;
  return "text/plain";
}

// Uploads, waits for the server to finish extracting text (adding an
// unprocessed file to a knowledge base can fail), then adds it to the KB.
// Returns the file id, or "" with *error set; a half-done upload is deleted.
std::string uploadToKnowledge(api::Client& api, const std::string& knowledgeId,
                              const std::string& filename, const std::string& mime,
                              const std::string& bytes, std::string* error) {
  api::Result up = api.uploadFile(filename, mime, bytes, true);
  if (!up.ok || !up.data.is_object() || !up.data.contains("id")) {
    *error = api::errorMessage(up);
    if (error->empty()) *error = "upload failed";
    return "";
  }
  const std::string id = up.data["id"].get<std::string>();
  for (int i = 0; i < 240; ++i) {
    api::Result st = api.fileProcessStatus(id);
    if (!st.ok || !st.data.is_object()) break;   // older servers: no status endpoint
    const std::string state = st.data.value("status", "");
    if (state == "completed") break;
    if (state == "failed") {
      *error = st.data.value("error", "the server could not process " + filename);
      api.deleteFile(id);
      return "";
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }
  api::Result add = api.addFileToKnowledge(knowledgeId, id);
  if (!add.ok) {
    *error = api::errorMessage(add);
    if (error->empty()) *error = "could not add " + filename + " to the knowledge base";
    api.deleteFile(id);
    return "";
  }
  return id;
}

// The server rejects slashes in stored names, so nested paths are flattened.
std::string syncFileName(const std::string& root, const std::string& path) {
  return core::replaceAll(relativeTo(root, path), "/", "__");
}

// ---- folder sync state ------------------------------------------------------

struct SyncedFile {
  std::string id;
  std::string hash;
};
using SyncMap = std::map<std::string, SyncedFile>;   // absolute path -> server file

std::atomic<int> gSyncGeneration{0};
SyncMap gSyncMap;
std::shared_ptr<FileFilter> gSyncFilter;
std::set<std::string> gSyncPending;
bool gSyncBusy = false;

// extra["kbSync"][kbId] = {folder, files: {path: {id, hash}}}
SyncMap loadSyncMap(const std::string& knowledgeId, const std::string& folder) {
  SyncMap map;
  const Json& extra = core::Settings::instance().extra;
  if (!extra.contains("kbSync") || !extra["kbSync"].is_object()) return map;
  const Json& all = extra["kbSync"];
  if (!all.contains(knowledgeId) || !all[knowledgeId].is_object()) return map;
  const Json& entry = all[knowledgeId];
  // A different folder's files are left alone in the KB rather than deleted.
  if (entry.value("folder", "") != folder || !entry.contains("files")) return map;
  for (auto it = entry["files"].begin(); it != entry["files"].end(); ++it) {
    if (!it.value().is_object()) continue;
    map[it.key()] = {it.value().value("id", ""), it.value().value("hash", "")};
  }
  return map;
}

void saveSyncMap(const std::string& knowledgeId, const std::string& folder, const SyncMap& map) {
  core::Settings& settings = core::Settings::instance();
  if (!settings.extra.contains("kbSync") || !settings.extra["kbSync"].is_object()) {
    settings.extra["kbSync"] = Json::object();
  }
  Json files = Json::object();
  for (const auto& [path, file] : map) files[path] = {{"id", file.id}, {"hash", file.hash}};
  settings.extra["kbSync"][knowledgeId] = {{"folder", folder}, {"files", files}};
  settings.save();
}

std::string clockTime() { return core::formatTime(core::nowSeconds(), "%H:%M:%S"); }

// Re-uploads whatever the watcher queued. One batch at a time; events that
// arrive meanwhile wait in gSyncPending for the next round.
void pumpSyncQueue(int generation) {
  App* app = &App::instance();
  if (generation != gSyncGeneration || gSyncBusy || gSyncPending.empty() || !gSyncFilter) return;
  gSyncBusy = true;
  std::vector<std::string> paths(gSyncPending.begin(), gSyncPending.end());
  gSyncPending.clear();
  const std::string folder = app->folderSyncPath;
  const std::string knowledgeId = app->folderSyncKnowledgeId;
  std::shared_ptr<FileFilter> filter = gSyncFilter;
  SyncMap map = gSyncMap;
  async::run([app, generation, paths, folder, knowledgeId, filter, map]() mutable {
    std::vector<std::string> updated;
    std::vector<std::string> removed;
    std::string lastError;
    for (const std::string& path : paths) {
      struct stat st {};
      const bool exists = ::stat(path.c_str(), &st) == 0;
      if (!exists || S_ISDIR(st.st_mode)) {
        // Deleted or renamed away; a vanished directory takes its files along.
        if (exists) continue;
        for (auto it = map.begin(); it != map.end();) {
          if (it->first == path || core::startsWith(it->first, path + "/")) {
            app->api().removeFileFromKnowledge(knowledgeId, it->second.id);
            removed.push_back(relativeTo(folder, it->first));
            it = map.erase(it);
          } else {
            ++it;
          }
        }
        continue;
      }
      if (!eligiblePath(*filter, path)) continue;
      std::string bytes;
      if (!readTextFile(path, &bytes) || core::trim(bytes).empty()) continue;
      const std::string hash = contentHash(bytes);
      auto it = map.find(path);
      if (it != map.end() && it->second.hash == hash) continue;   // touch, not an edit
      const std::string rel = relativeTo(folder, path);
      async::post([app, generation, rel] {
        if (generation == gSyncGeneration) app->folderSyncStatus = "Re-uploading " + rel + "…";
      });
      if (it != map.end()) app->api().removeFileFromKnowledge(knowledgeId, it->second.id);
      std::string error;
      const std::string id = uploadToKnowledge(app->api(), knowledgeId,
                                               syncFileName(folder, path), uploadMime(path),
                                               bytes, &error);
      if (id.empty()) {
        map.erase(path);
        lastError = rel + ": " + error;
      } else {
        map[path] = {id, hash};
        updated.push_back(rel);
      }
    }
    async::post([app, generation, folder, knowledgeId, map, updated, removed, lastError] {
      // Persist even if the sync was stopped meanwhile, so the uploads it did
      // make are still tracked next time.
      saveSyncMap(knowledgeId, folder, map);
      if (generation != gSyncGeneration) return;
      gSyncMap = map;
      gSyncBusy = false;
      if (!lastError.empty()) {
        app->folderSyncStatus = "Upload failed at " + clockTime() + " — " + lastError;
      } else if (!updated.empty() || !removed.empty()) {
        std::string what = !updated.empty() ? "Updated " + core::join(updated, ", ")
                                            : "Removed " + core::join(removed, ", ");
        if (!updated.empty() && !removed.empty()) what += "; removed " + core::join(removed, ", ");
        app->folderSyncStatus = what + " at " + clockTime();
      }
      pumpSyncQueue(generation);
    });
  });
}

// ---- URL capture ----------------------------------------------------------

void appendUtf8(std::string* out, uint32_t cp) {
  if (cp < 0x80) {
    out->push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out->push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out->push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x110000) {
    out->push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

std::string decodeEntities(const std::string& s) {
  static const std::map<std::string, uint32_t> kNamed = {
      {"amp", '&'},       {"lt", '<'},        {"gt", '>'},        {"quot", '"'},
      {"apos", '\''},     {"nbsp", ' '},      {"ndash", 0x2013},  {"mdash", 0x2014},
      {"hellip", 0x2026}, {"lsquo", 0x2018},  {"rsquo", 0x2019},  {"ldquo", 0x201C},
      {"rdquo", 0x201D},  {"copy", 0xA9},     {"reg", 0xAE},      {"trade", 0x2122},
      {"middot", 0xB7},   {"bull", 0x2022},   {"times", 0xD7},    {"deg", 0xB0},
      {"laquo", 0xAB},    {"raquo", 0xBB},    {"euro", 0x20AC},   {"pound", 0xA3},
  };
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] != '&') {
      out.push_back(s[i]);
      continue;
    }
    const size_t semi = s.find(';', i);
    if (semi == std::string::npos || semi - i > 10) {
      out.push_back('&');
      continue;
    }
    const std::string name = s.substr(i + 1, semi - i - 1);
    uint32_t cp = 0;
    if (!name.empty() && name[0] == '#') {
      const bool hex = name.size() > 1 && (name[1] == 'x' || name[1] == 'X');
      cp = static_cast<uint32_t>(std::strtoul(name.c_str() + (hex ? 2 : 1), nullptr, hex ? 16 : 10));
    } else {
      auto it = kNamed.find(name);
      if (it != kNamed.end()) cp = it->second;
    }
    if (cp == 0) {
      out.push_back('&');
      continue;
    }
    if (cp == 0xA0) cp = ' ';
    appendUtf8(&out, cp);
    i = semi;
  }
  return out;
}

std::string resolveUrl(const std::string& base, const std::string& href) {
  if (href.find("://") != std::string::npos) return href;
  const size_t schemeEnd = base.find("://");
  if (schemeEnd == std::string::npos) return href;
  if (core::startsWith(href, "//")) return base.substr(0, schemeEnd + 1) + href;
  const size_t hostEnd = base.find('/', schemeEnd + 3);
  const std::string origin = hostEnd == std::string::npos ? base : base.substr(0, hostEnd);
  if (core::startsWith(href, "/")) return origin + href;
  std::string dir = base.substr(0, base.find_first_of("?#"));
  const size_t slash = dir.find_last_of('/');
  dir = slash != std::string::npos && slash > schemeEnd + 2 ? dir.substr(0, slash + 1) : origin + "/";
  return dir + href;
}

std::string attrValue(const std::string& attrs, const std::string& name) {
  const std::string lowered = core::lower(attrs);
  size_t pos = 0;
  while ((pos = lowered.find(name, pos)) != std::string::npos) {
    const bool boundary = pos == 0 || std::isspace(static_cast<unsigned char>(lowered[pos - 1]));
    size_t p = pos + name.size();
    while (p < attrs.size() && std::isspace(static_cast<unsigned char>(attrs[p]))) ++p;
    if (!boundary || p >= attrs.size() || attrs[p] != '=') {
      pos += name.size();
      continue;
    }
    ++p;
    while (p < attrs.size() && std::isspace(static_cast<unsigned char>(attrs[p]))) ++p;
    if (p >= attrs.size()) return "";
    if (attrs[p] == '"' || attrs[p] == '\'') {
      const size_t end = attrs.find(attrs[p], p + 1);
      return decodeEntities(attrs.substr(p + 1, end == std::string::npos ? std::string::npos : end - p - 1));
    }
    size_t end = p;
    while (end < attrs.size() && !std::isspace(static_cast<unsigned char>(attrs[end]))) ++end;
    return decodeEntities(attrs.substr(p, end - p));
  }
  return "";
}

// A forgiving HTML -> Markdown pass: a hand-rolled tag scanner rather than a
// regex, because std::regex recurses per character and overflows the stack on
// real pages. Keeps headings, paragraphs, lists, links, code and preformatted
// blocks; drops scripts, styles, navigation and everything else decorative.
std::string htmlToMarkdown(const std::string& html, const std::string& baseUrl, std::string* title) {
  const std::string lowered = core::lower(html);
  title->clear();
  if (size_t t = lowered.find("<title"); t != std::string::npos) {
    const size_t open = lowered.find('>', t);
    const size_t close = lowered.find("</title", t);
    if (open != std::string::npos && close != std::string::npos && open < close) {
      *title = core::trim(decodeEntities(html.substr(open + 1, close - open - 1)));
    }
  }
  // Convert only the page's <main> (or <article>) when it has one, which
  // leaves the site's menus and sidebars behind; otherwise the whole <body>.
  size_t i = std::string::npos;
  size_t limit = html.size();
  for (const char* tag : {"main", "article"}) {
    const std::string open = std::string("<") + tag;
    for (size_t at = lowered.find(open); at != std::string::npos; at = lowered.find(open, at + 1)) {
      const char next = at + open.size() < lowered.size() ? lowered[at + open.size()] : '>';
      if (next != '>' && !std::isspace(static_cast<unsigned char>(next))) continue;
      const size_t close = lowered.rfind(std::string("</") + tag);
      if (close != std::string::npos && close > at) {
        i = at;
        limit = close;
      }
      break;
    }
    if (i != std::string::npos) break;
  }
  if (i == std::string::npos) i = lowered.find("<body");
  if (i == std::string::npos) i = 0;

  static const std::set<std::string> kSkip = {"script", "style", "noscript", "svg", "template",
                                              "iframe", "head",  "nav",      "aside", "footer",
                                              "form",   "button", "select",  "canvas", "object"};
  static const std::set<std::string> kBlock = {"p",       "div",    "section", "article", "main",
                                               "header",  "table",  "tr",      "ul",      "ol",
                                               "dl",      "dd",     "dt",      "figure",  "figcaption",
                                               "details", "summary", "blockquote", "caption"};
  std::string out;
  bool inPre = false;
  std::vector<std::pair<size_t, std::string>> links;   // output offset of '[', href

  auto endLine = [&](int newlines) {
    while (!out.empty() && out.back() == ' ') out.pop_back();
    int have = 0;
    for (size_t k = out.size(); k > 0 && out[k - 1] == '\n'; --k) ++have;
    if (out.empty()) return;
    for (int k = have; k < newlines; ++k) out.push_back('\n');
  };

  while (i < limit) {
    if (html[i] != '<') {
      const size_t next = html.find('<', i);
      const std::string text =
          decodeEntities(html.substr(i, next == std::string::npos ? std::string::npos : next - i));
      i = next == std::string::npos ? html.size() : next;
      if (inPre) {
        out += text;
        continue;
      }
      for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c))) {
          if (!out.empty() && out.back() != ' ' && out.back() != '\n') out.push_back(' ');
        } else {
          out.push_back(c);
        }
      }
      continue;
    }
    if (lowered.compare(i, 4, "<!--") == 0) {
      const size_t end = lowered.find("-->", i + 4);
      i = end == std::string::npos ? html.size() : end + 3;
      continue;
    }
    if (i + 1 < html.size() && (html[i + 1] == '!' || html[i + 1] == '?')) {
      const size_t end = html.find('>', i);
      i = end == std::string::npos ? html.size() : end + 1;
      continue;
    }
    // Tag: find its end, honouring quoted attribute values.
    size_t j = i + 1;
    const bool closing = j < html.size() && html[j] == '/';
    if (closing) ++j;
    const size_t nameStart = j;
    while (j < html.size() && (std::isalnum(static_cast<unsigned char>(html[j])) || html[j] == '-')) ++j;
    const std::string name = lowered.substr(nameStart, j - nameStart);
    char quote = 0;
    size_t end = j;
    for (; end < html.size(); ++end) {
      const char c = html[end];
      if (quote) {
        if (c == quote) quote = 0;
      } else if (c == '"' || c == '\'') {
        quote = c;
      } else if (c == '>') {
        break;
      }
    }
    if (name.empty()) {   // a stray '<' in text
      out.push_back('<');
      ++i;
      continue;
    }
    const std::string attrs = html.substr(j, end - j);
    i = end == html.size() ? end : end + 1;

    if (!closing && kSkip.count(name)) {
      const size_t close = lowered.find("</" + name, i);
      if (close == std::string::npos) break;
      const size_t gt = lowered.find('>', close);
      i = gt == std::string::npos ? html.size() : gt + 1;
      continue;
    }
    if (name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6') {
      endLine(2);
      if (!closing) out += std::string(name[1] - '0', '#') + " ";
      continue;
    }
    if (name == "pre") {
      endLine(2);
      out += closing ? "```" : "```\n";
      if (closing) endLine(2);
      inPre = !closing;
      continue;
    }
    if (name == "code" && !inPre) {
      out.push_back('`');
      continue;
    }
    if (name == "br") {
      if (inPre) out.push_back('\n');
      else endLine(1);
      continue;
    }
    if (name == "hr") {
      endLine(2);
      out += "---";
      endLine(2);
      continue;
    }
    if (name == "li" && !closing) {
      endLine(1);
      out += "- ";
      continue;
    }
    if (name == "td" || name == "th") {
      if (!out.empty() && out.back() != ' ' && out.back() != '\n') out.push_back(' ');
      continue;
    }
    if (name == "a") {
      if (!closing) {
        std::string href = attrValue(attrs, "href");
        if (href.empty() || href[0] == '#' || core::startsWith(core::lower(href), "javascript:")) {
          href.clear();
        }
        links.emplace_back(out.size(), href.empty() ? "" : resolveUrl(baseUrl, href));
      } else if (!links.empty()) {
        auto [start, href] = links.back();
        links.pop_back();
        while (!out.empty() && out.back() == ' ') out.pop_back();
        if (!href.empty() && start < out.size() &&
            out.find('\n', start) == std::string::npos) {
          while (start < out.size() && out[start] == ' ') ++start;
          out.insert(start, "[");
          out += "](" + href + ")";
        }
      }
      continue;
    }
    if (kBlock.count(name)) endLine(2);
  }

  // Tidy: trim each line, collapse runs of blank lines.
  std::string tidy;
  int blank = 0;
  for (const std::string& raw : core::split(out, '\n')) {
    std::string line = raw;
    while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) line.pop_back();
    if (core::trim(line).empty()) {
      if (++blank == 1 && !tidy.empty()) tidy += "\n";
      continue;
    }
    blank = 0;
    tidy += line + "\n";
  }
  return core::trim(tidy);
}

std::string fileNameFromTitle(const std::string& title, const std::string& url) {
  std::string name;
  for (char c : title) {
    if (std::strchr("/\\:*?\"<>|", c) || static_cast<unsigned char>(c) < 0x20) c = '-';
    if (c == ' ' && (name.empty() || name.back() == ' ')) continue;
    name.push_back(c);
  }
  name = core::trim(name);
  if (name.empty()) {
    const size_t start = url.find("://");
    name = url.substr(start == std::string::npos ? 0 : start + 3);
    name = name.substr(0, name.find_first_of("/?#"));
  }
  if (name.size() > 80) {
    size_t cut = 80;
    while (cut > 0 && (static_cast<unsigned char>(name[cut]) & 0xC0) == 0x80) --cut;
    name = core::trim(name.substr(0, cut));
  }
  return name + ".md";
}

// ---- citations --------------------------------------------------------------

std::string gCiteFileId;
std::string gCiteFileText;
std::string gCiteFileError;
bool gCiteFileLoading = false;
bool gCiteShowFile = false;
// Frames left to keep scrolling to the excerpt: the first frame that shows a
// freshly loaded file has no content height yet, so one request gets clamped.
int gCiteScrollFrames = 0;

const Json& jsonAt(const Json& arr, int i) {
  static const Json kNull;
  if (!arr.is_array() || i < 0 || i >= static_cast<int>(arr.size())) return kNull;
  return arr[static_cast<size_t>(i)];
}

std::string jsonString(const Json& obj, const char* key) {
  if (!obj.is_object() || !obj.contains(key)) return "";
  const Json& v = obj[key];
  return v.is_string() ? v.get<std::string>() : (v.is_null() ? "" : v.dump());
}

bool isWebUrl(const std::string& s) {
  return core::startsWith(s, "http://") || core::startsWith(s, "https://");
}

// ---- local index ----------------------------------------------------------

constexpr const char* kEmbedModel = "nomic-embed-text";
constexpr size_t kChunkChars = 800;
constexpr size_t kOverlapChars = 160;
constexpr size_t kEmbedBatch = 16;

std::atomic<bool> gIndexRunning{false};
std::atomic<bool> gIndexCancel{false};
std::atomic<int> gIndexDone{0};
std::atomic<int> gIndexTotal{0};
int gIndexChunks = -1;   // -1 until counted
int gIndexFiles = 0;
bool gIndexLoaded = false;

struct Hit {
  std::string path;
  std::string text;
  float score = 0.0f;
};
std::string gQuery;
std::vector<Hit> gHits;
bool gSearching = false;
std::string gSearchError;

std::string indexPath() { return core::supportDir() + "/local-index.db"; }

struct IndexDb {
  sqlite3* db = nullptr;
  IndexDb() {
    if (sqlite3_open(indexPath().c_str(), &db) != SQLITE_OK) {
      sqlite3_close(db);
      db = nullptr;
      return;
    }
    // WAL so a search can read while the indexer writes.
    exec("PRAGMA journal_mode=WAL; PRAGMA busy_timeout=5000;"
         "CREATE TABLE IF NOT EXISTS chunks(path TEXT NOT NULL, chunk INTEGER NOT NULL,"
         " text TEXT NOT NULL, mtime INTEGER NOT NULL, vector BLOB,"
         " PRIMARY KEY(path, chunk));");
  }
  ~IndexDb() {
    if (db) sqlite3_close(db);
  }
  IndexDb(const IndexDb&) = delete;
  IndexDb& operator=(const IndexDb&) = delete;
  explicit operator bool() const { return db != nullptr; }
  bool exec(const char* sql) { return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK; }
};

struct Statement {
  sqlite3_stmt* stmt = nullptr;
  Statement(sqlite3* db, const char* sql) { sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr); }
  ~Statement() { sqlite3_finalize(stmt); }
  Statement(const Statement&) = delete;
  Statement& operator=(const Statement&) = delete;
  explicit operator bool() const { return stmt != nullptr; }
};

std::string columnText(sqlite3_stmt* stmt, int col) {
  const unsigned char* text = sqlite3_column_text(stmt, col);
  return text ? reinterpret_cast<const char*>(text) : "";
}

// Splits on line boundaries into ~800-char chunks, carrying the last few lines
// of each chunk into the next so a definition is not cut off from its context.
std::vector<std::string> chunkText(const std::string& text) {
  std::vector<std::string> lines;
  for (std::string line : core::split(text, '\n')) {
    while (line.size() > kChunkChars) {
      lines.push_back(line.substr(0, kChunkChars));
      line.erase(0, kChunkChars);
    }
    lines.push_back(line);
  }
  std::vector<std::string> chunks;
  std::vector<std::string> current;
  size_t size = 0;
  auto flush = [&] {
    std::string chunk = core::join(current, "\n");
    if (!core::trim(chunk).empty()) chunks.push_back(chunk);
    std::vector<std::string> carry;
    size_t carried = 0;
    for (auto it = current.rbegin(); it != current.rend(); ++it) {
      if (carried + it->size() + 1 > kOverlapChars) break;
      carried += it->size() + 1;
      carry.insert(carry.begin(), *it);
    }
    current = carry;
    size = carried;
  };
  for (const std::string& line : lines) {
    if (size + line.size() + 1 > kChunkChars && size > kOverlapChars) flush();
    current.push_back(line);
    size += line.size() + 1;
  }
  if (size > 0) {
    std::string chunk = core::join(current, "\n");
    if (!core::trim(chunk).empty() && (chunks.empty() || size > kOverlapChars)) chunks.push_back(chunk);
  }
  return chunks;
}

std::string ollamaBase() {
  std::string url = core::Settings::instance().ollamaUrl;
  while (!url.empty() && url.back() == '/') url.pop_back();
  return url;
}

void normalize(std::vector<float>* v) {
  double norm = 0.0;
  for (float x : *v) norm += static_cast<double>(x) * x;
  norm = std::sqrt(norm);
  if (norm > 0.0) {
    for (float& x : *v) x = static_cast<float>(x / norm);
  }
}

// Blocking. nomic-embed-text expects task prefixes ("search_document: " /
// "search_query: "); callers add them.
bool embed(const std::string& base, const std::vector<std::string>& inputs,
           std::vector<std::vector<float>>* out, std::string* error) {
  net::Request req;
  req.method = "POST";
  req.url = base + "/api/embed";
  req.body = Json{{"model", kEmbedModel}, {"input", inputs}, {"truncate", true}}.dump();
  req.timeoutSec = 180;
  req.connectTimeoutSec = 5;
  net::Response res = net::perform(req);
  Json body = Json::parse(res.body, nullptr, false);
  if (!res.ok()) {
    *error = !res.error.empty() ? res.error
             : (body.is_object() && body.contains("error")) ? jsonString(body, "error")
                                                             : "HTTP " + std::to_string(res.status);
    return false;
  }
  if (!body.is_object() || !body.contains("embeddings") || !body["embeddings"].is_array() ||
      body["embeddings"].size() != inputs.size()) {
    *error = "Ollama returned no embeddings";
    return false;
  }
  out->clear();
  for (const Json& row : body["embeddings"]) {
    std::vector<float> v;
    v.reserve(row.size());
    for (const Json& x : row) v.push_back(x.get<float>());
    normalize(&v);
    out->push_back(std::move(v));
  }
  return true;
}

// Blocking: embeds the query, then a cosine scan over every stored chunk.
// Vectors are stored unit-length, so cosine is a dot product.
std::vector<Hit> searchIndex(const std::string& query, int k, std::string* error) {
  std::vector<Hit> hits;
  if (core::trim(query).empty() || k <= 0) return hits;
  std::vector<std::vector<float>> qv;
  if (!embed(ollamaBase(), {"search_query: " + query}, &qv, error)) return hits;
  const std::vector<float>& q = qv.front();
  IndexDb db;
  if (!db) {
    *error = "cannot open " + indexPath();
    return hits;
  }
  Statement st(db.db, "SELECT path, text, vector FROM chunks");
  if (!st) {
    *error = sqlite3_errmsg(db.db);
    return hits;
  }
  while (sqlite3_step(st.stmt) == SQLITE_ROW) {
    const void* blob = sqlite3_column_blob(st.stmt, 2);
    const int bytes = sqlite3_column_bytes(st.stmt, 2);
    if (!blob || bytes != static_cast<int>(q.size() * sizeof(float))) continue;
    const float* v = static_cast<const float*>(blob);
    float dot = 0.0f;
    for (size_t d = 0; d < q.size(); ++d) dot += q[d] * v[d];
    hits.push_back({columnText(st.stmt, 0), columnText(st.stmt, 1), dot});
  }
  const size_t keep = std::min(hits.size(), static_cast<size_t>(k));
  std::partial_sort(hits.begin(), hits.begin() + static_cast<long>(keep), hits.end(),
                    [](const Hit& a, const Hit& b) { return a.score > b.score; });
  hits.resize(keep);
  return hits;
}

void refreshIndexStats() {
  async::run([] {
    int chunks = 0, files = 0;
    IndexDb db;
    if (db) {
      Statement st(db.db, "SELECT COUNT(*), COUNT(DISTINCT path) FROM chunks");
      if (st && sqlite3_step(st.stmt) == SQLITE_ROW) {
        chunks = sqlite3_column_int(st.stmt, 0);
        files = sqlite3_column_int(st.stmt, 1);
      }
    }
    async::post([chunks, files] {
      gIndexChunks = chunks;
      gIndexFiles = files;
    });
  });
}

void persistLocalIndexSettings(const App& app) {
  core::Settings& settings = core::Settings::instance();
  settings.extra["localIndexFolder"] = app.localIndexFolder;
  settings.extra["localIndexEnabled"] = app.localIndexEnabled;
  settings.save();
}

void wrappedDim(const std::string& text) {
  ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(theme::palette().textDim));
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextUnformatted(text.c_str(), text.c_str() + text.size());
  ImGui::PopTextWrapPos();
  ImGui::PopStyleColor();
}

}  // namespace

// ---- folder sync ------------------------------------------------------------

void App::syncFolderToKnowledge(const std::string& folder, const std::string& knowledgeId) {
  if (folder.empty() || knowledgeId.empty()) {
    widgets::toastError("Choose a folder and a knowledge base to sync");
    return;
  }
  stopFolderSync();
  const std::string root = canonicalFolder(folder);
  const int generation = ++gSyncGeneration;
  folderSyncPath = root;
  folderSyncKnowledgeId = knowledgeId;
  folderSyncStatus = "Scanning " + root + "…";
  gSyncBusy = true;
  gSyncMap.clear();
  gSyncFilter.reset();
  SyncMap prior = loadSyncMap(knowledgeId, root);

  async::run([this, root, knowledgeId, generation, prior]() mutable {
    auto filter = std::make_shared<FileFilter>();
    const std::vector<std::string> files = eligibleFiles(root, filter.get());
    const std::set<std::string> present(files.begin(), files.end());
    SyncMap map;
    int uploaded = 0, unchanged = 0, removed = 0, failed = 0;
    std::string lastError;

    // Files that were synced before but are gone now leave the KB too.
    for (const auto& [path, file] : prior) {
      if (present.count(path)) {
        map[path] = file;
      } else {
        api_.removeFileFromKnowledge(knowledgeId, file.id);
        ++removed;
      }
    }
    // No map for this folder (first sync here, or settings were reset): adopt
    // the KB's files that carry our names, so identical ones are not uploaded
    // twice (the server rejects duplicate content) and stale ones get replaced.
    if (prior.empty()) {
      std::map<std::string, std::vector<SyncedFile>> remote;
      size_t seen = 0;
      for (int page = 1; page <= 100; ++page) {
        api::Result res = api_.get("/api/v1/knowledge/" + knowledgeId + "/files?page=" + std::to_string(page));
        if (!res.ok || !res.data.is_object() || !res.data.contains("items") ||
            !res.data["items"].is_array() || res.data["items"].empty()) {
          break;
        }
        for (const Json& item : res.data["items"]) {
          const Json& meta = item.contains("meta") ? item["meta"] : Json();
          remote[jsonString(item, "filename")].push_back({jsonString(item, "id"), jsonString(meta, "file_hash")});
          ++seen;
        }
        if (seen >= res.data.value("total", static_cast<size_t>(0))) break;
      }
      for (const std::string& path : files) {
        auto it = remote.find(syncFileName(root, path));
        if (it == remote.end()) continue;
        std::string bytes;
        const std::string hash = readTextFile(path, &bytes) ? contentHash(bytes) : "";
        const auto& candidates = it->second;
        auto same = std::find_if(candidates.begin(), candidates.end(),
                                 [&](const SyncedFile& f) { return f.hash == hash; });
        map[path] = same != candidates.end() ? *same : candidates.front();
      }
    }
    for (size_t i = 0; i < files.size(); ++i) {
      if (generation != gSyncGeneration) break;
      const std::string& path = files[i];
      std::string bytes;
      if (!readTextFile(path, &bytes) || core::trim(bytes).empty()) continue;
      const std::string hash = contentHash(bytes);
      auto it = map.find(path);
      if (it != map.end() && it->second.hash == hash) {
        ++unchanged;
        continue;
      }
      const std::string status = "Uploading " + std::to_string(i + 1) + "/" +
                                 std::to_string(files.size()) + ": " + relativeTo(root, path);
      async::post([this, generation, status] {
        if (generation == gSyncGeneration) folderSyncStatus = status;
      });
      if (it != map.end()) api_.removeFileFromKnowledge(knowledgeId, it->second.id);
      std::string error;
      const std::string id = uploadToKnowledge(api_, knowledgeId, syncFileName(root, path),
                                               uploadMime(path), bytes, &error);
      if (id.empty()) {
        map.erase(path);
        ++failed;
        lastError = relativeTo(root, path) + ": " + error;
      } else {
        map[path] = {id, hash};
        ++uploaded;
      }
    }

    async::post([this, root, knowledgeId, generation, map, filter, uploaded, unchanged, removed,
                 failed, lastError, total = files.size()] {
      saveSyncMap(knowledgeId, root, map);
      if (generation != gSyncGeneration) return;
      gSyncMap = map;
      gSyncFilter = filter;
      gSyncBusy = false;
      std::string status = "Watching " + std::to_string(total) + " files · " +
                           std::to_string(uploaded) + " uploaded";
      if (unchanged) status += ", " + std::to_string(unchanged) + " unchanged";
      if (removed) status += ", " + std::to_string(removed) + " removed";
      if (failed) status += ", " + std::to_string(failed) + " failed (" + lastError + ")";
      folderSyncStatus = status;
      folderSyncToken = native::watchFolder(root, [generation](const std::vector<std::string>& paths) {
        if (generation != gSyncGeneration) return;
        gSyncPending.insert(paths.begin(), paths.end());
        pumpSyncQueue(generation);
      });
      pumpSyncQueue(generation);
    });
  });
}

void App::stopFolderSync() {
  ++gSyncGeneration;
  if (folderSyncToken >= 0) native::stopWatching(folderSyncToken);
  folderSyncToken = -1;
  gSyncPending.clear();
  gSyncBusy = false;
  if (!folderSyncPath.empty()) folderSyncStatus = "Sync stopped";
}

// ---- URL capture ------------------------------------------------------------

void App::captureUrlToKnowledge(const std::string& rawUrl, const std::string& knowledgeId) {
  std::string url = core::trim(rawUrl);
  if (url.empty() || knowledgeId.empty()) {
    widgets::toastError("Enter a URL and choose a knowledge base");
    return;
  }
  if (url.find("://") == std::string::npos) url = "https://" + url;
  widgets::toastInfo("Capturing " + url);

  async::run([this, url, knowledgeId] {
    net::Request req;
    req.url = url;
    req.timeoutSec = 45;
    req.headers.emplace_back("Accept", "text/html,application/xhtml+xml,text/plain;q=0.9,*/*;q=0.5");
    net::Response res = net::perform(req);

    std::string title, body, why;
    if (!res.ok()) {
      why = !res.error.empty() ? res.error : "HTTP " + std::to_string(res.status);
    } else {
      const std::string type = core::lower(res.header("content-type"));
      const std::string head = core::lower(core::trim(res.body.substr(0, 512)));
      if (type.find("html") != std::string::npos || core::startsWith(head, "<!doctype html") ||
          core::startsWith(head, "<html")) {
        body = htmlToMarkdown(res.body, url, &title);
      } else if (core::startsWith(type, "text/") || type.find("json") != std::string::npos) {
        body = res.body;
      } else {
        why = "not a text page (" + type + ")";
      }
    }
    // Pages that build themselves in JavaScript come back nearly empty, and
    // some sites refuse non-browser clients; the server's web loader may still
    // manage them. Its text goes into the same visible .md file, rather than
    // letting it write bare vectors into the KB's collection.
    if (why.empty() && core::trim(body).size() < 200) why = "the page has almost no readable text";
    std::string via;
    if (!why.empty()) {
      api::Result web = api_.processWeb(url);
      const std::string text = web.ok && web.data.is_object() ? jsonString(web.data, "content") : "";
      // Ours keeps headings and links, so the loader's flat text has to bring
      // clearly more before it wins.
      if (core::trim(text).size() > core::trim(body).size() * 2 + 100) {
        body = text;
        via = " through the server's web loader";
      } else if (!web.ok) {
        why += "; server web loader: " + api::errorMessage(web);
      }
    }
    if (core::trim(body).empty()) {
      async::post([why] { widgets::toastError("Capture failed: " + why); });
      return;
    }

    if (title.empty()) title = url;
    const std::string name = fileNameFromTitle(title, url);
    const std::string doc = "# " + title + "\n\nSource: <" + url + ">  \nCaptured: " +
                            core::formatTime(core::nowSeconds()) + "\n\n" + body + "\n";
    std::string error;
    const std::string id = uploadToKnowledge(api_, knowledgeId, name, "text/markdown", doc, &error);
    async::post([name, id, error, via] {
      if (!id.empty()) {
        widgets::toast(widgets::ToastLevel::Success, "Added " + name + " to the knowledge base" + via);
      } else {
        widgets::toastError("Capture failed: " + error);
      }
    });
  });
}

// ---- citations ----------------------------------------------------------------

void App::openCitation(const Json& source, int chunkIndex) {
  citationPopup = source;
  citationChunk = chunkIndex;
  showCitation = true;
  gCiteShowFile = false;
}

void App::drawCitationPopup() {
  const char* popupId = "Citation##citationPopup";
  if (!showCitation) return;
  if (!ImGui::IsPopupOpen(popupId)) ImGui::OpenPopup(popupId);

  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(std::min(780.0f, viewport->Size.x - 80.0f),
                                  std::min(580.0f, viewport->Size.y - 80.0f)),
                           ImGuiCond_Appearing);
  bool open = true;
  if (!ImGui::BeginPopupModal(popupId, &open, ImGuiWindowFlags_NoSavedSettings)) {
    showCitation = false;
    return;
  }

  const Json& source = citationPopup.is_object() && citationPopup.contains("source")
                           ? citationPopup["source"] : citationPopup;
  const Json& documents = citationPopup.is_object() && citationPopup.contains("document")
                              ? citationPopup["document"] : Json();
  const Json& metadata = citationPopup.is_object() && citationPopup.contains("metadata")
                             ? citationPopup["metadata"] : Json();
  const Json& distances = citationPopup.is_object() && citationPopup.contains("distances")
                              ? citationPopup["distances"] : Json();
  const int count = documents.is_array() ? static_cast<int>(documents.size()) : 0;
  citationChunk = count > 0 ? std::clamp(citationChunk, 0, count - 1) : 0;
  const Json& doc = jsonAt(documents, citationChunk);
  const Json& meta = jsonAt(metadata, citationChunk);
  const std::string chunk = doc.is_string() ? doc.get<std::string>() : (doc.is_null() ? "" : doc.dump(2));

  std::string name = jsonString(source, "name");
  if (name.empty()) name = jsonString(meta, "name");
  if (name.empty()) name = jsonString(meta, "source");
  if (name.empty()) name = "Source";
  std::string url;
  for (const std::string& candidate :
       {jsonString(meta, "source"), jsonString(source, "url"), jsonString(source, "name")}) {
    if (isWebUrl(candidate)) {
      url = candidate;
      break;
    }
  }
  std::string fileId = jsonString(meta, "file_id");
  if (fileId.empty() && jsonString(source, "type") == "file") fileId = jsonString(source, "id");

  theme::pushBold();
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextUnformatted(name.c_str());
  ImGui::PopTextWrapPos();
  theme::pop();
  std::string info = count > 0 ? "Excerpt " + std::to_string(citationChunk + 1) + " of " +
                                     std::to_string(count)
                               : "No excerpt text";
  if (meta.is_object() && meta.contains("page") && meta["page"].is_number_integer()) {
    info += " · page " + std::to_string(meta["page"].get<int>() + 1);
  }
  const Json& distance = jsonAt(distances, citationChunk);
  if (distance.is_number()) {
    char buf[32];
    snprintf(buf, sizeof(buf), " · relevance %.2f", distance.get<double>());
    info += buf;
  }
  widgets::dimText("%s", info.c_str());

  // The flags are captured once: a click changes citationChunk, and
  // endDisabled must match the beginDisabled that preceded it.
  const bool atFirst = citationChunk <= 0;
  const bool atLast = citationChunk >= count - 1;
  widgets::beginDisabled(atFirst);
  if (widgets::ghostButton("< Previous")) {
    --citationChunk;
    gCiteScrollFrames = 3;
  }
  widgets::endDisabled(atFirst);
  ImGui::SameLine();
  widgets::beginDisabled(atLast);
  if (widgets::ghostButton("Next >")) {
    ++citationChunk;
    gCiteScrollFrames = 3;
  }
  widgets::endDisabled(atLast);
  ImGui::SameLine();
  if (widgets::ghostButton("Copy excerpt")) platform::setClipboardText(chunk);
  if (!url.empty()) {
    ImGui::SameLine();
    if (widgets::primaryButton("Open source")) platform::openUrl(url);
  } else if (!fileId.empty()) {
    ImGui::SameLine();
    if (gCiteShowFile) {
      if (widgets::primaryButton("Show excerpt only")) gCiteShowFile = false;
    } else if (widgets::primaryButton("Open source")) {
      gCiteShowFile = true;
      gCiteScrollFrames = 3;
      if (gCiteFileId != fileId) {
        gCiteFileId = fileId;
        gCiteFileText.clear();
        gCiteFileError.clear();
        gCiteFileLoading = true;
        async::run([this, fileId] {
          // Extracted text first: it works for PDFs and Office files, where
          // the raw /content would be binary.
          std::string text, error;
          api::Result res = api_.get("/api/v1/files/" + fileId + "/data/content");
          if (res.ok && res.data.is_object()) text = jsonString(res.data, "content");
          if (text.empty()) {
            std::string raw;
            if (api_.fetchBinary("/api/v1/files/" + fileId + "/content", &raw) &&
                std::memchr(raw.data(), '\0', std::min<size_t>(raw.size(), 1024)) == nullptr) {
              text = raw;
            } else {
              error = res.ok ? "the file has no text to show" : api::errorMessage(res);
            }
          }
          async::post([fileId, text, error] {
            if (gCiteFileId != fileId) return;
            gCiteFileLoading = false;
            gCiteFileText = text;
            gCiteFileError = error;
          });
        });
      }
    }
  }
  ImGui::Separator();

  const float footer = ImGui::GetFrameHeightWithSpacing() + 6.0f;
  ImGui::BeginChild("##citationBody", ImVec2(0, -footer), ImGuiChildFlags_None);
  ImGui::PushTextWrapPos(0.0f);
  if (gCiteShowFile && gCiteFileLoading) {
    widgets::loadingRow("Loading the source file…");
  } else if (gCiteShowFile && !gCiteFileError.empty()) {
    widgets::dimText("Could not load the file: %s", gCiteFileError.c_str());
  } else if (gCiteShowFile) {
    // Locate the excerpt; extraction can reflow whitespace, so fall back to
    // matching its opening words.
    size_t at = chunk.empty() ? std::string::npos : gCiteFileText.find(chunk);
    size_t len = chunk.size();
    if (at == std::string::npos && !chunk.empty()) {
      const std::string probe = core::trim(chunk).substr(0, 60);
      at = probe.empty() ? std::string::npos : gCiteFileText.find(probe);
      len = std::min(chunk.size(), at == std::string::npos ? 0 : gCiteFileText.size() - at);
    }
    if (at == std::string::npos) {
      widgets::dimText("The excerpt was not found verbatim in the file.");
      ImGui::TextUnformatted(gCiteFileText.c_str(), gCiteFileText.c_str() + gCiteFileText.size());
    } else {
      const char* text = gCiteFileText.c_str();
      if (at > 0) ImGui::TextUnformatted(text, text + at);
      ImDrawList* draw = ImGui::GetWindowDrawList();
      draw->ChannelsSplit(2);
      draw->ChannelsSetCurrent(1);
      ImGui::TextUnformatted(text + at, text + at + len);
      const ImVec2 min = ImGui::GetItemRectMin();
      const ImVec2 max = ImGui::GetItemRectMax();
      if (gCiteScrollFrames > 0) {
        // From the excerpt's own rect (SetScrollHereY would use the block
        // before it), with a few lines of context above.
        ImGui::SetScrollFromPosY(min.y - ImGui::GetWindowPos().y - 3.0f * ImGui::GetTextLineHeight(), 0.0f);
        --gCiteScrollFrames;
      }
      draw->ChannelsSetCurrent(0);
      const ImU32 tint = (theme::palette().accent & ~IM_COL32_A_MASK) | (0x40u << IM_COL32_A_SHIFT);
      draw->AddRectFilled(ImVec2(min.x - 4, min.y - 2), ImVec2(max.x + 4, max.y + 2), tint, 4.0f);
      draw->ChannelsMerge();
      if (at + len < gCiteFileText.size()) {
        ImGui::TextUnformatted(text + at + len, text + gCiteFileText.size());
      }
    }
  } else {
    if (gCiteScrollFrames > 0) {
      ImGui::SetScrollY(0.0f);
      gCiteScrollFrames = 0;
    }
    ImGui::TextUnformatted(chunk.c_str(), chunk.c_str() + chunk.size());
  }
  ImGui::PopTextWrapPos();
  ImGui::EndChild();

  if (widgets::ghostButton("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    open = false;
  }
  if (!open) {
    ImGui::CloseCurrentPopup();
    showCitation = false;
  }
  ImGui::EndPopup();
}

// ---- local index ------------------------------------------------------------

void App::indexFolderLocally(const std::string& folder) {
  if (folder.empty()) {
    widgets::toastError("Choose a folder to index");
    return;
  }
  if (gIndexRunning) return;
  const std::string root = canonicalFolder(folder);
  localIndexFolder = root;
  persistLocalIndexSettings(*this);
  gIndexRunning = true;
  gIndexCancel = false;
  gIndexDone = 0;
  gIndexTotal = 0;
  localIndexStatus = "Scanning " + root + "…";
  const std::string base = ollamaBase();

  async::run([this, root, base] {
    const double started = core::nowMillis();
    auto finish = [this](const std::string& status) {
      async::post([this, status] {
        gIndexRunning = false;
        localIndexStatus = status;
        refreshIndexStats();
      });
    };
    FileFilter filter;
    const std::vector<std::string> files = eligibleFiles(root, &filter);
    gIndexTotal = static_cast<int>(files.size());
    IndexDb db;
    if (!db) {
      finish("Cannot open " + indexPath());
      return;
    }

    // What is already indexed, so unchanged files are skipped.
    std::map<std::string, int64_t> indexed;
    {
      Statement st(db.db, "SELECT path, MIN(mtime) FROM chunks GROUP BY path");
      while (st && sqlite3_step(st.stmt) == SQLITE_ROW) {
        indexed[columnText(st.stmt, 0)] = sqlite3_column_int64(st.stmt, 1);
      }
    }
    // The index covers one folder: drop rows for anything not in this walk.
    const std::set<std::string> present(files.begin(), files.end());
    {
      Statement del(db.db, "DELETE FROM chunks WHERE path = ?");
      for (const auto& [path, mtime] : indexed) {
        if (present.count(path)) continue;
        sqlite3_bind_text(del.stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(del.stmt);
        sqlite3_reset(del.stmt);
      }
    }

    int updatedFiles = 0, totalChunks = 0, newChunks = 0;
    for (size_t i = 0; i < files.size(); ++i) {
      if (gIndexCancel) {
        finish("Stopped after " + std::to_string(i) + " of " + std::to_string(files.size()) + " files");
        return;
      }
      const std::string& path = files[i];
      gIndexDone = static_cast<int>(i);
      struct stat st {};
      if (::stat(path.c_str(), &st) != 0) continue;
      const int64_t mtime = static_cast<int64_t>(st.st_mtimespec.tv_sec);
      auto known = indexed.find(path);
      if (known != indexed.end() && known->second == mtime) {
        continue;
      }
      std::string text;
      if (!readTextFile(path, &text)) continue;
      const std::string rel = relativeTo(root, path);
      const std::vector<std::string> chunks = chunkText(text);
      // The path goes into the embedded text: queries often name the thing
      // a file is about ("the socket.io transport") more than its contents.
      std::vector<std::vector<float>> vectors;
      for (size_t b = 0; b < chunks.size(); b += kEmbedBatch) {
        std::vector<std::string> inputs;
        for (size_t c = b; c < std::min(chunks.size(), b + kEmbedBatch); ++c) {
          inputs.push_back("search_document: " + rel + "\n" + chunks[c]);
        }
        std::vector<std::vector<float>> batch;
        std::string error;
        if (!embed(base, inputs, &batch, &error)) {
          finish("Embedding failed (" + error + ") — check that Ollama at " + base + " has " +
                 kEmbedModel);
          return;
        }
        for (auto& v : batch) vectors.push_back(std::move(v));
      }
      db.exec("BEGIN");
      {
        Statement del(db.db, "DELETE FROM chunks WHERE path = ?");
        sqlite3_bind_text(del.stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(del.stmt);
        Statement ins(db.db, "INSERT INTO chunks(path, chunk, text, mtime, vector) VALUES(?,?,?,?,?)");
        for (size_t c = 0; c < chunks.size(); ++c) {
          sqlite3_bind_text(ins.stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
          sqlite3_bind_int(ins.stmt, 2, static_cast<int>(c));
          sqlite3_bind_text(ins.stmt, 3, chunks[c].c_str(), -1, SQLITE_TRANSIENT);
          sqlite3_bind_int64(ins.stmt, 4, mtime);
          sqlite3_bind_blob(ins.stmt, 5, vectors[c].data(),
                            static_cast<int>(vectors[c].size() * sizeof(float)), SQLITE_TRANSIENT);
          sqlite3_step(ins.stmt);
          sqlite3_reset(ins.stmt);
        }
      }
      db.exec("COMMIT");
      ++updatedFiles;
      newChunks += static_cast<int>(chunks.size());
      const std::string status = "Indexing " + std::to_string(i + 1) + "/" +
                                 std::to_string(files.size()) + ": " + rel;
      async::post([this, status] {
        if (gIndexRunning) localIndexStatus = status;
      });
    }
    gIndexDone = static_cast<int>(files.size());
    {
      Statement st(db.db, "SELECT COUNT(*) FROM chunks");
      if (st && sqlite3_step(st.stmt) == SQLITE_ROW) totalChunks = sqlite3_column_int(st.stmt, 0);
    }
    char secs[32];
    snprintf(secs, sizeof(secs), "%.1f", (core::nowMillis() - started) / 1000.0);
    finish("Indexed " + std::to_string(files.size()) + " files, " + std::to_string(totalChunks) +
           " chunks (" + std::to_string(updatedFiles) + " files re-embedded, " +
           std::to_string(newChunks) + " chunks) in " + secs + " s");
  });
}

// Blocking: embeds the query over the network. Call it from a worker.
std::vector<std::pair<std::string, std::string>> App::localSearch(const std::string& query, int k) {
  std::string error;
  std::vector<std::pair<std::string, std::string>> out;
  for (Hit& hit : searchIndex(query, k, &error)) out.emplace_back(std::move(hit.path), std::move(hit.text));
  if (!error.empty()) core::debugLog("localSearch: %s", error.c_str());
  return out;
}

void App::drawLocalIndexSection(float width) {
  if (!gIndexLoaded) {
    gIndexLoaded = true;
    const Json& extra = settings().extra;
    if (localIndexFolder.empty() && extra.contains("localIndexFolder") &&
        extra["localIndexFolder"].is_string()) {
      localIndexFolder = extra["localIndexFolder"].get<std::string>();
    }
    if (extra.contains("localIndexEnabled") && extra["localIndexEnabled"].is_boolean()) {
      localIndexEnabled = extra["localIndexEnabled"].get<bool>();
    }
    refreshIndexStats();
  }
  if (width <= 0.0f) width = ImGui::GetContentRegionAvail().x;
  const ImGuiStyle& style = ImGui::GetStyle();
  ImGui::PushID("localIndex");

  widgets::sectionHeader("LOCAL INDEX");
  wrappedDim("Files are chunked and embedded with " + std::string(kEmbedModel) + " on Ollama (" +
             ollamaBase() + "); the vectors stay in local-index.db on this Mac.");
  ImGui::Spacing();

  const bool running = gIndexRunning;
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(localIndexFolder.empty() ? "No folder chosen" : localIndexFolder.c_str());
  ImGui::SameLine();
  widgets::beginDisabled(running);
  if (widgets::ghostButton("Choose…")) {
    std::vector<std::string> picked = native::chooseFolder();
    if (!picked.empty()) {
      localIndexFolder = picked.front();
      persistLocalIndexSettings(*this);
    }
  }
  ImGui::SameLine();
  const bool noFolder = localIndexFolder.empty();
  widgets::beginDisabled(noFolder);
  if (widgets::primaryButton("Index")) indexFolderLocally(localIndexFolder);
  widgets::endDisabled(noFolder);
  widgets::endDisabled(running);

  if (running) {
    const int total = gIndexTotal;
    const float fraction = total > 0 ? static_cast<float>(gIndexDone) / static_cast<float>(total) : 0.0f;
    ImGui::ProgressBar(fraction, ImVec2(std::max(80.0f, width - 90.0f), 0), "");
    ImGui::SameLine();
    if (widgets::ghostButton("Stop")) gIndexCancel = true;
  }
  if (!localIndexStatus.empty()) wrappedDim(localIndexStatus);
  if (gIndexChunks >= 0) {
    widgets::dimText("%d chunks from %d files in the index", gIndexChunks, gIndexFiles);
  }

  ImGui::Spacing();
  auto runSearch = [] {
    const std::string query = gQuery;
    if (core::trim(query).empty() || gSearching) return;
    gSearching = true;
    gSearchError.clear();
    async::run([query] {
      std::string error;
      std::vector<Hit> hits = searchIndex(query, 6, &error);
      async::post([hits, error] {
        gSearching = false;
        gHits = hits;
        gSearchError = error;
      });
    });
  };
  ImGui::SetNextItemWidth(std::max(120.0f, width - 90.0f - style.ItemSpacing.x));
  if (ImGui::InputTextWithHint("##query", "Search the index…", &gQuery,
                               ImGuiInputTextFlags_EnterReturnsTrue)) {
    runSearch();
  }
  ImGui::SameLine();
  const bool searchOff = gSearching || gIndexChunks == 0;
  widgets::beginDisabled(searchOff);
  if (widgets::primaryButton("Search", ImVec2(90.0f, 0))) runSearch();
  widgets::endDisabled(searchOff);

  if (gSearching) widgets::loadingRow("Searching…");
  if (!gSearchError.empty()) wrappedDim("Search failed: " + gSearchError);

  if (!gHits.empty()) {
    if (widgets::ghostButton("Attach top hits to next message")) {
      std::string doc = "# Local index results for: " + gQuery + "\n\n";
      for (size_t i = 0; i < std::min<size_t>(gHits.size(), 4); ++i) {
        const Hit& hit = gHits[i];
        doc += "### " + relativeTo(localIndexFolder, hit.path) + "\n```" +
               core::extension(hit.path) + "\n" + hit.text + "\n```\n\n";
      }
      attachBytes("local-index-hits.md", "text/markdown", doc);
      widgets::toast(widgets::ToastLevel::Success, "Top hits attached to the composer");
    }
  }
  for (size_t i = 0; i < gHits.size(); ++i) {
    const Hit& hit = gHits[i];
    ImGui::PushID(static_cast<int>(i));
    widgets::Card card;
    card.begin();
    theme::pushBold();
    ImGui::TextUnformatted(relativeTo(localIndexFolder, hit.path).c_str());
    theme::pop();
    ImGui::SameLine();
    widgets::dimText("%.3f", hit.score);
    std::string preview = core::trim(hit.text);
    if (preview.size() > 420) {
      size_t cut = 420;
      while (cut > 0 && (static_cast<unsigned char>(preview[cut]) & 0xC0) == 0x80) --cut;
      preview = preview.substr(0, cut) + "…";
    }
    theme::pushMono();
    wrappedDim(preview);
    theme::pop();
    if (widgets::ghostButton("Copy")) platform::setClipboardText(hit.text);
    ImGui::SameLine();
    if (widgets::ghostButton("Reveal in Finder")) platform::revealInFinder(hit.path);
    card.end();
    ImGui::Spacing();
    ImGui::PopID();
  }

  ImGui::Spacing();
  if (ImGui::Checkbox("Add top index hits to every message", &localIndexEnabled)) {
    persistLocalIndexSettings(*this);
  }
  ImGui::PopID();
}

}  // namespace app
