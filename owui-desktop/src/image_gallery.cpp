// Image gallery (#117): every generated image lands in support/images with
// its prompt, negative prompt, model, size, seed and engine written into the
// PNG, listed in an index with favourites; a grid with search, favourites,
// reuse-prompt, open / reveal / delete; a rescan that picks up PNGs dropped
// into the folder (A1111 / ComfyUI metadata read from them).
#include <filesystem>

#include "app.h"
#include "core/async.h"
#include "core/common.h"
#include "core/imagemeta.h"
#include "imgui_stdlib.h"
#include "pane_common.h"
#include "platform.h"
#include "ui/images.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace fs = std::filesystem;

namespace app {
namespace {

std::vector<imagemeta::Item> gItems;
bool gLoaded = false;
std::string gQuery;
bool gFavouritesOnly = false;
float gTile = 180.0f;
std::string gSelected;   // file name
int gSaveCounter = 0;

std::string galleryDir() { return core::supportDir() + "/images"; }
std::string indexPath() { return galleryDir() + "/index.json"; }

void load() {
  gLoaded = true;
  bool ok = false;
  const std::string raw = core::readFile(indexPath(), &ok);
  gItems = ok ? imagemeta::itemsFromJson(Json::parse(raw, nullptr, false)) : std::vector<imagemeta::Item>{};
}

void save() {
  std::error_code ec;
  fs::create_directories(galleryDir(), ec);
  core::writeFile(indexPath(), imagemeta::itemsToJson(gItems).dump());
}

imagemeta::Item* find(const std::string& file) {
  for (imagemeta::Item& it : gItems) {
    if (it.file == file) return &it;
  }
  return nullptr;
}

}  // namespace

void App::galleryAdd(const std::string& bytes, imagemeta::Meta meta) {
  if (!gLoaded) load();
  std::error_code ec;
  fs::create_directories(galleryDir(), ec);
  const int64_t now = core::nowSeconds();
  if (meta.created.empty()) meta.created = core::formatTime(now);
  imagemeta::Item item;
  item.created = now;
  item.meta = meta;
  const bool png = imagemeta::isPng(bytes);
  item.file = imagemeta::fileNameFor(now, ++gSaveCounter, png ? "png" : "jpg");
  std::string out = png ? imagemeta::writeText(bytes, imagemeta::toText(meta)) : bytes;
  if (out.empty()) out = bytes;
  if (png) imagemeta::pngSize(out, &item.width, &item.height);
  if (!core::writeFile(galleryDir() + "/" + item.file, out)) {
    widgets::toastError("Could not save the image to the gallery");
    return;
  }
  gItems.push_back(item);
  save();
}

int App::galleryRescan() {
  if (!gLoaded) load();
  std::error_code ec;
  fs::create_directories(galleryDir(), ec);
  int added = 0;
  for (const auto& entry : fs::directory_iterator(galleryDir(), ec)) {
    if (!entry.is_regular_file(ec)) continue;
    const std::string name = entry.path().filename().string();
    const std::string ext = core::lower(entry.path().extension().string());
    if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".webp") continue;
    if (find(name)) continue;
    imagemeta::Item item;
    item.file = name;
    item.created = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::seconds>(fs::last_write_time(entry.path(), ec).time_since_epoch()).count());
    if (item.created <= 0) item.created = core::nowSeconds();
    bool ok = false;
    const std::string bytes = core::readFile(entry.path().string(), &ok);
    if (ok && imagemeta::isPng(bytes)) {
      item.meta = imagemeta::read(bytes);
      imagemeta::pngSize(bytes, &item.width, &item.height);
    }
    gItems.push_back(item);
    ++added;
  }
  // Files that went away.
  gItems.erase(std::remove_if(gItems.begin(), gItems.end(), [&](const imagemeta::Item& it) { return !fs::exists(galleryDir() + "/" + it.file, ec); }), gItems.end());
  save();
  return added;
}

