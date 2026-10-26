// Redaction in the app: every outgoing prompt is scanned; secrets and
// personal data are shown in a review sheet (mask / keep per item, "always
// mask this kind" switches) and replaced with placeholders before the text
// leaves the machine. The placeholder -> value map is kept only locally
// (support dir, never on the server), so messages and replies are shown with
// the real values again.
#include "app.h"

#include <algorithm>

#include "core/common.h"
#include "core/redact.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

std::string mapPath() { return core::supportDir() + "/redactions.json"; }

// chat id -> {placeholder -> value}, loaded once.
Json& allMaps() {
  static Json maps = [] {
    bool ok = false;
    const std::string raw = core::readFile(mapPath(), &ok);
    Json parsed = ok ? Json::parse(raw, nullptr, false) : Json::object();
    return parsed.is_discarded() || !parsed.is_object() ? Json::object() : parsed;
  }();
  return maps;
}

std::map<std::string, std::string> mapFor(const std::string& chatId) {
  std::map<std::string, std::string> out;
  const Json& maps = allMaps();
  const std::string key = chatId.empty() ? "new" : chatId;
  if (maps.contains(key) && maps[key].is_object()) {
    for (const auto& [p, v] : maps[key].items()) {
      if (v.is_string()) out[p] = v;
    }
  }
  return out;
}

void saveMap(const std::string& chatId, const std::map<std::string, std::string>& mapping) {
  Json& maps = allMaps();
  const std::string key = chatId.empty() ? "new" : chatId;
  Json entry = Json::object();
  for (const auto& [p, v] : mapping) entry[p] = v;
  maps[key] = entry;
  core::writeFile(mapPath(), maps.dump(2));
}

const char* policyKey(redact::Kind kind) {
  switch (kind) {
    case redact::Kind::ApiKey: return "redactApiKey";
    case redact::Kind::Token: return "redactToken";
    case redact::Kind::PrivateKey: return "redactPrivateKey";
    case redact::Kind::Password: return "redactPassword";
    case redact::Kind::Card: return "redactCard";
    case redact::Kind::Ssn: return "redactSsn";
    case redact::Kind::Iban: return "redactIban";
    case redact::Kind::Email: return "redactEmail";
    case redact::Kind::Phone: return "redactPhone";
    case redact::Kind::Ip: return "redactIp";
  }
  return "redactOther";
}

}  // namespace

// "ask" (default), "always" (mask without asking), "never" (ignore this kind).
std::string App::redactionPolicy(int kind) const {
  return core::Settings::instance().extra.value(policyKey(static_cast<redact::Kind>(kind)), "ask");
}

void App::setRedactionPolicy(int kind, const std::string& policy) {
  settings().extra[policyKey(static_cast<redact::Kind>(kind))] = policy;
  settings().save();
}

bool App::interceptRedaction(const std::string& text) {
  if (!settings().extra.value("redactScan", true)) return false;
  std::vector<redact::Finding> findings = redact::scan(text);
  // Kinds set to "never" are not findings at all.
  findings.erase(std::remove_if(findings.begin(), findings.end(), [&](const redact::Finding& f) {
                   return redactionPolicy(static_cast<int>(f.kind)) == "never";
                 }), findings.end());
  if (findings.empty()) return false;

  bool needsReview = false;
  for (const redact::Finding& f : findings) {
    if (redactionPolicy(static_cast<int>(f.kind)) != "always") needsReview = true;
  }
  if (!needsReview) {
    std::vector<bool> mask(findings.size(), true);
    sendRedacted(text, findings, mask);
    return true;
  }
  redactionSheet = RedactionSheet{};
  redactionSheet.open = true;
  redactionSheet.justOpened = true;
  redactionSheet.text = text;
  redactionSheet.findings = findings;
  redactionSheet.mask.assign(findings.size(), true);
  return true;
}

void App::sendRedacted(const std::string& text, const std::vector<redact::Finding>& findings,
                       const std::vector<bool>& mask) {
  std::map<std::string, std::string> mapping = mapFor(chat.id);
  const size_t before = mapping.size();
  const std::string masked = redact::apply(text, findings, mask, &mapping);
  if (mapping.size() != before) saveMap(chat.id, mapping);
  int count = 0;
  for (bool m : mask) count += m ? 1 : 0;
  sendingRedacted_ = true;
  historyOverride_ = text;
  sendMessage(masked);
  historyOverride_.clear();
  sendingRedacted_ = false;
  if (count > 0) {
    widgets::toastInfo("Masked " + std::to_string(count) + (count == 1 ? " item" : " items") +
                       " before sending - shown unmasked here only");
  }
}

