#include "sketch.h"

#include <algorithm>
#include <cmath>

#include <zlib.h>

namespace sketch {
namespace {

void be32(std::string& out, uint32_t v) {
  for (int i = 3; i >= 0; --i) out.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
}

void chunk(std::string& out, const char* type, const std::string& data) {
  be32(out, static_cast<uint32_t>(data.size()));
  const std::string body = std::string(type, 4) + data;
  out += body;
  be32(out, static_cast<uint32_t>(::crc32(0L, reinterpret_cast<const Bytef*>(body.data()), static_cast<uInt>(body.size()))));
}

float segmentDistance(float px, float py, const Point& a, const Point& b) {
  const float dx = b.x - a.x, dy = b.y - a.y;
  const float len2 = dx * dx + dy * dy;
  float t = len2 > 0.0f ? ((px - a.x) * dx + (py - a.y) * dy) / len2 : 0.0f;
  t = std::clamp(t, 0.0f, 1.0f);
  const float cx = a.x + t * dx - px, cy = a.y + t * dy - py;
  return std::sqrt(cx * cx + cy * cy);
}

// One stroke is painted through a coverage mask holding the strongest
// coverage any of its segments gives a pixel, then blended once - so the
// anti-aliased edges don't darken where consecutive segments overlap.
void paintStroke(Image& img, const std::vector<Point>& pts, float radius, Rgb color) {
  float minX = pts[0].x, maxX = pts[0].x, minY = pts[0].y, maxY = pts[0].y;
  for (const Point& p : pts) {
    minX = std::min(minX, p.x);
    maxX = std::max(maxX, p.x);
    minY = std::min(minY, p.y);
    maxY = std::max(maxY, p.y);
  }
  const int bx0 = std::max(0, static_cast<int>(std::floor(minX - radius - 1)));
  const int bx1 = std::min(img.width - 1, static_cast<int>(std::ceil(maxX + radius + 1)));
  const int by0 = std::max(0, static_cast<int>(std::floor(minY - radius - 1)));
  const int by1 = std::min(img.height - 1, static_cast<int>(std::ceil(maxY + radius + 1)));
  if (bx1 < bx0 || by1 < by0) return;
  const int mw = bx1 - bx0 + 1, mh = by1 - by0 + 1;
  std::vector<float> mask(static_cast<size_t>(mw) * mh, 0.0f);
  const size_t segments = pts.size() == 1 ? 1 : pts.size() - 1;
  for (size_t i = 0; i < segments; ++i) {
    const Point& a = pts[i];
    const Point& b = pts.size() == 1 ? pts[0] : pts[i + 1];
    const int x0 = std::max(bx0, static_cast<int>(std::floor(std::min(a.x, b.x) - radius - 1)));
    const int x1 = std::min(bx1, static_cast<int>(std::ceil(std::max(a.x, b.x) + radius + 1)));
    const int y0 = std::max(by0, static_cast<int>(std::floor(std::min(a.y, b.y) - radius - 1)));
    const int y1 = std::min(by1, static_cast<int>(std::ceil(std::max(a.y, b.y) + radius + 1)));
    for (int y = y0; y <= y1; ++y) {
      for (int x = x0; x <= x1; ++x) {
        const float d = segmentDistance(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, a, b);
        const float coverage = std::clamp(radius + 0.5f - d, 0.0f, 1.0f);
        float& m = mask[static_cast<size_t>(y - by0) * mw + (x - bx0)];
        m = std::max(m, coverage);
      }
    }
  }
  for (int y = by0; y <= by1; ++y) {
    for (int x = bx0; x <= bx1; ++x) {
      const float coverage = mask[static_cast<size_t>(y - by0) * mw + (x - bx0)];
      if (coverage <= 0.0f) continue;
      uint8_t* p = &img.rgba[(static_cast<size_t>(y) * img.width + x) * 4];
      p[0] = static_cast<uint8_t>(std::lround(p[0] + (color.r - p[0]) * coverage));
      p[1] = static_cast<uint8_t>(std::lround(p[1] + (color.g - p[1]) * coverage));
      p[2] = static_cast<uint8_t>(std::lround(p[2] + (color.b - p[2]) * coverage));
      p[3] = 255;
    }
  }
}

}  // namespace

void add(Doc& doc, Stroke stroke) {
  if (stroke.points.empty()) return;
  doc.strokes.push_back(std::move(stroke));
  doc.redo.clear();
}

bool undo(Doc& doc) {
  if (doc.strokes.empty()) return false;
  doc.redo.push_back(std::move(doc.strokes.back()));
  doc.strokes.pop_back();
  return true;
}

bool redo(Doc& doc) {
  if (doc.redo.empty()) return false;
  doc.strokes.push_back(std::move(doc.redo.back()));
  doc.redo.pop_back();
  return true;
}

void clear(Doc& doc) {
  doc.strokes.clear();
  doc.redo.clear();
}

bool empty(const Doc& doc) {
  for (const Stroke& s : doc.strokes) {
    if (!s.eraser) return false;
  }
  return true;
}

Rgb Image::at(int x, int y) const {
  if (x < 0 || y < 0 || x >= width || y >= height) return Rgb{};
  const uint8_t* p = &rgba[(static_cast<size_t>(y) * width + x) * 4];
  return Rgb{p[0], p[1], p[2]};
}

Image rasterize(const Doc& doc, float scale) {
  Image img;
  img.width = std::max(1, static_cast<int>(std::lround(doc.width * scale)));
  img.height = std::max(1, static_cast<int>(std::lround(doc.height * scale)));
  img.rgba.resize(static_cast<size_t>(img.width) * img.height * 4);
  for (size_t i = 0; i < img.rgba.size(); i += 4) {
    img.rgba[i] = doc.background.r;
    img.rgba[i + 1] = doc.background.g;
    img.rgba[i + 2] = doc.background.b;
    img.rgba[i + 3] = 255;
  }
  for (const Stroke& s : doc.strokes) {
    const Rgb color = s.eraser ? doc.background : s.color;
    const float radius = std::max(0.5f, s.width * scale * 0.5f);
    std::vector<Point> pts;
    pts.reserve(s.points.size());
    for (const Point& p : s.points) pts.push_back(Point{p.x * scale, p.y * scale});
    paintStroke(img, pts, radius, color);
  }
  return img;
}

Image trim(const Image& image, Rgb background, int margin) {
  int x0 = image.width, y0 = image.height, x1 = -1, y1 = -1;
  for (int y = 0; y < image.height; ++y) {
    for (int x = 0; x < image.width; ++x) {
      const Rgb c = image.at(x, y);
      if (std::abs(c.r - background.r) + std::abs(c.g - background.g) + std::abs(c.b - background.b) > 24) {
        x0 = std::min(x0, x);
        y0 = std::min(y0, y);
        x1 = std::max(x1, x);
        y1 = std::max(y1, y);
      }
    }
  }
  if (x1 < 0) return image;
  x0 = std::max(0, x0 - margin);
  y0 = std::max(0, y0 - margin);
  x1 = std::min(image.width - 1, x1 + margin);
  y1 = std::min(image.height - 1, y1 + margin);
  Image out;
  out.width = x1 - x0 + 1;
  out.height = y1 - y0 + 1;
  out.rgba.resize(static_cast<size_t>(out.width) * out.height * 4);
  for (int y = 0; y < out.height; ++y) {
    std::copy_n(&image.rgba[(static_cast<size_t>(y + y0) * image.width + x0) * 4], static_cast<size_t>(out.width) * 4, &out.rgba[static_cast<size_t>(y) * out.width * 4]);
  }
  return out;
}

std::string encodePng(const Image& image) {
  if (image.width <= 0 || image.height <= 0 || image.rgba.size() != static_cast<size_t>(image.width) * image.height * 4) return "";
  std::string raw;
  raw.reserve(static_cast<size_t>(image.height) * (1 + image.width * 4));
  for (int y = 0; y < image.height; ++y) {
    raw.push_back('\0');   // filter: none
    raw.append(reinterpret_cast<const char*>(&image.rgba[static_cast<size_t>(y) * image.width * 4]), static_cast<size_t>(image.width) * 4);
  }
  uLongf zlen = compressBound(static_cast<uLong>(raw.size()));
  std::string z(zlen, '\0');
  if (compress2(reinterpret_cast<Bytef*>(&z[0]), &zlen, reinterpret_cast<const Bytef*>(raw.data()), static_cast<uLong>(raw.size()), 6) != Z_OK) return "";
  z.resize(zlen);
  std::string ihdr;
  be32(ihdr, static_cast<uint32_t>(image.width));
  be32(ihdr, static_cast<uint32_t>(image.height));
  ihdr += std::string("\x08\x06\x00\x00\x00", 5);   // 8-bit RGBA, deflate, adaptive filters, no interlace
  std::string out("\x89PNG\r\n\x1a\n", 8);
  chunk(out, "IHDR", ihdr);
  chunk(out, "IDAT", z);
  chunk(out, "IEND", "");
  return out;
}

std::string summary(const Doc& doc) {
  size_t points = 0;
  for (const Stroke& s : doc.strokes) points += s.points.size();
  return std::to_string(doc.strokes.size()) + (doc.strokes.size() == 1 ? " stroke, " : " strokes, ") + std::to_string(points) + (points == 1 ? " point" : " points");
}

Json toJson(const Doc& doc) {
  Json strokes = Json::array();
  for (const Stroke& s : doc.strokes) {
    Json pts = Json::array();
    for (const Point& p : s.points) pts.push_back(Json::array({std::round(p.x * 10) / 10, std::round(p.y * 10) / 10}));
    strokes.push_back(Json{{"points", pts}, {"color", Json::array({s.color.r, s.color.g, s.color.b})}, {"width", s.width}, {"eraser", s.eraser}});
  }
  return Json{{"width", doc.width}, {"height", doc.height}, {"background", Json::array({doc.background.r, doc.background.g, doc.background.b})}, {"strokes", strokes}};
}

Doc fromJson(const Json& j) {
  Doc d;
  if (!j.is_object()) return d;
  d.width = std::clamp(j.value("width", 800), 16, 4096);
  d.height = std::clamp(j.value("height", 500), 16, 4096);
  auto rgb = [](const Json& c, Rgb fallback) {
    if (!c.is_array() || c.size() != 3) return fallback;
    return Rgb{static_cast<uint8_t>(c[0].get<int>()), static_cast<uint8_t>(c[1].get<int>()), static_cast<uint8_t>(c[2].get<int>())};
  };
  d.background = rgb(j.value("background", Json()), d.background);
  for (const auto& s : j.value("strokes", Json::array())) {
    if (!s.is_object()) continue;
    Stroke st;
    st.color = rgb(s.value("color", Json()), Rgb{});
    st.width = std::clamp(s.value("width", 4.0f), 0.5f, 128.0f);
    st.eraser = s.value("eraser", false);
    for (const auto& p : s.value("points", Json::array())) {
      if (p.is_array() && p.size() == 2 && p[0].is_number() && p[1].is_number()) st.points.push_back(Point{p[0].get<float>(), p[1].get<float>()});
    }
    if (!st.points.empty()) d.strokes.push_back(st);
  }
  return d;
}

}  // namespace sketch
