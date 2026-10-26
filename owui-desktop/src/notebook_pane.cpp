// Notebook tab (#98): prompt, code and markdown cells over a persistent
// Python kernel; outputs inline (stdout, stderr, the last value, tracebacks,
// figures); prompt cells answered by the selected model with the notebook so
// far as context, and code in the answer one click from a new cell; saved
// as .ipynb under support/notebooks, importable and exportable.
#include <filesystem>
#include <memory>
#include <set>

#include "app.h"
#include "core/async.h"
#include "core/common.h"
#include "core/notebook.h"
#include "imgui_stdlib.h"
#include "pane_common.h"
#include "platform.h"
#include "ui/images.h"
#include "ui/markdown.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace fs = std::filesystem;

namespace app {
namespace {

notebook::Notebook gNb;
std::string gPath;
bool gLoaded = false;
std::shared_ptr<notebook::Kernel> gKernel;   // workers hold their own reference while a cell runs
bool gKernelStarting = false;
std::string gKernelError;
std::deque<std::string> gQueue;   // code cells waiting to run
std::string gRunning;             // the code cell running now
std::set<std::string> gAsking;    // prompt cells waiting for the model
std::set<std::string> gEditing;   // markdown cells in edit mode
std::string gFocusCell;           // a cell whose editor takes focus next frame
int gTimeoutSec = 120;

std::string notebooksDir() { return core::supportDir() + "/notebooks"; }

void save() {
  if (gPath.empty()) return;
  std::error_code ec;
  fs::create_directories(fs::path(gPath).parent_path(), ec);
  if (!gKernel || gKernel->version().empty()) {
  } else {
    gNb.pythonVersion = gKernel->version();
  }
  core::writeFile(gPath, notebook::toIpynb(gNb).dump(1));
}

int indexOf(const std::string& id) {
  for (size_t i = 0; i < gNb.cells.size(); ++i) {
    if (gNb.cells[i].id == id) return static_cast<int>(i);
  }
  return -1;
}

void newNotebook() {
  gNb = notebook::Notebook{};
  gNb.title = "Untitled";
  notebook::Cell first{notebook::newCellId(), "code", "", {}, 0, ""};
  gNb.cells.push_back(first);
  gPath = notebooksDir() + "/notebook-" + std::to_string(core::nowSeconds()) + ".ipynb";
  gQueue.clear();
  gFocusCell = first.id;
  save();
}

bool openNotebook(const std::string& path) {
  bool ok = false;
  const std::string raw = core::readFile(path, &ok);
  if (!ok) {
    widgets::toastError("Could not read " + path);
    return false;
  }
  std::string error;
  notebook::Notebook nb = notebook::fromIpynb(Json::parse(raw, nullptr, false), &error);
  if (!error.empty()) {
    widgets::toastError(core::baseName(path) + ": " + error);
    return false;
  }
  if (nb.title == "Untitled") nb.title = fs::path(path).stem().string();
  gNb = nb;
  gPath = path;
  gQueue.clear();
  return true;
}

std::vector<std::string> listNotebooks() {
  std::vector<std::pair<fs::file_time_type, std::string>> found;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(notebooksDir(), ec)) {
    if (e.path().extension() == ".ipynb") found.push_back({fs::last_write_time(e.path(), ec), e.path().string()});
  }
  std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
  std::vector<std::string> out;
  for (const auto& f : found) out.push_back(f.second);
  return out;
}

int charFilter(ImGuiInputTextCallbackData* data) {
  // Shift+Enter runs the cell: keep it from typing a newline.
  if (data->EventChar == '\n' && ImGui::GetIO().KeyShift) return 1;
  return 0;
}

}  // namespace

void App::ensureKernel() {
  if ((gKernel && gKernel->alive()) || gKernelStarting) return;
  gKernelStarting = true;
  gKernelError.clear();
  const std::string workdir = lspProjectFolder().empty() ? std::string(getenv("HOME") ? getenv("HOME") : "/tmp") : lspProjectFolder();
  async::run([workdir] {
    auto k = std::make_shared<notebook::Kernel>();
    std::string error;
    const bool ok = k->start(&error, workdir);
    async::post([k, ok, error] {
      gKernelStarting = false;
      if (!ok) {
        gKernelError = error;
        return;
      }
      gKernel = k;
    });
  });
}

