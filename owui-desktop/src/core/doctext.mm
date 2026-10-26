#include "doctext.h"

#import <Foundation/Foundation.h>
#import <Quartz/Quartz.h>

#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

#include "common.h"

namespace doctext {
namespace {

constexpr size_t kPseudoPageChars = 3500;

uint16_t u16(const std::string& s, size_t at) {
  return static_cast<uint16_t>(static_cast<unsigned char>(s[at]) | (static_cast<unsigned char>(s[at + 1]) << 8));
}
uint32_t u32(const std::string& s, size_t at) {
  return static_cast<uint32_t>(u16(s, at)) | (static_cast<uint32_t>(u16(s, at + 2)) << 16);
}

bool inflateRaw(const char* data, size_t size, size_t expected, std::string* out) {
  z_stream z{};
  if (inflateInit2(&z, -MAX_WBITS) != Z_OK) return false;
  out->resize(expected > 0 ? expected : size * 4 + 1024);
  z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data));
  z.avail_in = static_cast<uInt>(size);
  size_t produced = 0;
  int rc = Z_OK;
  do {
    if (produced == out->size()) out->resize(out->size() * 2);
    z.next_out = reinterpret_cast<Bytef*>(&(*out)[produced]);
    z.avail_out = static_cast<uInt>(out->size() - produced);
    rc = inflate(&z, Z_NO_FLUSH);
    produced = out->size() - z.avail_out;
  } while (rc == Z_OK);
  inflateEnd(&z);
  if (rc != Z_STREAM_END) return false;
  out->resize(produced);
  return true;
}

// Central directory walk: calls fn(name, method, compSize, uncompSize, localOffset).
template <typename Fn>
void walkZip(const std::string& zip, Fn fn) {
  if (zip.size() < 22) return;
  size_t eocd = std::string::npos;
  const size_t start = zip.size() >= 65557 ? zip.size() - 65557 : 0;
  for (size_t i = zip.size() - 22; i + 1 > start; --i) {
    if (zip.compare(i, 4, "PK\x05\x06") == 0) {
      eocd = i;
      break;
    }
    if (i == 0) break;
  }
  if (eocd == std::string::npos) return;
  const uint16_t count = u16(zip, eocd + 10);
  size_t at = u32(zip, eocd + 16);
  for (uint16_t n = 0; n < count && at + 46 <= zip.size(); ++n) {
    if (zip.compare(at, 4, "PK\x01\x02") != 0) break;
    const uint16_t method = u16(zip, at + 10);
    const uint32_t comp = u32(zip, at + 20), uncomp = u32(zip, at + 24);
    const uint16_t nameLen = u16(zip, at + 28), extraLen = u16(zip, at + 30), commentLen = u16(zip, at + 32);
    const uint32_t local = u32(zip, at + 42);
    if (at + 46 + nameLen > zip.size()) break;
    if (!fn(zip.substr(at + 46, nameLen), method, comp, uncomp, local)) break;
    at += 46 + nameLen + extraLen + commentLen;
  }
}

std::string decodeEntities(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] != '&') {
      out += s[i];
      continue;
    }
    const size_t semi = s.find(';', i);
    if (semi == std::string::npos || semi - i > 10) {
      out += s[i];
      continue;
    }
    const std::string name = s.substr(i + 1, semi - i - 1);
    if (name == "amp") out += '&';
    else if (name == "lt") out += '<';
    else if (name == "gt") out += '>';
    else if (name == "quot") out += '"';
    else if (name == "apos") out += '\'';
    else if (name == "nbsp") out += ' ';
    else if (!name.empty() && name[0] == '#') {
      uint32_t cp = 0;
      if (name.size() > 1 && (name[1] == 'x' || name[1] == 'X')) cp = static_cast<uint32_t>(strtoul(name.c_str() + 2, nullptr, 16));
      else cp = static_cast<uint32_t>(strtoul(name.c_str() + 1, nullptr, 10));
      if (cp < 0x80) out += static_cast<char>(cp);
      else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
      else if (cp < 0x10000) { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
      else { out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    } else {
      out += s[i];
      continue;
    }
    i = semi;
  }
  return out;
}

