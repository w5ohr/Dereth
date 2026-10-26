// Image editor (#116): inpaint with a mask brush, img2img, variations and
// upscaling against the ComfyUI at Settings > Connection's ComfyUI URL.
// Sources are resized to a size SD models like (multiples of 8, longest side
// 1536) with Core Graphics, uploaded to ComfyUI's temp folder, and results
// come back through PreviewImage (temp too) - nothing lands in ComfyUI's
// input or output folders. Results can be saved to the gallery with their
// prompt, model, seed and mode written into the PNG, or edited further.
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>

#include <atomic>
#include <random>

#include "app.h"
#include "core/async.h"
#include "core/common.h"
#include "core/http.h"
#include "core/imagemeta.h"
#include "core/imgedit.h"
#include "core/updater.h"
#include "imgui_stdlib.h"
#include "platform.h"
#include "ui/images.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

struct Result {
  std::string png;
  std::string key;   // image cache key
};

struct State {
  std::string name;          // where it came from
  std::string png;           // the fitted source, PNG
  std::string key;
  int width = 0, height = 0;
  sketch::Doc mask;          // in image pixels
  sketch::Stroke current;
  bool painting = false;
  float brush = 40.0f;
  imgedit::Job job;
  std::vector<std::string> checkpoints;
  bool loadingModels = false;
  std::atomic<bool> running{false};
  std::string status;
  double started = 0.0;
  std::vector<Result> results;
  int shown = -1;            // a result shown big instead of the source
  bool openCanvasFocus = false;
};
State gE;
int gMode = 0;

std::string base() {
  std::string u = core::trim(core::Settings::instance().comfyUrl);
  while (!u.empty() && u.back() == '/') u.pop_back();
  return u;
}

// Decode with ImageIO and redraw at a size SD models accept (or exactly
// targetW x targetH when given); returns PNG.
std::string fitPng(const std::string& bytes, int* w, int* h, std::string* error, int targetW = 0, int targetH = 0) {
  @autoreleasepool {
    NSData* data = [NSData dataWithBytes:bytes.data() length:bytes.size()];
    CGImageSourceRef src = CGImageSourceCreateWithData((__bridge CFDataRef)data, nullptr);
    CGImageRef img = src ? CGImageSourceCreateImageAtIndex(src, 0, nullptr) : nullptr;
    if (src) CFRelease(src);
    if (!img) {
      if (error) *error = "not an image this Mac can read";
      return "";
    }
    if (targetW > 0 && targetH > 0) {
      *w = targetW;
      *h = targetH;
    } else {
      imgedit::fit(static_cast<int>(CGImageGetWidth(img)), static_cast<int>(CGImageGetHeight(img)), 1536, w, h);
    }
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(nullptr, *w, *h, 8, 0, space, kCGImageAlphaNoneSkipLast);
    CGColorSpaceRelease(space);
    CGContextSetInterpolationQuality(ctx, kCGInterpolationHigh);
    CGContextDrawImage(ctx, CGRectMake(0, 0, *w, *h), img);
    CGImageRelease(img);
    CGImageRef out = CGBitmapContextCreateImage(ctx);
    CGContextRelease(ctx);
    NSMutableData* png = [NSMutableData data];
    CGImageDestinationRef dst = CGImageDestinationCreateWithData((__bridge CFMutableDataRef)png, CFSTR("public.png"), 1, nullptr);
    CGImageDestinationAddImage(dst, out, nullptr);
    CGImageDestinationFinalize(dst);
    CFRelease(dst);
    CGImageRelease(out);
    return std::string(static_cast<const char*>(png.bytes), png.length);
  }
}

net::Response comfy(const std::string& method, const std::string& path, const std::string& body = "") {
  net::Request r;
  r.method = method;
  r.url = base() + path;
  r.body = body;
  r.timeoutSec = 60;
  r.connectTimeoutSec = 5;
  return net::perform(r);
}

std::string upload(const std::string& png, const std::string& name, std::string* error) {
  net::Request r;
  r.method = "POST";
  r.url = base() + "/upload/image";
  r.files.push_back(net::FilePart{"image", name, "image/png", png});
  r.fields = {{"type", "temp"}, {"overwrite", "true"}};
  r.timeoutSec = 60;
  const net::Response res = net::perform(r);
  if (!res.error.empty() || res.status != 200) {
    if (error) *error = "upload failed: " + (res.error.empty() ? "HTTP " + std::to_string(res.status) + " " + res.body.substr(0, 200) : res.error);
    return "";
  }
  return imgedit::uploadedName(Json::parse(res.body, nullptr, false));
}

