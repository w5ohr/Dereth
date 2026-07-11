#!/usr/bin/env python3
"""Tenfold armor art pipeline (issue #667).

Generates the 'of the Tenfold' set's unique look from the covenant ClothingTable
part textures: obsidian-dark plate, gold seam inlay, a ten-rayed sunburst chest
emblem, and matching emissive maps for a soft enchanted glow. Also draws the
set's 32x32 AC-style inventory icons and writes the acarmor gid meshes + index
entries that bind the pieces to the new art.

Outputs (all committed):
  assets/acmodels/tex/tenfold_<id>.png      diffuse (4x covenant resolution)
  assets/acmodels/tex/tenfold_<id>_e.png    emissive map (motifs only)
  assets/acarmor/tenfold_<gid>.json         covenant geometry, Tenfold materials
  assets/acarmor/index.json                 + 'of the tenfold' item entries
  assets/acicons/tenfold_<slug>.png         inventory icons
  assets/acicons/index.json                 + name -> icon entries

Run: python3 tools/tenfold_art.py
"""
import json
import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEXD = os.path.join(ROOT, 'assets', 'acmodels', 'tex')
ARMD = os.path.join(ROOT, 'assets', 'acarmor')
ICOD = os.path.join(ROOT, 'assets', 'acicons')
UP = 4  # working scale over the 1999-vintage source resolution

# ── palette ──────────────────────────────────────────────────────────────────
# obsidian plate: near-black with a cold blue-violet sheen (Oblivion ebony)
OBSIDIAN = [(0.00, (4, 5, 9)), (0.22, (11, 13, 20)), (0.45, (23, 26, 40)),
            (0.68, (46, 52, 74)), (0.86, (82, 90, 120)), (1.00, (134, 144, 180))]
GOLD_DK, GOLD, GOLD_HI = (98, 66, 18), (214, 166, 60), (255, 226, 124)
EM_GOLD = (255, 196, 90)   # emissive tint (scaled by EM_LVL)
EM_LVL = 0.62

def _lum(img):
    a = np.asarray(img.convert('L'), dtype=np.float32) / 255.0
    lo, hi = np.percentile(a, 2), np.percentile(a, 98)
    return np.clip((a - lo) / max(hi - lo, 1e-4), 0, 1)

def _blur(a, r):
    im = Image.fromarray((a * 255).astype(np.uint8))
    return np.asarray(im.filter(ImageFilter.GaussianBlur(r)), dtype=np.float32) / 255.0

def _ramp(L, anchors):
    xs = np.array([x for x, _ in anchors]); out = np.zeros(L.shape + (3,), np.float32)
    for c in range(3):
        ys = np.array([col[c] for _, col in anchors], np.float32)
        out[..., c] = np.interp(L, xs, ys)
    return out

def _gold(b):
    """shade gold by local brightness b (HxW) -> HxWx3"""
    return _ramp(b, [(0.0, GOLD_DK), (0.55, GOLD), (1.0, GOLD_HI)])

# ── motif drawing (image space, at UP scale) ─────────────────────────────────
def _star_rays(d, cx, cy, r_in, r_out, n=10, a0=90, half=None, w=None):
    """ten-rayed sunburst: slim tapered rays every 360/n deg, alternating long/
    short (one crown per year-arc). half='left' draws only rays whose tip lands
    at x<=cx (the chest texture mirrors at its right edge)."""
    for k in range(n):
        a = math.radians(a0 + k * 360.0 / n)
        dx, dy = math.cos(a), -math.sin(a)
        if half == 'left' and dx > 0.001:
            continue
        ro = r_out if k % 2 == 0 else r_in + (r_out - r_in) * 0.55
        ww = w or (ro - r_in) * 0.13
        tip = (cx + dx * ro, cy + dy * ro)
        px, py = -dy, dx
        b1 = (cx + dx * r_in + px * ww, cy + dy * r_in + py * ww)
        b2 = (cx + dx * r_in - px * ww, cy + dy * r_in - py * ww)
        d.polygon([tip, b1, b2], fill=255)

def _ring(d, cx, cy, r, w):
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=255, width=int(w))

