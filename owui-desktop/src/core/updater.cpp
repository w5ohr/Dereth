#include "updater.h"

#include <CommonCrypto/CommonDigest.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

extern char** environ;
namespace fs = std::filesystem;

namespace updater {
namespace {

std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

std::string unescapeXml(std::string s) {
  const std::pair<const char*, const char*> map[] = {{"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}, {"&amp;", "&"}};
  for (const auto& [from, to] : map) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
      s.replace(pos, strlen(from), to);
      pos += strlen(to);
    }
  }
  return s;
}

// The text of <tag>...</tag> inside `xml` (CDATA unwrapped), "" when absent.
std::string element(const std::string& xml, const std::string& tag) {
  const std::regex re("<" + tag + R"((\s[^>]*)?>([\s\S]*?)</)" + tag + ">");
  std::smatch m;
  if (!std::regex_search(xml, m, re)) return "";
  std::string v = m[2].str();
  const size_t cd = v.find("<![CDATA[");
  if (cd != std::string::npos) {
    const size_t end = v.find("]]>", cd);
    return trim(v.substr(cd + 9, end == std::string::npos ? std::string::npos : end - cd - 9));
  }
  return trim(unescapeXml(v));
}

std::string attribute(const std::string& tagText, const std::string& name) {
  const std::regex re("\\s" + name + R"re(\s*=\s*("([^"]*)"|'([^']*)'))re");
  std::smatch m;
  if (!std::regex_search(tagText, m, re)) return "";
  return unescapeXml(m[2].matched ? m[2].str() : m[3].str());
}

struct Run {
  int code = -1;
  std::string out;
};
Run run(const std::vector<std::string>& args) {
  Run r;
  int pipeFd[2];
  if (pipe(pipeFd) != 0) return r;
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, pipeFd[1], STDOUT_FILENO);
  posix_spawn_file_actions_adddup2(&actions, pipeFd[1], STDERR_FILENO);
  posix_spawn_file_actions_addclose(&actions, pipeFd[0]);
  std::vector<char*> argv;
  std::vector<std::string> store = args;
  for (auto& a : store) argv.push_back(a.data());
  argv.push_back(nullptr);
  pid_t pid = -1;
  const int rc = posix_spawn(&pid, store[0].c_str(), &actions, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  ::close(pipeFd[1]);
  if (rc != 0) {
    ::close(pipeFd[0]);
    r.out = std::string("spawn ") + store[0] + ": " + strerror(rc);
    return r;
  }
  char buf[4096];
  ssize_t n;
  while ((n = ::read(pipeFd[0], buf, sizeof(buf))) > 0) r.out.append(buf, static_cast<size_t>(n));
  ::close(pipeFd[0]);
  int status = 0;
  waitpid(pid, &status, 0);
  r.code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
  return r;
}

bool isLocalHost(const std::string& url) {
  static const std::regex re(R"(^https?://(localhost|127\.0\.0\.1|\[::1\])(:\d+)?(/|$))");
  return std::regex_search(url, re);
}

// Rename when possible (same volume, atomic), else copy and remove.
bool moveBundle(const std::string& from, const std::string& to, std::string* error) {
  std::error_code ec;
  fs::rename(from, to, ec);
  if (!ec) return true;
  const Run c = run({"/usr/bin/ditto", from, to});
  if (c.code != 0) {
    if (error) *error = "could not move " + from + ": " + trim(c.out);
    return false;
  }
  fs::remove_all(from, ec);
  return true;
}

}  // namespace

int compare(const std::string& a, const std::string& b) {
  std::vector<long> x, y;
  auto split = [](const std::string& s, std::vector<long>* out) {
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, '.')) out->push_back(std::atol(part.c_str()));
  };
  split(a, &x);
  split(b, &y);
  const size_t n = std::max(x.size(), y.size());
  for (size_t i = 0; i < n; ++i) {
    const long p = i < x.size() ? x[i] : 0, q = i < y.size() ? y[i] : 0;
    if (p != q) return p < q ? -1 : 1;
  }
  return 0;
}

