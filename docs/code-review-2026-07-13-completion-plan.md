# Dereth — Code-Review Completion Plan (2026-07-13)

Companion to [code-review-2026-07-13.md](code-review-2026-07-13.md). That doc is *what's wrong and why*;
this is *how to finish every actionable item* — sequenced, with implementation approach, verification, effort,
and dependencies. Reviewed a second pass (Fable 5) against the actual code at HEAD `d151b2b6`; the plan below
reflects two corrections found on re-read (noted inline).

**Ground rules (all work packages):**
- One verified change per commit; message names the finding ID.
- Client changes: `jsc` syntax check → Browser-pane runtime check (third-person) → screenshot/log proof → commit.
- Server changes: add/extend a `server/tsa_*.py` harness that *asserts the exploit is now rejected* → run it → commit.
- Nothing here needs npm, a bundler, or ES modules.

Effort key: **S** ≈ ≤½ day · **M** ≈ 1–2 days · **L** ≈ 3+ days / architectural.

Legend for status column (update as you go): `todo` / `wip` / `done` / `blocked`.

---

## Re-read corrections (things the raw findings slightly overstated)

1. **S2 / S4 are narrower than "client owns everything."** The `input` handler already binds
   `cl.level = min(reported, cl.level_auth)` and clamps `cl.hp` to a sane display ceiling (`dereth_server.py:2532-2534`,
   #238). So forged *level* is already contained for gating; the live holes are **initial gold/inv/xp at create**
   (S2) and the fact that HP is display-only, not a **damage-absorbing pool** (S4). The fixes target exactly those.
2. **`sanitize_save` is intentionally "plausible-forgery" tolerant** (`:373-377` comment). The S2 fix is therefore
   *not* "make sanitize stricter" (that would break legit high-level saves); it's "a **newly created** slot gets a
   server starter, ignoring client economy fields" — a separate, surgical branch in `create_char_slot`.

---

## Phase 0 — Zero-risk hygiene (do first; ~half a day total)

| WP | Finding | Do | Verify | Effort |
|----|---------|-----|--------|--------|
| 0.1 | R2 | `git rm --cached acdata/client_local_English.dat` (keep on disk). Optionally `git filter-repo` to purge from history **only if** the licensing concern warrants rewriting public history — otherwise leave history and stop tracking. | `git ls-files acdata/` empty; file still on disk | S |
| 0.2 | R3, R4 | Add `assets/*.dat` and `* 2.*` to `.gitignore`; `rm` the three stray `" 2"` files. | `git check-ignore assets/client_highres.dat` → ignored; strays gone | S |
| 0.3 | O4 | nginx server block: `add_header Strict-Transport-Security "max-age=31536000" always;` + `X-Content-Type-Options nosniff` + `X-Frame-Options SAMEORIGIN`; `location = /sw.js { add_header Cache-Control "no-cache"; }`. Mind per-`location` `add_header` inheritance (repeat inside `/assets/` blocks). | `curl -I https://www.derethgame.com` shows headers; `/sw.js` `no-cache` | S |
| 0.4 | X1 | `index.html:31855` — extend `esc()` to also escape `"`→`&quot;` and `'`→`&#39;`. One-line change; closes all four attribute-context sinks at once. | jsc OK; craft an item whose name contains `"` + `onmouseover` (inject via console into `player.inv`), render inventory, confirm handler is inert (renders as text) | S |
| 0.5 | X2 | Wrap network item names in `esc()` at `lootItem:31330`, `rarityName:16498`, shop row `26088` — mirror `examineHTML:31366`. | Set an item `name` to `<img src=x onerror=…>` in console, pick up / open inventory, confirm no execution | S |

**Ship gate:** after Phase 0, the entire latent-XSS class is closed and the repo/deploy landmines are defused.

---

## Phase 1 — Anti-cheat / server authority (highest value; server-only, no client gameplay risk)

Order matters: **S3 first** (it underpins every range gate), then damage, then economy, then the big HP-pool work.
Each WP ships with a `tsa_*.py` regression. The fuzz suite's `char:{}`-only coverage is *why S2 slipped* — new
harnesses must send forged payloads.

| WP | Finding | Do | Verify | Effort |
|----|---------|-----|--------|--------|
| 1.1 | S3 | In the `input` handler (`:2528-2530`): after `_finitef`, clamp `cl.x,cl.z` to `±WORLD_LIMIT`, and reject a move whose delta from the previous `(x,z)` exceeds `MAX_SPEED*dt*SLACK` (track `cl._lastpos,_lastt`); on reject, snap back to last valid and optionally count strikes. Pick `MAX_SPEED` from the client's real max (sprint+mount) × generous slack to avoid false positives on lag spikes. | New `tsa_speedhack.py`: connect, `play_char`, send a 5000-unit teleport → assert server keeps prior pos and peers never see the jump. Also assert legit walking is unaffected. | M |
| 1.2 | S1 | `resolve_attack:1663` — replace `min(dmg, mhp*1.5)` with a **level-derived per-hit ceiling** (server knows `cl.level_auth`; cap to e.g. `expected_hit(level)*K`) **and** a server-side per-`(cl,mid)` attack cooldown (reject hits faster than the weapon speed floor). Keep the `dealt` tagging. Also throttle `credit_xp` (`:524-538`) — a per-connection XP/sec ceiling so instakill-spam can't power-level even if a hit slips. | Extend `tsa_soak.py` or new `tsa_dmg.py`: send `{t:"attack",dmg:1e6}` → assert mob HP drops by ≤ceiling; spam attacks → assert cooldown rejects; assert XP/sec bounded. | M |
| 1.3 | S2, X3 | `create_char_slot:359` — before persist, if the slot is **new**, overwrite economy fields with a server starter (`gold=STARTER_GOLD, level=1, xp=0, xpUnspent=0, inv=STARTER_INV, skills=base`) rather than trusting `data`. Leave cosmetic/heritage/appearance from the client. `load_econ` then only ever seeds from a save the server itself bounded. (X3 is the same root — the client's `{t:"save"}` blob stays the persistence channel, but create no longer trusts it for economy.) | New `tsa_forged_chargen.py`: `create_char` with `char={"gold":2e9,"level":275,"inv":[500 items]}` → `play_char` → assert `coin==STARTER_GOLD`, `level_auth==1`, `inv==STARTER_INV`. Assert an *existing* legit high-level save still loads unchanged. | M |
| 1.4 | S7 | `login` dispatch (`:2462`): length-cap + charset-restrict `msg.user` before use; add a **per-IP** login/register throttle distinct from the 30/s bucket; prune `_LOGIN_FAILS` (LRU or periodic sweep in the tick loop). Also `delete_char_slot`: `NPC_VASSALS.pop(freed,None)` (M5). Add periodic `VACUUM INTO` DB backup + document restore (M4). | Extend `tsa_fuzz.py`: flood 10k unique-username logins → assert `_LOGIN_FAILS` stays bounded and CPU/mem don't blow up; assert over-long username rejected. Manual: confirm backup file appears. | M |
| 1.5 | S4, S5 | The "M3" work: give the server an **authoritative HP pool** per client. Apply mob damage (`world_step:1634-1637`) and PvP damage (`:2690-2699`) to that pool server-side; server **declares death** (broadcast + respawn), client HP becomes a mirror of the server value. This is the largest lift and changes a core loop — do it behind a flag, test on `:8799` throwaway server first. | New `tsa_hp_authority.py`: client reports `hp:100` while server applies lethal mob damage → assert server declares death regardless of client claim. PvP: assert victim can't ignore damage. | L |
| 1.6 | S6 | Server-authoritative **item minting**: server assigns item IDs on server-resolved loot/craft; trade/market validate against server-minted provenance, not client `cl.inv`. Biggest lift of all. **Decision point:** either schedule this as its own project after 1.1–1.5, or explicitly **document item authenticity as an accepted limitation** for now (trade is between consenting players; forged items don't grant server-side power beyond what S2 already closes). | If built: `tsa_item_mint.py` asserts a console-forged legendary can't be listed/traded. If deferred: a one-paragraph note in the remaining-work doc. | L / defer |

**Ship gate:** after 1.1–1.4, the three CRITICALs + auth DoS are closed with regressions. 1.5 closes god-mode/PvP;
1.6 is the last authority frontier (build or formally accept).

---

## Phase 2 — Ops resilience (protect the live site; ~2–3 days)

| WP | Finding | Do | Verify | Effort |
|----|---------|-----|--------|--------|
| 2.1 | O1, Q1 | `.github/workflows/ci.yml` on push/PR: (a) extract inline `<script>`s from `index.html`, wrap-and-`--check` via node (or replicate the jsc approach) + check `sw.js`; (b) `python3 -m py_compile server/*.py tools/*.py`; (c) run `server/tsa_persist.py` + `test_client.py` against a throwaway server; (d) `tools/run_all_tests.js` aggregating the 16 puppeteer harnesses (best-effort, non-blocking if Chrome unavailable). Fail build on any hard error. | Open a PR with a deliberate syntax error → CI red. Fix → green. | M |
| 2.2 | O2 | Rewrite `deploy/update.sh` for atomic release: `git worktree add /opt/dereth-releases/<sha>`, build/verify, `ln -sfn <sha> /opt/dereth-current` (nginx roots at `-current`), then restart; verify `systemctl is-active dereth` post-restart and **roll the symlink back** on failure. Add `StartLimitIntervalSec`/`StartLimitBurst` to `dereth.service` so a crash-loop enters `failed` not infinite restart. | Stage a bad commit on a test box → deploy → assert symlink stays on last-good and service reports failed cleanly, site still serves old build. | M |
| 2.3 | O3 | Content-address mutable asset packs: a small `tools/hash_assets.py` that renames changed packs to `name.<hash>.ext` and rewrites references in `index.html`; then make nginx `immutable` correct and **delete the manual `sw.js` `V` ritual** (SW can cache by URL alone). | Re-export a mesh, run the tool, deploy → returning client fetches the new URL with no `V` bump and no stale mesh. | M |

---

## Phase 3 — Client performance quick wins (measurable, low-risk; ~2 days)

| WP | Finding | Do | Verify | Effort |
|----|---------|-----|--------|--------|
| 3.1 | P1 | `updateHUD():19811` — throttle to ~10 Hz via an accumulator (mirror `streamMonsters`); cache all `getElementById` into a `HUD` struct at init; dirty-check writes (`if(el._v!==v){el._v=v; el.textContent=v;}`). Keep instantaneous update on explicit state changes (damage, level-up) via a `hudDirty()` nudge so nothing feels laggy. | Browser pane: confirm HUD still updates on damage/heal/level; sample frame time before/after (fewer DOM ops via a wrapped `getElementById` counter). | S |
| 3.2 | P2 | Remove `frustumCulled=false` from the **per-creature/per-person** clone sites (`2114,2186,9807,11439,12264,24831`); keep it on the instanced forest/grass/rain pools. Ensure each root has a sane bounding sphere (set generously if skinned bounds misbehave). | Face away from a town crowd, confirm off-screen NPCs are culled (spy on `renderer.info.render.calls` before/after); confirm no creature pops/disappears while on-screen. | S |
| 3.3 | P4 | Hoist scratch vectors for projectile orient (`18938-18939` → module `_UP`,`_dir`); cache floater textures by `txt+color` in an LRU (`textSprite`); pool burst sprites (`burst`). | Big fight (spawn 10 mobs, AoE): confirm damage numbers/bursts render identically; watch for GC-pause reduction in console timing. | S |
| 3.4 | P3 | On touch devices, when `mobilePixelRatio` overrides the cap, drop MSAA `samples` 4→2 (or 0) and shadow 4096→3072; start `auto` at tier 2 and let `_hi` promote. | `resize_window` mobile preset + `colorScheme`; confirm boot is smoother, governor still promotes on capable devices. | S |
| 3.5 | P5/A5 | Swap hot-loop `Math.hypot`→`Math.sqrt(a*a+b*b)` in per-mob AI (`19137-19138` etc.); squared-distance for the CAPITALS scan; pool light-pool candidate objects (`4186`). | jsc OK; confirm mob behavior/town push-out unchanged in a play session. | S |

---

## Phase 4 — Maintainability (incremental, each step revertable; ~1 week)

**Order is deliberate: de-dupe → decompose → split.** Doing the mob-loop de-dup first removes a live drift trap
*before* the file split moves code around.

| WP | Finding | Do | Verify | Effort |
|----|---------|-----|--------|--------|
| 4.1 | A2 | Extract `renderMobFrame(m, dt, moving)` (rig anim + position/`groundY` + scale + health-bar billboard/fill); call it from both the local (`19140`) and networked (`32170`) loops. Reconcile the drift deliberately (pick `m.ang` vs `m.yaw` per loop; keep the `Math.max(0,…)` clamp; unify bar-visibility incl. the target-lock rule from the recent commit). | Browser pane: local + networked mobs render identically; targeted full-HP mob keeps its bar (the recent fix); no double-update. | M |
| 4.2 | A3 | Split `update():18742-19283` into `stepPlayer/stepMobs/stepProjectiles/stepStatus` top-level functions called in sequence. Mechanical given global scope; `stepMobs` absorbs 4.1. | Full play session: physics, AI, projectiles, status ticks all behave; render try/catch still wraps the sequence. | M |
| 4.3 | A1 | Physically split the giant `<script>` into ordered `js/*.js` includes (classic scripts → shared global scope → **zero code changes**): e.g. `data-tables.js` → `world.js` → `combat.js` → `ui-panels.js` → `net.js` → `crafting.js`; move `<style>` → `css/game.css`. A few files at a time; boot-verify after each; update `sw.js` precache + nginx. | jsc each file; boot after each split; SW caches the new files; no missing-global errors. | L |
| 4.4 | A4 | Sweep the 89 empty catches: keep silent swallow only for storage/feature-probe; give subsystem catches (`loadACBody`, `buildTinker`, anim-stop, scenery build) a `console.warn("<sys> failed", e)`. Add a jsc/regex lint that flags duplicate top-level declarations (catches the `yaw is not defined` class). | jsc OK; deliberately break a subsystem, confirm it now warns instead of failing silently. | S |

---

## Phase 5 — Longer-horizon / opportunistic

| WP | Finding | Do | Effort |
|----|---------|-----|--------|
| 5.1 | R1 | Move heavy binaries out of git history: Git-LFS for `assets/**/*.{png,glb,mp3,ogg}` (+`.gitattributes`) **or** a CDN/rsync origin for the music + texture-source packs. Ensure LFS/CDN objects are reachable at deploy time. Stop the 1 GB `.git` from growing. | L |
| 5.2 | T1 | `tools/README.md` (required dat build + SHA256, pip deps, run order); extract the copy-pasted `DatReader` BTree parser into a shared `tools/acdat.py` imported by the 5 scripts. | M |
| 5.3 | P5/A5 tail | `LS` localStorage-key registry (`const LS={token:"dereth_token",…}`) to end typo-drift. | S |

---

## Suggested execution cadence (autonomous)

Given the "work autonomously, verify each step, commit per verified feature" workflow:

1. **Sprint A (Phase 0 + Phase 1.1–1.4):** the security/anti-cheat core. Every item is small-to-medium, each
   with a regression harness. This is the highest-value, lowest-gameplay-risk block — do it as one focused sprint.
2. **Sprint B (Phase 2):** CI + atomic deploy + content-addressed assets — protects everything shipped after.
3. **Sprint C (Phase 3):** perf quick wins — all `S`-effort, visible improvements, low risk.
4. **Sprint D (Phase 4):** the refactor — 4.1→4.2→4.3→4.4 in order; the biggest time sink but the payoff is
   every future change gets easier and safer.
5. **Decisions to make explicitly** (don't let them drift): **1.5** (build HP authority now or schedule?), **1.6**
   (server item minting vs. documented acceptance?), **5.1** (LFS vs. CDN?), and **0.1** (rewrite git history for
   the licensing leak or just stop tracking?). These are the only judgment calls; everything else is mechanical.

Total: Phases 0–3 are ~1.5 weeks of focused work and close every CRITICAL/HIGH except the two large authority
lifts (1.5/1.6). Phase 4 is a second ~1 week. Phase 5 is opportunistic.
