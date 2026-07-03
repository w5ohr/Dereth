#!/usr/bin/env python3
"""Export every readable Book weenie's pages from the ACE world DB.

Walks 3-Core WeenieDefaults/SQL/Book/**.sql; each file's name carries the book's
display name and its `weenie_properties_book_page_data` rows carry the page text
exactly as retail served it.

Output assets/acbooks.json: { nameLower: {n: DisplayName, pages:[text,...]} }
(pages ordered by page_Id; empty/whitespace pages dropped; lowest wcid wins a name).
"""
import os, re, json

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE = os.path.join(ROOT, "ACE-World-16PY-master", "Database", "3-Core",
                    "9 WeenieDefaults", "SQL", "Book")
OUT = os.path.join(ROOT, "assets", "acbooks.json")

ROW = re.compile(r"\((\d+),\s*(\d+),\s*0x[0-9A-Fa-f]+,\s*'(?:[^'\\]|''|\\.)*',\s*'(?:[^'\\]|''|\\.)*',\s*(?:True|False|\d),\s*'((?:[^'\\]|''|\\.)*)'\)", re.S)

def unsql(s):
    return s.replace("''", "'").replace("\\'", "'").replace("\\r\\n", "\n").replace("\\n", "\n")

def main():
    books = {}
    seen_wcid = {}
    files = []
    for root, _, fns in os.walk(BASE):
        for fn in fns:
            m = re.match(r"(\d+) (.+)\.sql$", fn)
            if m: files.append((int(m.group(1)), m.group(2), os.path.join(root, fn)))
    for wcid, name, path in sorted(files):
        key = name.lower()
        if key in seen_wcid and seen_wcid[key] <= wcid: continue
        src = open(path, encoding="utf-8", errors="replace").read()
        blk = re.search(r"INSERT INTO `weenie_properties_book_page_data`.*?;\n", src, re.S)
        if not blk: continue
        pages = sorted(((int(pid), unsql(txt)) for _, pid, txt in ROW.findall(blk.group(0))), key=lambda x: x[0])
        texts = [t.strip() for _, t in pages if t.strip()]
        if not texts: continue
        books[key] = dict(n=name, pages=texts[:12])
        seen_wcid[key] = wcid
    json.dump(books, open(OUT, "w"), separators=(",", ":"), ensure_ascii=False)
    npages = sum(len(b["pages"]) for b in books.values())
    print(f"books: {len(books):,} · pages: {npages:,} · {os.path.getsize(OUT)//1024} KB")
    for probe in ("shadow note translation", "the book of eibhil"):
        if probe in books: print(f"  {probe}: {len(books[probe]['pages'])} pages · '{books[probe]['pages'][0][:70]}…'")

if __name__ == "__main__":
    main()
