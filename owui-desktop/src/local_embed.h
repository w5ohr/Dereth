// The local embedding call (nomic-embed-text through the Ollama URL) that the
// local file index uses, exposed for the chat index.
#pragma once

#include <string>
#include <vector>

namespace app {

// Blocking. Unit-length vectors, one per input; false with `error` set when
// Ollama is unreachable or the model is missing.
bool embedTexts(const std::vector<std::string>& inputs, std::vector<std::vector<float>>* out, std::string* error);
std::string embedModelName();

}  // namespace app