def _diamond(d, cx, cy, r, ry=None):
    ry = ry or r * 1.5
    d.polygon([(cx, cy - ry), (cx + r, cy), (cx, cy + ry), (cx - r, cy)], fill=255)

def _volute(d, cx, cy, r, w, turns=1.6, flip=1):
    """a single filigree spiral (Ionic volute)"""
    pts = []
    steps = 60
    for i in range(steps + 1):
        t = i / steps
        a = t * turns * 2 * math.pi
        rr = r * (1 - 0.82 * t)
        pts.append((cx + flip * rr * math.cos(a), cy - rr * math.sin(a)))
    d.line(pts, fill=255, width=int(w), joint='curve')

def _hline(d, x0, x1, y, w):
    d.line([(x0, y), (x1, y)], fill=255, width=int(w))

def _vline(d, x, y0, y1, w):
    d.line([(x, y0), (x, y1)], fill=255, width=int(w))

# per-texture motif painters: f(d_motif, d_emiss, W, H) with W,H at UP scale.
# The torso/girth meshes mirror at u=1.0 (texture right edge = body centerline).
def _m_chest_front(dm, de, W, H):
    cx, cy = W - 1, int(H * 0.40)
    _star_rays(dm, cx, cy, W * 0.065, W * 0.27, half='left')
    _ring(dm, cx, cy, W * 0.092, W * 0.018)
    _star_rays(de, cx, cy, W * 0.065, W * 0.27, half='left')
    _ring(de, cx, cy, W * 0.092, W * 0.018)

def _m_chest_back(dm, de, W, H):
    _vline(dm, W - 1 - W * 0.012, H * 0.14, H * 0.60, W * 0.010)   # spine inlay
    _diamond(dm, W - 1, H * 0.34, W * 0.045)
    _diamond(de, W - 1, H * 0.34, W * 0.045)

def _m_girth_front(dm, de, W, H):
    _diamond(dm, W - 1, H * 0.16, W * 0.045, W * 0.045)             # buckle at centerline
    _diamond(de, W - 1, H * 0.16, W * 0.045, W * 0.045)

def _m_girth_back(dm, de, W, H):
    pass                                                            # seam inlay only

def _m_pauldron_l(dm, de, W, H):
    cx, cy = W * 0.42, H * 0.24
    _volute(dm, cx, cy, W * 0.16, W * 0.020, flip=1)
    _volute(dm, cx, cy, W * 0.16, W * 0.020, flip=-1)
    _volute(de, cx, cy, W * 0.16, W * 0.020, flip=1)
    _volute(de, cx, cy, W * 0.16, W * 0.020, flip=-1)

def _m_pauldron_r(dm, de, W, H):
    cx, cy = W * 0.50, H * 0.22
    _volute(dm, cx, cy, W * 0.24, W * 0.035, flip=1)
    _volute(dm, cx, cy, W * 0.24, W * 0.035, flip=-1)
    _volute(de, cx, cy, W * 0.24, W * 0.035, flip=1)
    _volute(de, cx, cy, W * 0.24, W * 0.035, flip=-1)

def _m_bracer(dm, de, W, H):
    _vline(dm, W * 0.50, H * 0.10, H * 0.86, W * 0.022)
    _diamond(dm, W * 0.50, H * 0.42, W * 0.085)
    _diamond(de, W * 0.50, H * 0.42, W * 0.085)

def _m_bracer_fan(dm, de, W, H):
    pass  # the elbow fan keeps seam inlay only — its painted rim is already ornate

def _m_gauntlet(dm, de, W, H):
    _hline(dm, 0, W, H * 0.52, H * 0.016)                           # knuckle bar
    for i in range(4):                                              # gilded knuckle studs
        x = W * (0.14 + i * 0.24)
        _diamond(dm, x, H * 0.52, W * 0.028, W * 0.04)
        _diamond(de, x, H * 0.52, W * 0.028, W * 0.04)

def _m_tasset_greave(dm, de, W, H):
    cx, cy = W * 0.50, H * 0.585                                    # poleyn (knee) disc
    _ring(dm, cx, cy, W * 0.16, W * 0.030)
    _diamond(dm, cx, cy, W * 0.065)
    _diamond(de, cx, cy, W * 0.065)

def _m_solleret(dm, de, W, H):
    pass                                                            # seam inlay only

