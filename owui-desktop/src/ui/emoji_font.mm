#include "ui/emoji_font.h"

#import <CoreGraphics/CoreGraphics.h>
#import <CoreText/CoreText.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "imgui_internal.h"

namespace emoji {
namespace {

constexpr int kZeroWidthGlyph = 0x7fffffff;   // our marker: a codepoint drawn as nothing
constexpr int kLetterGlyph = 0x7ff00000;      // + 0..25: a regional indicator drawn as its letter
constexpr unsigned kEarly = 1;                // ImFontConfig::FontLoaderFlags bit: the early, presentation-only source
constexpr float kScale = 0.80f;               // Apple Color Emoji glyphs are ~1.3x their point size: this keeps them inside the line

CTFontRef baseFont() {
  static CTFontRef font = CTFontCreateWithName(CFSTR("AppleColorEmoji"), 64.0, nullptr);
  return font;
}

int glyphFor(CTFontRef font, uint32_t codepoint) {
  UniChar chars[2];
  CFIndex count = 1;
  if (codepoint > 0xFFFF) {
    const uint32_t v = codepoint - 0x10000;
    chars[0] = static_cast<UniChar>(0xD800 + (v >> 10));
    chars[1] = static_cast<UniChar>(0xDC00 + (v & 0x3FF));
    count = 2;
  } else {
    chars[0] = static_cast<UniChar>(codepoint);
  }
  CGGlyph glyphs[2] = {0, 0};
  if (!CTFontGetGlyphsForCharacters(font, chars, glyphs, count)) return 0;
  return glyphs[0];
}

// ---- loader callbacks ---------------------------------------------------------

bool srcInit(ImFontAtlas*, ImFontConfig* src) {
  CTFontRef font = baseFont();
  if (!font) return false;
  src->FontLoaderData = (void*)CFRetain(font);
  return true;
}

void srcDestroy(ImFontAtlas*, ImFontConfig* src) {
  if (src->FontLoaderData) CFRelease((CTFontRef)src->FontLoaderData);
  src->FontLoaderData = nullptr;
}

int glyphIndex(ImFontAtlas*, ImFontConfig* src, ImWchar codepoint) {
  const uint32_t cp = static_cast<uint32_t>(codepoint);
  const bool early = (src->FontLoaderFlags & kEarly) != 0;
  if (isZeroWidth(cp)) return kZeroWidthGlyph;
  if (isRegionalIndicator(cp)) return kLetterGlyph + static_cast<int>(cp - 0x1F1E6);
  if (early ? !hasEmojiPresentation(cp) : !isEmojiCandidate(cp)) return 0;
  if (!src->FontLoaderData) return 0;
  return glyphFor((CTFontRef)src->FontLoaderData, cp);
}

CTFontRef letterFont(float size) {
  return CTFontCreateWithName(CFSTR("Helvetica-Bold"), size, nullptr);
}

bool containsGlyph(ImFontAtlas*, ImFontConfig*, int glyph) { return glyph != 0; }

bool bakedInit(ImFontAtlas*, ImFontConfig* src, ImFontBaked* baked, void* data) {
  const float density = src->RasterizerDensity * baked->RasterizerDensity;
  CTFontRef sized = CTFontCreateCopyWithAttributes((CTFontRef)src->FontLoaderData, baked->Size * kScale * density, nullptr, nullptr);
  *reinterpret_cast<CTFontRef*>(data) = sized;
  return sized != nullptr;
}

void bakedDestroy(ImFontAtlas*, ImFontConfig*, ImFontBaked*, void* data) {
  CTFontRef* slot = reinterpret_cast<CTFontRef*>(data);
  if (*slot) CFRelease(*slot);
  *slot = nullptr;
}

bool loadGlyph(ImFontAtlas* atlas, ImFontConfig* src, ImFontBaked* baked, void* data, int glyphIndexIn, ImFontGlyph* out, float* outAdvance) {
  if (glyphIndexIn == kZeroWidthGlyph) {
    if (outAdvance) *outAdvance = 0.0f;
    else out->AdvanceX = 0.0f;
    return true;
  }
  CTFontRef font = *reinterpret_cast<CTFontRef*>(data);
  if (!font) return false;
  const float density = src->RasterizerDensity * baked->RasterizerDensity;
  const bool letter = glyphIndexIn >= kLetterGlyph && glyphIndexIn < kLetterGlyph + 26;
  CTFontRef owned = nullptr;
  if (letter) {   // a regional indicator: its capital letter in bold, tinted like text
    owned = letterFont(baked->Size * 0.86f * density);
    font = owned;
  }
  CGGlyph glyph = static_cast<CGGlyph>(glyphIndexIn);
  if (letter) {
    UniChar ch = static_cast<UniChar>('A' + (glyphIndexIn - kLetterGlyph));
    CTFontGetGlyphsForCharacters(font, &ch, &glyph, 1);
  }
  struct Release {
    CTFontRef f;
    ~Release() {
      if (f) CFRelease(f);
    }
  } release{owned};
  CGSize advance;
  CTFontGetAdvancesForGlyphs(font, kCTFontOrientationHorizontal, &glyph, &advance, 1);
  const float advanceX = static_cast<float>(advance.width) / density;
  if (outAdvance) {
    *outAdvance = advanceX;
    return true;
  }
  out->AdvanceX = advanceX;
  const CGRect box = CTFontGetBoundingRectsForGlyphs(font, kCTFontOrientationHorizontal, &glyph, nullptr, 1);
  if (box.size.width <= 0 || box.size.height <= 0) return true;   // nothing to draw
  const int pad = 1;
  const int w = static_cast<int>(std::ceil(box.size.width)) + pad * 2;
  const int h = static_cast<int>(std::ceil(box.size.height)) + pad * 2;
  ImFontAtlasRectId pack = ImFontAtlasPackAddRect(atlas, w, h);
  if (pack == ImFontAtlasRectId_Invalid) return false;
  ImTextureRect* r = ImFontAtlasPackGetRect(atlas, pack);

  std::vector<unsigned char> pixels(static_cast<size_t>(w) * h * 4, 0);
  CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
  CGContextRef ctx = CGBitmapContextCreate(pixels.data(), w, h, 8, w * 4, space, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
  CGColorSpaceRelease(space);
  if (!ctx) return false;
  // The glyph's origin sits so its bounding box lands inside the padded bitmap.
  const CGPoint origin = CGPointMake(pad - box.origin.x, pad - box.origin.y);
  if (letter) {
    CGContextSetRGBFillColor(ctx, 1, 1, 1, 1);   // white: the atlas tints it with the text colour
  }
  CTFontDrawGlyphs(font, &glyph, &origin, 1, ctx);
  CGContextRelease(ctx);
  // Core Graphics writes premultiplied alpha; the atlas wants straight alpha.
  for (size_t i = 0; i < pixels.size(); i += 4) {
    const unsigned a = pixels[i + 3];
    if (a == 0 || a == 255) continue;
    for (int c = 0; c < 3; ++c) pixels[i + c] = static_cast<unsigned char>(std::min(255u, (pixels[i + c] * 255u + a / 2) / a));
  }
  // Rows are top-down in memory, y grows down in ImGui. Emoji are centred in
  // the line box rather than put on the baseline, as Apple's own text layout does.
  const float lineH = baked->Ascent - baked->Descent;
  out->X0 = (static_cast<float>(box.origin.x) - pad) / density;
  out->X1 = out->X0 + static_cast<float>(w) / density;
  out->Y0 = IM_ROUND((lineH - static_cast<float>(h) / density) * 0.5f);
  out->Y1 = out->Y0 + static_cast<float>(h) / density;
  out->Visible = true;
  out->Colored = !letter;
  out->PackId = pack;
  ImFontAtlasBakedSetFontGlyphBitmap(atlas, baked, src, out, r, pixels.data(), ImTextureFormat_RGBA32, w * 4);
  return true;
}

}  // namespace

bool isZeroWidth(uint32_t cp) {
  // Joiner, variation selectors, tags, the keycap mark - and skin-tone
  // modifiers, which can't combine without shaping (the base emoji shows).
  return cp == 0x200D || (cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0xE0020 && cp <= 0xE007F) || cp == 0x20E3 || (cp >= 0xE0100 && cp <= 0xE01EF) || (cp >= 0x1F3FB && cp <= 0x1F3FF);
}

bool isRegionalIndicator(uint32_t cp) { return cp >= 0x1F1E6 && cp <= 0x1F1FF; }

bool hasEmojiPresentation(uint32_t cp) {
  if (cp >= 0x1F000) {
    if (cp >= 0x1F000 && cp <= 0x1F0FF) return cp == 0x1F004 || cp == 0x1F0CF;   // mahjong / cards: two are emoji
    if (cp >= 0x1F321 && cp <= 0x1F32C) return false;                             // weather symbols are text by default
    return cp <= 0x1FAFF;
  }
  static const uint32_t ranges[][2] = {{0x231A, 0x231B}, {0x23E9, 0x23EC}, {0x23F0, 0x23F0}, {0x23F3, 0x23F3}, {0x25FD, 0x25FE}, {0x2614, 0x2615}, {0x2648, 0x2653},
                                       {0x267F, 0x267F}, {0x2693, 0x2693}, {0x26A1, 0x26A1}, {0x26AA, 0x26AB}, {0x26BD, 0x26BE}, {0x26C4, 0x26C5}, {0x26CE, 0x26CE},
                                       {0x26D4, 0x26D4}, {0x26EA, 0x26EA}, {0x26F2, 0x26F3}, {0x26F5, 0x26F5}, {0x26FA, 0x26FA}, {0x26FD, 0x26FD}, {0x2705, 0x2705},
                                       {0x270A, 0x270B}, {0x2728, 0x2728}, {0x274C, 0x274C}, {0x274E, 0x274E}, {0x2753, 0x2755}, {0x2757, 0x2757}, {0x2795, 0x2797},
                                       {0x27B0, 0x27B0}, {0x27BF, 0x27BF}, {0x2B1B, 0x2B1C}, {0x2B50, 0x2B50}, {0x2B55, 0x2B55},
                                       // Not Emoji_Presentation in Unicode, but in chat these are always written with
                                       // U+FE0F to mean the red emoji, and one-codepoint layout can't see the selector.
                                       {0x2763, 0x2764}};
  for (const auto& r : ranges) {
    if (cp >= r[0] && cp <= r[1]) return true;
  }
  return false;
}

bool isEmojiCandidate(uint32_t cp) {
  return (cp >= 0x1F000 && cp <= 0x1FAFF) ||   // emoticons, symbols, transport, flags' regional letters, supplemental
         (cp >= 0x2600 && cp <= 0x27BF) ||     // miscellaneous symbols and dingbats (fallback when no text font has them)
         (cp >= 0x2300 && cp <= 0x23FF) ||     // watch, hourglass, play buttons
         (cp >= 0x2B00 && cp <= 0x2BFF) ||     // stars, arrows
         (cp >= 0x2190 && cp <= 0x21FF) || cp == 0x00A9 || cp == 0x00AE || cp == 0x203C || cp == 0x2049 || cp == 0x2122 || cp == 0x2139 || cp == 0x3030 || cp == 0x303D ||
         cp == 0x3297 || cp == 0x3299;
}

int astralCount(const std::string& s) {
  int n = 0;
  for (unsigned char c : s) {
    if ((c & 0xF8) == 0xF0) ++n;   // a 4-byte UTF-8 lead byte
  }
  return n;
}

const ImFontLoader* loader() {
  static ImFontLoader l = [] {
    ImFontLoader x;
    x.Name = "CoreText emoji";
    x.FontSrcInit = srcInit;
    x.FontSrcDestroy = srcDestroy;
    x.FontSrcGetGlyphIndexFromCodepoint = glyphIndex;
    x.FontSrcContainsGlyph = containsGlyph;
    x.FontBakedInit = bakedInit;
    x.FontBakedDestroy = bakedDestroy;
    x.FontBakedLoadGlyph = loadGlyph;
    x.FontBakedSrcLoaderDataSize = sizeof(CTFontRef);
    return x;
  }();
  return &l;
}

bool available() { return baseFont() != nullptr; }

void merge(float sizePixels, bool early) {
  if (!available()) return;
  ImFontConfig config;
  config.MergeMode = true;
  config.FontLoader = loader();
  config.FontLoaderFlags = early ? kEarly : 0;
  config.SizePixels = sizePixels;
  ImFormatString(config.Name, IM_ARRAYSIZE(config.Name), "Apple Color Emoji");
  config.FontData = nullptr;
  config.FontDataOwnedByAtlas = false;
  ImGui::GetIO().Fonts->AddFont(&config);
}

}  // namespace emoji
