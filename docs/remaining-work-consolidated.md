# Dereth — Remaining Work (THE authoritative list)

**This is the single source of truth for outstanding work.** It supersedes and retires every other
to-do / gaps / roadmap document — those files now redirect here:

> `wrapup-remaining-work.md` · `REMAINING-WORK.md` · `ac-remaining-gaps.md` · `fidelity-split.md` ·
> `ac-newplayer-split.md` · `ac-data-extraction-roadmap.md` · `plan-ac-heads.md` ·
> `asherons-call-authenticity-gaps.md` · `asherons-call-longtail-gaps.md` · `three-agent-dat-update.md` ·
> `playtest-2026-07-05.md` · `loop-test-findings.md` · `building-entry-audit.md` · `../ROADMAP.md`

The `asherons-call-*-reference.md`, `kilmer-saga-*`, `ships.md`, and `asherons-call-housing-reference.md`
files are **design reference**, not to-do lists — they stay as-is.

**Live tracker — reconciled + code-verified, statuses current as of 2026-07-06.** This is a working
list: each item carries a **Status** (`open` / `WIP:<agent>` / `done` / `blocked`). Anything not listed
below (and everything marked `done`) is shipped — see **Confirmed shipped** at the bottom. Effort key:
**S** ≈ 1–2 days · **M** ≈ 1–2 weeks · **L** ≈ 2+ weeks / architectural risk.

