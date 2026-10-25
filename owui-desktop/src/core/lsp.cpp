#include "lsp.h"

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <regex>
#include <sstream>

extern char** environ;

namespace lsp {
namespace {

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

std::string extensionOf(const std::string& path) {
  const size_t dot = path.find_last_of('.');
  const size_t slash = path.find_last_of('/');
  if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "";
  return lower(path.substr(dot));
}

bool isIdentStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool isIdent(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

bool looksLikeCode(const std::string& w) {
  if (w.size() < 3) return false;
  if (w.find('_') != std::string::npos) return true;
  bool lowerSeen = false;
  for (size_t i = 0; i < w.size(); ++i) {
    if (std::islower(static_cast<unsigned char>(w[i]))) lowerSeen = true;
    if (i > 0 && std::isupper(static_cast<unsigned char>(w[i])) && lowerSeen) return true;   // camelCase / PascalCase
  }
  return false;
}

std::string readAll(const std::string& path, bool* ok) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    if (ok) *ok = false;
    return "";
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  if (ok) *ok = true;
  return ss.str();
}

}  // namespace

// ---- framing ---------------------------------------------------------------

std::string frame(const Json& message) {
  const std::string body = message.dump();
  return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}

void Framer::feed(const char* data, size_t size) { buffer_.append(data, size); }

bool Framer::next(Json* out) {
  for (;;) {
    const size_t headerEnd = buffer_.find("\r\n\r\n");
    if (headerEnd == std::string::npos) return false;
    size_t length = std::string::npos;
    std::istringstream headers(buffer_.substr(0, headerEnd));
    std::string line;
    while (std::getline(headers, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      const size_t colon = line.find(':');
      if (colon == std::string::npos) continue;
      if (lower(trim(line.substr(0, colon))) == "content-length") length = static_cast<size_t>(std::strtoul(trim(line.substr(colon + 1)).c_str(), nullptr, 10));
    }
    if (length == std::string::npos) {   // junk before a header: drop it
      buffer_.erase(0, headerEnd + 4);
      continue;
    }
    if (buffer_.size() < headerEnd + 4 + length) return false;
    const std::string body = buffer_.substr(headerEnd + 4, length);
    buffer_.erase(0, headerEnd + 4 + length);
    Json j = Json::parse(body, nullptr, false);
    if (j.is_discarded()) continue;
    *out = std::move(j);
    return true;
  }
}

// ---- servers -----------------------------------------------------------------

std::vector<ServerSpec> defaultServers() {
  return {
      {"clangd", "clangd", {"--background-index=false", "--log=error"}, {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".m", ".mm"}},
      {"pyright", "pyright-langserver", {"--stdio"}, {".py"}},
      {"typescript", "typescript-language-server", {"--stdio"}, {".ts", ".tsx", ".js", ".jsx", ".mjs"}},
      {"gopls", "gopls", {}, {".go"}},
      {"rust-analyzer", "rust-analyzer", {}, {".rs"}},
  };
}

const ServerSpec* serverFor(const std::vector<ServerSpec>& servers, const std::string& path) {
  const std::string ext = extensionOf(path);
  for (const ServerSpec& s : servers) {
    if (std::find(s.extensions.begin(), s.extensions.end(), ext) != s.extensions.end()) return &s;
  }
  return nullptr;
}

std::string findExecutable(const std::string& command) {
  if (command.empty()) return "";
  if (command.find('/') != std::string::npos) return access(command.c_str(), X_OK) == 0 ? command : "";
  std::string path = getenv("PATH") ? getenv("PATH") : "";
  // A GUI app starts with a short PATH; add the usual install places.
  for (const char* extra : {"/opt/homebrew/bin", "/usr/local/bin", "/usr/bin", "/bin"}) path += std::string(":") + extra;
  const char* home = getenv("HOME");
  if (home) path += std::string(":") + home + "/.local/bin:" + home + "/.cargo/bin:" + home + "/go/bin:" + home + "/.npm-global/bin";
  std::istringstream dirs(path);
  std::string dir;
  while (std::getline(dirs, dir, ':')) {
    if (dir.empty()) continue;
    const std::string candidate = dir + "/" + command;
    struct stat st {};
    if (stat(candidate.c_str(), &st) == 0 && S_ISREG(st.st_mode) && access(candidate.c_str(), X_OK) == 0) return candidate;
  }
  return "";
}

std::string languageIdFor(const std::string& path) {
  const std::string ext = extensionOf(path);
  if (ext == ".c" || ext == ".h") return "c";
  if (ext == ".m") return "objective-c";
  if (ext == ".mm") return "objective-cpp";
  if (ext == ".cc" || ext == ".cpp" || ext == ".cxx" || ext == ".hh" || ext == ".hpp" || ext == ".hxx") return "cpp";
  if (ext == ".py") return "python";
  if (ext == ".ts") return "typescript";
  if (ext == ".tsx") return "typescriptreact";
  if (ext == ".js" || ext == ".mjs") return "javascript";
  if (ext == ".jsx") return "javascriptreact";
  if (ext == ".go") return "go";
  if (ext == ".rs") return "rust";
  return "plaintext";
}

std::string pathToUri(const std::string& path) {
  static const char* hex = "0123456789ABCDEF";
  std::string out = "file://";
  for (unsigned char c : path) {
    if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
    else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

std::string uriToPath(const std::string& uri) {
  std::string s = uri.rfind("file://", 0) == 0 ? uri.substr(7) : uri;
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) && std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
      out += static_cast<char>(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16));
      i += 2;
    } else {
      out += s[i];
    }
  }
  return out;
}

