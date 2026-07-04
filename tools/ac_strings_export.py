#!/usr/bin/env python3
"""Extract the UI/string tables from client_local_English.dat (never extracted before).

The local dat holds 101 StringTable resources (0x21) + 15 auxiliary tables (0x23).
Formats vary by table and some payloads are lightly scrambled, so this extractor uses a
heuristic UTF-16 harvest � WHICH DOES NOT WORK YET: the 0x21 payloads are CIPHERED
(AC obfuscates local string tables). TODO: port the decode from ACEmulator
ACE.DatLoader/FileTypes/StringTable.cs, then re-run. Output:

  assets/acstrings.json   {"tables": {didhex: [strings...]}, "counts": {...}}
"""
import os, json, importlib.util

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
spec = importlib.util.spec_from_file_location("ame", os.path.join(ROOT, "tools", "ac_model_export.py"))
ame = importlib.util.module_from_spec(spec); spec.loader.exec_module(ame)


def harvest_utf16(raw, min_chars=3):
    out, i, n = [], 0, len(raw)
    cur = []
    while i + 1 < n:
        lo, hi = raw[i], raw[i + 1]
        code = lo | (hi << 8)
        printable = (32 <= code < 0xD800 and code != 0x7F) and (hi == 0 or 0x00A0 <= code <= 0x2122)
        if printable:
            cur.append(chr(code)); i += 2
        else:
            if len(cur) >= min_chars:
                s = "".join(cur).strip()
                if s and any(c.isalpha() for c in s): out.append(s)
            cur = []; i += 2 if hi == 0 and lo == 0 else 1
    if len(cur) >= min_chars:
        s = "".join(cur).strip()
        if s and any(c.isalpha() for c in s): out.append(s)
    return out


def main():
    loc = ame.DatReader(os.path.join(ROOT, "acdata", "client_local_English.dat"))
    tables, counts = {}, {}
    for did in sorted(loc.files):
        if (did >> 24) not in (0x21, 0x23):
            continue
        strs = harvest_utf16(loc.read(did))
        if strs:
            key = "%08x" % did
            tables[key] = strs
            counts[key] = len(strs)
    out = os.path.join(ROOT, "assets", "acstrings.json")
    json.dump(dict(tables=tables, counts=counts), open(out, "w", encoding="utf-8"),
              ensure_ascii=False, separators=(",", ":"))
    total = sum(counts.values())
    print(f"tables: {len(tables)} · strings: {total} · {os.path.getsize(out)//1024} KB")
    # a taste of what's inside
    for k in list(tables)[:3]:
        print(" ", k, "->", [s[:40] for s in tables[k][:4]])


if __name__ == "__main__":
    main()
