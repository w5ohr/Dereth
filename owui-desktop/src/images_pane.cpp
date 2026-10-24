// Images: text-to-image generation through the server's configured backend.
#include "core/async.h"
#include "core/common.h"
#include "pane_common.h"
#include "platform.h"
#include "ui/icons.h"
#include "ui/images.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

bool gLoaded = false;
std::string gPrompt;
std::string gNegative;
std::string gModel;
int gCount = 1;
int gSizeIndex = 1;
bool gGenerating = false;
Json gResults = Json::array();

const char* kSizes[] = {"512x512", "1024x1024", "1024x1792", "1792x1024"};

}  // namespace

void App::drawImagesPane() {
  if (!gLoaded) {
    gLoaded = true;
    async::run([this] {
      api::Result config = api_.imageConfig();
      api::Result models = api_.imageModels();
      async::post([this, config, models] {
        if (config.ok) imageConfig = config.data;
        if (models.ok) {
          imageModelList = models.data.is_array() ? models.data
                                                  : models.data.value("data", Json::array());
          if (gModel.empty() && !imageModelList.empty()) {
            gModel = imageModelList[0].is_string() ? imageModelList[0].get<std::string>()
                                                   : imageModelList[0].value("id", "");
          }
        }
      });
    });
  }

  paneHeader("Images", "Generate images with the server's image backend", [&] {
    ImGui::SameLine(ImGui::GetWindowWidth() - 120.0f);
    if (ImGui::Button("Refresh", ImVec2(90, 30))) gLoaded = false;
  });

  paneBody("##images");
  const float width = ImGui::GetContentRegionAvail().x - 18.0f;

  const std::string engine = imageConfig.value("engine", "");
  const bool enabled = imageConfig.value("enabled", !engine.empty());
  if (!enabled) {
    card("notice", [&] {
      icons::inlineIcon(icons::Icon::Warning, theme::palette().warning);
      ImGui::SameLine(0, 6);
      ImGui::TextUnformatted(
          "Image generation is turned off on this server. Enable it under Admin > Images.");
    }, width);
  }

  card("prompt", [&] {
    ImGui::TextUnformatted("Prompt");
    ImGui::InputTextMultiline("##imgprompt", &gPrompt, ImVec2(width - 40.0f, 70.0f));
    ImGui::TextUnformatted("Negative prompt");
    ImGui::SetNextItemWidth(width - 40.0f);
    ImGui::InputTextWithHint("##imgneg", "optional", &gNegative);

    ImGui::SetNextItemWidth(220);
    if (ImGui::BeginCombo("Model", gModel.empty() ? "(server default)" : gModel.c_str())) {
      for (const auto& model : imageModelList) {
        const std::string id = model.is_string() ? model.get<std::string>() : model.value("id", "");
        if (id.empty()) continue;
        if (ImGui::Selectable(id.c_str(), id == gModel)) gModel = id;
      }
      ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160);
    ImGui::Combo("Size", &gSizeIndex, kSizes, IM_ARRAYSIZE(kSizes));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::SliderInt("Count", &gCount, 1, 4);

    ImGui::Dummy(ImVec2(0, 6));
    widgets::beginDisabled(gGenerating || core::trim(gPrompt).empty());
    if (widgets::primaryButton(gGenerating ? "Generating..." : "Generate", ImVec2(160, 32))) {
      gGenerating = true;
      Json form{{"prompt", core::trim(gPrompt)},
                {"n", gCount},
                {"size", kSizes[gSizeIndex]}};
      if (!gModel.empty()) form["model"] = gModel;
      if (!core::trim(gNegative).empty()) form["negative_prompt"] = core::trim(gNegative);
      async::run([this, form] {
        api::Result res = api_.generateImages(form);
        async::post([res] {
          gGenerating = false;
          if (res.ok) {
            gResults = res.data.is_array() ? res.data : res.data.value("data", Json::array());
            widgets::toast(widgets::ToastLevel::Success, "Image ready");
          } else {
            widgets::toastError("Generation failed: " + api::errorMessage(res));
          }
        });
      });
    }
    widgets::endDisabled(gGenerating || core::trim(gPrompt).empty());
    if (gGenerating) {
      ImGui::SameLine();
      widgets::loadingRow("This can take a while on large models");
    }
    ImGui::SameLine();
    if (ImGui::Button("Send prompt to chat", ImVec2(180, 32))) {
      composer = core::trim(gPrompt);
      imageGenerationEnabled = true;
      pane = Pane::Chat;
      focusComposer = true;
    }
  }, width);

  if (!gResults.empty()) {
    widgets::sectionHeader("RESULTS");
    float x = 0.0f;
    for (size_t i = 0; i < gResults.size(); ++i) {
      const Json& result = gResults[i];
      const std::string url = result.is_string() ? result.get<std::string>()
                                                 : result.value("url", "");
      if (url.empty()) continue;
      images::Entry entry = images::get(url, [this, url] { return fetchImageBytes(url); });
      ImGui::PushID(static_cast<int>(i));
      if (entry.ready()) {
        const float side = 260.0f;
        const float scale = side / static_cast<float>(entry.width);
        ImGui::BeginGroup();
        ImGui::Image(entry.texture, ImVec2(side, static_cast<float>(entry.height) * scale));
        if (ImGui::SmallButton("Save as...")) {
          const std::string path = platform::saveFile("generated.png");
          if (!path.empty()) {
            const std::string bytes = fetchImageBytes(url);
            core::writeFile(path, bytes);
            widgets::toast(widgets::ToastLevel::Success, "Saved to " + path);
          }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Open")) platform::openUrl(imageUrlFor(url));
        ImGui::EndGroup();
        x += 270.0f;
        if (x + 270.0f < width) {
          ImGui::SameLine();
        } else {
          x = 0.0f;
        }
      } else if (entry.failed) {
        widgets::dimText("could not load %s", url.c_str());
      } else {
        widgets::loadingRow("decoding...");
      }
      ImGui::PopID();
    }
    ImGui::NewLine();
  }

  if (!imageConfig.empty()) {
    widgets::sectionHeader("BACKEND");
    card("config", [&] {
      field("Engine", imageConfig.value("engine", "(none)"));
      if (imageConfig.contains("comfyui") && imageConfig["comfyui"].is_object()) {
        field("ComfyUI", imageConfig["comfyui"].value("COMFYUI_BASE_URL", ""));
      }
      if (imageConfig.contains("openai") && imageConfig["openai"].is_object()) {
        field("OpenAI base", imageConfig["openai"].value("OPENAI_API_BASE_URL", ""));
      }
      if (imageConfig.contains("automatic1111") && imageConfig["automatic1111"].is_object()) {
        field("A1111", imageConfig["automatic1111"].value("AUTOMATIC1111_BASE_URL", ""));
      }
    }, width);
  }

  paneEnd();
}

}  // namespace app
