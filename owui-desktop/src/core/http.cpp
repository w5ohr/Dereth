#include "core/http.h"

#include <algorithm>
#include <chrono>

#include <curl/curl.h>

#include <cstdio>
#include <cstring>
#include <mutex>

#include "core/common.h"

namespace net {
namespace {

std::vector<std::function<void(const Transfer&)>> gObservers;
std::mutex gObserverMutex;
std::atomic<bool> gCaptureBodies{true};

double epochMs() {
  return static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

std::string capped(const std::string& body) {
  if (body.size() <= kCaptureBytes) return body;
  size_t cut = kCaptureBytes;
  while (cut > 0 && (static_cast<unsigned char>(body[cut]) & 0xC0) == 0x80) --cut;
  return body.substr(0, cut);
}

struct ChunkSink {
  const std::function<bool(const char*, size_t)>* fn = nullptr;
  const std::atomic<bool>* cancel = nullptr;
  bool aborted = false;
  std::string capture;   // the first kCaptureBytes, for observers
};

size_t writeToString(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* out = static_cast<std::string*>(userdata);
  out->append(ptr, size * nmemb);
  return size * nmemb;
}

size_t writeToSink(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* sink = static_cast<ChunkSink*>(userdata);
  size_t len = size * nmemb;
  if (sink->cancel && sink->cancel->load()) {
    sink->aborted = true;
    return 0;  // signals an error to curl, which stops the transfer
  }
  if (gCaptureBodies.load() && sink->capture.size() < kCaptureBytes) sink->capture.append(ptr, std::min(len, kCaptureBytes - sink->capture.size()));
  if (!(*sink->fn)(ptr, len)) {
    sink->aborted = true;
    return 0;
  }
  return len;
}

size_t writeToFile(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* f = static_cast<FILE*>(userdata);
  return fwrite(ptr, size, nmemb, f) * size;
}

size_t collectHeader(char* buffer, size_t size, size_t nitems, void* userdata) {
  auto* headers = static_cast<std::vector<std::pair<std::string, std::string>>*>(userdata);
  std::string line(buffer, size * nitems);
  size_t colon = line.find(':');
  if (colon != std::string::npos) {
    headers->emplace_back(core::lower(core::trim(line.substr(0, colon))),
                          core::trim(line.substr(colon + 1)));
  }
  return size * nitems;
}

// Builds the easy handle shared by every entry point. `mime` and `list` are
// owned by the caller so they outlive the transfer.
CURL* makeHandle(const Request& req, curl_slist** list, curl_mime** mime) {
  CURL* curl = curl_easy_init();
  if (!curl) return nullptr;

  curl_easy_setopt(curl, CURLOPT_URL, req.url.c_str());
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(req.timeoutSec));
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, static_cast<long>(req.connectTimeoutSec));
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "OpenWebUI-Desktop/1.0 (macOS; C++)");

  for (const auto& [k, v] : req.headers) {
    *list = curl_slist_append(*list, (k + ": " + v).c_str());
  }

  const bool multipart = !req.files.empty() || !req.fields.empty();
  if (multipart) {
    *mime = curl_mime_init(curl);
    for (const auto& [k, v] : req.fields) {
      curl_mimepart* part = curl_mime_addpart(*mime);
      curl_mime_name(part, k.c_str());
      curl_mime_data(part, v.c_str(), v.size());
    }
    for (const auto& f : req.files) {
      curl_mimepart* part = curl_mime_addpart(*mime);
      curl_mime_name(part, f.field.c_str());
      curl_mime_filename(part, f.filename.c_str());
      if (!f.mime.empty()) curl_mime_type(part, f.mime.c_str());
      curl_mime_data(part, f.data.data(), f.data.size());
    }
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, *mime);
  } else if (!req.body.empty() || req.method == "POST" || req.method == "PUT" ||
             req.method == "PATCH") {
    *list = curl_slist_append(*list, ("Content-Type: " + req.contentType).c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req.body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(req.body.size()));
  }

  if (req.method != "GET" && req.method != "POST") {
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, req.method.c_str());
  } else if (req.method == "POST" && !multipart && req.body.empty()) {
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
  }

  if (*list) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, *list);
  return curl;
}

void finish(CURL* curl, curl_slist* list, curl_mime* mime, Response* res, CURLcode code,
            bool abortedByCaller, const Request* req = nullptr, double startedAtMs = 0.0,
            const std::string* responseBody = nullptr, bool streamed = false) {
  Transfer transfer;
  transfer.startedAtMs = startedAtMs;
  transfer.streamed = streamed;
  if (curl) {
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    res->status = status;
    curl_off_t up = 0, down = 0, total = 0;
    curl_easy_getinfo(curl, CURLINFO_SIZE_UPLOAD_T, &up);
    curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD_T, &down);
    curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME_T, &total);
    transfer.bytesOut = static_cast<int64_t>(up);
    transfer.bytesIn = static_cast<int64_t>(down);
    transfer.ms = static_cast<double>(total) / 1000.0;
    transfer.status = status;
  }
  if (code != CURLE_OK && !abortedByCaller) {
    res->error = curl_easy_strerror(code);
  }
  if (req) {
    transfer.method = req->method;
    transfer.url = req->url;
    transfer.error = res->error;
    transfer.contentType = res->header("content-type");
    if (gCaptureBodies.load()) {
      if (!req->files.empty() || !req->fields.empty()) {
        transfer.requestBody = "<multipart: " + std::to_string(req->files.size()) + " file(s), " + std::to_string(req->fields.size()) + " field(s)>";
      } else {
        transfer.requestBody = capped(req->body);
      }
      if (responseBody) transfer.responseBody = capped(*responseBody);
    }
    std::vector<std::function<void(const Transfer&)>> observers;
    {
      std::lock_guard<std::mutex> lock(gObserverMutex);
      observers = gObservers;
    }
    for (const auto& observer : observers) observer(transfer);
  }
  if (mime) curl_mime_free(mime);
  if (list) curl_slist_free_all(list);
  if (curl) curl_easy_cleanup(curl);
}

}  // namespace

