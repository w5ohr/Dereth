// Sketch input window (#120): a canvas to draw on with pen and eraser,
// colours, widths, undo / redo, clear; "Attach" rasterises the drawing
// (trimmed to what was drawn unless switched off), encodes a PNG and adds
// it to the composer as an image attachment for a vision model.
#include <cmath>

#include "app.h"
#include "core/common.h"
#include "core/sketch.h"
#include "imgui_stdlib.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

sketch::Doc gDoc;
sketch::Stroke gCurrent;
bool gDrawing = false;
int gColor = 0;
float gWidth = 4.0f;
bool gEraser = false;
bool gTrim = true;
float gScale = 1.5f;   // export resolution over the canvas size
std::string gAsk;

struct Swatch {
  const char* name;
  sketch::Rgb rgb;
};
const Swatch kSwatches[] = {{"Black", {20, 20, 24}}, {"Red", {220, 50, 47}}, {"Blue", {38, 110, 220}}, {"Green", {40, 160, 80}}, {"Orange", {240, 140, 20}}, {"Purple", {140, 70, 200}}, {"Grey", {130, 130, 140}}};

ImU32 toU32(sketch::Rgb c, float alpha = 1.0f) { return ImGui::ColorConvertFloat4ToU32(ImVec4(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, alpha)); }

void drawStroke(ImDrawList* dl, const sketch::Stroke& s, ImVec2 origin, float scale, sketch::Rgb background) {
  const ImU32 col = toU32(s.eraser ? background : s.color);
  const float thickness = std::max(1.0f, s.width * scale);
  auto at = [&](const sketch::Point& p) { return ImVec2(origin.x + p.x * scale, origin.y + p.y * scale); };
  if (s.points.size() == 1) {
    dl->AddCircleFilled(at(s.points[0]), thickness * 0.5f, col);
    return;
  }
  for (size_t i = 1; i < s.points.size(); ++i) {
    dl->AddLine(at(s.points[i - 1]), at(s.points[i]), col, thickness);
    dl->AddCircleFilled(at(s.points[i]), thickness * 0.5f, col);   // round joins and caps
  }
  dl->AddCircleFilled(at(s.points[0]), thickness * 0.5f, col);
}

}  // namespace

void App::openSketch() { showSketch = true; }

bool App::attachSketch() {
  if (sketch::empty(gDoc)) {
    widgets::toastInfo("Draw something first");
    return false;
  }
  sketch::Image img = sketch::rasterize(gDoc, gScale);
  if (gTrim) img = sketch::trim(img, gDoc.background, static_cast<int>(24 * gScale));
  const std::string png = sketch::encodePng(img);
  if (png.empty()) {
    widgets::toastError("Could not encode the sketch");
    return false;
  }
  attachBytes("sketch.png", "image/png", png);
  if (!core::trim(gAsk).empty() && core::trim(composer).empty()) composer = core::trim(gAsk);
  pane = Pane::Chat;
  focusComposer = true;
  widgets::toast(widgets::ToastLevel::Success, "Sketch attached (" + std::to_string(img.width) + "x" + std::to_string(img.height) + ", " + core::humanBytes(static_cast<int64_t>(png.size())) + ")");
  return true;
}

