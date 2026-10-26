// Reading mode (#63): a narrower measure, larger text and more line spacing
// for the transcript, the sidebar tucked away, and every per-message control
// hidden until the pointer rests on that message. The pure parts: the
// preferences and the layout decisions. App-free; --readingtest covers it.
#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace readingmode {

using Json = nlohmann::json;

struct Prefs {
  bool enabled = false;
  float measure = 680.0f;        // content width in points (480 .. 1000)
  float textScale = 1.15f;       // 1.0 .. 1.6
  float lineSpacing = 1.3f;      // multiplier on the vertical item spacing (1.0 .. 2.0)
  bool hideSidebar = true;
  bool controlsOnHover = true;   // action rows, chips and follow-ups only when hovered or focused
};
Prefs fromJson(const Json& extra);   // extra["readingMode"]
Json toJson(const Prefs& prefs);

float contentWidth(const Prefs& prefs, float available, float defaultMax);   // what the transcript gets
float itemSpacingY(const Prefs& prefs, float base);
bool showControls(const Prefs& prefs, bool hovered, bool focused);
std::string summary(const Prefs& prefs);   // "680 px measure, text 115%, line spacing 1.3x"

}  // namespace readingmode
