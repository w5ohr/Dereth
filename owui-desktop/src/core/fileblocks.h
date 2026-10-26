// Finds the file blocks in a reply: fenced code blocks that name the file
// they belong to - in the fence's info string (```python path=src/a.py,
// ```ts title="x.ts", ```js src/app.js), in a first-line comment
// (// file: src/a.py, # path: a.py, <!-- a.html -->), or in the nearest
// non-empty line above the fence when it is a heading, bold or inline-code
// path (### src/a.py, **a.py**, `src/a.py`). App-free; --applytest covers it.
#pragma once

#include <string>
#include <vector>

namespace fileblocks {

struct Block {
  std::string path;       // as written (relative or absolute)
  std::string language;
  std::string code;       // the body, without a file-comment first line
  std::string source;     // "info" | "comment" | "heading" - where the path came from
};

std::vector<Block> extract(const std::string& markdown);

// True when the token looks like a file path: has an extension or a slash,
// no spaces, not a URL.
bool looksLikePath(const std::string& token);

}  // namespace fileblocks