std::string tidy(std::string text) {
  // Collapse runs of blank lines, trim trailing spaces.
  std::string out;
  int blank = 0;
  for (const std::string& raw : core::split(text, '\n')) {
    std::string line = raw;
    while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) line.pop_back();
    if (line.empty()) {
      if (++blank > 1) continue;
    } else {
      blank = 0;
    }
    out += line + "\n";
  }
  while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) out.pop_back();
  return out;
}

std::string attr(const std::string& tag, const std::string& name) {
  const size_t at = tag.find(name + "=\"");
  if (at == std::string::npos) return "";
  const size_t start = at + name.size() + 2;
  const size_t end = tag.find('"', start);
  return end == std::string::npos ? "" : tag.substr(start, end - start);
}

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  return s;
}

// ---- formats

Document fromPdf(const std::string& bytes) {
  Document doc;
  doc.kind = "pdf";
  @autoreleasepool {
    NSData* data = [NSData dataWithBytes:bytes.data() length:bytes.size()];
    PDFDocument* pdf = [[PDFDocument alloc] initWithData:data];
    if (!pdf) {
      doc.error = "PDFKit could not read this file";
      return doc;
    }
    if (pdf.isLocked) {
      doc.error = "The PDF is password-protected";
      return doc;
    }
    const NSUInteger count = pdf.pageCount;
    for (NSUInteger i = 0; i < count; ++i) {
      PDFPage* page = [pdf pageAtIndex:i];
      NSString* text = page ? page.string : nil;
      Page p;
      p.number = static_cast<int>(i) + 1;
      p.label = "p." + std::to_string(p.number);
      p.text = tidy(text ? std::string(text.UTF8String ? text.UTF8String : "") : "");
      doc.pages.push_back(p);
    }
  }
  if (doc.chars() == 0) {
    doc.error = doc.pages.empty() ? "The PDF has no pages" : "The PDF has no text layer (a scan?) - nothing to read";
    doc.pages.clear();
  }
  return doc;
}

Document fromDocx(const std::string& bytes) {
  Document doc;
  doc.kind = "docx";
  std::string xml;
  if (!zipEntry(bytes, "word/document.xml", &xml)) {
    doc.error = "Not a Word document (no word/document.xml)";
    return doc;
  }
  const std::string text = xmlText(xml, {"w:t"}, {"/w:p", "w:br", "w:cr"});
  // Word writes lastRenderedPageBreak where its layout broke pages; honour
  // those when present, else fall back to parts of a few thousand characters.
  if (text.find('\f') != std::string::npos) {
    int n = 0;
    for (const std::string& part : core::split(text, '\f')) {
      const std::string body = tidy(part);
      if (body.empty() && n > 0) continue;
      Page p;
      p.number = ++n;
      p.label = "p." + std::to_string(n);
      p.text = body;
      doc.pages.push_back(p);
    }
  } else {
    doc.pages = paginate(tidy(text), kPseudoPageChars, "part");
  }
  if (doc.pages.empty()) doc.error = "The document has no text";
  return doc;
}

Document fromPptx(const std::string& bytes) {
  Document doc;
  doc.kind = "pptx";
  std::vector<std::pair<int, std::string>> slides;
  for (const std::string& name : zipNames(bytes)) {
    if (name.rfind("ppt/slides/slide", 0) == 0 && core::endsWith(name, ".xml")) {
      slides.push_back({atoi(name.c_str() + 16), name});
    }
  }
  std::sort(slides.begin(), slides.end());
  for (const auto& [number, name] : slides) {
    std::string xml;
    if (!zipEntry(bytes, name, &xml)) continue;
    Page p;
    p.number = number;
    p.label = "slide " + std::to_string(number);
    p.text = tidy(xmlText(xml, {"a:t"}, {"/a:p"}));
    doc.pages.push_back(p);
  }
  if (doc.pages.empty()) doc.error = "Not a PowerPoint file (no slides)";
  return doc;
}