// ---- symbols -----------------------------------------------------------------

std::string kindName(int kind) {
  static const char* names[] = {"", "file", "module", "namespace", "package", "class", "method", "property", "field", "constructor", "enum", "interface", "function",
                                "variable", "constant", "string", "number", "boolean", "array", "object", "key", "null", "enum member", "struct", "event", "operator", "type parameter"};
  return kind > 0 && kind < 27 ? names[kind] : "symbol";
}

namespace {
void flattenInto(const Json& list, const std::string& path, const std::string& container, std::vector<Symbol>* out) {
  if (!list.is_array()) return;
  for (const auto& s : list) {
    if (!s.is_object() || !s.contains("name")) continue;
    Symbol sym;
    sym.name = s.value("name", "");
    sym.kind = s.value("kind", 0);
    sym.detail = s.value("detail", "");
    sym.path = path;
    if (s.contains("location")) {   // SymbolInformation
      const Json& loc = s["location"];
      sym.path = uriToPath(loc.value("uri", pathToUri(path)));
      const Json range = loc.value("range", Json::object());
      sym.line = sym.startLine = range.value("start", Json::object()).value("line", 0);
      sym.character = range.value("start", Json::object()).value("character", 0);
      sym.endLine = range.value("end", Json::object()).value("line", sym.line);
      sym.container = s.value("containerName", container);
    } else {   // DocumentSymbol
      const Json range = s.value("range", Json::object());
      const Json sel = s.value("selectionRange", range);
      sym.line = sel.value("start", Json::object()).value("line", 0);
      sym.character = sel.value("start", Json::object()).value("character", 0);
      sym.startLine = range.value("start", Json::object()).value("line", sym.line);
      sym.endLine = range.value("end", Json::object()).value("line", sym.line);
      sym.container = container;
    }
    // clangd names out-of-line definitions "pool::order": split the qualifier off.
    const size_t qual = sym.name.rfind("::");
    if (qual != std::string::npos) {
      const std::string q = sym.name.substr(0, qual);
      sym.name = sym.name.substr(qual + 2);
      sym.container = sym.container.empty() ? q : sym.container + "::" + q;
    }
    out->push_back(sym);
    if (s.contains("children")) {
      const std::string childContainer = sym.container.empty() ? sym.name : sym.container + "::" + sym.name;
      flattenInto(s["children"], path, childContainer, out);
    }
  }
}
}  // namespace

std::vector<Symbol> flattenDocumentSymbols(const Json& result, const std::string& path) {
  std::vector<Symbol> out;
  flattenInto(result, path, "", &out);
  return out;
}

std::vector<Symbol> fromWorkspaceSymbols(const Json& result) {
  std::vector<Symbol> out;
  flattenInto(result, "", "", &out);
  return out;
}

