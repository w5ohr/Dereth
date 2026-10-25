// `Open WebUI --imgedittest`: the ComfyUI workflows (shape and links), model
// presets, the mask image, size fitting and the reply parsers - offline.
#include <cstdio>
#include <string>

#include "core/imgedit.h"
#include "stb_image.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
}  // namespace

int runImgEditSelfTest() {
  using namespace imgedit;
  printf("Open WebUI desktop image-editing self-test\n\n");

  const Preset sdxl = presetFor("sd_xl_base_1.0.safetensors"), flux = presetFor("flux1-schnell-fp8.safetensors");
  report("presets: SDXL 25 steps / cfg 6.5 / karras; Flux schnell 4 steps / cfg 1 / simple / no negative", sdxl.steps == 25 && sdxl.cfg == 6.5 && sdxl.usesNegative && flux.steps == 4 && flux.cfg == 1.0 &&
                                                                                                              flux.scheduler == "simple" && !flux.usesNegative);
  Job j;
  j.checkpoint = "sd_xl_base_1.0.safetensors";
  j.image = "src.png [temp]";
  j.mask = "mask.png [temp]";
  j.prompt = "a red hot air balloon";
  j.negative = "blurry";
  j.denoise = 1.0;
  j.seed = 7;
  j.preset = sdxl;
  std::string bad;
  const Json in = workflow(j);
  report("inpaint: mask grown, noise mask on the latent, result composited over the original through the mask, previewed", linksValid(in, &bad) && in["10"]["inputs"]["channel"] == "red" && in["6"]["class_type"] == "SetLatentNoiseMask" &&
                                                                                                                                 in["12"]["inputs"]["destination"][0] == "2" && in["12"]["inputs"]["mask"][0] == "11" && in["30"]["inputs"]["images"][0] == "12" &&
                                                                                                                                 in["8"]["inputs"]["denoise"] == 1.0 && in["8"]["inputs"]["seed"] == 7 && in["4"]["inputs"]["text"] == "blurry",
         bad);
  j.mode = Mode::Img2Img;
  j.denoise = 0.55;
  const Json i2i = workflow(j);
  report("img2img: no mask nodes, strength as denoise, decoded output previewed", linksValid(i2i) && !i2i.contains("10") && !i2i.contains("12") && i2i["8"]["inputs"]["latent_image"][0] == "5" && i2i["8"]["inputs"]["denoise"] == 0.55 &&
                                                                                       i2i["30"]["inputs"]["images"][0] == "9");
  j.mode = Mode::Variations;
  j.count = 20;
  const Json var = workflow(j);
  report("variations: the latent repeated (capped at 8) before sampling", linksValid(var) && var["7"]["inputs"]["amount"] == 8 && var["8"]["inputs"]["latent_image"][0] == "7");
  j.mode = Mode::Upscale;
  j.scale = 2.0;
  j.detail = false;
  const Json up = workflow(j);
  report("upscale: lanczos scale straight to the preview, no model loaded", linksValid(up) && up.size() == 3 && up["20"]["inputs"]["upscale_method"] == "lanczos" && up["30"]["inputs"]["images"][0] == "20" && !up.contains("1"));
  j.detail = true;
  const Json upd = workflow(j);
  report("upscale + detail: the scaled image re-encoded and sampled at 0.3", linksValid(upd) && upd["5"]["inputs"]["pixels"][0] == "20" && upd["8"]["inputs"]["denoise"] == 0.3);
  Job fl = j;
  fl.mode = Mode::Img2Img;
  fl.checkpoint = "flux1-schnell-fp8.safetensors";
  fl.preset = flux;
  report("Flux drops the negative prompt", workflow(fl)["4"]["inputs"]["text"] == "");
  Json broken = in;
  broken["8"]["inputs"]["model"] = Json::array({"99", 0});
  report("linksValid catches a dangling link", !linksValid(broken, &bad) && bad == "8.model -> 99", bad);

  sketch::Doc strokes;
  strokes.width = 100;
  strokes.height = 50;
  sketch::Stroke s;
  s.points = {{20, 25}, {40, 25}};
  s.width = 10;
  s.color = sketch::Rgb{200, 30, 30};
  sketch::add(strokes, s);
  const std::string png = maskPng(strokes, 200, 100);
  int w = 0, h = 0, n = 0;
  unsigned char* px = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(png.data()), static_cast<int>(png.size()), &w, &h, &n, 4);
  const bool white = px && px[(50 * 200 + 60) * 4] == 255 && px[(50 * 200 + 60) * 4 + 1] == 255;   // the stroke, scaled 2x
  const bool black = px && px[(10 * 200 + 180) * 4] == 0;
  if (px) stbi_image_free(px);
  report("maskPng: strokes scaled to the image, white on black", w == 200 && h == 100 && white && black && !maskEmpty(strokes) && maskEmpty(sketch::Doc{}));
  int fw = 0, fh = 0;
  fit(3000, 2000, 1536, &fw, &fh);
  int gw = 0, gh = 0;
  fit(1023, 769, 1536, &gw, &gh);
  int a = 0, b = 0, c = 0, d = 0, e = 0, f = 0;
  workingSize(768, 512, "sd_xl_base_1.0.safetensors", &a, &b);
  workingSize(3000, 2000, "flux1-schnell-fp8.safetensors", &c, &d);
  workingSize(300, 300, "sd_xl_base_1.0.safetensors", &e, &f);
  report("workingSize: SDXL / Flux near one megapixel, up-scaling capped at 2x, multiples of 8", a == 1248 && b == 832 && c == 1248 && d == 832 && e == 600 && f == 600,
         std::to_string(a) + "x" + std::to_string(b) + " " + std::to_string(c) + "x" + std::to_string(d) + " " + std::to_string(e) + "x" + std::to_string(f));
  report("fit: longest side capped, both sides multiples of 8", fw == 1536 && fh == 1024 && gw == 1016 && gh == 768, std::to_string(fw) + "x" + std::to_string(fh) + ", " + std::to_string(gw) + "x" + std::to_string(gh));

  report("uploadedName", uploadedName(Json{{"name", "a.png"}, {"subfolder", ""}, {"type", "temp"}}) == "a.png [temp]" && uploadedName(Json{{"name", "b.png"}, {"subfolder", "x"}, {"type", "input"}}) == "x/b.png" &&
                             uploadedName(Json::object()).empty());
  const Json running = Json::parse(R"({"abc":{"status":{"status_str":"success","completed":false}}})");
  const Json finished = Json::parse(R"({"abc":{"status":{"status_str":"success","completed":true},"outputs":{"30":{"images":[{"filename":"ComfyUI_temp_x_00001_.png","subfolder":"","type":"temp"},{"filename":"ComfyUI_temp_x_00002_.png","subfolder":"","type":"temp"}]}}}})");
  const Json failed = Json::parse(R"({"abc":{"status":{"status_str":"error","completed":false,"messages":[["execution_start",{}],["execution_error",{"exception_message":"Error while deserializing header: MetadataIncompleteBuffer"}]]}}})");
  bool done = true;
  std::string error;
  const bool notYet = historyImages(Json::object(), "abc", &done, &error).empty() && !done && historyImages(running, "abc", &done, &error).empty() && !done;
  const auto imgs = historyImages(finished, "abc", &done, &error);
  report("historyImages: absent / running give nothing, finished gives its images", notYet && done && imgs.size() == 2 && imgs[1].filename == "ComfyUI_temp_x_00002_.png" && imgs[0].type == "temp");
  error.clear();
  report("historyImages: an execution error is reported with ComfyUI's message", historyImages(failed, "abc", &done, &error).empty() && done && error.find("MetadataIncompleteBuffer") != std::string::npos, error);
  report("viewQuery encodes", viewQuery(Output{"a b.png", "sub/dir", "temp"}) == "filename=a%20b.png&subfolder=sub%2Fdir&type=temp");
  const Json info = Json::parse(R"({"CheckpointLoaderSimple":{"input":{"required":{"ckpt_name":[["flux1-schnell-fp8.safetensors","sd_xl_base_1.0.safetensors"],{}]}}}})");
  report("checkpoints from object_info", checkpoints(info).size() == 2 && checkpoints(info)[1] == "sd_xl_base_1.0.safetensors" && checkpoints(Json::object()).empty());
  j.mode = Mode::Inpaint;
  j.preset = sdxl;
  j.checkpoint = "sd_xl_base_1.0.safetensors";
  Job u;
  u.mode = Mode::Upscale;
  u.scale = 4;
  report("summary", summary(j) == "inpaint - sd_xl_base_1.0 - 25 steps - seed 7" && summary(u) == "upscale 4x", summary(j));

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