void App::drawSketchWindow() {
  if (!showSketch) return;
  const auto& palette = theme::palette();
  ImGui::OpenPopup("Sketch");
  ImGui::SetNextWindowSize(ImVec2(980, 740), ImGuiCond_Appearing);
  if (!ImGui::BeginPopupModal("Sketch", &showSketch)) return;
  ImGuiIO& io = ImGui::GetIO();
  if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    showSketch = false;
    ImGui::CloseCurrentPopup();
  }
  if (!gDrawing && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
    if (io.KeyShift) sketch::redo(gDoc);
    else sketch::undo(gDoc);
  }

  // Toolbar.
  for (int i = 0; i < IM_ARRAYSIZE(kSwatches); ++i) {
    ImGui::PushID(i);
    const bool on = !gEraser && gColor == i;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##swatch", ImVec2(26, 26))) {
      gColor = i;
      gEraser = false;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kSwatches[i].name);
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + 13, p.y + 13), 10.0f, toU32(kSwatches[i].rgb));
    if (on) ImGui::GetWindowDrawList()->AddCircle(ImVec2(p.x + 13, p.y + 13), 12.5f, palette.text, 0, 2.0f);
    ImGui::PopID();
    ImGui::SameLine(0, 4);
  }
  ImGui::SameLine(0, 12);
  if (ImGui::RadioButton("Pen", !gEraser)) gEraser = false;
  ImGui::SameLine();
  if (ImGui::RadioButton("Eraser", gEraser)) gEraser = true;
  ImGui::SameLine();
  ImGui::SetNextItemWidth(140);
  ImGui::SliderFloat("Width", &gWidth, 1.0f, 32.0f, "%.0f px");
  ImGui::SameLine();
  widgets::beginDisabled(gDoc.strokes.empty());
  if (ImGui::Button("Undo")) sketch::undo(gDoc);
  widgets::endDisabled(gDoc.strokes.empty());
  ImGui::SameLine();
  widgets::beginDisabled(gDoc.redo.empty());
  if (ImGui::Button("Redo")) sketch::redo(gDoc);
  widgets::endDisabled(gDoc.redo.empty());
  ImGui::SameLine();
  widgets::beginDisabled(gDoc.strokes.empty());
  if (ImGui::Button("Clear")) sketch::clear(gDoc);
  widgets::endDisabled(gDoc.strokes.empty());
  ImGui::SameLine();
  widgets::dimText("%s", sketch::summary(gDoc).c_str());

  // Canvas: the document fitted into the space left, drawn live.
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const float footer = 76.0f;
  const float scale = std::min(avail.x / gDoc.width, (avail.y - footer) / gDoc.height);
  const ImVec2 size(gDoc.width * scale, gDoc.height * scale);
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail.x - size.x) * 0.5f);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##canvas", size);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), toU32(gDoc.background), 4.0f);
  dl->AddRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), palette.border, 4.0f, 1.0f);
  const ImVec2 mouse = ImGui::GetMousePos();
  const sketch::Point here{std::clamp((mouse.x - origin.x) / scale, 0.0f, static_cast<float>(gDoc.width)), std::clamp((mouse.y - origin.y) / scale, 0.0f, static_cast<float>(gDoc.height))};
  if (ImGui::IsItemActivated()) {
    gDrawing = true;
    gCurrent = sketch::Stroke{};
    gCurrent.color = kSwatches[gColor].rgb;
    gCurrent.width = gWidth;
    gCurrent.eraser = gEraser;
    gCurrent.points.push_back(here);
  }
  if (gDrawing && ImGui::IsItemActive()) {
    const sketch::Point& last = gCurrent.points.back();
    if (std::hypot(here.x - last.x, here.y - last.y) >= 1.0f) gCurrent.points.push_back(here);
  }
  if (gDrawing && ImGui::IsItemDeactivated()) {
    gDrawing = false;
    sketch::add(gDoc, gCurrent);
    gCurrent = sketch::Stroke{};
  }
  dl->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
  for (const sketch::Stroke& s : gDoc.strokes) drawStroke(dl, s, origin, scale, gDoc.background);
  if (gDrawing) drawStroke(dl, gCurrent, origin, scale, gDoc.background);
  if (ImGui::IsItemHovered()) dl->AddCircle(mouse, std::max(2.0f, gWidth * scale * 0.5f), gEraser ? palette.textDim : toU32(kSwatches[gColor].rgb, 0.8f), 0, 1.0f);
  dl->PopClipRect();
  if (gDoc.strokes.empty() && !gDrawing) {
    const char* hint = "Draw here - a diagram, an equation, a UI mock-up. Cmd+Z undoes.";
    const ImVec2 ts = ImGui::CalcTextSize(hint);
    dl->AddText(ImVec2(origin.x + (size.x - ts.x) * 0.5f, origin.y + (size.y - ts.y) * 0.5f), IM_COL32(150, 150, 160, 255), hint);
  }

  // Footer.
  ImGui::SetNextItemWidth(avail.x - 360.0f);
  ImGui::InputTextWithHint("##sketchask", "Question to send with it (optional, e.g. \"Turn this into a Mermaid diagram\")", &gAsk);
  ImGui::SameLine();
  ImGui::Checkbox("Trim to the drawing", &gTrim);
  ImGui::SameLine();
  widgets::beginDisabled(sketch::empty(gDoc));
  if (widgets::primaryButton("Attach", ImVec2(110, 0)) && attachSketch()) {
    showSketch = false;
    ImGui::CloseCurrentPopup();
  }
  widgets::endDisabled(sketch::empty(gDoc));
  widgets::dimText("Attaches a PNG (%d x %d before trimming) to the message; pick a vision model to send it to. The drawing stays here until you clear it.", static_cast<int>(gDoc.width * gScale), static_cast<int>(gDoc.height * gScale));
  ImGui::EndPopup();
}

}  // namespace app
