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
  // Draws extra controls and results under a code block (run locally, lint,
  // apply to file). Called inside the block's ImGui ID scope.
  std::function<void(const std::string& code, const std::string& language)> onCodeBlockActions;
  // Applies a diff-shaped block; receives the (possibly hunk-filtered) diff.
  std::function<void(const std::string& diff)> onApplyDiff;
  // Invoked when a link is clicked; defaults to opening in the browser.
  std::function<void(const std::string& url)> onLink;
  // Renders a diagram or math block to PNG bytes (mermaid | dot | math) at
  // the given width; runs on a worker. When unset, such blocks stay as code.
  std::function<std::string(const std::string& kind, const std::string& code, float width)> blockImage;
};

void render(const char* id, const std::string& text, const Options& options);

// Strips markdown down to plain text (for titles, previews and TTS).
std::string toPlainText(const std::string& text);

}  // namespace md
