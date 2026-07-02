#!/usr/bin/env python3
"""Extract the real Dereth from the AC client data files (user-supplied, gitignored acdata/).

Reads the Turbine DAT BTree format (as documented by the ACEmulator project):
  - client_cell_1.dat  -> every outdoor landblock: 9x9 height indices + 9x9 terrain/road words
  - client_portal.dat  -> the Region file (0x13000000) LandHeightTable (256 floats)

Outputs:
  - assets/acmap.png        2041x2041 RGBA data texture: R = height index (byte, into the height
                            table), G = terrain type (0..31), B = road bits (0..3), A = 255
  - acdata/heights.json     the 256-float land height table (embedded into index.html)
  - acdata/verify-*.png     hillshade / type previews for eyeballing against the official map.png
"""
import struct, sys, os, json
import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ACD = os.path.join(ROOT, "acdata")

class DatReader:
    def __init__(self, path):
        self.f = open(path, "rb")
        self.f.seek(0x140)
        hdr = self.f.read(0x24)
        (self.file_type, self.block_size, self.file_size, self.data_set,
         self.data_subset, self.free_head, self.free_tail, self.free_count,
         self.btree) = struct.unpack("<9I", hdr)
        self.files = {}
        self._walk(self.btree)

    def _read_chain(self, offset, want=None):
        """Follow the sector chain from `offset`, returning payload bytes."""
        out = bytearray()
        bs = self.block_size
        while offset:
            self.f.seek(offset)
            block = self.f.read(bs)
            nxt = struct.unpack("<I", block[:4])[0] & 0x7FFFFFFF
            out += block[4:]
            offset = nxt
            if want is not None and len(out) >= want:
                break
        return bytes(out[:want]) if want is not None else bytes(out)

    def _walk(self, offset):
        # directory: u32 branches[62], u32 count, entries[count] of 6 u32s
        raw = self._read_chain(offset, 62*4 + 4 + 61*24)
        branches = struct.unpack("<62I", raw[:248])
        count = struct.unpack("<I", raw[248:252])[0]
        entries = []
        p = 252
        for _ in range(count):
            bitflags, oid, off, size, date, it = struct.unpack("<6I", raw[p:p+24]); p += 24
            entries.append((oid, off, size))
            self.files[oid] = (off, size)
        if branches[0] != 0:                      # not a leaf: descend interleaved branches
            for i in range(count + 1):
                if branches[i]:
                    self._walk(branches[i])

    def read(self, oid):
        off, size = self.files[oid]
        return self._read_chain(off, size)

def land_height_table(portal):
    """Find the 256-float LandHeightTable inside the Region file (0x13000000)."""
    data = portal.read(0x13000000)
    best = None
    n = len(data)
    for p in range(0, n - 1024, 4):
        vals = np.frombuffer(data[p:p+1024], dtype="<f4")
        if vals[0] != 0.0:
            continue
        if np.all(np.isfinite(vals)) and np.all(vals >= -1.0) and np.all(vals <= 1000.0) and vals.max() > 100:
            # plausible: starts at 0, spans real terrain heights
            best = (p, vals.copy())
            break
    if not best:
        raise SystemExit("LandHeightTable not found in region file")
    print(f"height table @ region+0x{best[0]:X}: 0 .. {best[1].max():.1f} (mean {best[1].mean():.1f})")
    return best[1]

def main():
    cell = DatReader(os.path.join(ACD, "client_cell_1.dat"))
    portal = DatReader(os.path.join(ACD, "client_portal.dat"))
    print(f"cell.dat: {len(cell.files):,} files · portal.dat: {len(portal.files):,} files")

    table = land_height_table(portal)
    with open(os.path.join(ACD, "heights.json"), "w") as f:
        json.dump([round(float(v), 3) for v in table], f)

    W = 255*8 + 1   # 2041 vertices per axis
    hidx = np.zeros((W, W), dtype=np.uint8)      # height table index
    ttyp = np.zeros((W, W), dtype=np.uint8)      # terrain type 0..31
    road = np.zeros((W, W), dtype=np.uint8)      # road bits 0..3
    blocks = 0
    for bx in range(255):
        for by in range(255):
            oid = ((bx << 8 | by) << 16) | 0xFFFF
            if oid not in cell.files:
                continue
            raw = cell.read(oid)
            terr = np.frombuffer(raw[8:8+162], dtype="<u2").reshape(9, 9)
            hgt  = np.frombuffer(raw[8+162:8+162+81], dtype=np.uint8).reshape(9, 9)
            # landblock local x runs east, y runs north; array is [x][y]
            gx, gy = bx*8, by*8
            hidx[gx:gx+9, gy:gy+9] = hgt
            ttyp[gx:gx+9, gy:gy+9] = (terr >> 2) & 0x1F
            road[gx:gx+9, gy:gy+9] = terr & 0x3
            blocks += 1
    print(f"landblocks read: {blocks:,} / 65025")

    # data texture: image row 0 = NORTH edge (y = W-1), column 0 = WEST (x = 0)
    R = hidx.T[::-1, :]
    G = ttyp.T[::-1, :]
    B = road.T[::-1, :]
    A = np.full_like(R, 255)
    img = Image.fromarray(np.stack([R, G, B, A], axis=-1), "RGBA")
    os.makedirs(os.path.join(ROOT, "assets"), exist_ok=True)
    img.save(os.path.join(ROOT, "assets", "acmap.png"), optimize=True)
    print("assets/acmap.png written:", img.size)

    # ---- verification previews ----
    hf = table[hidx].astype(np.float32)          # true metres
    hs = hf.T[::-1, :]
    gx, gy = np.gradient(hs)
    shade = np.clip(128 + gx*4 - gy*4, 0, 255).astype(np.uint8)
    sea = hs <= 0.05
    prev = np.stack([shade, shade, shade], axis=-1)
    prev[sea] = (24, 34, 68)
    rd = B > 0
    prev[rd] = (120, 90, 60)
    Image.fromarray(prev, "RGB").resize((720, 720)).save(os.path.join(ACD, "verify-hillshade.jpg"), quality=86)
    # terrain-type false colour + histogram
    hist = np.bincount(G.ravel(), minlength=32)
    print("terrain type histogram (type: count):")
    for t in np.argsort(hist)[::-1][:14]:
        if hist[t]: print(f"  {t:2d}: {hist[t]:>9,}")
    pal = np.random.RandomState(7).randint(40, 255, (32, 3)).astype(np.uint8)
    tv = pal[G]; tv[sea] = (24, 34, 68)
    Image.fromarray(tv, "RGB").resize((720, 720)).save(os.path.join(ACD, "verify-types.jpg"), quality=86)
    print("previews written")

if __name__ == "__main__":
    main()
