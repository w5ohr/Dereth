# Migration Plan — `three.module` (ESM) + WebGPURenderer

**Goal:** move the client off the minified UMD Three.js global (`three.min.js`, r159) to the ES-module
build, and off `WebGLRenderer` onto `WebGPURenderer`. **Split across 2 agents on 2 machines**, synced via
git. This document is the shared coordination artifact — both machines pull it and follow it.

Authored from a measured survey of `index.html` at the time of writing. Numbers below are real counts.

---

## 0. Reality check (read first)

- **The two migrations are NOT independent.** WebGPU requires the ES-module build (WebGPURenderer, node
  materials, and TSL ship **only** as ESM — there is no UMD `THREE.WebGPURenderer`). So "three instead of
  three.min" is not a cosmetic deminify — it is the **foundation** the WebGPU work stands on. Do it first.
- **The long pole is custom shaders, not the renderer swap.** `WebGPURenderer` does **not** run GLSL
  `ShaderMaterial` or `onBeforeCompile` patches. Every custom shader must be rebuilt as a **NodeMaterial /
  TSL** (Three.js Shading Language, JS-authored node shaders). Inventory below: **6 `ShaderMaterial` + 6
  `onBeforeCompile` + a hand-rolled post-processing chain**. Budget the bulk of the effort here.
- **The payoff architecture:** `WebGPURenderer` runs the *same* NodeMaterial code on **both** a WebGPU
  backend **and** a WebGL2 fallback backend (`forceWebGL` / automatic when WebGPU is absent). So the end
  state is **one renderer + automatic fallback** — not a permanent dual WebGL/WebGPU code path. That is the
  target and it is what makes the migration worth doing.
- **Version bump comes with it.** r159's WebGPU/TSL is early. Bump to the **latest stable Three.js**
  (r17x at time of writing; `three/webgpu` + `three/tsl` are the stable entry points there). The r159→r17x
  API delta is small compared to the WebGPU work, but it is real — fold it into Phase A.
- **Offline / Apple-Silicon target is viable.** Safari 18+ (macOS/iOS, 2024) ships WebGPU on by default;
  Chrome/Edge on macOS too. Keep the WebGL2 fallback for older Safari/Firefox. Verify on the actual
  offline Apple-Silicon browser this game targets ([[dereth-game-project]], [[dereth-world-scale]]).
- **Do NOT do this on `main`.** WebGPU work will break the live game repeatedly. Use a shared
  `webgpu` branch; merge to `main` only when it renders correctly with fallback (see §4).

---

## 1. Measured current state (`index.html`, one ~34.5k-line file)

| Surface | Count / detail | Migration impact |
|---|---|---|
| Three.js | **r159**, UMD, loaded `<script src="three.min.js">` (line 918), global `THREE` | swap to ESM `three.module.js` + importmap |
| Vendor addons | `vendor/GLTFLoader.js`, `vendor/SkeletonUtils.js` — UMD, assign to `THREE.*` | replace with ESM `three/addons/**` |
| Color mgmt | already modern: `colorSpace` ×35, `outputColorSpace` ×2, `useLegacyLights` ×2 | ✅ already migrated (r152+ workflow); little to do |
| `ShaderMaterial` (GLSL) | **6** — sky dome (3676), a fullscreen pass (4540), a backside FX (4824), portal (6428, 6465), aetheria (29113) | **→ NodeMaterial/TSL** |
| `onBeforeCompile` | **6** — base injector (3880), ground detail-blend (4255), water (4416), wind-sway (4777) | **→ NodeMaterial/TSL** (unsupported on WebGPU) |
| Hand-rolled post | bloom + filmic tonemap + vignette + SSAO, inline (2065); `POST.rtScene/rtA/rtB/rtAO(2)`, `quadCam`, fullscreen `quad` (4550-4574) | **→ `PostProcessing` node graph / TSL passes** |
| Reflection | `REFL.rt` water reflection target (4678) | RenderTarget re-render; TSL-ify the water sampler |
| MSAA | `samples:4` on `WebGLRenderTarget` gated by `capabilities.isWebGL2` (4550) | WebGPU MSAA differs; use renderer AA / node AA |
| Render targets | **8** `WebGLRenderTarget` | `RenderTarget` (mostly drop-in) |
| Pixel readback | **1** `readRenderTargetPixels` (portrait/snapshot) + direct `gl.readPixels` in the appraisal panel | **→ `readRenderTargetPixelsAsync`** (async) |
| Renderers | **2** `new THREE.WebGLRenderer` (main + ID-portrait) | **→ `WebGPURenderer`** (async `init()`) |
| `uniforms` refs | ~82 | migrate to TSL `uniform()` nodes |
| Boot | classic `<script>` blocks run inline at parse, use `THREE` immediately | **ordering gotcha** — see §3.1 |

