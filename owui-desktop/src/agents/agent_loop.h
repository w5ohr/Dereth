// The agent loop: "do this until done". Plan the goal into steps, carry each
// step out with the tool loop, have the model check the step's outcome and
// retry with its feedback, then write the answer. UI-agnostic like the tool
// loop: the app drives it through hooks (step snapshots, status, pause,
// approvals) and can run it headless.
#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "tool_loop.h"

namespace agents {

enum StepState { StepPending = 0, StepRunning = 1, StepDone = 2, StepFailed = 3, StepSkipped = 4 };

struct AgentStep {
  std::string title;
  int state = StepPending;
  std::string result;        // what the model reported when the step finished
  std::string feedback;      // the verifier's objection, when it had one
  int attempts = 0;
  int toolCalls = 0;
};

struct AgentConfig {
  std::string model;                 // an Ollama model name
  Json history;                      // OpenAI-shaped messages before the goal
  std::string goal;                  // the user's request
  std::vector<ToolSpec> tools;
  Json options = Json::object();
  std::string systemPrefix;
  int maxSteps = 6;                  // the plan is cut here
  int maxAttempts = 2;               // per step, including the first
  int maxRoundsPerStep = 8;          // tool-call rounds per attempt
  bool verify = true;                // judge every step's outcome
};

struct AgentHooks {
  // The whole step list, whenever anything about it changes.
  std::function<void(const std::vector<AgentStep>&)> onSteps;
  // Short phase text for the UI: "Planning...", "Step 2/4: ...".
  std::function<void(const std::string& status)> onStatus;
  // The final answer as it streams. Return false to stop.
  std::function<bool(const std::string& content, const std::string& reasoning)> onDelta;
  // Blocks while the user has paused the run; returns at once when cancelled.
  std::function<void()> waitWhilePaused;
  // Forwarded to the tool loop.
  std::function<int(const Call&)> approve;
  std::function<ToolOutcome(const Call&)> invoke;
  std::function<void(const Call&, CallState, const ToolOutcome* outcome)> onCallState;
  std::function<void(const Json& usage)> onUsage;
};

struct AgentResult {
  bool ok = false;
  std::string error;
  int stepsDone = 0;
  int stepsFailed = 0;
  int toolCalls = 0;
};

AgentResult runAgent(api::Client& api, const std::string& directOllamaUrl, const AgentConfig& config,
                     const AgentHooks& hooks, const std::atomic<bool>* cancel);

// Exposed for the self-test: the plan the model is asked for, and its parsing.
Json planSchema(int maxSteps);
std::vector<std::string> parsePlan(const std::string& text, int maxSteps);
bool parseVerdict(const std::string& text, bool* done, std::string* feedback);

}  // namespace agents