std::vector<Item> parseAppcast(const std::string& xml, std::string* error) {
  std::vector<Item> items;
  if (xml.find("<rss") == std::string::npos && xml.find("<channel") == std::string::npos) {
    if (error) *error = "not an appcast (no <rss> or <channel>)";
    return items;
  }
  static const std::regex itemRe(R"(<item(\s[^>]*)?>([\s\S]*?)</item>)");
  static const std::regex enclosureRe(R"(<enclosure\s[^>]*>)");
  for (auto it = std::sregex_iterator(xml.begin(), xml.end(), itemRe); it != std::sregex_iterator(); ++it) {
    const std::string body = (*it)[2].str();
    Item item;
    item.title = element(body, "title");
    item.notes = element(body, "description");
    item.notesUrl = element(body, "sparkle:releaseNotesLink");
    item.pubDate = element(body, "pubDate");
    item.minimumSystem = element(body, "sparkle:minimumSystemVersion");
    item.version = element(body, "sparkle:shortVersionString");
    item.build = element(body, "sparkle:version");
    std::smatch e;
    if (std::regex_search(body, e, enclosureRe)) {
      const std::string tag = e[0].str();
      item.url = attribute(tag, "url");
      item.length = std::atoll(attribute(tag, "length").c_str());
      item.sha256 = attribute(tag, "owui:sha256");
      if (item.version.empty()) item.version = attribute(tag, "sparkle:shortVersionString");
      if (item.build.empty()) item.build = attribute(tag, "sparkle:version");
    }
    if (item.version.empty()) item.version = item.build;
    if (!item.version.empty() && !item.url.empty()) items.push_back(item);
  }
  if (items.empty() && error) *error = "the appcast lists no installable items";
  return items;
}

std::string problem(const Item& item, const std::string& osVersion) {
  if (item.sha256.size() != 64) return "the appcast gives no SHA-256 for it";
  if (item.url.rfind("https://", 0) != 0 && !isLocalHost(item.url)) return "the download is not over HTTPS";
  if (!item.minimumSystem.empty() && !osVersion.empty() && compare(osVersion, item.minimumSystem) < 0) return "it needs macOS " + item.minimumSystem;
  return "";
}

int pick(const std::vector<Item>& items, const std::string& current, const std::string& osVersion, const std::vector<std::string>& skipped) {
  int best = -1;
  for (size_t i = 0; i < items.size(); ++i) {
    const Item& it = items[i];
    if (compare(it.version, current) <= 0) continue;
    if (std::find(skipped.begin(), skipped.end(), it.version) != skipped.end()) continue;
    if (!problem(it, osVersion).empty()) continue;
    if (best < 0 || compare(it.version, items[static_cast<size_t>(best)].version) > 0) best = static_cast<int>(i);
  }
  return best;
}

std::string sha256Hex(const std::string& bytes) {
  unsigned char digest[CC_SHA256_DIGEST_LENGTH];
  CC_SHA256(bytes.data(), static_cast<CC_LONG>(bytes.size()), digest);
  static const char* hex = "0123456789abcdef";
  std::string out;
  for (unsigned char c : digest) {
    out += hex[c >> 4];
    out += hex[c & 15];
  }
  return out;
}

std::string sha256File(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return "";
  CC_SHA256_CTX ctx;
  CC_SHA256_Init(&ctx);
  char buf[1 << 16];
  while (in) {
    in.read(buf, sizeof(buf));
    if (in.gcount() > 0) CC_SHA256_Update(&ctx, buf, static_cast<CC_LONG>(in.gcount()));
  }
  unsigned char digest[CC_SHA256_DIGEST_LENGTH];
  CC_SHA256_Final(digest, &ctx);
  static const char* hex = "0123456789abcdef";
  std::string out;
  for (unsigned char c : digest) {
    out += hex[c >> 4];
    out += hex[c & 15];
  }
  return out;
}

std::string bundleValue(const std::string& appPath, const std::string& key) {
  const Run r = run({"/usr/libexec/PlistBuddy", "-c", "Print :" + key, appPath + "/Contents/Info.plist"});
  return r.code == 0 ? trim(r.out) : "";
}

std::string backupVersion(const std::string& backupDir) {
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(backupDir, ec)) {
    if (e.path().extension() == ".app") return bundleValue(e.path().string(), "CFBundleShortVersionString");
  }
  return "";
}

