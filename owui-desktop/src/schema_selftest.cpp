// `Open WebUI --schematest`: offline checks of the JSON Schema validator,
// the schema inference and the JSON extraction behind structured output
// mode. No server needed.
#include <cstdio>
#include <string>
#include <vector>

#include "core/common.h"
#include "core/jsonschema.h"

using Json = nlohmann::json;

namespace {

int gPassed = 0, gFailed = 0;

void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ",
         detail.c_str());
  (ok ? gPassed : gFailed)++;
}

std::string joined(const std::vector<std::string>& errors) {
  std::string out;
  for (const std::string& e : errors) {
    if (!out.empty()) out += " | ";
    out += e;
  }
  return out;
}

// Expects `value` to produce exactly `expected` violations, each containing
// the matching needle.
void expect(const std::string& name, const Json& schema, const Json& value,
            const std::vector<std::string>& needles) {
  const std::vector<std::string> errors = schema::validate(schema, value);
  bool ok = errors.size() == needles.size();
  for (size_t i = 0; ok && i < needles.size(); ++i) {
    ok = errors[i].find(needles[i]) != std::string::npos;
  }
  report(name, ok, ok ? (errors.empty() ? "" : joined(errors)) : "got: " + joined(errors));
}

}  // namespace

int runSchemaSelfTest() {
  printf("Open WebUI desktop schema self-test\n\n");
  using json = nlohmann::json;

  const Json person = json::parse(R"({
    "type": "object",
    "properties": {
      "name": {"type": "string", "minLength": 1},
      "age": {"type": "integer", "minimum": 0, "maximum": 150},
      "email": {"type": "string", "pattern": "^[^@]+@[^@]+$"},
      "tags": {"type": "array", "items": {"type": "string"}, "uniqueItems": true, "maxItems": 3},
      "mood": {"type": "string", "enum": ["happy", "sad"]}
    },
    "required": ["name", "age"],
    "additionalProperties": false
  })");

  printf("validate\n");
  expect("valid object", person, json::parse(R"({"name":"Ann","age":40,"email":"a@b","tags":["x","y"],"mood":"happy"})"), {});
  expect("missing required", person, json::parse(R"({"name":"Ann"})"), {"missing required \"age\""});
  expect("wrong type", person, json::parse(R"({"name":"Ann","age":"forty"})"), {"$.age: expected integer, got string"});
  expect("float for integer", person, json::parse(R"({"name":"Ann","age":40.5})"), {"$.age: expected integer"});
  expect("whole float passes integer", person, json::parse(R"({"name":"Ann","age":40.0})"), {});
  expect("minimum", person, json::parse(R"({"name":"Ann","age":-1})"), {"$.age: -1 is below 0"});
  expect("maximum", person, json::parse(R"({"name":"Ann","age":200})"), {"$.age: 200 is above 150"});
  expect("minLength", person, json::parse(R"({"name":"","age":1})"), {"$.name: shorter than 1"});
  expect("pattern", person, json::parse(R"({"name":"Ann","age":1,"email":"nope"})"), {"$.email: does not match"});
  expect("enum", person, json::parse(R"({"name":"Ann","age":1,"mood":"meh"})"), {"$.mood: \"meh\" is not one of \"happy\", \"sad\""});
  expect("array item type", person, json::parse(R"({"name":"Ann","age":1,"tags":["x",2]})"), {"$.tags[1]: expected string"});
  expect("uniqueItems", person, json::parse(R"({"name":"Ann","age":1,"tags":["x","x"]})"), {"$.tags[1]: duplicate"});
  expect("maxItems", person, json::parse(R"({"name":"Ann","age":1,"tags":["a","b","c","d"]})"), {"$.tags: more than 3 items"});
  expect("additionalProperties", person, json::parse(R"({"name":"Ann","age":1,"extra":true})"), {"unexpected property \"extra\""});
  expect("several at once", person, json::parse(R"({"age":"x","mood":"meh"})"),
         {"missing required \"name\"", "$.age: expected integer", "$.mood:"});
  expect("root type", person, json::parse(R"([1,2])"), {"$: expected object, got array"});

  const Json list = json::parse(R"({"type":"array","minItems":1,"items":{"$ref":"#/$defs/row"},
    "$defs":{"row":{"type":"object","properties":{"id":{"type":"integer"}},"required":["id"]}}})");
  expect("$ref into $defs", list, json::parse(R"([{"id":1},{"id":2}])"), {});
  expect("$ref violation path", list, json::parse(R"([{"id":1},{"nope":2}])"), {"$[1]: missing required \"id\""});
  expect("minItems", list, json::parse(R"([])"), {"$: fewer than 1 items"});

  const Json combos = json::parse(R"({"type":"object","properties":{
    "v":{"anyOf":[{"type":"string"},{"type":"integer"}]},
    "w":{"oneOf":[{"type":"number","minimum":10},{"type":"number","maximum":5}]},
    "x":{"allOf":[{"type":"integer"},{"minimum":3}]},
    "y":{"not":{"const":"no"}},
    "z":{"type":["string","null"]}}})");
  expect("anyOf passes", combos, json::parse(R"({"v":"s"})"), {});
  expect("anyOf fails", combos, json::parse(R"({"v":true})"), {"$.v: matches none"});
  expect("oneOf exactly one", combos, json::parse(R"({"w":20})"), {});
  expect("oneOf none", combos, json::parse(R"({"w":7})"), {"$.w: matches 0"});
  expect("allOf", combos, json::parse(R"({"x":2})"), {"$.x: 2 is below 3"});
  expect("not", combos, json::parse(R"({"y":"no"})"), {"$.y: matches the forbidden"});
  expect("type list with null", combos, json::parse(R"({"z":null})"), {});
  expect("type list miss", combos, json::parse(R"({"z":3})"), {"$.z: expected string | null"});
  expect("multipleOf", json::parse(R"({"type":"number","multipleOf":0.5})"), Json(2.25), {"not a multiple of 0.5"});
  expect("exclusiveMaximum", json::parse(R"({"type":"number","exclusiveMaximum":5})"), Json(5), {"is above or equal to 5"});
  expect("boolean schema false", json::parse(R"({"properties":{"a":false}})"), json::parse(R"({"a":1})"), {"$.a: not allowed"});

  printf("\ncheck\n");
  report("good schema", schema::check(person).empty());
  report("bad type", schema::check(json::parse(R"({"type":"strng"})")) == "unknown type \"strng\"",
         schema::check(json::parse(R"({"type":"strng"})")));
  report("dangling $ref", core::startsWith(schema::check(json::parse(R"({"$ref":"#/$defs/x"})")), "unresolvable $ref"));
  report("nested bad type", !schema::check(json::parse(R"({"type":"object","properties":{"a":{"items":{"type":"int"}}}})")).empty());
  report("bad pattern", core::startsWith(schema::check(json::parse(R"({"type":"string","pattern":"("})")), "invalid pattern"));
  report("not an object", schema::check(Json(3)) == "a schema is a JSON object");

  printf("\ninfer\n");
  const Json inferred = schema::infer(json::parse(R"({"name":"Ann","age":40,"score":1.5,"ok":true,"tags":["a"],"meta":{"k":null}})"));
  report("object types", inferred["properties"]["name"]["type"] == "string" &&
                             inferred["properties"]["age"]["type"] == "integer" &&
                             inferred["properties"]["score"]["type"] == "number" &&
                             inferred["properties"]["ok"]["type"] == "boolean");
  report("array items", inferred["properties"]["tags"]["items"]["type"] == "string");
  report("nested object", inferred["properties"]["meta"]["properties"]["k"]["type"] == "null");
  report("all required", inferred["required"].size() == 6 && inferred["additionalProperties"] == false);
  report("inferred schema validates its example",
         schema::validate(inferred, json::parse(R"({"name":"Bo","age":1,"score":2.0,"ok":false,"tags":[],"meta":{"k":null}})")).empty());
  report("inferred schema rejects a drift",
         schema::validate(inferred, json::parse(R"({"name":"Bo","age":"1","score":2.0,"ok":false,"tags":[],"meta":{"k":null}})")).size() == 1);

  printf("\nextract\n");
  Json out;
  report("bare object", schema::extractJson(" {\"a\": 1} ", &out) && out["a"] == 1);
  report("bare array", schema::extractJson("[1, 2]", &out) && out.size() == 2);
  report("fenced json", schema::extractJson("Here you go:\n```json\n{\"a\": [1,2]}\n```\nDone.", &out) && out["a"][1] == 2);
  report("fenced without tag", schema::extractJson("```\n{\"b\": true}\n```", &out) && out["b"] == true);
  report("embedded in prose", schema::extractJson("Sure! The answer is {\"x\": \"y}\"} and that's it.", &out) && out["x"] == "y}");
  report("skips a broken candidate", schema::extractJson("{oops} then {\"ok\": 1}", &out) && out["ok"] == 1);
  report("no json", !schema::extractJson("just words", &out));
  report("empty", !schema::extractJson("", &out));

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
