// `Open WebUI --ttstest`: offline checks of the local text-to-speech fallback.
#include <cstdio>
#include <string>

#include "core/tts.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
}  // namespace

int runTtsSelfTest() {
  using namespace tts;
  printf("Open WebUI desktop local-TTS self-test\n\n");

  Prefs d = fromJson(Json::object());
  report("defaults: auto, system voice, natural rate and pitch, code skipped", d.mode == Mode::Auto && d.localVoice.empty() && d.rate == 1.0f && d.pitch == 1.0f && d.skipCode && d.serverChunkChars == 3000);
  Prefs p = fromJson(Json{{"tts", Json{{"mode", "local"}, {"localVoice", "com.apple.voice.enhanced.en-US.Samantha"}, {"rate", 9.0}, {"pitch", 0.1}, {"skipCode", false}}}});
  report("fromJson clamps and round-trips", p.mode == Mode::Local && p.rate == 2.0f && p.pitch == 0.5f && !p.skipCode && fromJson(Json{{"tts", toJson(p)}}).localVoice == p.localVoice &&
                                                 toJson(p)["mode"] == "local" && modeFromName("server") == Mode::Server && modeFromName("bogus") == Mode::Auto);

  report("choose: auto takes the server until it fails, then this Mac", choose(d, false, true) == Engine::Server && choose(d, true, true) == Engine::Local && choose(d, true, false) == Engine::None);
  Prefs server = d;
  server.mode = Mode::Server;
  report("choose: server-only never falls back; local-only never asks the server",
         choose(server, true, true) == Engine::Server && choose(p, false, true) == Engine::Local && choose(p, false, false) == Engine::None);

  const std::string md = "## Lighthouse notes\n\nThe **winches** need grease [1]. See [the guide](https://example.org/guide) and https://docs.example.com/x/y.\n\n"
                         "```python\nprint('hi')\n```\n\n- first item\n- second `code` item\n\n| Part | Count |\n|---|---|\n| bolts | 12 |\n\n> quoted line\n\n1. numbered";
  const std::string spoken = speakable(md, true);
  report("speakable: headings, emphasis, citations, links, URLs, bullets, tables, quotes",
         spoken.find("Lighthouse notes\n") == 0 && spoken.find("The winches need grease. See the guide and link to docs.example.com.") != std::string::npos &&
             spoken.find("Code block omitted.") != std::string::npos && spoken.find("print") == std::string::npos && spoken.find("first item\nsecond code item") != std::string::npos &&
             spoken.find("Part, Count.\nbolts, 12.") != std::string::npos && spoken.find("|") == std::string::npos && spoken.find("quoted line") != std::string::npos &&
             spoken.find("1. numbered") != std::string::npos && spoken.find('#') == std::string::npos && spoken.find('*') == std::string::npos,
         spoken.substr(0, 120));
  report("speakable: code read out when not skipped", speakable("```\nx = 1\n```", false) == "x = 1" && speakable("", true).empty());

  const std::string longText = "First sentence. Second one! Third? " + std::string(50, 'a') + " " + std::string(50, 'b') + ". Last.";
  std::vector<std::string> parts = chunk(longText, 40);
  bool sizesOk = !parts.empty();
  for (const std::string& s : parts) sizesOk = sizesOk && !s.empty() && s.size() <= 50;   // an unbreakable 50-char word may exceed 40
  report("chunk: packs sentences, cuts oversize ones at spaces, nothing empty",
         sizesOk && parts[0] == "First sentence. Second one! Third?" && parts.back().find("Last.") != std::string::npos && chunk("", 100).empty() && chunk("one line\nanother", 100).size() == 1,
         std::to_string(parts.size()) + " parts");
  report("appleRate: natural is 0.5, slower scales down, 2x tops out well below 1",
         appleRate(1.0f) == 0.5f && appleRate(0.5f) == 0.25f && appleRate(2.0f) > 0.79f && appleRate(2.0f) < 0.81f && appleRate(9.0f) == appleRate(2.0f));

  std::vector<Voice> voices{{"v.fr", "Thomas", "fr-FR", 1}, {"v.us1", "Samantha", "en-US", 1}, {"v.us2", "Samantha", "en-US", 2}, {"v.gb", "Daniel", "en-GB", 3}};
  report("pickVoice: installed preference wins, else the best enhanced/premium voice for the tag, then the language, else the system voice",
         pickVoice(voices, "v.gb", "en-US") == "v.gb" && pickVoice(voices, "gone", "en-US") == "v.us2" && pickVoice(voices, "", "en-AU") == "v.gb" && pickVoice(voices, "", "de-DE").empty() &&
             pickVoice(voices, "", "fr-FR").empty() && pickVoice(voices, "", "").empty());
  report("voiceLabel", voiceLabel(voices[2]) == "Samantha (en-US, enhanced)" && voiceLabel(voices[3]) == "Daniel (en-GB, premium)" && voiceLabel(voices[0]) == "Thomas (fr-FR)");

  Queue q;
  report("queue: add splits into parts, refuses duplicates, counts messages", q.add("m1", longText, 40) && !q.add("m1", "again", 40) && q.add("m2", "Short.", 40) && q.has("m1") && q.has("m2") &&
                                                                                 q.messagesPending() == 2 && q.status() == "Reading aloud, 2 more queued" && !q.active);
  Item it;
  report("queue: next pops in order with part numbers; status while playing", q.next(&it) && it.messageId == "m1" && it.part == 1 && it.parts == parts.size() && q.active && q.has("m1") &&
                                                                                 q.messagesPending() == 1 && q.status().find("(part 1 of") != std::string::npos);
  q.remove("m1");
  report("queue: remove drops the playing message and its parts; finish/next/clear", !q.active && !q.has("m1") && q.next(&it) && it.messageId == "m2" && it.parts == 1 && !q.next(nullptr) &&
                                                                                        [&] { q.add("m3", "x.", 40); q.clear(); return !q.active && q.items.empty() && q.status().empty(); }());

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
