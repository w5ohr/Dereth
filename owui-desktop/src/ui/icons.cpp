#include "ui/icons.h"

#include <cmath>

#include "imgui_internal.h"
#include "ui/theme.h"

namespace icons {
namespace {

void line(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 c, float t) { dl->AddLine(a, b, c, t); }

void arrow(ImDrawList* dl, ImVec2 c, float s, ImU32 col, float t, float angleDeg) {
  const float rad = angleDeg * 3.14159265f / 180.0f;
  auto rot = [&](ImVec2 p) {
    float x = p.x - c.x, y = p.y - c.y;
    return ImVec2(c.x + x * cosf(rad) - y * sinf(rad), c.y + x * sinf(rad) + y * cosf(rad));
  };
  ImVec2 tip(c.x, c.y - s * 0.5f);
  ImVec2 tail(c.x, c.y + s * 0.5f);
  line(dl, rot(tail), rot(tip), col, t);
  line(dl, rot(tip), rot(ImVec2(c.x - s * 0.32f, c.y - s * 0.16f)), col, t);
  line(dl, rot(tip), rot(ImVec2(c.x + s * 0.32f, c.y - s * 0.16f)), col, t);
}

}  // namespace

void draw(ImDrawList* dl, Icon icon, ImVec2 c, float s, ImU32 col, float t) {
  const float h = s * 0.5f;
  const float q = s * 0.25f;
  switch (icon) {
    case Icon::Plus:
      line(dl, ImVec2(c.x - h, c.y), ImVec2(c.x + h, c.y), col, t);
      line(dl, ImVec2(c.x, c.y - h), ImVec2(c.x, c.y + h), col, t);
      break;
    case Icon::Close:
      line(dl, ImVec2(c.x - q, c.y - q), ImVec2(c.x + q, c.y + q), col, t);
      line(dl, ImVec2(c.x + q, c.y - q), ImVec2(c.x - q, c.y + q), col, t);
      break;
    case Icon::Send:
      arrow(dl, c, s * 0.9f, col, t, 0.0f);
      break;
    case Icon::Stop:
      dl->AddRectFilled(ImVec2(c.x - q, c.y - q), ImVec2(c.x + q, c.y + q), col, 2.0f);
      break;
    case Icon::Play:
      dl->AddTriangleFilled(ImVec2(c.x - q, c.y - h * 0.8f), ImVec2(c.x - q, c.y + h * 0.8f),
                            ImVec2(c.x + h * 0.8f, c.y), col);
      break;
    case Icon::Copy:
      dl->AddRect(ImVec2(c.x - h, c.y - h), ImVec2(c.x + q * 0.6f, c.y + q * 0.6f), col, 2.0f, t);
      dl->AddRect(ImVec2(c.x - q * 0.6f, c.y - q * 0.6f), ImVec2(c.x + h, c.y + h), col, 2.0f, t);
      break;
    case Icon::Trash:
      line(dl, ImVec2(c.x - h, c.y - q), ImVec2(c.x + h, c.y - q), col, t);
      dl->AddRect(ImVec2(c.x - q * 1.4f, c.y - q), ImVec2(c.x + q * 1.4f, c.y + h), col, 2.0f, t);
      line(dl, ImVec2(c.x - q * 0.5f, c.y - h), ImVec2(c.x + q * 0.5f, c.y - h), col, t);
      break;
    case Icon::Pencil:
      line(dl, ImVec2(c.x - h, c.y + h), ImVec2(c.x - q * 0.6f, c.y + q * 0.6f), col, t);
      line(dl, ImVec2(c.x - q * 0.6f, c.y + q * 0.6f), ImVec2(c.x + h * 0.8f, c.y - h * 0.8f), col, t);
      line(dl, ImVec2(c.x - h, c.y + h), ImVec2(c.x - q * 0.2f, c.y + h * 0.85f), col, t);
      break;
    case Icon::Refresh: {
      dl->PathArcTo(c, h * 0.8f, 0.6f, 5.2f, 20);
      dl->PathStroke(col, t);
      ImVec2 tip(c.x + h * 0.8f * cosf(0.6f), c.y + h * 0.8f * sinf(0.6f));
      line(dl, tip, ImVec2(tip.x - q * 0.7f, tip.y - q * 0.2f), col, t);
      line(dl, tip, ImVec2(tip.x + q * 0.1f, tip.y - q * 0.8f), col, t);
      break;
    }
    case Icon::Chevron:
      line(dl, ImVec2(c.x - q, c.y - q * 0.5f), ImVec2(c.x, c.y + q * 0.5f), col, t);
      line(dl, ImVec2(c.x + q, c.y - q * 0.5f), ImVec2(c.x, c.y + q * 0.5f), col, t);
      break;
    case Icon::ChevronRight:
      line(dl, ImVec2(c.x - q * 0.5f, c.y - q), ImVec2(c.x + q * 0.5f, c.y), col, t);
      line(dl, ImVec2(c.x - q * 0.5f, c.y + q), ImVec2(c.x + q * 0.5f, c.y), col, t);
      break;
    case Icon::Search:
      dl->AddCircle(ImVec2(c.x - q * 0.3f, c.y - q * 0.3f), h * 0.6f, col, 0, t);
      line(dl, ImVec2(c.x + q * 0.2f, c.y + q * 0.2f), ImVec2(c.x + h * 0.8f, c.y + h * 0.8f), col, t);
      break;
    case Icon::Paperclip: {
      // Shaft, hook and inner return — a recognisable clip at small sizes.
      const float left = c.x - q * 0.7f;
      const float right = c.x + q * 0.7f;
      dl->PathClear();
      dl->PathLineTo(ImVec2(right, c.y - h * 0.55f));
      dl->PathLineTo(ImVec2(right, c.y + q * 0.5f));
      dl->PathArcTo(ImVec2(c.x, c.y + q * 0.5f), q * 0.7f, 0.0f, 3.14159f, 12);
      dl->PathLineTo(ImVec2(left, c.y - h * 0.55f));
      dl->PathArcTo(ImVec2(c.x - q * 0.1f, c.y - h * 0.55f), q * 0.6f, 3.14159f, 6.28318f, 12);
      dl->PathLineTo(ImVec2(c.x + q * 0.5f, c.y + q * 0.3f));
      dl->PathStroke(col, t);
      break;
    }
    case Icon::Mic:
      dl->AddRectFilled(ImVec2(c.x - q * 0.5f, c.y - h), ImVec2(c.x + q * 0.5f, c.y + q * 0.2f), col,
                        q * 0.5f);
      dl->PathArcTo(c, h * 0.7f, 0.0f, 3.14159f, 16);
      dl->PathStroke(col, t);
      line(dl, ImVec2(c.x, c.y + h * 0.7f), ImVec2(c.x, c.y + h), col, t);
      break;
    case Icon::Image:
      dl->AddRect(ImVec2(c.x - h, c.y - h * 0.8f), ImVec2(c.x + h, c.y + h * 0.8f), col, 2.0f, t);
      dl->AddCircleFilled(ImVec2(c.x - q * 0.6f, c.y - q * 0.4f), q * 0.35f, col);
      dl->AddTriangleFilled(ImVec2(c.x - q, c.y + h * 0.7f), ImVec2(c.x + q * 0.3f, c.y - q * 0.1f),
                            ImVec2(c.x + h * 0.9f, c.y + h * 0.7f), col);
      break;
    case Icon::Pin:
      line(dl, ImVec2(c.x, c.y + h), ImVec2(c.x, c.y), col, t);
      dl->AddCircleFilled(ImVec2(c.x, c.y - q * 0.6f), q * 0.9f, col);
      break;
    case Icon::Star: {
      ImVec2 pts[10];
      for (int i = 0; i < 10; ++i) {
        float a = -1.5708f + i * 0.6283f;
        float r = (i % 2 == 0) ? h : h * 0.45f;
        pts[i] = ImVec2(c.x + cosf(a) * r, c.y + sinf(a) * r);
      }
      dl->AddConvexPolyFilled(pts, 10, col);
      break;
    }
    case Icon::Gear:
      dl->AddCircle(c, h * 0.55f, col, 0, t);
      for (int i = 0; i < 6; ++i) {
        float a = i * 1.0472f;
        line(dl, ImVec2(c.x + cosf(a) * h * 0.65f, c.y + sinf(a) * h * 0.65f),
             ImVec2(c.x + cosf(a) * h, c.y + sinf(a) * h), col, t);
      }
      break;
    case Icon::User:
      dl->AddCircle(ImVec2(c.x, c.y - q * 0.6f), q * 0.8f, col, 0, t);
      dl->PathArcTo(ImVec2(c.x, c.y + h * 1.1f), h * 0.85f, 3.34159f, 6.08318f, 16);
      dl->PathStroke(col, t);
      break;
    case Icon::Folder:
      line(dl, ImVec2(c.x - h, c.y - q), ImVec2(c.x - q * 0.2f, c.y - q), col, t);
      dl->AddRect(ImVec2(c.x - h, c.y - q * 0.4f), ImVec2(c.x + h, c.y + h * 0.8f), col, 2.0f, t);
      break;
    case Icon::Chat:
      dl->AddRect(ImVec2(c.x - h, c.y - h * 0.85f), ImVec2(c.x + h, c.y + q * 0.5f), col, 3.0f, t);
      line(dl, ImVec2(c.x - q * 0.5f, c.y + q * 0.5f), ImVec2(c.x - q * 0.1f, c.y + h), col, t);
      break;
    case Icon::Archive:
      dl->AddRect(ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y - q * 0.6f), col, 2.0f, t);
      dl->AddRect(ImVec2(c.x - h * 0.85f, c.y - q * 0.4f), ImVec2(c.x + h * 0.85f, c.y + h), col, 2.0f, t);
      line(dl, ImVec2(c.x - q * 0.5f, c.y + q * 0.1f), ImVec2(c.x + q * 0.5f, c.y + q * 0.1f), col, t);
      break;
    case Icon::Share:
      dl->AddCircle(ImVec2(c.x + h * 0.6f, c.y - h * 0.6f), q * 0.5f, col, 0, t);
      dl->AddCircle(ImVec2(c.x - h * 0.6f, c.y), q * 0.5f, col, 0, t);
      dl->AddCircle(ImVec2(c.x + h * 0.6f, c.y + h * 0.6f), q * 0.5f, col, 0, t);
      line(dl, ImVec2(c.x - q * 0.7f, c.y - q * 0.2f), ImVec2(c.x + q, c.y - q * 0.9f), col, t);
      line(dl, ImVec2(c.x - q * 0.7f, c.y + q * 0.2f), ImVec2(c.x + q, c.y + q * 0.9f), col, t);
      break;
    case Icon::ThumbUp:
      dl->AddRect(ImVec2(c.x - h, c.y - q * 0.2f), ImVec2(c.x - q * 0.4f, c.y + h), col, 1.5f, t);
      dl->AddRect(ImVec2(c.x - q * 0.2f, c.y - q * 0.2f), ImVec2(c.x + h, c.y + h), col, 2.0f, t);
      line(dl, ImVec2(c.x, c.y - q * 0.2f), ImVec2(c.x + q * 0.3f, c.y - h), col, t);
      break;
    case Icon::ThumbDown:
      dl->AddRect(ImVec2(c.x - h, c.y - h), ImVec2(c.x - q * 0.4f, c.y + q * 0.2f), col, 1.5f, t);
      dl->AddRect(ImVec2(c.x - q * 0.2f, c.y - h), ImVec2(c.x + h, c.y + q * 0.2f), col, 2.0f, t);
      line(dl, ImVec2(c.x, c.y + q * 0.2f), ImVec2(c.x + q * 0.3f, c.y + h), col, t);
      break;
    case Icon::Globe:
      dl->AddCircle(c, h * 0.9f, col, 0, t);
      dl->AddEllipse(c, ImVec2(h * 0.4f, h * 0.9f), col, 0.0f, 0, t);
      line(dl, ImVec2(c.x - h * 0.9f, c.y), ImVec2(c.x + h * 0.9f, c.y), col, t);
      break;
    case Icon::Code:
      line(dl, ImVec2(c.x - q * 0.3f, c.y - h), ImVec2(c.x - h, c.y), col, t);
      line(dl, ImVec2(c.x - h, c.y), ImVec2(c.x - q * 0.3f, c.y + h), col, t);
      line(dl, ImVec2(c.x + q * 0.3f, c.y - h), ImVec2(c.x + h, c.y), col, t);
      line(dl, ImVec2(c.x + h, c.y), ImVec2(c.x + q * 0.3f, c.y + h), col, t);
      break;
    case Icon::Book:
      dl->AddRect(ImVec2(c.x - h, c.y - h * 0.9f), ImVec2(c.x + h, c.y + h * 0.9f), col, 2.0f, t);
      line(dl, ImVec2(c.x, c.y - h * 0.9f), ImVec2(c.x, c.y + h * 0.9f), col, t);
      break;
    case Icon::Sparkle:
      line(dl, ImVec2(c.x, c.y - h), ImVec2(c.x, c.y + h), col, t);
      line(dl, ImVec2(c.x - h, c.y), ImVec2(c.x + h, c.y), col, t);
      line(dl, ImVec2(c.x - q * 0.7f, c.y - q * 0.7f), ImVec2(c.x + q * 0.7f, c.y + q * 0.7f), col,
           t * 0.7f);
      line(dl, ImVec2(c.x + q * 0.7f, c.y - q * 0.7f), ImVec2(c.x - q * 0.7f, c.y + q * 0.7f), col,
           t * 0.7f);
      break;
    case Icon::Check:
      line(dl, ImVec2(c.x - h * 0.8f, c.y), ImVec2(c.x - q * 0.2f, c.y + q * 0.8f), col, t);
      line(dl, ImVec2(c.x - q * 0.2f, c.y + q * 0.8f), ImVec2(c.x + h * 0.85f, c.y - q * 0.9f), col, t);
      break;
    case Icon::Menu:
      for (int i = -1; i <= 1; ++i) {
        line(dl, ImVec2(c.x - h, c.y + i * q * 0.8f), ImVec2(c.x + h, c.y + i * q * 0.8f), col, t);
      }
      break;
    case Icon::Download:
      arrow(dl, ImVec2(c.x, c.y - q * 0.3f), s * 0.7f, col, t, 180.0f);
      line(dl, ImVec2(c.x - h, c.y + h * 0.8f), ImVec2(c.x + h, c.y + h * 0.8f), col, t);
      break;
    case Icon::Speaker:
      dl->AddTriangleFilled(ImVec2(c.x - h * 0.3f, c.y - q), ImVec2(c.x - h * 0.3f, c.y + q),
                            ImVec2(c.x - h, c.y), col);
      dl->AddRectFilled(ImVec2(c.x - h, c.y - q * 0.5f), ImVec2(c.x - h * 0.4f, c.y + q * 0.5f), col);
      dl->AddTriangleFilled(ImVec2(c.x - h * 0.4f, c.y - h * 0.8f),
                            ImVec2(c.x - h * 0.4f, c.y + h * 0.8f), ImVec2(c.x + q * 0.2f, c.y), col);
      dl->PathArcTo(ImVec2(c.x + q * 0.2f, c.y), h * 0.6f, -1.0f, 1.0f, 12);
      dl->PathStroke(col, t);
      break;
    case Icon::Branch:
      dl->AddCircle(ImVec2(c.x - q, c.y - h * 0.7f), q * 0.5f, col, 0, t);
      dl->AddCircle(ImVec2(c.x - q, c.y + h * 0.7f), q * 0.5f, col, 0, t);
      dl->AddCircle(ImVec2(c.x + h * 0.7f, c.y - h * 0.7f), q * 0.5f, col, 0, t);
      line(dl, ImVec2(c.x - q, c.y - q * 0.6f), ImVec2(c.x - q, c.y + q * 0.6f), col, t);
      line(dl, ImVec2(c.x - q, c.y), ImVec2(c.x + h * 0.7f, c.y), col, t);
      line(dl, ImVec2(c.x + h * 0.7f, c.y), ImVec2(c.x + h * 0.7f, c.y - q * 0.6f), col, t);
      break;
    case Icon::Info:
      dl->AddCircle(c, h * 0.9f, col, 0, t);
      line(dl, ImVec2(c.x, c.y - q * 0.2f), ImVec2(c.x, c.y + q * 0.9f), col, t);
      dl->AddCircleFilled(ImVec2(c.x, c.y - q * 0.9f), t * 0.9f, col);
      break;
    case Icon::Warning:
      dl->AddTriangle(ImVec2(c.x, c.y - h), ImVec2(c.x - h, c.y + h * 0.8f),
                      ImVec2(c.x + h, c.y + h * 0.8f), col, t);
      line(dl, ImVec2(c.x, c.y - q * 0.4f), ImVec2(c.x, c.y + q * 0.5f), col, t);
      break;
    case Icon::Note:
      dl->AddRect(ImVec2(c.x - h * 0.8f, c.y - h), ImVec2(c.x + h * 0.8f, c.y + h), col, 2.0f, t);
      for (int i = -1; i <= 1; ++i) {
        line(dl, ImVec2(c.x - q, c.y + i * q * 0.7f), ImVec2(c.x + q, c.y + i * q * 0.7f), col,
             t * 0.8f);
      }
      break;
    case Icon::Channel:
      line(dl, ImVec2(c.x - q, c.y - h), ImVec2(c.x - h * 0.7f, c.y + h), col, t);
      line(dl, ImVec2(c.x + h * 0.7f, c.y - h), ImVec2(c.x + q * 0.6f, c.y + h), col, t);
      line(dl, ImVec2(c.x - h, c.y - q * 0.6f), ImVec2(c.x + h, c.y - q * 0.6f), col, t);
      line(dl, ImVec2(c.x - h, c.y + q * 0.6f), ImVec2(c.x + h, c.y + q * 0.6f), col, t);
      break;
    case Icon::Wrench:
      dl->AddCircle(ImVec2(c.x - q * 0.8f, c.y - q * 0.8f), q * 0.8f, col, 0, t);
      line(dl, ImVec2(c.x - q * 0.2f, c.y - q * 0.2f), ImVec2(c.x + h, c.y + h), col, t * 1.4f);
      break;
    case Icon::Brain:
      dl->AddCircle(ImVec2(c.x - q * 0.6f, c.y), h * 0.6f, col, 0, t);
      dl->AddCircle(ImVec2(c.x + q * 0.6f, c.y), h * 0.6f, col, 0, t);
      line(dl, ImVec2(c.x, c.y - q * 0.7f), ImVec2(c.x, c.y + q * 0.7f), col, t * 0.8f);
      break;
    case Icon::Layers:
      dl->AddQuad(ImVec2(c.x, c.y - h), ImVec2(c.x + h, c.y - q * 0.4f), ImVec2(c.x, c.y + q * 0.2f),
                  ImVec2(c.x - h, c.y - q * 0.4f), col, t);
      line(dl, ImVec2(c.x - h, c.y + q * 0.3f), ImVec2(c.x, c.y + h * 0.9f), col, t);
      line(dl, ImVec2(c.x + h, c.y + q * 0.3f), ImVec2(c.x, c.y + h * 0.9f), col, t);
      break;
    case Icon::Plug:
      // Two prongs, a body, a cord: the MCP tool-server toggle.
      line(dl, ImVec2(c.x - q * 0.5f, c.y - h), ImVec2(c.x - q * 0.5f, c.y - q * 0.3f), col, t);
      line(dl, ImVec2(c.x + q * 0.5f, c.y - h), ImVec2(c.x + q * 0.5f, c.y - q * 0.3f), col, t);
      dl->AddRect(ImVec2(c.x - h * 0.9f, c.y - q * 0.3f), ImVec2(c.x + h * 0.9f, c.y + q * 0.5f), col,
                  q * 0.3f, t);
      line(dl, ImVec2(c.x, c.y + q * 0.5f), ImVec2(c.x, c.y + h), col, t);
      break;
    case Icon::Camera:
      // Body with a raised viewfinder bump and a lens.
      dl->AddRect(ImVec2(c.x - h, c.y - q * 0.4f), ImVec2(c.x + h, c.y + h * 0.8f), col, q * 0.3f, t);
      line(dl, ImVec2(c.x - q * 0.5f, c.y - q * 0.4f), ImVec2(c.x - q * 0.3f, c.y - h * 0.8f), col, t);
      line(dl, ImVec2(c.x - q * 0.3f, c.y - h * 0.8f), ImVec2(c.x + q * 0.3f, c.y - h * 0.8f), col, t);
      line(dl, ImVec2(c.x + q * 0.3f, c.y - h * 0.8f), ImVec2(c.x + q * 0.5f, c.y - q * 0.4f), col, t);
      dl->AddCircle(ImVec2(c.x, c.y + q * 0.2f), q * 0.5f, col, 0, t);
      break;
  }
}

