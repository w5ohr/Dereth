// Auto-update (#129): a Sparkle-style appcast (RSS with enclosures), the
// newest applicable item, SHA-256 checks, and installing from a DMG with a
// backup of the version it replaces so it can be rolled back. App-free;
// --updatetest covers it, including a real DMG install and rollback.
#pragma once

#include <string>
#include <vector>

namespace updater {

struct Item {
  std::string title;
  std::string version;        // sparkle:shortVersionString (what the user sees), else sparkle:version
  std::string build;          // sparkle:version
  std::string url;            // the enclosure
  long long length = 0;
  std::string sha256;         // owui:sha256 on the enclosure (required to install)
  std::string notes;          // <description> (markdown or plain text, CDATA allowed)
  std::string notesUrl;       // sparkle:releaseNotesLink
  std::string pubDate;
  std::string minimumSystem;  // sparkle:minimumSystemVersion
};
std::vector<Item> parseAppcast(const std::string& xml, std::string* error = nullptr);
// Why an item can't be installed ("" when it can): no sha256, a non-https URL
// for a remote host, an OS too old.
std::string problem(const Item& item, const std::string& osVersion);
// The newest item newer than `current`, not skipped and installable; -1 when none.
int pick(const std::vector<Item>& items, const std::string& current, const std::string& osVersion, const std::vector<std::string>& skipped);
int compare(const std::string& a, const std::string& b);   // dotted numbers: -1, 0, 1

std::string sha256Hex(const std::string& bytes);
std::string sha256File(const std::string& path);   // "" when unreadable

// Bundle facts read with PlistBuddy.
std::string bundleValue(const std::string& appPath, const std::string& key);

struct Result {
  bool ok = false;
  std::string error;
  std::string log;   // one line per step
};
// Mount the DMG read-only, find its .app, check bundle id / version / signature,
// copy it next to `target`, move `target` into `backupDir` and the new one into
// place (restoring the old one if anything fails), detach.
Result installFromDmg(const std::string& dmg, const std::string& expectedId, const std::string& expectedVersion, const std::string& target, const std::string& backupDir);
// Swap the backup in `backupDir` and `target` (so a rollback can itself be rolled back).
Result rollback(const std::string& target, const std::string& backupDir);
std::string backupVersion(const std::string& backupDir);   // "" when there is none

}  // namespace updater
