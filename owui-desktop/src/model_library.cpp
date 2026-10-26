// Model library (Tools > Models): search the Ollama library or Hugging Face
// GGUF repositories, see each model's variants with sizes, queue pulls that
// run one after another through the server's Ollama passthrough with
// progress and cancel, and a free-space check before anything is queued.
#include <sys/statvfs.h>

#include <atomic>
#include <deque>
#include <map>
#include <memory>

#include "app.h"
#include "core/async.h"
#include "core/common.h"
#include "core/http.h"
#include "core/modellib.h"
#include "core/sysmetrics.h"
#include "imgui_stdlib.h"
#include "pane_common.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

std::string gQuery;
int gSource = 0;   // 0 Ollama library, 1 Hugging Face GGUF
bool gSearching = false;
std::string gError;
std::vector<modellib::Entry> gResults;
std::string gSearchedFor;

struct Variants {
  bool loading = false;
  std::string error;
  std::vector<modellib::Variant> list;
};
std::map<std::string, Variants> gVariants;   // entry id -> variants
std::string gExpanded;

struct PullJob {
  std::string name;
  double bytes = -1.0;
  std::string status = "queued";
  double completed = 0.0, total = 0.0;
  bool running = false, done = false, failed = false;
  std::atomic<bool> cancel{false};
};
std::deque<std::shared_ptr<PullJob>> gQueue;

double freeForModels() {
  struct statvfs fs {};
  if (statvfs(sysmetrics::modelsFolder().c_str(), &fs) != 0) return -1.0;
  return static_cast<double>(fs.f_bavail) * static_cast<double>(fs.f_frsize);
}

// ollama.com answers a JSON Accept with a bare tag list (no sizes), so its
// pages are asked for as HTML; the Hub API is JSON.
std::string fetch(const std::string& url, std::string* error) {
  net::Request req;
  req.url = url;
  req.timeoutSec = 20;
  req.headers.push_back({"Accept", url.find("huggingface.co") != std::string::npos ? "application/json" : "text/html"});
  net::Response res = net::perform(req);
  if (!res.ok()) {
    *error = res.error.empty() ? "HTTP " + std::to_string(res.status) : res.error;
    return "";
  }
  return res.body;
}

}  // namespace

void App::modelPullNext() {
  for (const auto& job : gQueue) {
    if (job->running) return;
  }
  std::shared_ptr<PullJob> next;
  for (const auto& job : gQueue) {
    if (!job->done && !job->failed) {
      next = job;
      break;
    }
  }
  if (!next) return;
  next->running = true;
  next->status = "starting...";
  async::runDetached([this, next] {
    api::Result res = api_.ollamaPull(
        next->name,
        [next](const Json& line) {
          const modellib::Progress p = modellib::parseProgress(line);
          async::post([next, p] {
            if (!p.error.empty()) {
              next->status = p.error;
              next->failed = true;
              return;
            }
            next->status = p.status;
            if (p.total > 0) {
              next->completed = p.completed;
              next->total = p.total;
            }
            if (p.done) next->done = true;
          });
          return !next->cancel.load();
        },
        &next->cancel);
    async::post([this, next, res] {
      next->running = false;
      if (next->cancel.load()) {
        next->failed = true;
        next->status = "cancelled";
      } else if (!res.ok && !next->done) {
        next->failed = true;
        next->status = api::errorMessage(res);
      } else if (!next->failed) {
        next->done = true;
        next->status = "done";
        widgets::toast(widgets::ToastLevel::Success, "Pulled " + next->name);
        refreshModels();
      }
      modelPullNext();
    });
  });
}

void App::modelQueuePull(const std::string& name, double bytes) {
  for (const auto& job : gQueue) {
    if (job->name == name && !job->done && !job->failed) {
      widgets::toastInfo(name + " is already queued");
      return;
    }
  }
  const double free = freeForModels();
  if (!modellib::enoughSpace(bytes, free)) {
    widgets::toastError("Not enough space for " + name + ": needs " + modellib::humanSize(bytes) + " plus headroom, " + modellib::humanSize(free) + " free");
    return;
  }
  auto job = std::make_shared<PullJob>();
  job->name = name;
  job->bytes = bytes;
  gQueue.push_back(job);
  modelPullNext();
}

