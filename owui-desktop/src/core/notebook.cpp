#include "notebook.h"

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <random>
#include <regex>

extern char** environ;

namespace notebook {
namespace {

const char kDriver[] = R"py(
import sys, json, ast, io, traceback, base64, contextlib, signal
_g = {"__name__": "__main__"}
_count = 0
_out = sys.__stdout__
def _send(o):
    _out.write(json.dumps(o) + "\n"); _out.flush()
_send({"ready": True, "version": sys.version.split()[0]})
signal.signal(signal.SIGINT, signal.SIG_IGN)
while True:
    line = sys.stdin.readline()
    if not line:
        break
    try:
        req = json.loads(line)
    except Exception:
        continue
    code = req.get("code", "")
    _count += 1
    name = "<cell %d>" % _count
    o, e = io.StringIO(), io.StringIO()
    res = {"id": req.get("id"), "count": _count}
    signal.signal(signal.SIGINT, signal.default_int_handler)
    try:
        tree = ast.parse(code, name, "exec")
        last = None
        if tree.body and isinstance(tree.body[-1], ast.Expr):
            last = ast.Expression(tree.body.pop().value)
        with contextlib.redirect_stdout(o), contextlib.redirect_stderr(e):
            exec(compile(tree, name, "exec"), _g)
            if last is not None:
                v = eval(compile(last, name, "eval"), _g)
                if v is not None:
                    res["result"] = repr(v)
                    _g["_"] = v
    except KeyboardInterrupt:
        res["error"] = {"ename": "KeyboardInterrupt", "evalue": "interrupted", "traceback": []}
    except BaseException as x:
        tb = traceback.format_exception(type(x), x, x.__traceback__)
        res["error"] = {"ename": type(x).__name__, "evalue": str(x), "traceback": [t for t in tb if 'File "<string>"' not in t]}
    finally:
        signal.signal(signal.SIGINT, signal.SIG_IGN)
    imgs = []
    plt = sys.modules.get("matplotlib.pyplot")
    if plt is not None:
        try:
            for n in plt.get_fignums():
                b = io.BytesIO()
                plt.figure(n).savefig(b, format="png", dpi=100, bbox_inches="tight")
                imgs.append(base64.b64encode(b.getvalue()).decode())
            plt.close("all")
        except Exception as x:
            e.write("figure capture failed: %s\n" % x)
    res["stdout"] = o.getvalue()
    res["stderr"] = e.getvalue()
    res["images"] = imgs
    _send(res)
)py";

std::string trimRight(std::string s) {
  while (!s.empty() && (s.back() == '\n' || s.back() == ' ' || s.back() == '\r')) s.pop_back();
  return s;
}

std::string joinSource(const Json& source) {
  if (source.is_string()) return source.get<std::string>();
  std::string out;
  if (source.is_array()) {
    for (const auto& line : source) {
      if (line.is_string()) out += line.get<std::string>();
    }
  }
  return out;
}

}  // namespace

const char* driverSource() { return kDriver; }

std::string newCellId() {
  static std::mt19937_64 rng{std::random_device{}()};
  static const char* hex = "0123456789abcdef";
  std::string out;
  uint64_t v = rng();
  for (int i = 0; i < 12; ++i, v >>= 4) out += hex[v & 15];
  return out;
}

std::vector<std::string> sourceLines(const std::string& text) {
  std::vector<std::string> out;
  size_t start = 0;
  while (start < text.size()) {
    const size_t nl = text.find('\n', start);
    if (nl == std::string::npos) {
      out.push_back(text.substr(start));
      break;
    }
    out.push_back(text.substr(start, nl - start + 1));
    start = nl + 1;
  }
  return out;
}

