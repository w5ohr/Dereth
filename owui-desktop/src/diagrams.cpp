// Diagrams and math inline: a mermaid, dot/graphviz or math block (and a
// $$...$$ paragraph) is rendered by an offscreen WKWebView - KaTeX, Mermaid
// and Viz.js from their CDNs - snapshotted to a PNG and shown in the
// transcript as an image, with the source one click away. The renderer is
// called from the image cache's worker and blocks there; the web view runs
// one job at a time on the main thread.
#include "app.h"

#include "core/common.h"
#include "native.h"
#include "ui/theme.h"

namespace app {
namespace {

std::string escapeHtml(const std::string& text) {
  std::string out;
  for (char c : text) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      default: out += c;
    }
  }
  return out;
}

std::string hex(ImU32 c) {
  char buf[16];
  snprintf(buf, sizeof(buf), "#%02x%02x%02x", (c >> IM_COL32_R_SHIFT) & 0xff, (c >> IM_COL32_G_SHIFT) & 0xff,
           (c >> IM_COL32_B_SHIFT) & 0xff);
  return buf;
}

// Shared page shell: the content area is `width` CSS px wide; `done()`
// reports the laid-out height (or an error) once fonts and scripts settled.
std::string shell(const std::string& body, const std::string& script, float width, const std::string& fg,
                  const std::string& bg) {
  return "<!doctype html><html><head><meta charset=\"utf-8\"><style>"
         "html,body{margin:0;padding:0;background:" + bg + ";color:" + fg + ";font-family:-apple-system,sans-serif;font-size:15px}"
         "body{width:" + std::to_string(static_cast<int>(width) - 24) + "px;padding:12px;overflow:hidden}"
         "svg{max-width:100%;height:auto}.katex-display{margin:4px 0}.katex{font-size:1.15em}"
         "</style></head><body>" + body +
         // No requestAnimationFrame here: it never fires in a view that is not on screen.
         // Height is the body's own box (padding included), not the viewport.
         "<script>let finished=false;function done(){finished=true;document.fonts.ready.then(()=>setTimeout(()=>"
         "window.webkit.messageHandlers.owuiDone.postMessage(Math.ceil(document.body.offsetHeight)),60))}"
         "function fail(e){finished=true;window.webkit.messageHandlers.owuiDone.postMessage('error:'+(e&&e.message?e.message:String(e)))}"
         // A stray script error only fails the render when nothing finished shortly after it.
         "window.addEventListener('error',e=>setTimeout(()=>{if(!finished)fail(e.error||e.message)},1500));</script>" + script + "</body></html>";
}

}  // namespace

std::string App::renderBlockImage(const std::string& kind, const std::string& code, float width) {
  const auto& palette = theme::palette();
  const std::string fg = hex(palette.text), bg = hex(palette.panel);
  const bool dark = theme::isDark();
  const std::string src = Json(code).dump();   // JSON string literal for the page's script
  std::string html;
  if (kind == "mermaid") {
    html = shell("<pre class=\"mermaid\" style=\"background:transparent\">" + escapeHtml(code) + "</pre>",
                 "<script type=\"module\">try{const m=(await import('https://cdn.jsdelivr.net/npm/mermaid@11/dist/mermaid.esm.min.mjs')).default;"
                 "m.initialize({startOnLoad:false,theme:'" + std::string(dark ? "dark" : "default") + "',securityLevel:'loose'});"
                 "await m.run();done()}catch(e){fail(e)}</script>",
                 width, fg, bg);
  } else if (kind == "dot") {
    html = shell("<div id=\"g\"></div>",
                 "<script type=\"module\">try{const {instance}=await import('https://cdn.jsdelivr.net/npm/@viz-js/viz@3.11.0/lib/viz-standalone.mjs');"
                 "const viz=await instance();const a={bgcolor:'transparent',color:'" + fg + "',fontcolor:'" + fg + "'};"
                 "const svg=viz.renderSVGElement(" + src + ",{graphAttributes:a,nodeAttributes:a,edgeAttributes:a});"
                 "document.getElementById('g').appendChild(svg);done()}catch(e){fail(e)}</script>",
                 width, fg, bg);
  } else {   // math
    html = shell("<link rel=\"stylesheet\" href=\"https://cdn.jsdelivr.net/npm/katex@0.16.11/dist/katex.min.css\"><div id=\"m\"></div>",
                 // The source goes in its own script, not an onload attribute: the JSON literal holds quotes.
                 "<script crossorigin=\"anonymous\" src=\"https://cdn.jsdelivr.net/npm/katex@0.16.11/dist/katex.min.js\" onerror=\"fail('KaTeX could not be loaded')\"></script>"
                 "<script>try{katex.render(" + src + ",document.getElementById('m'),{displayMode:true,throwOnError:false});done()}catch(e){fail(e)}</script>",
                 width, fg, bg);
  }
  return native::renderHtmlToPng(html, width, 15000);
}

}  // namespace app
