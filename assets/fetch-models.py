#!/usr/bin/env python3
"""Download every rigged glTF the game uses into assets/models/ so the game's own
static server can serve them to clients (no runtime CDN dependency; works offline).

Run once from the repo root:  python3 assets/fetch-models.py
Idempotent — already-present, valid .glb files are skipped. The binaries are
git-ignored (see .gitignore); this script is the committed, reproducible source of truth.

Sources are all CC0 / permissive and fetched server-side (curl-style), so CORS/hot-link
rules don't apply — only the local same-origin fetch by the browser matters at runtime.
"""
import os, sys, subprocess, struct

ROOT = os.path.dirname(os.path.abspath(__file__))            # assets/
MODELS = os.path.join(ROOT, "models")

KAYADV = ("https://cdn.jsdelivr.net/gh/KayKit-Game-Assets/KayKit-Character-Pack-Adventures-1.0@main/"
          "addons/kaykit_character_pack_adventures/Characters/gltf/")
KAYSKEL = ("https://cdn.jsdelivr.net/gh/KayKit-Game-Assets/KayKit-Character-Pack-Skeletons-1.0@main/"
           "addons/kaykit_character_pack_skeletons/Characters/gltf/")
THREE = "https://cdn.jsdelivr.net/gh/mrdoob/three.js@r128/examples/models/gltf/"
KHR = "https://cdn.jsdelivr.net/gh/KhronosGroup/glTF-Sample-Models@master/2.0/"
BJS = "https://cdn.jsdelivr.net/gh/BabylonJS/Assets@master/meshes/"
RPM = "https://cdn.jsdelivr.net/gh/readyplayerme/animation-library@master/"
# cached full (clothed) Ready Player Me avatars committed in the animation-library-demo repo
RPMAV = ("https://cdn.jsdelivr.net/gh/readyplayerme/animation-library-demo@master/"
         "Assets/Ready%20Player%20Me/Avatars/")

# (local relative path under assets/models/, source URL)
MANIFEST = [
    # --- people: thematic fantasy humans (KayKit Adventurers, CC0) ---
    ("people/Barbarian.glb",    KAYADV + "Barbarian.glb"),
    ("people/Knight.glb",       KAYADV + "Knight.glb"),
    ("people/Mage.glb",         KAYADV + "Mage.glb"),
    ("people/Rogue.glb",        KAYADV + "Rogue.glb"),
    ("people/Rogue_Hooded.glb", KAYADV + "Rogue_Hooded.glb"),
    # --- people: generic rigged humans ---
    ("people/Soldier.glb",      THREE + "Soldier.glb"),
    ("people/Xbot.glb",         THREE + "Xbot.glb"),
    ("people/Xbot_bjs.glb",     BJS + "Xbot.glb"),
    ("people/CesiumMan.glb",    KHR + "CesiumMan/glTF-Binary/CesiumMan.glb"),
    ("people/RiggedFigure.glb", KHR + "RiggedFigure/glTF-Binary/RiggedFigure.glb"),
    ("people/HVGirl.glb",       BJS + "HVGirl.glb"),
    # --- people: sci-fi oddities (rare) ---
    ("people/Astronaut.glb",       "https://modelviewer.dev/shared-assets/models/Astronaut.glb"),
    ("people/RobotExpressive.glb", THREE + "RobotExpressive/RobotExpressive.glb"),
    ("people/BrainStem.glb",       KHR + "BrainStem/glTF-Binary/BrainStem.glb"),
    # --- people: Ready Player Me base bodies (gendered) + shared animation clips ---
    #     the *_TPose meshes are static; idle/walk clips (below) apply to them (same skeleton).
    ("people/Feminine_TPose.glb",  RPM + "feminine/glb/Feminine_TPose.glb"),
    ("people/Masculine_TPose.glb", RPM + "masculine/glb/Masculine_TPose.glb"),
    # clothed, bearded RPM man (full outfit + hair); animated by the M_Idle/M_Walk clips (same RPM rig)
    ("people/Masculine.glb", RPMAV + "64e359ea58f50a12df573a70/2fac66e374c947c41bc74325c6e3d934/64e359ea58f50a12df573a70.glb"),
    ("anim/F_Idle.glb", RPM + "feminine/glb/idle/F_Standing_Idle_001.glb"),
    ("anim/F_Walk.glb", RPM + "feminine/glb/locomotion/F_Walk_002.glb"),
    ("anim/M_Idle.glb", RPM + "masculine/glb/idle/M_Standing_Idle_001.glb"),
    ("anim/M_Walk.glb", RPM + "masculine/glb/locomotion/M_Walk_002.glb"),
    # --- monsters ---
    ("monsters/Fox.glb",              KHR + "Fox/glTF-Binary/Fox.glb"),
    ("monsters/Skeleton_Warrior.glb", KAYSKEL + "Skeleton_Warrior.glb"),
    ("monsters/Skeleton_Minion.glb",  KAYSKEL + "Skeleton_Minion.glb"),
    ("monsters/Skeleton_Mage.glb",    KAYSKEL + "Skeleton_Mage.glb"),
    ("monsters/Skeleton_Rogue.glb",   KAYSKEL + "Skeleton_Rogue.glb"),
]

def valid_glb(path):
    try:
        with open(path, "rb") as f:
            head = f.read(12)
        return len(head) >= 12 and head[:4] == b"glTF" and struct.unpack("<I", head[8:12])[0] > 0
    except OSError:
        return False

def main():
    ok = skip = fail = 0
    for rel, url in MANIFEST:
        dst = os.path.join(MODELS, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if valid_glb(dst):
            print(f"  skip  {rel}  (already present)"); skip += 1; continue
        try:
            # curl handles TLS/CA certs portably (macOS python's urllib often lacks a cert bundle)
            subprocess.run(["curl", "-fsSL", "--retry", "2", "--max-time", "120",
                            "-A", "dereth-fetch-models", "-o", dst, url],
                           check=True, capture_output=True)
            if not valid_glb(dst):
                if os.path.exists(dst): os.remove(dst)
                raise ValueError("downloaded file is not a valid binary glTF")
            print(f"  ok    {rel}  ({os.path.getsize(dst)//1024} KB)"); ok += 1
        except Exception as e:
            msg = e.stderr.decode().strip() if isinstance(e, subprocess.CalledProcessError) and e.stderr else e
            print(f"  FAIL  {rel}  <- {url}\n        {msg}", file=sys.stderr); fail += 1
    print(f"\n{ok} downloaded, {skip} skipped, {fail} failed  ->  {MODELS}")
    return 1 if fail else 0

if __name__ == "__main__":
    sys.exit(main())
