#include "camera.h"

#include <algorithm>
#include <cmath>
#include <ctime>

namespace camera {

Kind kindFromType(const std::string& t) {
  if (t.find("ContinuityCamera") != std::string::npos) return Kind::Continuity;
  if (t.find("DeskView") != std::string::npos) return Kind::DeskView;
  if (t.find("BuiltIn") != std::string::npos) return Kind::BuiltIn;
  if (t.find("External") != std::string::npos) return Kind::External;
  return Kind::Other;
}

const char* kindLabel(Kind kind) {
  switch (kind) {
    case Kind::Continuity: return "iPhone (Continuity Camera)";
    case Kind::BuiltIn: return "built in";
    case Kind::External: return "external";
    case Kind::DeskView: return "Desk View";
    case Kind::Other: return "camera";
  }
  return "camera";
}

std::vector<Device> ordered(std::vector<Device> devices) {
  auto rank = [](Kind k) {
    switch (k) {
      case Kind::Continuity: return 0;
      case Kind::BuiltIn: return 1;
      case Kind::External: return 2;
      case Kind::DeskView: return 3;
      case Kind::Other: return 4;
    }
    return 5;
  };
  std::stable_sort(devices.begin(), devices.end(), [&](const Device& a, const Device& b) { return rank(a.kind) < rank(b.kind); });
  return devices;
}

const Device* choose(const std::vector<Device>& devices, const std::string& savedId) {
  if (devices.empty()) return nullptr;
  for (const Device& d : devices) {
    if (!savedId.empty() && d.id == savedId) return &d;
  }
  return &devices.front();
}

void fitSize(int width, int height, int maxSide, int* outWidth, int* outHeight) {
  if (width <= 0 || height <= 0 || maxSide <= 0) {
    *outWidth = std::max(width, 0);
    *outHeight = std::max(height, 0);
    return;
  }
  const int longest = std::max(width, height);
  if (longest <= maxSide) {
    *outWidth = width;
    *outHeight = height;
    return;
  }
  const double scale = static_cast<double>(maxSide) / longest;
  *outWidth = std::max(1, static_cast<int>(std::lround(width * scale)));
  *outHeight = std::max(1, static_cast<int>(std::lround(height * scale)));
}

std::string photoName(double unixTime) {
  std::time_t t = static_cast<std::time_t>(unixTime);
  std::tm local{};
  localtime_r(&t, &local);
  char buf[48];
  std::strftime(buf, sizeof buf, "photo-%Y-%m-%d-%H%M%S.jpg", &local);
  return buf;
}

}  // namespace camera
