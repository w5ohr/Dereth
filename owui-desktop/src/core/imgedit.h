// Image editing (#116): ComfyUI workflows for inpainting (a painted mask,
// the result pasted back so only the masked area changes), img2img,
// variations and upscaling (lanczos, with an optional detail pass), model
// presets, the mask image, size fitting, and the parsers for ComfyUI's
// replies. Uploads go to ComfyUI's temp folder and results come from
// PreviewImage, so nothing is written to its input or output folders.
// App-free; --imgedittest covers it.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/sketch.h"

namespace imgedit {

using Json = nlohmann::json;

enum class Mode { Inpaint, Img2Img, Variations, Upscale };
const char* modeName(Mode m);

struct Preset {
  int steps = 25;
  double cfg = 6.5;
  std::string sampler = "dpmpp_2m";
  std::string scheduler = "karras";
  bool usesNegative = true;
};
Preset presetFor(const std::string& checkpoint);   // Flux: 4 steps, cfg 1, euler / simple, no negative

struct Job {
  Mode mode = Mode::Inpaint;
  std::string checkpoint;
  std::string image;          // the uploaded source, as ComfyUI names it ("x.png [temp]")
  std::string mask;           // the uploaded mask (inpaint), white = change
  std::string prompt, negative;
  double denoise = 0.75;      // img2img / variations strength; inpaint 1.0 by default
  int64_t seed = 1;
  int count = 4;              // variations
  double scale = 2.0;         // upscale
  bool detail = false;        // upscale: a light img2img pass after scaling
  int grow = 8;               // inpaint: mask grown by this many pixels
  Preset preset;
};
Json workflow(const Job& job);
// Every ["node", output] reference in a workflow points at a node that exists.
bool linksValid(const Json& workflow, std::string* bad = nullptr);

// The mask as an RGB PNG the size of the image: strokes white on black.
std::string maskPng(const sketch::Doc& strokes, int width, int height);
bool maskEmpty(const sketch::Doc& strokes);
// A size for SD models: both sides multiples of 8 and the longer side at most `maxSide`.
void fit(int width, int height, int maxSide, int* outW, int* outH);
// The size to run a model at: SDXL and Flux about one megapixel (small images
// scaled up to 2x, large ones down, longest side at most 1536), SD 1.x about
// 0.4 MP, anything else left at its fitted size. Multiples of 8.
void workingSize(int width, int height, const std::string& checkpoint, int* outW, int* outH);

std::string uploadedName(const Json& uploadReply);                 // {"name","subfolder","type"} -> "name [temp]"
struct Output {
  std::string filename, subfolder, type;
};
// /history/<id>: the images of the finished prompt; *done false while it runs,
// *error set when ComfyUI reports a failure.
std::vector<Output> historyImages(const Json& history, const std::string& promptId, bool* done, std::string* error);
std::string viewQuery(const Output& o);                            // "filename=...&subfolder=...&type=temp"
std::vector<std::string> checkpoints(const Json& objectInfo);      // /object_info/CheckpointLoaderSimple
std::string summary(const Job& job);                               // "inpaint - sd_xl_base - 25 steps - seed 7"

}  // namespace imgedit