std::vector<Term> termsFromQuestion(const std::string& question, size_t max) {
  std::vector<Term> out;
  std::set<std::string> seen;
  auto add = [&](const std::string& raw) {
    std::string t = trim(raw);
    while (!t.empty() && (t.back() == '(' || t.back() == ')' || t.back() == '.' || t.back() == ',')) t.pop_back();
    if (t.empty()) return;
    Term term;
    const size_t q = t.rfind("::");
    const size_t dot = t.rfind('.');
    if (q != std::string::npos) {
      term.qualifier = t.substr(0, q);
      term.name = t.substr(q + 2);
    } else if (dot != std::string::npos && dot > 0 && dot + 1 < t.size() && isIdentStart(t[dot + 1])) {
      term.qualifier = t.substr(0, dot);
      term.name = t.substr(dot + 1);
    } else {
      term.name = t;
    }
    if (term.name.size() < 2 || !isIdentStart(term.name[0])) return;
    for (char c : term.name) {
      if (!isIdent(c)) return;
    }
    const std::string key = term.qualifier + "::" + term.name;
    if (seen.insert(key).second && out.size() < max) out.push_back(term);
  };
  // `backticked` first.
  for (size_t i = 0; i < question.size(); ++i) {
    if (question[i] != '`') continue;
    const size_t j = question.find('`', i + 1);
    if (j == std::string::npos) break;
    const std::string inside = question.substr(i + 1, j - i - 1);
    if (inside.find(' ') == std::string::npos) add(inside);
    i = j;
  }
  // Then a::b, f(), snake_case and camelCase words in the text.
  size_t i = 0;
  while (i < question.size()) {
    // Skip words inside other words, after a backtick (handled above) and after '/' (commands like /no_think, paths).
    if (!isIdentStart(question[i]) || (i > 0 && (isIdent(question[i - 1]) || question[i - 1] == '`' || question[i - 1] == '/'))) {
      ++i;
      continue;
    }
    size_t j = i;
    // a::b and a.b (no space after the dot, so a sentence end never joins).
    while (j < question.size() && (isIdent(question[j]) || (question[j] == ':' && j + 2 < question.size() && question[j + 1] == ':' && isIdentStart(question[j + 2])) ||
                                   (question[j] == '.' && j + 1 < question.size() && isIdentStart(question[j + 1])))) {
      j += question[j] == ':' ? 2 : 1;
    }
    std::string word = question.substr(i, j - i);
    const bool qualified = word.find("::") != std::string::npos || word.find('.') != std::string::npos;
    const bool called = j < question.size() && question[j] == '(';
    const size_t cut = word.find("::") != std::string::npos ? word.rfind("::") + 2 : word.find('.') != std::string::npos ? word.rfind('.') + 1 : 0;
    const std::string last = word.substr(cut);
    if (qualified || (called && last.size() >= 3) || looksLikeCode(last)) add(word);
    i = j;
  }
  return out;
}

bool matches(const Symbol& symbol, const Term& term) {
  if (symbol.name != term.name) return false;
  if (term.qualifier.empty()) return true;
  const std::string q = term.qualifier.substr(term.qualifier.rfind("::") == std::string::npos ? 0 : term.qualifier.rfind("::") + 2);
  const std::string c = symbol.container;
  return c == term.qualifier || (c.size() >= q.size() && c.compare(c.size() - q.size(), q.size(), q) == 0) || c.find(term.qualifier) != std::string::npos;
}

int bestMatch(const std::vector<Symbol>& candidates, const Term& term) {
  int best = -1;
  int bestScore = -1;
  for (size_t i = 0; i < candidates.size(); ++i) {
    const Symbol& s = candidates[i];
    if (!matches(s, term)) continue;
    int score = std::min(400, s.endLine - s.startLine) * 2;   // a body beats a declaration
    if (s.kind == 5 || s.kind == 6 || s.kind == 9 || s.kind == 10 || s.kind == 11 || s.kind == 12 || s.kind == 23) score += 50;   // types and functions
    if (!term.qualifier.empty() && !s.container.empty()) score += 20;
    if (score > bestScore) {
      bestScore = score;
      best = static_cast<int>(i);
    }
  }
  return best;
}

std::string hoverText(const Json& hoverResult, size_t maxChars) {
  if (!hoverResult.is_object() || !hoverResult.contains("contents")) return "";
  const Json& c = hoverResult["contents"];
  std::string text;
  auto piece = [&](const Json& p) {
    if (p.is_string()) return p.get<std::string>();
    if (p.is_object()) {
      const std::string v = p.value("value", "");
      const std::string lang = p.value("language", "");
      return lang.empty() ? v : "```" + lang + "\n" + v + "\n```";
    }
    return std::string();
  };
  if (c.is_array()) {
    for (const auto& p : c) {
      const std::string s = piece(p);
      if (!s.empty()) text += (text.empty() ? "" : "\n") + s;
    }
  } else {
    text = piece(c);
  }
  text = trim(text);
  if (text.size() > maxChars) text = text.substr(0, maxChars) + "...";
  return text;
}