void App::openNotebookTab() {
  pane = Pane::Tools;
  toolsTabRequest = "notebook";
}

void App::queueNotebookCell(const std::string& id) {
  const int i = indexOf(id);
  if (i < 0) return;
  notebook::Cell& c = gNb.cells[static_cast<size_t>(i)];
  if (c.kind == "markdown") {
    gEditing.erase(id);
    return;
  }
  if (c.kind == "prompt") {
    askNotebookPrompt(id);
    return;
  }
  if (std::find(gQueue.begin(), gQueue.end(), id) == gQueue.end() && gRunning != id) gQueue.push_back(id);
  ensureKernel();
}

void App::askNotebookPrompt(const std::string& id) {
  const int i = indexOf(id);
  if (i < 0 || gAsking.count(id)) return;
  notebook::Cell& c = gNb.cells[static_cast<size_t>(i)];
  if (core::trim(c.source).empty()) return;
  if (selectedModels.empty()) {
    widgets::toastInfo("Pick a model first");
    return;
  }
  const std::string model = selectedModels.front();
  const std::string context = notebook::promptContext(gNb, static_cast<size_t>(i));
  const std::string system =
      "You are helping inside a Python notebook. The user runs code cells in a persistent Python " + (gKernel ? gKernel->version() : std::string("3")) +
      " kernel (standard library only unless they import more). When code helps, give one ```python block that can run as the next cell and uses the variables already defined. The notebook so far:\n\n" +
      (context.empty() ? std::string("(empty)") : context);
  const Json payload{{"model", model}, {"stream", false}, {"messages", Json::array({Json{{"role", "system"}, {"content", system}}, Json{{"role", "user"}, {"content", c.source}}})}};
  gAsking.insert(id);
  c.outputs.clear();
  c.model = model;
  async::run([this, id, payload] {
    api::Result res = api_.completion(payload);
    std::string answer;
    if (res.ok) {
      const Json choices = res.data.value("choices", Json::array());
      if (!choices.empty()) answer = notebook::stripThinking(choices[0].value("message", Json::object()).value("content", ""));
    }
    const std::string error = res.ok ? "" : api::errorMessage(res);
    async::post([id, answer, error] {
      gAsking.erase(id);
      const int at = indexOf(id);
      if (at < 0) return;
      notebook::Cell& cell = gNb.cells[static_cast<size_t>(at)];
      if (!error.empty()) cell.outputs = {notebook::Output{"error", "", "ModelError: " + error, {}, ""}};
      else cell.outputs = {notebook::Output{"answer", "", answer, {}, ""}};
      save();
    });
  });
}

void App::tickNotebook() {
  if (!gRunning.empty() || gQueue.empty()) return;
  if (!gKernel || !gKernel->alive()) {
    ensureKernel();
    return;
  }
  const std::string id = gQueue.front();
  gQueue.pop_front();
  const int i = indexOf(id);
  if (i < 0 || gNb.cells[static_cast<size_t>(i)].kind != "code") return;
  gRunning = id;
  gNb.cells[static_cast<size_t>(i)].outputs.clear();
  const std::string code = gNb.cells[static_cast<size_t>(i)].source;
  std::shared_ptr<notebook::Kernel> kernel = gKernel;
  const int timeoutMs = gTimeoutSec * 1000;
  async::run([id, code, kernel, timeoutMs] {
    notebook::ExecResult r = kernel->execute(code, timeoutMs);
    async::post([id, r] {
      gRunning.clear();
      const int at = indexOf(id);
      if (at >= 0) {
        notebook::Cell& cell = gNb.cells[static_cast<size_t>(at)];
        cell.outputs = notebook::toOutputs(r);
        if (r.count > 0) cell.count = r.count;
      }
      if (!r.ename.empty() || !r.kernelError.empty() || r.timedOut) gQueue.clear();   // Run all stops at the first error
      save();
    });
  });
}