Not present (good news): no `EffectComposer`/`ShaderPass`/`UnrealBloomPass` addons, no legacy `Geometry`,
no `.encoding`/`gammaFactor`/`physicallyCorrectLights`. Post is hand-rolled, which is *more* work to port
than stock passes, but there are no addon-version headaches.

---

## 2. Interpreting "three instead of three.min"

Two readings:
- **(a) trivial:** swap `three.min.js` for the unminified UMD `three.js` (same r159). One line. Buys
  readable stack traces, nothing else. **Subsumed by (b).**
- **(b) the real move (do this):** adopt the **ES-module build** (`three.module.js`) via an **importmap**,
  because it is the only way to import `WebGPURenderer`, node materials, and TSL. This is the Phase-A
  foundation. It keeps the game self-hosted/offline (vendor the ESM files; no CDN).

We take (b). It delivers the deminified/debuggable build *and* unblocks WebGPU.

---

## 3. Phases (respect the A→B→C dependency)

### Phase A — ESM foundation (ONE agent, lands first; both machines pull before Phase C)
Goal: the game runs **exactly as today on `WebGLRenderer`**, but Three.js is now the ESM build behind an
importmap. No WebGPU yet. This de-risks everything downstream.

1. **Vendor the ESM builds** into `vendor/` (self-hosted, offline): `three.module.js`, `three.webgpu.js`
   (or `three/webgpu`), `three.tsl.js` (or `three/tsl`), and the `jsm/` addons actually used
   (`loaders/GLTFLoader.js`, `utils/SkeletonUtils.js`, plus any TSL display passes for post). Pin the exact
   revision; record it in `vendor/THREE_VERSION`.
2. **Add an importmap + module bootstrap** in `index.html`:
   ```html
   <script type="importmap">{"imports":{
     "three":"./vendor/three.module.js",
     "three/webgpu":"./vendor/three.webgpu.js",
     "three/tsl":"./vendor/three.tsl.js",
     "three/addons/":"./vendor/jsm/"}}</script>
   <script type="module">
     import * as THREE from 'three';
     import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
     import { clone as skeletonClone } from 'three/addons/utils/SkeletonUtils.js';
     window.THREE = THREE; window.GLTFLoader = GLTFLoader; window.skeletonClone = skeletonClone;
     window.__bootThree();     // hand control to the game
   </script>
   ```
3. **Fix the boot-ordering gotcha (§3.1).** Module scripts are deferred → they run AFTER classic scripts.
   The game currently touches `THREE` at parse time in classic `<script>`s. Wrap the game's entry so it is
   invoked by the module (`window.__bootThree`) once `THREE` is on `window`, instead of auto-running.
4. **Bump r159 → latest stable.** Work the (small) API delta: node/webgpu entry points, any renamed
   constants, `WebGLRenderer` param tweaks. Keep `WebGLRenderer` for now.
5. **Verify parity:** jsc syntax → Browser-pane runtime (third-person, [[over-shoulder-test-view]]) →
   confirm zero console errors and the world renders identically. Commit + **push to `webgpu`**. Both
   machines pull. **This is the sync checkpoint that unblocks Phase C.**

