// Lightweight syntax highlighting for code blocks. Tokenises a line into
// coloured spans; no parser, just enough lexing to make code readable.
#pragma once

#include <string>
#include <vector>

#include "imgui.h"

namespace highlight {

enum class Kind { Plain, Keyword, Type, String, Comment, Number, Function, Punct, Preproc, Attribute };

struct Span {
  size_t begin;
  size_t end;
  Kind kind;
};

// Normalises aliases ("py" -> "python", "c++" -> "cpp") and returns "" when
// the language is unknown, in which case callers should render plain text.
std::string normalizeLanguage(const std::string& language);

// `inBlockComment` carries multi-line comment state between lines.
std::vector<Span> tokenize(const std::string& line, const std::string& language,
                           bool* inBlockComment);

ImU32 colorFor(Kind kind, bool darkTheme);

// Draws one line with the current font at the cursor and advances.
void drawLine(const std::string& line, const std::string& language, bool* inBlockComment,
              bool darkTheme);

// HTML rendering for exports: <span class="hl-keyword">...</span>.
std::string toHtml(const std::string& code, const std::string& language);
const char* cssClass(Kind kind);

}  // namespace highlight
