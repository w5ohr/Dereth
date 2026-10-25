#include "budgets.h"

#include <cstdio>

namespace budgets {

Limits fromJson(const Json& j) {
  Limits l;
  if (!j.is_object()) return l;
  if (j.contains("chatTokens") && j["chatTokens"].is_number()) l.chatTokens = j["chatTokens"].get<int64_t>();
  if (j.contains("dayTokens") && j["dayTokens"].is_number()) l.dayTokens = j["dayTokens"].get<int64_t>();
  if (j.contains("dayUsd") && j["dayUsd"].is_number()) l.dayUsd = j["dayUsd"].get<double>();
  if (j.contains("warnAt") && j["warnAt"].is_number()) l.warnAt = j["warnAt"].get<double>();
  if (l.warnAt < 0.1) l.warnAt = 0.1;
  if (l.warnAt > 1.0) l.warnAt = 1.0;
  return l;
}

Json toJson(const Limits& l) {
  return Json{{"chatTokens", l.chatTokens}, {"dayTokens", l.dayTokens}, {"dayUsd", l.dayUsd}, {"warnAt", l.warnAt}};
}

Spend spend(const std::vector<usagelog::Record>& records, const std::string& chatId, const usagelog::Prices& prices,
            int64_t nowSeconds) {
  Spend s;
  const std::string today = usagelog::dayOf(nowSeconds);
  for (const usagelog::Record& r : records) {
    const int64_t tokens = static_cast<int64_t>(r.promptTokens) + r.completionTokens;
    double usd = 0.0;
    auto price = prices.perMillion.find(r.model);
    if (price != prices.perMillion.end()) {
      usd = r.promptTokens / 1e6 * price->second.first + r.completionTokens / 1e6 * price->second.second;
    }
    if (usagelog::dayOf(r.at) == today) {
      s.dayTokens += tokens;
      s.dayUsd += usd;
    }
    if (!chatId.empty() && r.chatId == chatId) {
      s.chatTokens += tokens;
      s.chatUsd += usd;
    }
  }
  return s;
}

std::string tokens(int64_t n) {
  char buf[32];
  if (n >= 1000000) snprintf(buf, sizeof buf, "%.2fM", n / 1e6);
  else if (n >= 1000) snprintf(buf, sizeof buf, "%.1fk", n / 1e3);
  else snprintf(buf, sizeof buf, "%lld", static_cast<long long>(n));
  return buf;
}

Verdict check(const Limits& l, const Spend& s, int64_t aboutToAdd) {
  Verdict v;
  auto consider = [&](double used, double cap, const std::string& what, bool money) {
    if (cap <= 0) return;
    char buf[160];
    auto fmt = [&](double x) { return money ? "$" + std::string(std::to_string(x).substr(0, std::to_string(x).find('.') + 3)) : tokens(static_cast<int64_t>(x)); };
    if (used >= cap) {
      if (v.level != Level::Over) v.message.clear();
      v.level = Level::Over;
      snprintf(buf, sizeof buf, "%s: %s of %s used", what.c_str(), fmt(used).c_str(), fmt(cap).c_str());
      v.message += (v.message.empty() ? "" : "; ") + std::string(buf);
    } else if (used >= cap * l.warnAt && v.level != Level::Over) {
      v.level = Level::Warn;
      snprintf(buf, sizeof buf, "%s: %s of %s used (%.0f%%)", what.c_str(), fmt(used).c_str(), fmt(cap).c_str(), 100.0 * used / cap);
      v.message += (v.message.empty() ? "" : "; ") + std::string(buf);
    }
  };
  consider(static_cast<double>(s.chatTokens + aboutToAdd), static_cast<double>(l.chatTokens), "this chat's tokens", false);
  consider(static_cast<double>(s.dayTokens + aboutToAdd), static_cast<double>(l.dayTokens), "today's tokens", false);
  consider(s.dayUsd, l.dayUsd, "today's cost", true);
  return v;
}

std::string summary(const Limits& l, const Spend& s) {
  std::string out;
  if (l.dayTokens > 0) out += "today " + tokens(s.dayTokens) + " / " + tokens(l.dayTokens) + " tokens";
  if (l.dayUsd > 0) {
    char buf[48];
    snprintf(buf, sizeof buf, "$%.2f / $%.2f", s.dayUsd, l.dayUsd);
    out += (out.empty() ? "today " : ", ") + std::string(buf);
  }
  if (l.chatTokens > 0) out += (out.empty() ? "" : "; ") + std::string("this chat ") + tokens(s.chatTokens) + " / " + tokens(l.chatTokens);
  return out;
}

}  // namespace budgets
