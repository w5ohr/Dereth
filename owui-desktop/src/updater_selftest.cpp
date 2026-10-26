// `Open WebUI --updatetest`: appcast parsing and selection, SHA-256, and a
// real update - this app's own bundle copied as "installed", a 9.9 copy
// re-signed and wrapped in a DMG, installed, rolled back and forward again.
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "core/updater.h"

extern char** environ;
namespace fs = std::filesystem;

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
int sh(const std::vector<std::string>& args) {
  std::vector<std::string> store = args;
  std::vector<char*> argv;
  for (auto& a : store) argv.push_back(a.data());
  argv.push_back(nullptr);
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
  posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
  pid_t pid;
  const int rc = posix_spawn(&pid, store[0].c_str(), &actions, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  if (rc != 0) return -1;
  int status = 0;
  waitpid(pid, &status, 0);
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
std::string ownBundle() {
  char exe[4096];
  uint32_t size = sizeof(exe);
  if (_NSGetExecutablePath(exe, &size) != 0) return "";
  return fs::weakly_canonical(fs::path(exe)).parent_path().parent_path().parent_path().string();   // .../Open WebUI.app
}
}  // namespace

int runUpdateSelfTest() {
  using namespace updater;
  printf("Open WebUI desktop auto-update self-test\n\n");
  const std::string sha = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
  report("sha256Hex of \"abc\" matches the FIPS vector", sha256Hex("abc") == sha);
  report("compare dotted versions", compare("1.10", "1.9") == 1 && compare("1.1", "1.1.0") == 0 && compare("1.1", "1.2") == -1 && compare("2", "1.99") == 1);

  const std::string xml = R"xml(<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0" xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle" xmlns:owui="https://openwebui.desktop/appcast">
<channel><title>Open WebUI Desktop</title>
<item><title>Version 1.0</title><sparkle:shortVersionString>1.0</sparkle:shortVersionString>
  <enclosure url="https://example.org/Open-WebUI-1.0.dmg" length="100" owui:sha256=")xml" + sha + R"xml(" type="application/octet-stream"/></item>
<item><title>Version 1.2</title><pubDate>Mon, 26 Oct 2026 10:00:00 +0000</pubDate>
  <description><![CDATA[## New
- Faster **search** & <b>fixes</b>]]></description>
  <sparkle:minimumSystemVersion>13.0</sparkle:minimumSystemVersion>
  <enclosure url="https://example.org/Open-WebUI-1.2.dmg" sparkle:version="12" sparkle:shortVersionString="1.2" length="4500000" owui:sha256=")xml" + sha + R"xml(" type="application/octet-stream"/></item>
<item><title>Version 1.3 (no hash)</title><sparkle:shortVersionString>1.3</sparkle:shortVersionString>
  <enclosure url="https://example.org/Open-WebUI-1.3.dmg" length="1"/></item>
<item><title>Version 1.4 (plain http)</title><sparkle:shortVersionString>1.4</sparkle:shortVersionString>
  <enclosure url="http://example.org/Open-WebUI-1.4.dmg" length="1" owui:sha256=")xml" + sha + R"xml("/></item>
<item><title>Version 2.0 (future OS)</title><sparkle:shortVersionString>2.0</sparkle:shortVersionString><sparkle:minimumSystemVersion>99.0</sparkle:minimumSystemVersion>
  <enclosure url="https://example.org/Open-WebUI-2.0.dmg" length="1" owui:sha256=")xml" + sha + R"xml("/></item>
<item><title>Local test</title><sparkle:shortVersionString>1.1.5</sparkle:shortVersionString>
  <enclosure url="http://127.0.0.1:8765/Open-WebUI-1.1.5.dmg" length="1" owui:sha256=")xml" + sha + R"xml("/></item>
</channel></rss>)xml";
  std::string err;
  const auto items = parseAppcast(xml, &err);
  report("parseAppcast: six items; versions from elements or enclosure attributes; length, hash, notes (CDATA), minimum OS", items.size() == 6 && items[1].version == "1.2" && items[1].build == "12" && items[1].length == 4500000 &&
                                                                                                                                items[1].sha256 == sha && items[1].notes.find("**search** & <b>fixes</b>") != std::string::npos && items[1].minimumSystem == "13.0" &&
                                                                                                                                items[0].version == "1.0",
         err);
  report("problem: no hash, plain http to a remote host, too-new OS; localhost http allowed", problem(items[2], "15.0").find("SHA-256") != std::string::npos && problem(items[3], "15.0").find("HTTPS") != std::string::npos &&
                                                                                                 problem(items[4], "15.0").find("99.0") != std::string::npos && problem(items[5], "15.0").empty() && problem(items[1], "15.0").empty());
  report("pick: the newest installable newer one (1.2 over 1.1.5; 1.3 / 1.4 / 2.0 refused)", pick(items, "1.1", "15.0", {}) == 1);
  report("pick: skipped versions and up to date", items[static_cast<size_t>(pick(items, "1.1", "15.0", {"1.2"}))].version == "1.1.5" && pick(items, "1.2", "15.0", {}) == -1);
  report("parseAppcast rejects non-appcasts", parseAppcast("<html></html>", &err).empty() && err.find("not an appcast") != std::string::npos);

  // A real install and rollback.
  const std::string self = ownBundle();
  const std::string id = bundleValue(self, "CFBundleIdentifier"), current = bundleValue(self, "CFBundleShortVersionString");
  report("bundleValue reads this app's id and version", !id.empty() && !current.empty(), id + " " + current);
  const fs::path base = fs::temp_directory_path() / "owui-updatetest";
  fs::remove_all(base);
  fs::create_directories(base / "installed");
  fs::create_directories(base / "new");
  const std::string target = (base / "installed" / "Open WebUI.app").string();
  const std::string next = (base / "new" / "Open WebUI.app").string();
  const std::string dmg = (base / "Open-WebUI-9.9.dmg").string();
  const std::string backups = (base / "previous").string();
  bool prepared = sh({"/usr/bin/ditto", "--noextattr", "--norsrc", self, target}) == 0 && sh({"/usr/bin/ditto", "--noextattr", "--norsrc", self, next}) == 0 &&
                  sh({"/usr/libexec/PlistBuddy", "-c", "Set :CFBundleShortVersionString 9.9", next + "/Contents/Info.plist"}) == 0 &&
                  sh({"/usr/bin/codesign", "--force", "--deep", "--sign", "-", next}) == 0 &&
                  sh({"/usr/bin/hdiutil", "create", "-quiet", "-volname", "Open WebUI 9.9", "-srcfolder", (base / "new").string(), "-format", "UDZO", "-ov", dmg}) == 0;
  report("fixture: an installed copy and a signed 9.9 copy in a real DMG", prepared && fs::exists(dmg) && bundleValue(next, "CFBundleShortVersionString") == "9.9");

  const Result wrong = installFromDmg(dmg, id, "9.8", target, backups);
  report("a version that doesn't match the appcast is refused and the installed app is untouched", !wrong.ok && wrong.error.find("9.9") != std::string::npos && bundleValue(target, "CFBundleShortVersionString") == current, wrong.error);
  const Result other = installFromDmg(dmg, "com.example.other", "9.9", target, backups);
  report("an image holding a different app is refused", !other.ok && other.error.find("com.example.other") != std::string::npos);
  const Result ok = installFromDmg(dmg, id, "9.9", target, backups);
  report("install: 9.9 in place, the previous version kept as the backup, nothing left beside it", ok.ok && bundleValue(target, "CFBundleShortVersionString") == "9.9" && backupVersion(backups) == current &&
                                                                                                        !fs::exists(target + ".incoming"),
         ok.ok ? "" : ok.error);
  const bool runs = sh({target + "/Contents/MacOS/Open WebUI", "--pooltest"}) == 0;
  report("the installed 9.9 runs", runs);
  const Result back = rollback(target, backups);
  report("rollback: the earlier version is back and 9.9 becomes the backup", back.ok && bundleValue(target, "CFBundleShortVersionString") == current && backupVersion(backups) == "9.9", back.error);
  const Result forward = rollback(target, backups);
  report("rolling back again restores 9.9 (a rollback is reversible)", forward.ok && bundleValue(target, "CFBundleShortVersionString") == "9.9" && backupVersion(backups) == current);
  fs::remove_all(base);
  fs::remove_all(fs::temp_directory_path() / "owui-updatetest-empty");
  fs::create_directories(fs::temp_directory_path() / "owui-updatetest-empty");
  report("rollback with no backup says so", !rollback(target, (fs::temp_directory_path() / "owui-updatetest-empty").string()).ok);
  fs::remove_all(fs::temp_directory_path() / "owui-updatetest-empty");

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
