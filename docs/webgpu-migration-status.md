# WebGPU migration — live status (shared scratch log)

Both machines/agents update this. Plan: [webgpu-migration-plan.md](webgpu-migration-plan.md).
**Branch: `webgpu`** (never work on `main` until Phase D cutover). Always `git pull --rebase origin webgpu`
before you touch anything.

---

## Phase A — ESM foundation  →  IN PROGRESS (core done, one step left)

**Done (machine 1, this commit):**
- Vendored Three.js **r159 ESM** into `vendor/` (SAME revision as the old `three.min.js` — pure loader
  swap, no behavioural change): `vendor/three.module.js`, `vendor/jsm/loaders/GLTFLoader.js`,
  `vendor/jsm/utils/SkeletonUtils.js`, `vendor/jsm/utils/BufferGeometryUtils.js` (GLTFLoader dep).
- **File-split** (the Phase-C parallelization prerequisite): the two inline `<script>` blocks moved to
  `js/game.js` (33k lines) and `js/craft.js`. `index.html` is now ~940 lines (HTML/CSS + boot only).
- **Boot bootstrap** (`index.html`): importmap + a `type=module` that imports THREE + addons, copies the
  frozen ESM namespace to a mutable `window.THREE`, re-attaches `THREE.GLTFLoader` / `THREE.SkeletonUtils`,
  then **injects** `js/game.js` + `js/craft.js` (`async=false`, order-preserving).
  - ⚠️ **Gotcha learned:** relying on defer/module *document-order* to run the module before a deferred
    classic script did NOT hold — the classic script ran first and its no-THREE guard wiped `<body>`
    (removing the module element) before it executed. Inject the game scripts from inside the module
    instead. Also: the ESM namespace is frozen → must copy before adding `.GLTFLoader`.
- **sw.js** (v7→v8): `js/game.js`+`js/craft.js` are network-first (they're the app code, change every
  deploy); `vendor/*` is stale-while-revalidate (pinned dep). Removed the `three.min.js` cache rule.
- Removed unused `three.min.js`, `vendor/GLTFLoader.js`, `vendor/SkeletonUtils.js` (UMD).
- **Verified** (Browser pane, real load): `THREE` is object r159; `THREE.GLTFLoader`/`SkeletonUtils.clone`
  present; game boots, world renders (avatar + NPCs via SkeletonUtils + 186 ore nodes), renderer still
  `WebGLRenderer` (parity), **zero console errors**.
  - Testing note: a stale service worker served old HTML — had to `unregister()` + clear caches + load a
    cache-busted `?nocache=N` URL in a fresh tab. Do the same when verifying.

**Still TODO in Phase A:**
- **A.4 — bump r159 → latest stable** (r17x) where `three/webgpu` + `three/tsl` are stable. Separate
  commit; re-verify parity. (Deferred deliberately so the loader swap and the version bump don't mix.)

## Phase B — WebGPURenderer scaffold  →  NOT STARTED (single-agent, after A.4)
## Phase C — TSL/NodeMaterial rewrites  →  NOT STARTED (parallel; see plan §4 ownership table)
## Phase D — fallback QA / perf / cutover  →  NOT STARTED

---

### For the other machine, to sync now
```
git fetch origin && git checkout webgpu && git pull --rebase origin webgpu
# then hard-reload the preview past the service worker: unregister SW + clear caches + ?nocache=N
```