> **Current status (2026-07-06, post PR #137):** **Lane A ✅ done** (#1 Damage/Crit Rating + Crushing
> Blow, #4 content) · **Lane B ✅ done** (#2 hats, #3 dye subpalettes, #12 dungeon-lighting slider,
> #13 shield mesh) · **Lane C ✅ done** (#5 entry corridors, #6 0x21 layout decode → `aclayouts.json`,
> #10 gold window chrome, #11 PAGE-99 paperdoll, #19 shop wield gates) · **all 4 design decisions
> resolved** (#14 keep incantation gate · #15 keep soft casting · #16 keep vitals model · #17 go retail:
> 3-day PK key, retail quest lockouts, mana-stone battery + draw-from-mana). **Still genuinely open:**
> **#18** Academy staff leak (lane B, bug) · the terrain-lane blockers (#7–#9, needs A1 data) ·
> on-hardware eyeballs (#10 chrome look, #13 window/doorway).
>
> **Playtest sweep (2026-07-06 night, main @ 46bf62d — post-#137 merged whole): CLEAN, no new findings.**
> Exercised: rear-attack DR geometry (chasing mobs face the player — no false rear bonus), Crushing
> Blow (~12% on bludgeon, damage spikes verified), the #14 incantation buff-up nudge, hats/dye-subpalette/
> dglight code paths, real-geometry dungeon run (kill/XP/drops/exit), death→vitae→half-gold, save fields
> (aetheria/academy), region+landmark+weather ticks across 5 regions, chrome/paperdoll/shop post-merge.
> Zero console errors. (#18, then the only open buildable item, has since been FIXED in PR #145.)
>
> **⚠️ TestSystemA overnight sweep (2026-07-07, 15 runs, main @ c3232dc) — 7 NEW findings, filed as
> items #20–#26 below (log-only, nothing fixed). Agents: claim by lane before fixing.**
> **#20** (A) duplicate `craftResultItem` · **#21** (C) 292 items → unexported icon DIDs ·
> **#22** (C) 20 dead functions (check `ccRandomize`/`resetUIPos` first) · **#23** (A) silent
> `delete_char` of active slot · **#24** (B) remote wield never syncs (players look bare-handed
> online) · **#25** (A) client `houseboot` has no server handler · **#26** (C) Docker image omits
> the server's runtime data files (loot catalog degrades). Full detail + reusable server harnesses
> (`server/tsa_*.py`) and per-run notes in `docs/testsystem-a-log.md`. Server/data/static layers
> exhaustively covered & green; only the live in-browser 3D/UI lane is untested (Chrome extension down).
>
> **⚠️ TestSystemB overnight sweep (2026-07-07, 13 runs, main @ c3232dc) — 1 HIGH + 1 LOW finding, filed
> as items #27–#28 below (log-only, nothing fixed). Agents: claim by lane before fixing.**
> **#27** (A) **dual-wield double-kill / double-loot** — a dual-wield killing blow re-kills the corpse
> via the unguarded off-hand `applyHit`, doubling kills/XP/gold/loot (~62% of killing swings; confirmed
> repro). · **#28** (A, LOW/not-reachable) `drinkPotion` doesn't clamp a negative heal amount.
> 52 subsystems + 5 boundary/depth batches otherwise **green** (breadth exhaustive; the FX-colour
> palette shipped in PR #149). Full detail + reusable harnesses (`$CLAUDE_JOB_DIR/tmp/tsb_*.js`) and
> per-run notes in `docs/testsystemb-playtest.md` (PR #146).
>
> **⚠️ TestSystemC test loop (2026-07-07, 21 passes, main @ c3232dc) — 2 runtime-confirmed findings,
> filed as items #29–#30 below (log-only, nothing fixed). Agents: claim by lane before fixing.**
> **#29** (A) **weapon-only offense buffs (oil, Might elixir, Heart Seeker crit) leak onto SPELL
> damage/crit** — `applyHit` applies them before the spell/phys branch; *shares the `applyHit` head
> with #27, coordinate.* · **#30** (A, LOW) focus-mana battery not refunded on fizzle/abort (silent
> battery→pool transfer; `focusPaid` captured but unused). Defensive combat + the full vendor economy
> (2,306 buyable items, 0 arbitrage loops) audited **clean**; MMO harness 47/47 every pass. Full
> pass-by-pass evidence in `docs/loop-test-findings.md` (PR #147, merged).

---

## Lane assignments — 3 agents (claim before you build)

Lanes are split **by code domain** so the three agents rarely touch the same region of `index.html`
and never the same item. **Rules:** (1) work **only** your lane's items; (2) before starting an item,
change its **Status** in the tables below from `open` to `WIP:<agent>`, and to `done` when merged;
(3) if you finish your lane, take the next `open` item from the **shared backlog** (§ Blocked / Design)
and mark it WIP first; (4) never edit an item another agent has marked `WIP`.

### ▶ CURRENT ROUND (2026-07-07) — fix the 11 test-sweep findings (#20–#30)

The three original build lanes are **complete** (historical table below). The open work now is the
**TestSystemA/B/C findings #20–#30** — re-laned by code region so agents never touch the same file
region. **Claim your whole lane; mark each item `WIP:<agent>` before you start it.**

| Lane | Agent | Domain (why these don't collide) | Items (priority order) | Effort |
|------|-------|----------------------------------|------------------------|--------|
| **A — Combat & magic core** | **Agent 1** | `index.html` damage/kill/cast math: `applyHit`, `killMonster`, `executeSpell`, `drinkPotion` | **#27** dual-wield double-loot 🔴HIGH · **#29** weapon buffs leak onto spell dmg · **#30** focus-mana not refunded on fizzle · **#28** potion negative-clamp | 3×S + 1×S–M · **✅ done (this PR)** |
| **B — Server, netcode & deploy** | **Agent 2** | `server/dereth_server.py` handlers, client↔server sync, `deploy/Dockerfile` | **#24** remote wield never syncs online · **#23** silent `delete_char` refusal · **#25** `houseboot` has no server handler · **#26** Docker omits runtime data files | 3×S + 1×S–M |
| **C — Assets, extractors & code hygiene** | **Agent 3** | Python extractors + `index.html` **non-combat** cleanup only | **#21** 292 items → unexported icon DIDs (extractor) · **#20** duplicate `craftResultItem` · **#22** 20 dead functions (check `ccRandomize`/`resetUIPos` first) | 3×S |

**Coordination notes (read before editing):**
- **Lane A: do #27 and #29 as ONE coordinated edit** — both change the *head* of `applyHit`
  (`index.html` ~10083). #27 adds the `if(m.hp<=0) return;` dead-guard; #29 moves the weapon-only
  offense buffs (`buffMightT`, oil `itemBuff("dmg")`, Heart-Seeker `gearFx.crit`) inside the `opts.phys`
  branch. Keep Damage Rating / Life Vulnerability / Aetheria surges universal (design call). Start with
  **#27 (HIGH, player-reachable, economy-affecting)**.
- **Lane C also edits `index.html`** but only the craft (`craftResultItem` ~14355/14528) and the 20 dead
  top-level functions — **never** the combat/magic functions Lane A owns. Different functions → git
  merges cleanly; still, rebase on Lane A's merge before finishing to stay tidy.
- **Lane B is mostly a separate file** (`dereth_server.py` + `Dockerfile`); only **#24** also touches
  `index.html` (the input-tick/snapshot/`remoteApp` net path) — a net-sync region Lanes A & C don't use.

<details><summary>Round-1 build lanes (all ✅ complete — historical)</summary>

| Lane | Agent | Domain | Items | State |
|------|-------|--------|-------|-------|
| A — Combat & item data | Agent 1 | combat math + item/loot tables | #1 Damage/Crit Rating + Crushing Blow · #4 content top-offs | ✅ complete |
| B — Avatar, clothing & rendering | Agent 2 | avatar rig, mesh/clothing render | #2 hats · #3 dye subpalettes · #12 dungeon lighting · #13 shield arm + doorway/window · #18 Academy staff leak | ✅ complete |
| C — UI/HUD, town & tooling | Agent 3 | HUD/CSS, town placement, extractors | #5 path_blocked · #6 0x21 layout decode · #10 window chrome · #11 paperdoll · #19 shop wield gates | ✅ complete |

</details>

**Not agent work:** items **#7–#9** are the **excluded Agent-1 terrain re-extraction lane** (blocked —
leave alone). Items **#14–#17** are **design decisions** already resolved (see the status block up top).
On-hardware eyeball passes (#10 look, #13 window/doorway) await a real-GPU session.

---

## 1 · Buildable now (present data & tools)

| # | Lane | Item | What's undone | Size | Status |
|---|------|------|---------------|------|--------|
| 1 | A | **Damage Rating / Crit-Damage Rating / Crushing Blow** ✅ | Sneak Attack, Recklessness & Dirty Fighting shipped; the DR/CDR multipliers, Crushing Blow, and the rear-attack **+20 DR** are not (only a Luminance "Aura of Valor" damage-rating buff and a mace *blurb* exist). *(auth-gaps Cb5)* | S–M | **done** |
| 2 | B | **Hats as real part-16 head-mesh swaps** ✅ | Cowl/cap/qafiya/turban/fez/kasa are still procedural cloth props (`felt()`/coils), not real head-model swaps. Cosmetic. *(consolidated Tier4)* | S–M | **done** |
| 3 | B | **Dye subpalettes on base clothing** ✅ | `acclothing.json` subpalettes aren't applied to base garments — no `subPalette`/`dyeSub` wiring. *(consolidated Tier4)* | S | **done** |
| 4 | A | **Content top-offs** *(optional)* | ~23 more salvage materials, extra gems, hide variants. (Armor sets, named bosses, and the full creature roster are already present.) *(items/regions refs — MATERIALS catalog already exhaustive: ~89 mats incl. all AC gems/stones/cloths)* | S | **done** |
| 5 | C | **`path_blocked` interior-packing residual** ✅ | ~~Entry centreline grazes.~~ `tbCutDoorway` clears a player-width corridor inward of every cut door; 56-town in-engine re-audit: interior wall-locks 14→0, pass 95.5%→97.5%, no regressions. *(building-entry-audit; PR #137)* | S | **done** |
| 6 | C | **`0x21` StringTable UI-layout decode** ✅ | ~~Undocumented.~~ Framing reversed (layoutId/800×600 header, element declaration+body pairs, five-u32 x/y/w/h/z geometry + 0x06 texture refs); `tools/ac_layout_export.py` → `assets/aclayouts.json` (101 layouts, 2,901 elements, 76% validated geometry). *(PR #137)* | follow-up | **done** |
| 18 | B | **Academy staff leak on death / generic exit** *(bug, medium)* | Dying inside the Training Academy interior (or any plain-`exitDungeon` path) skips `exitAcademyHall`, leaking the 8 interior staff NPCs + their obstacle entries into the overworld at world-origin coords; `curDungeon` also stays set. Self-heals only on hall re-entry. Fix: call `_clearAcademyNpcs()` from `exitDungeon` when `curDungeon.academy` (and null `curDungeon`). *(playtest #23 — FIXED in PR: exitDungeon strikes the staff on every exit path + nulls curDungeon; verified: die-inside → 0 leaks/0 origin obstacles, re-entry idempotent, chain/regular exits unaffected)* | S | **done** |
| 19 | C | **Shop rows omit wield reqs & item-magic info** ✅ | ~~Blind gear buys.~~ Shop rows render wield gates (red when unmet), cantrip tier, resolved retail spells, spellcraft; unwieldable stock gets an amber ⚠ Buy button + "needs …" tooltip. *(playtest 2026-07-06 #24; PR #137)* | S | **done** |
| 20 | C | **TestSystemA TSA-1 — duplicate `craftResultItem` (merge artifact)** | index.html declares it TWICE (≈14355 and ≈14528) with different bodies; hoisting silently runs the newer (dye-pot-aware) one, the older is dead and invites divergence. Fix: delete the first body. *(TestSystemA run 2)* | S | open |
| 21 | C | **TestSystemA TSA-2 — 292 items reference unexported icon DIDs** | 292/4,338 acitems (incl. torch/wand/sceptre/branith's staff) have icon DIDs with no PNG in acicons (3,961 exported); they fall to real-but-generic CATEGORY icons. Fix: re-run/extend `ac_icon_export.py` for the missing DIDs (some are palette-variant ids needing a base-icon fallback). *(TestSystemA run 4)* | S | open |
| 22 | C | **TestSystemA TSA-3 — 20 dead top-level functions** | Never referenced from JS or HTML: `_acHeadTex acClock addFeature addTattoo armorTypeOf bestMeleeEff bestMeleeSkillValue buildCrystal ccRandomize hsType isCapitalName kcInspectHook primaryAttack resetUIPos rollCantrips rollCrit rollFurniture salvageValue spawnEmberEcho talkEmissary`. Delete — but FIRST check `ccRandomize` (lost creator Randomize button?) and `resetUIPos` (HUD-reset entry point) for small UX regressions. *(TestSystemA run 5)* | S | open |
| 23 | B | **TestSystemA TSA-4 — `delete_char` of the active slot is silently ignored** | dereth_server.py ≈1407: the (correct) refusal to delete the character you're playing sends NOTHING back; clients can't tell refusal from packet loss (and create_char auto-plays the new slot, so create→delete on one slot always hits it). Fix: send a system/play_err message in the refusal branch. *(TestSystemA run 6)* | S | open |
| 24 | B | **TestSystemA TSA-5 — remote players' wield never syncs online** | The client input tick carries no weapon/offhand fields, the snapshot relays none, and `remoteApp`/`reconcileRemotes` dress remotes without equipment — other players always render bare-handed/shield-less in multiplayer. Fix: add wield kinds to the input tick + snapshot and hand them to the remote-avatar dresser. *(TestSystemA run 8)* | S–M | open |
| 25 | B | **TestSystemA TSA-6 — client sends `houseboot`, server has no handler** | index.html:9624 `/house boot <name>` does `netSend({t:"houseboot",...})` online, but dereth_server.py has NO `houseboot` case — the message is silently dropped, so booting a guest from your dwelling has no multiplayer effect (the local log lies "X is booted"). Net-symmetry audit found this is the ONLY client→server type without a server handler. Fix: add a `houseboot` handler (evict the named guest from the house instance) or make the command offline-only. *(TestSystemA run 11)* | S | open |
| 26 | B | **TestSystemA TSA-7 — Docker image omits the server's runtime data files** | `deploy/Dockerfile` COPYs only `server/dereth_server.py`, but the server reads FOUR sibling/relative files at boot: `admin_kilmer.json` (admin char seed) and `../assets/{acrewards,acitems,acspellstats}.json` (the retail item/spell catalog mirror). All have graceful try/except fallbacks so the container still boots — but a **Docker-deployed server silently loses the retail loot catalog**, so shared/online loot falls back to the simplified generator (real retail gear stats gone) and the Admin account gets no seeded character. The systemd path (`git clone` whole repo → /opt/dereth) is unaffected. Fix: COPY `server/admin_kilmer.json` + the needed `assets/*.json` into the image (or document the degraded mode). *(TestSystemA run 14)* | S | open |
| 27 | A | **TestSystemB TSB-1 — dual-wield double-kill / double-loot (HIGH, player-reachable)** | `applyHit` (index.html ~10083) has no `if(m.hp<=0) return` guard and `killMonster` (~10179) has no already-dead re-entry guard. `meleeAttack` fires `applyHit(m,…)` **twice** per dual-wield swing (main ~15553 + off-hand ~15555) with no hp check between, so a **killing main strike lets the off-hand strike re-kill the corpse** → doubled kill count, XP, gold, and a **full second loot roll**. Confirmed via the real dual-wield path (`tsb_dw.js`): 60 one-shot swings → **97 kills (37 excess), 62% double-counted, 2–6 extra drops each** (~1.6× loot/XP on killing blows). **Blast radius = dual-wield only** — AoE/ring/projectile/splash/wall re-query `monsters[]` and splice the corpse, so a localized fix resolves it. FIXED — `if(!m||m.hp<=0) return;` atop `applyHit`. Verified live: killing blow = 1 kill, off-hand follow-up on the corpse = **0 extra kills/XP/loot**. *(TestSystemB pass 8–9)* | S | **done** |
| 28 | A | **TestSystemB TSB-2 — `drinkPotion` doesn't clamp a negative heal amount (LOW)** ✅ | ~~a negative `amt` reduces hp.~~ FIXED — `amt=Math.max(0,+amt||0)` at the top of `drinkPotion`. Verified live: −50 potion leaves hp unchanged, positive heals still work. *(TestSystemB pass 10)* | S | **done** |