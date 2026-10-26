// Async image cache: a key (URL, file id, data: URI) plus a loader that
// returns encoded bytes. Decoding happens on a worker, the Metal upload on
// the UI thread.
#pragma once

#include <functional>
#include <string>

#include "imgui.h"

namespace images {

struct Entry {
  ImTextureID texture = 0;
  int width = 0;
  int height = 0;
  bool loading = false;
  bool failed = false;
  bool ready() const { return texture != 0; }
};

void setDevice(void* metalDevice);

// Looks the key up; starts `loader` on a worker the first time it is seen.
Entry get(const std::string& key, const std::function<std::string()>& loader);

// Drops a cached entry so the next get() reloads it.
void invalidate(const std::string& key);

// Uploads everything decoded since the last frame. Call once per frame.
void uploadPending();

size_t cachedCount();
void clear();

}  // namespace images
