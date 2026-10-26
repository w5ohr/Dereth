#include "auditlog.h"

#include <algorithm>
#include <ctime>
#include <fstream>

#include "common.h"

namespace auditlog {
namespace {

std::mutex gMutex;

Json toJson(const Record& r) {
  return Json{{"at", r.at},       {"kind", r.kind},     {"host", r.host},     {"method", r.method},
              {"path", r.path},   {"detail", r.detail}, {"status", r.status}, {"out", r.bytesOut},
              {"in", r.bytesIn},  {"ms", r.ms},         {"ok", r.ok}};
}

Record fromJson(const Json& j) {
  Record r;
  r.at = j.value("at", static_cast<int64_t>(0));
  r.kind = j.value("kind", "");
  r.host = j.value("host", "");
  r.method = j.value("method", "");
  r.path = j.value("path", "");
  r.detail = j.value("detail", "");
  r.status = j.value("status", 0L);
  r.bytesOut = j.value("out", static_cast<int64_t>(0));
  r.bytesIn = j.value("in", static_cast<int64_t>(0));
  r.ms = j.value("ms", 0.0);
  r.ok = j.value("ok", true);
  return r;
}

std::string quoteCsv(const std::string& s) {
  std::string out = "\"";
  for (char c : s) out += c == '"' ? "\"\"" : std::string(1, c);
  return out + "\"";
}

}  // namespace

void splitUrl(const std::string& url, std::string* host, std::string* path) {
  std::string rest = url;
  const size_t scheme = rest.find("://");
  if (scheme != std::string::npos) rest = rest.substr(scheme + 3);
  const size_t slash = rest.find('/');
  *host = slash == std::string::npos ? rest : rest.substr(0, slash);
  *path = slash == std::string::npos ? "/" : rest.substr(slash);
  const size_t query = path->find_first_of("?#");
  if (query != std::string::npos) *path = path->substr(0, query);
  // Credentials in the authority never make it to the log.
  const size_t at = host->find('@');
  if (at != std::string::npos) *host = host->substr(at + 1);
}

void append(const std::string& file, const Record& record) {
  std::lock_guard<std::mutex> lock(gMutex);
  std::ofstream f(file, std::ios::app);
  if (f) f << toJson(record).dump() << "\n";
}

std::vector<Record> load(const std::string& file, int sinceDays) {
  std::vector<Record> out;
  std::string raw;
  {
    std::lock_guard<std::mutex> lock(gMutex);
    bool ok = false;
    raw = core::readFile(file, &ok);
    if (!ok) return out;
  }
  const int64_t cutoff = sinceDays > 0 ? static_cast<int64_t>(std::time(nullptr)) - int64_t(sinceDays) * 86400 : 0;
  for (const std::string& line : core::split(raw, '\n')) {
    if (core::trim(line).empty()) continue;
    Json j = Json::parse(line, nullptr, false);
    if (j.is_discarded() || !j.is_object()) continue;
    Record r = fromJson(j);
    if (r.at >= cutoff) out.push_back(r);
  }
  return out;
}

void trim(const std::string& file, size_t maxLines, size_t keep) {
  std::lock_guard<std::mutex> lock(gMutex);
  bool ok = false;
  const std::string raw = core::readFile(file, &ok);
  if (!ok) return;
  std::vector<std::string> lines = core::split(raw, '\n');
  while (!lines.empty() && core::trim(lines.back()).empty()) lines.pop_back();
  if (lines.size() <= maxLines) return;
  std::string out;
  for (size_t i = lines.size() - std::min(keep, lines.size()); i < lines.size(); ++i) out += lines[i] + "\n";
  core::writeFile(file, out);
}

void clear(const std::string& file) {
  std::lock_guard<std::mutex> lock(gMutex);
  core::writeFile(file, "");
}

std::vector<HostRow> byHost(const std::vector<Record>& records) {
  std::map<std::string, HostRow> rows;
  for (const Record& r : records) {
    if (r.kind != "http") continue;
    HostRow& row = rows[r.host];
    row.host = r.host;
    row.requests += 1;
    if (!r.ok) row.failures += 1;
    row.bytesOut += r.bytesOut;
    row.bytesIn += r.bytesIn;
  }
  std::vector<HostRow> out;
  for (auto& [h, row] : rows) out.push_back(row);
  std::sort(out.begin(), out.end(), [](const HostRow& a, const HostRow& b) { return a.bytesOut + a.bytesIn > b.bytesOut + b.bytesIn; });
  return out;
}

std::string csv(const std::vector<Record>& records) {
  std::string out = "time,kind,host,method,path,status,bytes_out,bytes_in,ms,ok,detail\n";
  for (const Record& r : records) {
    std::time_t t = static_cast<std::time_t>(r.at);
    std::tm local{};
    localtime_r(&t, &local);
    char when[32];
    std::strftime(when, sizeof(when), "%Y-%m-%dT%H:%M:%S", &local);
    out += std::string(when) + "," + r.kind + "," + quoteCsv(r.host) + "," + quoteCsv(r.method) + "," + quoteCsv(r.path) + "," +
           std::to_string(r.status) + "," + std::to_string(r.bytesOut) + "," + std::to_string(r.bytesIn) + "," +
           std::to_string(static_cast<long>(r.ms)) + "," + (r.ok ? "1" : "0") + "," + quoteCsv(r.detail) + "\n";
  }
  return out;
}

std::string json(const std::vector<Record>& records) {
  Json arr = Json::array();
  for (const Record& r : records) arr.push_back(toJson(r));
  return arr.dump(2);
}

}  // namespace auditlog
