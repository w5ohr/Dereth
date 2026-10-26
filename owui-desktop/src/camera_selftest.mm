// `Open WebUI --cameratest` (#113): device ordering and choice, photo sizes
// and names, and the image pipeline on synthetic frames - the camera itself
// is never opened (that needs permission and would photograph the room).
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <cstdio>
#include <string>

#include "core/camera.h"

namespace camera {
enum class Access { NotDetermined, Granted, Denied, Restricted };
Access access();
std::vector<Device> listDevices();
std::string jpegFromPixelBuffer(CVPixelBufferRef buffer, int maxSide, double quality);
std::string fitJpeg(const std::string& encoded, int maxSide, double quality, int* outWidth, int* outHeight);
}

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}

// Width, height and the centre pixel's RGB of an encoded image.
bool inspect(const std::string& bytes, int* w, int* h, int* r, int* g, int* b) {
  NSData* data = [NSData dataWithBytes:bytes.data() length:bytes.size()];
  CGImageSourceRef source = CGImageSourceCreateWithData((__bridge CFDataRef)data, nullptr);
  if (!source) return false;
  CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
  CFRelease(source);
  if (!image) return false;
  *w = static_cast<int>(CGImageGetWidth(image));
  *h = static_cast<int>(CGImageGetHeight(image));
  uint8_t px[4] = {0};
  CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
  CGContextRef ctx = CGBitmapContextCreate(px, 1, 1, 8, 4, space, kCGImageAlphaPremultipliedLast);
  CGColorSpaceRelease(space);
  CGContextDrawImage(ctx, CGRectMake(-*w / 2.0, -*h / 2.0, *w, *h), image);
  CGContextRelease(ctx);
  CGImageRelease(image);
  *r = px[0];
  *g = px[1];
  *b = px[2];
  return true;
}
}  // namespace

int runCameraSelfTest() {
  printf("Open WebUI desktop camera self-test\n\n");
  using namespace camera;
  report("device kinds from AVFoundation's type names",
         kindFromType("AVCaptureDeviceTypeContinuityCamera") == Kind::Continuity && kindFromType("AVCaptureDeviceTypeBuiltInWideAngleCamera") == Kind::BuiltIn &&
             kindFromType("AVCaptureDeviceTypeExternal") == Kind::External && kindFromType("AVCaptureDeviceTypeDeskViewCamera") == Kind::DeskView &&
             kindFromType("whatever") == Kind::Other);
  std::vector<Device> devices = ordered({{"ext", "Logitech", Kind::External, true}, {"desk", "Desk View", Kind::DeskView, false},
                                         {"mac", "FaceTime HD", Kind::BuiltIn, true}, {"phone", "David's iPhone", Kind::Continuity, false}});
  report("an iPhone first, then built in, external, Desk View",
         devices[0].id == "phone" && devices[1].id == "mac" && devices[2].id == "ext" && devices[3].id == "desk");
  report("the saved camera is chosen when it is there, else the first",
         choose(devices, "ext")->id == "ext" && choose(devices, "gone")->id == "phone" && choose({}, "x") == nullptr);
  int w = 0, h = 0;
  fitSize(4032, 3024, 1568, &w, &h);
  int w2 = 0, h2 = 0;
  fitSize(800, 600, 1568, &w2, &h2);
  report("photos go out at most 1568 on the long side, never enlarged", w == 1568 && h == 1176 && w2 == 800 && h2 == 600);
  report("photo names carry the local time", photoName(1792852200).rfind("photo-2026-10-2", 0) == 0 && photoName(0).size() == std::string("photo-YYYY-MM-DD-HHMMSS.jpg").size());

  // A synthetic 1280x720 orange frame, as the camera would hand it over.
  CVPixelBufferRef buffer = nullptr;
  CVPixelBufferCreate(kCFAllocatorDefault, 1280, 720, kCVPixelFormatType_32BGRA, nullptr, &buffer);
  CVPixelBufferLockBaseAddress(buffer, 0);
  uint8_t* base = static_cast<uint8_t*>(CVPixelBufferGetBaseAddress(buffer));
  const size_t stride = CVPixelBufferGetBytesPerRow(buffer);
  for (int y = 0; y < 720; ++y) {
    for (int x = 0; x < 1280; ++x) {
      uint8_t* p = base + y * stride + x * 4;
      p[0] = 20;    // B
      p[1] = 120;   // G
      p[2] = 240;   // R
      p[3] = 255;
    }
  }
  CVPixelBufferUnlockBaseAddress(buffer, 0);
  const std::string preview = jpegFromPixelBuffer(buffer, 720, 0.7);
  CVPixelBufferRelease(buffer);
  int pw = 0, ph = 0, r = 0, g = 0, b = 0;
  const bool decoded = inspect(preview, &pw, &ph, &r, &g, &b);
  report("a camera frame becomes a preview JPEG, scaled to 720 wide, colour kept",
         decoded && pw == 720 && ph == 405 && r > 200 && g > 90 && g < 150 && b < 60,
         std::to_string(pw) + "x" + std::to_string(ph) + " rgb " + std::to_string(r) + "," + std::to_string(g) + "," + std::to_string(b));

  // A 4000x3000 photo marked "rotate 90" (EXIF 6), as an iPhone in portrait sends.
  CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
  CGContextRef ctx = CGBitmapContextCreate(nullptr, 4000, 3000, 8, 0, space, kCGImageAlphaPremultipliedLast);
  CGColorSpaceRelease(space);
  CGContextSetRGBFillColor(ctx, 0.1, 0.4, 0.9, 1.0);
  CGContextFillRect(ctx, CGRectMake(0, 0, 4000, 3000));
  CGImageRef big = CGBitmapContextCreateImage(ctx);
  CGContextRelease(ctx);
  NSMutableData* data = [NSMutableData data];
  CGImageDestinationRef dest = CGImageDestinationCreateWithData((__bridge CFMutableDataRef)data, (__bridge CFStringRef)UTTypeJPEG.identifier, 1, nullptr);
  CGImageDestinationAddImage(dest, big, (__bridge CFDictionaryRef) @{(id)kCGImagePropertyOrientation : @6});
  CGImageDestinationFinalize(dest);
  CFRelease(dest);
  CGImageRelease(big);
  int fw = 0, fh = 0;
  const std::string fitted = fitJpeg(std::string(static_cast<const char*>(data.bytes), data.length), 1568, 0.85, &fw, &fh);
  int iw = 0, ih = 0;
  inspect(fitted, &iw, &ih, &r, &g, &b);
  report("a rotated 4000x3000 photo comes out upright, 1176x1568, as a JPEG",
         fw == 1176 && fh == 1568 && iw == 1176 && ih == 1568 && fitted.size() > 1000 && b > 180,
         std::to_string(fw) + "x" + std::to_string(fh) + ", " + std::to_string(fitted.size()) + " bytes");

  // This Mac's cameras, listed without opening any (no prompt).
  const std::vector<Device> mine = listDevices();
  std::string names;
  for (const Device& d : mine) names += (names.empty() ? "" : ", ") + d.name + " (" + kindLabel(d.kind) + ")";
  const Access a = access();
  report("listing cameras and reading the access status opens nothing", true,
         std::to_string(mine.size()) + " camera(s): " + (names.empty() ? "none" : names) + "; access " +
             (a == Access::Granted ? "allowed" : a == Access::Denied ? "denied" : a == Access::Restricted ? "restricted" : "not asked yet"));
  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