void App::drawImageGallery(float width) {
  if (!gLoaded) {
    load();
    galleryRescan();
  }
  const auto& palette = theme::palette();
  widgets::sectionHeader(("GALLERY (" + std::to_string(gItems.size()) + ")").c_str());
  card("gallery", [&] {
    ImGui::SetNextItemWidth(260);
    ImGui::InputTextWithHint("##gq", "Search prompts, models, seeds", &gQuery);
    ImGui::SameLine();
    ImGui::Checkbox("Favourites only", &gFavouritesOnly);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::SliderFloat("##tile", &gTile, 110.0f, 320.0f, "%.0f px");
    ImGui::SameLine();
    if (ImGui::SmallButton("Rescan folder")) {
      const int n = galleryRescan();
      widgets::toastInfo(n == 0 ? "No new images in the gallery folder" : "Added " + std::to_string(n) + (n == 1 ? " image" : " images") + " from the folder");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Open folder")) platform::revealInFinder(galleryDir());
    ImGui::SameLine();
    widgets::dimText("%s", galleryDir().c_str());
    ImGui::PushTextWrapPos(width - 20.0f);
    widgets::dimText("Every generated image is saved here with its prompt, negative prompt, model, size, seed (when the backend reports one) and engine written into the PNG. Drop PNGs from Automatic1111 or ComfyUI into the folder and Rescan reads theirs.");
    ImGui::PopTextWrapPos();

    const std::vector<imagemeta::Item> shown = imagemeta::filter(gItems, gQuery, gFavouritesOnly);
    if (shown.empty()) widgets::dimText(gItems.empty() ? "Nothing generated yet." : "No image matches.");
    const float tile = gTile;
    const int perRow = std::max(1, static_cast<int>((width - 30.0f) / (tile + 12.0f)));
    int col = 0;
    std::string toDelete;
    for (const imagemeta::Item& it : shown) {
      const std::string path = galleryDir() + "/" + it.file;
      ImGui::PushID(it.file.c_str());
      ImGui::BeginGroup();
      images::Entry entry = images::get("file://" + path, [path] { return core::readFile(path); });
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      const bool selected = gSelected == it.file;
      ImGui::GetWindowDrawList()->AddRectFilled(origin, ImVec2(origin.x + tile, origin.y + tile), palette.panelAlt, 6.0f);
      if (selected) ImGui::GetWindowDrawList()->AddRect(origin, ImVec2(origin.x + tile, origin.y + tile), palette.accent, 6.0f, 2.0f);
      if (entry.ready() && entry.width > 0 && entry.height > 0) {
        const float scale = std::min(tile / static_cast<float>(entry.width), tile / static_cast<float>(entry.height));
        const ImVec2 size(entry.width * scale, entry.height * scale);
        ImGui::SetCursorScreenPos(ImVec2(origin.x + (tile - size.x) * 0.5f, origin.y + (tile - size.y) * 0.5f));
        ImGui::Image(entry.texture, size);
      }
      ImGui::SetCursorScreenPos(origin);
      if (ImGui::InvisibleButton("##tile", ImVec2(tile, tile))) gSelected = selected ? "" : it.file;
      if (ImGui::IsItemHovered() && !it.meta.prompt.empty()) ImGui::SetTooltip("%s", it.meta.prompt.substr(0, 300).c_str());
      if (it.favourite) ImGui::GetWindowDrawList()->AddText(ImVec2(origin.x + 6.0f, origin.y + 4.0f), palette.warning, "\xe2\x98\x85");
      // Row of small controls under the tile.
      ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + tile + 2.0f));
      if (ImGui::SmallButton(it.favourite ? "\xe2\x98\x85" : "\xe2\x98\x86")) {
        if (imagemeta::Item* real = find(it.file)) real->favourite = !real->favourite;
        save();
      }
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", it.favourite ? "Remove from favourites" : "Favourite");
      ImGui::SameLine();
      widgets::beginDisabled(imagemeta::empty(it.meta));
      if (ImGui::SmallButton("Reuse")) imageReusePrompt(it.meta);
      widgets::endDisabled(imagemeta::empty(it.meta));
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Put this prompt, negative prompt, model and size back into the generator");
      ImGui::SameLine();
      if (ImGui::SmallButton("Open")) platform::openUrl("file://" + path);
      ImGui::SameLine();
      if (ImGui::SmallButton("x")) toDelete = it.file;
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Delete this image and its entry");
      ImGui::EndGroup();
      ImGui::PopID();
      if (++col < perRow) ImGui::SameLine(0.0f, 12.0f);
      else col = 0;
    }
    if (col != 0) ImGui::NewLine();
    if (!toDelete.empty()) {
      std::error_code ec;
      fs::remove(galleryDir() + "/" + toDelete, ec);
      images::invalidate("file://" + galleryDir() + "/" + toDelete);
      gItems.erase(std::remove_if(gItems.begin(), gItems.end(), [&](const imagemeta::Item& it) { return it.file == toDelete; }), gItems.end());
      if (gSelected == toDelete) gSelected.clear();
      save();
      widgets::toastInfo("Deleted " + toDelete);
    }

    if (const imagemeta::Item* sel = find(gSelected)) {
      ImGui::Separator();
      theme::pushBold();
      ImGui::TextUnformatted(sel->file.c_str());
      theme::pop();
      ImGui::SameLine();
      widgets::dimText("%s%s", core::formatTime(sel->created).c_str(), sel->width > 0 ? ("  " + std::to_string(sel->width) + "x" + std::to_string(sel->height)).c_str() : "");
      ImGui::SameLine();
      if (ImGui::SmallButton("Reveal in Finder")) platform::revealInFinder(galleryDir() + "/" + sel->file);
      ImGui::SameLine();
      if (ImGui::SmallButton("Copy parameters")) {
        platform::setClipboardText(imagemeta::parametersLine(sel->meta));
        widgets::toast(widgets::ToastLevel::Success, "Copied");
      }
      field("Prompt", sel->meta.prompt.empty() ? "(none recorded)" : sel->meta.prompt);
      if (!sel->meta.negative.empty()) field("Negative", sel->meta.negative);
      field("Model", sel->meta.model.empty() ? "(unknown)" : sel->meta.model);
      field("Size", sel->meta.size.empty() ? "(unknown)" : sel->meta.size);
      field("Seed", sel->meta.seed.empty() ? "(not reported by the backend)" : sel->meta.seed);
      field("Engine", sel->meta.engine.empty() ? "(unknown)" : sel->meta.engine);
    }
  }, width);
}

}  // namespace app
