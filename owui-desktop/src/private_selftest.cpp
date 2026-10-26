// `Open WebUI --privatetest`: offline checks of the private-chat store -
// sealing, tamper detection, save / load / list / remove in a scratch folder.
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <string>

#include "core/common.h"
#include "core/privatestore.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
}  // namespace

int runPrivateSelfTest() {
  using namespace privatestore;
  printf("Open WebUI desktop private-chat store self-test\n\n");

  printf("crypto\n");
  const std::string key = randomBytes(32);
  const std::string other = randomBytes(32);
  report("random key is 32 bytes and not zero", key.size() == 32 && key != std::string(32, '\0') && key != other);
  const std::string plain = "{\"title\": \"secret plans\", \"body\": \"the quick brown fox\"}";
  const std::string blob = seal(plain, key);
  report("sealed blob is larger and opaque", blob.size() > plain.size() + 48 && blob.find("secret") == std::string::npos);
  std::string back;
  report("opens with the right key", open(blob, key, &back) && back == plain);
  report("fails with another key", !open(blob, other, &back));
  std::string tampered = blob;
  tampered[30] ^= 0x01;
  report("detects a flipped byte", !open(tampered, key, &back));
  report("rejects a short blob", !open("OWP1xx", key, &back));
  report("different iv each time", seal(plain, key) != blob);
  report("empty plaintext round-trips", open(seal("", key), key, &back) && back.empty());
  report("wrong key size refused", seal(plain, "short").empty());

  printf("\nstore\n");
  const std::string dir = "/tmp/owui-privatetest-" + std::to_string(getpid());
  const std::string id = newId();
  report("ids are local-", isPrivateId(id) && !isPrivateId("abc"));
  Json doc{{"title", "Plans"}, {"updated_at", 1700000000}, {"history", {{"messages", Json::object()}}}};
  report("save", save(dir, key, id, doc));
  struct stat st {};
  report("file is private to the user", stat(dir.c_str(), &st) == 0 && (st.st_mode & 0077) == 0);
  bool ok = false;
  const std::string raw = core::readFile(dir + "/" + id + ".enc", &ok);
  report("file does not contain the title in clear", ok && raw.find("Plans") == std::string::npos);
  auto loaded = load(dir, key, id);
  report("load", loaded && (*loaded)["title"] == "Plans");
  report("load with wrong key fails", !load(dir, other, id));
  const std::string id2 = newId();
  save(dir, key, id2, Json{{"title", "Newer"}, {"updated_at", 1700000500}});
  auto entries = list(dir, key);
  report("list newest first", entries.size() == 2 && entries[0].title == "Newer" && entries[1].id == id);
  core::writeFile(dir + "/not-a-chat.enc", "junk");
  report("junk files skipped", list(dir, key).size() == 2);
  report("remove", remove(dir, id) && list(dir, key).size() == 1);
  report("remove missing fails quietly", !remove(dir, id));

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
