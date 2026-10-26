// Logprob view in the app (#78): the request fields, the per-message
// "confidence" toggle in the action row, the coloured token flow with
// alternatives on hover, and the Settings switch.
#include <set>

#include "app.h"
#include "core/logprobs.h"
#include "core/settings.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {
std::set<std::string> gShown;   // message ids drawn as a token flow
}  // namespace

logprobs::Prefs App::logprobPrefs() const { return logprobs::prefsFromJson(core::Settings::instance().extra); }

void App::applyLogprobRequest(Json& fields) {
  const logprobs::Prefs p = logprobPrefs();
  if (!p.enabled || !fields.is_object()) return;
  fields["logprobs"] = true;
  fields["top_logprobs"] = p.top;
}

bool App::confidenceShown(const std::string& messageId) const { return gShown.count(messageId) > 0; }

void App::drawConfidenceButton(const Message& message) {
  if (message.logprobs.empty()) return;
  const auto& palette = theme::palette();
  const bool on = confidenceShown(message.id);
  ImGui::SameLine();
  if (icons::button("confidence", icons::Icon::Layers, on ? "Hide token confidence" : "Token confidence: colour each token by how sure the model was", 0.0f, on ? palette.accent : 0)) {
    if (on) gShown.erase(message.id);
    else gShown.insert(message.id);
  }
}

void App::drawTokenFlow(const Message& message, float width) {
  const auto& palette = theme::palette();
  const std::vector<logprobs::Token> tokens = logprobs::fromJson(message.logprobs);
  const logprobs::Stats s = logprobs::stats(tokens);
  widgets::dimText("Confidence: %s", logprobs::statsLine(s, tokens).c_str());
  if (!logprobs::aligned(tokens, unredact(message.content))) {
    widgets::dimText("The tokens do not match the text shown (thinking or edits in between), so this is the raw token stream.");
  }
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const float lineH = ImGui::GetTextLineHeight() + 4.0f;
  const ImVec2 mouse = ImGui::GetMousePos();
  float x = 0.0f, y = 0.0f;
  int hovered = -1;
  for (size_t i = 0; i < tokens.size(); ++i) {
    const std::string& tok = tokens[i].token;
    size_t start = 0;
    bool first = true;
    while (start <= tok.size()) {
      const size_t nl = tok.find('\n', start);
      const std::string piece = tok.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
      if (!first) {
        x = 0.0f;
        y += lineH;
      }
      first = false;
      if (!piece.empty()) {
        const ImVec2 size = ImGui::CalcTextSize(piece.c_str());
        if (x > 0.0f && x + size.x > width) {
          x = 0.0f;
          y += lineH;
        }
        const ImVec2 a(origin.x + x, origin.y + y), b(origin.x + x + size.x, origin.y + y + lineH);
        const logprobs::Rgb c = logprobs::tint(logprobs::confidence(tokens[i].logprob));
        dl->AddRectFilled(a, b, ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, 0.38f)), 2.0f);
        dl->AddText(ImVec2(a.x, a.y + 2.0f), palette.text, piece.c_str());
        if (mouse.x >= a.x && mouse.x < b.x && mouse.y >= a.y && mouse.y < b.y) hovered = static_cast<int>(i);
        x += size.x;
      }
      if (nl == std::string::npos) break;
      start = nl + 1;
    }
  }
  ImGui::Dummy(ImVec2(width, y + lineH));
  if (hovered >= 0 && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
    const logprobs::Token& t = tokens[static_cast<size_t>(hovered)];
    ImGui::BeginTooltip();
    theme::pushMono();
    ImGui::TextUnformatted(("'" + t.token + "'").c_str());
    theme::pop();
    ImGui::SameLine();
    const double p = logprobs::confidence(t.logprob);
    const logprobs::Rgb c = logprobs::tint(p);
    ImGui::TextColored(ImVec4(c.r, c.g, c.b, 1.0f), "%s", logprobs::percent(p).c_str());
    bool any = false;
    for (const logprobs::Alt& a : t.alts) {
      if (a.token == t.token) continue;
      if (!any) widgets::dimText("alternatives");
      any = true;
      const double ap = logprobs::confidence(a.logprob);
      const logprobs::Rgb ac = logprobs::tint(ap);
      theme::pushMono();
      ImGui::TextUnformatted(("  '" + a.token + "'").c_str());
      theme::pop();
      ImGui::SameLine();
      ImGui::TextColored(ImVec4(ac.r, ac.g, ac.b, 1.0f), "%s", logprobs::percent(ap).c_str());
    }
    ImGui::EndTooltip();
  }
  widgets::dimText("%s", "green: sure, amber: unsure, red: under 50%. Hover a token for the alternatives the model weighed.");
}

void App::drawLogprobSettings() {
  logprobs::Prefs p = logprobPrefs();
  bool changed = ImGui::Checkbox("Ask for token probabilities (a confidence view on every reply)", &p.enabled);
  if (p.enabled) {
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    changed |= ImGui::SliderInt("alternatives", &p.top, 1, 8);
  }
  if (changed) {
    settings().extra["logprobs"] = logprobs::prefsToJson(p);
    settings().save();
  }
  if (p.enabled) widgets::dimText("Ollama 0.12+ and OpenAI-style backends return them; the server passes them on for OpenAI-style models. Anthropic has none.");
}

}  // namespace app
