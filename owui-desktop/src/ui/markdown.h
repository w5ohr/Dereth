// A markdown renderer for chat messages: headings, lists, tables, quotes,
// fenced code with copy/run, inline bold/italic/code/links/images.
#pragma once

#include <functional>
#include <string>

#include "imgui.h"

namespace md {

struct Options {
  float wrapWidth = 0.0f;   // 0 = use remaining content width
  bool compact = false;     // tighter spacing, used in sidebars and previews

  // Returns encoded image bytes for a markdown image URL. When unset, images
  // are rendered as links.
  std::function<std::string(const std::string& url)> imageLoader;
  // Invoked by the "Run" button on a code block.
  std::function<void(const std::string& code, const std::string& language)> onRunCode;
  // Invoked when a link is clicked; defaults to opening in the browser.
  std::function<void(const std::string& url)> onLink;
};

void render(const char* id, const std::string& text, const Options& options);

// Strips markdown down to plain text (for titles, previews and TTS).
std::string toPlainText(const std::string& text);

}  // namespace md