Document fromXlsx(const std::string& bytes) {
  Document doc;
  doc.kind = "xlsx";
  std::vector<std::string> shared;
  std::string xml;
  if (zipEntry(bytes, "xl/sharedStrings.xml", &xml)) {
    size_t at = 0;
    while ((at = xml.find("<si>", at)) != std::string::npos) {
      const size_t end = xml.find("</si>", at);
      if (end == std::string::npos) break;
      shared.push_back(xmlText(xml.substr(at, end - at), {"t"}, {}));
      at = end + 5;
    }
  }
  std::vector<std::string> sheetNames;
  if (zipEntry(bytes, "xl/workbook.xml", &xml)) {
    size_t at = 0;
    while ((at = xml.find("<sheet ", at)) != std::string::npos) {
      const size_t end = xml.find('>', at);
      if (end == std::string::npos) break;
      sheetNames.push_back(decodeEntities(attr(xml.substr(at, end - at), "name")));
      at = end;
    }
  }
  for (int n = 1; n <= 200; ++n) {
    if (!zipEntry(bytes, "xl/worksheets/sheet" + std::to_string(n) + ".xml", &xml)) break;
    std::string text;
    size_t at = 0;
    while ((at = xml.find("<row", at)) != std::string::npos) {
      const size_t rowEnd = xml.find("</row>", at);
      if (rowEnd == std::string::npos) break;
      const std::string row = xml.substr(at, rowEnd - at);
      std::string line;
      size_t c = 0;
      while ((c = row.find("<c ", c)) != std::string::npos) {
        const size_t tagEnd = row.find('>', c);
        if (tagEnd == std::string::npos) break;
        const std::string tag = row.substr(c, tagEnd - c);
        const std::string type = attr(tag, "t");
        std::string value;
        if (tag.back() != '/') {
          const size_t cellEnd = row.find("</c>", tagEnd);
          const std::string cell = row.substr(tagEnd + 1, cellEnd == std::string::npos ? std::string::npos : cellEnd - tagEnd - 1);
          if (type == "s") {
            const std::string v = xmlText(cell, {"v"}, {});
            const size_t idx = static_cast<size_t>(atoi(v.c_str()));
            value = idx < shared.size() ? shared[idx] : "";
          } else if (type == "inlineStr") {
            value = xmlText(cell, {"t"}, {});
          } else {
            value = xmlText(cell, {"v"}, {});
          }
        }
        if (!line.empty()) line += "\t";
        line += value;
        c = tagEnd;
      }
      while (!line.empty() && line.back() == '\t') line.pop_back();
      if (!line.empty()) text += line + "\n";
      at = rowEnd + 6;
    }
    Page p;
    p.number = n;
    const std::string name = n - 1 < static_cast<int>(sheetNames.size()) ? sheetNames[n - 1] : "Sheet" + std::to_string(n);
    p.label = "sheet " + name;
    p.text = tidy(text);
    doc.pages.push_back(p);
  }
  if (doc.pages.empty()) doc.error = "Not an Excel file (no worksheets)";
  return doc;
}

Document fromTextutil(const std::string& ext, const std::string& bytes) {
  Document doc;
  doc.kind = "rtf";
  static int counter = 0;
  const std::string path = core::cacheDir() + "/doctext-" + std::to_string(getpid()) + "-" + std::to_string(++counter) + "." + ext;
  if (!core::writeFile(path, bytes)) {
    doc.error = "Could not write a temporary copy";
    return doc;
  }
  chmod(path.c_str(), 0600);
  std::string text;
  const std::string command = "/usr/bin/textutil -convert txt -stdout -encoding UTF-8 '" + path + "' 2>/dev/null";
  if (FILE* pipe = popen(command.c_str(), "r")) {
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, pipe)) > 0) text.append(buf, n);
    pclose(pipe);
  }
  unlink(path.c_str());
  doc.pages = paginate(tidy(text), kPseudoPageChars, "part");
  if (doc.pages.empty()) doc.error = "textutil could not read this file";
  return doc;
}