bool button(const char* id, Icon icon, const char* tooltip, float size, ImU32 color) {
  ImGuiStyle& style = ImGui::GetStyle();
  const float iconSize = size > 0.0f ? size : ImGui::GetFontSize();
  const ImVec2 box(iconSize + style.FramePadding.x * 1.4f, iconSize + style.FramePadding.y * 1.4f);
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  bool clicked = ImGui::InvisibleButton(id, box);
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  if (hovered) {
    dl->AddRectFilled(pos, ImVec2(pos.x + box.x, pos.y + box.y), theme::palette().panelAlt, 6.0f);
  }
  ImU32 tint = color ? color : (hovered ? theme::palette().text : theme::palette().textDim);
  draw(dl, icon, ImVec2(pos.x + box.x * 0.5f, pos.y + box.y * 0.5f), iconSize * 0.78f, tint);
  if (tooltip && hovered) ImGui::SetTooltip("%s", tooltip);
  return clicked;
}

void inlineIcon(Icon icon, ImU32 color, float scale) {
  const float size = ImGui::GetFontSize() * scale;
  ImVec2 pos = ImGui::GetCursorScreenPos();
  draw(ImGui::GetWindowDrawList(), icon,
       ImVec2(pos.x + size * 0.5f, pos.y + ImGui::GetTextLineHeight() * 0.5f), size,
       color ? color : theme::palette().textDim);
  ImGui::Dummy(ImVec2(size, ImGui::GetTextLineHeight()));
}

}  // namespace icons
