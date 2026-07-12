#!/usr/bin/env python3
"""Generate the opt-in HD texture pack (#694): assets/hd/ mirrors assets/<pack>/tex/.

Upscales every dat-pack texture with a FAITHFUL super-resolution CNN — FSRCNN
(via OpenCV dnn_superres; tiny TF models fetched on first run) — chosen over
GAN upscalers (Real-ESRGAN et al.) deliberately: FSRCNN sharpens what is there
and hallucinates nothing, which is exactly the #694 fidelity guardrail for
iconic AC textures. Colour channels go through the CNN; alpha (where present)
is upscaled with Lanczos.

Scale policy: <=256px sides x4 · 512px x2 · >=1024 copied-through-skipped.
Incremental: an up-to-date output (mtime >= source) is skipped, so re-runs
after a pack re-export only process what changed.

Output is a GENERATED ARTIFACT, not committed to git (assets/hd/ is
gitignored — it would triple the repo). Run this after deploy on the server
(or locally) to materialise the pack; the client feature-detects it via
assets/hd/manifest.json and silently falls back to the SD files when absent.

Fallback: without opencv-contrib (dnn_superres) the tool uses Lanczos + a mild
unsharp mask — clearly noted in the manifest so nobody mistakes it for the CNN.

Usage:  python3 tools/ac_hd_upscale.py [--packs acmodels,acflora,...] [--force]
"""
import os, sys, json, time, urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "assets")
OUT = os.path.join(ASSETS, "hd")
MODEL_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "sr_models")
MODEL_URLS = {  # Saafke's pretrained FSRCNN TF exports (MIT-licensed)
    2: "https://github.com/Saafke/FSRCNN_Tensorflow/raw/master/models/FSRCNN_x2.pb",
    4: "https://github.com/Saafke/FSRCNN_Tensorflow/raw/master/models/FSRCNN_x4.pb",
}

def sr_engines():
    """Return {scale: upsampler} using OpenCV dnn_superres, or None → Lanczos fallback."""
    try:
        import cv2
        from cv2 import dnn_superres
    except Exception:
        return None
    os.makedirs(MODEL_DIR, exist_ok=True)
    eng = {}
    for scale, url in MODEL_URLS.items():
        pb = os.path.join(MODEL_DIR, os.path.basename(url))
        if not os.path.exists(pb):
            print("fetching", os.path.basename(url))
            urllib.request.urlretrieve(url, pb)
        sr = dnn_superres.DnnSuperResImpl_create()
        sr.readModel(pb)
        sr.setModel("fsrcnn", scale)
        eng[scale] = sr
    return eng

def upscale(src, dst, eng):
    import numpy as np
    from PIL import Image, ImageFilter
    im = Image.open(src)
    im = im.convert("RGBA") if (im.mode in ("P", "LA") or "transparency" in im.info) else \
         (im if im.mode in ("RGB", "RGBA") else im.convert("RGB"))
    w, h = im.size
    if max(w, h) >= 1024:
        return None                       # already high-res: client keeps using the SD path
    scale = 4 if max(w, h) <= 256 else 2
    if eng:
        import cv2
        rgb = np.asarray(im.convert("RGB"))[:, :, ::-1]        # PIL RGB → cv2 BGR
        up = eng[scale].upsample(np.ascontiguousarray(rgb))[:, :, ::-1]
        out = Image.fromarray(np.ascontiguousarray(up), "RGB")
    else:
        out = im.convert("RGB").resize((w * scale, h * scale), Image.LANCZOS) \
                .filter(ImageFilter.UnsharpMask(radius=1.4, percent=70, threshold=2))
    if im.mode == "RGBA":                  # alpha follows with Lanczos (clip masks stay crisp enough)
        a = im.getchannel("A").resize((w * scale, h * scale), Image.LANCZOS)
        out = out.convert("RGBA"); out.putalpha(a)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    out.save(dst, optimize=True)
    return scale

def main():
    packs = None
    force = "--force" in sys.argv
    for a in sys.argv[1:]:
        if a.startswith("--packs"):
            packs = a.split("=", 1)[1].split(",") if "=" in a else None
    eng = sr_engines()
    if not eng:
        print("NOTE: opencv-contrib (dnn_superres) unavailable — Lanczos+unsharp fallback")
    todo = []
    for pack in sorted(os.listdir(ASSETS)):
        if pack in ("hd", "acicons", "acui", "pwa", "sky", "music", "acmusic", "acsounds", "models"):
            continue                       # icons/UI/audio/etc. gain nothing from SR
        if packs and pack not in packs:
            continue
        texdir = os.path.join(ASSETS, pack, "tex")
        if not os.path.isdir(texdir):
            continue
        for fn in sorted(os.listdir(texdir)):
            if fn.lower().endswith(".png"):
                todo.append((pack, fn))
    done, skipped, copied, t0 = [], 0, 0, time.time()
    for i, (pack, fn) in enumerate(todo):
        src = os.path.join(ASSETS, pack, "tex", fn)
        rel = f"{pack}/tex/{fn}"
        dst = os.path.join(OUT, pack, "tex", fn)
        if not force and os.path.exists(dst) and os.path.getmtime(dst) >= os.path.getmtime(src):
            done.append(rel); skipped += 1; continue
        s = upscale(src, dst, eng)
        if s: done.append(rel)
        else: copied += 1
        if (i + 1) % 200 == 0:
            print(f"{i+1}/{len(todo)}  ({time.time()-t0:.0f}s)")
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, "manifest.json"), "w") as f:
        json.dump({"engine": "fsrcnn" if eng else "lanczos", "generated": int(time.time()),
                   "files": sorted(done)}, f)
    size = sum(os.path.getsize(os.path.join(dp, f)) for dp, _, fs in os.walk(OUT) for f in fs)
    print(f"HD pack: {len(done)} files ({skipped} up-to-date, {copied} already-hi-res skipped), "
          f"{size/1e6:.0f} MB, {time.time()-t0:.0f}s")

if __name__ == "__main__":
    main()
