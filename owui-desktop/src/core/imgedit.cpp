#include "imgedit.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace imgedit {
namespace {

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string urlEncode(const std::string& s) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : s) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
    else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

Json link(const char* node, int output) { return Json::array({node, output}); }

}  // namespace

const char* modeName(Mode m) {
  switch (m) {
    case Mode::Inpaint: return "inpaint";
    case Mode::Img2Img: return "img2img";
    case Mode::Variations: return "variations";
    case Mode::Upscale: return "upscale";
  }
  return "?";
}

Preset presetFor(const std::string& checkpoint) {
  Preset p;
  if (lower(checkpoint).find("flux") != std::string::npos) {
    p.steps = lower(checkpoint).find("schnell") != std::string::npos ? 4 : 20;
    p.cfg = 1.0;
    p.sampler = "euler";
    p.scheduler = "simple";
    p.usesNegative = false;
  }
  return p;
}

Json workflow(const Job& j) {
  Json w = Json::object();
  w["2"] = {{"class_type", "LoadImage"}, {"inputs", {{"image", j.image}}}};
  if (j.mode == Mode::Upscale) {
    w["20"] = {{"class_type", "ImageScaleBy"}, {"inputs", {{"image", link("2", 0)}, {"upscale_method", "lanczos"}, {"scale_by", j.scale}}}};
    if (!j.detail) {
      w["30"] = {{"class_type", "PreviewImage"}, {"inputs", {{"images", link("20", 0)}}}};
      return w;
    }
  }
  w["1"] = {{"class_type", "CheckpointLoaderSimple"}, {"inputs", {{"ckpt_name", j.checkpoint}}}};
  w["3"] = {{"class_type", "CLIPTextEncode"}, {"inputs", {{"text", j.prompt}, {"clip", link("1", 1)}}}};
  w["4"] = {{"class_type", "CLIPTextEncode"}, {"inputs", {{"text", j.preset.usesNegative ? j.negative : std::string()}, {"clip", link("1", 1)}}}};
  const Json pixels = j.mode == Mode::Upscale ? link("20", 0) : link("2", 0);
  w["5"] = {{"class_type", "VAEEncode"}, {"inputs", {{"pixels", pixels}, {"vae", link("1", 2)}}}};
  Json latent = link("5", 0);
  double denoise = j.denoise;
  if (j.mode == Mode::Inpaint) {
    w["10"] = {{"class_type", "LoadImageMask"}, {"inputs", {{"image", j.mask}, {"channel", "red"}}}};
    w["11"] = {{"class_type", "GrowMask"}, {"inputs", {{"mask", link("10", 0)}, {"expand", j.grow}, {"tapered_corners", true}}}};
    w["6"] = {{"class_type", "SetLatentNoiseMask"}, {"inputs", {{"samples", latent}, {"mask", link("11", 0)}}}};
    latent = link("6", 0);
  } else if (j.mode == Mode::Variations) {
    w["7"] = {{"class_type", "RepeatLatentBatch"}, {"inputs", {{"samples", latent}, {"amount", std::clamp(j.count, 1, 8)}}}};
    latent = link("7", 0);
  } else if (j.mode == Mode::Upscale) {
    denoise = 0.3;   // the detail pass sharpens; it doesn't repaint
  }
  w["8"] = {{"class_type", "KSampler"},
            {"inputs",
             {{"model", link("1", 0)}, {"seed", j.seed}, {"steps", j.preset.steps}, {"cfg", j.preset.cfg}, {"sampler_name", j.preset.sampler}, {"scheduler", j.preset.scheduler},
              {"positive", link("3", 0)}, {"negative", link("4", 0)}, {"latent_image", latent}, {"denoise", std::clamp(denoise, 0.05, 1.0)}}}};
  w["9"] = {{"class_type", "VAEDecode"}, {"inputs", {{"samples", link("8", 0)}, {"vae", link("1", 2)}}}};
  Json result = link("9", 0);
  if (j.mode == Mode::Inpaint) {   // paste only the masked area over the original: the rest stays pixel-exact
    w["12"] = {{"class_type", "ImageCompositeMasked"},
               {"inputs", {{"destination", link("2", 0)}, {"source", link("9", 0)}, {"x", 0}, {"y", 0}, {"resize_source", false}, {"mask", link("11", 0)}}}};
    result = link("12", 0);
  }
  w["30"] = {{"class_type", "PreviewImage"}, {"inputs", {{"images", result}}}};
  return w;
}

bool linksValid(const Json& w, std::string* bad) {
  for (auto it = w.begin(); it != w.end(); ++it) {
    const Json inputs = it->value("inputs", Json::object());
    for (auto in = inputs.begin(); in != inputs.end(); ++in) {
      const Json& v = in.value();
      if (v.is_array() && v.size() == 2 && v[0].is_string() && v[1].is_number_integer() && !w.contains(v[0].get<std::string>())) {
        if (bad) *bad = it.key() + "." + in.key() + " -> " + v[0].get<std::string>();
        return false;
      }
    }
  }
  return true;
}

