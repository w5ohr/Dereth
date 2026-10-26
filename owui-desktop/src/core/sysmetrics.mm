#include "sysmetrics.h"

#import <Foundation/Foundation.h>
#import <IOKit/IOKitLib.h>
#import <IOKit/ps/IOPSKeys.h>
#import <IOKit/ps/IOPowerSources.h>
#import <Metal/Metal.h>

#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/processor_info.h>
#include <sys/statvfs.h>
#include <sys/sysctl.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>

namespace sysmetrics {
namespace {

double nowSeconds() {
  return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// CPU: ticks since the previous call.
double cpuPercent() {
  static uint64_t lastUser = 0, lastSystem = 0, lastIdle = 0, lastNice = 0;
  natural_t count = 0;
  processor_info_array_t info = nullptr;
  mach_msg_type_number_t infoCount = 0;
  if (host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &count, &info, &infoCount) != KERN_SUCCESS) return -1.0;
  uint64_t user = 0, system = 0, idle = 0, nice = 0;
  for (natural_t i = 0; i < count; ++i) {
    const integer_t* ticks = info + i * CPU_STATE_MAX;
    user += ticks[CPU_STATE_USER];
    system += ticks[CPU_STATE_SYSTEM];
    idle += ticks[CPU_STATE_IDLE];
    nice += ticks[CPU_STATE_NICE];
  }
  vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(info), infoCount * sizeof(integer_t));
  double out = -1.0;
  if (lastUser || lastIdle) {
    const double busy = static_cast<double>((user - lastUser) + (system - lastSystem) + (nice - lastNice));
    const double total = busy + static_cast<double>(idle - lastIdle);
    if (total > 0) out = std::clamp(100.0 * busy / total, 0.0, 100.0);
  }
  lastUser = user;
  lastSystem = system;
  lastIdle = idle;
  lastNice = nice;
  return out;
}

void memory(Sample* s) {
  int64_t total = 0;
  size_t len = sizeof total;
  sysctlbyname("hw.memsize", &total, &len, nullptr, 0);
  s->memTotal = static_cast<double>(total);
  vm_statistics64_data_t vm{};
  mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
  if (host_statistics64(mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vm), &count) == KERN_SUCCESS) {
    const double page = static_cast<double>(vm_kernel_page_size);
    s->memCompressed = static_cast<double>(vm.compressor_page_count) * page;
    s->memUsed = (static_cast<double>(vm.active_count) + static_cast<double>(vm.wire_count)) * page + s->memCompressed;
  }
  xsw_usage swap{};
  len = sizeof swap;
  if (sysctlbyname("vm.swapusage", &swap, &len, nullptr, 0) == 0) s->swapUsed = static_cast<double>(swap.xsu_used);
}

double numberFor(NSDictionary* dict, NSArray<NSString*>* keys) {
  for (NSString* key in keys) {
    id v = dict[key];
    if ([v isKindOfClass:[NSNumber class]]) return [v doubleValue];
  }
  return -1.0;
}

void gpu(Sample* s) {
  io_iterator_t it = 0;
  if (IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching("IOAccelerator"), &it) != KERN_SUCCESS) return;
  io_object_t service;
  while ((service = IOIteratorNext(it))) {
    CFTypeRef stats = IORegistryEntryCreateCFProperty(service, CFSTR("PerformanceStatistics"), kCFAllocatorDefault, 0);
    if (stats && CFGetTypeID(stats) == CFDictionaryGetTypeID()) {
      NSDictionary* dict = (__bridge NSDictionary*)stats;
      const double util = numberFor(dict, @[ @"Device Utilization %", @"GPU Activity(%)", @"Renderer Utilization %" ]);
      const double mem = numberFor(dict, @[ @"In use system memory", @"Alloc system memory" ]);
      if (util >= 0) s->gpuPercent = std::clamp(util, 0.0, 100.0);
      if (mem >= 0) s->gpuMemUsed = mem;
    }
    if (stats) CFRelease(stats);
    IOObjectRelease(service);
    if (s->gpuPercent >= 0) break;
  }
  IOObjectRelease(it);
}

void metal(Sample* s) {
  static id<MTLDevice> device = nil;
  static bool tried = false;
  if (!tried) {
    tried = true;
    device = MTLCreateSystemDefaultDevice();
  }
  if (!device) return;
  s->metalAllocated = static_cast<double>(device.currentAllocatedSize);
  s->metalWorkingSet = static_cast<double>(device.recommendedMaxWorkingSetSize);
}

void power(Sample* s) {
  CFTypeRef blob = IOPSCopyPowerSourcesInfo();
  if (!blob) return;
  CFArrayRef list = IOPSCopyPowerSourcesList(blob);
  if (list) {
    for (CFIndex i = 0; i < CFArrayGetCount(list); ++i) {
      CFDictionaryRef desc = IOPSGetPowerSourceDescription(blob, CFArrayGetValueAtIndex(list, i));
      if (!desc) continue;
      NSDictionary* d = (__bridge NSDictionary*)desc;
      const double cap = numberFor(d, @[ @kIOPSCurrentCapacityKey ]);
      const double max = numberFor(d, @[ @kIOPSMaxCapacityKey ]);
      if (cap >= 0 && max > 0) s->batteryPercent = 100.0 * cap / max;
      NSString* state = d[@kIOPSPowerSourceStateKey];
      s->onBattery = [state isEqualToString:@kIOPSBatteryPowerValue];
    }
    CFRelease(list);
  }
  CFRelease(blob);
}

}  // namespace

bool onBatteryNow() {
  Sample s;
  @autoreleasepool {
    power(&s);
  }
  return s.onBattery;
}

std::string modelsFolder() {
  if (const char* env = std::getenv("OLLAMA_MODELS")) {
    if (*env) return env;
  }
  return std::string([NSHomeDirectory() UTF8String]) + "/.ollama/models";
}

Sample sample() {
  Sample s;
  s.at = nowSeconds();
  s.cpuPercent = cpuPercent();
  memory(&s);
  gpu(&s);
  metal(&s);
  @autoreleasepool {
    switch ([NSProcessInfo processInfo].thermalState) {
      case NSProcessInfoThermalStateNominal: s.thermal = 0; break;
      case NSProcessInfoThermalStateFair: s.thermal = 1; break;
      case NSProcessInfoThermalStateSerious: s.thermal = 2; break;
      default: s.thermal = 3; break;
    }
    power(&s);
  }
  struct statvfs fs {};
  std::string folder = modelsFolder();
  if (statvfs(folder.c_str(), &fs) != 0) {
    folder = std::string([NSHomeDirectory() UTF8String]);
    if (statvfs(folder.c_str(), &fs) != 0) return s;
  }
  s.modelsDiskFree = static_cast<double>(fs.f_bavail) * static_cast<double>(fs.f_frsize);
  return s;
}

std::string thermalName(int thermal) {
  switch (thermal) {
    case 0: return "nominal";
    case 1: return "fair";
    case 2: return "serious";
    default: return "critical";
  }
}

void Ring::push(const Sample& s) {
  samples.push_back(s);
  while (samples.size() > cap) samples.pop_front();
}

std::vector<float> Ring::series(double Sample::*field) const {
  std::vector<float> out;
  out.reserve(samples.size());
  for (const Sample& s : samples) {
    const double v = s.*field;
    out.push_back(static_cast<float>(v < 0 ? 0.0 : v));
  }
  return out;
}

}  // namespace sysmetrics