Json toIpynb(const Notebook& nb) {
  Json cells = Json::array();
  for (const Cell& c : nb.cells) {
    Json cell{{"id", c.id.empty() ? newCellId() : c.id}, {"metadata", Json::object()}};
    if (c.kind == "code") {
      cell["cell_type"] = "code";
      cell["source"] = sourceLines(c.source);
      cell["execution_count"] = c.count > 0 ? Json(c.count) : Json(nullptr);
      Json outputs = Json::array();
      for (const Output& o : c.outputs) {
        if (o.type == "stream") outputs.push_back(Json{{"output_type", "stream"}, {"name", o.name.empty() ? "stdout" : o.name}, {"text", sourceLines(o.text)}});
        else if (o.type == "result") outputs.push_back(Json{{"output_type", "execute_result"}, {"execution_count", c.count > 0 ? Json(c.count) : Json(nullptr)}, {"data", Json{{"text/plain", sourceLines(o.text)}}}, {"metadata", Json::object()}});
        else if (o.type == "image") outputs.push_back(Json{{"output_type", "display_data"}, {"data", Json{{"image/png", o.png}}}, {"metadata", Json::object()}});
        else if (o.type == "error") {
          const size_t colon = o.text.find(": ");
          outputs.push_back(Json{{"output_type", "error"}, {"ename", o.text.substr(0, colon)}, {"evalue", colon == std::string::npos ? "" : o.text.substr(colon + 2)}, {"traceback", o.traceback}});
        }
      }
      cell["outputs"] = outputs;
    } else {
      cell["cell_type"] = "markdown";
      if (c.kind == "prompt") {
        std::string answer;
        for (const Output& o : c.outputs) {
          if (o.type == "answer") answer += o.text;
        }
        cell["source"] = sourceLines("**Prompt:** " + c.source + (answer.empty() ? "" : "\n\n" + answer));
        cell["metadata"]["owui"] = Json{{"kind", "prompt"}, {"prompt", c.source}, {"answer", answer}, {"model", c.model}};
      } else {
        cell["source"] = sourceLines(c.source);
      }
    }
    cells.push_back(cell);
  }
  return Json{{"nbformat", 4},
              {"nbformat_minor", 5},
              {"metadata",
               Json{{"kernelspec", Json{{"name", "python3"}, {"display_name", "Python 3"}, {"language", "python"}}},
                    {"language_info", Json{{"name", "python"}, {"version", nb.pythonVersion}}},
                    {"owui", Json{{"title", nb.title}}}}},
              {"cells", cells}};
}

Notebook fromIpynb(const Json& j, std::string* error) {
  Notebook nb;
  if (!j.is_object() || !j.contains("cells") || !j["cells"].is_array()) {
    if (error) *error = "not a notebook: no cells array";
    return nb;
  }
  if (j.value("nbformat", 4) < 4) {
    if (error) *error = "nbformat " + std::to_string(j.value("nbformat", 0)) + " is too old (4 needed)";
    return nb;
  }
  const Json meta = j.value("metadata", Json::object());
  nb.title = meta.value("owui", Json::object()).value("title", nb.title);
  nb.pythonVersion = meta.value("language_info", Json::object()).value("version", "");
  for (const auto& c : j["cells"]) {
    if (!c.is_object()) continue;
    Cell cell;
    cell.id = c.value("id", newCellId());
    const std::string type = c.value("cell_type", "code");
    const Json owui = c.value("metadata", Json::object()).value("owui", Json::object());
    if (type == "markdown" && owui.value("kind", "") == "prompt") {
      cell.kind = "prompt";
      cell.source = owui.value("prompt", "");
      cell.model = owui.value("model", "");
      const std::string answer = owui.value("answer", "");
      if (!answer.empty()) cell.outputs.push_back(Output{"answer", "", answer, {}, ""});
    } else if (type == "code") {
      cell.kind = "code";
      cell.source = joinSource(c.value("source", Json()));
      cell.count = c.value("execution_count", Json()).is_number_integer() ? c["execution_count"].get<int>() : 0;
      for (const auto& o : c.value("outputs", Json::array())) {
        const std::string ot = o.value("output_type", "");
        if (ot == "stream") cell.outputs.push_back(Output{"stream", o.value("name", "stdout"), joinSource(o.value("text", Json())), {}, ""});
        else if (ot == "execute_result" || ot == "display_data") {
          const Json data = o.value("data", Json::object());
          if (data.contains("image/png")) cell.outputs.push_back(Output{"image", "", "", {}, joinSource(data["image/png"])});
          else if (data.contains("text/plain")) cell.outputs.push_back(Output{"result", "", joinSource(data["text/plain"]), {}, ""});
        } else if (ot == "error") {
          Output e{"error", "", o.value("ename", "") + ": " + o.value("evalue", ""), {}, ""};
          for (const auto& t : o.value("traceback", Json::array())) {
            if (t.is_string()) e.traceback.push_back(t);
          }
          cell.outputs.push_back(e);
        }
      }
    } else {
      cell.kind = "markdown";
      cell.source = joinSource(c.value("source", Json()));
    }
    nb.cells.push_back(cell);
  }
  return nb;
}

