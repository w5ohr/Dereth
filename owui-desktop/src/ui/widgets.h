// Small reusable UI pieces: toasts, spinners, pills, headers, confirm modals.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "imgui.h"

namespace widgets {

enum class ToastLevel { Info, Success, Warning, Error };

void toast(ToastLevel level, const std::string& text);
void toastInfo(const std::string& text);
bool toastsVisible();   // a toast is on screen (an animation that keeps the frame rate up)
void toastError(const std::string& text);
void renderToasts();

void spinner(const char* id, float radius, float thickness, ImU32 color = 0);
void loadingRow(const char* text);

void pill(const std::string& text, ImU32 bg, ImU32 fg = 0);
void sectionHeader(const char* text);
void dimText(const char* fmt, ...);
void centeredText(const char* text);
bool linkText(const char* text);

// A full-width button styled as the primary action.
bool primaryButton(const char* label, ImVec2 size = ImVec2(0, 0));
bool ghostButton(const char* label, ImVec2 size = ImVec2(0, 0));
bool dangerButton(const char* label, ImVec2 size = ImVec2(0, 0));

// Search field with a magnifier and placeholder.
bool searchField(const char* id, std::string* value, const char* placeholder, float width = 0.0f);

// Confirm dialog driven by a bool flag the caller owns.
bool confirmModal(const char* id, const char* message, const char* confirmLabel = "Delete");

// Draws a rounded card background behind the next widgets.
struct Card {
  ImVec2 start;
  ImU32 color;
  float rounding;
  void begin(ImU32 bg = 0, float rounding = 10.0f);
  void end(float padding = 10.0f);
};

void beginDisabled(bool disabled);
void endDisabled(bool disabled);

std::string humanJson(const std::string& raw);

}  // namespace widgets
