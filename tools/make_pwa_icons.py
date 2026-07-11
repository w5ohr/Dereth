#!/usr/bin/env python3
"""Generate the PWA icon set (assets/pwa/) — stdlib only, no PIL.

The mark mirrors the title screen: a gold diamond (outer ring + filled inner
diamond, the ◈ of the DERETH logo) on the game's near-black background.
Sizes: apple-touch-icon 180, manifest 192/512, and a maskable 512 whose art
sits inside the 80%-circle safe zone.

Run from the repo root:  python3 tools/make_pwa_icons.py
"""
import os
import struct
import zlib

BG = (0x12, 0x10, 0x0C)          # near-black parchment-brown, a touch warmer than --bg
GOLD_HI = (0xE8, 0xC4, 0x62)     # top of the gold gradient
GOLD_LO = (0xB8, 0x8A, 0x2E)     # bottom of the gold gradient
EDGE = (0x3A, 0x2F, 0x1C)        # --border: thin dark seam between ring and fill


def png_write(path, w, h, rgba_rows):
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + bytes(row) for row in rgba_rows)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def gold_at(y, size):
    t = y / max(1, size - 1)
    return tuple(int(a + (b - a) * t) for a, b in zip(GOLD_HI, GOLD_LO))


def draw(size, scale):
    """Diamond via the manhattan metric: d = |dx|+|dy| against radius bands."""
    cx = cy = (size - 1) / 2.0
    r_outer = size * 0.44 * scale          # outer edge of the gold ring
    r_ring = size * 0.36 * scale           # inner edge of the ring (gap begins)
    r_gap = size * 0.30 * scale            # gap ends, inner filled diamond begins
    edge_w = max(1.5, size * 0.012)        # dark seam thickness
    rows = []
    for y in range(size):
        row = bytearray()
        g = gold_at(y, size)
        for x in range(size):
            d = abs(x - cx) + abs(y - cy)
            if d <= r_gap - edge_w:
                px = g                                       # inner diamond fill
            elif d <= r_gap:
                px = EDGE
            elif d <= r_ring:
                px = BG                                      # gap between fill and ring
            elif d <= r_ring + edge_w:
                px = EDGE
            elif d <= r_outer:
                px = g                                       # the gold ring
            elif d <= r_outer + edge_w:
                px = EDGE
            else:
                px = BG
            row += bytes((px[0], px[1], px[2], 255))
        rows.append(row)
    return rows


def main():
    out = os.path.join(os.path.dirname(__file__), "..", "assets", "pwa")
    os.makedirs(out, exist_ok=True)
    for name, size, scale in [
        ("apple-touch-icon.png", 180, 1.0),
        ("icon-192.png", 192, 1.0),
        ("icon-512.png", 512, 1.0),
        ("icon-512-maskable.png", 512, 0.66),   # corners stay inside the 80% safe circle
    ]:
        png_write(os.path.join(out, name), size, size, draw(size, scale))
        print("wrote", name, f"{size}x{size}")


if __name__ == "__main__":
    main()
