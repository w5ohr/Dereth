#include "core/i18n.h"
#include "core/common.h"
#include "core/crashreport.h"

#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <random>
#include <sstream>

namespace core {
namespace {

const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void ensureDir(const std::string& path) {
  if (path.empty()) return;
  std::string acc;
  for (size_t i = 0; i < path.size(); ++i) {
    acc += path[i];
    if (path[i] == '/' && acc.size() > 1) mkdir(acc.c_str(), 0755);
  }
  mkdir(path.c_str(), 0755);
}

}  // namespace

std::string trim(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  return s.substr(b, e - b);
}

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

bool startsWith(const std::string& s, const std::string& prefix) {
  return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool containsNoCase(const std::string& haystack, const std::string& needle) {
  if (needle.empty()) return true;
  return lower(haystack).find(lower(needle)) != std::string::npos;
}

std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == sep) {
      out.push_back(cur);
      cur.clear();
    } else {
      cur += c;
    }
  }
  out.push_back(cur);
  return out;
}

std::string join(const std::vector<std::string>& parts, const std::string& sep) {
  std::string out;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i) out += sep;
    out += parts[i];
  }
  return out;
}

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
  if (from.empty()) return s;
  size_t pos = 0;
  while ((pos = s.find(from, pos)) != std::string::npos) {
    s.replace(pos, from.size(), to);
    pos += to.size();
  }
  return s;
}

int64_t nowSeconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

double nowMillis() {
  return std::chrono::duration<double, std::milli>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

std::string formatTime(int64_t unixSeconds, const char* fmt) {
  if (unixSeconds <= 0) return "";
  std::time_t t = static_cast<std::time_t>(unixSeconds);
  std::tm tm{};
  localtime_r(&t, &tm);
  char buf[128];
  std::strftime(buf, sizeof(buf), fmt, &tm);
  return buf;
}

std::string relativeTime(int64_t unixSeconds) {
  if (unixSeconds <= 0) return "";
  return i18n::relative(unixSeconds, nowSeconds());   // "2 minutes ago", "vor 2 Minuten", ...

}

std::string humanBytes(int64_t bytes) {
  return i18n::bytes(bytes);   // the decimal separator follows the locale

}

std::string supportDir() {
  const char* home = std::getenv("HOME");
  std::string dir = std::string(home ? home : ".") + "/Library/Application Support/Open WebUI Desktop";
  ensureDir(dir);
  return dir;
}

std::string cacheDir() {
  std::string dir = supportDir() + "/cache";
  ensureDir(dir);
  return dir;
}

std::string readFile(const std::string& path, bool* ok) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    if (ok) *ok = false;
    return {};
  }
  std::ostringstream ss;
  ss << f.rdbuf();
  if (ok) *ok = true;
  return ss.str();
}

bool writeFile(const std::string& path, const std::string& data) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(data.data(), static_cast<std::streamsize>(data.size()));
  return f.good();
}

std::string baseName(const std::string& path) {
  size_t p = path.find_last_of('/');
  return p == std::string::npos ? path : path.substr(p + 1);
}

std::string extension(const std::string& path) {
  std::string base = baseName(path);
  size_t p = base.find_last_of('.');
  return p == std::string::npos ? "" : lower(base.substr(p + 1));
}

std::string mimeForExtension(const std::string& ext) {
  static const struct { const char* ext; const char* mime; } kTable[] = {
      {"png", "image/png"},   {"jpg", "image/jpeg"},  {"jpeg", "image/jpeg"},
      {"gif", "image/gif"},   {"webp", "image/webp"}, {"bmp", "image/bmp"},
      {"pdf", "application/pdf"}, {"txt", "text/plain"}, {"md", "text/markdown"},
      {"csv", "text/csv"},    {"json", "application/json"}, {"html", "text/html"},
      {"wav", "audio/wav"},   {"mp3", "audio/mpeg"},  {"m4a", "audio/mp4"},
      {"docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
      {"xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
      {"pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
  };
  for (const auto& row : kTable) {
    if (ext == row.ext) return row.mime;
  }
  return "application/octet-stream";
}

void debugLog(const char* fmt, ...) {
  static const bool enabled = std::getenv("OWUI_DEBUG") != nullptr;
  char line[512];
  va_list args;
  va_start(args, fmt);
  vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  crashreport::note(line);   // the last lines go into a crash report (kept on the Mac)
  if (enabled) fprintf(stderr, "[owui] %s\n", line);
}

std::string uuid() {
  static std::mt19937_64 rng(std::random_device{}());
  static const char* hex = "0123456789abcdef";
  std::string out;
  out.reserve(36);
  for (int i = 0; i < 36; ++i) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      out += '-';
    } else if (i == 14) {
      out += '4';
    } else {
      int v = static_cast<int>(rng() & 0xf);
      if (i == 19) v = (v & 0x3) | 0x8;
      out += hex[v];
    }
  }
  return out;
}

std::string base64Encode(const std::string& raw) {
  std::string out;
  out.reserve(((raw.size() + 2) / 3) * 4);
  size_t i = 0;
  while (i + 2 < raw.size()) {
    uint32_t n = (static_cast<unsigned char>(raw[i]) << 16) |
                 (static_cast<unsigned char>(raw[i + 1]) << 8) |
                 static_cast<unsigned char>(raw[i + 2]);
    out += kB64[(n >> 18) & 63];
    out += kB64[(n >> 12) & 63];
    out += kB64[(n >> 6) & 63];
    out += kB64[n & 63];
    i += 3;
  }
  size_t rem = raw.size() - i;
  if (rem == 1) {
    uint32_t n = static_cast<unsigned char>(raw[i]) << 16;
    out += kB64[(n >> 18) & 63];
    out += kB64[(n >> 12) & 63];
    out += "==";
  } else if (rem == 2) {
    uint32_t n = (static_cast<unsigned char>(raw[i]) << 16) |
                 (static_cast<unsigned char>(raw[i + 1]) << 8);
    out += kB64[(n >> 18) & 63];
    out += kB64[(n >> 12) & 63];
    out += kB64[(n >> 6) & 63];
    out += '=';
  }
  return out;
}

std::string base64Decode(const std::string& encoded) {
  auto value = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  std::string out;
  int buf = 0, bits = 0;
  for (char c : encoded) {
    int v = value(c);
    if (v < 0) continue;
    buf = (buf << 6) | v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out += static_cast<char>((buf >> bits) & 0xff);
    }
  }
  return out;
}

std::string urlEncode(const std::string& raw) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : raw) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

}  // namespace core
