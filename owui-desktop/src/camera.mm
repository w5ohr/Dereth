// Camera capture (#113), the system side and the Camera sheet: AVFoundation
// devices (built in, external, an iPhone through Continuity Camera, Desk
// View), a low-rate live preview, a real photo resized for vision models,
// and attaching it to the message. The camera runs only while the sheet is
// open, and macOS is asked for access only from the sheet's button.
#import <AVFoundation/AVFoundation.h>
#import <Cocoa/Cocoa.h>
#import <CoreImage/CoreImage.h>
#import <ImageIO/ImageIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <atomic>
#include <ctime>
#include <functional>
#include <mutex>

#include "app.h"
#include "core/async.h"
#include "core/camera.h"
#include "core/common.h"
#include "core/settings.h"
#include "ui/images.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace camera {

enum class Access { NotDetermined, Granted, Denied, Restricted };

Access access() {
  switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo]) {
    case AVAuthorizationStatusAuthorized: return Access::Granted;
    case AVAuthorizationStatusDenied: return Access::Denied;
    case AVAuthorizationStatusRestricted: return Access::Restricted;
    default: return Access::NotDetermined;
  }
}

void requestAccess(std::function<void(bool)> done) {
  [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
                           completionHandler:^(BOOL granted) {
                             dispatch_async(dispatch_get_main_queue(), ^{
                               if (done) done(granted);
                             });
                           }];
}

std::vector<Device> listDevices() {
  NSMutableArray<AVCaptureDeviceType>* types = [NSMutableArray arrayWithObject:AVCaptureDeviceTypeBuiltInWideAngleCamera];
  if (@available(macOS 14.0, *)) {
    [types addObject:AVCaptureDeviceTypeExternal];
    [types addObject:AVCaptureDeviceTypeContinuityCamera];
  } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [types addObject:AVCaptureDeviceTypeExternalUnknown];
#pragma clang diagnostic pop
  }
  [types addObject:AVCaptureDeviceTypeDeskViewCamera];
  AVCaptureDeviceDiscoverySession* discovery = [AVCaptureDeviceDiscoverySession discoverySessionWithDeviceTypes:types
                                                                                                        mediaType:AVMediaTypeVideo
                                                                                                         position:AVCaptureDevicePositionUnspecified];
  std::vector<Device> out;
  for (AVCaptureDevice* d in discovery.devices) {
    Device device;
    device.id = d.uniqueID.UTF8String ?: "";
    device.name = d.localizedName.UTF8String ?: "Camera";
    device.kind = kindFromType(d.deviceType.UTF8String ?: "");
    // An iPhone can arrive typed as external; the flag says what it is.
    if (@available(macOS 13.0, *)) {
      if (d.continuityCamera && device.kind != Kind::DeskView) device.kind = Kind::Continuity;
    }
    device.front = device.kind == Kind::BuiltIn || device.kind == Kind::External;   // facing you: mirror the preview
    out.push_back(device);
  }
  return ordered(out);
}

namespace {

CIContext* ciContext() {
  static CIContext* context = [CIContext contextWithOptions:@{kCIContextUseSoftwareRenderer : @NO}];
  return context;
}

std::string jpegOf(CGImageRef image, double quality) {
  NSMutableData* data = [NSMutableData data];
  CGImageDestinationRef dest = CGImageDestinationCreateWithData((__bridge CFMutableDataRef)data, (__bridge CFStringRef)UTTypeJPEG.identifier, 1, nullptr);
  if (!dest) return "";
  NSDictionary* props = @{(id)kCGImageDestinationLossyCompressionQuality : @(quality)};
  CGImageDestinationAddImage(dest, image, (__bridge CFDictionaryRef)props);
  const bool ok = CGImageDestinationFinalize(dest);
  CFRelease(dest);
  return ok ? std::string(static_cast<const char*>(data.bytes), data.length) : std::string();
}

}  // namespace

