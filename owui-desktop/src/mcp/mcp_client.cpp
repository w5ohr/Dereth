#include "mcp/mcp_client.h"

#include <fcntl.h>
#include <glob.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstring>

#include "core/common.h"
#include "core/http.h"

extern char** environ;

namespace mcp {
namespace {

constexpr const char* kProtocolVersion = "2025-06-18";
constexpr size_t kMaxLog = 200;

std::string home() {
  const char* h = std::getenv("HOME");
  return h ? h : "";
}

// GUI apps inherit a bare PATH, which is where most "npx: command not found"
// reports come from. Servers are looked up on the user's shell-ish PATH.
std::string augmentedPath() {
  const char* current = std::getenv("PATH");
  std::string path = current ? current : "/usr/bin:/bin";
  const std::string h = home();
  for (const std::string& extra : {std::string("/opt/homebrew/bin"), std::string("/usr/local/bin"),
                                   h + "/.local/bin", h + "/.npm-global/bin", h + "/.cargo/bin",
                                   h + "/.bun/bin", h + "/.deno/bin", h + "/go/bin"}) {
    if (path.find(extra) == std::string::npos) path += ":" + extra;
  }
  // nvm keeps node under a versioned directory; take the newest one.
  glob_t g{};
  const std::string pattern = h + "/.nvm/versions/node/*/bin";
  if (glob(pattern.c_str(), 0, nullptr, &g) == 0 && g.gl_pathc > 0) {
    path += ":" + std::string(g.gl_pathv[g.gl_pathc - 1]);
  }
  globfree(&g);
  return path;
}

std::string expandHome(const std::string& p) {
  if (p.size() >= 2 && p[0] == '~' && p[1] == '/') return home() + p.substr(1);
  if (p == "~") return home();
  return p;
}

std::string findExecutable(const std::string& command, const std::string& path) {
  if (command.find('/') != std::string::npos) {
    const std::string expanded = expandHome(command);
    return access(expanded.c_str(), X_OK) == 0 ? expanded : "";
  }
  for (const std::string& dir : core::split(path, ':')) {
    if (dir.empty()) continue;
    const std::string candidate = dir + "/" + command;
    struct stat st{};
    if (stat(candidate.c_str(), &st) == 0 && S_ISREG(st.st_mode) && access(candidate.c_str(), X_OK) == 0) {
      return candidate;
    }
  }
  return "";
}

bool headerEquals(const std::string& a, const char* b) {
  return core::lower(a) == core::lower(b);
}

std::string findHeader(const net::Response& res, const char* name) {
  for (const auto& [key, value] : res.headers) {
    if (headerEquals(key, name)) return core::trim(value);
  }
  return "";
}

std::string errorText(const Json& err) {
  if (!err.is_object()) return err.dump();
  std::string text = err.value("message", "error");
  if (err.contains("code")) text += " (" + std::to_string(err.value("code", 0)) + ")";
  if (err.contains("data") && !err["data"].is_null()) {
    const std::string data = err["data"].is_string() ? err["data"].get<std::string>() : err["data"].dump();
    if (!data.empty()) text += ": " + data.substr(0, 400);
  }
  return text;
}

}  // namespace

// ---------------------------------------------------------------- config

Json ServerConfig::toJson() const {
  Json h = Json::array();
  for (const auto& [k, v] : headers) h.push_back(Json{{"name", k}, {"value", v}});
  return Json{{"id", id},           {"name", name},   {"transport", transport}, {"command", command},
              {"args", args},       {"env", env},     {"url", url},             {"headers", h},
              {"enabled", enabled}};
}

ServerConfig ServerConfig::fromJson(const Json& j) {
  ServerConfig c;
  c.id = j.value("id", core::uuid());
  c.name = j.value("name", "server");
  c.transport = j.value("transport", "stdio");
  c.command = j.value("command", "");
  if (j.contains("args") && j["args"].is_array()) {
    for (const auto& a : j["args"]) {
      if (a.is_string()) c.args.push_back(a);
    }
  }
  if (j.contains("env") && j["env"].is_object()) {
    for (auto it = j["env"].begin(); it != j["env"].end(); ++it) {
      if (it.value().is_string()) c.env[it.key()] = it.value();
    }
  }
  c.url = j.value("url", "");
  if (j.contains("headers")) {
    if (j["headers"].is_array()) {
      for (const auto& h : j["headers"]) {
        if (h.is_object()) c.headers.emplace_back(h.value("name", ""), h.value("value", ""));
      }
    } else if (j["headers"].is_object()) {
      for (auto it = j["headers"].begin(); it != j["headers"].end(); ++it) {
        if (it.value().is_string()) c.headers.emplace_back(it.key(), it.value());
      }
    }
  }
  c.enabled = j.value("enabled", true);
  return c;
}

std::vector<ServerConfig> importConfigFile(const std::string& path, std::string* error) {
  std::vector<ServerConfig> out;
  bool ok = false;
  const std::string text = core::readFile(expandHome(path), &ok);
  if (!ok) {
    if (error) *error = "Cannot read " + path;
    return out;
  }
  Json j = Json::parse(text, nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    if (error) *error = path + " is not a JSON object";
    return out;
  }
  const Json servers = j.value("mcpServers", j.value("servers", Json::object()));
  if (!servers.is_object()) {
    if (error) *error = "No mcpServers entry in " + path;
    return out;
  }
  for (auto it = servers.begin(); it != servers.end(); ++it) {
    const Json& entry = it.value();
    if (!entry.is_object()) continue;
    Json normalised = entry;
    normalised["name"] = it.key();
    normalised["id"] = core::uuid();
    normalised["transport"] = entry.contains("url") ? "http" : "stdio";
    if (entry.value("disabled", false)) normalised["enabled"] = false;
    out.push_back(ServerConfig::fromJson(normalised));
  }
  return out;
}

std::string qualifiedToolName(const std::string& serverName, const std::string& toolName) {
  auto clean = [](const std::string& s) {
    std::string out;
    for (char ch : s) {
      const bool ok = std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-';
      out += ok ? ch : '_';
    }
    return out;
  };
  std::string name = clean(serverName) + "__" + clean(toolName);
  if (name.size() > 64) name = name.substr(0, 64);
  return name;
}

// ---------------------------------------------------------------- client

Client::Client(ServerConfig config) : config_(std::move(config)) {}

Client::~Client() { close(); }

Json Client::serverInfo() const {
  std::lock_guard<std::mutex> lock(stateMutex_);
  return serverInfo_;
}
std::string Client::instructions() const {
  std::lock_guard<std::mutex> lock(stateMutex_);
  return instructions_;
}
Json Client::capabilities() const {
  std::lock_guard<std::mutex> lock(stateMutex_);
  return capabilities_;
}
std::vector<Tool> Client::tools() const {
  std::lock_guard<std::mutex> lock(stateMutex_);
  return tools_;
}
std::vector<Resource> Client::resources() const {
  std::lock_guard<std::mutex> lock(stateMutex_);
  return resources_;
}
std::vector<Prompt> Client::prompts() const {
  std::lock_guard<std::mutex> lock(stateMutex_);
  return prompts_;
}
std::vector<std::string> Client::log() const {
  std::lock_guard<std::mutex> lock(stateMutex_);
  return std::vector<std::string>(log_.begin(), log_.end());
}
std::string Client::lastError() const {
  std::lock_guard<std::mutex> lock(stateMutex_);
  return lastError_;
}
void Client::setRoots(const std::vector<std::string>& paths) {
  std::lock_guard<std::mutex> lock(stateMutex_);
  roots_ = paths;
}

void Client::addLog(const std::string& line) {
  std::lock_guard<std::mutex> lock(stateMutex_);
  log_.push_back(line.size() > 2000 ? line.substr(0, 2000) + "..." : line);
  while (log_.size() > kMaxLog) log_.pop_front();
}

bool Client::connect(std::string* error, int timeoutSec) {
  std::string err;
  if (connected_.load()) return true;
  closing_.store(false);
  if (config_.transport == "stdio") {
    if (!startStdio(&err)) {
      if (error) *error = err;
      std::lock_guard<std::mutex> lock(stateMutex_);
      lastError_ = err;
      return false;
    }
  } else if (config_.transport != "http") {
    if (error) *error = "Unknown transport '" + config_.transport + "'";
    return false;
  } else if (config_.url.empty()) {
    if (error) *error = "No URL configured";
    return false;
  }

  const Json params{
      {"protocolVersion", kProtocolVersion},
      {"capabilities", Json{{"roots", Json{{"listChanged", false}}}}},
      {"clientInfo", Json{{"name", "Open WebUI Desktop"}, {"version", "1.0"}}},
  };
  // The handshake is the one request allowed before connected_ flips.
  connected_.store(true);
  Json result = request("initialize", params, timeoutSec, &err);
  if (result.is_null()) {
    connected_.store(false);
    close();
    if (error) *error = "initialize failed: " + err;
    std::lock_guard<std::mutex> lock(stateMutex_);
    lastError_ = *error;
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(stateMutex_);
    serverInfo_ = result.value("serverInfo", Json::object());
    capabilities_ = result.value("capabilities", Json::object());
    instructions_ = result.value("instructions", "");
    protocolVersion_ = result.value("protocolVersion", kProtocolVersion);
    lastError_.clear();
  }
  notify("notifications/initialized", Json::object());

  refreshTools(&err);
  const Json caps = capabilities();
  if (caps.contains("resources")) refreshResources(nullptr);
  if (caps.contains("prompts")) refreshPrompts(nullptr);
  core::debugLog("mcp %s: connected, %zu tools", config_.name.c_str(), tools().size());
  return true;
}

void Client::close() {
  closing_.store(true);
  connected_.store(false);
  failAllPending("connection closed");
  if (config_.transport == "stdio") {
    if (stdinFd_ >= 0) {
      ::close(stdinFd_);   // EOF asks a well-behaved server to exit
      stdinFd_ = -1;
    }
    if (pid_ > 0) {
      int status = 0;
      bool exited = false;
      for (int i = 0; i < 20 && !exited; ++i) {
        if (waitpid(pid_, &status, WNOHANG) == pid_) {
          exited = true;
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      if (!exited) {
        kill(pid_, SIGTERM);
        for (int i = 0; i < 10 && !exited; ++i) {
          if (waitpid(pid_, &status, WNOHANG) == pid_) exited = true;
          else std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
      }
      if (!exited) {
        kill(pid_, SIGKILL);
        waitpid(pid_, &status, 0);
      }
      pid_ = -1;
    }
    if (reader_.joinable()) reader_.join();
    if (stderrReader_.joinable()) stderrReader_.join();
  } else if (!sessionId_.empty()) {
    // Streamable HTTP: a DELETE ends the session; servers may not support it.
    net::Request req;
    req.method = "DELETE";
    req.url = config_.url;
    req.timeoutSec = 5;
    req.headers.emplace_back("Mcp-Session-Id", sessionId_);
    for (const auto& h : config_.headers) req.headers.push_back(h);
    net::perform(req);
    sessionId_.clear();
  }
}

// ---------------------------------------------------------------- stdio

bool Client::startStdio(std::string* error) {
  if (config_.command.empty()) {
    *error = "No command configured";
    return false;
  }
  const std::string path = augmentedPath();
  const std::string exe = findExecutable(config_.command, path);
  if (exe.empty()) {
    *error = "'" + config_.command + "' was not found on PATH";
    return false;
  }

  int in[2], out[2], err[2];
  if (pipe(in) != 0 || pipe(out) != 0 || pipe(err) != 0) {
    *error = std::string("pipe: ") + strerror(errno);
    return false;
  }
  for (int fd : {in[1], out[0], err[0]}) fcntl(fd, F_SETFD, FD_CLOEXEC);

  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, in[0], STDIN_FILENO);
  posix_spawn_file_actions_adddup2(&actions, out[1], STDOUT_FILENO);
  posix_spawn_file_actions_adddup2(&actions, err[1], STDERR_FILENO);
  for (int fd : {in[0], in[1], out[0], out[1], err[0], err[1]}) {
    posix_spawn_file_actions_addclose(&actions, fd);
  }

  std::vector<std::string> argvStore;
  argvStore.push_back(exe);
  for (const auto& a : config_.args) argvStore.push_back(expandHome(a));
  std::vector<char*> argv;
  for (auto& a : argvStore) argv.push_back(a.data());
  argv.push_back(nullptr);

  std::vector<std::string> envStore;
  for (char** e = environ; e && *e; ++e) {
    const std::string entry(*e);
    const std::string key = entry.substr(0, entry.find('='));
    if (key == "PATH" || config_.env.count(key)) continue;
    envStore.push_back(entry);
  }
  envStore.push_back("PATH=" + path);
  for (const auto& [k, v] : config_.env) envStore.push_back(k + "=" + v);
  std::vector<char*> envp;
  for (auto& e : envStore) envp.push_back(e.data());
  envp.push_back(nullptr);

  pid_t pid = -1;
  const int rc = posix_spawn(&pid, exe.c_str(), &actions, nullptr, argv.data(), envp.data());
  posix_spawn_file_actions_destroy(&actions);
  ::close(in[0]);
  ::close(out[1]);
  ::close(err[1]);
  if (rc != 0) {
    ::close(in[1]);
    ::close(out[0]);
    ::close(err[0]);
    *error = std::string("spawn: ") + strerror(rc);
    return false;
  }
  pid_ = pid;
  stdinFd_ = in[1];
  stdoutFd_ = out[0];
  stderrFd_ = err[0];
  reader_ = std::thread([this] { readLoop(); });
  stderrReader_ = std::thread([this] { readStderrLoop(); });
  core::debugLog("mcp %s: spawned %s (pid %d)", config_.name.c_str(), exe.c_str(), pid);
  return true;
}

bool Client::sendLine(const std::string& line) {
  std::lock_guard<std::mutex> lock(writeMutex_);
  if (stdinFd_ < 0) return false;
  std::string data = line + "\n";
  size_t offset = 0;
  while (offset < data.size()) {
    const ssize_t n = ::write(stdinFd_, data.data() + offset, data.size() - offset);
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    offset += static_cast<size_t>(n);
  }
  return true;
}

void Client::readLoop() {
  std::string buffer;
  char chunk[16384];
  const int fd = stdoutFd_;
  while (true) {
    const ssize_t n = ::read(fd, chunk, sizeof(chunk));
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) break;
    buffer.append(chunk, static_cast<size_t>(n));
    size_t pos;
    while ((pos = buffer.find('\n')) != std::string::npos) {
      const std::string line = core::trim(buffer.substr(0, pos));
      buffer.erase(0, pos + 1);
      if (line.empty()) continue;
      Json message = Json::parse(line, nullptr, false);
      if (message.is_discarded()) {
        addLog("stdout: " + line);
        continue;
      }
      if (message.is_array()) {
        for (const auto& m : message) handleIncoming(m);
      } else {
        handleIncoming(message);
      }
    }
  }
  ::close(fd);
  stdoutFd_ = -1;
  if (!closing_.load()) {
    connected_.store(false);
    addLog("server exited");
    {
      std::lock_guard<std::mutex> lock(stateMutex_);
      lastError_ = "server exited";
    }
    failAllPending("server exited");
  }
}

void Client::readStderrLoop() {
  std::string buffer;
  char chunk[4096];
  const int fd = stderrFd_;
  while (true) {
    const ssize_t n = ::read(fd, chunk, sizeof(chunk));
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) break;
    buffer.append(chunk, static_cast<size_t>(n));
    size_t pos;
    while ((pos = buffer.find('\n')) != std::string::npos) {
      const std::string line = buffer.substr(0, pos);
      buffer.erase(0, pos + 1);
      if (!core::trim(line).empty()) addLog(line);
    }
  }
  if (!core::trim(buffer).empty()) addLog(buffer);
  ::close(fd);
  stderrFd_ = -1;
}

// ---------------------------------------------------------------- dispatch

void Client::handleIncoming(const Json& message) {
  if (!message.is_object()) return;
  const bool hasId = message.contains("id") && !message["id"].is_null();
  const bool hasMethod = message.contains("method");
  if (hasMethod && hasId) {
    handleServerRequest(message);
    return;
  }
  if (hasMethod) {
    const std::string method = message["method"];
    const Json params = message.value("params", Json::object());
    if (method == "notifications/tools/list_changed") {
      toolsChanged_.store(true);
    } else if (method == "notifications/message") {
      const Json data = params.value("data", Json());
      addLog("[" + params.value("level", "info") + "] " +
             (data.is_string() ? data.get<std::string>() : data.dump()));
    }
    return;
  }
  if (!hasId || !message["id"].is_number_integer()) return;
  const int64_t id = message["id"];
  std::shared_ptr<Pending> pending;
  {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    auto it = pending_.find(id);
    if (it == pending_.end()) return;
    pending = it->second;
    pending_.erase(it);
  }
  std::lock_guard<std::mutex> lock(pending->mutex);
  pending->response = message;
  pending->done = true;
  pending->cv.notify_all();
}

void Client::handleServerRequest(const Json& message) {
  const std::string method = message["method"];
  Json reply{{"jsonrpc", "2.0"}, {"id", message["id"]}};
  if (method == "ping") {
    reply["result"] = Json::object();
  } else if (method == "roots/list") {
    Json roots = Json::array();
    std::lock_guard<std::mutex> lock(stateMutex_);
    for (const std::string& path : roots_) {
      roots.push_back(Json{{"uri", "file://" + path}, {"name", core::baseName(path)}});
    }
    reply["result"] = Json{{"roots", roots}};
  } else {
    // sampling/createMessage and elicitation/create would need UI; decline.
    reply["error"] = Json{{"code", -32601}, {"message", "Method not supported by this client: " + method}};
  }
  if (config_.transport == "stdio") {
    sendLine(reply.dump());
  } else {
    httpPost(reply, false, nullptr, 15, nullptr);
  }
}

void Client::failAllPending(const std::string& why) {
  std::map<int64_t, std::shared_ptr<Pending>> taken;
  {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    taken.swap(pending_);
  }
  for (auto& [id, pending] : taken) {
    std::lock_guard<std::mutex> lock(pending->mutex);
    pending->response = Json{{"error", Json{{"code", -32000}, {"message", why}}}};
    pending->done = true;
    pending->cv.notify_all();
  }
}

// ---------------------------------------------------------------- requests

Json Client::request(const std::string& method, const Json& params, int timeoutSec,
                     std::string* error) {
  if (!connected_.load()) {
    if (error) *error = "not connected";
    return Json();
  }
  const int64_t id = nextId_++;
  Json message{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};

  Json response;
  if (config_.transport == "stdio") {
    auto pending = std::make_shared<Pending>();
    {
      std::lock_guard<std::mutex> lock(pendingMutex_);
      pending_[id] = pending;
    }
    if (!sendLine(message.dump())) {
      std::lock_guard<std::mutex> lock(pendingMutex_);
      pending_.erase(id);
      if (error) *error = "write to server failed";
      return Json();
    }
    std::unique_lock<std::mutex> lock(pending->mutex);
    if (!pending->cv.wait_for(lock, std::chrono::seconds(timeoutSec), [&] { return pending->done; })) {
      lock.unlock();
      std::lock_guard<std::mutex> plock(pendingMutex_);
      pending_.erase(id);
      if (error) *error = method + " timed out after " + std::to_string(timeoutSec) + " s";
      return Json();
    }
    response = pending->response;
  } else {
    std::string err;
    if (!httpPost(message, true, &response, timeoutSec, &err)) {
      if (error) *error = err;
      return Json();
    }
  }

  if (response.contains("error") && !response["error"].is_null()) {
    if (error) *error = errorText(response["error"]);
    return Json();
  }
  if (!response.contains("result")) {
    if (error) *error = "malformed response to " + method;
    return Json();
  }
  // A result of null is legal; hand back an empty object so callers can
  // tell "succeeded with nothing" from "failed".
  return response["result"].is_null() ? Json::object() : response["result"];
}

void Client::notify(const std::string& method, const Json& params) {
  if (!connected_.load()) return;
  Json message{{"jsonrpc", "2.0"}, {"method", method}, {"params", params}};
  if (config_.transport == "stdio") {
    sendLine(message.dump());
  } else {
    httpPost(message, false, nullptr, 15, nullptr);
  }
}

bool Client::httpPost(const Json& message, bool expectReply, Json* reply, int timeoutSec,
                      std::string* error) {
  net::Request req;
  req.method = "POST";
  req.url = config_.url;
  req.body = message.dump();
  req.timeoutSec = timeoutSec;
  req.headers.emplace_back("Accept", "application/json, text/event-stream");
  if (!sessionId_.empty()) req.headers.emplace_back("Mcp-Session-Id", sessionId_);
  {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (!protocolVersion_.empty()) req.headers.emplace_back("MCP-Protocol-Version", protocolVersion_);
  }
  for (const auto& h : config_.headers) req.headers.push_back(h);

  const int64_t wantId = (expectReply && message.contains("id")) ? message["id"].get<int64_t>() : -1;
  std::string body;
  bool found = false;
  Json foundReply;

  // The reply arrives either as one JSON document or as an SSE stream whose
  // events carry JSON-RPC messages; the stream may stay open after our reply,
  // so stop reading as soon as it shows up.
  auto consider = [&](const Json& m) {
    if (!m.is_object()) return;
    if (m.contains("id") && m["id"].is_number_integer() && m["id"].get<int64_t>() == wantId &&
        !m.contains("method")) {
      foundReply = m;
      found = true;
    } else {
      handleIncoming(m);
    }
  };
  auto scanSse = [&]() {
    size_t pos;
    while ((pos = body.find("\n\n")) != std::string::npos) {
      const std::string block = body.substr(0, pos);
      body.erase(0, pos + 2);
      std::string data;
      for (const auto& rawLine : core::split(block, '\n')) {
        std::string line = rawLine;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (core::startsWith(line, "data:")) {
          std::string value = line.substr(5);
          if (!value.empty() && value[0] == ' ') value.erase(0, 1);
          if (!data.empty()) data += "\n";
          data += value;
        }
      }
      if (data.empty()) continue;
      Json m = Json::parse(data, nullptr, false);
      if (m.is_discarded()) continue;
      if (m.is_array()) {
        for (const auto& item : m) consider(item);
      } else {
        consider(m);
      }
    }
  };

  // net::stream has no overall timeout (replies can legitimately take a
  // while), so the deadline is enforced with a timer that trips its cancel.
  std::atomic<bool> expired{false};
  std::mutex timerMutex;
  std::condition_variable timerCv;
  bool finished = false;
  std::thread timer([&] {
    std::unique_lock<std::mutex> lock(timerMutex);
    if (!timerCv.wait_for(lock, std::chrono::seconds(timeoutSec), [&] { return finished; })) {
      expired.store(true);
    }
  });

  net::Response res = net::stream(
      req,
      [&](const char* data, size_t len) {
        if (!expectReply) return true;
        body.append(data, len);
        const size_t first = body.find_first_not_of(" \r\n\t");
        if (first == std::string::npos) return true;
        if (body[first] == '{' || body[first] == '[') {
          Json m = Json::parse(body, nullptr, false);
          if (m.is_discarded()) return true;   // more to come
          if (m.is_array()) {
            for (const auto& item : m) consider(item);
          } else {
            consider(m);
          }
          return !found;
        }
        scanSse();
        return !found;
      },
      &expired);
  {
    std::lock_guard<std::mutex> lock(timerMutex);
    finished = true;
  }
  timerCv.notify_all();
  timer.join();

  const std::string session = findHeader(res, "Mcp-Session-Id");
  if (!session.empty()) sessionId_ = session;

  if (expired.load() && !found) {
    if (error) {
      *error = message.value("method", "request") + " timed out after " + std::to_string(timeoutSec) + " s";
    }
    return false;
  }

  if (res.status == 404 && !sessionId_.empty() && message.value("method", "") != "initialize") {
    sessionId_.clear();
    connected_.store(false);
    if (error) *error = "session expired (404); reconnect";
    return false;
  }
  if (!expectReply) {
    return res.status >= 200 && res.status < 300;
  }
  if (found) {
    if (reply) *reply = foundReply;
    return true;
  }
  if (!res.error.empty() && res.status == 0) {
    if (error) *error = res.error;
    return false;
  }
  if (res.status < 200 || res.status >= 300) {
    if (error) {
      *error = "HTTP " + std::to_string(res.status);
      const std::string snippet = core::trim(res.body.substr(0, 300));
      if (!snippet.empty()) *error += ": " + snippet;
      if (res.status == 405 || res.status == 404) {
        *error += " (is this a Streamable HTTP endpoint? The older SSE transport is not supported)";
      }
    }
    return false;
  }
  if (error) *error = "no JSON-RPC reply in the response";
  return false;
}

// ---------------------------------------------------------------- features

bool Client::refreshTools(std::string* error) {
  std::vector<Tool> all;
  std::string cursor;
  for (int page = 0; page < 50; ++page) {
    Json params = Json::object();
    if (!cursor.empty()) params["cursor"] = cursor;
    std::string err;
    Json result = request("tools/list", params, 30, &err);
    if (result.is_null()) {
      if (error) *error = err;
      return false;
    }
    for (const auto& t : result.value("tools", Json::array())) {
      if (!t.is_object()) continue;
      Tool tool;
      tool.name = t.value("name", "");
      tool.description = t.value("description", "");
      tool.inputSchema = t.value("inputSchema", Json{{"type", "object"}, {"properties", Json::object()}});
      if (t.contains("annotations") && t["annotations"].is_object()) tool.annotations = t["annotations"];
      if (!tool.name.empty()) all.push_back(tool);
    }
    cursor = result.value("nextCursor", "");
    if (cursor.empty()) break;
  }
  std::lock_guard<std::mutex> lock(stateMutex_);
  tools_ = all;
  toolsChanged_.store(false);
  return true;
}

bool Client::refreshResources(std::string* error) {
  std::string err;
  Json result = request("resources/list", Json::object(), 30, &err);
  if (result.is_null()) {
    if (error) *error = err;
    return false;
  }
  std::vector<Resource> all;
  for (const auto& r : result.value("resources", Json::array())) {
    if (!r.is_object()) continue;
    all.push_back(Resource{r.value("uri", ""), r.value("name", ""), r.value("description", ""),
                           r.value("mimeType", "")});
  }
  std::lock_guard<std::mutex> lock(stateMutex_);
  resources_ = all;
  return true;
}

bool Client::refreshPrompts(std::string* error) {
  std::string err;
  Json result = request("prompts/list", Json::object(), 30, &err);
  if (result.is_null()) {
    if (error) *error = err;
    return false;
  }
  std::vector<Prompt> all;
  for (const auto& p : result.value("prompts", Json::array())) {
    if (!p.is_object()) continue;
    all.push_back(Prompt{p.value("name", ""), p.value("description", ""),
                         p.value("arguments", Json::array())});
  }
  std::lock_guard<std::mutex> lock(stateMutex_);
  prompts_ = all;
  return true;
}

CallResult Client::callTool(const std::string& name, const Json& arguments, int timeoutSec) {
  CallResult out;
  const double started = core::nowMillis();
  std::string err;
  Json result = request("tools/call",
                        Json{{"name", name}, {"arguments", arguments.is_object() ? arguments : Json::object()}},
                        timeoutSec, &err);
  out.durationMs = core::nowMillis() - started;
  if (result.is_null()) {
    out.error = err;
    return out;
  }
  out.ok = true;
  out.isError = result.value("isError", false);
  if (result.contains("content") && result["content"].is_array()) out.content = result["content"];
  if (result.contains("structuredContent")) out.structured = result["structuredContent"];
  for (const auto& item : out.content) {
    if (!item.is_object()) continue;
    const std::string type = item.value("type", "text");
    if (!out.text.empty()) out.text += "\n";
    if (type == "text") {
      out.text += item.value("text", "");
    } else if (type == "image" || type == "audio") {
      out.text += "[" + type + " " + item.value("mimeType", "") + "]";
    } else if (type == "resource") {
      const Json res = item.value("resource", Json::object());
      if (res.contains("text") && res["text"].is_string()) out.text += res["text"].get<std::string>();
      else out.text += "[resource " + res.value("uri", "") + "]";
    } else if (type == "resource_link") {
      out.text += "[resource " + item.value("uri", "") + "]";
    } else {
      out.text += item.dump();
    }
  }
  if (out.text.empty() && !out.structured.is_null()) out.text = out.structured.dump(2);
  return out;
}

Json Client::readResource(const std::string& uri, std::string* error) {
  return request("resources/read", Json{{"uri", uri}}, 60, error);
}

Json Client::getPrompt(const std::string& name, const Json& arguments, std::string* error) {
  Json params{{"name", name}};
  if (arguments.is_object() && !arguments.empty()) params["arguments"] = arguments;
  return request("prompts/get", params, 60, error);
}

}  // namespace mcp
