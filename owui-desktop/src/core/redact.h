// Redaction: finds secrets and personal data in text about to leave the
// machine (API keys, tokens, private keys, passwords, card numbers, SSNs,
// IBANs, emails, phone numbers, IP addresses) and replaces them with stable
// placeholders like [EMAIL_1]. The mapping stays local so replies can be
// shown with the real values again. App-free; tested by --redacttest.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace redact {

// Kinds, in the order they are reported; also the placeholder stem.
enum class Kind { ApiKey, Token, PrivateKey, Password, Card, Ssn, Iban, Email, Phone, Ip };
const char* kindName(Kind kind);          // "API_KEY", "EMAIL", ...
const char* kindLabel(Kind kind);         // "API key", "email address", ...
bool kindIsSecret(Kind kind);             // secrets vs. personal data

struct Finding {
  Kind kind;
  std::string value;
  size_t start = 0, end = 0;   // [start, end) in the scanned text
};

// Every match, left to right, longest first on overlaps.
std::vector<Finding> scan(const std::string& text);

// Replaces the findings whose kinds are in `mask` with placeholders. The same
// value always gets the same placeholder; `mapping` (placeholder -> value) is
// extended so later calls keep numbering consistent across a chat.
std::string apply(const std::string& text, const std::vector<Finding>& findings,
                  const std::vector<bool>& mask, std::map<std::string, std::string>* mapping);

// Puts the real values back for display.
std::string restore(const std::string& text, const std::map<std::string, std::string>& mapping);

// "sk-live…9Xq" style preview for the review sheet.
std::string preview(const std::string& value);

bool luhn(const std::string& digits);

}  // namespace redact