// A frame from the camera (BGRA) as a JPEG at most maxSide on its longest side.
std::string jpegFromPixelBuffer(CVPixelBufferRef buffer, int maxSide, double quality) {
  if (!buffer) return "";
  CIImage* image = [CIImage imageWithCVPixelBuffer:buffer];
  const CGRect extent = image.extent;
  int w = 0, h = 0;
  fitSize(static_cast<int>(extent.size.width), static_cast<int>(extent.size.height), maxSide, &w, &h);
  if (w <= 0 || h <= 0) return "";
  const double scale = static_cast<double>(w) / extent.size.width;
  CIImage* scaled = [image imageByApplyingTransform:CGAffineTransformMakeScale(scale, scale)];
  CGImageRef cg = [ciContext() createCGImage:scaled fromRect:CGRectMake(0, 0, w, h)];
  if (!cg) return "";
  std::string out = jpegOf(cg, quality);
  CGImageRelease(cg);
  return out;
}

// An encoded photo (JPEG or HEIC, any orientation) re-encoded upright as a
// JPEG at most maxSide on its longest side.
std::string fitJpeg(const std::string& encoded, int maxSide, double quality, int* outWidth, int* outHeight) {
  NSData* data = [NSData dataWithBytes:encoded.data() length:encoded.size()];
  CGImageSourceRef source = CGImageSourceCreateWithData((__bridge CFDataRef)data, nullptr);
  if (!source) return "";
  NSDictionary* options = @{(id)kCGImageSourceCreateThumbnailFromImageAlways : @YES,
                            (id)kCGImageSourceCreateThumbnailWithTransform : @YES,
                            (id)kCGImageSourceThumbnailMaxPixelSize : @(maxSide)};
  CGImageRef image = CGImageSourceCreateThumbnailAtIndex(source, 0, (__bridge CFDictionaryRef)options);
  CFRelease(source);
  if (!image) return "";
  if (outWidth) *outWidth = static_cast<int>(CGImageGetWidth(image));
  if (outHeight) *outHeight = static_cast<int>(CGImageGetHeight(image));
  std::string out = jpegOf(image, quality);
  CGImageRelease(image);
  return out;
}

}  // namespace camera

@interface OWUICamera : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate, AVCapturePhotoCaptureDelegate>
@property(nonatomic, strong) AVCaptureSession* session;
@property(nonatomic, strong) AVCapturePhotoOutput* photoOutput;
@property(nonatomic, strong) dispatch_queue_t queue;
@property(nonatomic, copy) NSString* deviceId;
- (BOOL)startWithDevice:(NSString*)deviceId error:(std::string*)error;
- (void)stop;
- (std::string)preview:(uint64_t*)seq;
- (void)takePhoto:(std::function<void(std::string jpeg, std::string error)>)done;
@end

@implementation OWUICamera {
  std::mutex _mutex;
  std::string _preview;
  uint64_t _seq;
  double _lastPreview;
  std::function<void(std::string, std::string)> _photoDone;
}

- (BOOL)startWithDevice:(NSString*)deviceId error:(std::string*)error {
  [self stop];
  AVCaptureDevice* device = [AVCaptureDevice deviceWithUniqueID:deviceId];
  if (!device) {
    *error = "that camera is not connected";
    return NO;
  }
  NSError* err = nil;
  AVCaptureDeviceInput* input = [AVCaptureDeviceInput deviceInputWithDevice:device error:&err];
  if (!input) {
    *error = err.localizedDescription.UTF8String ?: "the camera could not be opened";
    return NO;
  }
  AVCaptureSession* session = [[AVCaptureSession alloc] init];
  session.sessionPreset = AVCaptureSessionPresetPhoto;
  if (![session canAddInput:input]) {
    *error = "the camera is busy";
    return NO;
  }
  [session addInput:input];
  AVCaptureVideoDataOutput* video = [[AVCaptureVideoDataOutput alloc] init];
  video.videoSettings = @{(id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)};
  video.alwaysDiscardsLateVideoFrames = YES;
  self.queue = dispatch_queue_create("owui.camera", DISPATCH_QUEUE_SERIAL);
  [video setSampleBufferDelegate:self queue:self.queue];
  if ([session canAddOutput:video]) [session addOutput:video];
  AVCapturePhotoOutput* photo = [[AVCapturePhotoOutput alloc] init];
  if ([session canAddOutput:photo]) [session addOutput:photo];
  self.photoOutput = photo;
  self.session = session;
  self.deviceId = deviceId;
  dispatch_async(self.queue, ^{
    [session startRunning];
  });
  return YES;
}

