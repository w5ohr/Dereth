#include "pane_common.h"

#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {

void paneHeader(const char* title, const char* subtitle,
                const std::function<void()>& actions) {
  ImGui::BeginChild("##paneheader", ImVec2(0, subtitle ? 62.0f : 54.0f));
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 origin = ImGui::GetWindowPos();
  dl->AddLine(ImVec2(origin.x, origin.y + ImGui::GetWindowHeight() - 1),
              ImVec2(origin.x + ImGui::GetWindowWidth(), origin.y + ImGui::GetWindowHeight() - 1),
              theme::palette().border);
  ImGui::Dummy(ImVec2(0, 10));
  ImGui::Indent(18.0f);
  ImGui::BeginGroup();
  theme::pushScaled(1.2f);
  theme::pushBold();
  ImGui::TextUnformatted(title);
  theme::pop();
  ImGui::PopFont();
  if (subtitle) widgets::dimText("%s", subtitle);
  ImGui::EndGroup();
  if (actions) {
    ImGui::SameLine();
    actions();
  }
  ImGui::Unindent(18.0f);
  ImGui::EndChild();
}

void paneBody(const char* id, float width) {
  ImGui::BeginChild(id, ImVec2(width, 0));
  ImGui::Dummy(ImVec2(0, 8));
  ImGui::Indent(18.0f);
}

void paneEnd() {
  ImGui::Unindent(18.0f);
  ImGui::Dummy(ImVec2(0, 24));
  ImGui::EndChild();
}

void card(const char* id, const std::function<void()>& body, float width) {
  ImGui::PushID(id);
  const ImVec2 start = ImGui::GetCursorScreenPos();
  const float cardWidth = width > 0.0f ? width : ImGui::GetContentRegionAvail().x - 18.0f;

  // The frame has to be painted behind the contents, and its height is only
  // known afterwards, so draw into a separate channel and merge at the end.
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->ChannelsSplit(2);
  dl->ChannelsSetCurrent(1);

  ImGui::BeginGroup();
  ImGui::Dummy(ImVec2(cardWidth, 1));
  ImGui::Indent(12.0f);
  ImGui::Dummy(ImVec2(0, 4));
  body();
  ImGui::Unindent(12.0f);
  ImGui::Dummy(ImVec2(0, 6));
  ImGui::EndGroup();
  const ImVec2 size = ImGui::GetItemRectSize();

  dl->ChannelsSetCurrent(0);
  dl->AddRectFilled(start, ImVec2(start.x + size.x, start.y + size.y), theme::palette().panel,
                    10.0f);
  dl->AddRect(start, ImVec2(start.x + size.x, start.y + size.y), theme::palette().border, 10.0f,
              1.0f);
  dl->ChannelsMerge();

  ImGui::Dummy(ImVec2(0, 8));
  ImGui::PopID();
}

void field(const char* label, const std::string& value) {
  ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(theme::palette().textFaint));
  ImGui::TextUnformatted(label);
  ImGui::PopStyleColor();
  ImGui::SameLine(170.0f);
  ImGui::TextUnformatted(value.c_str());
}

void emptyState(const char* message) {
  ImGui::Dummy(ImVec2(0, 30));
  ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(theme::palette().textFaint));
  widgets::centeredText(message);
  ImGui::PopStyleColor();
}

void monoOutput(const char* id, const std::string& text, ImVec2 size, bool scrollToEnd) {
  ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::toVec4(theme::palette().codeBg));
  ImGui::BeginChild(id, size, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
  theme::pushMono();
  if (text.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(theme::palette().textFaint));
    ImGui::TextUnformatted("(no output)");
    ImGui::PopStyleColor();
  } else {
    // TextUnformatted skips lines outside the visible area for long text, so
    // even a large log stays cheap to draw.
    ImGui::TextUnformatted(text.data(), text.data() + text.size());
  }
  theme::pop();
  if (scrollToEnd) ImGui::SetScrollHereY(1.0f);
  ImGui::EndChild();
  ImGui::PopStyleColor();
}

}  // namespace app
