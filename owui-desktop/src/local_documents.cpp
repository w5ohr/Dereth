// Documents read on this Mac: an attached PDF / Word / PowerPoint / Excel /
// text file is extracted into pages (core/doctext) instead of being uploaded,
// cached under support/documents/<hash>.json (and .pdf for the viewer), and
// rides along with the prompt as a system message with page labels so the
// reply can cite "[name p.3]"; those citations become buttons under the reply
// that open the PDF column on that page. Private chats always read locally;
// server chats do when the setting is on, falling back to an upload when a
// file has no text.
#include "app.h"

#include <sys/stat.h>

#include <algorithm>
#include <map>
#include <regex>

#include "core/async.h"
#include "core/common.h"
#include "core/doctext.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace app {
namespace {

std::string documentsDir() { return core::supportDir() + "/documents"; }

// FNV-1a over the bytes: a cache key, not a security boundary.
std::string contentKey(const std::string& bytes) {
  uint64_t h = 1469598103934665603ULL;
  for (unsigned char c : bytes) {
    h ^= c;
    h *= 1099511628211ULL;
  }
  char buf[40];
  snprintf(buf, sizeof buf, "%016llx%zx", static_cast<unsigned long long>(h), bytes.size());
  return buf;
}

std::map<std::string, doctext::Document> gCache;

const doctext::Document* loadDocument(const std::string& hash) {
  auto it = gCache.find(hash);
  if (it != gCache.end()) return &it->second;
  bool ok = false;
  const std::string raw = core::readFile(documentsDir() + "/" + hash + ".json", &ok);
  if (!ok) return nullptr;
  Json j = Json::parse(raw, nullptr, false);
  if (j.is_discarded()) return nullptr;
  doctext::Document doc = doctext::fromJson(j);
  if (!doc.ok()) return nullptr;
  return &(gCache[hash] = std::move(doc));
}

struct DocRef {
  std::string hash, name, kind;
  int pages = 0;
};

std::vector<DocRef> docsIn(const Json& files) {
  std::vector<DocRef> out;
  if (!files.is_array()) return out;
  for (const auto& f : files) {
    if (!f.is_object() || f.value("type", "") != "local_document") continue;
    out.push_back({f.value("hash", ""), f.value("name", ""), f.value("kind", ""), f.value("pages", 0)});
  }
  return out;
}

}  // namespace

void App::attachDocument(size_t index) {
  if (index >= attachments.size()) return;
  attachments[index].reading = true;
  attachments[index].uploading = true;   // the send guard waits for it
  const Attachment copy = attachments[index];
  const bool isPrivate = chat.isPrivate;
  async::run([this, index, copy, isPrivate] {
    doctext::Document doc = doctext::extract(copy.name, copy.mime, copy.bytes);
    const std::string hash = contentKey(copy.bytes);
    if (doc.ok()) {
      mkdir(documentsDir().c_str(), 0700);
      core::writeFile(documentsDir() + "/" + hash + ".json", doctext::toJson(doc).dump());
      if (doc.kind == "pdf") core::writeFile(documentsDir() + "/" + hash + ".pdf", copy.bytes);
    }
    async::post([this, index, doc, hash, copy, isPrivate] {
      if (index >= attachments.size() || attachments[index].name != copy.name) return;
      Attachment& a = attachments[index];
      a.reading = false;
      if (doc.ok()) {
        a.uploading = false;
        a.local = true;
        a.hash = hash;
        a.kind = doc.kind;
        a.pages = static_cast<int>(doc.pages.size());
        a.chars = doc.chars();
        gCache[hash] = doc;
        return;
      }
      if (isPrivate) {
        a.uploading = false;
        a.failed = true;
        widgets::toastError(copy.name + ": " + doc.error + " (private chats never upload)");
        return;
      }
      widgets::toastInfo(copy.name + ": " + doc.error + " - uploading to the server instead");
      uploadAttachment(index);
    });
  });
}

Json App::localDocumentMessage(const std::vector<std::string>& messageIds, const std::string& model,
                               const std::string& query) {
  std::vector<DocRef> docs;
  for (const std::string& id : messageIds) {
    auto it = chat.messages.find(id);
    if (it == chat.messages.end()) continue;
    for (const DocRef& ref : docsIn(it->second.files)) {
      if (std::none_of(docs.begin(), docs.end(), [&](const DocRef& d) { return d.hash == ref.hash; })) docs.push_back(ref);
    }
  }
  if (docs.empty()) return Json();
  // Roughly 40% of the window in characters, shared between the documents.
  const size_t window = static_cast<size_t>(std::max(2048, contextWindowFor(model)));
  const size_t budget = std::clamp<size_t>(window * 4 * 2 / 5, 12000, 90000);
  const size_t each = std::max<size_t>(3000, budget / docs.size());
  std::string body =
      "The user attached the documents below; they were read on this Mac and their text follows, page by page. "
      "Answer from them and cite the place you used as [name p.N] (or [name slide N], [name sheet X], [name part N]).\n\n";
  for (const DocRef& ref : docs) {
    const doctext::Document* doc = loadDocument(ref.hash);
    if (!doc) {
      body += "## " + ref.name + " (its extracted text is no longer on this Mac - ask the user to attach it again)\n\n";
      continue;
    }
    const std::string block = doctext::render(*doc, ref.name, each, query);
    body += block + "\n";
    {
      std::string labels;
      for (const std::string& line : core::split(block, '\n')) {
        if (!line.empty() && line[0] == '[') labels += line.substr(0, line.find(']') + 1) + " ";
      }
      core::debugLog("documents: %s -> %zu chars: %s", ref.name.c_str(), block.size(), labels.c_str());
    }
  }
  return Json{{"role", "system"}, {"content", body}};
}