Result installFromDmg(const std::string& dmg, const std::string& expectedId, const std::string& expectedVersion, const std::string& target, const std::string& backupDir) {
  Result r;
  auto step = [&](const std::string& line) { r.log += line + "\n"; };
  std::error_code ec;
  char tmpl[] = "/tmp/owui-update-XXXXXX";
  const char* mnt = mkdtemp(tmpl);
  if (!mnt) {
    r.error = "could not make a mount point";
    return r;
  }
  const std::string mount = mnt;
  const Run attach = run({"/usr/bin/hdiutil", "attach", "-readonly", "-nobrowse", "-noautoopen", "-noverify", "-mountpoint", mount, dmg});
  if (attach.code != 0) {
    r.error = "could not open the disk image: " + trim(attach.out);
    fs::remove(mount, ec);
    return r;
  }
  step("mounted " + dmg);
  auto detach = [&] {
    run({"/usr/bin/hdiutil", "detach", "-quiet", mount});
    fs::remove(mount, ec);
  };
  std::string source;
  for (const auto& e : fs::directory_iterator(mount, ec)) {
    if (e.path().extension() == ".app" && !fs::is_symlink(e.path())) source = e.path().string();
  }
  if (source.empty()) {
    detach();
    r.error = "the disk image holds no app";
    return r;
  }
  const std::string id = bundleValue(source, "CFBundleIdentifier"), version = bundleValue(source, "CFBundleShortVersionString");
  if (id != expectedId) {
    detach();
    r.error = "the image's app is " + (id.empty() ? std::string("unidentified") : id) + ", not " + expectedId;
    return r;
  }
  if (!expectedVersion.empty() && version != expectedVersion) {
    detach();
    r.error = "the image holds version " + version + ", the appcast promised " + expectedVersion;
    return r;
  }
  const Run sig = run({"/usr/bin/codesign", "--verify", "--deep", source});
  if (sig.code != 0) {
    detach();
    r.error = "the new app's signature does not verify: " + trim(sig.out);
    return r;
  }
  step("checked " + id + " " + version + " and its signature");
  const std::string incoming = target + ".incoming";
  fs::remove_all(incoming, ec);
  const Run copy = run({"/usr/bin/ditto", "--noextattr", "--norsrc", source, incoming});
  detach();
  if (copy.code != 0) {
    fs::remove_all(incoming, ec);
    r.error = "could not copy the new app: " + trim(copy.out);
    return r;
  }
  step("copied it next to " + target);
  fs::create_directories(backupDir, ec);
  for (const auto& e : fs::directory_iterator(backupDir, ec)) {
    if (e.path().extension() == ".app") fs::remove_all(e.path(), ec);
  }
  const std::string backup = backupDir + "/" + fs::path(target).filename().string();
  std::string err;
  if (fs::exists(target) && !moveBundle(target, backup, &err)) {
    fs::remove_all(incoming, ec);
    r.error = "could not set the current version aside: " + err;
    return r;
  }
  step("kept the old version in " + backupDir);
  fs::rename(incoming, target, ec);
  if (ec) {
    moveBundle(backup, target, &err);   // put the old one back
    fs::remove_all(incoming, ec);
    r.error = "could not put the new version in place: " + ec.message();
    return r;
  }
  step("installed " + version + " at " + target);
  r.ok = true;
  return r;
}

Result rollback(const std::string& target, const std::string& backupDir) {
  Result r;
  std::error_code ec;
  std::string backup;
  for (const auto& e : fs::directory_iterator(backupDir, ec)) {
    if (e.path().extension() == ".app") backup = e.path().string();
  }
  if (backup.empty()) {
    r.error = "there is no earlier version to go back to";
    return r;
  }
  const std::string from = bundleValue(target, "CFBundleShortVersionString"), to = bundleValue(backup, "CFBundleShortVersionString");
  const std::string swap = backupDir + "/swap.app";
  fs::remove_all(swap, ec);
  std::string err;
  if (!moveBundle(target, swap, &err)) {
    r.error = err;
    return r;
  }
  if (!moveBundle(backup, target, &err)) {
    moveBundle(swap, target, &err);
    r.error = "could not restore the earlier version: " + err;
    return r;
  }
  fs::rename(swap, backup, ec);   // the version rolled back from becomes the backup
  r.log = "rolled back from " + from + " to " + to + "\n";
  r.ok = true;
  return r;
}

}  // namespace updater