- (void)stop {
  AVCaptureSession* session = self.session;
  self.session = nil;
  self.photoOutput = nil;
  self.deviceId = nil;
  if (session) [session stopRunning];
  std::lock_guard<std::mutex> lock(_mutex);
  _preview.clear();
}

- (void)captureOutput:(AVCaptureOutput*)output didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer fromConnection:(AVCaptureConnection*)connection {
  (void)output;
  (void)connection;
  const double now = [NSProcessInfo processInfo].systemUptime;
  if (now - _lastPreview < 0.12) return;   // about eight preview frames a second is plenty
  _lastPreview = now;
  const std::string jpeg = camera::jpegFromPixelBuffer(CMSampleBufferGetImageBuffer(sampleBuffer), 720, 0.7);
  if (jpeg.empty()) return;
  std::lock_guard<std::mutex> lock(_mutex);
  _preview = jpeg;
  ++_seq;
}

- (std::string)preview:(uint64_t*)seq {
  std::lock_guard<std::mutex> lock(_mutex);
  *seq = _seq;
  return _preview;
}

- (void)takePhoto:(std::function<void(std::string, std::string)>)done {
  if (!self.photoOutput || !self.session.running) {
    done("", "the camera is not running");
    return;
  }
  _photoDone = done;
  AVCapturePhotoSettings* settings = [AVCapturePhotoSettings photoSettingsWithFormat:@{AVVideoCodecKey : AVVideoCodecTypeJPEG}];
  [self.photoOutput capturePhotoWithSettings:settings delegate:self];
}

- (void)captureOutput:(AVCapturePhotoOutput*)output didFinishProcessingPhoto:(AVCapturePhoto*)photo error:(NSError*)error {
  (void)output;
  std::string jpeg, failure;
  if (error) {
    failure = error.localizedDescription.UTF8String ?: "the photo failed";
  } else {
    NSData* data = photo.fileDataRepresentation;
    if (data) jpeg = camera::fitJpeg(std::string(static_cast<const char*>(data.bytes), data.length), 1568, 0.85, nullptr, nullptr);
    if (jpeg.empty()) failure = "the photo could not be encoded";
  }
  auto done = _photoDone;
  _photoDone = nullptr;
  dispatch_async(dispatch_get_main_queue(), ^{
    if (done) done(jpeg, failure);
  });
}

@end

