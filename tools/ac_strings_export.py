#!/usr/bin/env python3
"""Extract string tables from client_local_English.dat (never extracted before).

Exact ACE format (ACE.DatLoader FileTypes/StringTable.cs + Entity/StringTableData.cs):
  StringTable: Id(u32) Language(u32) Unknown(u8) SmartArray<StringTableData>
  StringTableData: Id(u32), u16 varnames{ustr}, u16 vars{ustr}, u32 strings{ustr},
                   u32 comments{u32}, u8
  ustr = compressed-uint length + plain UTF-16LE chars. SmartArray count = compressed uint.

The fifteen 0x23 tables decode cleanly with this layout (0x2300000E = the 873 retail
CHARACTER TITLES). The 101 0x21 tables use a different (undocumented) layout — skipped,
flagged for follow-up. Output:

  assets/acstrings.json  {"titles":[...873...], "tables":{didhex:{sid:[strings]}}}
"""
import os, json, struct, importlib.util

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
spec = importlib.util.spec_from_file_location("ame", os.path.join(ROOT, "tools", "ac_model_export.py"))
ame = importlib.util.module_from_spec(spec); spec.loader.exec_module(ame)


class R:
    def __init__(s, d): s.d = d; s.o = 0
    def u8(s): v = s.d[s.o]; s.o += 1; return v
    def u16(s): v = struct.unpack_from('<H', s.d, s.o)[0]; s.o += 2; return v
    def u32(s): v = struct.unpack_from('<I', s.d, s.o)[0]; s.o += 4; return v
    def cuint(s):
        b0 = s.u8()
        if not b0 & 0x80: return b0
        b1 = s.u8()
        if not b0 & 0x40: return ((b0 & 0x7f) << 8) | b1
        return ((((b0 & 0x3f) << 8) | b1) << 16) | s.u16()
    def ustr(s):
        n = s.cuint()
        out = []
        for _ in range(n): out.append(chr(s.u16()))
        return ''.join(out)


def parse_table(raw):
    r = R(raw)
    fid = r.u32(); lang = r.u32(); r.u8()
    n = r.cuint()
    data = {}
    for _ in range(n):
        sid = r.u32()
        for _ in range(r.u16()): r.ustr()          # varnames (unused)
        for _ in range(r.u16()): r.ustr()          # vars (unused)
        strs = [r.ustr() for _ in range(r.u32())]
        for _ in range(r.u32()): r.u32()           # comments
        r.u8()
        if strs: data[sid] = strs
    return fid, lang, data


def main():
    loc = ame.DatReader(os.path.join(ROOT, "acdata", "client_local_English.dat"))
    tables, titles, ok, fail = {}, [], 0, 0
    for did in sorted(loc.files):
        if (did >> 24) != 0x23: continue
        try:
            fid, lang, data = parse_table(loc.read(did))
            tables["%08x" % did] = {str(k): v for k, v in data.items()}
            ok += 1
            if did == 0x2300000E:
                for sid in sorted(data): titles.extend(data[sid])
        except Exception:
            fail += 1
    out = os.path.join(ROOT, "assets", "acstrings.json")
    json.dump(dict(titles=titles, tables=tables), open(out, "w", encoding="utf-8"),
              ensure_ascii=False, separators=(",", ":"))
    total = sum(len(v) for t in tables.values() for v in t.values())
    print(f"0x23 tables decoded: {ok} (failed {fail}) · strings: {total} · titles: {len(titles)} · {os.path.getsize(out)//1024} KB")
    print("  title samples:", titles[:6])


if __name__ == "__main__":
    main()
