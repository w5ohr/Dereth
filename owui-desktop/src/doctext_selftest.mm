// `Open WebUI --doctest`: offline checks of native document text extraction.
#import <CoreText/CoreText.h>
#import <Foundation/Foundation.h>

#include <zlib.h>

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "core/doctext.h"

namespace {
int gPassed = 0, gFailed = 0;
void report(const std::string& name, bool ok, const std::string& detail = "") {
  printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", name.c_str(), detail.empty() ? "" : " - ", detail.c_str());
  (ok ? gPassed : gFailed)++;
}

void put16(std::string* s, uint32_t v) { s->push_back(static_cast<char>(v & 0xff)); s->push_back(static_cast<char>((v >> 8) & 0xff)); }
void put32(std::string* s, uint32_t v) { put16(s, v & 0xffff); put16(s, v >> 16); }

std::string rawDeflate(const std::string& data) {
  z_stream z{};
  deflateInit2(&z, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
  std::string out(deflateBound(&z, data.size()), '\0');
  z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
  z.avail_in = static_cast<uInt>(data.size());
  z.next_out = reinterpret_cast<Bytef*>(&out[0]);
  z.avail_out = static_cast<uInt>(out.size());
  deflate(&z, Z_FINISH);
  out.resize(out.size() - z.avail_out);
  deflateEnd(&z);
  return out;
}

// A zip with the given entries; `deflated` picks method 8 for all of them.
std::string makeZip(const std::vector<std::pair<std::string, std::string>>& entries, bool deflated) {
  std::string out, central;
  for (const auto& [name, data] : entries) {
    const std::string payload = deflated ? rawDeflate(data) : data;
    const uint32_t crc = static_cast<uint32_t>(crc32(0, reinterpret_cast<const Bytef*>(data.data()), static_cast<uInt>(data.size())));
    const uint32_t offset = static_cast<uint32_t>(out.size());
    out += "PK\x03\x04";
    put16(&out, 20); put16(&out, 0); put16(&out, deflated ? 8 : 0); put16(&out, 0); put16(&out, 0);
    put32(&out, crc); put32(&out, static_cast<uint32_t>(payload.size())); put32(&out, static_cast<uint32_t>(data.size()));
    put16(&out, static_cast<uint32_t>(name.size())); put16(&out, 0);
    out += name + payload;
    central += "PK\x01\x02";
    put16(&central, 20); put16(&central, 20); put16(&central, 0); put16(&central, deflated ? 8 : 0); put16(&central, 0); put16(&central, 0);
    put32(&central, crc); put32(&central, static_cast<uint32_t>(payload.size())); put32(&central, static_cast<uint32_t>(data.size()));
    put16(&central, static_cast<uint32_t>(name.size())); put16(&central, 0); put16(&central, 0); put16(&central, 0); put16(&central, 0);
    put32(&central, 0); put32(&central, offset);
    central += name;
  }
  const uint32_t cdOffset = static_cast<uint32_t>(out.size());
  out += central;
  out += "PK\x05\x06";
  put16(&out, 0); put16(&out, 0); put16(&out, static_cast<uint32_t>(entries.size())); put16(&out, static_cast<uint32_t>(entries.size()));
  put32(&out, static_cast<uint32_t>(central.size())); put32(&out, cdOffset); put16(&out, 0);
  return out;
}

// A two-page PDF drawn with Quartz, so PDFKit has a real text layer to read.
std::string makePdf() {
  NSMutableData* data = [NSMutableData data];
  CGDataConsumerRef consumer = CGDataConsumerCreateWithCFData((__bridge CFMutableDataRef)data);
  CGRect box = CGRectMake(0, 0, 300, 200);
  CGContextRef ctx = CGPDFContextCreate(consumer, &box, nullptr);
  const char* lines[] = {"Hello doctext page one", "Second page about budgets"};
  for (const char* line : lines) {
    CGPDFContextBeginPage(ctx, nullptr);
    CTFontRef font = CTFontCreateWithName(CFSTR("Helvetica"), 14, nullptr);
    NSDictionary* attrs = @{(__bridge id)kCTFontAttributeName : (__bridge id)font};
    NSAttributedString* text = [[NSAttributedString alloc] initWithString:[NSString stringWithUTF8String:line] attributes:attrs];
    CTLineRef ctLine = CTLineCreateWithAttributedString((__bridge CFAttributedStringRef)text);
    CGContextSetTextPosition(ctx, 20, 100);
    CTLineDraw(ctLine, ctx);
    CFRelease(ctLine);
    CFRelease(font);
    CGPDFContextEndPage(ctx);
  }
  CGPDFContextClose(ctx);
  CGContextRelease(ctx);
  CGDataConsumerRelease(consumer);
  return std::string(static_cast<const char*>(data.bytes), data.length);
}
}  // namespace

int runDocTextSelfTest() {
  using namespace doctext;
  printf("Open WebUI desktop document-text self-test\n\n");

  report("kindFor", kindFor("a.PDF", "") == "pdf" && kindFor("b.docx", "") == "docx" && kindFor("c.pptx", "") == "pptx" &&
                        kindFor("d.xlsx", "") == "xlsx" && kindFor("e.rtf", "") == "rtf" && kindFor("f.md", "") == "text" &&
                        kindFor("g.zip", "application/zip").empty() && kindFor("notes", "text/plain") == "text");

  const std::string docXml =
      "<?xml version=\"1.0\"?><w:document><w:body>"
      "<w:p><w:r><w:t>Title</w:t></w:r></w:p>"
      "<w:p><w:r><w:t xml:space=\"preserve\">Alpha </w:t></w:r><w:r><w:tab/><w:t>beta &amp; gamma</w:t></w:r></w:p>"
      "<w:p><w:r><w:lastRenderedPageBreak/><w:t>Page two text</w:t></w:r></w:p>"
      "</w:body></w:document>";
  const std::string text = xmlText(docXml, {"w:t"}, {"/w:p"});
  report("xmlText: runs joined, tab, entity, paragraph breaks, page break marker",
         text == "Title\nAlpha \tbeta & gamma\n\fPage two text\n", text);

  const std::string docx = makeZip({{"[Content_Types].xml", "<Types/>"}, {"word/document.xml", docXml}}, true);
  report("zip: names and a deflated entry", zipNames(docx).size() == 2 && [&] { std::string s; return zipEntry(docx, "word/document.xml", &s) && s == docXml; }());
  Document d = extract("report.docx", "", docx);
  report("docx: two pages split at Word's page break", d.ok() && d.kind == "docx" && d.pages.size() == 2 && d.pages[0].label == "p.1" &&
                                                           d.pages[0].text == "Title\nAlpha \tbeta & gamma" && d.pages[1].text == "Page two text");

  const std::string slide = "<p:sld><p:txBody><a:p><a:r><a:t>Slide %s heading</a:t></a:r></a:p><a:p><a:r><a:t>bullet</a:t></a:r></a:p></p:txBody></p:sld>";
  auto slideXml = [&](const char* n) { char buf[400]; snprintf(buf, sizeof buf, slide.c_str(), n); return std::string(buf); };
  const std::string pptx = makeZip({{"ppt/slides/slide10.xml", slideXml("ten")}, {"ppt/slides/slide2.xml", slideXml("two")}, {"ppt/slides/slide1.xml", slideXml("one")}}, false);
  Document p = extract("deck.pptx", "", pptx);
  report("pptx: slides in numeric order with their text",
         p.ok() && p.pages.size() == 3 && p.pages[0].label == "slide 1" && p.pages[1].label == "slide 2" && p.pages[2].label == "slide 10" &&
             p.pages[2].text == "Slide ten heading\nbullet");

  const std::string shared = "<sst><si><t>Name</t></si><si><r><t>Am</t></r><r><t>ount</t></r></si><si><t>Widget</t></si></sst>";
  const std::string workbook = "<workbook><sheets><sheet name=\"Sales Q3\" sheetId=\"1\" r:id=\"rId1\"/></sheets></workbook>";
  const std::string sheet = "<worksheet><sheetData><row r=\"1\"><c r=\"A1\" t=\"s\"><v>0</v></c><c r=\"B1\" t=\"s\"><v>1</v></c></row>"
                            "<row r=\"2\"><c r=\"A2\" t=\"s\"><v>2</v></c><c r=\"B2\"><v>12.5</v></c><c r=\"C2\" t=\"inlineStr\"><is><t>note</t></is></c></row></sheetData></worksheet>";
  const std::string xlsx = makeZip({{"xl/sharedStrings.xml", shared}, {"xl/workbook.xml", workbook}, {"xl/worksheets/sheet1.xml", sheet}}, true);
  Document x = extract("book.xlsx", "", xlsx);
  report("xlsx: shared strings (with runs), numbers and inline strings as tab rows, sheet name",
         x.ok() && x.pages.size() == 1 && x.pages[0].label == "sheet Sales Q3" && x.pages[0].text == "Name\tAmount\nWidget\t12.5\tnote", x.ok() ? x.pages[0].text : x.error);

  std::string big;
  for (int i = 0; i < 120; ++i) big += "Paragraph " + std::to_string(i) + " with some words in it to fill the page.\n";
  std::vector<Page> parts = paginate(big, 2000, "part");
  report("paginate: parts of about the asked size at paragraph boundaries",
         parts.size() >= 3 && parts[0].label == "part 1" && parts[0].text.size() <= 2100 && parts.back().text.rfind("Paragraph 119") != std::string::npos);

  Document t = extract("notes.md", "", "# Heading\n\nSome text here.\n");
  report("text: one part", t.ok() && t.kind == "text" && t.pages.size() == 1 && t.pages[0].text == "# Heading\n\nSome text here.");
  Document h = extract("page.html", "", "<html><head><style>p{}</style></head><body><h1>Hi &amp; bye</h1><p>One</p><p>Two</p></body></html>");
  report("html: tags stripped, entities decoded", h.ok() && h.pages[0].text.find("Hi & bye") != std::string::npos && h.pages[0].text.find("One\nTwo") != std::string::npos &&
                                                      h.pages[0].text.find("p{}") == std::string::npos, h.ok() ? h.pages[0].text : h.error);

  Document pdf = extract("two.pdf", "application/pdf", makePdf());
  report("pdf: PDFKit reads both pages", pdf.ok() && pdf.pages.size() == 2 && pdf.pages[0].text.find("Hello doctext page one") != std::string::npos &&
                                              pdf.pages[1].text.find("budgets") != std::string::npos, pdf.ok() ? pdf.pages[0].text : pdf.error);
  Document scan = extract("blank.pdf", "", "%PDF-1.4 garbage");
  report("pdf: unreadable file reports an error", !scan.ok() && !scan.error.empty());

  Document round = fromJson(toJson(x));
  report("toJson / fromJson", round.ok() && round.kind == "xlsx" && round.pages[0].label == "sheet Sales Q3");

  const std::string whole = render(d, "report.docx", 10000, "anything");
  report("render: whole document when it fits", whole.find("## report.docx (2 pages, docx)") == 0 && whole.find("[p.1] Title") != std::string::npos &&
                                                     whole.find("[p.2] Page two text") != std::string::npos);
  Document long1;
  long1.kind = "pdf";
  for (int i = 1; i <= 30; ++i) {
    Page pg;
    pg.number = i;
    pg.label = "p." + std::to_string(i);
    pg.text = i == 17 ? "The warranty period lasts twenty-four months from delivery of the compressor." : "Filler text about unrelated matters number " + std::to_string(i) + ". " + std::string(400, 'x');
    long1.pages.push_back(pg);
  }
  const std::string excerpt = render(long1, "manual.pdf", 2500, "How long is the warranty on the compressor?");
  report("render: over budget picks the matching page plus the opening and says so",
         excerpt.find("(excerpts:") != std::string::npos && excerpt.find("[p.17] The warranty") != std::string::npos &&
             excerpt.find("[p.1] Filler") != std::string::npos && excerpt.find("[p.25]") == std::string::npos && excerpt.size() <= 2700,
         std::to_string(excerpt.size()));

  printf("\n%d passed, %d failed\n", gPassed, gFailed);
  return gFailed == 0 ? 0 : 1;
}
