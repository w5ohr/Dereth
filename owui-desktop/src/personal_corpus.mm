// Personal corpus (#106), the app side: indexes the sources switched on into
// personal-corpus.db (same chunking, embeddings and hybrid search as the local
// file index), offers a corpus__search tool to the model, and draws its
// section in Tools > Local index. Reading the real sources needs Full Disk
// Access; nothing is read until "Index now" is pressed.
#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>

#include <sqlite3.h>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <ctime>
#include <set>

#include "app.h"
#include "core/async.h"
#include "core/common.h"
#include "core/corpus.h"
#include "core/settings.h"
#include "imgui_stdlib.h"
#include "local_embed.h"
#include "pane_common.h"
#include "ui/a11y_imgui.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace corpus {

std::string libraryRoot() {
  // $HOME, not the passwd entry: a test run with a scratch HOME reads a scratch Library.
  const char* home = std::getenv("HOME");
  return std::string(home && *home ? home : "") + "/Library";
}

std::string dbPath() { return core::supportDir() + "/personal-corpus.db"; }

namespace {

Json toJson(id value) {
  if ([value isKindOfClass:[NSDictionary class]]) {
    Json o = Json::object();
    for (id key in (NSDictionary*)value) {
      if ([key isKindOfClass:[NSString class]]) o[[(NSString*)key UTF8String] ?: ""] = toJson(((NSDictionary*)value)[key]);
    }
    return o;
  }
  if ([value isKindOfClass:[NSArray class]]) {
    Json a = Json::array();
    for (id item in (NSArray*)value) a.push_back(toJson(item));
    return a;
  }
  if ([value isKindOfClass:[NSString class]]) return std::string([(NSString*)value UTF8String] ?: "");
  if ([value isKindOfClass:[NSDate class]]) return [(NSDate*)value timeIntervalSince1970];
  if ([value isKindOfClass:[NSNumber class]]) {
    NSNumber* n = value;
    if (strcmp(n.objCType, @encode(BOOL)) == 0 || strcmp(n.objCType, "c") == 0) return static_cast<bool>(n.boolValue);
    return n.doubleValue;
  }
  return nullptr;   // data and the rest are not needed
}

}  // namespace

ReadResult readReadingListFile(const std::string& library) {
  ReadResult r;
  const std::string path = bookmarksPath(library);
  FILE* probe = std::fopen(path.c_str(), "rb");
  if (!probe) {
    const int err = errno;
    if (err == EPERM || err == EACCES) {
      r.permission = true;
      r.error = "Reading List needs Full Disk Access: System Settings > Privacy & Security > Full Disk Access, then switch on Open WebUI.";
    } else {
      r.error = "Reading List: nothing at " + path;
    }
    return r;
  }
  std::fclose(probe);
  NSData* data = [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:path.c_str()]];
  NSError* error = nil;
  id plist = data ? [NSPropertyListSerialization propertyListWithData:data options:NSPropertyListImmutable format:nil error:&error] : nil;
  if (!plist) {
    r.error = "Reading List: Bookmarks.plist could not be read";
    return r;
  }
  return readReadingList(toJson(plist));
}

