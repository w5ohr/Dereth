// System metrics for the health dashboard (#136): CPU load, memory and
// pressure, swap, GPU utilisation and memory (IOKit's accelerator
// statistics), Metal's allocation and working-set figures, thermal state,
// power source and the free space where Ollama keeps its models. One cheap
// sample per call; a ring keeps the last few minutes. App-free; --healthtest
// checks the numbers are sane.
#pragma once

#include <deque>
#include <string>
#include <vector>

namespace sysmetrics {

struct Sample {
  double at = 0.0;                 // unix seconds
  double cpuPercent = -1.0;        // 0-100 over all cores; -1 until two samples exist
  double memUsed = 0.0;            // bytes in use (active + wired + compressed)
  double memTotal = 0.0;
  double memCompressed = 0.0;      // bytes held by the compressor (a pressure sign)
  double swapUsed = 0.0;
  double gpuPercent = -1.0;        // -1 when IOKit exposes no figure
  double gpuMemUsed = -1.0;        // bytes the GPU has in use, -1 unknown
  double metalAllocated = -1.0;    // this process's Metal allocation
  double metalWorkingSet = -1.0;   // the device's recommended max working set
  int thermal = 0;                 // 0 nominal, 1 fair, 2 serious, 3 critical
  bool onBattery = false;
  double batteryPercent = -1.0;    // -1 when there is no battery
  double modelsDiskFree = -1.0;    // bytes free on the volume holding the Ollama models folder
};

Sample sample();                         // blocking for a few ms at most
std::string thermalName(int thermal);
std::string modelsFolder();              // OLLAMA_MODELS or ~/.ollama/models

struct Ring {
  size_t cap = 300;
  std::deque<Sample> samples;
  void push(const Sample& s);
  // One float per sample for plotting; -1 values become 0.
  std::vector<float> series(double Sample::*field) const;
  const Sample* latest() const { return samples.empty() ? nullptr : &samples.back(); }
};

}  // namespace sysmetrics
