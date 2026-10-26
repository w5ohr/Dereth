// Chat export: Markdown, self-contained HTML with highlighting, and PDF
// rendered through WebKit.
#pragma once

#include <string>

#include "app.h"

namespace exporter {

std::string toMarkdown(const app::ChatDoc& chat);
std::string toHtml(const app::ChatDoc& chat, bool dark);
// Blocks until the PDF is written. Returns false on failure.
bool htmlToPdf(const std::string& html, const std::string& outPath);

}  // namespace exporter
