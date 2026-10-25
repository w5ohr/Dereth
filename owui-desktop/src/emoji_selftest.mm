// `Open WebUI --emojitest`: builds a real (headless) font atlas with the
// text face, the Unicode and maths fallbacks and the Core Text emoji source,
// and checks which source each character comes from and what it looks like.
#include <cstdio>
#include <string>

#include "imgui.h"
#include "imgui_internal.h"
#include "ui/emoji_font.h"
#include "ui/theme.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
// The glyph's pixels in the atlas: how many are opaque, and whether any are clearly coloured.
void inspect(const ImFontGlyph* g, int* opaque, bool* colourful) {
  *opaque = 0;
  *colourful = false;
  ImTextureData* tex = ImGui::GetIO().Fonts->TexData;
  if (!g || !g->Visible || !tex || !tex->Pixels) return;
  const int x0 = static_cast<int>(g->U0 * tex->Width), x1 = static_cast<int>(g->U1 * tex->Width);
  const int y0 = static_cast<int>(g->V0 * tex->Height), y1 = static_cast<int>(g->V1 * tex->Height);
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      const unsigned char* p = static_cast<const unsigned char*>(tex->GetPixelsAt(x, y));
      if (p[3] > 200) ++*opaque;
      const int spread = std::max({p[0], p[1], p[2]}) - std::min({p[0], p[1], p[2]});
      if (p[3] > 200 && spread > 60) *colourful = true;
    }
  }
}
}  // namespace

int runEmojiSelfTest() {
  printf("Open WebUI desktop emoji / wide-character self-test\n\n");
  report("ImWchar is 32-bit (emoji above U+FFFF survive decoding)", sizeof(ImWchar) == 4 && IM_UNICODE_CODEPOINT_MAX == 0x10FFFF);
  report("zero-width marks and emoji ranges", emoji::isZeroWidth(0xFE0F) && emoji::isZeroWidth(0x200D) && emoji::isZeroWidth(0xE0067) && !emoji::isZeroWidth('A') && emoji::isEmojiCandidate(0x1F680) &&
                                                  emoji::isEmojiCandidate(0x2705) && !emoji::isEmojiCandidate('A') && !emoji::isEmojiCandidate(0x4E2D) && emoji::hasEmojiPresentation(0x2B50) &&
                                                  emoji::hasEmojiPresentation(0x1F680) && !emoji::hasEmojiPresentation(0x26A0) && !emoji::hasEmojiPresentation(0x1F321) && emoji::isRegionalIndicator(0x1F1EC));
  report("astralCount", emoji::astralCount("ok \xF0\x9F\x9A\x80 and \xE2\x9C\x85 and \xF0\x9D\x91\xA5") == 2 && emoji::astralCount("plain") == 0);
  report("Apple Color Emoji opens through Core Text", emoji::available());

  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(800, 600);
  io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
  theme::loadFonts(16.0f);
  ImGui::NewFrame();
  ImFontBaked* baked = theme::fonts.regular->GetFontBaked(16.0f);
  auto glyph = [&](ImWchar cp) { return baked->FindGlyphNoFallback(cp); };

  const ImFontGlyph* a = glyph('A');
  int opaque = 0;
  bool colourful = false;
  inspect(a, &opaque, &colourful);
  report("'A' comes from the text face: monochrome, not flagged coloured", a && a->Visible && !a->Colored && !colourful && opaque > 0);
  const ImFontGlyph* rocket = glyph(0x1F680);
  inspect(rocket, &opaque, &colourful);
  report("U+1F680 rocket: a visible coloured glyph with real colour pixels", rocket && rocket->Visible && rocket->Colored && colourful && opaque > 20 && rocket->AdvanceX > 8.0f,
         rocket ? "advance " + std::to_string(rocket->AdvanceX) + ", " + std::to_string(opaque) + " opaque px" : "missing");
  const ImFontGlyph* check = glyph(0x2705);
  inspect(check, &opaque, &colourful);
  report("U+2705 check mark (BMP, not in the text fonts) comes from emoji, coloured", check && check->Colored && colourful);
  const ImFontGlyph* star = glyph(0x2B50);
  inspect(star, &opaque, &colourful);
  report("U+2B50 star (emoji by default, also in Arial Unicode) comes from emoji, coloured", star && star->Colored && colourful);
  const ImFontGlyph* heart = glyph(0x2764);
  inspect(heart, &opaque, &colourful);
  report("U+2764 heart is drawn as the red emoji (chat writes it with U+FE0F)", heart && heart->Colored && colourful);
  const ImFontGlyph* warn = glyph(0x26A0);
  report("U+26A0 warning sign (text by default) stays a text glyph", warn && warn->Visible && !warn->Colored);
  const ImFontGlyph* g = glyph(0x1F1EC);
  report("regional indicator G draws as the letter, tinted like text", g && g->Visible && !g->Colored && g->AdvanceX > 4.0f);
  const ImFontGlyph* tone = glyph(0x1F3FD);
  report("skin-tone modifier takes no space (the base emoji shows)", tone && tone->AdvanceX == 0.0f && !tone->Visible);
  const ImFontGlyph* han = glyph(0x4E2D);
  report("U+4E2D (CJK) comes from the Unicode fallback, monochrome", han && han->Visible && !han->Colored);
  const ImFontGlyph* mathX = glyph(0x1D465);
  report("U+1D465 mathematical italic x comes from STIX Two Math", mathX && mathX->Visible && !mathX->Colored);
  const ImFontGlyph* vs16 = glyph(0xFE0F);
  const ImFontGlyph* zwj = glyph(0x200D);
  report("variation selector 16 and the zero-width joiner take no space and draw nothing", vs16 && zwj && vs16->AdvanceX == 0.0f && zwj->AdvanceX == 0.0f && !vs16->Visible);
  rocket = glyph(0x1F680);   // re-fetch: loading later glyphs grows the vector and moves earlier ones
  const float lineH = baked->Ascent - baked->Descent;
  report("emoji sit on the line: inside the line box, not taller than about 1.3x the text size", rocket && rocket->Y0 > -3.0f && rocket->Y1 < lineH + 3.0f && rocket->Y1 - rocket->Y0 < 16.0f * 1.3f && rocket->Y1 > rocket->Y0,
         rocket ? "y " + std::to_string(rocket->Y0) + ".." + std::to_string(rocket->Y1) + " in a " + std::to_string(lineH) + " px line" : "");
  const ImVec2 sz = ImGui::CalcTextSize("go \xF0\x9F\x9A\x80\xEF\xB8\x8F!");
  const ImVec2 plain = ImGui::CalcTextSize("go !");
  report("text measurement includes the emoji's width and not the selector's", sz.x > plain.x + 8.0f && sz.x < plain.x + 30.0f, std::to_string(plain.x) + " -> " + std::to_string(sz.x));
  ImGui::EndFrame();
  ImGui::DestroyContext();

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