void App::drawNotebookSection(float width) {
  const auto& palette = theme::palette();
  if (!gLoaded) {
    gLoaded = true;
    const auto existing = listNotebooks();
    if (existing.empty() || !openNotebook(existing.front())) newNotebook();
  }
  tickNotebook();
  ImGuiIO& io = ImGui::GetIO();

  // Toolbar.
  ImGui::SetNextItemWidth(220);
  if (ImGui::InputTextWithHint("##nbtitle", "Title", &gNb.title, ImGuiInputTextFlags_EnterReturnsTrue) || ImGui::IsItemDeactivatedAfterEdit()) save();
  ImGui::SameLine();
  if (ImGui::Button("New")) newNotebook();
  ImGui::SameLine();
  if (ImGui::BeginCombo("##nbopen", "Open", ImGuiComboFlags_NoPreview)) {
    for (const std::string& p : listNotebooks()) {
      bool ok = false;
      const Json meta = Json::parse(core::readFile(p, &ok), nullptr, false);
      const std::string title = meta.is_object() ? meta.value("metadata", Json::object()).value("owui", Json::object()).value("title", fs::path(p).stem().string()) : fs::path(p).stem().string();
      if (ImGui::Selectable((title + "##" + p).c_str(), p == gPath)) openNotebook(p);
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  ImGui::TextUnformatted("Open");
  ImGui::SameLine();
  if (ImGui::Button("Import .ipynb...")) {
    const auto picked = platform::openFiles(false);
    if (!picked.empty()) {
      const std::string dest = notebooksDir() + "/" + fs::path(picked.front()).filename().string();
      std::error_code ec;
      fs::create_directories(notebooksDir(), ec);
      if (dest != picked.front()) fs::copy_file(picked.front(), dest, fs::copy_options::overwrite_existing, ec);
      if (openNotebook(dest)) widgets::toast(widgets::ToastLevel::Success, "Imported " + fs::path(dest).filename().string());
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Export .ipynb...")) {
    const std::string path = platform::saveFile((gNb.title.empty() ? std::string("notebook") : gNb.title) + ".ipynb");
    if (!path.empty() && core::writeFile(path, notebook::toIpynb(gNb).dump(1))) widgets::toast(widgets::ToastLevel::Success, "Exported " + path);
  }
  ImGui::SameLine(0, 18);
  if (widgets::primaryButton("Run all", ImVec2(80, 0))) {
    for (const notebook::Cell& c : gNb.cells) {
      if (c.kind == "code" && std::find(gQueue.begin(), gQueue.end(), c.id) == gQueue.end()) gQueue.push_back(c.id);
    }
    ensureKernel();
  }
  ImGui::SameLine();
  const bool busy = !gRunning.empty();
  widgets::beginDisabled(!busy);
  if (ImGui::Button("Interrupt") && gKernel) {
    gQueue.clear();
    gKernel->interrupt();
  }
  widgets::endDisabled(!busy);
  ImGui::SameLine();
  widgets::beginDisabled(gKernelStarting);
  if (ImGui::Button("Restart kernel")) {
    gQueue.clear();
    if (gKernel && !busy) {
      std::shared_ptr<notebook::Kernel> k = gKernel;
      gKernelStarting = true;
      async::run([k] {
        std::string error;
        const bool ok = k->restart(&error);
        async::post([ok, error] {
          gKernelStarting = false;
          gKernelError = ok ? "" : error;
          for (notebook::Cell& c : gNb.cells) c.count = 0;
          widgets::toastInfo(ok ? "Kernel restarted - variables cleared" : "Restart failed: " + error);
        });
      });
    } else if (!gKernel) {
      ensureKernel();
    }
  }
  widgets::endDisabled(gKernelStarting);
  ImGui::SameLine();
  const std::string state = gKernelStarting ? "starting" : !gKernel || !gKernel->alive() ? "stopped" : busy ? "busy" : "idle";
  ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(state == "idle" ? palette.success : state == "busy" ? palette.warning : palette.textDim));
  ImGui::Text("Python %s - %s", gKernel && !gKernel->version().empty() ? gKernel->version().c_str() : "", state.c_str());
  ImGui::PopStyleColor();
  if (!gKernelError.empty()) {
    ImGui::SameLine();
    ImGui::TextColored(theme::toVec4(palette.danger), "%s", gKernelError.c_str());
  }
  widgets::dimText("Shift+Enter runs a cell. Prompts go to %s with the notebook so far. %s", selectedModels.empty() ? "(no model selected)" : selectedModels.front().c_str(), gPath.c_str());

  // Cells.
  ImGui::BeginChild("##nbcells", ImVec2(width, std::max(300.0f, ImGui::GetContentRegionAvail().y)), ImGuiChildFlags_None);
  const float gutter = 64.0f;
  const float cellW = width - gutter - 20.0f;
  int moveFrom = -1, moveTo = -1, removeAt = -1, insertAt = -1;
  std::string insertKind, insertSource;
  for (size_t i = 0; i < gNb.cells.size(); ++i) {
    notebook::Cell& c = gNb.cells[i];
    ImGui::PushID(c.id.c_str());
    // Gutter.
    const bool queued = std::find(gQueue.begin(), gQueue.end(), c.id) != gQueue.end();
    std::string label = c.kind == "code" ? (gRunning == c.id ? "In [*]" : queued ? "In [ ]" : "In [" + (c.count > 0 ? std::to_string(c.count) : std::string(" ")) + "]") : c.kind == "prompt" ? (gAsking.count(c.id) ? "Ask..." : "Ask") : "Md";
    theme::pushMono();
    ImGui::TextColored(theme::toVec4(c.kind == "code" ? palette.accent : c.kind == "prompt" ? palette.warning : palette.textDim), "%s", label.c_str());
    theme::pop();
    ImGui::SameLine(gutter);
    ImGui::BeginGroup();
    // Editor (markdown cells render until edited).
    const bool renderMd = c.kind == "markdown" && !gEditing.count(c.id) && !core::trim(c.source).empty();
    if (renderMd) {
      md::Options options;
      options.wrapWidth = cellW;
      md::render(("md" + c.id).c_str(), c.source, options);
      if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) gEditing.insert(c.id);
    } else {
      if (c.kind == "code") theme::pushMono();
      int lines = 1;
      for (char ch : c.source) lines += ch == '\n';
      const float h = std::max(2, lines) * ImGui::GetTextLineHeight() + 12.0f;
      if (gFocusCell == c.id) {
        ImGui::SetKeyboardFocusHere();
        gFocusCell.clear();
      }
      const char* hint = c.kind == "prompt" ? "Ask the model about this notebook..." : c.kind == "markdown" ? "Markdown" : "";
      (void)hint;
      if (ImGui::InputTextMultiline("##src", &c.source, ImVec2(cellW, h), ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackCharFilter, charFilter)) {
      }
      const bool active = ImGui::IsItemActive();
      if (ImGui::IsItemDeactivatedAfterEdit()) save();
      if (c.kind == "code") theme::pop();
      if (active && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
        save();
        queueNotebookCell(c.id);
        if (i + 1 < gNb.cells.size()) gFocusCell = gNb.cells[i + 1].id;
      }
    }
    // Cell controls.
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 1));
    if (ImGui::SmallButton(c.kind == "markdown" ? (renderMd ? "Edit" : "Done") : c.kind == "prompt" ? "Ask" : "Run")) {
      if (c.kind == "markdown" && renderMd) gEditing.insert(c.id);
      else queueNotebookCell(c.id);
    }
    ImGui::SameLine();
    static const char* kinds[] = {"code", "prompt", "markdown"};
    int kind = c.kind == "prompt" ? 1 : c.kind == "markdown" ? 2 : 0;
    ImGui::SetNextItemWidth(100);
    if (ImGui::Combo("##kind", &kind, kinds, 3)) {
      c.kind = kinds[kind];
      c.outputs.clear();
      c.count = 0;
      if (c.kind == "markdown") gEditing.insert(c.id);
      save();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Up") && i > 0) moveFrom = static_cast<int>(i), moveTo = static_cast<int>(i) - 1;
    ImGui::SameLine();
    if (ImGui::SmallButton("Down") && i + 1 < gNb.cells.size()) moveFrom = static_cast<int>(i), moveTo = static_cast<int>(i) + 1;
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Code")) insertAt = static_cast<int>(i) + 1, insertKind = "code";
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Prompt")) insertAt = static_cast<int>(i) + 1, insertKind = "prompt";
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Text")) insertAt = static_cast<int>(i) + 1, insertKind = "markdown";
    ImGui::SameLine();
    if (ImGui::SmallButton("Delete")) removeAt = static_cast<int>(i);
    if (!c.outputs.empty()) {
      ImGui::SameLine();
      if (ImGui::SmallButton("Clear output")) {
        c.outputs.clear();
        save();
      }
    }
    ImGui::PopStyleVar();
    // Outputs.
    for (size_t k = 0; k < c.outputs.size(); ++k) {
      const notebook::Output& o = c.outputs[k];
      ImGui::PushID(static_cast<int>(k));
      if (o.type == "stream" || o.type == "result") {
        theme::pushMono();
        ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(o.name == "stderr" ? palette.warning : o.type == "result" ? palette.text : palette.textDim));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cellW);
        std::string text = o.text.size() > 20000 ? o.text.substr(0, 20000) + "\n... (output truncated)" : o.text;
        while (!text.empty() && text.back() == '\n') text.pop_back();
        ImGui::TextUnformatted(((o.type == "result" ? "Out: " : "") + text).c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        theme::pop();
      } else if (o.type == "error") {
        theme::pushMono();
        ImGui::TextColored(theme::toVec4(palette.danger), "%s", o.text.c_str());
        if (!o.traceback.empty() && ImGui::TreeNode("##tb", "Traceback (%d lines)", static_cast<int>(o.traceback.size()))) {
          ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cellW);
          for (const std::string& t : o.traceback) widgets::dimText("%s", t.c_str());
          ImGui::PopTextWrapPos();
          ImGui::TreePop();
        }
        theme::pop();
      } else if (o.type == "image") {
        const std::string png = o.png;
        images::Entry img = images::get("nbimg:" + c.id + ":" + std::to_string(k) + ":" + std::to_string(png.size()), [png] { return core::base64Decode(png); });
        if (img.ready()) {
          const float scale = std::min(1.0f, cellW / static_cast<float>(img.width));
          ImGui::Image(img.texture, ImVec2(img.width * scale, img.height * scale));
        } else {
          widgets::loadingRow("figure");
        }
      } else if (o.type == "answer") {
        md::Options options;
        options.wrapWidth = cellW;
        md::render(("ans" + c.id).c_str(), o.text, options);
        const std::string code = notebook::extractPython(o.text);
        if (!code.empty() && ImGui::SmallButton("Insert code below")) {
          insertAt = static_cast<int>(i) + 1;
          insertKind = "code";
          insertSource = code;
        }
        if (!c.model.empty()) {
          ImGui::SameLine();
          widgets::dimText("%s", c.model.c_str());
        }
      }
      ImGui::PopID();
    }
    if (c.kind == "prompt" && gAsking.count(c.id)) widgets::loadingRow("asking the model...");
    ImGui::EndGroup();
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::PopID();
  }
  if (ImGui::Button("+ Code cell")) insertAt = static_cast<int>(gNb.cells.size()), insertKind = "code";
  ImGui::SameLine();
  if (ImGui::Button("+ Prompt cell")) insertAt = static_cast<int>(gNb.cells.size()), insertKind = "prompt";
  ImGui::SameLine();
  if (ImGui::Button("+ Text cell")) insertAt = static_cast<int>(gNb.cells.size()), insertKind = "markdown";
  ImGui::EndChild();

  if (moveFrom >= 0) {
    std::swap(gNb.cells[static_cast<size_t>(moveFrom)], gNb.cells[static_cast<size_t>(moveTo)]);
    save();
  }
  if (removeAt >= 0) {
    gQueue.erase(std::remove(gQueue.begin(), gQueue.end(), gNb.cells[static_cast<size_t>(removeAt)].id), gQueue.end());
    gNb.cells.erase(gNb.cells.begin() + removeAt);
    if (gNb.cells.empty()) gNb.cells.push_back(notebook::Cell{notebook::newCellId(), "code", "", {}, 0, ""});
    save();
  }
  if (insertAt >= 0) {
    notebook::Cell cell{notebook::newCellId(), insertKind, insertSource, {}, 0, ""};
    gNb.cells.insert(gNb.cells.begin() + insertAt, cell);
    if (insertKind == "markdown") gEditing.insert(cell.id);
    gFocusCell = cell.id;
    save();
  }
}

}  // namespace app