# gain/lift brighten a source before the ramp — the limb textures are much darker
# than the torso's and rendered as featureless vinyl without it. seam scales inlay.
LIMB = dict(gain=1.28, lift=0.04, seam=0.8)
TEXCFG = {
    # the torso mesh's +z half is the avatar's FRONT (verified in-engine); 053C0 is that half
    '060053C0': dict(motif=_m_chest_front),          # breastplate front (mirrors at u=1)
    '060053BF': dict(motif=_m_chest_back),           # breastplate back
    '060053C3': dict(motif=_m_girth_front),          # girth front
    '060053C4': dict(motif=_m_girth_back),           # girth back
    '060053C8': dict(motif=_m_pauldron_l, **LIMB),   # left pauldron main
    '060053C9': dict(motif=_m_pauldron_r, **LIMB),   # right pauldron
    '060053CA': dict(motif=None, **LIMB),            # pauldron underside (kept plain)
    '060053BC': dict(motif=_m_bracer, **LIMB),       # bracer sleeve
    '060053BD': dict(motif=_m_bracer_fan, **LIMB),   # bracer elbow fan
    '060053C2': dict(motif=_m_gauntlet, **LIMB),     # gauntlet
    '060053BE': dict(motif=_m_tasset_greave, **LIMB),# tasset + greave
    '060053BB': dict(motif=_m_solleret, **LIMB),     # solleret
}

def build_texture(tid, cfg):
    src = Image.open(os.path.join(TEXD, tid + '.png')).convert('RGB')
    W, H = src.width * UP, src.height * UP
    src = src.resize((W, H), Image.LANCZOS)
    L = np.clip(_lum(src) * cfg.get('gain', 1.0) + cfg.get('lift', 0.0), 0, 1)

    base = _ramp(L, OBSIDIAN)

    # gold seam inlay: the painted plate boundaries are the texture's dark valleys
    b2 = _blur(L, 1.6 * UP)
    seams = np.clip((b2 - L - 0.065) * 7.0, 0, 1)
    seams = _blur(seams, 0.35 * UP)
    # keep the inlay off fine-grained regions (chainmail knit turns leopard-gold
    # otherwise): suppress where local variance is high
    var = np.clip(_blur(L * L, 1.2 * UP) - b2 * b2, 0, None)
    mail = np.clip((np.sqrt(var) - 0.10) * 12.0, 0, 1)
    goldmask = np.clip(seams * cfg.get('seam', 0.62) * (1 - mail * 0.92), 0, 1)

    # motifs (pure gold, on top of everything)
    mot = Image.new('L', (W, H), 0)
    emi = Image.new('L', (W, H), 0)
    if cfg.get('motif'):
        cfg['motif'](ImageDraw.Draw(mot), ImageDraw.Draw(emi), W, H)
        mot = mot.filter(ImageFilter.GaussianBlur(0.25 * UP))
    m = np.asarray(mot, np.float32) / 255.0
    goldmask = np.clip(goldmask + m, 0, 1)

    gold = _gold(np.clip(_blur(L, 0.8 * UP) * 0.8 + 0.25 + m * 0.25, 0, 1))
    out = base * (1 - goldmask[..., None]) + gold * goldmask[..., None]
    Image.fromarray(out.astype(np.uint8)).save(os.path.join(TEXD, f'tenfold_{tid}.png'))

    # emissive: the motif layer softly bloomed + a faint glow along every gold seam,
    # so the inlay still reads on the near-black plate in dim light
    e = np.asarray(emi.filter(ImageFilter.GaussianBlur(0.5 * UP)), np.float32) / 255.0
    e = np.clip(e * 1.4 + goldmask * 0.30, 0, 1) * EM_LVL
    em = (e[..., None] * np.array(EM_GOLD, np.float32)).astype(np.uint8)
    Image.fromarray(em).save(os.path.join(TEXD, f'tenfold_{tid}_e.png'))
    return f'tenfold_{tid}.png'

# ── gid meshes: covenant geometry rebound to the Tenfold materials ───────────
# covenant part gid -> tenfold gid  (see assets/acarmor/index.json 'covenant *')
GIDS = ['01002a6f', '01002a6e', '01002a7a', '01002a83', '01002a74', '01002a7c',
        '01002a7e', '01002a7f', '01002a73', '01002a77', '01002a75', '01002a76',
        '01002a71', '01002a78', '01002a72', '01002a79']
