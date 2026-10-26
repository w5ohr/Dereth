// Repo map: a compact index of the symbols a project defines - classes,
// functions, methods, types, exported names - one line per symbol with the
// file and line, built with per-language regexes (ctags-style, no parser).
// Sent instead of whole files it gives the model the shape of a codebase at
// a tenth of the tokens. App-free; --repomaptest covers the extractors.
#pragma once

#include <string>
#include <vector>

namespace repomap {

struct Symbol {
  std::string kind;      // class | func | method | type | const | export | section
  std::string name;
  std::string signature; // the line, trimmed (params etc.), capped
  int line = 0;
};

struct FileMap {
  std::string path;      // relative
  std::string language;
  std::vector<Symbol> symbols;
};

// Language from the file name; "" when the file is not indexed.
std::string languageOf(const std::string& path);

// The symbols in one file's text.
std::vector<Symbol> extract(const std::string& language, const std::string& text);

// Renders the map as the block that goes to the model, trimmed to `maxChars`
// (files with the most symbols kept whole first, then truncated with a note).
std::string render(const std::vector<FileMap>& files, size_t maxChars);

}  // namespace repomap
