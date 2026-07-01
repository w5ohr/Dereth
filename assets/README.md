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

## CDN "people" for NPCs (experimental, ON by default)
Beyond the single player avatar, strolling **townsfolk** can be populated with real rigged
humans pulled from public CDNs and assigned at random:
- `GLTF_PEOPLE` (in `index.html`) lists CDN `.glb` URLs of rigged men & women, all verified
  loadable & CORS-friendly for in-browser fetch:
  - **Soldier**, **Xbot** (men) — three.js examples
  - **CesiumMan** (man) — Khronos sample models
  - **HVGirl** (woman) — Babylon.js Assets
  - **Astronaut** (person) — Google model-viewer shared assets
  - **RobotExpressive** (character) — three.js examples
  (Ready Player Me men/women avatars work too — add `https://models.readyplayer.me/<id>.glb`
  — but their CDN was unreachable from this build's network, so they're not in the default list.)
- **Weighted pool:** each `GLTF_PEOPLE` entry is now `{url, w}` where `w` is the townsfolk-assignment
  weight. The **KayKit fantasy adventurers** (barbarian/knight/mage/rogue/hooded-rogue) are `w:6`,
  generic humans (Soldier, Xbot ×2, CesiumMan, RiggedFigure, HVGirl) `w:3`, and the sci-fi oddities
  (Astronaut, RobotExpressive, BrainStem) `w:1` — so the streets read as a medieval crowd (~59%
  fantasy humans) with the occasional out-of-place wanderer instead of half the town being robots.
- `loadGltfPeople()` downloads them once; `assignGltfNpcs()` clones each via
  **`THREE.SkeletonUtils.clone`** (skinned meshes need this, not plain `.clone()`) and progressively
  fills up to `MAX_GLTF_NPCS` (40) townsfolk via a **weighted pick** (`pickWeightedPerson()`), playing
  idle/walk through a per-NPC `AnimationMixer`. Flag: `USE_GLTF_NPCS` (default **true**).
- **Load-order rebalance:** small generic models fetch faster and would greedily grab the slots before
  the larger KayKit models arrive, undoing the weighting. So once *every* fetch has settled
  (`gltfPeopleDone===GLTF_PEOPLE.length`), `rebalanceGltfNpcs()` re-rolls all skinned townsfolk against
  the full weighted pool — guaranteeing the final crowd matches the weights regardless of arrival order.
- **Online-required** (offline support dropped). A failed fetch still falls back to procedural
  for that NPC. Set `USE_GLTF_NPCS=false` to force all-procedural townsfolk.
- The pool is **14 verified-loadable models** (5 KayKit adventurers + Soldier, Xbot ×2, CesiumMan,
  RiggedFigure, HVGirl=woman, Astronaut, RobotExpressive, BrainStem), cap `MAX_GLTF_NPCS=40`.
  Note: free hot-linkable *rigged human* glTFs are scarce (KayKit's human pack is exhausted at these
  5, and no other reachable CC0 source adds distinct humans) — to reach ~25 distinct men/women, paste
  **Ready Player Me** full-body URLs (`{url:"https://models.readyplayer.me/<id>.glb", w:6}`) into
  `GLTF_PEOPLE`; RPM's CDN works in a normal browser (it was just blocked from the dev sandbox).
  Avoid Draco-compressed models (e.g. Babylon YetiSmall) unless you also vendor a DRACOLoader.
- Quest-givers/vendors/criers stay procedural (they keep their markers, labels, and looks).
- `vendor/SkeletonUtils.js` (r128 global build) is required and loaded after the GLTFLoader.

## CDN monster models (experimental, ON by default)
Monsters can also swap their procedural mesh for a rigged glTF creature:
- `MONSTER_MODELS` (in `index.html`) maps a monster **kind** → an **array** of CDN `.glb` URLs;
  a random variant is chosen per spawn, so a kind with several models shows visual variety in a
  mob pack. Current mappings:
  - `mattekar → [Fox.glb]` (Khronos glTF-Sample-Models; rigged, ~1 MB, walk/run/survey clips).
    Free hot-linkable *creature* glTFs are scarce and none exactly match AC monster descriptions,
    so **Fox is a best-fit placeholder**.
  - `skeleton → [Warrior, Minion, Mage, Rogue]` — **KayKit Character Pack: Skeletons** (CC0, Kay
    Lousberg), 4 rigged undead humanoids (~4.8 MB each, 95 baked clips incl. idle/walk/attack).
    Verified loadable & CORS-friendly via jsDelivr. AC's "skeleton" kind now spawns as one of the
    four at random.
  Add more kinds/variants by pasting verified CDN URLs into the arrays here.
- `loadGltfMonsters()` downloads each URL once into `gltfMonsters{kind:[{scene,anims,h},…]}` (an
  array of loaded variants per kind).
- `spawnMonster(kind,x,z)` picks a random variant for the kind and uses `THREE.SkeletonUtils.clone`
  (scaled to the bestiary size, feet-to-ground, per-mob `AnimationMixer` playing a
  walk/run/survey/idle clip); all other kinds keep `buildCreature()`. Flag: `USE_GLTF_MONSTERS`
  (default **true**).
- **Streaming-safe disposal:** monster streaming (`streamMonsters`) despawns far mobs. glTF mobs
  share geometry with the cached source, so despawn only **stops the mixer + removes from scene**
  (no geometry `dispose()`, which would corrupt the cache/other clones); procedural mobs get a
  full `disposeObject3D()`. A failed model fetch falls back to procedural for that kind.

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
