// Memory proposals (#107): the prompt that asks a model for durable facts
// about the user in a turn, the lenient parsing of its answer, and the
// filtering against what is already remembered or was skipped before.
// App-free; --memorytest covers it. The app asks, shows the candidates under
// the reply, and saves only what the user accepts.
#pragma once

#include <string>
#include <vector>

namespace memoryprop {

std::string systemPrompt();
// `existing` are memories already saved (so the model does not repeat them).
std::string userPrompt(const std::string& userText, const std::string& assistantText, const std::vector<std::string>& existing);

// Pulls the list out of whatever the model wrote: {"memories": [...]}, a bare
// JSON array, fenced JSON, a <think> prefix, or "- " bullet lines.
std::vector<std::string> parse(const std::string& reply);

// Lowercased, punctuation and filler stripped, for comparisons.
std::string normalize(const std::string& text);
// True when the two say the same thing (containment or a high word overlap).
bool similar(const std::string& a, const std::string& b);
// Drops empties, near-duplicates of `existing` or `skipped`, duplicates of
// each other, over-long and under-short ones; keeps at most `maxCount`.
std::vector<std::string> filter(const std::vector<std::string>& candidates, const std::vector<std::string>& existing,
                                const std::vector<std::string>& skipped, size_t maxCount = 3);

}  // namespace memoryprop
