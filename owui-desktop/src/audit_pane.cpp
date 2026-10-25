// Audit log (Tools > Audit): every outbound transfer the app made - which
// host, which path, how many bytes each way - and every tool-call decision,
// searchable and exportable. The net layer reports transfers through an
// observer; MCP / plugin tool approvals are recorded where they are decided.
#include "app.h"

#include <algorithm>

#include "core/auditlog.h"
#include "core/common.h"
#include "core/http.h"
#include "imgui_stdlib.h"
#include "platform.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

std::string logPath() { return core::supportDir() + "/audit.jsonl"; }

int gRangeDays = 1;
std::string gFilter;
int gKind = 0;   // 0 all, 1 http, 2 tool
std::vector<auditlog::Record> gRecords;
double gLoadedAt = 0.0;
bool gConfirmClear = false;

std::string bytesText(int64_t n) { return core::humanBytes(n); }

bool matches(const auditlog::Record& r, const std::string& needle) {
  if (needle.empty()) return true;
  const std::string n = core::lower(needle);
  return core::lower(r.host).find(n) != std::string::npos || core::lower(r.path).find(n) != std::string::npos ||
         core::lower(r.method).find(n) != std::string::npos || core::lower(r.detail).find(n) != std::string::npos;
}

}  // namespace

void App::auditInit() {
  auditlog::trim(logPath(), 40000, 20000);
  net::setObserver([](const net::Transfer& t) {
    if (!core::Settings::instance().extra.value("auditLog", true)) return;
    auditlog::Record r;
    r.at = core::nowSeconds();
    r.kind = "http";
    auditlog::splitUrl(t.url, &r.host, &r.path);
    r.method = t.method;
    r.status = t.status;
    r.bytesOut = t.bytesOut;
    r.bytesIn = t.bytesIn;
    r.ms = t.ms;
    r.ok = t.error.empty() && t.status >= 200 && t.status < 400;
    if (!t.error.empty()) r.detail = t.error;
    auditlog::append(logPath(), r);
  });
}

void App::auditTool(const std::string& server, const std::string& tool, const std::string& qualified,
                    const std::string& decision) {
  if (!settings().extra.value("auditLog", true)) return;
  auditlog::Record r;
  r.at = core::nowSeconds();
  r.kind = "tool";
  r.host = server;
  r.method = tool;
  r.path = qualified;
  r.detail = decision;
  r.ok = decision.rfind("denied", 0) != 0;
  auditlog::append(logPath(), r);
  gLoadedAt = 0.0;
}