std::string Response::header(const std::string& name) const {
  std::string key = core::lower(name);
  for (const auto& [k, v] : headers) {
    if (k == key) return v;
  }
  return {};
}

void globalInit() {
  static std::once_flag once;
  std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

void addObserver(std::function<void(const Transfer&)> observer) {
  std::lock_guard<std::mutex> lock(gObserverMutex);
  gObservers.push_back(std::move(observer));
}

void setCaptureBodies(bool capture) { gCaptureBodies.store(capture); }

void setObserver(std::function<void(const Transfer&)> observer) {
  std::lock_guard<std::mutex> lock(gObserverMutex);
  gObservers.clear();
  gObservers.push_back(std::move(observer));
}

Response perform(const Request& req) {
  globalInit();
  Response res;
  curl_slist* list = nullptr;
  curl_mime* mime = nullptr;
  CURL* curl = makeHandle(req, &list, &mime);
  if (!curl) {
    res.error = "curl init failed";
    return res;
  }
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToString);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res.body);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, collectHeader);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &res.headers);
  const double started = epochMs();
  CURLcode code = curl_easy_perform(curl);
  finish(curl, list, mime, &res, code, false, &req, started, &res.body);
  return res;
}

Response stream(const Request& req,
                const std::function<bool(const char*, size_t)>& onChunk,
                const std::atomic<bool>* cancel) {
  globalInit();
  Response res;
  curl_slist* list = nullptr;
  curl_mime* mime = nullptr;
  CURL* curl = makeHandle(req, &list, &mime);
  if (!curl) {
    res.error = "curl init failed";
    return res;
  }
  ChunkSink sink{&onChunk, cancel, false};
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToSink);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
  // The write callback only sees cancel when bytes arrive; the progress
  // callback fires about once a second regardless, so a silent server can
  // still be abandoned.
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &sink);
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION,
                   +[](void* userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int {
                     auto* s = static_cast<ChunkSink*>(userdata);
                     if (s->cancel && s->cancel->load()) {
                       s->aborted = true;
                       return 1;
                     }
                     return 0;
                   });
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, collectHeader);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &res.headers);
  // Streamed responses can idle for a long time between tokens; disable the
  // overall timeout and rely on the low-speed guard instead.
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
  const double started = epochMs();
  CURLcode code = curl_easy_perform(curl);
  finish(curl, list, mime, &res, code, sink.aborted, &req, started, &sink.capture, true);
  return res;
}

Response sse(const Request& req,
             const std::function<bool(const std::string&, const std::string&)>& onEvent,
             const std::atomic<bool>* cancel) {
  std::string buffer;
  auto flushEvent = [&onEvent](const std::string& block) -> bool {
    std::string event = "message";
    std::string data;
    for (const auto& rawLine : core::split(block, '\n')) {
      std::string line = rawLine;
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.empty() || line[0] == ':') continue;
      size_t colon = line.find(':');
      std::string field = colon == std::string::npos ? line : line.substr(0, colon);
      std::string value = colon == std::string::npos ? "" : line.substr(colon + 1);
      if (!value.empty() && value[0] == ' ') value.erase(0, 1);
      if (field == "event") {
        event = value;
      } else if (field == "data") {
        if (!data.empty()) data += "\n";
        data += value;
      }
    }
    if (data.empty()) return true;
    return onEvent(event, data);
  };

  return stream(
      req,
      [&](const char* data, size_t len) {
        buffer.append(data, len);
        size_t pos;
        while ((pos = buffer.find("\n\n")) != std::string::npos) {
          std::string block = buffer.substr(0, pos);
          buffer.erase(0, pos + 2);
          if (!flushEvent(block)) return false;
        }
        return true;
      },
      cancel);
}

Response download(const Request& req, const std::string& destPath) {
  globalInit();
  Response res;
  FILE* f = fopen(destPath.c_str(), "wb");
  if (!f) {
    res.error = "cannot open " + destPath;
    return res;
  }
  curl_slist* list = nullptr;
  curl_mime* mime = nullptr;
  CURL* curl = makeHandle(req, &list, &mime);
  if (!curl) {
    fclose(f);
    res.error = "curl init failed";
    return res;
  }
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToFile);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, collectHeader);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &res.headers);
  const double started = epochMs();
  const std::string saved = "<saved to " + destPath + ">";
  CURLcode code = curl_easy_perform(curl);
  finish(curl, list, mime, &res, code, false, &req, started, &saved);
  fclose(f);
  if (!res.ok()) remove(destPath.c_str());
  return res;
}

}  // namespace net
