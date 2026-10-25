// `Open WebUI --redacttest`: offline checks of the secret / PII scanner,
// masking and restoring.
#include <cstdio>
#include <map>
#include <string>

#include "core/redact.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}
std::string kinds(const std::vector<redact::Finding>& f) {
  std::string out;
  for (const auto& x : f) out += std::string(redact::kindName(x.kind)) + " ";
  return out;
}
bool has(const std::vector<redact::Finding>& f, redact::Kind kind, const std::string& value) {
  for (const auto& x : f) {
    if (x.kind == kind && x.value == value) return true;
  }
  return false;
}
}  // namespace

int runRedactSelfTest() {
  using namespace redact;
  printf("Open WebUI desktop redaction self-test\n\n");

  printf("scan\n");
  auto f = scan("my key is AKIAIOSFODNN7EXAMPLE and that's it");
  report("aws key", has(f, Kind::ApiKey, "AKIAIOSFODNN7EXAMPLE"), kinds(f));
  f = scan("OPENAI_API_KEY=sk-proj-abcdefghijklmnopqrstuvwxyz0123456789");
  report("openai key (vendor shape wins over generic)", f.size() == 1 && f[0].kind == Kind::ApiKey &&
                                                           f[0].value == "sk-proj-abcdefghijklmnopqrstuvwxyz0123456789", kinds(f));
  f = scan("token ghp_ABCDEFGHIJKLMNOPQRSTUVWXYZabcdef123456 here");
  report("github token", has(f, Kind::Token, "ghp_ABCDEFGHIJKLMNOPQRSTUVWXYZabcdef123456"), kinds(f));
  f = scan("Authorization: Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOiIxMjM0NTY3ODkwIn0.dozjgNryP4J3jVmNHl0w5N_XgL0n3I9PlFUP0THsR8U");
  report("jwt (one finding, not bearer + jwt)", f.size() == 1 && f[0].kind == Kind::Token, kinds(f));
  f = scan("password: hunter2secret\napi_key = 'abcd1234efgh5678ijkl'");
  report("password and generic api key", has(f, Kind::Password, "hunter2secret") && has(f, Kind::ApiKey, "abcd1234efgh5678ijkl"), kinds(f));
  f = scan("-----BEGIN RSA PRIVATE KEY-----\nMIIEow\nAbC\n-----END RSA PRIVATE KEY-----");
  report("private key block", f.size() == 1 && f[0].kind == Kind::PrivateKey);
  f = scan("card 4111 1111 1111 1111 expires soon; order id 1234567890123");
  report("card passes luhn, plain id does not", f.size() == 1 && f[0].kind == Kind::Card && f[0].value == "4111 1111 1111 1111", kinds(f));
  f = scan("SSN 123-45-6789 and phone +1 (415) 555-0123 and email ann.lee@corp.co.uk");
  report("ssn / phone / email", has(f, Kind::Ssn, "123-45-6789") && has(f, Kind::Phone, "+1 (415) 555-0123") &&
                                   has(f, Kind::Email, "ann.lee@corp.co.uk"), kinds(f));
  f = scan("server 10.0.12.7 and localhost 127.0.0.1 and version 1.2.3.4");
  report("ip (not loopback)", has(f, Kind::Ip, "10.0.12.7") && !has(f, Kind::Ip, "127.0.0.1"), kinds(f));
  f = scan("IBAN DE89 3704 0044 0532 0130 00");
  report("iban", has(f, Kind::Iban, "DE89 3704 0044 0532 0130 00"), kinds(f));
  f = scan("example user test@example.com, timestamp 1729800000000, hash 5f4dcc3b5aa765d61d8327deb882cf99");
  report("no false positives on example.com / timestamps / hashes", f.empty(), kinds(f));
  f = scan("Just a normal question about std::vector<int> and git rebase -i HEAD~3.");
  report("clean prose is clean", f.empty(), kinds(f));

  printf("\napply / restore\n");
  std::map<std::string, std::string> mapping;
  const std::string text = "Use key sk-proj-abcdefghijklmnopqrstuvwxyz0123456789 for ann@corp.io and again ann@corp.io";
  f = scan(text);
  std::string masked = apply(text, f, std::vector<bool>(f.size(), true), &mapping);
  report("placeholders, same value same placeholder", masked == "Use key [API_KEY_1] for [EMAIL_1] and again [EMAIL_1]", masked);
  report("mapping has two entries", mapping.size() == 2 && mapping["[EMAIL_1]"] == "ann@corp.io");
  report("restore round-trips", restore(masked, mapping) == text);
  std::vector<bool> keepKey = {false, true, true};
  masked = apply(text, f, keepKey, &mapping);
  report("unmasked item stays", masked.find("sk-proj-") != std::string::npos && masked.find("[EMAIL_1]") != std::string::npos, masked);
  f = scan("new address bo@corp.io");
  masked = apply("new address bo@corp.io", f, {true}, &mapping);
  report("numbering continues across calls", masked == "new address [EMAIL_2]", masked);
  report("preview hides the middle", preview("sk-proj-abcdefghijklmnop") == "sk-pr…nop" && preview("short") == "sho**", preview("sk-proj-abcdefghijklmnop"));
  report("luhn", luhn("4111111111111111") && !luhn("4111111111111112"));

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
