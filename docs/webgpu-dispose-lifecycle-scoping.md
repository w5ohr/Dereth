# WebGPU dispose-lifecycle bug — scoping (machine 1 / FX-post agent)

Scope-only. The fix itself needs a real GPU to verify → **machine 2** (has the box + the `exitDungeon` repro).
This doc pins the mechanism, reconciles the A/B evidence, and lays out a phased, low-risk fix with exact
hook points.

## Symptom
Under `?gpu=1` (WebGPURenderer / r185): `GPUValidationError: [Buffer …] used in submit while destroyed`
(219–438× per run), sometimes device loss + frozen canvas. **Repro:** fresh char → `exitDungeon(true)`.

## Root cause
The game's eviction/teardown assumes **WebGL semantics**: `geometry.dispose()` / `material.dispose()` /
`texture.dispose()` free the GPU copy but three.js **lazily re-uploads** from the retained CPU-side data
(`BufferAttribute.array`, canvases) the next time the object is rendered. So the game freely disposes GPU
buffers of objects it intends to keep or rebuild.

On **WebGPU r185**, `dispose()` **destroys the `GPUBuffer` immediately**, and the backend caches **bind
groups / render pipelines** that still reference it. A submit that references a destroyed buffer → the
validation error; a live object holding a cached bind group over a destroyed buffer → persistent spam.

## Two distinct failure modes (both present — this is why the deferred-shim A/B "didn't fix it")
- **Mode A — in-flight submit references a just-destroyed buffer.** `buildWorld()` (js/game.js ~7144, run
  by `exitDungeon`) synchronously disposes **every** entity array in one tick (~7147-7165: monsters,
  projectiles, drops, npcs, shops, scenery, roadMeshes, nodes, dungeonObjs, netObjs, ships…). The
  **previous frame's GPU submit is still in flight** and references those buffers → destroying them mid-flight
  faults, once per still-referenced object → the high count. **This is the `exitDungeon` repro.**
