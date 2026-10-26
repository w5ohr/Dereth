// Camera capture (#113): take a photo with the Mac's camera - or an iPhone
// through Continuity Camera - and attach it to the message for a vision
// model. This part has no system calls: device ordering and choice, the
// size a photo is sent at, and its file name. camera.mm holds AVFoundation.
#pragma once

#include <string>
#include <vector>

namespace camera {

enum class Kind { Continuity, BuiltIn, External, DeskView, Other };

struct Device {
  std::string id;      // AVCaptureDevice.uniqueID
  std::string name;
  Kind kind = Kind::Other;
  bool front = false;  // shown mirrored, as a mirror would
};

Kind kindFromType(const std::string& deviceType);   // AVCaptureDeviceType... strings
const char* kindLabel(Kind kind);
// Continuity first (a phone is the better camera when it is there), then built in, external, Desk View.
std::vector<Device> ordered(std::vector<Device> devices);
// The saved device when it is still there, else the first; nullptr when none.
const Device* choose(const std::vector<Device>& devices, const std::string& savedId);

// The size a photo goes out at: the longest side at most maxSide, aspect kept.
void fitSize(int width, int height, int maxSide, int* outWidth, int* outHeight);
std::string photoName(double unixTime);   // "photo-2026-10-25-203812.jpg"

}  // namespace camera
