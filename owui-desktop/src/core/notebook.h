// Notebook mode (#98): prompt, code and markdown cells; a persistent Python
// kernel (python3 running a small driver that keeps one namespace, captures
// stdout / stderr, reports the last expression and tracebacks, and grabs
// matplotlib figures as PNG); .ipynb (nbformat 4.5) import and export; and
// the context a prompt cell sends to the model. App-free; --notebooktest
// covers it, with a live kernel.
#pragma once

#include <sys/types.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

namespace notebook {

using Json = nlohmann::json;

struct Output {
  std::string type;   // stream | result | error | image | answer
  std::string name;   // stream: stdout | stderr
  std::string text;   // stream / result / answer text; error: "Ename: evalue"
  std::vector<std::string> traceback;
  std::string png;    // image: base64
};

struct Cell {
  std::string id;
  std::string kind = "code";   // code | prompt | markdown
  std::string source;
  std::vector<Output> outputs;
  int count = 0;               // execution count, 0 = never run
  std::string model;           // prompt: the model that answered
};

struct Notebook {
  std::string title = "Untitled";
  std::vector<Cell> cells;
  std::string pythonVersion;
};

std::string newCellId();
Json toIpynb(const Notebook& nb);
Notebook fromIpynb(const Json& j, std::string* error = nullptr);
std::vector<std::string> sourceLines(const std::string& text);   // nbformat's list-of-lines with "\n" kept

// The notebook up to (not including) `index` as text for a prompt cell:
// code with its outputs, markdown, earlier prompts and answers; oldest
// cells dropped first when over `maxChars`.
std::string promptContext(const Notebook& nb, size_t index, size_t maxChars = 12000);
std::string extractPython(const std::string& answer);   // first ```python / ```py / ``` block, "" when none
std::string stripThinking(const std::string& text);

// ---- kernel ------------------------------------------------------------------
struct ExecResult {
  bool ok = false;          // the kernel answered (the code may still have raised)
  bool timedOut = false;
  int count = 0;
  std::string out, err, result;
  std::string ename, evalue;
  std::vector<std::string> traceback;
  std::vector<std::string> images;   // base64 PNG
  std::string kernelError;  // the kernel died / could not start
};
std::vector<Output> toOutputs(const ExecResult& r);
const char* driverSource();

class Kernel {
 public:
  Kernel() = default;
  ~Kernel();
  Kernel(const Kernel&) = delete;
  Kernel& operator=(const Kernel&) = delete;

  bool start(std::string* error, const std::string& workdir = "");
  ExecResult execute(const std::string& code, int timeoutMs);   // blocks
  void interrupt();                                             // KeyboardInterrupt in the running cell
  bool restart(std::string* error);
  void stop();
  bool alive() const { return alive_; }
  bool busy() const { return busy_; }
  const std::string& version() const { return version_; }

 private:
  bool write(const std::string& line);
  void readLoop();
  void drainStderr();

  pid_t pid_ = -1;
  int stdinFd_ = -1, stdoutFd_ = -1, stderrFd_ = -1;
  std::atomic<bool> alive_{false}, busy_{false};
  std::thread reader_, errReader_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<Json> replies_;
  std::string stray_;   // non-protocol stdout (os.write(1, ...)) shown with the cell
  std::string version_, workdir_;
  int nextId_ = 1;
};

}  // namespace notebook