- **Mode B — a still-LIVE object references a destroyed buffer.**
  - **B1:** a shared geometry/material/texture that is NOT flagged `_acShared` gets disposed by one object's
    teardown while **another live object still uses it**. WebGL re-uploads for the live user; WebGPU dangles.
    (Matches machine 2's "still-live objects re-bind the destroyed buffer.")
  - **B2:** `releaseObjectGPU` frees a **detached** object's buffers (cullWorld state 3), then `cullWorld`
    re-attaches it (`scene.add`, state 1) when the player returns — on WebGPU the re-attach may not re-upload
    / keeps a stale bind group. (Note: cullWorld detaches in state 2 *300 s before* releaseObjectGPU runs,
    and doesn't run in dungeons — so releaseObjectGPU is NOT an in-flight problem; its only risk is B2.)

**A/B evidence reconciled:** "no-op all dispose → zero faults" ⇒ buffer destruction is the sole trigger.
"3-frame deferred-dispose didn't fix it" ⇒ frame-count deferral (a) doesn't guarantee the GPU submit
actually completed, and (b) does nothing for Mode B (a live object still points at the buffer whenever you
finally destroy it). So the fix must handle **both** the in-flight timing (A) **and** the still-referenced
case (B).

## The choke point (good news)
Every eviction funnels through three one-line helpers (js/game.js ~18864-18866):
```js
function _dispTex(t){ if(t&&!t._acShared&&t.dispose) t.dispose(); }
function _dispGeo(g){ if(g&&!g._acShared&&g.dispose) g.dispose(); }
function _dispMat(m){ …_dispTex(x.map);_dispTex(x.normalMap); if(x.dispose)x.dispose(); }
```
`disposeObject3D` and `releaseObjectGPU` both call `_dispGeo`/`_dispMat`. Direct `.dispose()` sites that
BYPASS the choke are few and small: portal tube (~11168), grassMesh (~6624), a handful of one-off FX. So a
WebGPU-aware change at these three helpers covers ~all of the burst.

## Fix — phased, low-risk

### Phase 1 — deferred destruction + no-op the live-release (proven-safe direction; do first)
1. **On WebGPU, `_dispTex`/`_dispGeo`/`_dispMat` ENQUEUE the resource instead of destroying it now.** A
   `_wgpuDisposeQueue` holds `{resource}`; the object is still detached immediately by `disposeObject3D`
   (removed from the scene, so it stops being rendered) — only the **GPU-buffer destruction** is deferred.
2. **Flush the queue only after the in-flight submit is done.** After each WebGPU frame's submit, chain
   `renderer.backend.device.queue.onSubmittedWorkDone().then(flush)` (or, simplest and race-free: flush the
   PREVIOUS frame's queue at the START of the next `renderComposite` — by then the prior submit has
   completed). This kills **Mode A** (nothing is destroyed while a submit references it).
3. **On WebGPU, make `releaseObjectGPU` a NO-OP.** Its VRAM-freeing is a WebGL optimization; the objects are
   already detached (not rendered), so keeping their buffers resident is fine, and re-attach then needs no
   re-upload. Kills **Mode B2**. (Machine 2 already A/B'd that no-op'ing dispose = zero faults, so this is
   known-safe; it just trades some idle VRAM.)

This is the smallest change that should clear the `exitDungeon` repro (Mode A) and the fast-revisit path
(B2) outright. Wrap the few direct `.dispose()` bypass sites (portal tube, grass) in the same deferred helper.

### Phase 2 — only if residual faults remain (Mode B1: shared-but-unmarked)
Audit for shared geometry/material/texture that a teardown disposes while another live object still uses it,
and flag them `_acShared` (the existing #326 mechanism). Deferral alone won't save B1 — a genuinely
still-referenced resource dangles whenever it's finally destroyed. Candidates: merged town archetypes,
canvas/normal/atlas textures, any geometry shared across pooled objects. `grep` for geometries/materials
constructed once and reused by multiple meshes without `_acShared`.

### Phase 3 — end-state (optional, biggest lift): refcounted resource manager
`_dispGeo`/`_dispMat` decrement a refcount; a resource is destroyed only when refcount==0 AND after
`onSubmittedWorkDone()`. Robust but only worth it if Phases 1-2 leave gaps.

## Implementation hook points (for machine 2)
- **`_dispTex`/`_dispGeo`/`_dispMat`** (~18864): `if(IS_WEBGPU){ _wgpuDisposeQueue.push(resource); return; }`
  else the current immediate `.dispose()`.
- **`releaseObjectGPU`** (~18876): `if(IS_WEBGPU) return;` at the top.
- **Flush**: in `renderComposite`'s WebGPU branch (~3893), before the render, drain the queue accumulated
  since the last submit (prev-frame's submit is complete by now); OR after render, schedule
  `renderer.backend.device.queue.onSubmittedWorkDone().then(()=>flush())`. Prefer the drain-at-start form —
  no promise races, and `onSubmittedWorkDone` availability across the WebGL2-fallback backend is untested.
- **Direct bypass sites** to route through the deferred helper: portal tube `p.tube.geometry.dispose()`
  (~11168), `grassMesh.geometry.dispose()` (~6624), `mkPuddle`/level-up swirl material disposes, the two
  `snapshotToCanvas` RTs (already async-safe).

## Verification plan (machine 2, real GPU)
1. **exitDungeon repro:** fresh char → `exitDungeon(true)` under `?gpu=1`; count
   `used in submit while destroyed` at HEAD vs with the fix. Target: 0.
2. **Fast-revisit (B2):** walk >CULL_EVICT_R (800u) away for >CULL_EVICT_S (300s, or lower the consts via
   the #232 harness), return, and render the re-attached scenery. Target: 0 faults, correct render.
3. **Soak:** several dungeon enter/exit cycles + overworld roaming; watch for device loss / frozen canvas.
4. **WebGL parity:** confirm `IS_WEBGPU` gating leaves the WebGL path's immediate-dispose behavior untouched
   (VRAM still freed on WebGL; no deferral).

## Risks / tradeoffs
- Deferred destruction holds GPU buffers ~1 extra frame → negligible.
- `releaseObjectGPU` no-op on WebGPU → distant "will-return" objects keep their buffers resident (some idle
  VRAM). Acceptable; can be reclaimed later via the Phase-3 refcount manager if it ever matters.
- The drain-at-start-of-next-frame flush assumes one frame is enough for the prior submit to finish — true
  for a single in-flight frame; if the renderer ever pipelines >1 frame, use `onSubmittedWorkDone`.

## Backend reproducibility — UPDATE (machine 1, Apple-Silicon MacBook Pro, integrated GPU / Metal)
Machine 1 has a real WebGPU device (Dawn-on-**Metal**) and tried hard to reproduce the fault:
`exitDungeon` burst with **awaited** frames, with **in-flight (non-awaited)** submits (the Mode-A pattern),
with valid non-zero render targets (see the size-0 fix below, which removed the confound), AND a **6-cycle
`buildWorld` soak**. **Zero `used in submit while destroyed` across all of it.** So the bug appears
**backend-specific to machine 2's Windows/AMD/D3D12 backend** — Dawn-on-Metal seems to defer the real
buffer destruction until the referencing submit completes (spec-permitted), so it never faults.
- **Implication for priority:** this project's PRIMARY target is **offline on Apple Silicon** (Metal), where
  the bug does **not** manifest. It's real on D3D12 (machine 2's dev box) and worth fixing for cross-backend
  safety, but it is **not** a blocker for the intended platform. Verify the Phase-1 fix on machine 2 (the
  only place it reproduces).
- Caveat: machine 1's repro is synthetic (the Browser pane is a hidden/throttled tab, no real rAF pacing).
  Not 100% conclusive, but strong (multiple patterns, valid targets, soak — all clean).

## Size-0 render-target bug — FOUND & FIXED (machine 1, verified on Metal)
Separate, real WebGPU robustness gap surfaced while chasing the above: when the canvas is 0×0 (a
background / minimized / hidden tab, or pre-layout), the game's `renderer.setSize(innerWidth,innerHeight)`
and `cam.aspect=innerWidth/innerHeight` create **0-size swapchain/depth/color textures** → a flood of
`GPUValidationError: texture size … is empty` / `Could not create a swapchain texture of size 0`
(and NaN aspect). WebGL tolerated 0-size; WebGPU errors hard. **Fix:** clamp all five sizing sites to
`Math.max(1, …)` (js/game.js — 4× `renderer.setSize`, 1× `cam.aspect`). Verified on this Metal box: the
size-0 error flood is **gone** (0 errors, canvas stays valid through the exitDungeon rebuild). Harmless
no-op when `innerWidth>0` (the normal case). This also un-confounded the dispose repro above.

## One-line summary
Route all GPU disposal through the existing `_dispGeo`/`_dispMat`/`_dispTex` choke; on WebGPU **defer** the
actual destroy until the in-flight submit completes and **no-op `releaseObjectGPU`** — then, only if needed,
`_acShared`-audit for shared-but-unmarked resources.
