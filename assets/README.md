# External avatar models (experimental)

Dereth normally draws its characters from **procedural geometry** (built in code, fully
offline, part of the single HTML file). This folder is the opt-in path to a **realistic,
rigged human avatar** loaded from a glTF/GLB model — the route to photoreal-ish characters
without changing engines (see `docs/` discussion).

## How it works
- `vendor/GLTFLoader.js` (Three.js r128 global build) is loaded after `three.min.js`.
- On start the game calls `tryLoadGltfAvatar()` (in `index.html`). If the feature flag
  `USE_GLTF_AVATAR` is **true** and a model exists at `GLTF_AVATAR_URL` (default
  `assets/avatar.glb`), it loads, normalises it to ~1.85u tall, feet on the ground, and
  plays its idle animation via `THREE.AnimationMixer`.
- In **third person** the rigged model is shown instead of the procedural avatar.
- **Any failure** (flag off, loader missing, file absent, parse error) silently falls back
  to the **procedural avatar** — so the offline game always works.

## To enable
1. Put a rigged humanoid `.glb` at `assets/avatar.glb`.
2. In `index.html`, set `let USE_GLTF_AVATAR=true;`.
3. Reload and press **V** for third person.

## Where to get a model
- **Ready Player Me** — free, browser-made avatars; export/download a `.glb` (~1–3 MB). Stylised-realistic.
- **Mixamo** — free rigged characters + animation clips (FBX → convert to glTF) (~3–10 MB).
- **Character Creator 4 / iClone** (Reallusion) — realistic rigged exports.
- **Blender + Human Generator / MB-Lab** — full control; export glTF.
- Three.js sample `Soldier.glb` (~2.1 MB, rigged with idle/walk/run) is handy for testing.

## Notes / trade-offs
- Model files are **git-ignored** (`.gitignore`) — they're multi-MB binaries and not part of
  the self-contained game. The pipeline (loader + code) **is** committed.
- File-size scope: loader ≈ **94 KB** (always loaded); a real avatar adds **~1–15 MB**,
  fetched async at startup (the procedural avatar shows first, then the model swaps in).
- This keeps everything **local/offline** as long as the `.glb` is a local file. Do **not**
  point `GLTF_AVATAR_URL` at a remote CDN if you want to stay offline.
- Current limitation: the model plays its own baked clips (idle); it is **not** yet driven by
  the game's per-joint locomotion. Wiring walk/run/attack to the model's skeleton (or Mixamo
  clips) is the next step if you adopt this path.