void setSource(const std::string& name, const std::string& png, int w, int h) {
  gE.name = name;
  gE.png = png;
  gE.width = w;
  gE.height = h;
  gE.key = "edit-src:" + updater::sha256Hex(png).substr(0, 16);
  gE.mask = sketch::Doc{};
  gE.mask.width = w;
  gE.mask.height = h;
  gE.shown = -1;
}

}  // namespace

void App::openImageEditor(const std::string& bytes, const std::string& name) {
  std::string error;
  int w = 0, h = 0;
  const std::string png = fitPng(bytes, &w, &h, &error);
  if (png.empty()) {
    widgets::toastError("Can't edit " + name + ": " + error);
    return;
  }
  setSource(name, png, w, h);
  gE.results.clear();
  gE.status.clear();
  showImageEditor = true;
  if (gE.checkpoints.empty() && !gE.loadingModels) {
    gE.loadingModels = true;
    async::run([] {
      const net::Response r = comfy("GET", "/object_info/CheckpointLoaderSimple");
      const auto list = r.status == 200 ? imgedit::checkpoints(Json::parse(r.body, nullptr, false)) : std::vector<std::string>{};
      const std::string err = r.status == 200 ? "" : (r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error);
      async::post([list, err] {
        gE.loadingModels = false;
        gE.checkpoints = list;
        if (!err.empty()) gE.status = "ComfyUI at " + base() + " is not reachable: " + err;
        if (gE.job.checkpoint.empty() && !list.empty()) {
          gE.job.checkpoint = list.front();
          for (const auto& c : list) {
            if (c.find("sd_xl") != std::string::npos || c.find("sdxl") != std::string::npos) gE.job.checkpoint = c;   // SDXL inpaints better than schnell
          }
          gE.job.preset = imgedit::presetFor(gE.job.checkpoint);
        }
      });
    });
  }
}

void App::openImageFileInEditor() {
  const auto picked = platform::openFiles(false);
  if (picked.empty()) return;
  bool ok = false;
  const std::string bytes = core::readFile(picked.front(), &ok);
  if (ok) openImageEditor(bytes, core::baseName(picked.front()));
}

void App::runImageEdit() {
  if (gE.running || gE.png.empty()) return;
  imgedit::Job job = gE.job;
  job.mode = static_cast<imgedit::Mode>(gMode);
  if (job.mode == imgedit::Mode::Inpaint && imgedit::maskEmpty(gE.mask)) {
    gE.status = "Paint over the part to change first.";
    return;
  }
  if (job.mode != imgedit::Mode::Upscale && core::trim(job.prompt).empty()) {
    gE.status = "Describe what you want there.";
    return;
  }
  if (job.mode == imgedit::Mode::Inpaint) job.denoise = 1.0;
  // Run at the model's working size (SDXL / Flux about a megapixel); the mask follows.
  int ww = gE.width, wh = gE.height;
  if (job.mode != imgedit::Mode::Upscale) imgedit::workingSize(gE.width, gE.height, job.checkpoint, &ww, &wh);
  std::string png = gE.png;
  if (ww != gE.width || wh != gE.height) {
    std::string err;
    int rw = 0, rh = 0;
    png = fitPng(gE.png, &rw, &rh, &err, ww, wh);
    if (png.empty()) png = gE.png, ww = gE.width, wh = gE.height;
  }
  const std::string mask = job.mode == imgedit::Mode::Inpaint ? imgedit::maskPng(gE.mask, ww, wh) : "";
  gE.running = true;
  gE.started = core::nowMillis();
  gE.status = "Uploading...";
  async::run([this, job, png, mask]() mutable {
    std::string error;
    const std::string tag = std::to_string(core::nowSeconds());
    job.image = upload(png, "owui-src-" + tag + ".png", &error);
    if (error.empty() && !mask.empty()) job.mask = upload(mask, "owui-mask-" + tag + ".png", &error);
    std::vector<Result> out;
    if (error.empty()) {
      const Json wf = imgedit::workflow(job);
      const net::Response q = comfy("POST", "/prompt", Json{{"prompt", wf}, {"client_id", "owui-desktop"}}.dump());
      const Json qj = Json::parse(q.body, nullptr, false);
      const std::string id = qj.is_object() ? qj.value("prompt_id", "") : "";
      if (id.empty()) {
        error = "ComfyUI refused the workflow: " + (q.error.empty() ? q.body.substr(0, 300) : q.error);
      } else {
        async::post([] { gE.status = "Running on ComfyUI..."; });
        bool done = false;
        std::vector<imgedit::Output> outputs;
        for (int i = 0; i < 1200 && !done; ++i) {   // ten minutes at most
          usleep(500 * 1000);
          const net::Response h = comfy("GET", "/history/" + id);
          if (h.status == 200) outputs = imgedit::historyImages(Json::parse(h.body, nullptr, false), id, &done, &error);
        }
        if (!done && error.empty()) error = "ComfyUI did not finish in ten minutes";
        for (const auto& o : outputs) {
          const net::Response v = comfy("GET", "/view?" + imgedit::viewQuery(o));
          if (v.status == 200 && !v.body.empty()) out.push_back(Result{v.body, "edit-out:" + id + ":" + o.filename});
        }
        if (done && error.empty() && out.empty()) error = "ComfyUI returned no image";
      }
    }
    const double secs = (core::nowMillis() - gE.started) / 1000.0;
    async::post([out, error, secs, job] {
      gE.running = false;
      if (!error.empty()) {
        gE.status = error;
        return;
      }
      gE.results = out;
      gE.shown = out.empty() ? -1 : 0;
      char buf[96];
      snprintf(buf, sizeof(buf), "%zu result%s in %.1f s - ", out.size(), out.size() == 1 ? "" : "s", secs);
      gE.status = buf + imgedit::summary(job);
      if (!out.empty()) {
        int w = 0, h = 0;
        if (imagemeta::pngSize(out.front().png, &w, &h)) gE.status += " - " + std::to_string(w) + "x" + std::to_string(h);
      }
      gE.job.seed = job.seed;
    });
  });
}

