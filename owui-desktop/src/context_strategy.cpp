// Context strategy in the app: what happens when a chat outgrows the model's
// window. The strategy and its knobs live in the chat (chat.raw
// ["desktop_context"], defaulting to the settings), buildCompletionPayload
// applies the plan to the outgoing messages, a rolling summary is produced
// in the background after a turn that needed one, and the ctx label in the
// chat header opens a popover that shows exactly what the next send would
// drop or fold.
#include "app.h"

#include <algorithm>

#include "core/async.h"
#include "core/common.h"
#include "core/contextfit.h"
#include "imgui_stdlib.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

using contextfit::Strategy;

constexpr int kDefaultReserve = 1024;    // tokens kept free for the reply when max_tokens is unset
constexpr int kSummaryMinFold = 6;       // never summarise fewer messages than this

std::string excerpt(const std::string& text, size_t limit) {
  std::string out;
  for (char c : text) {
    if (c == '\n' || c == '\t') c = ' ';
    out += c;
  }
  out = core::trim(out);
  if (out.size() > limit) out = out.substr(0, limit - 1) + "...";
  return out;
}

}  // namespace

// ---------------------------------------------------------------- config

Json& App::contextConfig() {
  if (!chat.raw.is_object()) chat.raw = Json::object();
  Json& cfg = chat.raw["desktop_context"];
  if (!cfg.is_object()) cfg = Json::object();
  return cfg;
}

std::string App::contextStrategyKey() const {
  if (chat.raw.is_object() && chat.raw.contains("desktop_context") && chat.raw["desktop_context"].is_object()) {
    const std::string own = chat.raw["desktop_context"].value("strategy", "");
    if (!own.empty()) return own;
  }
  return core::Settings::instance().extra.value("contextStrategy", "drop");
}

int App::contextWindowTurns() const {
  if (chat.raw.is_object() && chat.raw.contains("desktop_context") && chat.raw["desktop_context"].is_object() &&
      chat.raw["desktop_context"].contains("windowTurns")) {
    return std::clamp(chat.raw["desktop_context"].value("windowTurns", 6), 1, 50);
  }
  return std::clamp(core::Settings::instance().extra.value("contextWindowTurns", 6), 1, 50);
}

// Tokens the request may use: the model's window less the reply reserve and
// the system prompt the server adds from the chat params.
int App::contextBudget() const {
  const std::string model = chat.models.empty() ? (selectedModels.empty() ? "" : selectedModels.front())
                                                : chat.models.front();
  int window = std::max(1024, contextWindowFor(model));
  int reserve = kDefaultReserve;
  if (chat.params.contains("max_tokens") && chat.params["max_tokens"].is_number()) {
    reserve = std::max(64, chat.params["max_tokens"].get<int>());
  }
  int system = 0;
  if (chat.params.contains("system") && chat.params["system"].is_string()) {
    system = estimateTokens(chat.params["system"].get<std::string>());
  }
  return std::max(512, window - reserve - system - 128);
}

// The cached rolling summary: {upto: <message id it covers through>, text, tokens}.
Json App::contextSummary() const {
  if (chat.raw.is_object() && chat.raw.contains("desktop_context_summary") &&
      chat.raw["desktop_context_summary"].is_object()) {
    return chat.raw["desktop_context_summary"];
  }
  return Json();
}

// ---------------------------------------------------------------- planning

// Plans the fit for an OpenAI-shaped message list; `ids` (optional, parallel)
// lets the cached summary be matched to the messages it covers.
contextfit::Plan App::planContext(const Json& messages, const std::vector<std::string>& ids) const {
  std::vector<contextfit::Msg> list;
  for (size_t i = 0; i < messages.size(); ++i) {
    const Json& m = messages[i];
    contextfit::Msg msg;
    msg.role = m.value("role", "user");
    const Json& content = m.contains("content") ? m["content"] : Json("");
    if (content.is_string()) {
      msg.tokens = estimateTokens(content.get<std::string>());
    } else if (content.is_array()) {
      for (const auto& part : content) {
        if (part.value("type", "") == "text") msg.tokens += estimateTokens(part.value("text", ""));
        else msg.tokens += 256;   // an image
      }
    }
    msg.id = i < ids.size() ? ids[i] : "";
    list.push_back(msg);
  }
  const Strategy strategy = contextfit::strategyFromKey(contextStrategyKey());
  int covers = 0, summaryTokens = 0;
  const Json summary = contextSummary();
  if (strategy == Strategy::Summary && summary.is_object()) {
    const std::string upto = summary.value("upto", "");
    for (size_t i = 0; i < list.size(); ++i) {
      if (!list[i].id.empty() && list[i].id == upto) covers = static_cast<int>(i) + 1;
    }
    summaryTokens = summary.value("tokens", estimateTokens(summary.value("text", "")));
  }
  return contextfit::fit(list, contextBudget(), strategy, contextWindowTurns(), covers, summaryTokens);
}