std::string stripHtml(const std::string& html) {
  std::string s = html;
  for (const char* block : {"script", "style"}) {
    size_t at;
    while ((at = lower(s).find(std::string("<") + block)) != std::string::npos) {
      const size_t end = lower(s).find(std::string("</") + block, at);
      s.erase(at, end == std::string::npos ? std::string::npos : end - at + strlen(block) + 3);
    }
  }
  std::string out;
  bool inTag = false;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '<') {
      inTag = true;
      const std::string head = lower(s.substr(i, 6));
      if (head.rfind("</p", 0) == 0 || head.rfind("<br", 0) == 0 || head.rfind("</div", 0) == 0 || head.rfind("</h", 0) == 0 ||
          head.rfind("</li", 0) == 0 || head.rfind("</tr", 0) == 0) {
        out += '\n';
      }
      continue;
    }
    if (s[i] == '>') {
      inTag = false;
      continue;
    }
    if (!inTag) out += s[i];
  }
  return decodeEntities(out);
}

}  // namespace

size_t Document::chars() const {
  size_t n = 0;
  for (const Page& p : pages) n += p.text.size();
  return n;
}

std::string kindFor(const std::string& name, const std::string& mime) {
  const std::string ext = lower(core::extension(name));
  if (ext == "pdf" || mime == "application/pdf") return "pdf";
  if (ext == "docx" || ext == "docm") return "docx";
  if (ext == "pptx") return "pptx";
  if (ext == "xlsx" || ext == "xlsm") return "xlsx";
  if (ext == "rtf" || ext == "doc" || ext == "odt" || ext == "rtfd" || ext == "webarchive") return "rtf";
  static const std::set<std::string> kText = {"txt", "md", "markdown", "csv", "tsv", "json", "xml", "yaml", "yml", "log", "html", "htm",
                                             "py", "js", "ts", "tsx", "jsx", "c", "cc", "cpp", "h", "hpp", "m", "mm", "swift", "go",
                                             "rs", "java", "kt", "rb", "sh", "zsh", "sql", "css", "toml", "ini", "cfg", "tex", "rst"};
  if (kText.count(ext) || mime.rfind("text/", 0) == 0) return "text";
  return "";
}

bool supported(const std::string& name, const std::string& mime) { return !kindFor(name, mime).empty(); }

Document extract(const std::string& name, const std::string& mime, const std::string& bytes) {
  const std::string kind = kindFor(name, mime);
  if (kind == "pdf") return fromPdf(bytes);
  if (kind == "docx") return fromDocx(bytes);
  if (kind == "pptx") return fromPptx(bytes);
  if (kind == "xlsx") return fromXlsx(bytes);
  if (kind == "rtf") return fromTextutil(lower(core::extension(name)), bytes);
  Document doc;
  doc.kind = "text";
  if (kind.empty()) {
    doc.error = "Not a document type this app reads";
    return doc;
  }
  const std::string ext = lower(core::extension(name));
  std::string text = (ext == "html" || ext == "htm") ? stripHtml(bytes) : bytes;
  if (text.find('\0') != std::string::npos) {
    doc.error = "The file is binary";
    return doc;
  }
  doc.pages = paginate(tidy(text), kPseudoPageChars, "part");
  if (doc.pages.empty()) doc.error = "The file is empty";
  return doc;
}

Json toJson(const Document& doc) {
  Json pages = Json::array();
  for (const Page& p : doc.pages) pages.push_back(Json{{"n", p.number}, {"label", p.label}, {"text", p.text}});
  return Json{{"kind", doc.kind}, {"pages", pages}};
}

Document fromJson(const Json& j) {
  Document doc;
  if (!j.is_object()) {
    doc.error = "bad cache entry";
    return doc;
  }
  doc.kind = j.value("kind", "");
  for (const auto& p : j.value("pages", Json::array())) {
    Page page;
    page.number = p.value("n", 0);
    page.label = p.value("label", "");
    page.text = p.value("text", "");
    doc.pages.push_back(page);
  }
  return doc;
}

// ---- zip

