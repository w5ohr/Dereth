// Editor bridge (#96): "src/app.cpp:120" in a reply opens that line in the
// editor of your choice (VS Code, Cursor, Zed, Xcode, Sublime, or your own
// command), and an editor sends a selection back through
// owui://explain?file=...&line=...&text=... . The pure part: recognising
// file references, resolving them, and building the open command.
// App-free; --editortest covers it.
#pragma once

#include <string>
#include <vector>

namespace editorbridge {

struct FileRef {
  std::string path;
  int line = 0;
  int column = 0;
};

// "path:line[:col]" where path has a slash or an extension; never a URL or
// a clock time. "~/x.py:3" and "/abs/x.h:3:4" count.
bool parseFileRef(const std::string& token, FileRef* out);
std::string linkFor(const FileRef& ref);                 // "editor://path:line[:col]"
bool parseLink(const std::string& link, FileRef* out);  // the inverse

// "~" expands with `home`; a relative path joins `projectRoot` when given.
std::string resolvePath(const std::string& path, const std::string& projectRoot, const std::string& home);

enum class Editor { VSCode, Cursor, Zed, Xcode, Sublime, Custom };
Editor editorFrom(const std::string& id);   // "vscode" | "cursor" | "zed" | "xcode" | "sublime" | "custom"
std::string editorId(Editor editor);
std::string editorName(Editor editor);
// argv to run; `customTemplate` uses {file} {line} {col}.
std::vector<std::string> openCommand(Editor editor, const FileRef& absolute, const std::string& customTemplate);
std::vector<std::string> splitArgs(const std::string& command);   // quote-aware

// The prompt an editor's "explain selection" turns into.
std::string explainPrompt(const std::string& file, int line, const std::string& language, const std::string& text);

}  // namespace editorbridge
