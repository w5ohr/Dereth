// Model Context Protocol client: one Client per configured server, speaking
// JSON-RPC 2.0 over a spawned process's stdio or over Streamable HTTP.
//
// Everything that talks to the server blocks, so call it from a worker
// (core/async.h). A Client is safe to share between threads: requests are
// matched to responses by id, and the stdio reader runs on its own thread.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

namespace mcp {

using Json = nlohmann::json;

// How to reach one server. Persisted in settings().extra["mcpServers"].
struct ServerConfig {
  std::string id;             // stable key (uuid) used by approvals and sync maps
  std::string name;           // shown in the UI and used as the tool-name prefix
  std::string transport = "stdio";   // stdio | http
  std::string command;        // stdio: executable, looked up on PATH
  std::vector<std::string> args;
  std::map<std::string, std::string> env;
  std::string url;            // http: the Streamable HTTP endpoint
  std::vector<std::pair<std::string, std::string>> headers;
  bool enabled = true;

  Json toJson() const;
  static ServerConfig fromJson(const Json& j);
};

struct Tool {
  std::string name;
  std::string description;
  Json inputSchema = Json::object();
};

struct Resource {
  std::string uri;
  std::string name;
  std::string description;
  std::string mimeType;
};

struct Prompt {
  std::string name;
  std::string description;
  Json arguments = Json::array();
};

// Outcome of tools/call. `text` is every text content item joined, which is
// what goes back to the model; `content` is the raw array for display.
struct CallResult {
  bool ok = false;            // transport and protocol succeeded
  bool isError = false;       // the tool itself reported failure
  std::string text;
  Json content = Json::array();
  Json structured;            // structuredContent when the server sent one
  std::string error;          // transport/protocol error text
  double durationMs = 0.0;
};

class Client {
 public:
  explicit Client(ServerConfig config);
  ~Client();
  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;

  // Spawns or contacts the server, runs the initialize handshake and lists
  // its tools. Blocks for up to `timeoutSec`.
  bool connect(std::string* error, int timeoutSec = 30);
  void close();
  bool connected() const { return connected_.load(); }

  const ServerConfig& config() const { return config_; }
  Json serverInfo() const;            // initialize result's serverInfo
  std::string instructions() const;   // initialize result's instructions
  Json capabilities() const;

  std::vector<Tool> tools() const;
  std::vector<Resource> resources() const;
  std::vector<Prompt> prompts() const;
  bool refreshTools(std::string* error);
  bool refreshResources(std::string* error);
  bool refreshPrompts(std::string* error);
  // Set by a notifications/tools/list_changed; cleared by refreshTools().
  bool toolsChanged() const { return toolsChanged_.load(); }

  CallResult callTool(const std::string& name, const Json& arguments, int timeoutSec = 120);
  Json readResource(const std::string& uri, std::string* error);
  Json getPrompt(const std::string& name, const Json& arguments, std::string* error);

  // Raw JSON-RPC. Returns the `result` member, or a null Json with `error` set.
  Json request(const std::string& method, const Json& params, int timeoutSec, std::string* error);
  void notify(const std::string& method, const Json& params);

  // Directories the server may treat as workspace roots (answers roots/list).
  void setRoots(const std::vector<std::string>& paths);

  // Recent stderr output and log notifications, newest last.
  std::vector<std::string> log() const;
  std::string lastError() const;

 private:
  struct Pending {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    Json response;
  };

  bool startStdio(std::string* error);
  bool sendLine(const std::string& line);
  void readLoop();
  void readStderrLoop();
  void handleIncoming(const Json& message);
  void handleServerRequest(const Json& message);
  void failAllPending(const std::string& why);
  void addLog(const std::string& line);

  // HTTP transport: one POST per message; the reply may be JSON or SSE.
  bool httpPost(const Json& message, bool expectReply, Json* reply, int timeoutSec,
                std::string* error);

  ServerConfig config_;
  std::atomic<bool> connected_{false};
  std::atomic<bool> closing_{false};
  std::atomic<bool> toolsChanged_{false};
  std::atomic<int64_t> nextId_{1};

  mutable std::mutex stateMutex_;
  Json serverInfo_ = Json::object();
  Json capabilities_ = Json::object();
  std::string instructions_;
  std::string protocolVersion_;
  std::vector<Tool> tools_;
  std::vector<Resource> resources_;
  std::vector<Prompt> prompts_;
  std::vector<std::string> roots_;
  std::deque<std::string> log_;
  std::string lastError_;

  std::mutex pendingMutex_;
  std::map<int64_t, std::shared_ptr<Pending>> pending_;

  // stdio
  int pid_ = -1;
  int stdinFd_ = -1;
  int stdoutFd_ = -1;
  int stderrFd_ = -1;
  std::mutex writeMutex_;
  std::thread reader_;
  std::thread stderrReader_;

  // http
  std::string sessionId_;
};

// Reads the mcpServers map of a Claude Desktop / Cursor style config file
// ({"mcpServers": {"name": {"command", "args", "env"} | {"url", "headers"}}}).
std::vector<ServerConfig> importConfigFile(const std::string& path, std::string* error);

// Tool names the model sees must match [A-Za-z0-9_-]{1,64}; this builds
// "<server>__<tool>" from a server name and a tool name, mangling as needed.
std::string qualifiedToolName(const std::string& serverName, const std::string& toolName);

}  // namespace mcp