void App::drawImageEditor() {
  if (!showImageEditor) return;
  const auto& palette = theme::palette();
  ImGui::OpenPopup("Edit image");
  ImGui::SetNextWindowSize(ImVec2(1180, 760), ImGuiCond_Appearing);
  if (!ImGui::BeginPopupModal("Edit image", &showImageEditor)) return;
  if (ImGui::IsKeyPressed(ImGuiKey_Escape) && !gE.running) {
    showImageEditor = false;
    ImGui::CloseCurrentPopup();
  }
  ImGuiIO& io = ImGui::GetIO();
  if (!gE.painting && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) sketch::undo(gE.mask);

  // Canvas: the source (or a result) fitted, the mask painted over it.
  const float panel = 360.0f;
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const float canvasW = avail.x - panel - 12.0f, canvasH = avail.y - 140.0f;
  const bool showingResult = gE.shown >= 0 && gE.shown < static_cast<int>(gE.results.size());
  const std::string key = showingResult ? gE.results[static_cast<size_t>(gE.shown)].key : gE.key;
  const std::string bytes = showingResult ? gE.results[static_cast<size_t>(gE.shown)].png : gE.png;
  images::Entry img = images::get(key, [bytes] { return bytes; });
  ImGui::BeginGroup();
  const float scale = gE.width > 0 ? std::min(canvasW / gE.width, canvasH / gE.height) : 1.0f;
  const ImVec2 size(gE.width * scale, gE.height * scale);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(origin, ImVec2(origin.x + canvasW, origin.y + canvasH), palette.panelAlt, 6.0f);
  const ImVec2 at(origin.x + (canvasW - size.x) / 2, origin.y + (canvasH - size.y) / 2);
  ImGui::SetCursorScreenPos(at);
  if (img.ready()) ImGui::Image(img.texture, size);
  else ImGui::Dummy(size);
  ImGui::SetCursorScreenPos(at);
  ImGui::InvisibleButton("##editcanvas", size);
  const bool canPaint = gMode == static_cast<int>(imgedit::Mode::Inpaint) && !showingResult && !gE.running;
  const ImVec2 m = ImGui::GetMousePos();
  const sketch::Point here{std::clamp((m.x - at.x) / scale, 0.0f, static_cast<float>(gE.width)), std::clamp((m.y - at.y) / scale, 0.0f, static_cast<float>(gE.height))};
  if (canPaint && ImGui::IsItemActivated()) {
    gE.painting = true;
    gE.current = sketch::Stroke{};
    gE.current.width = gE.brush;
    gE.current.eraser = io.KeyAlt;   // Option paints the mask away
    gE.current.points.push_back(here);
  }
  if (gE.painting && ImGui::IsItemActive()) {
    const auto& last = gE.current.points.back();
    if (std::hypot(here.x - last.x, here.y - last.y) >= 2.0f) gE.current.points.push_back(here);
  }
  if (gE.painting && ImGui::IsItemDeactivated()) {
    gE.painting = false;
    sketch::add(gE.mask, gE.current);
  }
  if (!showingResult && gMode == static_cast<int>(imgedit::Mode::Inpaint)) {
    // Show the mask as translucent red.
    dl->PushClipRect(at, ImVec2(at.x + size.x, at.y + size.y), true);
    auto drawStroke = [&](const sketch::Stroke& s) {
      const ImU32 col = s.eraser ? IM_COL32(0, 0, 0, 0) : IM_COL32(230, 40, 60, 110);
      if (s.eraser) return;
      const float r = s.width * scale * 0.5f;
      for (size_t i = 0; i < s.points.size(); ++i) {
        const ImVec2 p(at.x + s.points[i].x * scale, at.y + s.points[i].y * scale);
        dl->AddCircleFilled(p, r, col);
        if (i > 0) dl->AddLine(ImVec2(at.x + s.points[i - 1].x * scale, at.y + s.points[i - 1].y * scale), p, col, r * 2);
      }
    };
    for (const auto& s : gE.mask.strokes) drawStroke(s);
    if (gE.painting) drawStroke(gE.current);
    if (canPaint && ImGui::IsItemHovered()) dl->AddCircle(m, gE.brush * scale * 0.5f, IM_COL32(255, 255, 255, 180), 0, 1.5f);
    dl->PopClipRect();
  }
  ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + canvasH + 6));
  widgets::dimText("%s - %d x %d%s", gE.name.c_str(), gE.width, gE.height, showingResult ? "  (showing a result - click Source to paint)" : "");
  // Results strip.
  if (!gE.results.empty()) {
    if (ImGui::SmallButton("Source")) gE.shown = -1;
    for (size_t i = 0; i < gE.results.size(); ++i) {
      ImGui::SameLine();
      ImGui::PushID(static_cast<int>(i));
      images::Entry t = images::get(gE.results[i].key, [png = gE.results[i].png] { return png; });
      const float th = 72.0f, tw = gE.height > 0 ? th * gE.width / gE.height : th;
      if (t.ready()) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::ImageButton("##thumb", t.texture, ImVec2(tw, th))) gE.shown = static_cast<int>(i);
        if (gE.shown == static_cast<int>(i)) dl->AddRect(p, ImVec2(p.x + tw + 8, p.y + th + 8), palette.accent, 4.0f, 2.0f);
      }
      ImGui::PopID();
    }
  }
  ImGui::EndGroup();

  // Controls.
  ImGui::SameLine();
  ImGui::BeginChild("##editcontrols", ImVec2(panel, 0), ImGuiChildFlags_None);
  static const char* modes[] = {"Inpaint", "Img2img", "Variations", "Upscale"};
  for (int i = 0; i < 4; ++i) {
    if (i) ImGui::SameLine();
    ImGui::RadioButton(modes[i], &gMode, i);
  }
  ImGui::Separator();
  const imgedit::Mode mode = static_cast<imgedit::Mode>(gMode);
  if (mode == imgedit::Mode::Inpaint) {
    ImGui::PushTextWrapPos(panel - 10);
    widgets::dimText("Paint over what should change (Option paints the mask away, Cmd+Z undoes). Only the painted area changes; the rest stays exactly as it is.");
    ImGui::PopTextWrapPos();
    ImGui::SetNextItemWidth(200);
    ImGui::SliderFloat("Brush", &gE.brush, 8.0f, 240.0f, "%.0f px");
    ImGui::SameLine();
    widgets::beginDisabled(gE.mask.strokes.empty());
    if (ImGui::SmallButton("Clear mask")) {
      gE.mask.strokes.clear();
      gE.mask.redo.clear();
    }
    widgets::endDisabled(gE.mask.strokes.empty());
  }
  if (mode != imgedit::Mode::Upscale || gE.job.detail) {
    ImGui::SetNextItemWidth(panel - 90);
    const std::string current = gE.loadingModels ? "loading..." : gE.job.checkpoint.empty() ? "(no checkpoint)" : gE.job.checkpoint;
    if (ImGui::BeginCombo("Model", current.c_str())) {
      for (const auto& c : gE.checkpoints) {
        if (ImGui::Selectable(c.c_str(), c == gE.job.checkpoint)) {
          gE.job.checkpoint = c;
          gE.job.preset = imgedit::presetFor(c);
        }
      }
      ImGui::EndCombo();
    }
    ImGui::TextUnformatted(mode == imgedit::Mode::Inpaint ? "What goes in the painted area" : "Prompt");
    ImGui::InputTextMultiline("##editprompt", &gE.job.prompt, ImVec2(panel - 10, 70));
    if (gE.job.preset.usesNegative) {
      ImGui::SetNextItemWidth(panel - 10);
      ImGui::InputTextWithHint("##editneg", "Negative prompt (optional)", &gE.job.negative);
    }
  }
  if (mode == imgedit::Mode::Img2Img || mode == imgedit::Mode::Variations) {
    float d = static_cast<float>(gE.job.denoise);
    ImGui::SetNextItemWidth(200);
    if (ImGui::SliderFloat("Strength", &d, 0.1f, 1.0f, "%.2f")) gE.job.denoise = d;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("How far it may move from the source: low keeps the composition, high reimagines it.");
  }
  if (mode == imgedit::Mode::Variations) {
    ImGui::SetNextItemWidth(200);
    ImGui::SliderInt("Count", &gE.job.count, 1, 8);
  }
  if (mode == imgedit::Mode::Upscale) {
    int s = gE.job.scale >= 4 ? 1 : 0;
    ImGui::RadioButton("2x", &s, 0);
    ImGui::SameLine();
    ImGui::RadioButton("4x", &s, 1);
    gE.job.scale = s ? 4.0 : 2.0;
    ImGui::SameLine();
    ImGui::Checkbox("Add detail (a light pass with the model)", &gE.job.detail);
  }
  if (mode != imgedit::Mode::Upscale || gE.job.detail) {
    ImGui::SetNextItemWidth(120);
    ImGui::SliderInt("Steps", &gE.job.preset.steps, 1, 60);
    int seed = static_cast<int>(gE.job.seed);
    ImGui::SetNextItemWidth(120);
    if (ImGui::InputInt("Seed", &seed, 0, 0)) gE.job.seed = std::max(0, seed);
    ImGui::SameLine();
    if (ImGui::SmallButton("New seed")) {
      static std::mt19937 rng{std::random_device{}()};
      gE.job.seed = rng() % 1000000000;
    }
  }
  ImGui::Dummy(ImVec2(0, 6));
  widgets::beginDisabled(gE.running || gE.png.empty());
  if (widgets::primaryButton(gE.running ? "Working..." : "Run", ImVec2(120, 0))) runImageEdit();
  widgets::endDisabled(gE.running || gE.png.empty());
  if (gE.running) {
    ImGui::SameLine();
    widgets::dimText("%.0f s", (core::nowMillis() - gE.started) / 1000.0);
  }
  if (!gE.status.empty()) {
    ImGui::PushTextWrapPos(panel - 10);
    const bool bad = gE.status.find("not reachable") != std::string::npos || gE.status.find("failed") != std::string::npos || gE.status.find("refused") != std::string::npos ||
                     gE.status.find("error") != std::string::npos;
    ImGui::TextColored(theme::toVec4(bad ? palette.danger : palette.textDim), "%s", gE.status.c_str());
    ImGui::PopTextWrapPos();
  }
  widgets::dimText("ComfyUI: %s", base().c_str());
  if (showingResult) {
    ImGui::Separator();
    const Result& r = gE.results[static_cast<size_t>(gE.shown)];
    if (ImGui::Button("Save to gallery")) {
      imagemeta::Meta meta;
      meta.prompt = gE.job.prompt;
      meta.negative = gE.job.negative;
      meta.model = mode == imgedit::Mode::Upscale && !gE.job.detail ? "" : gE.job.checkpoint;
      meta.seed = std::to_string(gE.job.seed);
      meta.engine = std::string("comfyui (") + imgedit::modeName(mode) + " of " + gE.name + ")";
      int w = 0, h = 0;
      if (imagemeta::pngSize(r.png, &w, &h)) meta.size = std::to_string(w) + "x" + std::to_string(h);
      galleryAdd(r.png, meta);
      widgets::toast(widgets::ToastLevel::Success, "Saved to the gallery with its prompt and settings");
    }
    ImGui::SameLine();
    if (ImGui::Button("Edit this one")) {
      int w = 0, h = 0;
      std::string err;
      const std::string png = fitPng(r.png, &w, &h, &err);
      if (!png.empty()) {
        setSource(gE.name + " (edited)", png, w, h);
        gE.results.clear();
        gE.status = "Editing the result.";
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save as...")) {
      const std::string path = platform::saveFile("edited.png");
      if (!path.empty() && core::writeFile(path, r.png)) widgets::toast(widgets::ToastLevel::Success, "Saved " + path);
    }
  }
  ImGui::EndChild();
  ImGui::EndPopup();
}

}  // namespace app