// Rewrites `messages` per the plan; returns what changed for the status line.
std::string App::applyContextPlan(Json* messages, const std::vector<std::string>& ids) {
  lastContextPlan = Json();
  if (!messages->is_array() || messages->empty()) return "";
  const contextfit::Plan plan = planContext(*messages, ids);
  if (plan.cut.empty()) return "";
  Json kept = Json::array();
  bool summaryInserted = false;
  for (int index : plan.keep) {
    if (plan.summaryUsed && !summaryInserted) {
      // The summary stands where the folded prefix was: before the first kept non-system message.
      const Json& m = (*messages)[static_cast<size_t>(index)];
      if (m.value("role", "") != "system") {
        kept.push_back(Json{{"role", "system"},
                            {"content", "Summary of the earlier part of this conversation (older turns were "
                                        "folded to fit the model's context):\n" + contextSummary().value("text", "")}});
        summaryInserted = true;
      }
    }
    kept.push_back((*messages)[static_cast<size_t>(index)]);
  }
  if (plan.summaryUsed && !summaryInserted) {
    kept.insert(kept.begin(), Json{{"role", "system"}, {"content", contextSummary().value("text", "")}});
  }
  *messages = kept;
  // Remember which message ids were cut, for the rolling summary and the inspector.
  Json cutIds = Json::array();
  for (int index : plan.cut) {
    if (static_cast<size_t>(index) < ids.size()) cutIds.push_back(ids[static_cast<size_t>(index)]);
  }
  lastContextPlan = Json{{"strategy", contextStrategyKey()},
                         {"cut", static_cast<int>(plan.cut.size())},
                         {"cutIds", cutIds},
                         {"summaryUsed", plan.summaryUsed},
                         {"tokensBefore", plan.tokensBefore},
                         {"tokensAfter", plan.tokensAfter},
                         {"budget", plan.budget}};
  return std::to_string(plan.cut.size()) + (plan.summaryUsed ? " older messages folded into the summary"
                                                              : " older messages left out to fit the context");
}

// ---------------------------------------------------------------- rolling summary

// After a turn that cut messages under the summary strategy, fold the cut
// prefix (plus any earlier summary) into a fresh summary for the next turn.
void App::maybeSummariseContext() {
  if (chat.isPrivate || chat.id.empty()) return;
  if (contextfit::strategyFromKey(contextStrategyKey()) != Strategy::Summary) return;
  if (!lastContextPlan.is_object() || lastContextPlan.value("cut", 0) < kSummaryMinFold) return;
  if (contextSummaryPending_) return;
  const Json cutIds = lastContextPlan.value("cutIds", Json::array());
  if (!cutIds.is_array() || cutIds.empty()) return;
  const Json existing = contextSummary();
  const std::string coveredUpto = existing.is_object() ? existing.value("upto", "") : "";
  const std::string newestCut = cutIds.back();
  if (newestCut == coveredUpto) return;   // the summary already reaches this far

  // Transcript of what the summary has to stand for: the old summary (if
  // any) followed by the cut messages it does not cover yet.
  std::string transcript;
  if (existing.is_object()) transcript += "Earlier summary:\n" + existing.value("text", "") + "\n\n";
  bool pastCovered = coveredUpto.empty();
  for (const auto& idJson : cutIds) {
    const std::string id = idJson.get<std::string>();
    if (!pastCovered) {
      if (id == coveredUpto) pastCovered = true;
      continue;
    }
    auto it = chat.messages.find(id);
    if (it == chat.messages.end()) continue;
    transcript += (it->second.role == "user" ? "User: " : "Assistant: ") + it->second.content.substr(0, 6000) + "\n\n";
  }
  if (core::trim(transcript).empty()) return;

  const std::string model = chat.models.empty() ? (selectedModels.empty() ? "" : selectedModels.front())
                                                : chat.models.front();
  if (model.empty()) return;
  const Json payload{
      {"model", model},
      {"stream", false},
      {"messages", Json::array({
          Json{{"role", "system"},
               {"content", "You condense conversations. Write a dense summary of the exchange below in at most 300 "
                           "words: decisions, facts, names, numbers, code or file names mentioned, open questions, "
                           "and the user's goals and preferences. Plain prose or short bullets. No preamble."}},
          Json{{"role", "user"}, {"content", transcript}}})},
  };
  contextSummaryPending_ = true;
  const std::string chatId = chat.id;
  async::run([this, payload, chatId, newestCut] {
    api::Result res = api_.completion(payload);
    async::post([this, res, chatId, newestCut] {
      contextSummaryPending_ = false;
      if (chat.id != chatId) return;
      if (!res.ok) {
        widgets::toastError("Context summary failed: " + api::errorMessage(res));
        return;
      }
      std::string text;
      const Json& choices = res.data.value("choices", Json::array());
      if (!choices.empty()) text = choices[0].value("message", Json::object()).value("content", "");
      // A thinking model may wrap its notes in <think>; keep only the answer.
      const size_t think = text.rfind("</think>");
      if (think != std::string::npos) text = text.substr(think + 8);
      text = core::trim(text);
      if (text.empty()) return;
      chat.raw["desktop_context_summary"] = Json{{"upto", newestCut}, {"text", text}, {"tokens", estimateTokens(text)}};
      saveActiveChat();
      widgets::toastInfo("Earlier turns summarised for the next message (" + std::to_string(estimateTokens(text)) + " tokens)");
    });
  });
}

