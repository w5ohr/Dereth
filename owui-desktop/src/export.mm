#include "export.h"

#import <WebKit/WebKit.h>

#include <regex>

#include "core/common.h"
#include "ui/highlight.h"

namespace exporter {
namespace {

std::string escapeHtml(const std::string& s) {
  std::string out;
  for (char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      default: out += c;
    }
  }
  return out;
}

// Minimal markdown -> HTML for exports: fenced code (highlighted), headings,
// lists, paragraphs, inline code/bold/links. Good enough to read and print.
std::string markdownToHtml(const std::string& text) {
  std::string out;
  std::vector<std::string> lines = core::split(text, '\n');
  bool inList = false;
  bool inPara = false;
  auto closePara = [&] { if (inPara) { out += "</p>\n"; inPara = false; } };
  auto closeList = [&] { if (inList) { out += "</ul>\n"; inList = false; } };

  auto inlineHtml = [](std::string s) {
    s = escapeHtml(s);
    s = std::regex_replace(s, std::regex("`([^`]+)`"), "<code>$1</code>");
    s = std::regex_replace(s, std::regex("\\*\\*([^*]+)\\*\\*"), "<strong>$1</strong>");
    s = std::regex_replace(s, std::regex("\\[([^\\]]+)\\]\\((https?://[^)]+)\\)"), "<a href=\"$2\">$1</a>");
    return s;
  };

  for (size_t i = 0; i < lines.size(); ++i) {
    const std::string& line = lines[i];
    std::string trimmed = core::trim(line);
    if (core::startsWith(trimmed, "```")) {
      closePara();
      closeList();
      std::string language = highlight::normalizeLanguage(core::trim(trimmed.substr(3)));
      std::string code;
      ++i;
      for (; i < lines.size() && !core::startsWith(core::trim(lines[i]), "```"); ++i) {
        code += lines[i] + "\n";
      }
      out += "<pre><code>" + highlight::toHtml(code, language) + "</code></pre>\n";
      continue;
    }
    if (trimmed.empty()) {
      closePara();
      closeList();
      continue;
    }
    if (trimmed[0] == '#') {
      size_t level = 0;
      while (level < trimmed.size() && trimmed[level] == '#') ++level;
      if (level <= 6 && level < trimmed.size() && trimmed[level] == ' ') {
        closePara();
        closeList();
        out += "<h" + std::to_string(level + 1) + ">" + inlineHtml(core::trim(trimmed.substr(level))) +
               "</h" + std::to_string(level + 1) + ">\n";
        continue;
      }
    }
    if ((trimmed[0] == '-' || trimmed[0] == '*') && trimmed.size() > 1 && trimmed[1] == ' ') {
      closePara();
      if (!inList) {
        out += "<ul>\n";
        inList = true;
      }
      out += "<li>" + inlineHtml(trimmed.substr(2)) + "</li>\n";
      continue;
    }
    closeList();
    if (!inPara) {
      out += "<p>";
      inPara = true;
    } else {
      out += " ";
    }
    out += inlineHtml(trimmed);
  }
  closePara();
  closeList();
  return out;
}

}  // namespace

std::string toMarkdown(const app::ChatDoc& chat) {
  std::string out = "# " + chat.title + "\n\n";
  for (const std::string& id : chat.branch()) {
    const app::Message& message = chat.messages.at(id);
    out += message.role == "user" ? "## You" : "## " + (message.modelName.empty() ? "Assistant" : message.modelName);
    if (message.timestamp) out += "  _" + core::formatTime(message.timestamp) + "_";
    out += "\n\n" + message.content + "\n\n";
  }
  return out;
}

std::string toHtml(const app::ChatDoc& chat, bool dark) {
  std::string bg = dark ? "#0d0d0f" : "#ffffff";
  std::string fg = dark ? "#ececf0" : "#1a1a1e";
  std::string panel = dark ? "#17171a" : "#f4f4f8";
  std::string dim = dark ? "#a6a6b0" : "#5c5c66";
  std::string out = "<!doctype html><html><head><meta charset=\"utf-8\"><title>" +
                    escapeHtml(chat.title) + "</title><style>"
                    "body{font-family:-apple-system,Helvetica,Arial,sans-serif;max-width:860px;"
                    "margin:40px auto;padding:0 24px;background:" + bg + ";color:" + fg + ";line-height:1.5}"
                    "h1{font-size:26px}h2,h3{font-size:18px;margin-top:28px}"
                    ".msg{margin:18px 0;padding:14px 18px;border-radius:12px;background:" + panel + "}"
                    ".msg.user{background:transparent;border:1px solid " + panel + "}"
                    ".who{font-weight:600;margin-bottom:6px}.when{color:" + dim + ";font-weight:400;font-size:12px;margin-left:8px}"
                    "pre{background:" + (dark ? "#111116" : "#f0f0f4") + ";padding:12px 14px;border-radius:8px;overflow:auto;font-size:13px}"
                    "code{font-family:SFMono-Regular,Menlo,monospace}"
                    ".hl-k{color:" + (dark ? "#c792ea" : "#9628b4") + "}.hl-t{color:" + (dark ? "#ffcb6b" : "#a06400") + "}"
                    ".hl-s{color:" + (dark ? "#c3e88d" : "#288228") + "}.hl-c{color:#6e7681}.hl-n{color:" + (dark ? "#f78c6c" : "#c8461e") + "}"
                    ".hl-f{color:" + (dark ? "#82aaff" : "#1e5ac8") + "}.hl-p{color:#89ddff}.hl-a{color:#ffe68c}"
                    "@media print{body{background:#fff;color:#000}.msg{background:#f6f6f6}}"
                    "</style></head><body><h1>" + escapeHtml(chat.title) + "</h1>\n";
  for (const std::string& id : chat.branch()) {
    const app::Message& message = chat.messages.at(id);
    const bool user = message.role == "user";
    out += "<div class=\"msg " + std::string(user ? "user" : "assistant") + "\"><div class=\"who\">" +
           escapeHtml(user ? "You" : (message.modelName.empty() ? "Assistant" : message.modelName));
    if (message.timestamp) out += "<span class=\"when\">" + core::formatTime(message.timestamp) + "</span>";
    out += "</div>" + markdownToHtml(message.content) + "</div>\n";
  }
  out += "</body></html>\n";
  return out;
}

bool htmlToPdf(const std::string& html, const std::string& outPath) {
  __block bool ok = false;
  __block bool finished = false;
  WKWebView* web = [[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 820, 1100)];
  NSString* source = [NSString stringWithUTF8String:html.c_str()];
  [web loadHTMLString:source baseURL:nil];

  // Poll the load, then ask WebKit for a PDF. Runs the main loop while
  // waiting so the web view can do its work.
  for (int i = 0; i < 200 && web.loading; ++i) {
    [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
  }
  [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.3]];

  WKPDFConfiguration* config = [[WKPDFConfiguration alloc] init];
  [web createPDFWithConfiguration:config
                completionHandler:^(NSData* data, NSError* error) {
                  if (data && !error) {
                    ok = [data writeToFile:[NSString stringWithUTF8String:outPath.c_str()] atomically:YES];
                  }
                  finished = true;
                }];
  for (int i = 0; i < 400 && !finished; ++i) {
    [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
  }
  return ok;
}

}  // namespace exporter
