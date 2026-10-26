// `Open WebUI --imagemetatest`: offline checks of the PNG metadata and the gallery index.
#include <cstdio>
#include <string>

#include <zlib.h>

#include "core/imagemeta.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
void be32(std::string& s, uint32_t v) {
  for (int i = 3; i >= 0; --i) s.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
}
std::string chunk(const std::string& type, const std::string& data) {
  std::string out;
  be32(out, static_cast<uint32_t>(data.size()));
  out += type + data;
  const std::string in = type + data;
  be32(out, static_cast<uint32_t>(crc32(0L, reinterpret_cast<const Bytef*>(in.data()), static_cast<uInt>(in.size()))));
  return out;
}
// A 2x1 RGB PNG with one tEXt chunk already in it.
std::string tinyPng(const std::string& textKey, const std::string& textValue) {
  std::string ihdr;
  be32(ihdr, 2);
  be32(ihdr, 1);
  ihdr += std::string("\x08\x02\x00\x00\x00", 5);
  const std::string raw("\x00\xff\x00\x00\x00\x00\xff", 7);   // filter 0, red, blue
  uLongf len = compressBound(raw.size());
  std::string z(len, '\0');
  compress(reinterpret_cast<Bytef*>(&z[0]), &len, reinterpret_cast<const Bytef*>(raw.data()), raw.size());
  z.resize(len);
  std::string png("\x89PNG\r\n\x1a\n", 8);
  png += chunk("IHDR", ihdr);
  if (!textKey.empty()) png += chunk("tEXt", textKey + std::string(1, '\0') + textValue);
  png += chunk("IDAT", z);
  png += chunk("IEND", "");
  return png;
}
bool crcsValid(const std::string& png) {
  size_t pos = 8;
  while (pos + 12 <= png.size()) {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(png.data() + pos);
    const uint32_t len = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
    const std::string in = png.substr(pos + 4, 4 + len);
    const uint32_t want = static_cast<uint32_t>(crc32(0L, reinterpret_cast<const Bytef*>(in.data()), static_cast<uInt>(in.size())));
    const unsigned char* c = p + 8 + len;
    const uint32_t have = (uint32_t(c[0]) << 24) | (uint32_t(c[1]) << 16) | (uint32_t(c[2]) << 8) | c[3];
    if (want != have) return false;
    pos += 12 + len;
  }
  return pos == png.size();
}
}  // namespace

int runImageMetaSelfTest() {
  using namespace imagemeta;
  printf("Open WebUI desktop image-metadata self-test\n\n");

  const std::string png = tinyPng("Software", "test");
  int w = 0, h = 0;
  report("isPng / pngSize", isPng(png) && !isPng("nope") && pngSize(png, &w, &h) && w == 2 && h == 1);
  report("readText reads tEXt", readText(png).at("Software") == "test");

  Meta m;
  m.prompt = "a lighthouse at dusk, oil painting";
  m.negative = "blurry";
  m.model = "sdxl";
  m.size = "1024x1024";
  m.seed = "4242";
  m.engine = "comfyui";
  m.created = "2026-10-25 15:40";
  const std::string written = writeText(png, toText(m));
  report("writeText: still a PNG, CRCs valid, size kept, IHDR first", isPng(written) && crcsValid(written) && pngSize(written, &w, &h) && w == 2 && written.compare(12, 4, "IHDR") == 0 && !writeText("junk", {}).empty() == false);
  const Meta back = read(written);
  report("round-trip through iTXt", back.prompt == m.prompt && back.negative == "blurry" && back.model == "sdxl" && back.size == "1024x1024" && back.seed == "4242" && back.engine == "comfyui" && back.created == m.created &&
                                        readText(written).at("Software") == "test");
  const std::string rewritten = writeText(written, {{"prompt", "a different prompt"}, {"Software", "owui"}});
  report("writeText replaces existing keywords once", read(rewritten).prompt == "a different prompt" && readText(rewritten).at("Software") == "owui" && readText(rewritten).size() == readText(written).size() && crcsValid(rewritten));

  const std::string a1111 = "masterpiece, a red boat\nNegative prompt: lowres, text\nSteps: 20, Sampler: Euler a, CFG scale: 7, Seed: 123456, Size: 512x768, Model hash: abc, Model: dreamshaper_8, Version: v1.6";
  const Meta a = read(writeText(png, {{"parameters", a1111}}));
  report("A1111 parameters chunk: prompt, negative, seed, size, model, engine", a.prompt == "masterpiece, a red boat" && a.negative == "lowres, text" && a.seed == "123456" && a.size == "512x768" && a.model == "dreamshaper_8" &&
                                                                                    a.engine == "automatic1111" && a.parameters == a1111);
  const std::string comfy = R"({"3":{"class_type":"KSampler","inputs":{"seed":987654321,"steps":20}},"4":{"class_type":"CheckpointLoaderSimple","inputs":{"ckpt_name":"sd_xl_base.safetensors"}},"5":{"class_type":"EmptyLatentImage","inputs":{"width":1024,"height":768}},"6":{"class_type":"CLIPTextEncode","inputs":{"text":"a fox in snow"}},"7":{"class_type":"CLIPTextEncode","inputs":{"text":"ugly"}}})";
  const Meta c = read(writeText(png, {{"prompt", comfy}}));
  report("ComfyUI prompt graph: seed, checkpoint, size, positive / negative", c.seed == "987654321" && c.model == "sd_xl_base.safetensors" && c.size == "1024x768" && c.prompt == "a fox in snow" && c.negative == "ugly" && c.engine == "comfyui");
  report("parametersLine / empty", parametersLine(m) == "a lighthouse at dusk, oil painting\nNegative prompt: blurry\nSeed: 4242, Size: 1024x1024, Model: sdxl, Engine: comfyui" && empty(Meta{}) && !empty(m));

  Item i1;
  i1.file = "1-1.png";
  i1.meta = m;
  i1.created = 100;
  Item i2;
  i2.file = "2-1.png";
  i2.meta.prompt = "A FOX";
  i2.favourite = true;
  i2.created = 200;
  const auto items = itemsFromJson(itemsToJson({i1, i2}));
  report("index round-trip", items.size() == 2 && items[0].meta.seed == "4242" && items[1].favourite && items[1].created == 200 && itemsFromJson(Json("x")).empty());
  report("filter: newest first, favourites only, caseless query over prompt / seed", filter(items, "", false)[0].file == "2-1.png" && filter(items, "", true).size() == 1 && filter(items, "fox", false).size() == 1 &&
                                                                                       filter(items, "4242", false)[0].file == "1-1.png" && filter(items, "zzz", false).empty());
  report("fileNameFor", fileNameFor(1700000000, 2) == "1700000000-2.png");

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
