// A small JSON Schema validator (draft 2020-12 subset) for structured output
// mode: enough to check what a model sends back against a schema the user
// wrote, with readable messages. Supported: type (single or list), enum,
// const, properties, required, additionalProperties, items, minItems,
// maxItems, uniqueItems, minLength, maxLength, pattern, minimum, maximum,
// exclusiveMinimum, exclusiveMaximum, multipleOf, anyOf, oneOf, allOf, not,
// and local $ref into $defs / definitions. Unknown keywords are ignored.
#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "common.h"

namespace schema {

using Json = nlohmann::json;

// Returns the violations, empty when `value` matches. Each one names the
// path ("$.items[2].price") and what went wrong.
std::vector<std::string> validate(const Json& schema, const Json& value);

// Checks that a schema is usable: an object, valid types, resolvable $refs.
// Returns an empty string when it is.
std::string check(const Json& schema);

// Guesses a schema from an example value: object properties become required
// with their observed types, arrays take the schema of their first element.
Json infer(const Json& example);

// Pulls the JSON value out of a reply: the whole text, a fenced ```json
// block, or the first balanced {...} / [...] in it. False when none parses.
bool extractJson(const std::string& text, Json* out);

}  // namespace schema
