// Layout presets (#69): a named arrangement of the chat screen - sidebar
// and its width, which side column shows and whether it sits beside or
// below the transcript, reading mode, float-on-top and a window size.
// Built-ins plus the user's saved layouts in extra["layouts"]. App-free;
// --layouttest covers it.
#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace layouts {

using Json = nlohmann::json;

struct Layout {
  std::string name;
  bool sidebar = true;
  float sidebarWidth = 280.0f;     // 200 .. 480
  std::string column = "none";     // none | reference | pins | tree | artifacts
  bool columnBelow = false;        // the column under the transcript (horizontal split) instead of beside it
  bool reading = false;            // reading mode on
  bool floating = false;           // window floats above other apps
  float windowW = 0.0f, windowH = 0.0f;   // 0 = leave the window alone
  std::string referenceChatId;     // the reference chat to show, when the column is "reference"
  bool builtin = false;
};

const std::vector<std::string>& columnKinds();
bool validColumn(const std::string& kind);
std::vector<Layout> builtins();                 // Zen, Wide chat, Three columns, Split, Review
Json toJson(const Layout& l);
Layout fromJson(const Json& j);
std::vector<Layout> saved(const Json& extra);   // extra["layouts"]
Json toSaved(const std::vector<Layout>& list);
// Built-ins first, then saved ones; a saved layout with a built-in's name replaces it.
std::vector<Layout> all(const Json& extra);
int indexOf(const std::vector<Layout>& list, const std::string& name);   // -1 when absent
std::string summary(const Layout& l);           // "sidebar 280 - reference beside - reading"

}  // namespace layouts
