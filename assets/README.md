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

## Self-hosted "people" for NPCs (ON by default)
Strolling **townsfolk** are populated with real rigged men & women. The models are **self-hosted**
under `assets/models/` and served to the browser by the game's own static server (same-origin, no
runtime CDN dependency, works offline). Run the fetcher once to populate them:

```bash
python3 assets/fetch-models.py        # downloads every model into assets/models/ (idempotent)
```

The `.glb` binaries are **git-ignored** (multi-MB); `assets/fetch-models.py` is the committed,
reproducible source of truth for where each came from (all CC0 / permissive). Because they're
fetched server-side, CORS/hot-link rules don't apply — only the browser's local same-origin fetch does.

- `GLTF_PEOPLE` (in `index.html`) lists entries with a **sex** tag (`'f'`/`'m'`/`'n'` = female/male/
  neutral, since players & townsfolk are a mix) and a townsfolk **weight** `w`. Two entry shapes:
  - `{url, sex, w}` — a self-contained glTF (mesh + baked clips in one file).
  - `{mesh, clips:[…], sex, w}` — a static mesh + separate animation-clip glTFs. Used for the
    **Ready Player Me** base bodies: `Feminine_TPose.glb` / `Masculine_TPose.glb` are static meshes,
    animated by shared `anim/{F,M}_Idle.glb` + `_Walk.glb` clips. The clips bind by **bone name**
    (RPM's skeleton is identical across body & clips, so no retargeting is needed); on load each
    clip is renamed with an `idle_`/`walk_` prefix so the idle/walk detector finds it.
- **Gendered pool (16 models):** clear **women** — RPM Feminine, KayKit Mage, HVGirl; **men** — RPM
  Masculine, KayKit Barbarian/Knight/Rogue/Hooded, Soldier, Xbot ×2, CesiumMan; **neutral** —
  RiggedFigure, Astronaut, RobotExpressive, BrainStem. Verified live: townsfolk come out ~24–28%
  female, the rest male, oddities rare.
- **Weighted assignment:** KayKit fantasy humans + RPM bodies are `w:6`, generic humans `w:3`, sci-fi
  oddities `w:1` — the streets read as a medieval crowd with the occasional out-of-place wanderer.
- `loadGltfPeople()` loads each entry; `assignGltfNpcs()` clones via **`THREE.SkeletonUtils.clone`**
  (skinned meshes need this, not plain `.clone()`) and progressively fills up to `MAX_GLTF_NPCS` (40)
  townsfolk via a **weighted pick** (`pickWeightedPerson()`), playing idle/walk through a per-NPC
  `AnimationMixer`. The chosen model's sex is recorded on the NPC (`n.sex`) for future gendered
  names/voices. Flag: `USE_GLTF_NPCS` (default **true**).
- **Load-order rebalance:** small models load faster and would greedily grab the slots before the
  larger ones arrive, undoing the weighting. So once *every* load has settled
  (`gltfPeopleDone===GLTF_PEOPLE.length`), `rebalanceGltfNpcs()` re-rolls all skinned townsfolk against
  the full weighted pool — guaranteeing the final crowd matches the weights regardless of arrival order.
- A failed load still falls back to procedural for that NPC. Set `USE_GLTF_NPCS=false` to force
  all-procedural townsfolk.
- **Adding more distinct men/women:** drop rigged `.glb`s into `assets/models/people/`, add
  `{url, sex, w}` (or `{mesh, clips, sex, w}`) to `GLTF_PEOPLE`, and list their source in
  `assets/fetch-models.py`. Ready Player Me (`https://models.readyplayer.me/<id>.glb`) gives unlimited
  distinct avatars — add the URL to the fetcher (its CDN is reachable from a normal machine).
  Avoid Draco-compressed models unless you also vendor a DRACOLoader.
- Quest-givers/vendors/criers stay procedural (they keep their markers, labels, and looks).
- `vendor/SkeletonUtils.js` (r128 global build) is required and loaded after the GLTFLoader.

## Self-hosted monster models (ON by default)
Monsters can also swap their procedural mesh for a rigged glTF creature (also served from
`assets/models/monsters/` — same self-hosting as people, populated by `fetch-models.py`):
- `MONSTER_MODELS` (in `index.html`) maps a monster **kind** → an **array** of local `.glb` paths;
  a random variant is chosen per spawn, so a kind with several models shows visual variety in a
  mob pack. Current mappings:
  - `mattekar → [Fox.glb]` (Khronos glTF-Sample-Models; rigged, ~1 MB, walk/run/survey clips).
    Free *creature* glTFs matching AC monsters are scarce, so **Fox is a best-fit placeholder**.
  - `skeleton → [Warrior, Minion, Mage, Rogue]` — **KayKit Character Pack: Skeletons** (CC0, Kay
    Lousberg), 4 rigged undead humanoids (~4.8 MB each, 95 baked clips incl. idle/walk/attack).
    AC's "skeleton" kind spawns as one of the four at random.
  Add more kinds/variants by dropping `.glb`s into `assets/models/monsters/`, listing them in the
  arrays here, and adding their source to `assets/fetch-models.py`.
- `loadGltfMonsters()` loads each path once into `gltfMonsters{kind:[{scene,anims,h},…]}` (an
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
