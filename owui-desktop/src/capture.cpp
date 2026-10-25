// Screen capture to chat: the system capture UI (region or window) from a
// button, the palette, a menu item or a global hotkey; the shot is attached
// to the composer and any text in it (Live Text) is dropped in as a quoted
// block, so non-vision models can read it too.
#include "app.h"

#include "core/async.h"
#include "core/common.h"
#include "native.h"
#include "ui/widgets.h"

namespace app {

void App::captureScreenToChat(bool interactive) {
  if (!signedIn_) return;
  const bool ocr = settings().extra.value("captureOcr", true);
  async::run([this, interactive, ocr] {
    std::string png;
    if (!native::captureScreen(interactive, &png)) {
      async::post([] { widgets::toastInfo("Capture cancelled"); });
      return;
    }
    const std::string text = ocr ? core::trim(native::recognizeText(png)) : "";
    async::post([this, png, text] {
      pane = Pane::Chat;
      attachBytes("screenshot.png", "image/png", png);
      if (!text.empty()) {
        if (!composer.empty() && composer.back() != '\n') composer += "\n";
        composer += "Text in the screenshot:\n```\n" + text + "\n```\n";
        ++composerGeneration;
      }
      focusComposer = true;
      widgets::toast(widgets::ToastLevel::Success,
                     text.empty() ? "Screenshot attached" : "Screenshot attached with its text");
      native::showMainWindow();
    });
  });
}

}  // namespace app
