// Vector icons drawn with ImDrawList — no icon font to ship or fall back on.
#pragma once

#include "imgui.h"

namespace icons {

enum class Icon {
  Plus, Send, Stop, Copy, Trash, Pencil, Refresh, Chevron, ChevronRight, Search,
  Paperclip, Mic, Image, Pin, Star, Gear, User, Folder, Chat, Archive, Share,
  ThumbUp, ThumbDown, Globe, Code, Book, Sparkle, Check, Close, Menu, Download,
  Speaker, Branch, Info, Warning, Play, Note, Channel, Wrench, Brain, Layers,
};

void draw(ImDrawList* drawList, Icon icon, ImVec2 center, float size, ImU32 color,
          float thickness = 1.6f);

// An icon-only button. Returns true when clicked.
bool button(const char* id, Icon icon, const char* tooltip = nullptr, float size = 0.0f,
            ImU32 color = 0);

// Draws an icon inline with text flow and advances the cursor.
void inlineIcon(Icon icon, ImU32 color = 0, float scale = 1.0f);

}  // namespace icons
