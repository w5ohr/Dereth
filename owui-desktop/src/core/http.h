// Thin libcurl wrapper: blocking requests, chunked streaming and SSE.
// Everything here blocks, so call it from a worker thread (see core/async.h).
#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace net {

struct FilePart {
  std::string field;
  std::string filename;
  std::string mime;
  std::string data;
};

struct Request {
  std::string method = "GET";
  std::string url;
  std::vector<std::pair<std::string, std::string>> headers;
  std::string body;
  std::string contentType = "application/json";
  // When either of these is non-empty the request is sent as multipart/form-data.
  std::vector<FilePart> files;
  std::vector<std::pair<std::string, std::string>> fields;
  int timeoutSec = 300;
  int connectTimeoutSec = 15;
};

struct Response {
  long status = 0;
  std::string body;
  std::string error;
  std::vector<std::pair<std::string, std::string>> headers;

  bool ok() const { return error.empty() && status >= 200 && status < 300; }
  std::string header(const std::string& name) const;
};

void globalInit();

// Every finished transfer (any of the calls below) is reported here, from
// the calling thread. Bodies and headers are not part of it.
struct Transfer {
  std::string method;
  std::string url;
  long status = 0;
  int64_t bytesOut = 0, bytesIn = 0;
  double ms = 0.0;
  std::string error;
};
void setObserver(std::function<void(const Transfer&)> observer);

Response perform(const Request& req);

// Streams the response body. Returning false from onChunk aborts the transfer.
Response stream(const Request& req,
                const std::function<bool(const char* data, size_t len)>& onChunk,
                const std::atomic<bool>* cancel = nullptr);

// Server-sent events. `data` is the accumulated data: payload of one event.
Response sse(const Request& req,
             const std::function<bool(const std::string& event, const std::string& data)>& onEvent,
             const std::atomic<bool>* cancel = nullptr);

// Convenience: GET straight to a file on disk (used for images and attachments).
Response download(const Request& req, const std::string& destPath);

}  // namespace net
