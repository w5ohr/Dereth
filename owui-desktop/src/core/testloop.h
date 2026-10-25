// Test loop helpers, app-free: which test runner a project uses, the files
// a failure output names, and a compact failure digest for the fix prompt.
// --testlooptest covers them.
#pragma once

#include <string>
#include <vector>

namespace testloop {

struct Runner {
  std::string name;                  // "pytest", "unittest", "npm test", ...
  std::vector<std::string> argv;     // run from the project folder
};

// Runs the detection against the project folder's files; `hasPytest` says
// whether `python3 -m pytest` is usable (the caller probes it).
std::vector<Runner> detect(const std::string& folder, bool hasPytest);

// Paths mentioned in a test output that exist under `folder`, relative,
// deduplicated, in order of first mention.
std::vector<std::string> mentionedFiles(const std::string& output, const std::string& folder);

// The lines that matter from a long output: failure headers, assertion
// lines, tracebacks' last frames and the summary - capped.
std::string digest(const std::string& output, size_t limit = 12000);

// True when the output reads as a pass for the given runner.
bool looksGreen(const std::string& runnerName, int exitCode, const std::string& output);

}  // namespace testloop
