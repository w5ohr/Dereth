// Knowledge graph tab (#108): loads memories, notes and knowledge-base
// files from the server, builds the entity graph, lays it out with the
// force simulation and draws it as a node graph you can pan, drag, hover
// and click; a side list shows the clicked entity's sources or the
// clicked source's text; JSON export.
#include <cmath>

#include "app.h"
#include "core/async.h"
#include "core/common.h"
#include "core/kgraph.h"
#include "imgui_stdlib.h"
#include "pane_common.h"
#include "platform.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

std::vector<kgraph::Source> gSources;
kgraph::Graph gGraph;
kgraph::Options gOptions;
bool gLoading = false, gLoaded = false, gBuilt = false;
int gSteps = 0;          // layout steps left to run
std::string gSelected;   // node id
std::string gDragging;
bool gDragMoved = false;
std::string gFocus;      // search text
ImVec2 gPan(0, 0);
std::string gStatus;
int gCounts[3] = {0, 0, 0};   // memories, notes, knowledge files

void rebuild() {
  gGraph = kgraph::build(gSources, gOptions);
  kgraph::seedLayout(gGraph, 900.0f, 600.0f);
  gSteps = 400;
  gBuilt = true;
  gSelected.clear();
}

}  // namespace

void App::loadKnowledgeGraph() {
  if (gLoading) return;
  gLoading = true;
  gStatus = "Loading memories, notes and knowledge bases...";
  async::run([this] {
    std::vector<kgraph::Source> sources;
    int counts[3] = {0, 0, 0};
    api::Result mem = api_.memories();
    if (mem.ok && mem.data.is_array()) {
      for (const auto& m : mem.data) {
        const std::string content = m.value("content", "");
        if (content.empty()) continue;
        sources.push_back({"memory:" + m.value("id", std::to_string(sources.size())), "memory", content.substr(0, 48) + (content.size() > 48 ? "..." : ""), content});
        ++counts[0];
      }
    }
    api::Result notes = api_.notes();
    const Json noteList = notes.ok ? (notes.data.is_array() ? notes.data : notes.data.value("items", Json::array())) : Json::array();
    for (const auto& n : noteList) {
      std::string text;
      const Json data = n.value("data", Json::object());
      const Json content = data.value("content", Json());
      if (content.is_string()) text = content;
      else if (content.is_object()) text = content.value("md", content.value("html", ""));
      sources.push_back({"note:" + n.value("id", ""), "note", n.value("title", "Untitled"), text});
      ++counts[1];
    }
    api::Result kb = api_.knowledgeList();
    const Json kbList = kb.ok ? (kb.data.is_array() ? kb.data : kb.data.value("items", Json::array())) : Json::array();
    for (const auto& k : kbList) {
      const std::string kbName = k.value("name", "knowledge");
      const std::string kid = k.value("id", "");
      api::Result detail = api_.knowledge(kid);
      Json files = detail.ok ? detail.data.value("files", Json()) : k.value("files", Json());
      if (!files.is_array()) {   // Open WebUI 0.12 lists a base's files separately, paged
        api::Result paged = api_.request("GET", "/api/v1/knowledge/" + kid + "/files");
        files = paged.ok ? (paged.data.is_array() ? paged.data : paged.data.value("items", Json::array())) : Json::array();
      }
      for (const auto& f : files) {
        const std::string fid = f.value("id", "");
        if (fid.empty()) continue;
        api::Result file = api_.file(fid);
        std::string text, name = f.value("meta", Json::object()).value("name", f.value("filename", fid));
        if (file.ok) {
          text = file.data.value("data", Json::object()).value("content", "");
          name = file.data.value("filename", name);
        }
        if (text.empty()) text = name;
        sources.push_back({"knowledge:" + fid, "knowledge", kbName + " / " + name, text.substr(0, 20000)});
        ++counts[2];
      }
    }
    async::post([this, sources, counts] {
      gSources = sources;
      for (int i = 0; i < 3; ++i) gCounts[i] = counts[i];
      gLoading = false;
      gLoaded = true;
      gStatus = std::to_string(counts[0]) + " memories, " + std::to_string(counts[1]) + " notes, " + std::to_string(counts[2]) + " knowledge files";
      rebuild();
    });
  });
}

