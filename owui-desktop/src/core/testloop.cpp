#include "testloop.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <regex>
#include <set>

#include "common.h"

namespace testloop {
namespace {

bool exists(const std::string& path) {
  struct stat st {};
  return stat(path.c_str(), &st) == 0;
}

bool isDir(const std::string& path) {
  struct stat st {};
  return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool fileContains(const std::string& path, const std::string& needle) {
  bool ok = false;
  const std::string text = core::readFile(path, &ok);
  return ok && text.find(needle) != std::string::npos;
}

bool hasPythonTests(const std::string& folder) {
  for (const char* dir : {"tests", "test", "."}) {
    const std::string d = folder + "/" + dir;
    if (!isDir(d)) continue;
    // Any test_*.py or *_test.py at the top of that folder.
    DIR* h = opendir(d.c_str());
    if (!h) continue;
    bool found = false;
    while (dirent* e = readdir(h)) {
      const std::string name = e->d_name;
      if ((core::startsWith(name, "test_") || name.find("_test.py") != std::string::npos) && core::endsWith(name, ".py")) found = true;
    }
    closedir(h);
    if (found) return true;
  }
  return false;
}

}  // namespace

std::vector<Runner> detect(const std::string& folder, bool hasPytest) {
  std::vector<Runner> out;
  if (exists(folder + "/package.json") && fileContains(folder + "/package.json", "\"test\"")) {
    out.push_back({"npm test", {"npm", "test", "--silent"}});
  }
  if (exists(folder + "/Cargo.toml")) out.push_back({"cargo test", {"cargo", "test"}});
  if (exists(folder + "/go.mod")) out.push_back({"go test", {"go", "test", "./..."}});
  if (exists(folder + "/Package.swift")) out.push_back({"swift test", {"swift", "test"}});
  if (exists(folder + "/CMakeLists.txt") && isDir(folder + "/build")) {
    out.push_back({"ctest", {"ctest", "--test-dir", "build", "--output-on-failure"}});
  }
  if (exists(folder + "/pytest.ini") || exists(folder + "/conftest.py") || exists(folder + "/setup.cfg") ||
      (exists(folder + "/pyproject.toml") && fileContains(folder + "/pyproject.toml", "pytest")) || hasPythonTests(folder)) {
    if (hasPytest) out.push_back({"pytest", {"python3", "-m", "pytest", "-q", "-x", "--no-header"}});
    else out.push_back({"unittest", {"python3", "-m", "unittest", "discover", "-v"}});
  }
  if (exists(folder + "/Makefile") && fileContains(folder + "/Makefile", "\ntest:")) out.push_back({"make test", {"make", "test"}});
  return out;
}

std::vector<std::string> mentionedFiles(const std::string& output, const std::string& folder) {
  std::vector<std::string> out;
  std::set<std::string> seen;
  static const std::regex re(R"(([A-Za-z0-9_./\-]+\.(?:py|js|ts|tsx|jsx|mjs|cpp|cc|c|h|hpp|mm|m|swift|go|rs|rb|java|kt|cs))\b)");
  for (auto it = std::sregex_iterator(output.begin(), output.end(), re); it != std::sregex_iterator(); ++it) {
    std::string path = (*it)[1];
    // Absolute paths inside the folder become relative; anything outside is dropped.
    if (core::startsWith(path, folder + "/")) path = path.substr(folder.size() + 1);
    while (core::startsWith(path, "./")) path = path.substr(2);
    if (path.empty() || path[0] == '/' || path.find("..") != std::string::npos) continue;
    if (path.find("site-packages") != std::string::npos || path.find("node_modules") != std::string::npos) continue;
    if (!exists(folder + "/" + path)) continue;
    if (seen.insert(path).second) out.push_back(path);
  }
  return out;
}

std::string digest(const std::string& output, size_t limit) {
  if (output.size() <= limit) return output;
  // Keep the head (collection errors) and the tail (failures + summary).
  const size_t head = limit / 4, tail = limit - head;
  return output.substr(0, head) + "\n[... " + std::to_string(output.size() - limit) + " characters left out ...]\n" +
         output.substr(output.size() - tail);
}

bool looksGreen(const std::string& runnerName, int exitCode, const std::string& output) {
  if (exitCode != 0) return false;
  const std::string low = core::lower(output);
  if (runnerName == "pytest") return low.find(" failed") == std::string::npos && low.find("error") == std::string::npos;
  if (runnerName == "unittest") return low.find("\nok") != std::string::npos || low.find("ok\n") != std::string::npos || low.rfind("ok") != std::string::npos;
  return true;
}

}  // namespace testloop