std::string snippet(const std::string& fileText, int startLine, int endLine, int maxLines) {
  std::vector<std::string> lines;
  std::istringstream in(fileText);
  std::string line;
  while (std::getline(in, line)) lines.push_back(line);
  if (lines.empty() || startLine < 0 || startLine >= static_cast<int>(lines.size())) return "";
  const int last = std::min({endLine, static_cast<int>(lines.size()) - 1, startLine + maxLines - 1});
  std::string out;
  for (int i = startLine; i <= last; ++i) out += lines[static_cast<size_t>(i)] + "\n";
  if (endLine > last) out += "    // ... " + std::to_string(endLine - last) + " more lines\n";
  return out;
}

std::string formatBlock(const std::vector<Entry>& entries, const std::string& root, size_t budget) {
  if (entries.empty()) return "";
  std::string out = "Language-server context for symbols named in the question (definitions and signatures from the user's project, looked up just now):\n";
  for (const Entry& e : entries) {
    std::string rel = e.symbol.path;
    if (!root.empty() && rel.rfind(root, 0) == 0) rel = rel.substr(root.size() + (rel.size() > root.size() && rel[root.size()] == '/' ? 1 : 0));
    std::string section = "\n### " + (e.symbol.container.empty() ? "" : e.symbol.container + "::") + e.symbol.name + " (" + kindName(e.symbol.kind) + ") - " + rel + ":" + std::to_string(e.symbol.line + 1) + "\n";
    if (!e.hover.empty()) section += e.hover + "\n";
    if (!e.code.empty()) section += "```" + e.language + "\n" + e.code + "```\n";
    if (out.size() + section.size() > budget) {
      out += "\n(more symbols left out to stay within the context budget)\n";
      break;
    }
    out += section;
  }
  return out;
}

// ---- client ------------------------------------------------------------------

Client::~Client() { stop(); }

bool Client::start(const ServerSpec& spec, const std::string& rootFolder, std::string* error, int timeoutMs) {
  name_ = spec.name;
  const std::string exe = findExecutable(spec.command);
  if (exe.empty()) {
    if (error) *error = spec.command + " is not installed";
    return false;
  }
  int in[2], out[2], err[2];
  if (pipe(in) != 0 || pipe(out) != 0 || pipe(err) != 0) {
    if (error) *error = std::string("pipe: ") + strerror(errno);
    return false;
  }
  for (int fd : {in[1], out[0], err[0]}) fcntl(fd, F_SETFD, FD_CLOEXEC);
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, in[0], STDIN_FILENO);
  posix_spawn_file_actions_adddup2(&actions, out[1], STDOUT_FILENO);
  posix_spawn_file_actions_adddup2(&actions, err[1], STDERR_FILENO);
  for (int fd : {in[0], in[1], out[0], out[1], err[0], err[1]}) posix_spawn_file_actions_addclose(&actions, fd);
  posix_spawn_file_actions_addchdir_np(&actions, rootFolder.c_str());
  std::vector<std::string> argvStore{exe};
  for (const auto& a : spec.args) argvStore.push_back(a);
  std::vector<char*> argv;
  for (auto& a : argvStore) argv.push_back(a.data());
  argv.push_back(nullptr);
  pid_t pid = -1;
  const int rc = posix_spawn(&pid, exe.c_str(), &actions, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  ::close(in[0]);
  ::close(out[1]);
  ::close(err[1]);
  if (rc != 0) {
    ::close(in[1]);
    ::close(out[0]);
    ::close(err[0]);
    if (error) *error = std::string("spawn: ") + strerror(rc);
    return false;
  }
  pid_ = pid;
  stdinFd_ = in[1];
  stdoutFd_ = out[0];
  stderrFd_ = err[0];
  alive_ = true;
  reader_ = std::thread([this] { readLoop(); });
  errReader_ = std::thread([this] { drainStderr(); });

  const Json params{{"processId", static_cast<int>(getpid())},
                    {"rootUri", pathToUri(rootFolder)},
                    {"workspaceFolders", Json::array({Json{{"uri", pathToUri(rootFolder)}, {"name", rootFolder.substr(rootFolder.find_last_of('/') + 1)}}})},
                    {"capabilities",
                     Json{{"textDocument",
                           Json{{"documentSymbol", Json{{"hierarchicalDocumentSymbolSupport", true}}},
                                {"hover", Json{{"contentFormat", Json::array({"markdown", "plaintext"})}}},
                                {"definition", Json::object()}}},
                          {"workspace", Json{{"symbol", Json::object()}, {"workspaceFolders", true}}}}},
                    {"clientInfo", Json{{"name", "Open WebUI Desktop"}}}};
  std::string initError;
  const Json result = request("initialize", params, timeoutMs, &initError);
  if (result.is_null()) {
    if (error) *error = "initialize failed: " + initError;
    stop();
    return false;
  }
  notify("initialized", Json::object());
  return true;
}

