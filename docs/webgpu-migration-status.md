# WebGPU migration — live status (shared scratch log)

Both machines/agents update this. Plan: [webgpu-migration-plan.md](webgpu-migration-plan.md).
**Branch: `webgpu`** (never work on `main` until Phase D cutover). Always `git pull --rebase origin webgpu`
before you touch anything.

---

## Phase A — ESM foundation  →  ✅ COMPLETE

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

**A.4 — version bump  →  DONE (machine 1):**
- Bumped **r159 → r185** (latest stable; `three/webgpu` + `three/tsl` are mature here). Vendored the
  split build: `vendor/three.module.js` (650 KB, re-exports `./three.core.js`) + `vendor/three.core.js`
  (1.44 MB, REVISION 185) + r185 jsm addons. Importmap unchanged.
- **Breaking-change handled:** `useLegacyLights` was removed (~r165) — r185 is physical-lights-only, so
  `renderer.useLegacyLights=true` is now a no-op. Verified this does NOT regress the look: a runtime
  toggle produces identical luminance (118.1 both ways), and both the Coronation instance and the sunlit
  overworld render visually identical to r159 (ACES tone-mapping absorbs the legacy→physical delta).
  Comment at game.js updated; a future light re-tune, if ever wanted, is an on-hardware eyeball pass.
- **Custom shaders survive the bump:** overworld render confirms the ground detail-blend, water reflection,
  foliage/wind-sway `onBeforeCompile` shaders and the analytic-sky `ShaderMaterial` all still compile and
  render correctly on r185 (they get rewritten to TSL in Phase C regardless).
- **Verified:** boots, zero console errors, addons work, instance + overworld render at parity. Still on
  `WebGLRenderer`.

## Phase B — WebGPURenderer scaffold  →  NOT STARTED (single-agent, next up)
## Phase C — TSL/NodeMaterial rewrites  →  NOT STARTED (parallel; see plan §4 ownership table)
## Phase D — fallback QA / perf / cutover  →  NOT STARTED

---

### For the other machine, to sync now
```
git fetch origin && git checkout webgpu && git pull --rebase origin webgpu
# then hard-reload the preview past the service worker: unregister SW + clear caches + ?nocache=N
```
