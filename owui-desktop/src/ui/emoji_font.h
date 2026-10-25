// Colour emoji (#67): an ImGui font loader backed by Core Text that renders
// Apple Color Emoji glyphs into the RGBA atlas as coloured glyphs. Merged as
// the last source of each face, so text fonts keep every glyph they have and
// emoji fall through to it. Needs IMGUI_USE_WCHAR32 (emoji live above U+FFFF).
#pragma once

#include <cstdint>
#include <string>

#include "imgui.h"

struct ImFontLoader;

namespace emoji {

const ImFontLoader* loader();
bool available();                         // Apple Color Emoji could be opened
// Adds an emoji source to the font that was added last (MergeMode). The
// early source claims only characters Unicode shows as emoji by default
// (merge it before the text fallbacks); the late one takes any emoji
// candidate no text font had (merge it last).
void merge(float sizePixels, bool early);
// Joiners, variation selectors, tags and the keycap mark: drawn as nothing.
bool isZeroWidth(uint32_t codepoint);
bool isEmojiCandidate(uint32_t codepoint);     // a quick range test before asking Core Text
bool hasEmojiPresentation(uint32_t codepoint);  // Unicode Emoji_Presentation=Yes (shown as emoji without a selector)
bool isRegionalIndicator(uint32_t codepoint);   // flag halves: drawn as their letter (no sequence shaping)
// Counts in a UTF-8 string: codepoints above U+FFFF (which 16-bit ImWchar builds lose).
int astralCount(const std::string& utf8);

}  // namespace emoji
