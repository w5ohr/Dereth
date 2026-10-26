#include "repomap.h"

#include <algorithm>
#include <regex>

#include "common.h"

namespace repomap {
namespace {

struct Rule {
  const char* kind;
  std::regex re;
  int nameGroup;
};

std::string cap(std::string s, size_t limit = 110) {
  s = core::trim(s);
  if (s.size() > limit) s = s.substr(0, limit - 3) + "...";
  return s;
}

const std::vector<Rule>& rulesFor(const std::string& language) {
  static const std::vector<Rule> python = {
      {"class", std::regex(R"(^\s*class\s+([A-Za-z_]\w*))"), 1},
      {"func", std::regex(R"(^(?:async\s+)?def\s+([A-Za-z_]\w*)\s*\()"), 1},
      {"method", std::regex(R"(^\s+(?:async\s+)?def\s+([A-Za-z_]\w*)\s*\()"), 1},
      {"const", std::regex(R"(^([A-Z][A-Z0-9_]{2,})\s*=)"), 1},
  };
  static const std::vector<Rule> js = {
      {"class", std::regex(R"(^\s*(?:export\s+)?(?:default\s+)?(?:abstract\s+)?class\s+([A-Za-z_$][\w$]*))"), 1},
      {"func", std::regex(R"(^\s*(?:export\s+)?(?:default\s+)?(?:async\s+)?function\s*\*?\s*([A-Za-z_$][\w$]*))"), 1},
      {"func", std::regex(R"(^\s*(?:export\s+)?(?:const|let|var)\s+([A-Za-z_$][\w$]*)\s*(?::[^=]+)?=\s*(?:async\s*)?(?:\([^)]*\)|[A-Za-z_$][\w$]*)\s*=>)"), 1},
      {"type", std::regex(R"(^\s*(?:export\s+)?(?:interface|type|enum)\s+([A-Za-z_$][\w$]*))"), 1},
      {"method", std::regex(R"(^\s{2,}(?:public\s+|private\s+|protected\s+|static\s+|async\s+|readonly\s+)*([A-Za-z_$][\w$]*)\s*\([^)]*\)\s*(?::\s*[^{]+)?\{\s*$)"), 1},
      {"export", std::regex(R"(^\s*export\s+(?:const|let|var)\s+([A-Za-z_$][\w$]*)\s*[=:])"), 1},
  };
  static const std::vector<Rule> cpp = {
      {"class", std::regex(R"(^\s*(?:template\s*<[^>]*>\s*)?(?:class|struct|union|enum(?:\s+class)?)\s+([A-Za-z_]\w*)\s*(?:final\s*)?(?::|\{|$))"), 1},
      {"func", std::regex(R"(^(?:[A-Za-z_][\w:<>,\s\*&]*?\s+)?\*?&?\s*([A-Za-z_][\w:~]*)\s*\([^;]*\)\s*(?:const\s*)?(?:noexcept\s*)?(?:override\s*)?(?:->\s*[\w:<>]+\s*)?\{?\s*$)"), 1},
      {"type", std::regex(R"(^\s*(?:using|typedef)\s+([A-Za-z_]\w*)\s*=?)"), 1},
      {"const", std::regex(R"(^\s*(?:static\s+)?(?:constexpr|const)\s+[\w:<>]+\s+([A-Za-z_]\w*)\s*[={])"), 1},
      {"section", std::regex(R"(^namespace\s+([A-Za-z_]\w*))"), 1},
  };
  static const std::vector<Rule> go = {
      {"func", std::regex(R"(^func\s+(?:\([^)]*\)\s*)?([A-Za-z_]\w*)\s*\()"), 1},
      {"type", std::regex(R"(^type\s+([A-Za-z_]\w*)\s+)"), 1},
  };
  static const std::vector<Rule> rust = {
      {"func", std::regex(R"(^\s*(?:pub(?:\([^)]*\))?\s+)?(?:async\s+)?(?:unsafe\s+)?fn\s+([A-Za-z_]\w*))"), 1},
      {"type", std::regex(R"(^\s*(?:pub(?:\([^)]*\))?\s+)?(?:struct|enum|trait|type)\s+([A-Za-z_]\w*))"), 1},
      {"section", std::regex(R"(^\s*impl(?:<[^>]*>)?\s+(?:[\w:]+\s+for\s+)?([A-Za-z_][\w:]*))"), 1},
      {"section", std::regex(R"(^\s*(?:pub\s+)?mod\s+([A-Za-z_]\w*))"), 1},
  };
  static const std::vector<Rule> swift = {
      {"class", std::regex(R"(^\s*(?:public\s+|private\s+|internal\s+|open\s+|final\s+)*(?:class|struct|enum|protocol|actor|extension)\s+([A-Za-z_]\w*))"), 1},
      {"func", std::regex(R"(^\s*(?:public\s+|private\s+|internal\s+|open\s+|static\s+|override\s+|mutating\s+)*func\s+([A-Za-z_]\w*))"), 1},
  };
  static const std::vector<Rule> ruby = {
      {"class", std::regex(R"(^\s*(?:class|module)\s+([A-Z]\w*(?:::\w+)*))"), 1},
      {"func", std::regex(R"(^\s*def\s+(?:self\.)?([a-z_]\w*[?!=]?))"), 1},
  };
  static const std::vector<Rule> java = {
      {"class", std::regex(R"(^\s*(?:public\s+|private\s+|protected\s+|static\s+|final\s+|abstract\s+)*(?:class|interface|enum|record)\s+([A-Za-z_]\w*))"), 1},
      {"method", std::regex(R"(^\s+(?:public\s+|private\s+|protected\s+|static\s+|final\s+|abstract\s+|synchronized\s+)*[\w<>\[\],\s]+\s+([A-Za-z_]\w*)\s*\([^)]*\)\s*(?:throws\s+[\w,\s]+)?\s*\{)"), 1},
  };
  static const std::vector<Rule> markdown = {
      {"section", std::regex(R"(^(#{1,3})\s+(.+))"), 2},
  };
  static const std::vector<Rule> none;
  if (language == "python") return python;
  if (language == "javascript" || language == "typescript") return js;
  if (language == "cpp" || language == "c" || language == "objc") return cpp;
  if (language == "go") return go;
  if (language == "rust") return rust;
  if (language == "swift") return swift;
  if (language == "ruby") return ruby;
  if (language == "java" || language == "kotlin" || language == "csharp") return java;
  if (language == "markdown") return markdown;
  return none;
}

bool skipLine(const std::string& language, const std::string& line) {
  const std::string t = core::trim(line);
  if (t.empty()) return true;
  if (language == "cpp" || language == "c" || language == "objc") {
    const bool declaration = core::startsWith(t, "using ") || core::startsWith(t, "typedef ") || core::startsWith(t, "constexpr ") ||
                             core::startsWith(t, "const ") || core::startsWith(t, "static ");
    const bool assignment = t.find('=') != std::string::npos && t.find("==") == std::string::npos && t.find("operator") == std::string::npos;
    if (core::startsWith(t, "//") || core::startsWith(t, "#") || core::startsWith(t, "return ") || core::startsWith(t, "if ") ||
        core::startsWith(t, "else") || core::startsWith(t, "for ") || core::startsWith(t, "while ") || core::startsWith(t, "switch ") ||
        core::startsWith(t, "case ") || (assignment && !declaration)) {
      return true;
    }
  }
  if ((language == "javascript" || language == "typescript") && (core::startsWith(t, "//") || core::startsWith(t, "if ") || core::startsWith(t, "for ") || core::startsWith(t, "while ") || core::startsWith(t, "switch ") || core::startsWith(t, "return ") || core::startsWith(t, "} else"))) return true;
  if (language == "python" && core::startsWith(t, "#")) return true;
  return false;
}

}  // namespace

std::string languageOf(const std::string& path) {
  const std::string base = core::baseName(path);
  const size_t dot = base.rfind('.');
  const std::string ext = dot == std::string::npos ? "" : core::lower(base.substr(dot + 1));
  if (ext == "py") return "python";
  if (ext == "js" || ext == "mjs" || ext == "cjs" || ext == "jsx") return "javascript";
  if (ext == "ts" || ext == "tsx") return "typescript";
  if (ext == "cpp" || ext == "cc" || ext == "cxx" || ext == "hpp" || ext == "hh" || ext == "h") return "cpp";
  if (ext == "c") return "c";
  if (ext == "m" || ext == "mm") return "objc";
  if (ext == "go") return "go";
  if (ext == "rs") return "rust";
  if (ext == "swift") return "swift";
  if (ext == "rb") return "ruby";
  if (ext == "java") return "java";
  if (ext == "kt" || ext == "kts") return "kotlin";
  if (ext == "cs") return "csharp";
  if (ext == "md" || ext == "markdown") return "markdown";
  return "";
}

std::vector<Symbol> extract(const std::string& language, const std::string& text) {
  std::vector<Symbol> out;
  const std::vector<Rule>& rules = rulesFor(language);
  if (rules.empty()) return out;
  int lineNo = 0;
  bool inFence = false;
  for (const std::string& line : core::split(text, '\n')) {
    ++lineNo;
    if (language == "markdown") {
      if (core::startsWith(core::trim(line), "```")) inFence = !inFence;
      if (inFence) continue;
    }
    if (skipLine(language, line)) continue;
    // Minified or generated lines make std::regex give up (error_complexity)
    // or crawl; no symbol worth having sits on a 2000-character line.
    if (line.size() > 2000) continue;
    for (const Rule& rule : rules) {
      std::smatch m;
      bool hit = false;
      try {
        hit = std::regex_search(line, m, rule.re);
      } catch (const std::regex_error&) {
        hit = false;   // the pattern blew its budget on this line: not a symbol
      }
      if (!hit) continue;
      Symbol s;
      s.kind = rule.kind;
      s.name = m[static_cast<size_t>(rule.nameGroup)];
      s.signature = cap(line);
      s.line = lineNo;
      if (language == "markdown") s.signature = std::string(m[1].length(), '#') + " " + s.name;
      // C++ func rule is greedy: skip keywords and declarations ending in ';'.
      if (std::string(rule.kind) == "func" && (s.name == "if" || s.name == "while" || s.name == "for" || s.name == "switch" ||
                                               s.name == "return" || s.name == "catch" || s.name == "sizeof")) continue;
      out.push_back(s);
      break;   // one symbol per line
    }
    if (out.size() > 600) break;
  }
  return out;
}

std::string render(const std::vector<FileMap>& filesIn, size_t maxChars) {
  // Compact form: one line per file, symbols as "name() L12" / "class X L3".
  // Over budget, detail goes before files: methods and constants first,
  // then fewer symbols per file; a file drops out only as a last resort.
  auto label = [](const Symbol& s) {
    std::string n = s.name;
    if (s.kind == "func" || s.kind == "method") n += "()";
    else if (s.kind == "class") n = "class " + n;
    else if (s.kind == "type") n = "type " + n;
    else if (s.kind == "section") n = "[" + n + "]";
    return n + " L" + std::to_string(s.line);
  };
  const std::string header =
      "Repo map (what each file defines, name Lline; ask for any file in full when you need it):\n```\n";
  const std::string footer = "```\n";
  struct Pass { bool dropMinor; size_t perFile; };
  const Pass passes[] = {{false, 1000}, {true, 1000}, {true, 40}, {true, 20}, {true, 12}, {true, 6}, {true, 3}};
  std::string best;
  size_t droppedFiles = 0;
  for (const Pass& pass : passes) {
    std::string out = header;
    droppedFiles = 0;
    bool condensed = pass.dropMinor || pass.perFile < 1000;
    for (const FileMap& f : filesIn) {
      std::string line = f.path + ":";
      size_t shown = 0, total = 0;
      for (const Symbol& s : f.symbols) {
        if (pass.dropMinor && (s.kind == "method" || s.kind == "const" || s.kind == "export")) continue;
        ++total;
        if (shown >= pass.perFile) continue;
        line += (shown ? ", " : " ") + label(s);
        ++shown;
      }
      if (shown == 0) continue;
      if (total > shown) line += " (+" + std::to_string(total - shown) + " more)";
      line += "\n";
      if (out.size() + line.size() + footer.size() + 80 > maxChars) {
        ++droppedFiles;
        continue;
      }
      out += line;
    }
    if (droppedFiles > 0) out += "... " + std::to_string(droppedFiles) + " more file(s) left out of the map\n";
    else if (condensed) out += "(condensed to fit: methods/constants and long lists trimmed)\n";
    out += footer;
    best = out;
    if (droppedFiles == 0) break;
  }
  return best;
}

}  // namespace repomap