MAT_EXTRA = dict(metal=0.52, rough=0.42, emint=0.85)

def build_gids():
    for gid in GIDS:
        with open(os.path.join(ARMD, gid + '.json')) as f:
            g = json.load(f)
        for grp in g['groups']:
            t = grp['mat'].get('tex')
            if t:
                tid = t[:-4]
                nm = dict(grp['mat'])
                nm['tex'] = f'tenfold_{tid}.png'
                nm['emtex'] = f'tenfold_{tid}_e.png'
                nm.update(MAT_EXTRA)
                grp['mat'] = nm
            else:  # untextured groups go obsidian
                grp['mat'] = dict(grp['mat'], color=0x14161f, **MAT_EXTRA)
        with open(os.path.join(ARMD, f'tenfold_{gid}.json'), 'w') as f:
            json.dump(g, f, separators=(',', ':'))

ITEMS = {  # item name -> covenant swaps (p = setup part index, see partJoint)
    'aegis cuirass of the tenfold': [(9, '01002a6f'), (0, '01002a6e')],
    'pauldrons of the tenfold':     [(10, '01002a7a'), (13, '01002a83')],
    'vambraces of the tenfold':     [(11, '01002a74'), (14, '01002a7c')],
    'gauntlets of the tenfold':     [(12, '01002a7e'), (15, '01002a7f')],
    'greaves of the tenfold':       [(1, '01002a73'), (5, '01002a77'),
                                     (2, '01002a75'), (6, '01002a76')],
    'sabatons of the tenfold':      [(3, '01002a71'), (4, '01002a78'),
                                     (7, '01002a72'), (8, '01002a79')],
}

def bind_index():
    p = os.path.join(ARMD, 'index.json')
    with open(p) as f:
        idx = json.load(f)
    for name, swaps in ITEMS.items():
        idx['items'][name] = [{'p': pp, 'g': f'tenfold_{g}'} for pp, g in swaps]
    with open(p, 'w') as f:
        json.dump(idx, f, separators=(',', ':'))

# ── inventory icons (drawn at 8x, LANCZOS to 32) ─────────────────────────────
IS = 256  # icon working canvas
OB_LO, OB_MD, OB_HI = (16, 18, 28), (40, 45, 66), (96, 106, 140)
NAVY, NAVY_HI = (24, 30, 66), (52, 64, 118)

def _ic_canvas():
    im = Image.new('RGBA', (IS, IS), (0, 0, 0, 0))
    return im, ImageDraw.Draw(im)

def _plate(d, pts, lo=OB_LO, md=OB_MD, hi=OB_HI, gold_w=7):
    """a shaded obsidian plate: dark fill, top-left sheen, gold edge"""
    d.polygon(pts, fill=md + (255,))
    # sheen: shrink toward the top-left
    cx = sum(p[0] for p in pts) / len(pts); cy = sum(p[1] for p in pts) / len(pts)
    sh = [((x - cx) * 0.72 + cx - IS * 0.02, (y - cy) * 0.72 + cy - IS * 0.03) for x, y in pts]
    d.polygon(sh, fill=hi + (110,))
    in_ = [((x - cx) * 0.45 + cx + IS * 0.02, (y - cy) * 0.45 + cy + IS * 0.04) for x, y in pts]
    d.polygon(in_, fill=lo + (160,))
    d.line(pts + [pts[0]], fill=GOLD + (255,), width=gold_w, joint='curve')

def _finish(im, name):
    im = im.filter(ImageFilter.GaussianBlur(0.8))
    im.resize((32, 32), Image.LANCZOS).save(os.path.join(ICOD, f'tenfold_{name}.png'))

def _icon_cuirass():
    im, d = _ic_canvas()
    # breastplate silhouette: broad shoulders with a deep neck scoop cut into the top,
    # arm scallops at the sides, taper to a girdled waist
    _plate(d, [(38, 58), (92, 38), (104, 44), (114, 66), (128, 74), (142, 66), (152, 44),
               (164, 38), (218, 58), (196, 92), (198, 136), (174, 204), (128, 224),
               (82, 204), (58, 136), (60, 92)])
    d.ellipse([106, 108, 150, 152], outline=GOLD_HI + (255,), width=6)
    _star_rays_rgba(d, 128, 130, 24, 60)
    _finish(im, 'cuirass')

