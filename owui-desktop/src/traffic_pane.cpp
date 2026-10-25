// Traffic inspector in the app (#134): observers on the HTTP layer and the
// socket feed an in-memory log; the Tools "Traffic" tab shows it live with a
// latency waterfall, a detail view with bodies, replay, curl and HAR export.
#include <algorithm>

#include "app.h"
#include "core/async.h"
#include "core/auditlog.h"
#include "core/common.h"
#include "core/http.h"
#include "core/settings.h"
#include "core/socketio.h"
#include "core/traffic.h"
#include "pane_common.h"
#include "platform.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

traffic::Log gLog(1000);
std::string gFilter;
int gKind = 0;              // 0 all, 1 http, 2 socket
bool gHeartbeats = false;
bool gFollow = true;
int gSelected = 0;          // seq of the entry in the detail view
int gDetailSeq = 0;
std::string gReqText, gResText;
std::vector<traffic::Entry> gEntries;
double gSnapshotAt = 0.0;
size_t gLastCount = 0;
bool gReplaying = false;

const traffic::Entry* findEntry(int seq) {
  for (const traffic::Entry& e : gEntries) {
    if (e.seq == seq) return &e;
  }
  return nullptr;
}

}  // namespace

void App::trafficInit() {
  net::setCaptureBodies(settings().extra.value("trafficBodies", true));
  net::addObserver([](const net::Transfer& t) {
    traffic::Entry e;
    e.kind = "http";
    e.startMs = t.startedAtMs;
    e.ms = t.ms;
    e.method = t.method;
    e.url = t.url;
    auditlog::splitUrl(t.url, &e.host, &e.path);
    e.status = t.status;
    e.bytesOut = t.bytesOut;
    e.bytesIn = t.bytesIn;
    e.error = t.error;
    e.contentType = t.contentType;
    e.requestBody = t.requestBody;
    e.responseBody = t.responseBody;
    e.streamed = t.streamed;
    gLog.add(e);
  });
  sio::setObserver([](bool outgoing, const std::string& packet) {
    traffic::Entry e;
    e.kind = "ws";
    e.startMs = traffic::nowEpochMs();
    e.method = outgoing ? "send" : "recv";
    e.event = traffic::sioEvent(packet);
    e.host = "socket";
    e.path = e.event;
    e.responseBody = traffic::cap(packet);
    (outgoing ? e.bytesOut : e.bytesIn) = static_cast<int64_t>(packet.size());
    gLog.add(e);
  });
}

