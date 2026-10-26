// Recipes: a saved chain of prompts. Each step's prompt may use {{prev}}
// (the previous step's reply), the prompt variables ({{clipboard}},
// {{date}}, {{input:Label}} ...) and {{step:N}} (an earlier step's reply).
// Inputs are asked for once, before the first step. App-free; --recipetest
// covers parsing and expansion.
#pragma once

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "promptvars.h"

namespace recipes {

using Json = nlohmann::json;

struct Step {
  std::string prompt;
  std::string model;        // empty = the recipe's / current model
};

struct Recipe {
  std::string id;
  std::string name;
  std::string description;
  std::string model;        // default model for every step; empty = whatever is selected
  std::vector<Step> steps;
  bool newChatPerRun = true;

  Json toJson() const;
  static Recipe fromJson(const Json& j);
};

// Every distinct {{input:Label}} across the steps, in first-seen order.
std::vector<promptvars::Var> inputs(const Recipe& recipe);

// Expands one step: {{prev}} and {{step:N}} from `replies` (1-based; prev is
// the last), then the prompt variables with `ctx`.
std::string expandStep(const std::string& prompt, const std::vector<std::string>& replies,
                       const promptvars::Context& ctx);

// Recipes a user might start from.
std::vector<Recipe> examples();

}  // namespace recipes