// Display: placeholders in this chat's messages read as the real values.
std::string App::unredact(const std::string& text) const {
  if (text.find('[') == std::string::npos) return text;
  static std::string cachedChat;
  static std::map<std::string, std::string> cached;
  static size_t cachedSize = 0;
  const Json& maps = allMaps();
  const std::string key = chat.id.empty() ? "new" : chat.id;
  const size_t size = maps.contains(key) ? maps[key].size() : 0;
  if (cachedChat != key || cachedSize != size) {
    cachedChat = key;
    cached = mapFor(chat.id);
    cachedSize = size;
  }
  return redact::restore(text, cached);
}

// A brand-new chat gets its id after the first send: carry the map over.
void App::adoptRedactionsForNewChat() {
  if (chat.id.empty()) return;
  Json& maps = allMaps();
  if (!maps.contains("new") || maps["new"].empty()) return;
  maps[chat.id] = maps["new"];
  maps.erase("new");
  core::writeFile(mapPath(), maps.dump(2));
}

void App::drawRedactionSheet() {
  if (!redactionSheet.open) return;
  if (redactionSheet.justOpened) {
    ImGui::OpenPopup("Before this leaves your Mac");
    redactionSheet.justOpened = false;
  }
  const auto& palette = theme::palette();
  ImGui::SetNextWindowSize(ImVec2(640, 0), ImGuiCond_Appearing);
  bool keepOpen = true;
  if (!ImGui::BeginPopupModal("Before this leaves your Mac", &keepOpen, ImGuiWindowFlags_NoSavedSettings)) {
    if (!keepOpen) redactionSheet.open = false;
    return;
  }
  RedactionSheet& sheet = redactionSheet;
  widgets::dimText("The prompt holds things that look like secrets or personal data. Masked items are\n"
                   "replaced with placeholders for the model; this window shows the real values.");
  ImGui::Dummy(ImVec2(0, 4));

  if (ImGui::BeginTable("##findings", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Mask", ImGuiTableColumnFlags_WidthFixed, 50.0f);
    ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 160.0f);
    ImGui::TableSetupColumn("Value");
    ImGui::TableSetupColumn("Next time", ImGuiTableColumnFlags_WidthFixed, 150.0f);
    ImGui::TableHeadersRow();
    for (size_t i = 0; i < sheet.findings.size(); ++i) {
      const redact::Finding& f = sheet.findings[i];
      ImGui::PushID(static_cast<int>(i));
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      bool on = sheet.mask[i];
      if (ImGui::Checkbox("##mask", &on)) sheet.mask[i] = on;
      ImGui::TableSetColumnIndex(1);
      const bool secret = redact::kindIsSecret(f.kind);
      ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(secret ? palette.danger : palette.warning));
      ImGui::TextUnformatted(redact::kindLabel(f.kind));
      ImGui::PopStyleColor();
      ImGui::TableSetColumnIndex(2);
      theme::pushMono();
      ImGui::TextUnformatted(sheet.reveal ? f.value.c_str() : redact::preview(f.value).c_str());
      theme::pop();
      ImGui::TableSetColumnIndex(3);
      const std::string policy = redactionPolicy(static_cast<int>(f.kind));
      const char* labels[] = {"ask", "always mask", "never mask"};
      const char* values[] = {"ask", "always", "never"};
      int current = policy == "always" ? 1 : policy == "never" ? 2 : 0;
      ImGui::SetNextItemWidth(-1.0f);
      if (ImGui::Combo("##policy", &current, labels, 3)) setRedactionPolicy(static_cast<int>(f.kind), values[current]);
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  if (ImGui::Checkbox("Show full values", &sheet.reveal)) {}
  ImGui::Dummy(ImVec2(0, 6));

  bool send = false, sendAsIs = false;
  // The Enter that sent the prompt is still down on the frame this opens.
  if (!ImGui::IsKeyDown(ImGuiKey_Enter) && !ImGui::IsKeyDown(ImGuiKey_KeypadEnter)) sheet.enterArmed = true;
  if (widgets::primaryButton("Send masked", ImVec2(130, 0)) ||
      (sheet.enterArmed && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)))) {
    send = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Send as is")) sendAsIs = true;
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sends the prompt unchanged, with everything in it");
  ImGui::SameLine();
  if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    sheet.open = false;
    ImGui::CloseCurrentPopup();
  }
  if (send || sendAsIs) {
    sheet.open = false;
    ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    std::vector<bool> mask = sheet.mask;
    if (sendAsIs) std::fill(mask.begin(), mask.end(), false);
    sendRedacted(sheet.text, sheet.findings, mask);
    ++composerGeneration;
    focusComposer = true;
    return;
  }
  ImGui::EndPopup();
}

}  // namespace app