std::string promptContext(const Notebook& nb, size_t index, size_t maxChars) {
  std::vector<std::string> parts;
  for (size_t i = 0; i < std::min(index, nb.cells.size()); ++i) {
    const Cell& c = nb.cells[i];
    std::string part;
    if (c.kind == "code") {
      part = "In [" + (c.count > 0 ? std::to_string(c.count) : std::string(" ")) + "]:\n```python\n" + trimRight(c.source) + "\n```\n";
      std::string out;
      for (const Output& o : c.outputs) {
        if (o.type == "stream" || o.type == "result") out += o.text + (o.text.empty() || o.text.back() == '\n' ? "" : "\n");
        else if (o.type == "error") out += o.text + "\n";
        else if (o.type == "image") out += "[figure]\n";
      }
      if (out.size() > 1500) out = out.substr(0, 1500) + "\n... (output truncated)\n";
      if (!out.empty()) part += "Out:\n" + out;
    } else if (c.kind == "markdown") {
      part = trimRight(c.source) + "\n";
    } else {
      part = "Earlier question: " + trimRight(c.source) + "\n";
      for (const Output& o : c.outputs) {
        if (o.type == "answer") part += "Answer: " + (o.text.size() > 1500 ? o.text.substr(0, 1500) + "..." : o.text) + "\n";
      }
    }
    parts.push_back(part);
  }
  // Drop the oldest cells until it fits.
  size_t total = 0;
  for (const auto& p : parts) total += p.size() + 1;
  size_t first = 0;
  while (total > maxChars && first < parts.size()) total -= parts[first++].size() + 1;
  std::string out;
  if (first > 0) out += "(" + std::to_string(first) + " earlier cell" + (first == 1 ? "" : "s") + " left out)\n";
  for (size_t i = first; i < parts.size(); ++i) out += parts[i] + "\n";
  return out;
}

std::string extractPython(const std::string& answer) {
  static const std::regex fence(R"(```[ \t]*(python3?|py)?[ \t]*\n([\s\S]*?)```)");
  std::smatch m;
  if (!std::regex_search(answer, m, fence)) return "";
  return trimRight(m[2].str()) + "\n";
}

std::string stripThinking(const std::string& text) {
  std::string out = text;
  for (;;) {
    const size_t a = out.find("<think>");
    if (a == std::string::npos) break;
    const size_t b = out.find("</think>", a);
    out.erase(a, b == std::string::npos ? std::string::npos : b + 8 - a);
  }
  const size_t first = out.find_first_not_of(" \n\r\t");
  return first == std::string::npos ? "" : out.substr(first);
}

std::vector<Output> toOutputs(const ExecResult& r) {
  std::vector<Output> out;
  if (!r.out.empty()) out.push_back(Output{"stream", "stdout", r.out, {}, ""});
  if (!r.err.empty()) out.push_back(Output{"stream", "stderr", r.err, {}, ""});
  if (!r.result.empty()) out.push_back(Output{"result", "", r.result, {}, ""});
  for (const std::string& png : r.images) out.push_back(Output{"image", "", "", {}, png});
  if (!r.ename.empty()) out.push_back(Output{"error", "", r.ename + ": " + r.evalue, r.traceback, ""});
  if (!r.kernelError.empty()) out.push_back(Output{"error", "", "KernelError: " + r.kernelError, {}, ""});
  if (r.timedOut) out.push_back(Output{"error", "", "Timeout: the cell ran too long and was interrupted", {}, ""});
  return out;
}

// ---- kernel ------------------------------------------------------------------

Kernel::~Kernel() { stop(); }