def _star_rays_rgba(d, cx, cy, r_in, r_out, n=10):
    for k in range(n):
        a = math.radians(90 + k * 36)
        dx, dy = math.cos(a), -math.sin(a)
        px, py = -dy, dx
        w = (r_out - r_in) * 0.20
        d.polygon([(cx + dx * r_out, cy + dy * r_out),
                   (cx + dx * r_in + px * w, cy + dy * r_in + py * w),
                   (cx + dx * r_in - px * w, cy + dy * r_in - py * w)],
                  fill=GOLD_HI + (255,))

def _icon_pauldrons():
    im, d = _ic_canvas()
    for cx in (76, 180):
        _plate(d, [(cx - 48, 120), (cx - 30, 76), (cx, 62), (cx + 30, 76), (cx + 48, 120),
                   (cx + 34, 168), (cx - 34, 168)])
        _volute_rgba(d, cx, 108, 26, 5)
    _finish(im, 'pauldrons')

def _volute_rgba(d, cx, cy, r, w, turns=1.5):
    for flip in (1, -1):
        pts = []
        for i in range(49):
            t = i / 48
            a = t * turns * 2 * math.pi
            rr = r * (1 - 0.8 * t)
            pts.append((cx + flip * rr * math.cos(a), cy - rr * math.sin(a)))
        d.line(pts, fill=GOLD_HI + (255,), width=w, joint='curve')

def _icon_vambraces():
    im, d = _ic_canvas()
    for cx, tilt in ((80, -12), (176, 12)):
        pts = [(cx - 34 + tilt, 52), (cx + 34 + tilt, 52), (cx + 26 - tilt, 204), (cx - 26 - tilt, 204)]
        _plate(d, pts)
        d.polygon([(cx, 110), (cx + 14, 128), (cx, 146), (cx - 14, 128)], fill=GOLD_HI + (255,))
    _finish(im, 'vambraces')

def _icon_gauntlets():
    im, d = _ic_canvas()
    _plate(d, [(70, 40), (186, 40), (196, 96), (186, 130), (70, 130)])            # cuff
    for i, x in enumerate((84, 116, 148, 180)):                                   # fingers
        _plate(d, [(x - 14, 128), (x + 14, 128), (x + 12, 206 + (i % 2) * 10), (x - 12, 206 + (i % 2) * 10)], gold_w=5)
        d.polygon([(x, 138), (x + 9, 150), (x, 162), (x - 9, 150)], fill=GOLD_HI + (255,))
    d.line([(70, 130), (186, 130)], fill=GOLD + (255,), width=7)
    _finish(im, 'gauntlets')

def _icon_greaves():
    im, d = _ic_canvas()
    for cx in (78, 178):
        _plate(d, [(cx - 36, 40), (cx + 36, 40), (cx + 28, 216), (cx - 28, 216)])
        d.ellipse([cx - 18, 102, cx + 18, 138], outline=GOLD_HI + (255,), width=6)
        d.polygon([(cx, 110), (cx + 8, 120), (cx, 130), (cx - 8, 120)], fill=GOLD_HI + (255,))
    _finish(im, 'greaves')

def _icon_sabatons():
    im, d = _ic_canvas()
    for cx, fl in ((70, -1), (168, 1)):
        _plate(d, [(cx - 26, 60), (cx + 26, 60), (cx + 26, 150), (cx + fl * 66 + 8, 176),
                   (cx + fl * 66 + 8, 210), (cx - 26, 210)])
        d.line([(cx + fl * 30, 176), (cx + fl * 62, 186)], fill=GOLD_HI + (255,), width=6)
    _finish(im, 'sabatons')