// ---------------------------------------------------------------- popover

void App::drawContextPopover() {
  if (!ImGui::BeginPopup("##contextplan")) return;
  const auto& palette = theme::palette();
  const float width = 560.0f;
  theme::pushBold();
  ImGui::TextUnformatted("When this chat outgrows the model's context");
  theme::pop();

  Json& cfg = contextConfig();
  bool changed = false;
  const Strategy current = contextfit::strategyFromKey(contextStrategyKey());
  for (Strategy s : {Strategy::DropOldest, Strategy::Window, Strategy::Summary}) {
    if (ImGui::RadioButton(contextfit::strategyLabel(s), current == s)) {
      cfg["strategy"] = contextfit::strategyKey(s);
      changed = true;
    }
  }
  if (current == Strategy::Window) {
    int turns = contextWindowTurns();
    ImGui::SetNextItemWidth(220);
    if (ImGui::SliderInt("Turns kept", &turns, 1, 50)) {
      cfg["windowTurns"] = turns;
      changed = true;
    }
  }
  if (current == Strategy::Summary) {
    const Json summary = contextSummary();
    if (summary.is_object()) {
      widgets::dimText("Summary on file: %d tokens, written after the turn that first overflowed.",
                       summary.value("tokens", 0));
      if (ImGui::TreeNode("Show summary")) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
        ImGui::TextUnformatted(summary.value("text", "").c_str());
        ImGui::PopTextWrapPos();
        ImGui::TreePop();
      }
    } else {
      widgets::dimText("The summary is written in the background after the first turn that overflows;\n"
                       "that turn itself drops the oldest messages.");
    }
    if (contextSummaryPending_) widgets::dimText("Summarising...");
  }
  if (ImGui::Checkbox("Use this as the default for new chats", &contextDefaultTouched_)) {
    settings().extra["contextStrategy"] = contextfit::strategyKey(current);
    settings().extra["contextWindowTurns"] = contextWindowTurns();
    settings().save();
    contextDefaultTouched_ = false;
    widgets::toastInfo("Default context strategy saved");
  }
  ImGui::Separator();

  // Preview: what the next send would do with the branch as it stands.
  const std::vector<std::string> path = chat.branch();
  Json messages = Json::array();
  std::vector<std::string> ids;
  for (const std::string& id : path) {
    auto it = chat.messages.find(id);
    if (it == chat.messages.end() || (it->second.role == "assistant" && it->second.content.empty())) continue;
    messages.push_back(Json{{"role", it->second.role}, {"content", it->second.content}});
    ids.push_back(id);
  }
  if (!core::trim(composer).empty()) {
    messages.push_back(Json{{"role", "user"}, {"content", composer}});
    ids.push_back("");
  }
  const contextfit::Plan plan = planContext(messages, ids);
  const std::string model = chat.models.empty() ? (selectedModels.empty() ? "" : selectedModels.front())
                                                : chat.models.front();
  ImGui::Text("Model window ~%dk, budget for the request %d tokens, this branch ~%d.",
              std::max(1, contextWindowFor(model)) / 1000, plan.budget, plan.tokensBefore);
  if (plan.cut.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.success));
    ImGui::TextUnformatted("Everything fits - the next send goes out whole.");
    ImGui::PopStyleColor();
  } else {
    ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.warning));
    ImGui::Text("%zu of %zu messages would be %s, leaving ~%d tokens.", plan.cut.size(), messages.size(),
                plan.summaryUsed ? "folded into the summary" : "left out", plan.tokensAfter);
    ImGui::PopStyleColor();
  }
  ImGui::BeginChild("##ctxpreview", ImVec2(width, 220), ImGuiChildFlags_Borders);
  std::vector<bool> cut(messages.size(), false);
  for (int index : plan.cut) cut[static_cast<size_t>(index)] = true;
  for (size_t i = 0; i < messages.size(); ++i) {
    const Json& m = messages[i];
    const std::string role = m.value("role", "");
    const std::string text = m.value("content", "");
    const int tokens = estimateTokens(text);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(cut[i] ? palette.textFaint : palette.text));
    ImGui::Text("%s%-9s %5d  %s", cut[i] ? "- " : "  ", role.c_str(), tokens, excerpt(text, 60).c_str());
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", cut[i] ? (plan.summaryUsed ? "folded into the summary" : "left out of the next send")
                                     : "sent as is");
    }
  }
  ImGui::EndChild();
  widgets::dimText("System messages and the newest user turn are always kept. Open \"What was sent\" after a\n"
                   "reply to see the exact request.");
  if (changed && !chat.id.empty()) saveActiveChat();
  ImGui::EndPopup();
}

}  // namespace app