void App::drawLocalDocumentFile(const Json& file) {
  const std::string name = file.value("name", "document");
  const std::string kind = file.value("kind", "");
  const int pages = file.value("pages", 0);
  icons::inlineIcon(icons::Icon::Paperclip);
  ImGui::SameLine(0, 4);
  const std::string unit = kind == "pptx" ? "slide" : kind == "xlsx" ? "sheet" : kind == "pdf" || kind == "docx" ? "page" : "part";
  widgets::dimText("%s  -  %d %s%s, read on this Mac", name.c_str(), pages, unit.c_str(), pages == 1 ? "" : "s");
  if (kind == "pdf") {
    if (ImGui::IsItemHovered()) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
      ImGui::SetTooltip("Open in the document column");
    }
    if (ImGui::IsItemClicked()) openLocalDocument(file.value("hash", ""), name, 1);
  } else if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Text extracted locally; the file itself was not uploaded");
  }
}

void App::drawLocalDocumentRefs(const Message& message) {
  std::vector<DocRef> docs;
  for (const std::string& id : chat.branch()) {
    auto it = chat.messages.find(id);
    if (it == chat.messages.end()) continue;
    for (const DocRef& ref : docsIn(it->second.files)) {
      if (std::none_of(docs.begin(), docs.end(), [&](const DocRef& d) { return d.hash == ref.hash; })) docs.push_back(ref);
    }
  }
  if (docs.empty() || message.content.empty()) return;

  static const std::regex kRef(R"(\[([^\[\]\n]{1,120}?)[ ,]+(?:p\.|pp\.|page|slide|part|sheet)\s*([0-9]+)\]|\[(?:p\.|page)\s*([0-9]+)\])",
                               std::regex::icase);
  struct Ref {
    const DocRef* doc;
    int page;
  };
  std::vector<Ref> refs;
  auto add = [&](const DocRef* doc, int page) {
    if (!doc || page <= 0) return;
    for (const Ref& r : refs) {
      if (r.doc == doc && r.page == page) return;
    }
    refs.push_back({doc, page});
  };
  for (auto it = std::sregex_iterator(message.content.begin(), message.content.end(), kRef); it != std::sregex_iterator(); ++it) {
    const std::smatch& m = *it;
    if (m[3].matched) {   // bare [p.N]: only unambiguous with one document
      if (docs.size() == 1) add(&docs[0], atoi(m[3].str().c_str()));
      continue;
    }
    const std::string named = core::lower(core::trim(m[1].str()));
    const DocRef* hit = nullptr;
    for (const DocRef& d : docs) {
      const std::string lower = core::lower(d.name);
      const std::string stem = lower.substr(0, lower.rfind('.'));
      if (lower == named || stem == named || lower.find(named) != std::string::npos || named.find(stem) != std::string::npos) {
        hit = &d;
        break;
      }
    }
    add(hit, atoi(m[2].str().c_str()));
    if (refs.size() >= 12) break;
  }
  if (refs.empty()) return;

  const auto& palette = theme::palette();
  ImGui::Dummy(ImVec2(0, 2));
  widgets::dimText("Cited:");
  ImGui::SameLine();
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 2));
  for (size_t i = 0; i < refs.size(); ++i) {
    const Ref& r = refs[i];
    const std::string unit = r.doc->kind == "pptx" ? "slide " : r.doc->kind == "xlsx" ? "sheet " : r.doc->kind == "pdf" || r.doc->kind == "docx" ? "p." : "part ";
    const std::string label = r.doc->name + " " + unit + std::to_string(r.page) + "##ref" + std::to_string(i);
    if (r.doc->kind == "pdf") {
      if (widgets::ghostButton(label.c_str())) openLocalDocument(r.doc->hash, r.doc->name, r.page);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open the PDF at this page");
    } else {
      ImGui::PushStyleColor(ImGuiCol_Text, theme::toVec4(palette.textDim));
      ImGui::TextUnformatted(label.substr(0, label.find("##")).c_str());
      ImGui::PopStyleColor();
    }
    ImGui::SameLine();
  }
  ImGui::PopStyleVar();
  ImGui::NewLine();
}

void App::openLocalDocument(const std::string& hash, const std::string& name, int page) {
  if (hash.empty()) return;
  openDocument("local:" + hash, name, page, "");
}

}  // namespace app