def _icon_diadem():
    im, d = _ic_canvas()
    # gold circlet with ten points, front gem
    d.ellipse([38, 128, 218, 208], outline=GOLD + (255,), width=16)
    for k in range(10):
        a = math.pi * (0.08 + 0.84 * k / 9)
        cx, cy = 128 + 92 * math.cos(a), 158 - 34 * math.sin(a)
        h = 46 if k % 2 == 0 else 26
        d.polygon([(cx - 10, cy), (cx + 10, cy), (cx, cy - h)], fill=GOLD_HI + (255,))
    d.polygon([(128, 128), (146, 152), (128, 176), (110, 152)], fill=(58, 78, 190, 255))
    d.polygon([(128, 132), (140, 152), (128, 172), (116, 152)], fill=(120, 150, 255, 255))
    _finish(im, 'diadem')

def _icon_shirt():
    im, d = _ic_canvas()
    d.polygon([(58, 60), (100, 40), (156, 40), (198, 60), (216, 110), (186, 122), (176, 96),
               (176, 210), (80, 210), (80, 96), (70, 122), (40, 110)], fill=NAVY + (255,))
    d.polygon([(100, 40), (156, 40), (146, 70), (110, 70)], fill=NAVY_HI + (255,))
    d.line([(100, 40), (110, 70), (146, 70), (156, 40)], fill=GOLD + (255,), width=6)
    d.line([(80, 205), (176, 205)], fill=GOLD + (255,), width=8)
    _finish(im, 'shirt')

def _icon_breeches():
    im, d = _ic_canvas()
    d.polygon([(64, 46), (192, 46), (200, 120), (196, 214), (146, 214), (130, 120),
               (110, 214), (60, 214), (56, 120)], fill=NAVY + (255,))
    d.polygon([(64, 46), (192, 46), (194, 70), (62, 70)], fill=NAVY_HI + (255,))
    d.line([(62, 70), (194, 70)], fill=GOLD + (255,), width=7)
    _finish(im, 'breeches')

def _icon_cape():
    im, d = _ic_canvas()
    d.polygon([(84, 44), (172, 44), (196, 120), (208, 214), (160, 196), (128, 214),
               (96, 196), (48, 214), (60, 120)], fill=NAVY + (255,))
    d.polygon([(84, 44), (172, 44), (182, 82), (74, 82)], fill=NAVY_HI + (255,))
    for cx in (84, 172):
        d.ellipse([cx - 14, 36, cx + 14, 64], fill=GOLD + (255,))
        d.ellipse([cx - 7, 43, cx + 7, 57], fill=GOLD_HI + (255,))
    d.line([(84, 50), (172, 50)], fill=GOLD + (255,), width=6)
    _finish(im, 'cape')

ICONS = {
    'diadem of the tenfold': ('diadem', _icon_diadem),
    'aegis cuirass of the tenfold': ('cuirass', _icon_cuirass),
    'pauldrons of the tenfold': ('pauldrons', _icon_pauldrons),
    'vambraces of the tenfold': ('vambraces', _icon_vambraces),
    'gauntlets of the tenfold': ('gauntlets', _icon_gauntlets),
    'greaves of the tenfold': ('greaves', _icon_greaves),
    'sabatons of the tenfold': ('sabatons', _icon_sabatons),
    'shirt of the tenfold': ('shirt', _icon_shirt),
    'breeches of the tenfold': ('breeches', _icon_breeches),
    "kilmer's cape": ('cape', _icon_cape),
}

def bind_icons():
    p = os.path.join(ICOD, 'index.json')
    with open(p) as f:
        idx = json.load(f)
    for name, (slug, fn) in ICONS.items():
        fn()
        did = f'tenfold_{slug}'
        idx['icons'][did] = f'tenfold_{slug}.png'
        idx['names'][name] = did
    with open(p, 'w') as f:   # keep the file's original one-entry-per-line shape
        f.write('{\n"icons": {\n')
        f.write(',\n'.join(f'{json.dumps(k)}: {json.dumps(v)}' for k, v in idx['icons'].items()))
        f.write('\n},\n"names": {\n')
        f.write(',\n'.join(f'{json.dumps(k)}: {json.dumps(v)}' for k, v in idx['names'].items()))
        f.write('\n}\n}')

if __name__ == '__main__':
    for tid, cfg in TEXCFG.items():
        print('tex ', build_texture(tid, cfg))
    build_gids(); print('gids', len(GIDS))
    bind_index(); print('index bound:', len(ITEMS), 'items')
    bind_icons(); print('icons:', len(ICONS))
    print('done')
