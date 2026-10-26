// `Open WebUI --sketchtest`: offline checks of sketch input.
#include <cstdio>
#include <string>

#include "core/sketch.h"
#include "stb_image.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
sketch::Stroke line(float x0, float y0, float x1, float y1, sketch::Rgb c, float w, bool eraser = false) {
  sketch::Stroke s;
  s.points = {{x0, y0}, {x1, y1}};
  s.color = c;
  s.width = w;
  s.eraser = eraser;
  return s;
}
}  // namespace

int runSketchSelfTest() {
  using namespace sketch;
  printf("Open WebUI desktop sketch-input self-test\n\n");

  Doc d;
  d.width = 40;
  d.height = 20;
  report("an empty doc rasterises to the background", rasterize(d).at(5, 5) == (Rgb{255, 255, 255}) && empty(d) && rasterize(d).width == 40);
  add(d, line(5, 10, 35, 10, Rgb{0, 0, 0}, 3));
  Image img = rasterize(d);
  report("a stroke paints its core, leaves the rest, anti-aliases its edge", img.at(20, 10) == (Rgb{0, 0, 0}) && img.at(20, 2) == (Rgb{255, 255, 255}) && img.at(2, 10) == (Rgb{255, 255, 255}) && img.at(20, 11).r > 0 && img.at(20, 11).r < 255,
         "edge " + std::to_string(img.at(20, 11).r));
  add(d, line(18, 5, 18, 15, Rgb{}, 6, true));
  img = rasterize(d);
  report("the eraser paints the background over earlier strokes; empty() ignores erasers", img.at(18, 10) == (Rgb{255, 255, 255}) && img.at(30, 10) == (Rgb{0, 0, 0}) && !empty(d));
  report("undo / redo move whole strokes; a new stroke clears redo", undo(d) && rasterize(d).at(18, 10) == (Rgb{0, 0, 0}) && redo(d) && rasterize(d).at(18, 10) == (Rgb{255, 255, 255}) && undo(d) &&
                                                                         [&] { add(d, line(1, 1, 2, 2, Rgb{255, 0, 0}, 2)); return !redo(d); }() && d.strokes.size() == 2);
  Doc dot;
  dot.width = dot.height = 10;
  Stroke tap;
  tap.points = {{5, 5}};
  tap.color = Rgb{0, 0, 255};
  tap.width = 4;
  add(dot, tap);
  report("a single point paints a dot; rasterize scales", rasterize(dot).at(5, 5) == (Rgb{0, 0, 255}) && rasterize(dot, 2.0f).width == 20 && rasterize(dot, 2.0f).at(10, 10) == (Rgb{0, 0, 255}));

  Doc joints;
  joints.width = 60;
  joints.height = 20;
  Stroke many;
  many.width = 3;
  for (int x = 5; x <= 55; x += 2) many.points.push_back(Point{static_cast<float>(x), 10.3f});
  add(joints, many);
  const Image ji = rasterize(joints);
  report("a stroke of many short segments has the same edge at a joint as mid-segment (no beading)", ji.at(29, 11) == ji.at(30, 11) && ji.at(30, 11).r > 0 && ji.at(30, 11).r < 255,
         std::to_string(ji.at(29, 11).r) + " vs " + std::to_string(ji.at(30, 11).r));

  const Image trimmed = trim(rasterize(dot, 4.0f), Rgb{255, 255, 255}, 2);
  report("trim crops to the drawing plus the margin; a blank image is left whole", trimmed.width < 40 && trimmed.width >= 16 && trimmed.height == trimmed.width && trim(rasterize(Doc{}), Rgb{255, 255, 255}, 4).width == 800);

  const std::string png = encodePng(img);
  int w = 0, h = 0, n = 0;
  unsigned char* decoded = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(png.data()), static_cast<int>(png.size()), &w, &h, &n, 4);
  bool same = decoded && w == img.width && h == img.height;
  for (size_t i = 0; same && i < img.rgba.size(); ++i) same = decoded[i] == img.rgba[i];
  if (decoded) stbi_image_free(decoded);
  report("encodePng: stb_image decodes it to the same pixels", png.compare(0, 8, "\x89PNG\r\n\x1a\n") == 0 && same && encodePng(Image{}).empty(), std::to_string(png.size()) + " bytes");

  const Doc back = fromJson(toJson(d));
  report("JSON round-trip keeps strokes, colours, widths and erasers", back.strokes.size() == d.strokes.size() && back.width == 40 && back.strokes[0].color == (Rgb{0, 0, 0}) && back.strokes[1].color == (Rgb{255, 0, 0}) &&
                                                                       back.strokes[0].points.size() == 2 && fromJson(Json("x")).strokes.empty());
  report("summary / clear", summary(d) == "2 strokes, 4 points" && [&] { clear(d); return d.strokes.empty() && !undo(d) && summary(d) == "0 strokes, 0 points"; }());

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
