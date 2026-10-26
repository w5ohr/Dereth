#include "ui/images.h"

#import <Metal/Metal.h>

#include <mutex>
#include <unordered_map>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "core/async.h"

namespace images {
namespace {

struct Decoded {
  std::string key;
  std::vector<unsigned char> rgba;
  int width = 0;
  int height = 0;
  bool failed = false;
};

id<MTLDevice> gDevice = nil;
NSMutableArray* gRetained = nil;   // keeps MTLTexture objects alive under ARC

std::unordered_map<std::string, Entry> gCache;
std::mutex gCacheMutex;

std::vector<Decoded> gPending;
std::mutex gPendingMutex;

}  // namespace

void setDevice(void* metalDevice) {
  gDevice = (__bridge id<MTLDevice>)metalDevice;
  gRetained = [NSMutableArray array];
}

Entry get(const std::string& key, const std::function<std::string()>& loader) {
  {
    std::lock_guard<std::mutex> lock(gCacheMutex);
    auto it = gCache.find(key);
    if (it != gCache.end()) return it->second;
    Entry fresh;
    fresh.loading = true;
    gCache[key] = fresh;
  }

  async::run([key, loader] {
    Decoded decoded;
    decoded.key = key;
    std::string bytes = loader();
    if (bytes.empty()) {
      decoded.failed = true;
    } else {
      int w = 0, h = 0, channels = 0;
      unsigned char* pixels =
          stbi_load_from_memory(reinterpret_cast<const unsigned char*>(bytes.data()),
                                static_cast<int>(bytes.size()), &w, &h, &channels, 4);
      if (!pixels) {
        decoded.failed = true;
      } else {
        decoded.width = w;
        decoded.height = h;
        decoded.rgba.assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
        stbi_image_free(pixels);
      }
    }
    std::lock_guard<std::mutex> lock(gPendingMutex);
    gPending.push_back(std::move(decoded));
  });

  Entry pending;
  pending.loading = true;
  return pending;
}

void invalidate(const std::string& key) {
  std::lock_guard<std::mutex> lock(gCacheMutex);
  gCache.erase(key);
}

void uploadPending() {
  std::vector<Decoded> batch;
  {
    std::lock_guard<std::mutex> lock(gPendingMutex);
    batch.swap(gPending);
  }
  if (batch.empty()) return;

  for (auto& decoded : batch) {
    Entry entry;
    if (decoded.failed || !gDevice) {
      entry.failed = true;
    } else {
      MTLTextureDescriptor* desc = [MTLTextureDescriptor
          texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                       width:decoded.width
                                      height:decoded.height
                                   mipmapped:NO];
      desc.usage = MTLTextureUsageShaderRead;
      desc.storageMode = MTLStorageModeManaged;
      id<MTLTexture> texture = [gDevice newTextureWithDescriptor:desc];
      if (!texture) {
        entry.failed = true;
      } else {
        [texture replaceRegion:MTLRegionMake2D(0, 0, decoded.width, decoded.height)
                   mipmapLevel:0
                     withBytes:decoded.rgba.data()
                   bytesPerRow:decoded.width * 4];
        [gRetained addObject:texture];
        entry.texture = (ImTextureID)(intptr_t)(__bridge void*)texture;
        entry.width = decoded.width;
        entry.height = decoded.height;
      }
    }
    std::lock_guard<std::mutex> lock(gCacheMutex);
    gCache[decoded.key] = entry;
  }
}

size_t cachedCount() {
  std::lock_guard<std::mutex> lock(gCacheMutex);
  return gCache.size();
}

void clear() {
  std::lock_guard<std::mutex> lock(gCacheMutex);
  gCache.clear();
  if (gRetained) [gRetained removeAllObjects];
}

}  // namespace images
