// Sketch input (#120): a drawing made of strokes, its undo / redo, an
// anti-aliased rasteriser, trimming to the drawn area and a PNG encoder
// (zlib), so a sketch can go to a vision model as an image attachment.
// App-free; --sketchtest covers it.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace sketch {

using Json = nlohmann::json;

struct Rgb {
  uint8_t r = 0, g = 0, b = 0;
  bool operator==(const Rgb& o) const { return r == o.r && g == o.g && b == o.b; }
};

struct Point {
  float x = 0.0f, y = 0.0f;
};

struct Stroke {
  std::vector<Point> points;
  Rgb color;
  float width = 4.0f;    // diameter in canvas units
  bool eraser = false;   // paints the background colour
};

struct Doc {
  int width = 800, height = 500;
  Rgb background{255, 255, 255};
  std::vector<Stroke> strokes;
  std::vector<Stroke> redo;   // undone strokes, newest last
};

void add(Doc& doc, Stroke stroke);   // a finished stroke; clears the redo stack
bool undo(Doc& doc);
bool redo(Doc& doc);
void clear(Doc& doc);                // undoable as one step per stroke is not kept: clears both stacks
bool empty(const Doc& doc);

struct Image {
  int width = 0, height = 0;
  std::vector<uint8_t> rgba;   // width * height * 4
  Rgb at(int x, int y) const;
};
Image rasterize(const Doc& doc, float scale = 1.0f);
// The smallest box holding everything that differs from the background,
// grown by `margin` and clamped; the whole image when nothing is drawn.
Image trim(const Image& image, Rgb background, int margin);
std::string encodePng(const Image& image);
std::string summary(const Doc& doc);   // "3 strokes, 412 points"

Json toJson(const Doc& doc);
Doc fromJson(const Json& j);

}  // namespace sketch