void App::drawModelLibrarySection(float width) {
  const auto& palette = theme::palette();
  const double free = freeForModels();

  // Queue first: what is downloading right now.
  if (!gQueue.empty()) {
    widgets::sectionHeader("PULL QUEUE");
    card("pullqueue", [&] {
      std::shared_ptr<PullJob> remove;
      for (const auto& job : gQueue) {
        ImGui::PushID(job.get());
        theme::pushBold();
        ImGui::TextUnformatted(job->name.c_str());
        theme::pop();
        ImGui::SameLine();
        widgets::dimText("%s%s", job->status.c_str(), job->bytes > 0 ? ("  (" + modellib::humanSize(job->bytes) + ")").c_str() : "");
        if (job->running) {
          const float fraction = job->total > 0 ? static_cast<float>(job->completed / job->total) : 0.0f;
          char label[64];
          snprintf(label, sizeof label, "%.0f%%  %s / %s", fraction * 100.0f, modellib::humanSize(job->completed).c_str(), modellib::humanSize(job->total).c_str());
          ImGui::ProgressBar(fraction, ImVec2(width - 180.0f, 0), job->total > 0 ? label : "");
          ImGui::SameLine();
          if (ImGui::SmallButton("Cancel")) job->cancel.store(true);
        } else {
          ImGui::SameLine();
          if (ImGui::SmallButton(job->done ? "Clear" : job->failed ? "Remove" : "Drop")) remove = job;
        }
        ImGui::PopID();
      }
      if (remove) {
        if (!remove->done && !remove->failed) remove->failed = true;   // a queued job that was dropped
        gQueue.erase(std::remove(gQueue.begin(), gQueue.end(), remove), gQueue.end());
      }
    }, width);
  }

  widgets::sectionHeader("FIND A MODEL");
  card("modelsearch", [&] {
    const char* sources[] = {"Ollama library", "Hugging Face (GGUF)"};
    ImGui::SetNextItemWidth(190.0f);
    ImGui::Combo("##msource", &gSource, sources, 2);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(std::max(200.0f, width - 420.0f));
    const bool enter = ImGui::InputTextWithHint("##mquery", gSource == 0 ? "e.g. qwen3, llama, embed" : "e.g. Qwen2.5 0.5B instruct", &gQuery,
                                                ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    widgets::beginDisabled(gSearching || core::trim(gQuery).empty());
    if ((widgets::primaryButton(gSearching ? "Searching..." : "Search", ImVec2(110, 0)) || enter) && !gSearching && !core::trim(gQuery).empty()) {
      gSearching = true;
      gError.clear();
      gResults.clear();
      gExpanded.clear();
      const std::string query = core::trim(gQuery);
      const int source = gSource;
      async::run([query, source] {
        std::string error;
        const std::string body = fetch(source == 0 ? modellib::ollamaSearchUrl(query) : modellib::hfSearchUrl(query), &error);
        std::vector<modellib::Entry> results;
        if (error.empty()) results = source == 0 ? modellib::parseOllamaSearch(body) : modellib::parseHfSearch(body);
        async::post([query, results, error] {
          gSearching = false;
          gResults = results;
          gError = error;
          gSearchedFor = query;
        });
      });
    }
    widgets::endDisabled(gSearching || core::trim(gQuery).empty());
    widgets::dimText("%s free where Ollama keeps models (%s). Pulls go through the server's Ollama; a model needs its size plus some headroom.",
                     modellib::humanSize(free).c_str(), sysmetrics::modelsFolder().c_str());
    if (!gError.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.danger));
      ImGui::TextWrapped("Search failed: %s", gError.c_str());
      ImGui::PopStyleColor();
    }
  }, width);

  if (gResults.empty()) {
    if (!gSearchedFor.empty() && !gSearching && gError.empty()) widgets::dimText("No models matched \"%s\".", gSearchedFor.c_str());
    return;
  }
  widgets::dimText("%zu result%s for \"%s\"", gResults.size(), gResults.size() == 1 ? "" : "s", gSearchedFor.c_str());
  for (const modellib::Entry& e : gResults) {
    ImGui::PushID(e.id.c_str());
    card(e.id.c_str(), [&] {
      theme::pushBold();
      ImGui::TextUnformatted(e.id.c_str());
      theme::pop();
      ImGui::SameLine();
      const bool open = gExpanded == e.id;
      if (ImGui::SmallButton(open ? "Hide variants" : "Variants")) {
        gExpanded = open ? "" : e.id;
        if (!open && !gVariants[e.id].loading && gVariants[e.id].list.empty()) {
          Variants& v = gVariants[e.id];
          v.loading = true;
          v.error.clear();
          const std::string id = e.id;
          const std::string source = e.source;
          async::run([id, source] {
            std::string error;
            const std::string body = fetch(source == "ollama" ? modellib::ollamaTagsUrl(id) : modellib::hfTreeUrl(id), &error);
            std::vector<modellib::Variant> list;
            if (error.empty()) list = source == "ollama" ? modellib::parseOllamaTags(id, body) : modellib::parseHfTree(id, body);
            async::post([id, list, error] {
              Variants& v = gVariants[id];
              v.loading = false;
              v.list = list;
              v.error = error.empty() && list.empty() ? "no GGUF files or tags found" : error;
            });
          });
        }
      }
      if (!e.description.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - 40.0f);
        widgets::dimText("%s", e.description.c_str());
        ImGui::PopTextWrapPos();
      }
      if (open) {
        const Variants& v = gVariants[e.id];
        if (v.loading) {
          widgets::loadingRow("Loading variants");
        } else if (!v.error.empty()) {
          ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.danger));
          ImGui::TextUnformatted(v.error.c_str());
          ImGui::PopStyleColor();
        } else if (ImGui::BeginTable("##variants", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp, ImVec2(width - 40.0f, 0))) {
          ImGui::TableSetupColumn("Variant", ImGuiTableColumnFlags_WidthFixed, 140.0f);
          ImGui::TableSetupColumn("Pulls as");
          ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 90.0f);
          ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 80.0f);
          ImGui::TableHeadersRow();
          for (const modellib::Variant& var : v.list) {
            ImGui::PushID(var.pullName.c_str());
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(var.label.c_str());
            ImGui::TableSetColumnIndex(1);
            widgets::dimText("%s", var.pullName.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(modellib::humanSize(var.bytes).c_str());
            ImGui::TableSetColumnIndex(3);
            const bool fits = modellib::enoughSpace(var.bytes, free);
            widgets::beginDisabled(!fits);
            if (ImGui::SmallButton("Pull")) modelQueuePull(var.pullName, var.bytes);
            widgets::endDisabled(!fits);
            if (!fits && ImGui::IsItemHovered()) ImGui::SetTooltip("Not enough free space (%s free)", modellib::humanSize(free).c_str());
            ImGui::PopID();
          }
          ImGui::EndTable();
        }
      }
    }, width);
    ImGui::PopID();
  }
}

}  // namespace app
