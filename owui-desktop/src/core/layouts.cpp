#include "layouts.h"

#include <algorithm>

namespace layouts {

const std::vector<std::string>& columnKinds() {
  static const std::vector<std::string> kinds{"none", "reference", "pins", "tree", "artifacts"};
  return kinds;
}

bool validColumn(const std::string& kind) { return std::find(columnKinds().begin(), columnKinds().end(), kind) != columnKinds().end(); }

std::vector<Layout> builtins() {
  std::vector<Layout> out;
  Layout zen;
  zen.name = "Zen";
  zen.sidebar = false;
  zen.reading = true;
  out.push_back(zen);
  Layout wide;
  wide.name = "Wide chat";
  out.push_back(wide);
  Layout three;
  three.name = "Three columns";
  three.column = "reference";
  out.push_back(three);
  Layout split;
  split.name = "Split";
  split.column = "reference";
  split.columnBelow = true;
  out.push_back(split);
  Layout review;
  review.name = "Review";
  review.column = "pins";
  review.sidebarWidth = 240.0f;
  out.push_back(review);
  for (Layout& l : out) l.builtin = true;
  return out;
}

Json toJson(const Layout& l) {
  return Json{{"name", l.name},       {"sidebar", l.sidebar},   {"sidebarWidth", l.sidebarWidth}, {"column", l.column},   {"columnBelow", l.columnBelow},
              {"reading", l.reading}, {"floating", l.floating}, {"windowW", l.windowW},           {"windowH", l.windowH}, {"referenceChatId", l.referenceChatId}};
}

Layout fromJson(const Json& j) {
  Layout l;
  if (!j.is_object()) return l;
  l.name = j.value("name", "");
  l.sidebar = j.value("sidebar", true);
  l.sidebarWidth = std::clamp(j.value("sidebarWidth", 280.0f), 200.0f, 480.0f);
  const std::string column = j.value("column", "none");
  l.column = validColumn(column) ? column : "none";
  l.columnBelow = j.value("columnBelow", false);
  l.reading = j.value("reading", false);
  l.floating = j.value("floating", false);
  l.windowW = std::max(0.0f, j.value("windowW", 0.0f));
  l.windowH = std::max(0.0f, j.value("windowH", 0.0f));
  if (l.windowW > 0.0f && l.windowW < 480.0f) l.windowW = 480.0f;
  if (l.windowH > 0.0f && l.windowH < 360.0f) l.windowH = 360.0f;
  l.referenceChatId = j.value("referenceChatId", "");
  return l;
}

std::vector<Layout> saved(const Json& extra) {
  std::vector<Layout> out;
  const Json list = extra.is_object() ? extra.value("layouts", Json::array()) : Json::array();
  if (!list.is_array()) return out;
  for (const auto& j : list) {
    Layout l = fromJson(j);
    if (l.name.empty()) continue;
    if (indexOf(out, l.name) >= 0) continue;
    out.push_back(l);
  }
  return out;
}

Json toSaved(const std::vector<Layout>& list) {
  Json out = Json::array();
  for (const Layout& l : list) {
    if (!l.name.empty()) out.push_back(toJson(l));
  }
  return out;
}

std::vector<Layout> all(const Json& extra) {
  std::vector<Layout> out = builtins();
  for (const Layout& l : saved(extra)) {
    const int at = indexOf(out, l.name);
    if (at >= 0) out[static_cast<size_t>(at)] = l;
    else out.push_back(l);
  }
  return out;
}

int indexOf(const std::vector<Layout>& list, const std::string& name) {
  for (size_t i = 0; i < list.size(); ++i) {
    if (list[i].name == name) return static_cast<int>(i);
  }
  return -1;
}

std::string summary(const Layout& l) {
  std::string out = l.sidebar ? "sidebar " + std::to_string(static_cast<int>(l.sidebarWidth)) : "no sidebar";
  if (l.column != "none") out += " - " + l.column + (l.columnBelow ? " below" : " beside");
  if (l.reading) out += " - reading";
  if (l.floating) out += " - floating";
  if (l.windowW > 0.0f && l.windowH > 0.0f) out += " - " + std::to_string(static_cast<int>(l.windowW)) + "x" + std::to_string(static_cast<int>(l.windowH));
  return out;
}

}  // namespace layouts