void App::drawAuditSection(float width) {
  const auto& palette = theme::palette();
  widgets::sectionHeader("AUDIT");
  bool enabled = settings().extra.value("auditLog", true);
  if (ImGui::Checkbox("Record every outbound request (host, path, sizes - never bodies or headers) and tool decision", &enabled)) {
    settings().extra["auditLog"] = enabled;
    settings().save();
  }
  ImGui::SetNextItemWidth(110);
  const char* ranges[] = {"Today", "7 days", "30 days", "All"};
  int rangeIndex = gRangeDays == 1 ? 0 : gRangeDays == 7 ? 1 : gRangeDays == 30 ? 2 : 3;
  if (ImGui::Combo("##arange", &rangeIndex, ranges, 4)) {
    gRangeDays = rangeIndex == 0 ? 1 : rangeIndex == 1 ? 7 : rangeIndex == 2 ? 30 : 0;
    gLoadedAt = 0.0;
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(110);
  const char* kinds[] = {"All", "Requests", "Tool calls"};
  ImGui::Combo("##akind", &gKind, kinds, 3);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(260);
  ImGui::InputTextWithHint("##afilter", "filter host, path, tool, detail", &gFilter);
  ImGui::SameLine();
  if (ImGui::Button("Export CSV...")) {
    const std::string path = platform::saveFile("audit.csv");
    if (!path.empty()) {
      core::writeFile(path, auditlog::csv(gRecords));
      widgets::toast(widgets::ToastLevel::Success, "Saved " + std::to_string(gRecords.size()) + " rows to " + path);
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Export JSON...")) {
    const std::string path = platform::saveFile("audit.json");
    if (!path.empty()) {
      core::writeFile(path, auditlog::json(gRecords));
      widgets::toast(widgets::ToastLevel::Success, "Saved to " + path);
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Clear log")) gConfirmClear = true;
  if (gConfirmClear) {
    ImGui::SameLine();
    ImGui::TextUnformatted("Delete the whole audit log?");
    ImGui::SameLine();
    if (ImGui::SmallButton("Yes, clear")) {
      auditlog::clear(logPath());
      gConfirmClear = false;
      gLoadedAt = 0.0;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Keep")) gConfirmClear = false;
  }

  if (core::nowMillis() - gLoadedAt > 3000.0) {
    gRecords = auditlog::load(logPath(), gRangeDays);
    std::reverse(gRecords.begin(), gRecords.end());   // newest first
    gLoadedAt = core::nowMillis();
  }

  // Summary: hosts.
  const std::vector<auditlog::HostRow> hosts = auditlog::byHost(gRecords);
  int64_t out = 0, in = 0;
  int tools = 0, denied = 0;
  for (const auditlog::Record& r : gRecords) {
    if (r.kind == "http") {
      out += r.bytesOut;
      in += r.bytesIn;
    } else {
      ++tools;
      if (!r.ok) ++denied;
    }
  }
  ImGui::Dummy(ImVec2(0, 4));
  theme::pushBold();
  ImGui::Text("%zu requests to %zu host%s - %s sent, %s received - %d tool call%s%s", gRecords.size() - tools, hosts.size(),
              hosts.size() == 1 ? "" : "s", bytesText(out).c_str(), bytesText(in).c_str(), tools, tools == 1 ? "" : "s",
              denied ? (" (" + std::to_string(denied) + " denied)").c_str() : "");
  theme::pop();
  for (size_t i = 0; i < hosts.size() && i < 6; ++i) {
    const auditlog::HostRow& h = hosts[i];
    widgets::dimText("%s - %d request%s, %s out / %s in%s", h.host.c_str(), h.requests, h.requests == 1 ? "" : "s",
                     bytesText(h.bytesOut).c_str(), bytesText(h.bytesIn).c_str(),
                     h.failures ? (", " + std::to_string(h.failures) + " failed").c_str() : "");
  }
  ImGui::Dummy(ImVec2(0, 6));

  // Table.
  const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY |
                                ImGuiTableFlags_SizingStretchProp;
  if (ImGui::BeginTable("##audit", 7, flags, ImVec2(width - 20.0f, 0))) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 72.0f);
    ImGui::TableSetupColumn("Host", ImGuiTableColumnFlags_WidthFixed, 150.0f);
    ImGui::TableSetupColumn("What");
    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableSetupColumn("Out", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("In", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableHeadersRow();
    int shown = 0;
    for (const auditlog::Record& r : gRecords) {
      if (gKind == 1 && r.kind != "http") continue;
      if (gKind == 2 && r.kind != "tool") continue;
      if (!matches(r, gFilter)) continue;
      if (++shown > 1000) break;
      std::time_t t = static_cast<std::time_t>(r.at);
      std::tm local{};
      localtime_r(&t, &local);
      char when[16];
      std::strftime(when, sizeof(when), "%H:%M:%S", &local);
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted(when);
      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted(r.host.c_str());
      ImGui::TableSetColumnIndex(2);
      if (r.kind == "tool") {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(r.ok ? palette.success : palette.danger));
        ImGui::Text("tool %s - %s", r.method.c_str(), r.detail.c_str());
        ImGui::PopStyleColor();
      } else {
        ImGui::Text("%s %s", r.method.c_str(), r.path.c_str());
        if (!r.detail.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", r.detail.c_str());
      }
      ImGui::TableSetColumnIndex(3);
      if (r.kind == "http") {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(r.ok ? palette.textDim : palette.danger));
        ImGui::Text("%ld", r.status);
        ImGui::PopStyleColor();
      }
      ImGui::TableSetColumnIndex(4);
      if (r.kind == "http") ImGui::TextUnformatted(bytesText(r.bytesOut).c_str());
      ImGui::TableSetColumnIndex(5);
      if (r.kind == "http") ImGui::TextUnformatted(bytesText(r.bytesIn).c_str());
      ImGui::TableSetColumnIndex(6);
      if (r.kind == "http") ImGui::Text("%.0f", r.ms);
    }
    ImGui::EndTable();
    if (shown == 0) widgets::dimText("Nothing in this range.");
  }
}

}  // namespace app
