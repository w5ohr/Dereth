#include "redact.h"

#include <algorithm>
#include <cctype>
#include <regex>

namespace redact {
namespace {

struct Rule {
  Kind kind;
  std::regex re;
  int group;   // capture group that holds the value (0 = whole match)
};

const std::vector<Rule>& rules() {
  static const std::vector<Rule> list = {
      // Private keys: the whole PEM block.
      {Kind::PrivateKey, std::regex(R"(-----BEGIN [A-Z ]*PRIVATE KEY-----[\s\S]*?-----END [A-Z ]*PRIVATE KEY-----)"), 0},
      // Vendor key shapes.
      {Kind::ApiKey, std::regex(R"(\b(AKIA[0-9A-Z]{16})\b)"), 1},                                   // AWS access key
      {Kind::ApiKey, std::regex(R"(\b(sk-(?:proj-|ant-)?[A-Za-z0-9_-]{20,})\b)"), 1},             // OpenAI / Anthropic
      {Kind::ApiKey, std::regex(R"(\b(AIza[0-9A-Za-z_-]{35})\b)"), 1},                             // Google
      {Kind::ApiKey, std::regex(R"(\b((?:sk|pk|rk)_(?:live|test)_[0-9A-Za-z]{16,})\b)"), 1},       // Stripe
      {Kind::Token, std::regex(R"(\b(gh[pousr]_[A-Za-z0-9]{30,})\b)"), 1},                         // GitHub
      {Kind::Token, std::regex(R"(\b(github_pat_[A-Za-z0-9_]{40,})\b)"), 1},
      {Kind::Token, std::regex(R"(\b(xox[abprs]-[A-Za-z0-9-]{10,})\b)"), 1},                       // Slack
      {Kind::Token, std::regex(R"(\b(eyJ[A-Za-z0-9_-]{10,}\.eyJ[A-Za-z0-9_-]{10,}\.[A-Za-z0-9_-]{10,})\b)"), 1},   // JWT
      {Kind::Token, std::regex(R"(\b(hf_[A-Za-z0-9]{30,})\b)"), 1},                                // Hugging Face
      {Kind::Token, std::regex(R"((?:[Bb]earer\s+)([A-Za-z0-9_\-.=]{20,}))"), 1},
      // Generic assignments: api_key = ..., token: ..., password=...
      {Kind::ApiKey, std::regex(R"((?:api[_-]?key|apikey|secret[_-]?key|client[_-]?secret|access[_-]?key)\s*[:=]\s*["']?([A-Za-z0-9_\-./+=]{12,})["']?)", std::regex::icase), 1},
      {Kind::Token, std::regex(R"(\b(?:auth[_-]?token|access[_-]?token|refresh[_-]?token|token)\s*[:=]\s*["']?([A-Za-z0-9_\-./+=]{16,})["']?)", std::regex::icase), 1},
      {Kind::Password, std::regex(R"((?:password|passwd|pwd|passphrase)\s*[:=]\s*["']?([^\s"']{6,})["']?)", std::regex::icase), 1},
      // Personal data.
      {Kind::Ssn, std::regex(R"(\b(\d{3}-\d{2}-\d{4})\b)"), 1},
      {Kind::Iban, std::regex(R"(\b([A-Z]{2}\d{2}(?:[ ]?[A-Z0-9]{4}){3,7}(?:[ ]?[A-Z0-9]{1,4})?)\b)"), 1},
      {Kind::Card, std::regex(R"(\b((?:\d[ -]?){13,19})\b)"), 1},
      {Kind::Email, std::regex(R"(\b([A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,})\b)"), 1},
      // std::regex has no lookbehind: the leading context is matched, not asserted.
      {Kind::Phone, std::regex(R"((?:^|[^\w.-])(\+?\d{1,3}[ .-]?\(?\d{2,4}\)?[ .-]?\d{3,4}[ .-]?\d{3,4})(?![\w.-]))"), 1},
      {Kind::Ip, std::regex(R"(\b((?:25[0-5]|2[0-4]\d|1?\d?\d)(?:\.(?:25[0-5]|2[0-4]\d|1?\d?\d)){3})\b)"), 1},
  };
  return list;
}

std::string digitsOnly(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (std::isdigit(static_cast<unsigned char>(c))) out += c;
  }
  return out;
}

bool plausible(Kind kind, const std::string& value) {
  switch (kind) {
    case Kind::Card: {
      const std::string digits = digitsOnly(value);
      return digits.size() >= 13 && digits.size() <= 19 && luhn(digits);
    }
    case Kind::Phone: {
      const std::string digits = digitsOnly(value);
      if (digits.size() < 10 || digits.size() > 15) return false;
      // Not a bare run of digits (those are ids, timestamps, hashes).
      return value.find_first_of(" .-()+") != std::string::npos;
    }
    case Kind::Ip:
      return value != "0.0.0.0" && value != "127.0.0.1" && value.rfind("255.", 0) != 0;
    case Kind::Email:
      return value.find("example.com") == std::string::npos;
    case Kind::Iban: {
      // Country code, two check digits, and a plausible length for it.
      const std::string compact = [&] {
        std::string c;
        for (char ch : value) if (ch != ' ') c += ch;
        return c;
      }();
      return compact.size() >= 15 && compact.size() <= 34 && std::isalpha(static_cast<unsigned char>(compact[0])) &&
             std::isalpha(static_cast<unsigned char>(compact[1])) && std::isdigit(static_cast<unsigned char>(compact[2])) &&
             std::isdigit(static_cast<unsigned char>(compact[3]));
    }
    case Kind::Password:
      return value != "password" && value != "********";
    default:
      return true;
  }
}

}  // namespace

