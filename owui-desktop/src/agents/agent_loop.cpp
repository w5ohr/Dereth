#include "agent_loop.h"

#include <algorithm>

#include "core/common.h"
#include "core/jsonschema.h"

namespace agents {
namespace {

bool cancelled(const std::atomic<bool>* cancel) { return cancel && cancel->load(); }

std::string numbered(const std::vector<AgentStep>& steps) {
  std::string out;
  for (size_t i = 0; i < steps.size(); ++i) {
    out += std::to_string(i + 1) + ". " + steps[i].title;
    if (steps[i].state == StepDone) {
      out += "  [done] " + steps[i].result;
    } else if (steps[i].state == StepFailed) {
      out += "  [not completed] " + steps[i].result;
      if (!steps[i].feedback.empty()) out += " (" + steps[i].feedback + ")";
    }
    out += "\n";
  }
  return out;
}

std::string clip(const std::string& s, size_t limit) {
  return s.size() > limit ? s.substr(0, limit) + "..." : s;
}

// One model call through the tool loop, returning everything it said. With
// `tools` empty it is a plain completion; `format` constrains the output.
struct CallOutcome {
  std::string text;
  bool ok = false;
  std::string error;
  int toolCalls = 0;
};

CallOutcome ask(api::Client& api, const std::string& directUrl, const AgentConfig& config,
                const AgentHooks& hooks, const Json& messages, const std::vector<ToolSpec>& tools,
                const Json& format, int maxRounds, const std::atomic<bool>* cancel,
                const std::function<bool(const std::string&, const std::string&)>& onDelta) {
  LoopConfig loop;
  loop.model = config.model;
  loop.messages = messages;
  loop.tools = tools;
  loop.options = config.options;
  loop.systemPrefix = config.systemPrefix;
  loop.maxRounds = maxRounds;
  loop.format = format;

  CallOutcome out;
  LoopHooks lh;
  lh.onDelta = [&](const std::string& content, const std::string& reasoning) {
    out.text += content;
    if (onDelta) return onDelta(content, reasoning);
    return !cancelled(cancel);
  };
  // Every tool loop numbers its calls from 1; prefix them per call so the
  // cards of one step do not overwrite the cards of the one before.
  static std::atomic<int> askSeq{0};
  const std::string prefix = "s" + std::to_string(++askSeq) + "_";
  auto renamed = [prefix](const Call& call) {
    Call copy = call;
    copy.id = prefix + call.id;
    return copy;
  };
  if (hooks.approve) {
    lh.approve = [&hooks, renamed](const Call& call) { return hooks.approve(renamed(call)); };
  }
  lh.invoke = hooks.invoke;
  lh.onCallState = [&, renamed](const Call& call, CallState state, const ToolOutcome* outcome) {
    if (state == Running) ++out.toolCalls;
    if (hooks.onCallState) hooks.onCallState(renamed(call), state, outcome);
  };
  lh.onUsage = hooks.onUsage;
  const LoopResult result = runToolLoop(api, directUrl, loop, lh, cancel);
  out.ok = result.ok;
  out.error = result.error;
  return out;
}

Json withUser(const Json& history, const std::string& text) {
  Json messages = history.is_array() ? history : Json::array();
  messages.push_back(Json{{"role", "user"}, {"content", text}});
  return messages;
}

}  // namespace

Json planSchema(int maxSteps) {
  return Json{{"type", "object"},
              {"properties", Json{{"steps", Json{{"type", "array"},
                                                  {"items", Json{{"type", "string"}}},
                                                  {"minItems", 1},
                                                  {"maxItems", maxSteps}}}}},
              {"required", Json::array({"steps"})}};
}

std::vector<std::string> parsePlan(const std::string& text, int maxSteps) {
  std::vector<std::string> steps;
  Json value;
  if (schema::extractJson(text, &value)) {
    const Json* list = nullptr;
    if (value.is_array()) list = &value;
    else if (value.is_object() && value.contains("steps") && value["steps"].is_array()) list = &value["steps"];
    if (list) {
      for (const auto& item : *list) {
        std::string title;
        if (item.is_string()) title = item;
        else if (item.is_object()) title = item.value("title", item.value("step", item.value("description", "")));
        title = core::trim(title);
        if (!title.empty()) steps.push_back(title);
        if (static_cast<int>(steps.size()) >= maxSteps) break;
      }
    }
  }
  if (steps.empty()) {
    // No JSON: take numbered or bulleted lines.
    for (const std::string& raw : core::split(text, '\n')) {
      std::string line = core::trim(raw);
      size_t i = 0;
      while (i < line.size() && (std::isdigit(static_cast<unsigned char>(line[i])) || line[i] == '.' ||
                                 line[i] == ')' || line[i] == '-' || line[i] == '*' || line[i] == ' ')) {
        ++i;
      }
      if (i == 0 || i >= line.size()) continue;
      steps.push_back(line.substr(i));
      if (static_cast<int>(steps.size()) >= maxSteps) break;
    }
  }
  return steps;
}

bool parseVerdict(const std::string& text, bool* done, std::string* feedback) {
  Json value;
  if (schema::extractJson(text, &value) && value.is_object() && value.contains("done")) {
    const Json& d = value["done"];
    *done = d.is_boolean() ? d.get<bool>() : (d.is_string() && core::lower(d.get<std::string>()) == "true");
    *feedback = value.value("feedback", "");
    return true;
  }
  const std::string lowered = core::lower(core::trim(text));
  if (core::startsWith(lowered, "yes") || core::startsWith(lowered, "done")) {
    *done = true;
    *feedback = "";
    return true;
  }
  if (core::startsWith(lowered, "no")) {
    *done = false;
    *feedback = text;
    return true;
  }
  return false;
}

AgentResult runAgent(api::Client& api, const std::string& directUrl, const AgentConfig& config,
                     const AgentHooks& hooks, const std::atomic<bool>* cancel) {
  AgentResult result;
  std::vector<AgentStep> steps;
  auto publish = [&] { if (hooks.onSteps) hooks.onSteps(steps); };
  auto status = [&](const std::string& s) { if (hooks.onStatus) hooks.onStatus(s); };
  auto pause = [&] { if (hooks.waitWhilePaused) hooks.waitWhilePaused(); };
  auto bail = [&](const std::string& error) {
    result.error = error;
    result.ok = false;
    return result;
  };

  // ---- plan
  status("Planning...");
  {
    const std::string ask_ =
        "Goal:\n" + config.goal +
        "\n\nBreak this goal into the concrete steps you will carry out yourself, in order, using the tools "
        "you have when they help. Keep it to the fewest steps that get the job done (a simple goal is one "
        "step) and at most " + std::to_string(config.maxSteps) +
        ". Reply with JSON only: {\"steps\": [\"step 1\", \"step 2\"]}";
    const CallOutcome plan = ask(api, directUrl, config, hooks, withUser(config.history, ask_), {},
                                 planSchema(config.maxSteps), 1, cancel, nullptr);
    if (cancelled(cancel)) return bail("stopped");
    if (!plan.ok) return bail(plan.error.empty() ? "planning failed" : plan.error);
    for (const std::string& title : parsePlan(plan.text, config.maxSteps)) {
      AgentStep step;
      step.title = title;
      steps.push_back(step);
    }
    if (steps.empty()) {
      AgentStep step;
      step.title = clip(config.goal, 200);
      steps.push_back(step);
    }
    publish();
  }

  // ---- act, verify, retry
  const Json verdictSchema{{"type", "object"},
                           {"properties", Json{{"done", Json{{"type", "boolean"}}},
                                               {"feedback", Json{{"type", "string"}}}}},
                           {"required", Json::array({"done", "feedback"})}};
  for (size_t i = 0; i < steps.size(); ++i) {
    pause();
    if (cancelled(cancel)) return bail("stopped");
    AgentStep& step = steps[i];
    std::string feedback;
    for (int attempt = 1; attempt <= std::max(1, config.maxAttempts); ++attempt) {
      step.state = StepRunning;
      step.attempts = attempt;
      publish();
      status("Step " + std::to_string(i + 1) + "/" + std::to_string(steps.size()) + ": " + step.title);

      std::string instruction =
          "Goal:\n" + config.goal + "\n\nPlan:\n" + numbered(steps) +
          "\nCarry out step " + std::to_string(i + 1) + " now: " + step.title +
          "\nUse the tools when they are needed, one call at a time, and read their results. When the step is "
          "finished, report its outcome in a few plain sentences with the concrete facts (numbers, names, file "
          "paths, text produced) that later steps or the final answer will need. Do not ask the user questions; "
          "make a reasonable choice and say which. Do not start other steps.";
      if (!feedback.empty()) {
        instruction += "\n\nYour previous attempt at this step was judged incomplete: " + feedback +
                       "\nFix exactly that.";
      }
      const CallOutcome act = ask(api, directUrl, config, hooks, withUser(config.history, instruction),
                                  config.tools, Json(), config.maxRoundsPerStep, cancel, nullptr);
      step.toolCalls += act.toolCalls;
      result.toolCalls += act.toolCalls;
      if (cancelled(cancel)) {
        step.state = StepFailed;
        publish();
        return bail("stopped");
      }
      if (!act.ok) {
        step.state = StepFailed;
        step.result = act.error.empty() ? "the model call failed" : act.error;
        publish();
        break;
      }
      step.result = clip(core::trim(act.text), 4000);

      if (!config.verify) {
        step.state = StepDone;
        break;
      }
      pause();
      status("Checking step " + std::to_string(i + 1) + "...");
      const std::string check =
          "Goal:\n" + config.goal + "\n\nStep: " + step.title + "\n\nReported outcome:\n" + step.result +
          "\n\nJudge strictly whether this step was genuinely completed: an outcome that only promises, asks the "
          "user, or skips the work is not done. Reply with JSON only: {\"done\": true|false, \"feedback\": "
          "\"what is missing, in one sentence, or empty\"}";
      const CallOutcome verdict = ask(api, directUrl, config, hooks, withUser(Json::array(), check), {},
                                      verdictSchema, 1, cancel, nullptr);
      bool done = true;
      std::string why;
      if (verdict.ok && parseVerdict(verdict.text, &done, &why) && !done) {
        feedback = why.empty() ? "the step was not completed" : why;
        step.feedback = feedback;
        if (attempt < config.maxAttempts) {
          publish();
          continue;   // retry with the feedback
        }
        step.state = StepFailed;
        break;
      }
      step.feedback.clear();
      step.state = StepDone;
      break;
    }
    if (step.state == StepDone) ++result.stepsDone;
    else if (step.state == StepFailed) ++result.stepsFailed;
    publish();
  }

  // ---- answer
  pause();
  if (cancelled(cancel)) return bail("stopped");
  status("Writing the answer...");
  const std::string wrap =
      "Goal:\n" + config.goal + "\n\nSteps carried out and their outcomes:\n" + numbered(steps) +
      "\nWrite the final answer for the user: what was done and the results, with the concrete facts from the "
      "outcomes. If something could not be completed, say so plainly and what is left. Do not list the steps "
      "mechanically; answer the goal.";
  const CallOutcome answer = ask(api, directUrl, config, hooks, withUser(config.history, wrap), {}, Json(), 1,
                                 cancel, hooks.onDelta);
  if (cancelled(cancel)) return bail("stopped");
  if (!answer.ok) return bail(answer.error.empty() ? "the final answer failed" : answer.error);
  result.ok = true;
  status("");
  return result;
}

}  // namespace agents