std::vector<std::string> zipNames(const std::string& zip) {
  std::vector<std::string> out;
  walkZip(zip, [&](const std::string& name, uint16_t, uint32_t, uint32_t, uint32_t) {
    out.push_back(name);
    return true;
  });
  return out;
}

bool zipEntry(const std::string& zip, const std::string& entryName, std::string* out) {
  bool found = false;
  walkZip(zip, [&](const std::string& name, uint16_t method, uint32_t comp, uint32_t uncomp, uint32_t local) {
    if (name != entryName) return true;
    if (local + 30 > zip.size() || zip.compare(local, 4, "PK\x03\x04") != 0) return false;
    const size_t dataAt = local + 30 + u16(zip, local + 26) + u16(zip, local + 28);
    if (dataAt + comp > zip.size()) return false;
    if (method == 0) {
      *out = zip.substr(dataAt, comp);
      found = true;
    } else if (method == 8) {
      found = inflateRaw(zip.data() + dataAt, comp, uncomp, out);
    }
    return false;
  });
  return found;
}

// ---- xml

std::string xmlText(const std::string& xml, const std::vector<std::string>& textTags, const std::vector<std::string>& breakTags) {
  std::string out;
  int capturing = 0;
  std::string openText;   // the text tag currently open
  size_t i = 0;
  while (i < xml.size()) {
    if (xml[i] != '<') {
      const size_t next = xml.find('<', i);
      const std::string run = xml.substr(i, next == std::string::npos ? std::string::npos : next - i);
      if (capturing) out += decodeEntities(run);
      if (next == std::string::npos) break;
      i = next;
      continue;
    }
    const size_t end = xml.find('>', i);
    if (end == std::string::npos) break;
    const std::string tag = xml.substr(i + 1, end - i - 1);
    i = end + 1;
    if (tag.empty() || tag[0] == '?' || tag[0] == '!') continue;
    const bool selfClosing = tag.back() == '/';
    // A close tag keeps its slash in the name ("/w:p"); the search for the
    // name's end starts after it, or the name would be empty.
    const size_t space = tag.find_first_of(" /\t\n", tag[0] == '/' ? 1 : 0);
    const std::string name = tag.substr(0, space);
    if (name == "w:lastRenderedPageBreak" || (name == "w:br" && attr(tag, "w:type") == "page")) {
      out += '\f';
      continue;
    }
    if (name == "w:tab") {
      out += '\t';
      continue;
    }
    if (std::find(breakTags.begin(), breakTags.end(), name) != breakTags.end()) {
      out += '\n';
      continue;
    }
    if (!selfClosing && std::find(textTags.begin(), textTags.end(), name) != textTags.end()) {
      capturing++;
      openText = name;
    } else if (name.size() > 1 && name[0] == '/' && capturing && name.substr(1) == openText) {
      capturing--;
    }
  }
  return out;
}

std::vector<Page> paginate(const std::string& text, size_t pageChars, const std::string& labelPrefix) {
  std::vector<Page> out;
  std::string current;
  auto flush = [&] {
    const std::string body = tidy(current);
    current.clear();
    if (body.empty()) return;
    Page p;
    p.number = static_cast<int>(out.size()) + 1;
    p.label = labelPrefix + " " + std::to_string(p.number);
    p.text = body;
    out.push_back(p);
  };
  for (const std::string& para : core::split(text, '\n')) {
    if (!current.empty() && current.size() + para.size() > pageChars) flush();
    if (para.size() > pageChars * 2) {   // one enormous line: cut it
      for (size_t at = 0; at < para.size(); at += pageChars) {
        current = para.substr(at, pageChars);
        flush();
      }
      continue;
    }
    current += para + "\n";
  }
  flush();
  return out;
}

// ---- render

