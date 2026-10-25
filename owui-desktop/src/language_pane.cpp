// Settings > Language and region (#144): the language (System, English,
// Deutsch, Español, Français, 日本語, or a pseudo-locale that shows what is
// still untranslated) and a preview of dates, times, numbers, sizes and a
// right-to-left paragraph in the chosen locale.
#include "app.h"
#include "core/common.h"
#include "core/i18n.h"
#include "core/settings.h"
#include "pane_common.h"
#include "ui/markdown.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {

void App::drawLanguageSection(float width) {
  const std::string current = settings().extra.value("language", std::string());
  std::string label = i18n::tr("System");
  for (const auto& l : i18n::languages()) {
    if (current == l.code) label = *l.code ? l.name : std::string(i18n::tr("System")) + " (" + i18n::systemLanguage() + ")";
  }
  ImGui::SetNextItemWidth(300);
  if (ImGui::BeginCombo(i18n::tr("Language"), label.c_str())) {
    for (const auto& l : i18n::languages()) {
      const std::string name = *l.code ? l.name : std::string(i18n::tr("System")) + " (" + i18n::systemLanguage() + ")";
      if (ImGui::Selectable(name.c_str(), current == l.code)) {
        settings().extra["language"] = l.code;
        settings().save();
        i18n::setLanguage(l.code);
      }
    }
    ImGui::EndCombo();
  }
  bool fromLanguage = settings().extra.value("formatsFromLanguage", false);
  widgets::beginDisabled(current.empty());
  if (ImGui::Checkbox(i18n::tr("Formats from the language instead of this Mac's region"), &fromLanguage)) {
    settings().extra["formatsFromLanguage"] = fromLanguage;
    settings().save();
    i18n::setFormatsFromLanguage(fromLanguage);
  }
  widgets::endDisabled(current.empty());
  const int64_t now = core::nowSeconds();
  field(i18n::tr("Date"), i18n::dateTime(now, true));
  field(i18n::tr("Edited"), core::relativeTime(now - 150) + " / " + core::relativeTime(now - 2 * 86400));
  field(i18n::tr("Number"), i18n::number(1234567.891, 2));
  field(i18n::tr("Size"), core::humanBytes(5400000));
  field(i18n::tr("Locale"), i18n::localeId().empty() ? "system" : i18n::localeId());
  ImGui::PushTextWrapPos(width - 20.0f);
  widgets::dimText("The language covers the app's frame - the rail, pane headers, sidebar, sign-in, composer, the empty chat and section headers (%d strings); the rest is still English (choose Pseudo to see which). Dates, relative times, numbers and sizes follow the locale everywhere. Paragraphs in Hebrew, Arabic and other right-to-left scripts are right-aligned in their reading order:",
                   static_cast<int>(i18n::tableSize()));
  ImGui::PopTextWrapPos();
  md::Options o;
  o.wrapWidth = width - 40.0f;
  md::render("rtlsample", "שלום! הגרסה 1.2 של Open WebUI מוכנה להורדה (גודל 4.5 MB).", o);
}

}  // namespace app
