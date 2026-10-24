// Small shared helpers: string munging, time formatting, filesystem paths.
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace core {

std::string trim(const std::string& s);
std::string lower(std::string s);
bool startsWith(const std::string& s, const std::string& prefix);
bool endsWith(const std::string& s, const std::string& suffix);
bool containsNoCase(const std::string& haystack, const std::string& needle);
std::vector<std::string> split(const std::string& s, char sep);
std::string join(const std::vector<std::string>& parts, const std::string& sep);
std::string replaceAll(std::string s, const std::string& from, const std::string& to);

// "3 minutes ago", "Yesterday", "Mar 4" — for chat list timestamps (unix seconds).
std::string relativeTime(int64_t unixSeconds);
std::string formatTime(int64_t unixSeconds, const char* fmt = "%Y-%m-%d %H:%M");
int64_t nowSeconds();
double nowMillis();

std::string humanBytes(int64_t bytes);

// ~/Library/Application Support/Open WebUI Desktop, created on demand.
std::string supportDir();
std::string cacheDir();
std::string readFile(const std::string& path, bool* ok = nullptr);
bool writeFile(const std::string& path, const std::string& data);
std::string baseName(const std::string& path);
std::string extension(const std::string& path);
std::string mimeForExtension(const std::string& ext);

// Prints to stderr when OWUI_DEBUG=1 is set in the environment.
void debugLog(const char* fmt, ...);

std::string uuid();
std::string base64Encode(const std::string& raw);
std::string base64Decode(const std::string& encoded);
std::string urlEncode(const std::string& raw);

}  // namespace core