namespace {

std::vector<std::string> tokens(const std::string& text) {
  static const std::set<std::string> kStop = {"the", "and", "for", "that", "this", "with", "what", "which", "from", "are",
                                             "was", "were", "have", "has", "does", "about", "into", "your", "you", "its",
                                             "how", "why", "when", "where", "who", "say", "says", "tell", "please", "there"};
  std::vector<std::string> out;
  std::string word;
  auto push = [&] {
    // Crude stemming: "termination" and "terminate" both become "termin".
    if (word.size() >= 3 && !kStop.count(word)) out.push_back(word.size() > 6 ? word.substr(0, 6) : word);
    word.clear();
  };
  for (unsigned char c : text) {
    if (isalnum(c) || c >= 0x80) word += static_cast<char>(tolower(c));
    else push();
  }
  push();
  return out;
}

struct Chunk {
  size_t page = 0;
  size_t order = 0;
  std::string text;
  float score = 0.0f;
};

}  // namespace

std::string render(const Document& doc, const std::string& name, size_t budgetChars, const std::string& query) {
  std::string head = "## " + name + " (" + std::to_string(doc.pages.size()) + (doc.pages.size() == 1 ? " page" : " pages") + ", " + doc.kind + ")\n";
  size_t total = head.size();
  for (const Page& p : doc.pages) total += p.label.size() + p.text.size() + 4;
  if (total <= budgetChars) {
    std::string out = head;
    for (const Page& p : doc.pages) {
      if (p.text.empty()) continue;
      out += "[" + p.label + "] " + p.text + "\n";
    }
    return out;
  }

  // Too big: chunk every page, score chunks against the question, keep the best.
  std::vector<Chunk> chunks;
  for (size_t pi = 0; pi < doc.pages.size(); ++pi) {
    std::string current;
    auto flush = [&] {
      if (core::trim(current).empty()) {
        current.clear();
        return;
      }
      Chunk c;
      c.page = pi;
      c.order = chunks.size();
      c.text = core::trim(current);
      chunks.push_back(c);
      current.clear();
    };
    for (const std::string& line : core::split(doc.pages[pi].text, '\n')) {
      if (!current.empty() && current.size() + line.size() > 1000) flush();
      current += line + "\n";
    }
    flush();
  }
  if (chunks.empty()) return head + "(no text)\n";

  const std::vector<std::string> q = tokens(query);
  std::set<std::string> terms(q.begin(), q.end());
  std::map<std::string, int> df;
  std::vector<std::map<std::string, int>> tf(chunks.size());
  for (size_t i = 0; i < chunks.size(); ++i) {
    for (const std::string& t : tokens(chunks[i].text)) {
      if (terms.count(t) && tf[i][t]++ == 0) df[t]++;
    }
  }
  for (size_t i = 0; i < chunks.size(); ++i) {
    float score = 0.0f;
    for (const auto& [t, n] : tf[i]) {
      score += (1.0f + std::log(static_cast<float>(n))) * std::log(1.0f + static_cast<float>(chunks.size()) / static_cast<float>(df[t]));
    }
    chunks[i].score = score;
  }
  std::vector<size_t> order(chunks.size());
  for (size_t i = 0; i < order.size(); ++i) order[i] = i;
  std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
    if (chunks[a].score != chunks[b].score) return chunks[a].score > chunks[b].score;
    return a < b;
  });
  std::set<size_t> chosen = {0};   // the opening always rides along (title, abstract, headings)
  size_t used = head.size() + chunks[0].text.size() + 16;
  for (size_t idx : order) {
    if (chosen.count(idx)) continue;
    if (chunks[idx].score <= 0.0f && chosen.size() > 1) break;
    if (used + chunks[idx].text.size() + 16 > budgetChars) continue;
    chosen.insert(idx);
    used += chunks[idx].text.size() + 16;
  }
  std::string out = head + "(excerpts: " + std::to_string(chosen.size()) + " of " + std::to_string(chunks.size()) +
                    " parts, chosen for the question; the rest was left out)\n";
  size_t last = static_cast<size_t>(-1);
  for (size_t idx : chosen) {   // std::set iterates in document order
    if (last != static_cast<size_t>(-1) && idx != last + 1) out += "...\n";
    out += "[" + doc.pages[chunks[idx].page].label + "] " + chunks[idx].text + "\n";
    last = idx;
  }
  return out;
}

}  // namespace doctext