bool Kernel::start(std::string* error, const std::string& workdir) {
  stop();
  if (!workdir.empty()) workdir_ = workdir;
  const char* candidates[] = {"/opt/homebrew/bin/python3", "/usr/local/bin/python3", "/usr/bin/python3"};
  std::string exe;
  for (const char* c : candidates) {
    if (access(c, X_OK) == 0) {
      exe = c;
      break;
    }
  }
  if (exe.empty()) {
    if (error) *error = "python3 was not found";
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
  if (!workdir_.empty()) posix_spawn_file_actions_addchdir_np(&actions, workdir_.c_str());
  std::vector<std::string> argvStore{exe, "-u", "-c", kDriver};
  std::vector<char*> argv;
  for (auto& a : argvStore) argv.push_back(a.data());
  argv.push_back(nullptr);
  std::vector<std::string> envStore;
  for (char** e = environ; e && *e; ++e) {
    const std::string entry(*e);
    if (entry.rfind("MPLBACKEND=", 0) == 0 || entry.rfind("PYTHONUNBUFFERED=", 0) == 0) continue;
    envStore.push_back(entry);
  }
  envStore.push_back("MPLBACKEND=Agg");   // figures render off screen and come back as PNG
  envStore.push_back("PYTHONUNBUFFERED=1");
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
  // Wait for the driver's hello.
  std::unique_lock<std::mutex> lock(mutex_);
  const bool ready = cv_.wait_for(lock, std::chrono::seconds(10), [&] { return !replies_.empty() || !alive_; });
  if (!ready || replies_.empty() || !replies_.front().value("ready", false)) {
    lock.unlock();
    if (error) *error = "the Python kernel did not start";
    stop();
    return false;
  }
  version_ = replies_.front().value("version", "");
  replies_.pop_front();
  return true;
}

bool Kernel::write(const std::string& line) {
  if (!alive_) return false;
  size_t offset = 0;
  while (offset < line.size()) {
    const ssize_t n = ::write(stdinFd_, line.data() + offset, line.size() - offset);
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    offset += static_cast<size_t>(n);
  }
  return true;
}

ExecResult Kernel::execute(const std::string& code, int timeoutMs) {
  ExecResult r;
  if (!alive_) {
    r.kernelError = "the kernel is not running";
    return r;
  }
  busy_ = true;
  int id;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    id = nextId_++;
    stray_.clear();
  }
  if (!write(Json{{"id", id}, {"code", code}}.dump() + "\n")) {
    busy_ = false;
    r.kernelError = "could not send the cell to the kernel";
    return r;
  }
  auto waitFor = [&](int ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, std::chrono::milliseconds(ms), [&] {
      for (const Json& j : replies_) {
        if (j.value("id", -1) == id) return true;
      }
      return !alive_.load();
    });
  };
  bool got = waitFor(timeoutMs);
  if (!got && alive_) {   // too long: interrupt, then give it a moment to report
    r.timedOut = true;
    interrupt();
    got = waitFor(3000);
  }
  busy_ = false;
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto it = replies_.begin(); it != replies_.end(); ++it) {
    if (it->value("id", -1) != id) continue;
    const Json j = *it;
    replies_.erase(it);
    r.ok = true;
    r.count = j.value("count", 0);
    r.out = j.value("stdout", "") + stray_;
    r.err = j.value("stderr", "");
    r.result = j.value("result", "");
    if (j.contains("error") && j["error"].is_object()) {
      r.ename = j["error"].value("ename", "");
      r.evalue = j["error"].value("evalue", "");
      for (const auto& t : j["error"].value("traceback", Json::array())) {
        if (t.is_string()) r.traceback.push_back(t);
      }
    }
    for (const auto& img : j.value("images", Json::array())) {
      if (img.is_string()) r.images.push_back(img);
    }
    if (r.timedOut && r.ename == "KeyboardInterrupt") r.ename.clear(), r.evalue.clear();   // reported as the timeout instead
    return r;
  }
  r.kernelError = alive_ ? "the kernel did not answer" : "the kernel died (it was restarted)";
  return r;
}

void Kernel::interrupt() {
  if (pid_ > 0 && alive_) kill(pid_, SIGINT);
}

bool Kernel::restart(std::string* error) { return start(error, workdir_); }

void Kernel::readLoop() {
  std::string buffer;
  char buf[16384];
  for (;;) {
    const ssize_t n = ::read(stdoutFd_, buf, sizeof(buf));
    if (n <= 0) {
      if (n < 0 && errno == EINTR) continue;
      break;
    }
    buffer.append(buf, static_cast<size_t>(n));
    size_t nl;
    while ((nl = buffer.find('\n')) != std::string::npos) {
      const std::string line = buffer.substr(0, nl);
      buffer.erase(0, nl + 1);
      Json j = Json::parse(line, nullptr, false);
      std::lock_guard<std::mutex> lock(mutex_);
      if (!j.is_discarded() && j.is_object() && (j.contains("id") || j.contains("ready"))) replies_.push_back(std::move(j));
      else stray_ += line + "\n";
      cv_.notify_all();
    }
  }
  alive_ = false;
  std::lock_guard<std::mutex> lock(mutex_);
  cv_.notify_all();
}

void Kernel::drainStderr() {
  char buf[4096];
  while (::read(stderrFd_, buf, sizeof(buf)) > 0) {
  }
}

void Kernel::stop() {
  if (pid_ > 0) {
    if (stdinFd_ >= 0) ::close(stdinFd_);   // EOF: the driver's loop ends
    stdinFd_ = -1;
    for (int i = 0; i < 20; ++i) {
      int status = 0;
      if (waitpid(pid_, &status, WNOHANG) == pid_) {
        pid_ = -1;
        break;
      }
      usleep(25000);
    }
    if (pid_ > 0) {
      kill(pid_, SIGKILL);
      int status = 0;
      waitpid(pid_, &status, 0);
      pid_ = -1;
    }
  }
  if (stdinFd_ >= 0) ::close(stdinFd_);
  stdinFd_ = -1;
  if (reader_.joinable()) reader_.join();
  if (errReader_.joinable()) errReader_.join();
  if (stdoutFd_ >= 0) ::close(stdoutFd_);
  if (stderrFd_ >= 0) ::close(stderrFd_);
  stdoutFd_ = stderrFd_ = -1;
  alive_ = false;
  busy_ = false;
  std::lock_guard<std::mutex> lock(mutex_);
  replies_.clear();
  stray_.clear();
}

}  // namespace notebook