> Strong recommendation: as part of A (or immediately before it), **extract the render/shader code into its
> own file(s)** — e.g. `js/render.js` (renderer, POST pipeline, RTs) and `js/shaders.js` (the 6 ShaderMaterials
> + 6 onBeforeCompile). This is the [[dereth-ac-gap-analysis]] file-split idea, and it is what lets the two
> agents work in **separate files with no merge conflicts** in Phase C. Classic scripts share one global
> scope, so the split needs **zero code changes** — just ordered `<script src>` includes. See §4.

### Phase B — WebGPURenderer scaffold (ONE agent, after A)
1. Introduce `WebGPURenderer` behind a switch (`?gpu=1` URL param or a setting), `WebGLRenderer` still the
   default. Async: `renderer = new WebGPURenderer({...}); await renderer.init();` before the first frame;
   the boot path becomes async (or use `renderer.renderAsync`).
2. Get a **basic scene** (sky + terrain + a lit mesh) rendering on WebGPU. Expect built-in
   `MeshStandardMaterial` meshes to "just work"; the **custom-shader** meshes will render wrong/blank until
   Phase C — that is expected and is the Phase-C worklist.
3. Commit + push. Checkpoint: both renderers selectable; parity on stock materials.

### Phase C — TSL / NodeMaterial rewrites (BOTH agents in parallel, partitioned by subsystem)
This is the bulk. Convert every custom GLSL surface to nodes. Split so the two agents touch **different
files/regions** (see §4 ownership table). Each conversion: replace the `ShaderMaterial`/`onBeforeCompile`
with the node-material equivalent, port uniforms to TSL `uniform()`, verify on **both** backends.

- Custom `ShaderMaterial` → `NodeMaterial` (e.g. `MeshBasicNodeMaterial`/`MeshStandardNodeMaterial`) with a
  TSL `colorNode`/`positionNode`.
- `onBeforeCompile` chunk-injection → the corresponding node hook (`positionNode` for wind-sway/vertex
  displacement, `colorNode`/`outputNode` for ground detail-blend & water tinting).
- Hand-rolled post (bloom/tonemap/vignette/SSAO) → `three/webgpu` `PostProcessing` + TSL passes
  (`three/addons/tsl/display/*` for bloom, etc.), or port each fullscreen pass to a TSL fragment node.
- `readRenderTargetPixels` → `await readRenderTargetPixelsAsync(...)` (portrait/appraisal snapshot code).
- MSAA: use renderer AA / node AA instead of `samples` on a WebGL target.

### Phase D — integration, fallback QA, perf, cutover (BOTH agents, together)
1. Feature-detect WebGPU; auto-fallback to the WebGL2 backend. One code path, two backends.
2. Test matrix: Chrome/Edge (WebGPU), **Apple-Silicon Safari 18+** (WebGPU, the offline target), Safari
   17 / Firefox (fallback). Verify [[verify-dereth-workflow]] on each.
3. Perf pass ([[dereth-graphics-pipeline]] is perf-critical — the 14-light pool, instanced forests,
   analytic sky). Compare frame time WebGPU vs WebGL2; fix regressions.
4. Flip default to WebGPU-with-fallback behind a setting; when stable, **merge `webgpu` → `main`**, push.

---

## 4. Two-agent / two-machine coordination

**The #1 risk is two agents editing the 34.5k-line `index.html` → merge hell.** Mitigations, in order:

1. **File-split first (Phase A prerequisite).** Extract render/shader code into `js/render.js` +
   `js/shaders.js` (+ `js/post.js`). Classic scripts share global scope, so this is a pure cut-and-include
   with no code changes. After the split, the two agents own **different files** and rarely conflict.
2. **Shared `webgpu` branch, never `main`** until Phase D cutover. Both machines:
   `git pull --rebase origin webgpu` **before every push**; small, self-contained commits; push often.
3. **File/region ownership** (below). An agent edits only its files; cross-file needs are coordinated via a
   quick note in commit messages or a `docs/webgpu-migration-status.md` scratch log both update.
