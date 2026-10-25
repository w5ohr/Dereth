// Agent mode in the app: the composer's brain toggle sends the prompt as a
// goal to agents::runAgent instead of a plain reply. The run lives in the
// assistant message as `agentRun` (goal, steps, status, budget) so the step
// card survives a reload, tool calls show as the usual cards, and the final
// answer streams into the message body. Pause / resume / stop act on the
// stream.
#include "app.h"

#include <algorithm>
#include <chrono>
#include <thread>

#include "agents/agent_loop.h"
#include "core/async.h"
#include "core/common.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

Json stepsToJson(const std::vector<agents::AgentStep>& steps) {
  Json out = Json::array();
  for (const agents::AgentStep& step : steps) {
    out.push_back(Json{{"title", step.title},
                       {"state", step.state},
                       {"result", step.result},
                       {"feedback", step.feedback},
                       {"attempts", step.attempts},
                       {"toolCalls", step.toolCalls}});
  }
  return out;
}

std::map<std::string, bool> gExpanded;   // "<message>/<step>" -> result shown

}  // namespace

void App::startAgentStream(const std::string& assistantId, const std::string& model) {
  auto stream = std::make_shared<Stream>();
  stream->chatId = chat.id;
  stream->messageId = assistantId;
  stream->model = model;
  stream->sessionId = sessionId_;
  streams_.push_back(stream);
  timings[assistantId] = MessageTiming{core::nowMillis(), 0.0, 0.0};

  // The goal is the last user turn; everything before it is context.
  Json payload = buildCompletionPayload(assistantId, model);
  Json history = payload["messages"];
  std::string goal;
  if (history.is_array() && !history.empty() && history.back().value("role", "") == "user") {
    const Json& content = history.back()["content"];
    if (content.is_string()) {
      goal = content;
    } else if (content.is_array()) {
      for (const auto& part : content) {
        if (part.value("type", "") == "text") goal += part.value("text", "");
      }
    }
    history.erase(history.size() - 1);
  }

  agents::LoopConfig base = baseToolConfig(model);
  agents::AgentConfig config;
  config.model = base.model;
  config.history = history;
  config.goal = goal;
  config.tools = base.tools;
  config.options = base.options;
  config.systemPrefix = base.systemPrefix;
  config.maxSteps = std::clamp(settings().extra.value("agentMaxSteps", 6), 1, 12);
  config.maxAttempts = std::clamp(settings().extra.value("agentMaxAttempts", 2), 1, 4);
  config.maxRoundsPerStep = std::max(1, settings().extra.value("mcpMaxRounds", 8));
  config.verify = settings().extra.value("agentVerify", true);
  if (config.tools.empty()) {
    config.systemPrefix = "You are carrying out a multi-step task without tools: reason carefully and produce "
                          "the work yourself (text, code, calculations).";
  }

  {
    std::lock_guard<std::mutex> lock(stream->mutex);
    stream->agentRun = Json{{"goal", goal},
                            {"steps", Json::array()},
                            {"status", "Planning..."},
                            {"maxSteps", config.maxSteps},
                            {"verify", config.verify},
                            {"tools", static_cast<int>(config.tools.size())},
                            {"finished", false}};
    stream->status = "Planning...";
  }
  lastPayload = payload;
  lastPayload["transport"] = "ollama /api/chat (agent loop)";
  lastPayload["model"] = config.model;

  std::map<std::string, std::shared_ptr<mcp::Client>> clients;
  {
    std::lock_guard<std::mutex> lock(mcpMutex_);
    clients = mcpClients_;
  }
  std::map<std::string, agents::ToolSpec> byName;
  for (const auto& spec : config.tools) byName[spec.name] = spec;
  const bool autoApprove = settings().extra.value("mcpAutoApprove", false);
  const Json approvals = settings().extra.value("mcpApprovals", Json::object());
  const std::string directUrl = settings().ollamaUrl;

  async::runDetached([this, stream, config, clients, byName, autoApprove, approvals, directUrl] {
    agents::LoopHooks tool = makeToolHooks(stream, clients, byName, autoApprove, approvals);
    agents::AgentHooks hooks;
    hooks.approve = tool.approve;
    hooks.invoke = tool.invoke;
    hooks.onCallState = tool.onCallState;
    hooks.onUsage = tool.onUsage;
    hooks.onSteps = [stream](const std::vector<agents::AgentStep>& steps) {
      std::lock_guard<std::mutex> lock(stream->mutex);
      stream->started = true;
      stream->agentRun["steps"] = stepsToJson(steps);
    };
    hooks.onStatus = [stream](const std::string& status) {
      std::lock_guard<std::mutex> lock(stream->mutex);
      stream->agentRun["status"] = status;
      stream->status = status;
    };
    hooks.onDelta = [stream](const std::string& content, const std::string& reasoning) {
      std::lock_guard<std::mutex> lock(stream->mutex);
      stream->started = true;
      stream->content += content;
      stream->reasoning += reasoning;
      return !stream->cancel.load();
    };
    hooks.waitWhilePaused = [stream] {
      bool noted = false;
      while (stream->paused.load() && !stream->cancel.load()) {
        if (!noted) {
          std::lock_guard<std::mutex> lock(stream->mutex);
          stream->agentRun["status"] = "Paused";
          stream->status = "Paused";
          noted = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
      }
    };
    const agents::AgentResult result = agents::runAgent(api_, directUrl, config, hooks, &stream->cancel);
    std::lock_guard<std::mutex> lock(stream->mutex);
    stream->agentRun["finished"] = true;
    stream->agentRun["stepsDone"] = result.stepsDone;
    stream->agentRun["stepsFailed"] = result.stepsFailed;
    stream->agentRun["toolCalls"] = result.toolCalls;
    stream->agentRun["status"] = "";
    if (!result.ok && !stream->cancel.load()) stream->error = result.error;
    if (stream->cancel.load()) stream->agentRun["status"] = "Stopped";
    stream->done = true;
  });
}

void App::drawAgentCard(Message& message, float width) {
  const Json& run = message.agentRun;
  if (!run.is_object() || !run.contains("steps")) return;
  const auto& palette = theme::palette();
  std::shared_ptr<Stream> live = message.streaming ? streamFor(message.id) : nullptr;

  ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::toVec4(palette.panel));
  ImGui::PushStyleColor(ImGuiCol_Border, theme::toVec4(palette.border));
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
  ImGui::BeginChild("##agentrun", ImVec2(width, 0),
                    ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);

  // Header: brain icon, "Agent", status / summary, controls while running.
  icons::inlineIcon(icons::Icon::Brain, palette.accent);
  ImGui::SameLine(0, 6);
  theme::pushBold();
  ImGui::TextUnformatted("Agent");
  theme::pop();
  const Json& steps = run["steps"];
  const int total = steps.is_array() ? static_cast<int>(steps.size()) : 0;
  int done = 0, failed = 0, toolCalls = 0;
  if (steps.is_array()) {
    for (const auto& step : steps) {
      const int state = step.value("state", 0);
      if (state == agents::StepDone) ++done;
      if (state == agents::StepFailed) ++failed;
      toolCalls += step.value("toolCalls", 0);
    }
  }
  ImGui::SameLine();
  const std::string status = run.value("status", "");
  if (live && !status.empty()) {
    widgets::dimText("%s", status.c_str());
  } else {
    std::string summary = std::to_string(done) + "/" + std::to_string(total) + " steps done";
    if (failed) summary += ", " + std::to_string(failed) + " not completed";
    if (toolCalls) summary += " - " + std::to_string(toolCalls) + (toolCalls == 1 ? " tool call" : " tool calls");
    if (run.value("status", "") == "Stopped") summary += " - stopped";
    widgets::dimText("%s", summary.c_str());
  }
  if (live) {
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 150.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 2));
    const bool paused = live->paused.load();
    if (ImGui::SmallButton(paused ? "Resume" : "Pause")) live->paused.store(!paused);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pauses between steps; the current step finishes first");
    ImGui::SameLine();
    if (ImGui::SmallButton("Stop")) stopAllStreams();
    ImGui::PopStyleVar();
  }

  // Steps.
  if (total == 0) {
    widgets::dimText("Planning the steps...");
  }
  for (int i = 0; i < total; ++i) {
    const Json& step = steps[static_cast<size_t>(i)];
    const int state = step.value("state", 0);
    ImU32 color = palette.textFaint;
    switch (state) {
      case agents::StepRunning: color = palette.accent; break;
      case agents::StepDone: color = palette.success; break;
      case agents::StepFailed: color = palette.danger; break;
      default: break;
    }
    ImGui::PushID(i);
    const ImVec2 rowPos = ImGui::GetCursorScreenPos();
    const float rowHeight = ImGui::GetTextLineHeight() + 6.0f;
    const float innerWidth = ImGui::GetContentRegionAvail().x;
    const std::string key = message.id + "/" + std::to_string(i);
    if (ImGui::InvisibleButton("##step", ImVec2(innerWidth, rowHeight))) gExpanded[key] = !gExpanded[key];
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float midY = rowPos.y + rowHeight * 0.5f;
    if (state == agents::StepRunning) {
      const float t = static_cast<float>(ImGui::GetTime());
      dl->PathArcTo(ImVec2(rowPos.x + 8.0f, midY), 5.0f, t * 4.0f, t * 4.0f + 4.2f, 12);
      dl->PathStroke(color, 2.0f);
    } else if (state == agents::StepDone) {
      dl->AddCircleFilled(ImVec2(rowPos.x + 8.0f, midY), 5.0f, color);
      dl->AddLine(ImVec2(rowPos.x + 5.0f, midY), ImVec2(rowPos.x + 7.5f, midY + 2.5f), palette.bg, 1.6f);
      dl->AddLine(ImVec2(rowPos.x + 7.5f, midY + 2.5f), ImVec2(rowPos.x + 11.5f, midY - 2.5f), palette.bg, 1.6f);
    } else {
      dl->AddCircle(ImVec2(rowPos.x + 8.0f, midY), 4.5f, color, 0, state == agents::StepFailed ? 2.0f : 1.3f);
    }
    std::string label = std::to_string(i + 1) + ". " + step.value("title", "");
    const int attempts = step.value("attempts", 0);
    if (attempts > 1) label += "  (attempt " + std::to_string(attempts) + ")";
    const int calls = step.value("toolCalls", 0);
    if (calls > 0) label += "  - " + std::to_string(calls) + (calls == 1 ? " tool call" : " tool calls");
    dl->PushClipRect(rowPos, ImVec2(rowPos.x + innerWidth, rowPos.y + rowHeight), true);
    dl->AddText(ImVec2(rowPos.x + 22.0f, rowPos.y + 3.0f),
                state == agents::StepPending ? palette.textDim : palette.text, label.c_str());
    dl->PopClipRect();
    if (ImGui::IsItemHovered() && !step.value("result", "").empty()) {
      ImGui::SetTooltip("%s", gExpanded[key] ? "Hide the outcome" : "Show the outcome");
    }
    const std::string result = step.value("result", "");
    const std::string feedback = step.value("feedback", "");
    if (gExpanded[key] && (!result.empty() || !feedback.empty())) {
      ImGui::Indent(22.0f);
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + innerWidth - 30.0f);
      ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.textDim));
      if (!result.empty()) ImGui::TextUnformatted(result.c_str());
      ImGui::PopStyleColor();
      if (!feedback.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.warning));
        ImGui::TextWrapped("Check: %s", feedback.c_str());
        ImGui::PopStyleColor();
      }
      ImGui::PopTextWrapPos();
      ImGui::Unindent(22.0f);
    }
    ImGui::PopID();
  }
  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(2);
  ImGui::Dummy(ImVec2(0, 4));
}