void App::openGraphTab() { pane = Pane::Tools; toolsTabRequest = "graph"; }

void App::drawGraphSection(float width) {
  const auto& palette = theme::palette();
  if (!gLoaded && !gLoading) loadKnowledgeGraph();
  ImGui::PushTextWrapPos(width - 20.0f);
  widgets::dimText("Names, terms, #tags and `code` pulled out of your memories, notes and knowledge-base files, linked to what mentions them and to each other when they share a source. Drag nodes, drag the background to pan, click a node for its sources or text.");
  ImGui::PopTextWrapPos();
  widgets::beginDisabled(gLoading);
  if (ImGui::Button(gLoading ? "Loading..." : "Reload")) loadKnowledgeGraph();
  widgets::endDisabled(gLoading);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(130);
  if (ImGui::SliderInt("Min mentions", &gOptions.minMentions, 1, 5) && gLoaded) rebuild();
  ImGui::SameLine();
  ImGui::SetNextItemWidth(130);
  if (ImGui::SliderInt("Max entities", &gOptions.maxEntities, 10, 200) && gLoaded) rebuild();
  ImGui::SameLine();
  if (ImGui::Checkbox("Links between entities", &gOptions.coOccurrence) && gLoaded) rebuild();
  ImGui::SameLine();
  if (ImGui::SmallButton("Shake")) {
    kgraph::seedLayout(gGraph, 900.0f, 600.0f);
    gSteps = 400;
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Export JSON...")) {
    const std::string path = platform::saveFile("knowledge-graph.json");
    if (!path.empty() && core::writeFile(path, kgraph::toJson(gGraph).dump(2))) widgets::toast(widgets::ToastLevel::Success, "Saved " + path);
  }
  ImGui::SetNextItemWidth(200);
  ImGui::InputTextWithHint("##gfocus", "Find a node", &gFocus);
  ImGui::SameLine();
  widgets::dimText("%s%s%s", gStatus.c_str(), gBuilt ? " - " : "", gBuilt ? kgraph::stats(gGraph).c_str() : "");

  const float sideW = 300.0f;
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const float canvasW = avail.x - sideW - 8.0f, canvasH = std::max(300.0f, avail.y);
  ImGui::BeginChild("##graphcanvas", ImVec2(canvasW, canvasH), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + canvasW, origin.y + canvasH), palette.bg);
    if (gBuilt && gSteps > 0) {
      for (int i = 0; i < 4 && gSteps > 0; ++i, --gSteps) {
        if (kgraph::layoutStep(gGraph, canvasW, canvasH, 1.0f) < 0.3f) gSteps = 0;
      }
    }
    ImGui::InvisibleButton("##gcanvas", ImVec2(canvasW, canvasH));
    const bool hoveredCanvas = ImGui::IsItemHovered();
    const ImVec2 mouse = ImGui::GetMousePos();
    auto screen = [&](const kgraph::Node& n) { return ImVec2(origin.x + n.x + gPan.x, origin.y + n.y + gPan.y); };
    auto radius = [](const kgraph::Node& n) { return n.kind == "entity" ? 6.0f + std::min(14.0f, 2.0f * std::sqrt(static_cast<float>(n.weight))) : 9.0f; };
    // Hit test.
    std::string hovered;
    float best = 1e9f;
    for (const kgraph::Node& n : gGraph.nodes) {
      const ImVec2 p = screen(n);
      const float dx = mouse.x - p.x, dy = mouse.y - p.y, d = dx * dx + dy * dy, r = radius(n) + 4.0f;
      if (d < r * r && d < best) {
        best = d;
        hovered = n.id;
      }
    }
    if (ImGui::IsItemActivated()) {
      gDragging = hovered;
      gDragMoved = false;
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f)) {
      gDragMoved = true;
      const ImVec2 delta = ImGui::GetIO().MouseDelta;
      if (!gDragging.empty()) {
        const int i = kgraph::indexOf(gGraph, gDragging);
        if (i >= 0) {
          gGraph.nodes[static_cast<size_t>(i)].x += delta.x;
          gGraph.nodes[static_cast<size_t>(i)].y += delta.y;
          gGraph.nodes[static_cast<size_t>(i)].pinned = true;
          gSteps = std::max(gSteps, 60);
        }
      } else {
        gPan.x += delta.x;
        gPan.y += delta.y;
      }
    }
    if (ImGui::IsItemDeactivated()) {
      if (!gDragMoved) gSelected = gDragging.empty() ? "" : (gSelected == gDragging ? "" : gDragging);
      gDragging.clear();
    }
    // Highlight set: the selected node and its neighbours; the focus text.
    std::vector<std::string> lit = gSelected.empty() ? std::vector<std::string>{} : kgraph::neighbours(gGraph, gSelected);
    if (!gSelected.empty()) lit.push_back(gSelected);
    auto isLit = [&](const std::string& id) { return std::find(lit.begin(), lit.end(), id) != lit.end(); };
    const std::string focus = core::lower(core::trim(gFocus));
    dl->PushClipRect(origin, ImVec2(origin.x + canvasW, origin.y + canvasH), true);
    for (const kgraph::Edge& e : gGraph.edges) {
      const int a = kgraph::indexOf(gGraph, e.a), b = kgraph::indexOf(gGraph, e.b);
      if (a < 0 || b < 0) continue;
      const bool hot = !gSelected.empty() && (e.a == gSelected || e.b == gSelected);
      ImU32 col = e.coOccurrence ? palette.border : palette.textFaint;
      if (hot) col = palette.accent;
      else if (!gSelected.empty()) col = (col & 0x00ffffff) | 0x30000000;
      dl->AddLine(screen(gGraph.nodes[static_cast<size_t>(a)]), screen(gGraph.nodes[static_cast<size_t>(b)]), col, hot ? 2.0f : (e.coOccurrence ? 1.0f : 1.2f));
    }
    for (const kgraph::Node& n : gGraph.nodes) {
      const ImVec2 p = screen(n);
      const float r = radius(n);
      const bool entity = n.kind == "entity";
      ImU32 fill = entity ? palette.accent : n.kind == "memory" ? palette.success : n.kind == "note" ? palette.warning : palette.danger;
      const bool matched = !focus.empty() && core::lower(n.label).find(focus) != std::string::npos;
      const bool dim = (!gSelected.empty() && !isLit(n.id)) || (!focus.empty() && !matched);
      if (dim) fill = (fill & 0x00ffffff) | 0x50000000;
      if (entity) dl->AddCircleFilled(p, r, fill);
      else dl->AddRectFilled(ImVec2(p.x - r, p.y - r), ImVec2(p.x + r, p.y + r), fill, 3.0f);
      if (n.id == gSelected || n.id == hovered || matched) dl->AddCircle(p, r + 3.0f, palette.text, 0, 1.5f);
      if (!dim || n.id == hovered) {
        const ImVec2 ts = ImGui::CalcTextSize(n.label.c_str());
        dl->AddRectFilled(ImVec2(p.x + r + 2.0f, p.y - ts.y * 0.5f - 1.0f), ImVec2(p.x + r + ts.x + 6.0f, p.y + ts.y * 0.5f + 1.0f), (palette.bg & 0x00ffffff) | 0xb0000000, 3.0f);
        dl->AddText(ImVec2(p.x + r + 4.0f, p.y - ts.y * 0.5f), dim ? palette.textDim : palette.text, n.label.c_str());
      }
    }
    dl->PopClipRect();
    if (!hovered.empty() && hoveredCanvas) {
      const kgraph::Node& n = gGraph.nodes[static_cast<size_t>(kgraph::indexOf(gGraph, hovered))];
      ImGui::BeginTooltip();
      theme::pushBold();
      ImGui::TextUnformatted(n.label.c_str());
      theme::pop();
      if (n.kind == "entity") widgets::dimText("%d mention%s in %d source%s", n.weight, n.weight == 1 ? "" : "s", static_cast<int>(n.sources.size()), n.sources.size() == 1 ? "" : "s");
      else widgets::dimText("%s - %d entit%s", n.kind.c_str(), n.weight, n.weight == 1 ? "y" : "ies");
      ImGui::EndTooltip();
    }
    // Legend.
    const ImVec2 lg(origin.x + 10.0f, origin.y + canvasH - 22.0f);
    dl->AddCircleFilled(ImVec2(lg.x + 5, lg.y + 7), 5.0f, palette.accent);
    dl->AddText(ImVec2(lg.x + 14, lg.y), palette.textDim, "entity");
    dl->AddRectFilled(ImVec2(lg.x + 66, lg.y + 2), ImVec2(lg.x + 76, lg.y + 12), palette.success, 2.0f);
    dl->AddText(ImVec2(lg.x + 80, lg.y), palette.textDim, "memory");
    dl->AddRectFilled(ImVec2(lg.x + 142, lg.y + 2), ImVec2(lg.x + 152, lg.y + 12), palette.warning, 2.0f);
    dl->AddText(ImVec2(lg.x + 156, lg.y), palette.textDim, "note");
    dl->AddRectFilled(ImVec2(lg.x + 196, lg.y + 2), ImVec2(lg.x + 206, lg.y + 12), palette.danger, 2.0f);
    dl->AddText(ImVec2(lg.x + 210, lg.y), palette.textDim, "knowledge file");
    if (gBuilt && gGraph.nodes.empty()) dl->AddText(ImVec2(origin.x + 20, origin.y + 20), palette.textDim, "Nothing to draw - add memories, notes or knowledge files on the server.");
  }
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("##graphside", ImVec2(sideW, canvasH), ImGuiChildFlags_Borders);
  {
    const int i = kgraph::indexOf(gGraph, gSelected);
    if (i < 0) {
      widgets::dimText("Click a node.");
      if (gBuilt) {
        ImGui::Separator();
        widgets::dimText("Most mentioned");
        std::vector<const kgraph::Node*> top;
        for (const kgraph::Node& n : gGraph.nodes) {
          if (n.kind == "entity") top.push_back(&n);
        }
        std::sort(top.begin(), top.end(), [](const kgraph::Node* a, const kgraph::Node* b) { return a->weight > b->weight; });
        for (size_t k = 0; k < std::min<size_t>(12, top.size()); ++k) {
          ImGui::PushID(static_cast<int>(k));
          if (ImGui::Selectable((top[k]->label + "  (" + std::to_string(top[k]->weight) + ")").c_str())) gSelected = top[k]->id;
          ImGui::PopID();
        }
      }
    } else {
      const kgraph::Node& n = gGraph.nodes[static_cast<size_t>(i)];
      theme::pushBold();
      ImGui::TextWrapped("%s", n.label.c_str());
      theme::pop();
      if (n.kind == "entity") {
        widgets::dimText("%d mention%s - mentioned by:", n.weight, n.weight == 1 ? "" : "s");
        for (const std::string& sid : n.sources) {
          const int si = kgraph::indexOf(gGraph, sid);
          if (si < 0) continue;
          const kgraph::Node& s = gGraph.nodes[static_cast<size_t>(si)];
          ImGui::PushID(sid.c_str());
          if (ImGui::Selectable((s.kind + ": " + s.label).c_str())) gSelected = sid;
          ImGui::PopID();
        }
        ImGui::Separator();
        widgets::dimText("Shares a source with:");
        for (const std::string& nid : kgraph::neighbours(gGraph, gSelected)) {
          if (nid.rfind("entity:", 0) != 0) continue;
          const int ni = kgraph::indexOf(gGraph, nid);
          if (ni < 0) continue;
          ImGui::PushID(nid.c_str());
          if (ImGui::Selectable(gGraph.nodes[static_cast<size_t>(ni)].label.c_str())) gSelected = nid;
          ImGui::PopID();
        }
      } else {
        widgets::dimText("%s - %d entities", n.kind.c_str(), n.weight);
        for (const kgraph::Source& s : gSources) {
          if (s.id != n.id) continue;
          ImGui::PushTextWrapPos(sideW - 12.0f);
          ImGui::TextUnformatted(s.text.substr(0, 1500).c_str());
          ImGui::PopTextWrapPos();
        }
        ImGui::Separator();
        widgets::dimText("Mentions:");
        for (const std::string& nid : kgraph::neighbours(gGraph, gSelected)) {
          const int ni = kgraph::indexOf(gGraph, nid);
          if (ni < 0) continue;
          ImGui::PushID(nid.c_str());
          if (ImGui::Selectable(gGraph.nodes[static_cast<size_t>(ni)].label.c_str())) gSelected = nid;
          ImGui::PopID();
        }
      }
    }
  }
  ImGui::EndChild();
}

}  // namespace app