4. **Keep the game runnable at every commit** (WebGL default until D) so both machines can always test.
5. **Sync checkpoints** = the end of Phase A and Phase B: those land on `webgpu`, everyone pulls, *then*
   parallel Phase C starts. Do not start C before A is pulled on both machines.

### Ownership table (Phase C — the parallel bulk)

| Agent | Owns (files/subsystems) | Custom-shader worklist |
|---|---|---|
| **Agent 1 — world/terrain** | `js/shaders.js` world half + terrain/water code | sky dome (3676), ground detail-blend `onBeforeCompile` (4255), water `onBeforeCompile` (4416) + reflection RT (4678), wind-sway `onBeforeCompile` (4777) |
| **Agent 2 — FX/post** | `js/post.js`, `js/shaders.js` FX half | post pipeline (bloom/tonemap/vignette/SSAO, 2065 + 4550-4574), portal FX (6428, 6465), aetheria (29113), the two remaining `ShaderMaterial`s (4540, 4824), `readRenderTargetPixels`→async |

Phase A & B are **single-agent** (foundation must be serial). Assign A+B to whichever agent starts; the
other agent waits for the Phase-A push, pulls, then both start Phase C on their owned files.

### Git flow (both machines, every unit of work)
```
git checkout webgpu
git pull --rebase origin webgpu          # sync before you touch anything
# ... edit ONLY your owned files ...
# jsc syntax check → Browser-pane runtime verify (both backends where relevant)
git add <your files> && git commit -m "webgpu: <subsystem> → TSL (…)"
git pull --rebase origin webgpu          # re-sync in case the other agent pushed
git push origin webgpu                   # the other machine pulls this to stay in sync
```

---

## 5. Risks & gotchas (concrete)

- **`onBeforeCompile` is a no-op on WebGPU** — the 6 sites will silently stop affecting output. They must
  become node hooks, not "patch later."
- **GLSL `ShaderMaterial` doesn't run on the WebGPU backend.** No shim; rewrite in TSL.
- **Async everywhere:** `await renderer.init()`; `readRenderTargetPixelsAsync`; first frame after init.
  The boot and the appraisal-portrait snapshot both go async.
- **Module scripts run after classic scripts** — the boot-ordering fix (§3.3) is mandatory or `THREE` is
  `undefined` when the game code runs.
- **Self-hosted/offline:** vendor every ESM file (three core + webgpu + tsl + jsm addons). No CDN. Pin the
  revision; a strict CSP/local-file setup will reject anything external.
- **Fallback parity:** validate the WebGL2 backend of `WebGPURenderer`, not just native WebGPU — the
  offline Apple-Silicon target and older browsers depend on it.
- **Perf is not free:** WebGPU can be faster, but a naïve node-material port can regress. Keep the
  [[dereth-graphics-pipeline]] levers (light pool, instancing, analytic sky) intact; measure.
- **Snapshot/verify tooling:** the Browser-pane `gl.readPixels` trick used for headless proof
  ([[verify-dereth-workflow]]) is WebGL-specific — on WebGPU use `readRenderTargetPixelsAsync` or
  `toDataURL` off the canvas.

---

## 6. Definition of done

- No `three.min.js`; ESM `three.module.js` + importmap; vendor addons are ESM. Pinned revision recorded.
- Single `WebGPURenderer` path; WebGPU when available, automatic WebGL2 fallback otherwise.
- All 6 `ShaderMaterial` + 6 `onBeforeCompile` + the full post chain reproduced via NodeMaterial/TSL, with
  visual parity to today on **both** backends.
- Verified on Chrome + Apple-Silicon Safari 18+ (WebGPU) and a fallback browser (WebGL2); zero console
  errors; no perf regression vs the current WebGL build.
- Merged to `main`; both machines `git pull` and are in sync.

---

*Sizing: Phase A ≈ 1–2 days (foundation + bump + boot reorder + file-split). Phase B ≈ 1 day. Phase C ≈ the
bulk, ~1–2 weeks split across the two agents (shader-by-shader). Phase D ≈ 2–4 days (fallback + perf + QA).
The file-split in A is what makes C safely parallel — do not skip it.*
