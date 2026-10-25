// LSP context (#97): a minimal Language Server Protocol client (stdio,
// Content-Length framing) plus the pure helpers that turn a question into
// symbol lookups and the answers into a context block: identifiers named in
// the question, workspace / document symbol flattening, hover text,
// definition snippets. App-free; --lsptest covers it (with a live clangd
// round trip when clangd is installed).
#pragma once

#include <sys/types.h>

#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

namespace lsp {

using Json = nlohmann::json;

// ---- framing ---------------------------------------------------------------
std::string frame(const Json& message);   // "Content-Length: N\r\n\r\n{...}"
class Framer {
 public:
  void feed(const char* data, size_t size);
  bool next(Json* out);   // the next complete message, if any
 private:
  std::string buffer_;
};

// ---- servers -----------------------------------------------------------------
struct ServerSpec {
  std::string name;                    // "clangd"
  std::string command;                 // looked up on PATH
  std::vector<std::string> args;
  std::vector<std::string> extensions; // ".cpp", ".h", ...
};
std::vector<ServerSpec> defaultServers();
const ServerSpec* serverFor(const std::vector<ServerSpec>& servers, const std::string& path);
std::string findExecutable(const std::string& command);   // "" when not installed
std::string languageIdFor(const std::string& path);
std::string pathToUri(const std::string& path);
std::string uriToPath(const std::string& uri);

// ---- symbols -----------------------------------------------------------------
struct Symbol {
  std::string name;
  std::string container;   // enclosing namespace / class, "a::b" when nested
  int kind = 0;            // LSP SymbolKind
  std::string path;
  int line = 0, character = 0;   // selection start (0-based)
  int startLine = 0, endLine = 0;   // the whole definition's range
  std::string detail;
};
std::string kindName(int kind);
std::vector<Symbol> flattenDocumentSymbols(const Json& result, const std::string& path);   // DocumentSymbol[] or SymbolInformation[]
std::vector<Symbol> fromWorkspaceSymbols(const Json& result);

struct Term {
  std::string name;
  std::string qualifier;   // "pool" for pool::order
};
// Identifiers that look like code: `backticked`, a::b, snake_case,
// camelCase / PascalCase with an inner capital, and names called like f().
std::vector<Term> termsFromQuestion(const std::string& question, size_t max = 6);
bool matches(const Symbol& symbol, const Term& term);
// Among matches, the best one to show: definitions with a body first
// (longest range), functions and types before variables.
int bestMatch(const std::vector<Symbol>& candidates, const Term& term);   // -1 when none

std::string hoverText(const Json& hoverResult, size_t maxChars = 1200);
std::string snippet(const std::string& fileText, int startLine, int endLine, int maxLines = 40);   // 0-based, inclusive

struct Entry {
  Symbol symbol;
  std::string hover;
  std::string code;
  std::string language;
  std::string server;
};
std::string formatBlock(const std::vector<Entry>& entries, const std::string& root, size_t budget = 9000);

// ---- client ------------------------------------------------------------------
class Client {
 public:
  Client() = default;
  ~Client();
  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;

  bool start(const ServerSpec& spec, const std::string& rootFolder, std::string* error, int timeoutMs = 15000);
  // Blocks until the response or the timeout; null with *error on failure.
  Json request(const std::string& method, const Json& params, int timeoutMs, std::string* error);
  void notify(const std::string& method, const Json& params);
  bool openFile(const std::string& path, std::string* error);   // didOpen once per path
  bool alive() const { return alive_; }
  const std::string& name() const { return name_; }
  void stop();

 private:
  bool write(const Json& message);
  void readLoop();
  void drainStderr();

  std::string name_;
  pid_t pid_ = -1;
  int stdinFd_ = -1, stdoutFd_ = -1, stderrFd_ = -1;
  std::atomic<bool> alive_{false};
  std::thread reader_, errReader_;
  std::mutex writeMutex_, mutex_;
  std::condition_variable cv_;
  std::map<int, Json> responses_;
  int nextId_ = 1;
  std::set<std::string> opened_;
};

}  // namespace lsp