namespace app {
namespace {

OWUICamera* gCamera = nil;
std::vector<camera::Device> gDevices;
std::string gDeviceId;
std::string gPhoto;              // taken, waiting to be attached or retaken
uint64_t gPhotoSeq = 0;
std::string gCameraError;
bool gTaking = false;
double gSheetShownAt = 0;
uint64_t gShownPreviewSeq = 0;

OWUICamera* cam() {
  if (!gCamera) gCamera = [[OWUICamera alloc] init];
  return gCamera;
}

bool mirrored() {
  for (const camera::Device& d : gDevices) {
    if (d.id == gDeviceId) return d.front;
  }
  return false;
}

void startCamera() {
  gCameraError.clear();
  if (gDeviceId.empty()) return;
  std::string error;
  if (![cam() startWithDevice:[NSString stringWithUTF8String:gDeviceId.c_str()] error:&error]) gCameraError = error;
}

}  // namespace

void App::openCamera() {
  showCamera = true;
  gPhoto.clear();
  gDevices = camera::listDevices();
  const camera::Device* d = camera::choose(gDevices, settings().extra.value("cameraDevice", ""));
  gDeviceId = d ? d->id : "";
}

void App::tickCamera() {
  // The camera (and its light) is on only while a window shows the sheet.
  if (gCamera && gCamera.session && core::nowMillis() - gSheetShownAt > 800.0) [gCamera stop];
}

void App::drawCameraSheet() {
  if (!showCamera) return;
  gSheetShownAt = core::nowMillis();
  ImGui::OpenPopup("Camera");
  ImGui::SetNextWindowSize(ImVec2(760, 640), ImGuiCond_Appearing);
  if (!ImGui::BeginPopupModal("Camera", &showCamera)) return;
  if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    showCamera = false;
    ImGui::CloseCurrentPopup();
  }
  const camera::Access access = camera::access();
  if (access != camera::Access::Granted) {
    widgets::dimText("%s", access == camera::Access::NotDetermined ? "Camera access has not been asked for yet."
                                                                   : "Camera access is off for Open WebUI.");
    if (access == camera::Access::NotDetermined) {
      if (widgets::primaryButton("Allow camera access")) {
        camera::requestAccess([](bool granted) {
          if (granted) startCamera();
          else widgets::toastInfo("Camera access was not allowed");
        });
      }
    } else if (widgets::ghostButton("Open Camera privacy settings")) {
      [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_Camera"]];
    }
  } else {
    // Device picker; switching restarts the camera.
    ImGui::SetNextItemWidth(420.0f);
    std::string current = "(no camera)";
    for (const camera::Device& d : gDevices) {
      if (d.id == gDeviceId) current = d.name + " - " + camera::kindLabel(d.kind);
    }
    if (ImGui::BeginCombo("Camera", current.c_str())) {
      for (const camera::Device& d : gDevices) {
        const std::string label = d.name + " - " + camera::kindLabel(d.kind) + "##" + d.id;
        if (ImGui::Selectable(label.c_str(), d.id == gDeviceId)) {
          gDeviceId = d.id;
          settings().extra["cameraDevice"] = d.id;
          settings().save();
          startCamera();
        }
      }
      ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh")) gDevices = camera::listDevices();
    widgets::dimText("%s", "An iPhone near this Mac shows up here when Continuity Camera is on.");
    if (gDevices.empty()) widgets::dimText("%s", "No camera is connected.");
    if (!gCamera.session && gPhoto.empty() && !gDeviceId.empty() && gCameraError.empty()) startCamera();
  }
  if (!gCameraError.empty()) widgets::dimText("%s", gCameraError.c_str());

  // Preview, or the photo just taken.
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const ImVec2 box(avail.x, std::max(200.0f, avail.y - 48.0f));
  std::string key, bytes;
  if (!gPhoto.empty()) {
    key = "camera-photo:" + std::to_string(gPhotoSeq);
    bytes = gPhoto;
  } else if (gCamera.session) {
    uint64_t seq = 0;
    bytes = [cam() preview:&seq];
    if (seq != gShownPreviewSeq) {
      images::invalidate("camera-preview");
      gShownPreviewSeq = seq;
    }
    key = "camera-preview";
  }
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImGui::GetWindowDrawList()->AddRectFilled(origin, ImVec2(origin.x + box.x, origin.y + box.y), theme::palette().panelAlt, 8.0f);
  if (!key.empty() && !bytes.empty()) {
    images::Entry e = images::get(key, [bytes] { return bytes; });
    if (e.ready() && e.width > 0 && e.height > 0) {
      const float scale = std::min(box.x / e.width, box.y / e.height);
      const ImVec2 size(e.width * scale, e.height * scale);
      const ImVec2 at(origin.x + (box.x - size.x) * 0.5f, origin.y + (box.y - size.y) * 0.5f);
      const bool flip = gPhoto.empty() && mirrored();   // a preview is a mirror; the photo is not
      ImGui::GetWindowDrawList()->AddImage(e.texture, at, ImVec2(at.x + size.x, at.y + size.y), ImVec2(flip ? 1.0f : 0.0f, 0), ImVec2(flip ? 0.0f : 1.0f, 1));
    }
  }
  ImGui::Dummy(box);

  if (gPhoto.empty()) {
    widgets::beginDisabled(!gCamera.session || gTaking);
    if (widgets::primaryButton(gTaking ? "Taking..." : "Take photo")) {
      gTaking = true;
      [cam() takePhoto:[](std::string jpeg, std::string error) {
        gTaking = false;
        if (!error.empty()) {
          widgets::toastError(error);
          return;
        }
        gPhoto = jpeg;
        ++gPhotoSeq;
      }];
    }
    widgets::endDisabled(!gCamera.session || gTaking);
  } else {
    if (widgets::primaryButton("Attach to message")) {
      attachBytes(camera::photoName(static_cast<double>(std::time(nullptr))), "image/jpeg", gPhoto);
      gPhoto.clear();
      showCamera = false;
      focusComposer = true;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (widgets::ghostButton("Retake")) gPhoto.clear();
  }
  ImGui::SameLine();
  if (widgets::ghostButton("Close")) {
    showCamera = false;
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

}  // namespace app
