// Audit log: what left the machine, when, to where. One line per outbound
// HTTP transfer (host, method, path without its query, status, bytes each
// way, duration) and one per tool-call decision (server, tool, how it was
// decided), in support/audit.jsonl. Bodies and headers are never recorded.
// App-free; --audittest covers it.
#pragma once

#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace auditlog {

using Json = nlohmann::json;

struct Record {
  int64_t at = 0;          // unix seconds
  std::string kind;        // "http" | "tool"
  std::string host;        // "localhost:3001", "api.openai.com", or the MCP server name
  std::string method;      // GET/POST... or the tool name
  std::string path;        // URL path (query stripped) or the tool's qualified name
  std::string detail;      // tool: "allowed (always)" / "denied (user)"; http: error text
  long status = 0;
  int64_t bytesOut = 0, bytesIn = 0;
  double ms = 0.0;
  bool ok = true;
};

// Splits "https://host:port/path?x=y" into host:port and /path.
void splitUrl(const std::string& url, std::string* host, std::string* path);

void append(const std::string& file, const Record& record);      // thread-safe
std::vector<Record> load(const std::string& file, int sinceDays);
// Keeps the newest `keep` lines when the file has grown past `maxLines`.
void trim(const std::string& file, size_t maxLines, size_t keep);
void clear(const std::string& file);

struct HostRow {
  std::string host;
  int requests = 0;
  int failures = 0;
  int64_t bytesOut = 0, bytesIn = 0;
};
std::vector<HostRow> byHost(const std::vector<Record>& records);   // most traffic first
std::string csv(const std::vector<Record>& records);
std::string json(const std::vector<Record>& records);

}  // namespace auditlog