const char* kindName(Kind kind) {
  switch (kind) {
    case Kind::ApiKey: return "API_KEY";
    case Kind::Token: return "TOKEN";
    case Kind::PrivateKey: return "PRIVATE_KEY";
    case Kind::Password: return "PASSWORD";
    case Kind::Card: return "CARD";
    case Kind::Ssn: return "SSN";
    case Kind::Iban: return "IBAN";
    case Kind::Email: return "EMAIL";
    case Kind::Phone: return "PHONE";
    case Kind::Ip: return "IP";
  }
  return "VALUE";
}

const char* kindLabel(Kind kind) {
  switch (kind) {
    case Kind::ApiKey: return "API key";
    case Kind::Token: return "token";
    case Kind::PrivateKey: return "private key";
    case Kind::Password: return "password";
    case Kind::Card: return "card number";
    case Kind::Ssn: return "social security number";
    case Kind::Iban: return "IBAN";
    case Kind::Email: return "email address";
    case Kind::Phone: return "phone number";
    case Kind::Ip: return "IP address";
  }
  return "value";
}

bool kindIsSecret(Kind kind) {
  return kind == Kind::ApiKey || kind == Kind::Token || kind == Kind::PrivateKey || kind == Kind::Password;
}

bool luhn(const std::string& digits) {
  int sum = 0;
  bool twice = false;
  for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
    if (!std::isdigit(static_cast<unsigned char>(*it))) return false;
    int d = *it - '0';
    if (twice) {
      d *= 2;
      if (d > 9) d -= 9;
    }
    sum += d;
    twice = !twice;
  }
  return digits.size() >= 2 && sum % 10 == 0;
}

std::vector<Finding> scan(const std::string& text) {
  std::vector<Finding> found;
  for (const Rule& rule : rules()) {
    for (auto it = std::sregex_iterator(text.begin(), text.end(), rule.re); it != std::sregex_iterator(); ++it) {
      const std::smatch& m = *it;
      const int g = rule.group;
      if (!m[g].matched) continue;
      Finding f;
      f.kind = rule.kind;
      f.value = m[g];
      f.start = static_cast<size_t>(m.position(g));
      // A separator the pattern allowed after the last digit is not part of the value.
      while (!f.value.empty() && (f.value.back() == ' ' || f.value.back() == '-' || f.value.back() == '.')) {
        f.value.pop_back();
      }
      f.end = f.start + f.value.size();
      if (!plausible(f.kind, f.value)) continue;
      found.push_back(f);
    }
  }
  // Overlaps: keep the earlier rule (more specific) / longer match.
  std::stable_sort(found.begin(), found.end(), [](const Finding& a, const Finding& b) {
    if (a.start != b.start) return a.start < b.start;
    return (a.end - a.start) > (b.end - b.start);
  });
  std::vector<Finding> out;
  for (const Finding& f : found) {
    bool overlaps = false;
    for (const Finding& kept : out) {
      if (f.start < kept.end && kept.start < f.end) {
        overlaps = true;
        break;
      }
    }
    if (!overlaps) out.push_back(f);
  }
  return out;
}

std::string apply(const std::string& text, const std::vector<Finding>& findings,
                  const std::vector<bool>& mask, std::map<std::string, std::string>* mapping) {
  std::string out;
  size_t cursor = 0;
  std::map<Kind, int> counters;
  for (const auto& [placeholder, value] : *mapping) {
    // Continue numbering after what the chat already used.
    for (int k = 0; k <= static_cast<int>(Kind::Ip); ++k) {
      const std::string stem = std::string("[") + kindName(static_cast<Kind>(k)) + "_";
      if (placeholder.rfind(stem, 0) == 0) {
        const int n = std::atoi(placeholder.c_str() + stem.size());
        counters[static_cast<Kind>(k)] = std::max(counters[static_cast<Kind>(k)], n);
      }
    }
  }
  for (size_t i = 0; i < findings.size(); ++i) {
    const Finding& f = findings[i];
    if (i < mask.size() && !mask[i]) continue;
    if (f.start < cursor) continue;
    out.append(text, cursor, f.start - cursor);
    std::string placeholder;
    for (const auto& [p, v] : *mapping) {
      if (v == f.value) placeholder = p;
    }
    if (placeholder.empty()) {
      placeholder = std::string("[") + kindName(f.kind) + "_" + std::to_string(++counters[f.kind]) + "]";
      (*mapping)[placeholder] = f.value;
    }
    out += placeholder;
    cursor = f.end;
  }
  out.append(text, cursor, std::string::npos);
  return out;
}

std::string restore(const std::string& text, const std::map<std::string, std::string>& mapping) {
  if (mapping.empty() || text.find('[') == std::string::npos) return text;
  std::string out = text;
  for (const auto& [placeholder, value] : mapping) {
    size_t pos = 0;
    while ((pos = out.find(placeholder, pos)) != std::string::npos) {
      out.replace(pos, placeholder.size(), value);
      pos += value.size();
    }
  }
  return out;
}

std::string preview(const std::string& value) {
  std::string flat;
  for (char c : value) flat += (c == '\n' ? ' ' : c);
  if (flat.size() <= 12) return flat.substr(0, 3) + std::string(std::max<size_t>(1, flat.size() - 3), '*');
  return flat.substr(0, 5) + "…" + flat.substr(flat.size() - 3);
}

}  // namespace redact