bool maskEmpty(const sketch::Doc& strokes) { return sketch::empty(strokes); }

std::string maskPng(const sketch::Doc& strokes, int width, int height) {
  sketch::Doc d = strokes;
  const float sx = static_cast<float>(width) / std::max(1, strokes.width), sy = static_cast<float>(height) / std::max(1, strokes.height);
  d.width = width;
  d.height = height;
  d.background = sketch::Rgb{0, 0, 0};
  d.redo.clear();
  for (auto& s : d.strokes) {
    if (!s.eraser) s.color = sketch::Rgb{255, 255, 255};
    for (auto& p : s.points) p = sketch::Point{p.x * sx, p.y * sy};
    s.width *= (sx + sy) / 2.0f;
  }
  return sketch::encodePng(sketch::rasterize(d, 1.0f));
}

void fit(int width, int height, int maxSide, int* outW, int* outH) {
  double w = width, h = height;
  const double longest = std::max(w, h);
  if (longest > maxSide) {
    w *= maxSide / longest;
    h *= maxSide / longest;
  }
  *outW = std::max(8, static_cast<int>(w) / 8 * 8);
  *outH = std::max(8, static_cast<int>(h) / 8 * 8);
}

void workingSize(int width, int height, const std::string& checkpoint, int* outW, int* outH) {
  const std::string c = lower(checkpoint);
  double area = 0;
  if (c.find("xl") != std::string::npos || c.find("flux") != std::string::npos || c.find("sd3") != std::string::npos) area = 1024.0 * 1024.0;
  else if (c.find("v1-5") != std::string::npos || c.find("sd15") != std::string::npos || c.find("1.5") != std::string::npos) area = 640.0 * 640.0;
  if (area <= 0 || width <= 0 || height <= 0) {
    fit(width, height, 1536, outW, outH);
    return;
  }
  double s = std::sqrt(area / (static_cast<double>(width) * height));
  s = std::min(s, 2.0);
  fit(static_cast<int>(std::lround(width * s)), static_cast<int>(std::lround(height * s)), 1536, outW, outH);
}

std::string uploadedName(const Json& r) {
  if (!r.is_object() || !r.contains("name")) return "";
  std::string name = r.value("name", "");
  const std::string sub = r.value("subfolder", "");
  if (!sub.empty()) name = sub + "/" + name;
  const std::string type = r.value("type", "input");
  return type == "input" ? name : name + " [" + type + "]";
}

std::vector<Output> historyImages(const Json& history, const std::string& promptId, bool* done, std::string* error) {
  std::vector<Output> out;
  if (done) *done = false;
  if (!history.is_object() || !history.contains(promptId)) return out;
  const Json& entry = history[promptId];
  const Json status = entry.value("status", Json::object());
  if (status.value("status_str", "") == "error") {
    if (error) {
      *error = "ComfyUI reported an error";
      for (const auto& m : status.value("messages", Json::array())) {
        if (m.is_array() && m.size() == 2 && m[0] == "execution_error" && m[1].is_object()) *error = m[1].value("exception_message", *error);
      }
    }
    if (done) *done = true;
    return out;
  }
  if (!status.value("completed", entry.contains("outputs"))) return out;
  if (done) *done = true;
  const Json outputs = entry.value("outputs", Json::object());   // a copy: items() must not outlive it
  for (auto it = outputs.begin(); it != outputs.end(); ++it) {
    for (const auto& img : it->value("images", Json::array())) out.push_back(Output{img.value("filename", ""), img.value("subfolder", ""), img.value("type", "temp")});
  }
  return out;
}

std::string viewQuery(const Output& o) { return "filename=" + urlEncode(o.filename) + "&subfolder=" + urlEncode(o.subfolder) + "&type=" + urlEncode(o.type); }

std::vector<std::string> checkpoints(const Json& info) {
  std::vector<std::string> out;
  if (!info.is_object() || !info.contains("CheckpointLoaderSimple")) return out;
  const Json choice = info["CheckpointLoaderSimple"].value("input", Json::object()).value("required", Json::object()).value("ckpt_name", Json::array());
  if (choice.is_array() && !choice.empty() && choice[0].is_array()) {
    for (const auto& c : choice[0]) {
      if (c.is_string()) out.push_back(c);
    }
  }
  return out;
}

std::string summary(const Job& j) {
  std::string ck = j.checkpoint;
  const size_t dot = ck.rfind('.');
  if (dot != std::string::npos) ck = ck.substr(0, dot);
  std::string s = std::string(modeName(j.mode));
  if (j.mode == Mode::Upscale) {
    s += " " + std::to_string(static_cast<int>(j.scale)) + "x" + (j.detail ? " + detail" : "");
    if (!j.detail) return s;
  }
  return s + " - " + ck + " - " + std::to_string(j.preset.steps) + " steps - seed " + std::to_string(j.seed);
}

}  // namespace imgedit
