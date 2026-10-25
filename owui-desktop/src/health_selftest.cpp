// `Open WebUI --healthtest`: the system sampler returns sane numbers and the
// ring behaves.
#include <unistd.h>

#include <cstdio>
#include <string>

#include "core/sysmetrics.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
std::string gb(double bytes) {
  char buf[32];
  snprintf(buf, sizeof buf, "%.1f GB", bytes / 1e9);
  return buf;
}
}  // namespace

int runHealthSelfTest() {
  using namespace sysmetrics;
  printf("Open WebUI desktop health self-test\n\n");

  Sample a = sample();
  usleep(300 * 1000);
  Sample b = sample();
  report("memory: total above 1 GB, used within total, compressed within used",
         a.memTotal > 1e9 && a.memUsed > 0 && a.memUsed <= a.memTotal && a.memCompressed >= 0 && a.memCompressed <= a.memUsed,
         gb(a.memUsed) + " of " + gb(a.memTotal));
  report("cpu: unknown on the first sample, 0-100 on the second", a.cpuPercent < 0 && b.cpuPercent >= 0 && b.cpuPercent <= 100,
         std::to_string(static_cast<int>(b.cpuPercent)) + "%");
  report("gpu: utilisation 0-100 when IOKit reports it (Apple Silicon does)", b.gpuPercent >= 0 && b.gpuPercent <= 100,
         std::to_string(static_cast<int>(b.gpuPercent)) + "%, memory " + (b.gpuMemUsed >= 0 ? gb(b.gpuMemUsed) : "unknown"));
  report("metal: working set and allocation reported", b.metalWorkingSet > 1e9 && b.metalAllocated >= 0, gb(b.metalWorkingSet));
  report("thermal state 0-3 with a name", b.thermal >= 0 && b.thermal <= 3 && !thermalName(b.thermal).empty(), thermalName(b.thermal));
  report("models folder and free space", !modelsFolder().empty() && b.modelsDiskFree > 0, modelsFolder() + " - " + gb(b.modelsDiskFree) + " free");
  report("power: battery percent -1 or 0-100", b.batteryPercent < 0 || (b.batteryPercent >= 0 && b.batteryPercent <= 100),
         b.batteryPercent < 0 ? "no battery" : std::to_string(static_cast<int>(b.batteryPercent)) + "%" + (b.onBattery ? " on battery" : " on power"));
  report("timestamps advance", b.at > a.at);

  Ring ring;
  ring.cap = 5;
  for (int i = 0; i < 8; ++i) {
    Sample s;
    s.cpuPercent = i;
    s.gpuPercent = -1;
    ring.push(s);
  }
  const std::vector<float> cpu = ring.series(&Sample::cpuPercent);
  const std::vector<float> gpu = ring.series(&Sample::gpuPercent);
  report("ring: capped, oldest dropped, series in order, unknowns plotted as 0",
         ring.samples.size() == 5 && cpu.size() == 5 && cpu[0] == 3.0f && cpu[4] == 7.0f && gpu[0] == 0.0f && ring.latest()->cpuPercent == 7);

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