// The docs table beside chunks: what a hit came from.
bool storeDocMeta(const Doc& d) {
  sqlite3* db = nullptr;
  if (sqlite3_open(dbPath().c_str(), &db) != SQLITE_OK) {
    sqlite3_close(db);
    return false;
  }
  sqlite3_exec(db, "PRAGMA busy_timeout=5000; CREATE TABLE IF NOT EXISTS docs(id TEXT PRIMARY KEY, source TEXT, title TEXT, who TEXT, modified REAL);",
               nullptr, nullptr, nullptr);
  sqlite3_stmt* st = nullptr;
  sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO docs(id, source, title, who, modified) VALUES(?,?,?,?,?)", -1, &st, nullptr);
  sqlite3_bind_text(st, 1, d.id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 2, sourceName(d.source), -1, SQLITE_STATIC);
  sqlite3_bind_text(st, 3, d.title.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(st, 4, d.who.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_double(st, 5, d.modified);
  const bool ok = sqlite3_step(st) == SQLITE_DONE;
  sqlite3_finalize(st);
  sqlite3_close(db);
  return ok;
}

struct Meta {
  std::string source, title, who;
  double modified = 0;
};

std::map<std::string, Meta> loadMeta() {
  std::map<std::string, Meta> out;
  sqlite3* db = nullptr;
  if (sqlite3_open_v2(dbPath().c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
    sqlite3_close(db);
    return out;
  }
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(db, "SELECT id, source, title, who, modified FROM docs", -1, &st, nullptr) == SQLITE_OK) {
    while (sqlite3_step(st) == SQLITE_ROW) {
      auto text = [&](int i) {
        const unsigned char* t = sqlite3_column_text(st, i);
        return t ? std::string(reinterpret_cast<const char*>(t)) : std::string();
      };
      out[text(0)] = Meta{text(1), text(2), text(3), sqlite3_column_double(st, 4)};
    }
  }
  sqlite3_finalize(st);
  sqlite3_close(db);
  return out;
}

void forgetDoc(const std::string& id) {
  app::removeIndexed(dbPath(), id);
  sqlite3* db = nullptr;
  if (sqlite3_open(dbPath().c_str(), &db) == SQLITE_OK) {
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db, "DELETE FROM docs WHERE id = ?", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    sqlite3_finalize(st);
  }
  sqlite3_close(db);
}

std::string formatDate(double t) {
  if (t <= 0) return "";
  std::time_t tt = static_cast<std::time_t>(t);
  std::tm local{};
  localtime_r(&tt, &local);
  char buf[24];
  std::strftime(buf, sizeof buf, "%Y-%m-%d", &local);
  return buf;
}

// What the model (and the card's search box) gets for a query.
std::string search(const std::string& query, const std::string& onlySource, int k, std::string* error) {
  const std::map<std::string, Meta> meta = loadMeta();
  std::string err;
  std::vector<app::IndexHit> hits = app::searchIndexFile(dbPath(), query, onlySource.empty() ? k : k * 4, &err);
  std::string out;
  int shown = 0;
  for (const app::IndexHit& hit : hits) {
    auto m = meta.find(hit.path);
    const std::string source = m != meta.end() ? m->second.source : hit.path.substr(0, hit.path.find(':'));
    if (!onlySource.empty() && source != onlySource) continue;
    if (shown++ >= k) break;
    out += "[" + source + "] ";
    if (m != meta.end()) {
      out += m->second.title;
      if (!m->second.who.empty() && m->second.source == "mail") out += " - from " + m->second.who;
      const std::string date = formatDate(m->second.modified);
      if (!date.empty() && m->second.title.find(date) == std::string::npos) out += " (" + date + ")";
    } else {
      out += hit.path;
    }
    out += "\n" + core::trim(hit.text) + "\n\n";
  }
  if (out.empty()) {
    if (!err.empty()) *error = err;
    return "Nothing in the personal corpus matches that.";
  }
  if (!err.empty() && error) *error = "";   // partial (text-only) results still answer
  return out;
}

}  // namespace corpus

namespace app {
namespace {

std::atomic<bool> gCorpusRunning{false};
std::atomic<bool> gCorpusCancel{false};
std::string gCorpusStatus;
std::map<std::string, std::string> gSourceStatus;   // source name -> last result
std::set<std::string> gNeedsFda;
int gCorpusChunks = -1;
std::string gCorpusQuery;
std::string gCorpusResult;
bool gCorpusSearching = false;

void saveCorpusOptions(const corpus::Options& o) {
  core::Settings& cfg = core::Settings::instance();
  Json j = cfg.extra.value("personalCorpus", Json::object());
  if (!j.is_object()) j = Json::object();
  j["enabled"] = o.enabled;
  j["notes"] = o.notes;
  j["mail"] = o.mail;
  j["readingList"] = o.readingList;
  j["messages"] = o.messages;
  j["mailDays"] = o.mailDays;
  j["messagesDays"] = o.messagesDays;
  cfg.extra["personalCorpus"] = j;
  cfg.save();
}

}  // namespace

void App::indexPersonalCorpus() {
  if (gCorpusRunning) return;
  const corpus::Options o = corpus::optionsFromSettings();
  if (!o.any()) {
    widgets::toastError("Switch on at least one source first");
    return;
  }
  gCorpusRunning = true;
  gCorpusCancel = false;
  gCorpusStatus = "Reading...";
  async::run([o] {
    const double started = core::nowMillis();
    const std::string library = corpus::libraryRoot();
    const double now = static_cast<double>(std::time(nullptr));
    std::map<std::string, std::string> results;
    std::set<std::string> needsFda;
    std::vector<corpus::Doc> docs;
    std::set<std::string> readSources;   // sources that answered (their stale docs may go)
    auto take = [&](corpus::Source source, corpus::ReadResult r) {
      const std::string name = corpus::sourceName(source);
      if (!r.error.empty()) {
        results[name] = r.error;
        if (r.permission) needsFda.insert(name);
        return;
      }
      const size_t n = r.docs.size();
      results[name] = std::to_string(n) + (source == corpus::Source::Messages ? (n == 1 ? " conversation-day" : " conversation-days")
                                                                               : (n == 1 ? " item" : " items"));
      readSources.insert(name);
      for (auto& d : r.docs) docs.push_back(std::move(d));
    };
    if (o.notes) take(corpus::Source::Notes, corpus::readNotes(library, now));
    if (o.mail) take(corpus::Source::Mail, corpus::readMail(library, now, o.mailDays, o.maxMail));
    if (o.readingList) take(corpus::Source::ReadingList, corpus::readReadingListFile(library));
    if (o.messages) take(corpus::Source::Messages, corpus::readMessages(library, now, o.messagesDays, o.maxMessages));

    // Drop what a source that answered no longer has, and sources switched off.
    const std::map<std::string, int64_t> indexed = indexedPaths(corpus::dbPath());
    std::set<std::string> present;
    for (const auto& d : docs) present.insert(d.id);
    const std::set<std::string> enabled = [&] {
      std::set<std::string> s;
      if (o.notes) s.insert("notes");
      if (o.mail) s.insert("mail");
      if (o.readingList) s.insert("reading-list");
      if (o.messages) s.insert("messages");
      return s;
    }();
    int removed = 0;
    for (const auto& [id, mtime] : indexed) {
      const std::string source = id.substr(0, id.find(':'));
      if ((readSources.count(source) && !present.count(id)) || !enabled.count(source)) {
        corpus::forgetDoc(id);
        ++removed;
      }
    }
    int embedded = 0, skipped = 0;
    std::string failure;
    for (size_t i = 0; i < docs.size(); ++i) {
      if (gCorpusCancel) {
        failure = "Stopped after " + std::to_string(i) + " of " + std::to_string(docs.size());
        break;
      }
      const corpus::Doc& d = docs[i];
      const int64_t mtime = static_cast<int64_t>(d.modified);
      auto known = indexed.find(d.id);
      if (known != indexed.end() && known->second == mtime) {
        ++skipped;
        continue;
      }
      // The title leads the text unless the text already opens with it.
      const bool titled = d.text.compare(0, std::min(d.text.size(), static_cast<size_t>(400)), d.title) == 0 ||
                          d.text.substr(0, 400).find(d.title) != std::string::npos;
      const std::vector<std::string> chunks = chunkForIndex(titled ? d.text : d.title + "\n" + d.text);
      if (chunks.empty()) continue;
      std::string error;
      if (!indexDocument(corpus::dbPath(), d.id, mtime, std::string(corpus::sourceLabel(d.source)) + ": " + d.title, chunks, &error)) {
        failure = "Embedding failed (" + error + ") - check that Ollama has " + embedModelName();
        break;
      }
      corpus::storeDocMeta(d);
      ++embedded;
      if (i % 10 == 0) {
        const std::string status = "Indexing " + std::to_string(i + 1) + "/" + std::to_string(docs.size()) + ": " + corpus::sourceLabel(d.source);
        async::post([status] { gCorpusStatus = status; });
      }
    }
    char secs[32];
    std::snprintf(secs, sizeof secs, "%.1f", (core::nowMillis() - started) / 1000.0);
    const std::string summary = !failure.empty() ? failure
                                                 : std::to_string(docs.size()) + " items read, " + std::to_string(embedded) + " embedded, " +
                                                       std::to_string(skipped) + " unchanged, " + std::to_string(removed) + " removed in " + secs + " s";
    const int chunks = indexedChunkCount(corpus::dbPath());
    async::post([summary, results, needsFda, chunks] {
      gCorpusRunning = false;
      gCorpusStatus = summary;
      gSourceStatus = results;
      gNeedsFda = needsFda;
      gCorpusChunks = chunks;
    });
  });
}

std::vector<agents::ToolSpec> App::corpusToolSpecs() const {
  std::vector<agents::ToolSpec> out;
  const corpus::Options o = corpus::optionsFromSettings();
  if (!o.enabled) return out;
  agents::ToolSpec s;
  s.tool = "search";
  s.name = "corpus__search";
  s.serverId = "builtin:corpus";
  s.serverName = "corpus";
  s.description = "Search the user's own notes, email, reading list and text messages (indexed on this Mac) for passages about a topic.";
  s.parameters = Json{{"type", "object"},
                      {"properties",
                       {{"query", {{"type", "string"}, {"description", "what to look for, in a few words"}}},
                        {"source", {{"type", "string"}, {"enum", {"notes", "mail", "reading-list", "messages"}}, {"description", "only this source"}}},
                        {"limit", {{"type", "integer"}, {"description", "how many passages (default 6, at most 20)"}}}}},
                      {"required", {"query"}}};
  out.push_back(s);
  return out;
}

std::string App::callCorpusTool(const Json& args, std::string* error) {
  const std::string query = args.is_object() ? args.value("query", "") : "";
  if (core::trim(query).empty()) {
    *error = "search needs a query";
    return "";
  }
  const std::string source = args.is_object() ? args.value("source", "") : "";
  const int k = std::clamp(args.is_object() ? args.value("limit", 6) : 6, 1, 20);
  if (indexedChunkCount(corpus::dbPath()) == 0) {
    *error = "The personal corpus is empty. Ask the user to press \"Index now\" in Tools > Local index > Personal corpus.";
    return "";
  }
  return corpus::search(query, source, k, error);
}

void App::drawPersonalCorpusSection(float width) {
  if (gCorpusChunks < 0) gCorpusChunks = indexedChunkCount(corpus::dbPath());
  ImGui::PushID("personalCorpus");
  widgets::sectionHeader("PERSONAL CORPUS");
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + std::max(200.0f, width - 24.0f));
  widgets::dimText("%s", "An index of your Notes, Mail, Safari Reading List and Messages, built and kept on this Mac (embedded with "
                         "your Ollama, stored in the app's folder, never uploaded). The model can search it with the corpus__search "
                         "tool; the passages it finds go to the model you are chatting with. macOS protects these files: reading "
                         "them needs Full Disk Access for Open WebUI. Nothing is read until you press Index now.");
  ImGui::PopTextWrapPos();
  corpus::Options o = corpus::optionsFromSettings();
  bool changed = false;
  changed |= ImGui::Checkbox("Let the model search it (corpus__search, with tools on)", &o.enabled);
  changed |= ImGui::Checkbox("Index Notes", &o.notes);
  ImGui::SameLine();
  changed |= ImGui::Checkbox("Index Mail", &o.mail);
  ImGui::SameLine();
  changed |= ImGui::Checkbox("Index the Reading List", &o.readingList);
  ImGui::SameLine();
  changed |= ImGui::Checkbox("Index Messages", &o.messages);
  ImGui::SetNextItemWidth(160.0f);
  changed |= ImGui::SliderInt("days of Mail", &o.mailDays, 7, 3650);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(160.0f);
  changed |= ImGui::SliderInt("days of Messages", &o.messagesDays, 7, 3650);
  if (changed) saveCorpusOptions(o);

  if (gCorpusRunning) {
    if (widgets::ghostButton("Stop")) gCorpusCancel = true;
  } else {
    widgets::beginDisabled(!o.any());
    if (widgets::primaryButton("Index now")) indexPersonalCorpus();
    widgets::endDisabled(!o.any());
  }
  ImGui::SameLine();
  widgets::beginDisabled(gCorpusRunning || gCorpusChunks <= 0);
  if (widgets::ghostButton("Delete the index")) {
    std::remove(corpus::dbPath().c_str());
    std::remove((corpus::dbPath() + "-wal").c_str());
    std::remove((corpus::dbPath() + "-shm").c_str());
    gCorpusChunks = 0;
    gSourceStatus.clear();
    gCorpusStatus = "Index deleted";
  }
  widgets::endDisabled(gCorpusRunning || gCorpusChunks <= 0);
  ImGui::SameLine();
  widgets::dimText("%d passages indexed", std::max(gCorpusChunks, 0));
  if (!gCorpusStatus.empty()) widgets::dimText("%s", gCorpusStatus.c_str());
  for (const auto& [source, status] : gSourceStatus) widgets::dimText("%s: %s", source.c_str(), status.c_str());
  if (!gNeedsFda.empty() && widgets::ghostButton("Open Full Disk Access settings")) {
    [[NSWorkspace sharedWorkspace]
        openURL:[NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_AllFiles"]];
  }

  // Try a search here, as the model would.
  ImGui::SetNextItemWidth(std::max(200.0f, width - 140.0f));
  const bool enter = ImGui::InputTextWithHint("##corpusquery", "Search the corpus...", &gCorpusQuery, ImGuiInputTextFlags_EnterReturnsTrue);
  a11y::ui::nameLastItem("Search the corpus");
  ImGui::SameLine();
  widgets::beginDisabled(gCorpusSearching || core::trim(gCorpusQuery).empty());
  if ((ImGui::Button("Search") || enter) && !gCorpusSearching && !core::trim(gCorpusQuery).empty()) {
    gCorpusSearching = true;
    const std::string query = gCorpusQuery;
    async::run([query] {
      std::string error;
      std::string text = corpus::search(query, "", 6, &error);
      if (!error.empty()) text = error + "\n\n" + text;
      async::post([text] {
        gCorpusResult = text;
        gCorpusSearching = false;
      });
    });
  }
  widgets::endDisabled(gCorpusSearching || core::trim(gCorpusQuery).empty());
  if (!gCorpusResult.empty()) {
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + std::max(200.0f, width - 24.0f));
    ImGui::TextUnformatted(gCorpusResult.c_str());
    a11y::ui::textItem(gCorpusResult);
    ImGui::PopTextWrapPos();
  }
  ImGui::PopID();
}

}  // namespace app
