// `Open WebUI --modellibtest`: offline checks of the model-library parsers.
#include <cstdio>
#include <string>

#include "core/modellib.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
}  // namespace

int runModelLibSelfTest() {
  using namespace modellib;
  printf("Open WebUI desktop model-library self-test\n\n");

  report("urls", hfSearchUrl("qwen 0.5b") == "https://huggingface.co/api/models?search=qwen+0.5b&filter=gguf&sort=downloads&direction=-1&limit=25" &&
                     hfTreeUrl("Qwen/X-GGUF") == "https://huggingface.co/api/models/Qwen/X-GGUF/tree/main" &&
                     ollamaSearchUrl("qwen3") == "https://ollama.com/search?q=qwen3" && ollamaTagsUrl("qwen3") == "https://ollama.com/library/qwen3/tags");
  report("quantOf", quantOf("qwen2.5-0.5b-instruct-q4_k_m.gguf") == "Q4_K_M" && quantOf("Model.Q8_0.gguf") == "Q8_0" && quantOf("x-IQ4_XS.gguf") == "IQ4_XS" &&
                        quantOf("model-f16.gguf") == "F16" && quantOf("readme.gguf").empty());
  report("parseSize / humanSize", parseSize("5.2GB") == 5.2e9 && parseSize("523MB") == 523e6 && parseSize("abc") < 0 && humanSize(5.2e9) == "5.2 GB" && humanSize(523e6) == "523 MB");
  report("enoughSpace needs 10% headroom plus 512 MB", enoughSpace(5e9, 6.1e9) && !enoughSpace(5e9, 5.5e9) && enoughSpace(-1, 1e9) && enoughSpace(5e9, -1));

  std::vector<Entry> hf = parseHfSearch("[{\"id\":\"Qwen/Qwen2.5-0.5B-Instruct-GGUF\",\"downloads\":325006,\"likes\":157,\"pipeline_tag\":\"text-generation\"},{\"modelId\":\"x/y\"},{\"nope\":1}]");
  report("parseHfSearch", hf.size() == 2 && hf[0].id == "Qwen/Qwen2.5-0.5B-Instruct-GGUF" && hf[0].downloads == 325006 && hf[0].source == "hf" &&
                              hf[0].description.find("325006 downloads") != std::string::npos && hf[1].id == "x/y");
  std::vector<Variant> tree = parseHfTree("Qwen/Q-GGUF",
                                          "[{\"path\":\"README.md\",\"size\":10},{\"path\":\"q-q4_k_m.gguf\",\"size\":491400032},{\"path\":\"q-fp16.gguf\",\"size\":1266425696},"
                                          "{\"path\":\"big-q8_0-00001-of-00002.gguf\",\"size\":5},{\"path\":\"q-q2_k.gguf\",\"size\":415182688}]");
  report("parseHfTree: gguf only, split files skipped, sorted by size, pull names with quant tags",
         tree.size() == 3 && tree[0].label == "Q2_K" && tree[0].bytes == 415182688 && tree[0].pullName == "hf.co/Qwen/Q-GGUF:Q2_K" &&
             tree[2].label == "FP16" && tree[2].pullName == "hf.co/Qwen/Q-GGUF:FP16");

  const std::string searchHtml =
      "<a href=\"/library/qwen3\"><h2>qwen3</h2></a><p class=\"x\" title=\"Qwen3 is the latest &amp; greatest.\">Qwen3 is the latest &amp; greatest.</p>"
      "<a href=\"/library/qwen3\">dup</a><a href=\"/library/qwen3:8b\">tag link ignored</a>"
      "<a href=\"/library/llama3.2\"></a><p class=\"y\">Meta's small model</p>";
  std::vector<Entry> os = parseOllamaSearch(searchHtml);
  report("parseOllamaSearch: names deduped, tag links skipped, description from title or text",
         os.size() == 2 && os[0].id == "qwen3" && os[0].description == "Qwen3 is the latest & greatest." && os[1].id == "llama3.2" && os[1].description == "Meta's small model",
         os.empty() ? "none" : os[0].description);
  const std::string tagsHtml =
      "<a href=\"/library/qwen3:latest\">qwen3:latest</a> ... <span>5.2GB</span> ... <a href=\"/library/qwen3:latest\">again</a> <span>5.2GB</span>"
      "<a href=\"/library/qwen3:0.6b\">qwen3:0.6b</a> <div>523MB</div><a href=\"/library/qwen3:8b\">x</a> <span>5.2GB</span>";
  std::vector<Variant> tags = parseOllamaTags("qwen3", tagsHtml);
  report("parseOllamaTags: tags deduped with the size that follows each", tags.size() == 3 && tags[0].label == "latest" && tags[0].bytes == 5.2e9 &&
                                                                               tags[1].label == "0.6b" && tags[1].bytes == 523e6 && tags[2].pullName == "qwen3:8b",
         std::to_string(tags.size()));

  Progress p = parseProgress(Json{{"status", "pulling abc"}, {"completed", 100}, {"total", 400}});
  Progress d = parseProgress(Json{{"status", "success"}});
  Progress e = parseProgress(Json{{"error", "pull model manifest: file does not exist"}});
  report("parseProgress", p.status == "pulling abc" && p.completed == 100 && p.total == 400 && !p.done && d.done && e.error.find("manifest") != std::string::npos);

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