void App::drawAgentPopover() {
  if (!ImGui::BeginPopup("##agentopts")) return;
  theme::pushBold();
  ImGui::TextUnformatted("Agent mode");
  theme::pop();
  widgets::dimText("Plan the request into steps, carry each out with your MCP tools,\ncheck the outcome and retry, then answer.");
  bool changed = false;
  int maxSteps = std::clamp(settings().extra.value("agentMaxSteps", 6), 1, 12);
  ImGui::SetNextItemWidth(200);
  if (ImGui::SliderInt("Steps at most", &maxSteps, 1, 12)) {
    settings().extra["agentMaxSteps"] = maxSteps;
    changed = true;
  }
  int attempts = std::clamp(settings().extra.value("agentMaxAttempts", 2), 1, 4);
  ImGui::SetNextItemWidth(200);
  if (ImGui::SliderInt("Attempts per step", &attempts, 1, 4)) {
    settings().extra["agentMaxAttempts"] = attempts;
    changed = true;
  }
  bool verify = settings().extra.value("agentVerify", true);
  if (ImGui::Checkbox("Check each step's outcome before moving on", &verify)) {
    settings().extra["agentVerify"] = verify;
    changed = true;
  }
  widgets::dimText("Tool-call rounds per step follow the MCP setting (%d).",
                   std::max(1, settings().extra.value("mcpMaxRounds", 8)));
  if (changed) settings().save();
  ImGui::EndPopup();
}

}  // namespace app