void App::drawTrafficSection(float width) {
  const auto& palette = theme::palette();
  widgets::sectionHeader("TRAFFIC");
  if (ImGui::Button(gLog.paused() ? "Resume" : "Pause", ImVec2(80, 0))) gLog.setPaused(!gLog.paused());
  ImGui::SameLine();
  if (ImGui::Button("Clear")) {
    gLog.clear();
    gSelected = 0;
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(100);
  const char* kinds[] = {"All", "HTTP", "Socket"};
  ImGui::Combo("##tkind", &gKind, kinds, 3);
  ImGui::SameLine();
  ImGui::Checkbox("Heartbeats", &gHeartbeats);
  ImGui::SameLine();
  ImGui::Checkbox("Follow", &gFollow);
  ImGui::SameLine();
  bool bodies = settings().extra.value("trafficBodies", true);
  if (ImGui::Checkbox("Capture bodies", &bodies)) {
    settings().extra["trafficBodies"] = bodies;
    settings().save();
    net::setCaptureBodies(bodies);
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(220);
  ImGui::InputTextWithHint("##tfilter", "filter host, path, event, status", &gFilter);

  if (core::nowMillis() - gSnapshotAt > 250.0) {
    gEntries = gLog.snapshot();
    gSnapshotAt = core::nowMillis();
  }
  const std::vector<traffic::Entry> shown = traffic::filter(gEntries, gFilter, gKind, gHeartbeats);
  const std::string wsUrl = socket_.url();

  ImGui::SameLine();
  if (ImGui::Button("Export HAR...")) {
    const std::string path = platform::saveFile("open-webui-traffic.har");
    if (!path.empty()) {
      core::writeFile(path, traffic::har(shown, platform::appVersion(), wsUrl));
      widgets::toast(widgets::ToastLevel::Success, "Saved " + std::to_string(shown.size()) + " entries to " + path);
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Copy HAR")) {
    platform::setClipboardText(traffic::har(shown, platform::appVersion(), wsUrl));
    widgets::toast(widgets::ToastLevel::Success, "Copied a HAR with " + std::to_string(shown.size()) + " entries");
  }

  const traffic::Summary s = traffic::summarize(shown);
  ImGui::Dummy(ImVec2(0, 4));
  theme::pushBold();
  ImGui::Text("%d request%s, %d failed - p50 %.0f ms, p95 %.0f ms - %s out / %s in - %d socket packet%s", s.requests, s.requests == 1 ? "" : "s", s.failures, s.p50, s.p95,
              core::humanBytes(s.bytesOut).c_str(), core::humanBytes(s.bytesIn).c_str(), s.wsIn + s.wsOut, s.wsIn + s.wsOut == 1 ? "" : "s");
  theme::pop();
  widgets::dimText("%s%s%s. Bodies are kept to 16 KB each with secrets masked; headers are never kept. The newest 1000 entries stay in memory%s.",
                   s.slowest.empty() ? "No requests yet" : ("Slowest: " + s.slowest).c_str(), s.slowest.empty() ? "" : " at ", s.slowest.empty() ? "" : (std::to_string(static_cast<int>(s.slowestMs)) + " ms").c_str(),
                   gLog.paused() ? " (paused)" : "");
  ImGui::Dummy(ImVec2(0, 6));

  double lo = 0.0, hi = 0.0;
  const std::vector<traffic::Bar> bars = traffic::waterfall(shown, &lo, &hi);
  const float tableHeight = std::max(160.0f, ImGui::GetContentRegionAvail().y * (gSelected ? 0.48f : 0.92f));
  const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
  if (ImGui::BeginTable("##traffic", 8, flags, ImVec2(width - 20.0f, tableHeight))) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 92.0f);
    ImGui::TableSetupColumn("Method", ImGuiTableColumnFlags_WidthFixed, 58.0f);
    ImGui::TableSetupColumn("Host", ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableSetupColumn("Path / event");
    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 52.0f);
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 68.0f);
    ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthFixed, 52.0f);
    ImGui::TableSetupColumn("Waterfall", ImGuiTableColumnFlags_WidthFixed, 220.0f);
    ImGui::TableHeadersRow();
    for (size_t i = 0; i < shown.size(); ++i) {
      const traffic::Entry& e = shown[i];
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::PushID(e.seq);
      if (ImGui::Selectable(traffic::clockTime(e.startMs).c_str(), gSelected == e.seq, ImGuiSelectableFlags_SpanAllColumns)) gSelected = gSelected == e.seq ? 0 : e.seq;
      ImGui::PopID();
      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted(e.method.c_str());
      ImGui::TableSetColumnIndex(2);
      ImGui::TextUnformatted(e.host.c_str());
      ImGui::TableSetColumnIndex(3);
      if (e.kind == "ws") {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.textDim));
        ImGui::TextUnformatted(e.event.c_str());
        ImGui::PopStyleColor();
      } else {
        ImGui::TextUnformatted(e.path.c_str());
        if (!e.error.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", e.error.c_str());
      }
      ImGui::TableSetColumnIndex(4);
      if (e.kind == "http") {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(e.ok() ? palette.textDim : palette.danger));
        if (e.status > 0) ImGui::Text("%ld", e.status);
        else ImGui::TextUnformatted("err");
        ImGui::PopStyleColor();
      }
      ImGui::TableSetColumnIndex(5);
      ImGui::TextUnformatted(core::humanBytes(e.kind == "ws" ? e.bytesIn + e.bytesOut : e.bytesIn).c_str());
      ImGui::TableSetColumnIndex(6);
      if (e.kind == "http") ImGui::Text("%.0f", e.ms);
      ImGui::TableSetColumnIndex(7);
      {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        const float h = ImGui::GetTextLineHeight();
        const traffic::Bar& b = bars[i];
        const float x0 = p.x + b.x0 * w;
        const float x1 = std::max(x0 + 2.0f, p.x + b.x1 * w);
        const ImU32 colour = e.kind == "ws" ? palette.textDim : e.ok() ? palette.accent : palette.danger;
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(x0, p.y + 3.0f), ImVec2(x1, p.y + h - 3.0f), colour, 2.0f);
        ImGui::Dummy(ImVec2(w, h));
      }
    }
    if (gFollow && !gLog.paused() && shown.size() != gLastCount) ImGui::SetScrollHereY(1.0f);
    gLastCount = shown.size();
    ImGui::EndTable();
    if (shown.empty()) widgets::dimText("Nothing captured yet - every request and socket packet shows here as it happens.");
  }

  if (gSelected == 0) return;
  const traffic::Entry* e = findEntry(gSelected);
  if (!e) {
    gSelected = 0;
    return;
  }
  if (gDetailSeq != e->seq) {
    gDetailSeq = e->seq;
    gReqText = traffic::pretty(traffic::redact(e->requestBody));
    gResText = traffic::pretty(traffic::redact(e->responseBody));
  }
  ImGui::Dummy(ImVec2(0, 6));
  theme::pushBold();
  if (e->kind == "ws") ImGui::Text("socket %s - %s", e->method.c_str(), e->event.c_str());
  else ImGui::Text("%s %s", e->method.c_str(), e->url.c_str());
  theme::pop();
  if (e->kind == "http") {
    widgets::dimText("HTTP %ld%s - %.0f ms - %s out, %s in - %s - %s%s", e->status, e->error.empty() ? "" : (" (" + e->error + ")").c_str(), e->ms, core::humanBytes(e->bytesOut).c_str(),
                     core::humanBytes(e->bytesIn).c_str(), e->contentType.empty() ? "no content type" : e->contentType.c_str(), traffic::isoTime(e->startMs).c_str(),
                     e->streamed ? " - streamed" : "");
  } else {
    widgets::dimText("%s - %s", core::humanBytes(e->bytesIn + e->bytesOut).c_str(), traffic::isoTime(e->startMs).c_str());
  }
  const bool sameServer = e->kind == "http" && core::startsWith(e->url, api_.server());
  if (sameServer) {
    if (ImGui::Button(gReplaying ? "Replaying..." : "Replay") && !gReplaying) {
      const std::string method = e->method;
      const std::string path = e->url.substr(api_.server().size());
      Json body;
      bool bodyOk = true;
      if (!e->requestBody.empty()) {
        body = Json::parse(e->requestBody, nullptr, false);
        bodyOk = !body.is_discarded();
      }
      if (!bodyOk) {
        widgets::toastError("The request body was cut or is not JSON - copy it as curl instead");
      } else {
        gReplaying = true;
        async::run([this, method, path, body] {
          api::Result r = api_.request(method, path, body, 120);
          async::post([r] {
            gReplaying = false;
            widgets::toast(r.ok ? widgets::ToastLevel::Success : widgets::ToastLevel::Error, "Replayed: HTTP " + std::to_string(r.status) + (r.error.empty() ? "" : " - " + r.error));
          });
        });
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Open in API console")) consoleLoad(e->method, e->url.substr(api_.server().size()), e->requestBody);
    ImGui::SameLine();
  }
  if (e->kind == "http") {
    if (ImGui::Button("Copy as curl")) {
      platform::setClipboardText(traffic::curlCommand(*e));
      widgets::toast(widgets::ToastLevel::Success, "Copied a curl command (token as $OWUI_TOKEN)");
    }
    ImGui::SameLine();
  }
  if (ImGui::Button("Copy request")) platform::setClipboardText(gReqText);
  ImGui::SameLine();
  if (ImGui::Button("Copy response")) platform::setClipboardText(gResText);
  ImGui::SameLine();
  if (ImGui::Button("Close")) gSelected = 0;
  const float half = std::max(90.0f, (ImGui::GetContentRegionAvail().y - 40.0f) * 0.5f);
  if (e->kind == "http") {
    widgets::dimText("Request body");
    ImGui::InputTextMultiline("##treq", &gReqText, ImVec2(width - 20.0f, half), ImGuiInputTextFlags_ReadOnly);
    widgets::dimText("Response body");
  } else {
    widgets::dimText("Packet");
  }
  ImGui::InputTextMultiline("##tres", &gResText, ImVec2(width - 20.0f, half), ImGuiInputTextFlags_ReadOnly);
}

}  // namespace app
