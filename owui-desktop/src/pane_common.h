// Shared scaffolding for the non-chat panes: a title bar, lazy loading and a
// two-column list/detail layout.
#pragma once

#include <functional>
#include <string>

#include "app.h"
#include "imgui.h"
#include "imgui_stdlib.h"

namespace app {

// Header with a title, optional subtitle and a right-aligned action area.
void paneHeader(const char* title, const char* subtitle,
                const std::function<void()>& actions = nullptr);

// Begins the scrolling body of a pane. Pair with paneEnd().
void paneBody(const char* id, float width = 0.0f);
void paneEnd();

// Draws a bordered card around the widgets emitted by `body`.
void card(const char* id, const std::function<void()>& body, float width = 0.0f);

// Label: value row used by the detail views.
void field(const char* label, const std::string& value);

void emptyState(const char* message);

}  // namespace app