bool Client::write(const Json& message) {
  if (!alive_) return false;
  const std::string data = frame(message);
  std::lock_guard<std::mutex> lock(writeMutex_);
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

Json Client::request(const std::string& method, const Json& params, int timeoutMs, std::string* error) {
  int id;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    id = nextId_++;
  }
  if (!write(Json{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}})) {
    if (error) *error = "the server is not running";
    return Json();
  }
  std::unique_lock<std::mutex> lock(mutex_);
  const bool got = cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return responses_.count(id) > 0 || !alive_; });
  if (!got || !responses_.count(id)) {
    if (error) *error = alive_ ? method + " timed out after " + std::to_string(timeoutMs) + " ms" : "the server exited";
    return Json();
  }
  Json response = std::move(responses_[id]);
  responses_.erase(id);
  if (response.contains("error")) {
    if (error) *error = response["error"].value("message", response["error"].dump());
    return Json();
  }
  Json result = response.value("result", Json());
  if (result.is_null()) result = Json::array();   // "no result" is a valid answer, not a failure
  return result;
}

void Client::notify(const std::string& method, const Json& params) { write(Json{{"jsonrpc", "2.0"}, {"method", method}, {"params", params}}); }

bool Client::openFile(const std::string& path, std::string* error) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (opened_.count(path)) return true;
  }
  bool ok = false;
  const std::string text = readAll(path, &ok);
  if (!ok) {
    if (error) *error = "could not read " + path;
    return false;
  }
  notify("textDocument/didOpen", Json{{"textDocument", Json{{"uri", pathToUri(path)}, {"languageId", languageIdFor(path)}, {"version", 1}, {"text", text}}}});
  std::lock_guard<std::mutex> lock(mutex_);
  opened_.insert(path);
  return true;
}

void Client::readLoop() {
  Framer framer;
  char buf[16384];
  for (;;) {
    const ssize_t n = ::read(stdoutFd_, buf, sizeof(buf));
    if (n <= 0) {
      if (n < 0 && errno == EINTR) continue;
      break;
    }
    framer.feed(buf, static_cast<size_t>(n));
    Json msg;
    while (framer.next(&msg)) {
      const bool hasId = msg.contains("id") && !msg["id"].is_null();
      if (msg.contains("method")) {
        // A request from the server (configuration, progress, registration): answer it so it never waits on us.
        if (hasId) write(Json{{"jsonrpc", "2.0"}, {"id", msg["id"]}, {"result", msg["method"] == "workspace/configuration" ? Json::array() : Json()}});
        continue;
      }
      if (hasId && msg["id"].is_number_integer()) {
        std::lock_guard<std::mutex> lock(mutex_);
        responses_[msg["id"].get<int>()] = msg;
        cv_.notify_all();
      }
    }
  }
  alive_ = false;
  std::lock_guard<std::mutex> lock(mutex_);
  cv_.notify_all();
}

void Client::drainStderr() {
  char buf[4096];
  while (::read(stderrFd_, buf, sizeof(buf)) > 0) {
  }
}

void Client::stop() {
  if (pid_ > 0) {
    if (alive_) {
      std::string ignored;
      request("shutdown", Json(), 1500, &ignored);
      notify("exit", Json());
    }
    for (int i = 0; i < 20; ++i) {
      int status = 0;
      if (waitpid(pid_, &status, WNOHANG) == pid_) {
        pid_ = -1;
        break;
      }
      usleep(50000);
    }
    if (pid_ > 0) {
      kill(pid_, SIGKILL);
      int status = 0;
      waitpid(pid_, &status, 0);
      pid_ = -1;
    }
  }
  alive_ = false;
  if (stdinFd_ >= 0) ::close(stdinFd_);
  stdinFd_ = -1;
  if (reader_.joinable()) reader_.join();
  if (errReader_.joinable()) errReader_.join();
  if (stdoutFd_ >= 0) ::close(stdoutFd_);
  if (stderrFd_ >= 0) ::close(stderrFd_);
  stdoutFd_ = stderrFd_ = -1;
}

}  // namespace lsp
