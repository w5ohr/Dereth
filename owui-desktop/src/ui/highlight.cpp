#include "ui/highlight.h"

#include <cctype>
#include <set>
#include <unordered_map>

namespace highlight {
namespace {

struct LanguageSpec {
  std::set<std::string> keywords;
  std::set<std::string> types;
  const char* lineComment;
  const char* blockOpen;
  const char* blockClose;
  bool hashPreproc;        // C-style #include
  bool decorators;         // @name
};

const std::set<std::string> kCKeywords = {
    "if", "else", "for", "while", "do", "return", "break", "continue", "switch", "case",
    "default", "goto", "sizeof", "typedef", "struct", "union", "enum", "static", "extern",
    "const", "volatile", "inline", "register", "class", "public", "private", "protected",
    "virtual", "override", "final", "template", "typename", "namespace", "using", "new",
    "delete", "this", "nullptr", "true", "false", "try", "catch", "throw", "noexcept",
    "constexpr", "consteval", "static_cast", "dynamic_cast", "reinterpret_cast", "const_cast",
    "operator", "explicit", "friend", "mutable", "auto", "decltype", "co_await", "co_return",
    "import", "export", "module", "requires", "concept", "@interface", "@implementation",
    "@end", "@property", "@selector", "self", "super", "nil", "YES", "NO"};
const std::set<std::string> kCTypes = {
    "int", "long", "short", "char", "float", "double", "void", "bool", "unsigned", "signed",
    "size_t", "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t",
    "uint64_t", "string", "vector", "map", "set", "unordered_map", "shared_ptr", "unique_ptr",
    "std", "NSString", "NSArray", "NSDictionary", "id", "ImVec2", "ImU32"};

const std::set<std::string> kPyKeywords = {
    "def", "class", "if", "elif", "else", "for", "while", "return", "import", "from", "as",
    "try", "except", "finally", "with", "lambda", "yield", "pass", "break", "continue", "and",
    "or", "not", "in", "is", "None", "True", "False", "global", "nonlocal", "assert", "del",
    "raise", "async", "await", "match", "case"};
const std::set<std::string> kPyTypes = {"int", "str", "float", "bool", "list", "dict", "set",
                                         "tuple", "self", "cls", "object", "bytes", "Any",
                                         "Optional", "List", "Dict"};

const std::set<std::string> kJsKeywords = {
    "function", "return", "if", "else", "for", "while", "do", "switch", "case", "default",
    "break", "continue", "new", "delete", "typeof", "instanceof", "in", "of", "class",
    "extends", "super", "this", "import", "export", "from", "as", "try", "catch", "finally",
    "throw", "async", "await", "yield", "let", "const", "var", "true", "false", "null",
    "undefined", "static", "get", "set", "interface", "type", "enum", "implements",
    "readonly", "declare", "namespace", "abstract", "public", "private", "protected"};
const std::set<std::string> kJsTypes = {"string", "number", "boolean", "any", "void", "never",
                                         "unknown", "object", "Array", "Promise", "Map", "Set",
                                         "Record", "Partial", "console", "window", "document"};

const std::set<std::string> kGoKeywords = {
    "func", "package", "import", "return", "if", "else", "for", "range", "switch", "case",
    "default", "break", "continue", "go", "defer", "select", "chan", "type", "struct",
    "interface", "map", "var", "const", "nil", "true", "false", "fallthrough", "goto"};
const std::set<std::string> kGoTypes = {"int", "int64", "int32", "string", "bool", "byte",
                                         "rune", "error", "float64", "float32", "uint", "any"};

const std::set<std::string> kRustKeywords = {
    "fn", "let", "mut", "pub", "use", "mod", "struct", "enum", "impl", "trait", "for", "in",
    "while", "loop", "if", "else", "match", "return", "break", "continue", "as", "ref", "self",
    "Self", "super", "crate", "where", "move", "async", "await", "dyn", "unsafe", "static",
    "const", "type", "true", "false", "Some", "None", "Ok", "Err"};
const std::set<std::string> kRustTypes = {"i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64",
                                           "usize", "isize", "f32", "f64", "bool", "char", "str",
                                           "String", "Vec", "Option", "Result", "Box", "HashMap"};

const std::set<std::string> kShellKeywords = {
    "if", "then", "else", "elif", "fi", "for", "while", "do", "done", "case", "esac", "in",
    "function", "return", "exit", "local", "export", "set", "source", "echo", "cd", "test",
    "true", "false", "read", "shift"};

const std::set<std::string> kSqlKeywords = {
    "select", "from", "where", "insert", "into", "values", "update", "set", "delete", "create",
    "table", "drop", "alter", "join", "left", "right", "inner", "outer", "on", "group", "by",
    "order", "having", "limit", "offset", "as", "and", "or", "not", "null", "is", "in",
    "exists", "distinct", "count", "sum", "avg", "min", "max", "primary", "key", "index",
    "with", "union", "case", "when", "then", "else", "end", "between", "like"};

const std::set<std::string> kJavaKeywords = {
    "public", "private", "protected", "class", "interface", "extends", "implements", "static",
    "final", "void", "return", "if", "else", "for", "while", "do", "switch", "case", "default",
    "break", "continue", "new", "this", "super", "try", "catch", "finally", "throw", "throws",
    "import", "package", "null", "true", "false", "abstract", "synchronized", "instanceof",
    "enum", "record", "var", "using", "namespace", "override", "readonly", "async", "await",
    "func", "let", "guard", "struct", "protocol", "extension", "mutating", "some"};
const std::set<std::string> kJavaTypes = {"int", "long", "double", "float", "boolean", "char",
                                           "byte", "short", "String", "Integer", "List", "Map",
                                           "Object", "Bool", "Int", "Double", "Array"};

const std::unordered_map<std::string, LanguageSpec>& specs() {
  static const std::unordered_map<std::string, LanguageSpec> table = {
      {"cpp", {kCKeywords, kCTypes, "//", "/*", "*/", true, false}},
      {"c", {kCKeywords, kCTypes, "//", "/*", "*/", true, false}},
      {"objc", {kCKeywords, kCTypes, "//", "/*", "*/", true, false}},
      {"python", {kPyKeywords, kPyTypes, "#", "\"\"\"", "\"\"\"", false, true}},
      {"javascript", {kJsKeywords, kJsTypes, "//", "/*", "*/", false, true}},
      {"typescript", {kJsKeywords, kJsTypes, "//", "/*", "*/", false, true}},
      {"go", {kGoKeywords, kGoTypes, "//", "/*", "*/", false, false}},
      {"rust", {kRustKeywords, kRustTypes, "//", "/*", "*/", false, true}},
      {"shell", {kShellKeywords, {}, "#", "", "", false, false}},
      {"sql", {kSqlKeywords, {}, "--", "/*", "*/", false, false}},
      {"java", {kJavaKeywords, kJavaTypes, "//", "/*", "*/", false, true}},
      {"csharp", {kJavaKeywords, kJavaTypes, "//", "/*", "*/", true, true}},
      {"swift", {kJavaKeywords, kJavaTypes, "//", "/*", "*/", false, true}},
      {"kotlin", {kJavaKeywords, kJavaTypes, "//", "/*", "*/", false, true}},
      {"php", {kJsKeywords, kJsTypes, "//", "/*", "*/", false, false}},
      {"ruby", {kPyKeywords, {}, "#", "=begin", "=end", false, false}},
      {"json", {{"true", "false", "null"}, {}, "", "", "", false, false}},
      {"yaml", {{"true", "false", "null"}, {}, "#", "", "", false, false}},
      {"toml", {{"true", "false"}, {}, "#", "", "", false, false}},
      {"html", {{}, {}, "", "<!--", "-->", false, false}},
      {"css", {{"important"}, {}, "", "/*", "*/", false, false}},
      {"diff", {{}, {}, "", "", "", false, false}},
      {"makefile", {{}, {}, "#", "", "", false, false}},
      {"cmake", {{"if", "else", "endif", "set", "function", "endfunction", "foreach", "endforeach"},
                 {}, "#", "", "", false, false}},
  };
  return table;
}

bool isIdent(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

}  // namespace

std::string normalizeLanguage(const std::string& language) {
  static const std::unordered_map<std::string, std::string> aliases = {
      {"py", "python"}, {"python3", "python"}, {"js", "javascript"}, {"jsx", "javascript"},
      {"mjs", "javascript"}, {"ts", "typescript"}, {"tsx", "typescript"}, {"c++", "cpp"},
      {"cc", "cpp"}, {"cxx", "cpp"}, {"hpp", "cpp"}, {"h", "c"}, {"objective-c", "objc"},
      {"objectivec", "objc"}, {"mm", "objc"}, {"sh", "shell"}, {"bash", "shell"},
      {"zsh", "shell"}, {"console", "shell"}, {"rs", "rust"}, {"golang", "go"},
      {"cs", "csharp"}, {"c#", "csharp"}, {"kt", "kotlin"}, {"rb", "ruby"}, {"yml", "yaml"},
      {"htm", "html"}, {"xml", "html"}, {"svg", "html"}, {"patch", "diff"}, {"mk", "makefile"},
      {"make", "makefile"}, {"text", ""}, {"txt", ""}, {"plaintext", ""}, {"", ""},
  };
  std::string lower;
  for (char c : language) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  auto alias = aliases.find(lower);
  if (alias != aliases.end()) return alias->second;
  return specs().count(lower) ? lower : "";
}

std::vector<Span> tokenize(const std::string& line, const std::string& language,
                           bool* inBlockComment) {
  std::vector<Span> spans;
  auto specIt = specs().find(language);
  if (specIt == specs().end()) {
    spans.push_back({0, line.size(), Kind::Plain});
    return spans;
  }
  const LanguageSpec& spec = specIt->second;

  if (language == "diff") {
    Kind kind = Kind::Plain;
    if (line.rfind("+++", 0) == 0 || line.rfind("---", 0) == 0) kind = Kind::Comment;
    else if (line.rfind("@@", 0) == 0) kind = Kind::Function;
    else if (!line.empty() && line[0] == '+') kind = Kind::String;
    else if (!line.empty() && line[0] == '-') kind = Kind::Keyword;
    spans.push_back({0, line.size(), kind});
    return spans;
  }

  const std::string blockOpen = spec.blockOpen ? spec.blockOpen : "";
  const std::string blockClose = spec.blockClose ? spec.blockClose : "";
  const std::string lineComment = spec.lineComment ? spec.lineComment : "";
  size_t i = 0;
  const size_t n = line.size();

  auto push = [&](size_t begin, size_t end, Kind kind) {
    if (end > begin) spans.push_back({begin, end, kind});
  };

  while (i < n) {
    if (*inBlockComment) {
      size_t close = blockClose.empty() ? std::string::npos : line.find(blockClose, i);
      if (close == std::string::npos) {
        push(i, n, Kind::Comment);
        return spans;
      }
      push(i, close + blockClose.size(), Kind::Comment);
      i = close + blockClose.size();
      *inBlockComment = false;
      continue;
    }
    const char c = line[i];

    if (!blockOpen.empty() && line.compare(i, blockOpen.size(), blockOpen) == 0) {
      size_t close = line.find(blockClose, i + blockOpen.size());
      if (close == std::string::npos) {
        push(i, n, Kind::Comment);
        *inBlockComment = true;
        return spans;
      }
      push(i, close + blockClose.size(), Kind::Comment);
      i = close + blockClose.size();
      continue;
    }
    if (!lineComment.empty() && line.compare(i, lineComment.size(), lineComment) == 0) {
      push(i, n, Kind::Comment);
      return spans;
    }
    if (spec.hashPreproc && c == '#' && i == line.find_first_not_of(" \t")) {
      push(i, n, Kind::Preproc);
      return spans;
    }
    if (c == '"' || c == '\'' || c == '`') {
      size_t j = i + 1;
      while (j < n && line[j] != c) {
        if (line[j] == '\\') ++j;
        ++j;
      }
      push(i, std::min(j + 1, n), Kind::String);
      i = std::min(j + 1, n);
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(c)) ||
        (c == '.' && i + 1 < n && std::isdigit(static_cast<unsigned char>(line[i + 1])))) {
      size_t j = i;
      while (j < n && (std::isalnum(static_cast<unsigned char>(line[j])) || line[j] == '.' ||
                       line[j] == '_')) {
        ++j;
      }
      push(i, j, Kind::Number);
      i = j;
      continue;
    }
    if (spec.decorators && c == '@' && i + 1 < n && isIdent(line[i + 1])) {
      size_t j = i + 1;
      while (j < n && (isIdent(line[j]) || line[j] == '.')) ++j;
      push(i, j, Kind::Attribute);
      i = j;
      continue;
    }
    if (isIdent(c) || c == '@') {
      size_t j = i;
      while (j < n && (isIdent(line[j]) || line[j] == '@')) ++j;
      std::string word = line.substr(i, j - i);
      Kind kind = Kind::Plain;
      std::string lowered;
      for (char ch : word) lowered += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
      const bool caseInsensitive = (language == "sql");
      if (spec.keywords.count(caseInsensitive ? lowered : word)) {
        kind = Kind::Keyword;
      } else if (spec.types.count(word)) {
        kind = Kind::Type;
      } else {
        size_t k = j;
        while (k < n && line[k] == ' ') ++k;
        if (k < n && line[k] == '(') kind = Kind::Function;
        else if (std::isupper(static_cast<unsigned char>(word[0])) && word.size() > 1 &&
                 language != "shell" && language != "sql")
          kind = Kind::Type;
      }
      push(i, j, kind);
      i = j;
      continue;
    }
    if (std::ispunct(static_cast<unsigned char>(c))) {
      push(i, i + 1, Kind::Punct);
      i += 1;
      continue;
    }
    size_t j = i;
    while (j < n && line[j] == ' ') ++j;
    if (j == i) j = i + 1;
    push(i, j, Kind::Plain);
    i = j;
  }
  return spans;
}

ImU32 colorFor(Kind kind, bool dark) {
  if (dark) {
    switch (kind) {
      case Kind::Keyword: return IM_COL32(199, 146, 234, 255);
      case Kind::Type: return IM_COL32(255, 203, 107, 255);
      case Kind::String: return IM_COL32(195, 232, 141, 255);
      case Kind::Comment: return IM_COL32(110, 118, 129, 255);
      case Kind::Number: return IM_COL32(247, 140, 108, 255);
      case Kind::Function: return IM_COL32(130, 170, 255, 255);
      case Kind::Punct: return IM_COL32(170, 175, 190, 255);
      case Kind::Preproc: return IM_COL32(137, 221, 255, 255);
      case Kind::Attribute: return IM_COL32(255, 230, 140, 255);
      default: return IM_COL32(230, 230, 236, 255);
    }
  }
  switch (kind) {
    case Kind::Keyword: return IM_COL32(150, 40, 180, 255);
    case Kind::Type: return IM_COL32(160, 100, 0, 255);
    case Kind::String: return IM_COL32(40, 130, 40, 255);
    case Kind::Comment: return IM_COL32(120, 128, 140, 255);
    case Kind::Number: return IM_COL32(200, 70, 30, 255);
    case Kind::Function: return IM_COL32(30, 90, 200, 255);
    case Kind::Punct: return IM_COL32(80, 80, 90, 255);
    case Kind::Preproc: return IM_COL32(0, 120, 160, 255);
    case Kind::Attribute: return IM_COL32(170, 130, 0, 255);
    default: return IM_COL32(26, 26, 30, 255);
  }
}

void drawLine(const std::string& line, const std::string& language, bool* inBlockComment,
              bool darkTheme) {
  if (line.empty()) {
    ImGui::TextUnformatted("");
    return;
  }
  std::vector<Span> spans = tokenize(line, language, inBlockComment);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  ImVec2 pos = ImGui::GetCursorScreenPos();
  ImFont* font = ImGui::GetFont();
  const float size = ImGui::GetFontSize();
  float x = pos.x;
  for (const Span& span : spans) {
    const char* begin = line.c_str() + span.begin;
    const char* end = line.c_str() + span.end;
    dl->AddText(font, size, ImVec2(x, pos.y), colorFor(span.kind, darkTheme), begin, end);
    x += font->CalcTextSizeA(size, FLT_MAX, 0.0f, begin, end).x;
  }
  ImGui::Dummy(ImVec2(x - pos.x, ImGui::GetTextLineHeight()));
}

const char* cssClass(Kind kind) {
  switch (kind) {
    case Kind::Keyword: return "hl-k";
    case Kind::Type: return "hl-t";
    case Kind::String: return "hl-s";
    case Kind::Comment: return "hl-c";
    case Kind::Number: return "hl-n";
    case Kind::Function: return "hl-f";
    case Kind::Preproc: return "hl-p";
    case Kind::Attribute: return "hl-a";
    default: return "";
  }
}

std::string toHtml(const std::string& code, const std::string& language) {
  auto escape = [](const std::string& s) {
    std::string out;
    for (char c : s) {
      switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        default: out += c;
      }
    }
    return out;
  };
  std::string out;
  bool inBlock = false;
  size_t start = 0;
  while (start <= code.size()) {
    size_t nl = code.find('\n', start);
    std::string line = code.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
    for (const Span& span : tokenize(line, language, &inBlock)) {
      std::string text = escape(line.substr(span.begin, span.end - span.begin));
      const char* cls = cssClass(span.kind);
      out += cls[0] ? "<span class=\"" + std::string(cls) + "\">" + text + "</span>" : text;
    }
    out += "\n";
    if (nl == std::string::npos) break;
    start = nl + 1;
  }
  return out;
}

}  // namespace highlight
