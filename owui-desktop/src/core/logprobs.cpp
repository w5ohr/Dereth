#include "logprobs.h"

#include <algorithm>
#include <cmath>

namespace logprobs {

Prefs prefsFromJson(const Json& extra) {
  Prefs p;
  const Json j = extra.is_object() ? extra.value("logprobs", Json::object()) : Json::object();
  if (!j.is_object()) return p;
  p.enabled = j.value("enabled", false);
  p.top = std::clamp(j.value("top", p.top), 1, 8);
  return p;
}

Json prefsToJson(const Prefs& p) { return Json{{"enabled", p.enabled}, {"top", p.top}}; }

std::vector<Token> fromEntries(const Json& list) {
  std::vector<Token> out;
  if (!list.is_array()) return out;
  for (const auto& e : list) {
    if (!e.is_object() || !e.contains("token") || !e["token"].is_string()) continue;
    Token t;
    t.token = e["token"];
    t.logprob = e.value("logprob", 0.0);
    if (e.contains("top_logprobs") && e["top_logprobs"].is_array()) {
      for (const auto& a : e["top_logprobs"]) {
        if (!a.is_object() || !a.contains("token") || !a["token"].is_string()) continue;
        t.alts.push_back(Alt{a["token"], a.value("logprob", 0.0)});
      }
    }
    out.push_back(std::move(t));
  }
  return out;
}

std::vector<Token> fromOpenAIChoice(const Json& choice) {
  if (!choice.is_object() || !choice.contains("logprobs") || !choice["logprobs"].is_object()) return {};
  return fromEntries(choice["logprobs"].value("content", Json::array()));
}

Json toJson(const std::vector<Token>& tokens) {
  Json out = Json::array();
  for (const Token& t : tokens) {
    Json alts = Json::array();
    for (const Alt& a : t.alts) alts.push_back(Json::array({a.token, a.logprob}));
    out.push_back(Json::array({t.token, t.logprob, alts}));
  }
  return out;
}

std::vector<Token> fromJson(const Json& compact) {
  std::vector<Token> out;
  if (!compact.is_array()) return out;
  for (const auto& e : compact) {
    if (!e.is_array() || e.size() < 2 || !e[0].is_string() || !e[1].is_number()) continue;
    Token t;
    t.token = e[0];
    t.logprob = e[1];
    if (e.size() > 2 && e[2].is_array()) {
      for (const auto& a : e[2]) {
        if (a.is_array() && a.size() == 2 && a[0].is_string() && a[1].is_number()) t.alts.push_back(Alt{a[0], a[1]});
      }
    }
    out.push_back(std::move(t));
  }
  return out;
}

double confidence(double logprob) {
  if (!std::isfinite(logprob)) return 0.0;
  return std::clamp(std::exp(std::min(0.0, logprob)), 0.0, 1.0);
}

Rgb tint(double p) {
  const Rgb red{0.90f, 0.32f, 0.30f}, amber{0.95f, 0.70f, 0.20f}, green{0.30f, 0.76f, 0.42f};
  auto mix = [](Rgb a, Rgb b, double t) {
    const float f = static_cast<float>(std::clamp(t, 0.0, 1.0));
    return Rgb{a.r + (b.r - a.r) * f, a.g + (b.g - a.g) * f, a.b + (b.b - a.b) * f};
  };
  if (p < 0.5) return mix(red, amber, p / 0.5);
  return mix(amber, green, (p - 0.5) / 0.5);
}

std::string percent(double p) { return std::to_string(static_cast<int>(std::lround(std::clamp(p, 0.0, 1.0) * 100.0))) + "%"; }

Stats stats(const std::vector<Token>& tokens) {
  Stats s;
  double sum = 0.0;
  for (size_t i = 0; i < tokens.size(); ++i) {
    const double p = confidence(tokens[i].logprob);
    sum += p;
    if (p < 0.5) ++s.low;
    if (p < s.lowestP) {
      s.lowestP = p;
      s.lowest = static_cast<int>(i);
    }
  }
  s.count = static_cast<int>(tokens.size());
  s.mean = tokens.empty() ? 0.0 : sum / static_cast<double>(tokens.size());
  return s;
}

std::string statsLine(const Stats& s, const std::vector<Token>& tokens) {
  if (s.count == 0) return "no token probabilities";
  std::string out = "mean " + percent(s.mean) + " over " + std::to_string(s.count) + " token" + (s.count == 1 ? "" : "s") + " - " + std::to_string(s.low) + " under 50%";
  if (s.lowest >= 0 && s.lowest < static_cast<int>(tokens.size())) out += " - lowest '" + tokens[static_cast<size_t>(s.lowest)].token + "' " + percent(s.lowestP);
  return out;
}

std::string tooltip(const Token& t) {
  std::string out = "'" + t.token + "'  " + percent(confidence(t.logprob));
  bool any = false;
  for (const Alt& a : t.alts) {
    if (a.token == t.token) continue;
    if (!any) out += "\nalternatives:";
    any = true;
    out += "\n  '" + a.token + "'  " + percent(confidence(a.logprob));
  }
  return out;
}

bool aligned(const std::vector<Token>& tokens, const std::string& text) {
  std::string joined;
  for (const Token& t : tokens) joined += t.token;
  if (joined.empty()) return false;
  return text.compare(0, joined.size(), joined) == 0;
}

}  // namespace logprobs
