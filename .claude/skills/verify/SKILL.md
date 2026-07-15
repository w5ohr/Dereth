---
name: verify
description: Launch and drive the Dereth client headless to verify a change — serve, boot into the overworld, screenshot, read console.
---

# Verifying Dereth changes

The client is a static-file game (`index.html` + `js/game.js` + `js/craft.js`, classic scripts in shared
global scope; Three.js ESM via importmap). No build step — serve and drive.

## Serve

`python -m http.server 8765` (per `.claude/launch.json`) drops concurrent fetches under load; prefer:

```
npx http-server . -p 8765 -c-1 --silent
```

## Drive (puppeteer-core + installed Chrome)

Chrome: `C:/Program Files/Google/Chrome/Application/chrome.exe`. A ready driver lives in the session
scratchpad as `drive.js` (recreate from this recipe if gone). Key steps:

1. Fresh `--user-data-dir` per run AND abort the `/sw.js` request (request interception) — a stale
   service worker serves old HTML/JS and its failures poison whole runs.
2. `goto http://localhost:8765/index.html?nocache=N` (add `&gpu=1` for the WebGPURenderer path).
3. `waitForFunction('typeof startGame==="function" && renderer !== null')` — initThree is async.
4. Skip menus: seed a save and load it —
   `localStorage.setItem('dereth_save_v1', seed); startGame(true)`.
   The seed MUST have `academy:{done:true}` or the load warps into the Academy interior (game.js
   ~30912). Get a seed once via `startGame(false,'aluvian')` → `exitDungeon(true)` → `saveGame()` →
   read localStorage. A known-good seed: scratchpad `save-seed.json`.
5. Wait for arrival with `waitForFunction('!portalTransit')` (its clock runs slower than wall time),
   then settle ~2s.
6. For comparable screenshots pin the scene:
   `gameTime=0.40; weather="clear"; weatherT=9999; cloudCover=0; wetness=0; snowAmt=0;` (wait ~5s for
   lerps), camera via `player.pitch/player.yaw`.
7. Console: `page.on('console'|'pageerror')`. Ignore the `net::ERR_FAILED` asset spam (environment) and
   `NotAllowedError: pointer lock` (headless). Real signals: `NodeBuilder`, `GPUValidationError`,
   `Device Lost`, `update() failed`, `renderComposite() failed`.
8. Read pixels from screenshots with `pngjs` — `drawImage` readback of the WebGPU canvas returns black.

## Gotchas

- `settings.gfx="0"; applyGfxSetting()` (tier 0) cuts load time and disables the WebGL POST grade —
  use it when comparing WebGL vs WebGPU output.
- WebGPU + dungeon-exit flows: known dispose-lifecycle bug wedges the renderer (see
  docs/webgpu-migration-status.md). Verify overworld things via the save-seed path instead.
- On Windows/AMD, Chrome intermittently drops the WebGPU instance ("valid external Instance reference
  no longer exists") ~50% of runs, headless worse than headed (`headless:false`). Retry; it's the box,
  not the change.
- Weather is random per run and visibly recolors the sky — pin it (step 6) before any color comparison.
