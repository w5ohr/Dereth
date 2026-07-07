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
| **A — Combat & magic core** ✅ **done** | **Agent 1** | `index.html` damage/kill/cast math: `applyHit`, `killMonster`, `executeSpell`, `drinkPotion` | ~~**#27** dual-wield double-loot 🔴HIGH · **#29** weapon buffs leak onto spell dmg · **#30** focus-mana not refunded on fizzle · **#28** potion negative-clamp~~ — all merged (PR #155) | 3×S + 1×S–M |
| **B — Server, netcode & deploy** ✅ **done** | **Agent 1** | `server/dereth_server.py` handlers, client↔server sync, `deploy/Dockerfile` | ~~**#24** remote wield sync · **#23** delete_char refusal · **#25** houseboot handler · **#26** Docker data files~~ — all merged | 3×S + 1×S–M |
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
| 20 | C | **TestSystemA TSA-1 — duplicate `craftResultItem` (merge artifact)** **✅ FIXED (lane C): deleted the dead first body (was index.html ~14363); the dye-pot-aware `craftResultItem(nm)` is now the sole definition. jsc clean, boots clean.**| index.html declares it TWICE (≈14355 and ≈14528) with different bodies; hoisting silently runs the newer (dye-pot-aware) one, the older is dead and invites divergence. Fix: delete the first body. *(TestSystemA run 2)* | S | **done** |
| 21 | C | **TestSystemA TSA-2 — 292 items reference unexported icon DIDs** **✅ FIXED (lane C): all 188 missing DIDs (→292 items) were IN portal.dat and DECODABLE — none needed the palette-variant fallback; the icon extractor's ItemType-flag selection just skipped these weenies (torches/wands/sceptres/staves/ingots/shafts…). Exported the 188 real 32×32 PNGs + merged into acicons/index.json (3960→4148); referenced-DIDs-missing 292→0. Made durable: `ac_icon_export.py` now unions in every DID referenced by acitems.json. Verified: PNGs serve 200 in-browser, real coloured content.**| 292/4,338 acitems (incl. torch/wand/sceptre/branith's staff) have icon DIDs with no PNG in acicons (3,961 exported); they fall to real-but-generic CATEGORY icons. Fix: re-run/extend `ac_icon_export.py` for the missing DIDs (some are palette-variant ids needing a base-icon fallback). *(TestSystemA run 4)* | S | **done** |
| 22 | C | **TestSystemA TSA-3 — 20 dead top-level functions** **✅ FIXED (lane C): all 20 removed (brace-matched); `ccRandomize`/`resetUIPos` confirmed to have NO orphaned UI button (no regression); `primaryAttack`/`salvageValue` live paths verified elsewhere. jsc clean, boots clean, 0 console errors.**| Never referenced from JS or HTML: `_acHeadTex acClock addFeature addTattoo armorTypeOf bestMeleeEff bestMeleeSkillValue buildCrystal ccRandomize hsType isCapitalName kcInspectHook primaryAttack resetUIPos rollCantrips rollCrit rollFurniture salvageValue spawnEmberEcho talkEmissary`. Delete — but FIRST check `ccRandomize` (lost creator Randomize button?) and `resetUIPos` (HUD-reset entry point) for small UX regressions. *(TestSystemA run 5)* | S | **done** |
| 23 | B | **TestSystemA TSA-4 — `delete_char` of the active slot is silently ignored** | dereth_server.py ≈1407: the (correct) refusal to delete the character you're playing sends NOTHING back; clients can't tell refusal from packet loss (and create_char auto-plays the new slot, so create→delete on one slot always hits it). Fix: send a system/play_err message in the refusal branch. *(TestSystemA run 6 · FIXED Lane B: refusal branch now replies `play_err` so the client distinguishes it from packet loss)* | S | done |
| 24 | B | **TestSystemA TSA-5 — remote players' wield never syncs online** | The client input tick carries no weapon/offhand fields, the snapshot relays none, and `remoteApp`/`reconcileRemotes` dress remotes without equipment — other players always render bare-handed/shield-less in multiplayer. Fix: add wield kinds to the input tick + snapshot and hand them to the remote-avatar dresser. *(TestSystemA run 8 · FIXED Lane B: input tick carries `wt`/`wmode`/`shield`, server stores + relays them in the snapshot, and `dressRemoteWield` toggles each remote's held-weapon/bow/shield meshes — verified end-to-end headless)* | S–M | done |
| 25 | B | **TestSystemA TSA-6 — client sends `houseboot`, server has no handler** | index.html:9624 `/house boot <name>` does `netSend({t:"houseboot",...})` online, but dereth_server.py has NO `houseboot` case — the message is silently dropped, so booting a guest from your dwelling has no multiplayer effect (the local log lies "X is booted"). Net-symmetry audit found this is the ONLY client→server type without a server handler. Fix: add a `houseboot` handler (evict the named guest from the house instance) or make the command offline-only. *(TestSystemA run 11 · FIXED Lane B: added a `houseboot` server handler — notifies the booted guest if online, else corrects the client's optimistic log)* | S | done |
| 26 | B | **TestSystemA TSA-7 — Docker image omits the server's runtime data files** | `deploy/Dockerfile` COPYs only `server/dereth_server.py`, but the server reads FOUR sibling/relative files at boot: `admin_kilmer.json` (admin char seed) and `../assets/{acrewards,acitems,acspellstats}.json` (the retail item/spell catalog mirror). All have graceful try/except fallbacks so the container still boots — but a **Docker-deployed server silently loses the retail loot catalog**, so shared/online loot falls back to the simplified generator (real retail gear stats gone) and the Admin account gets no seeded character. The systemd path (`git clone` whole repo → /opt/dereth) is unaffected. Fix: COPY `server/admin_kilmer.json` + the needed `assets/*.json` into the image (or document the degraded mode). *(TestSystemA run 14 · FIXED Lane B: Dockerfile now COPYs `server/admin_kilmer.json` + `assets/{acrewards,acitems,acspellstats}.json` into the image)* | S | done |
| 27 | A | **TestSystemB TSB-1 — dual-wield double-kill / double-loot (HIGH, player-reachable)** | `applyHit` (index.html ~10083) has no `if(m.hp<=0) return` guard and `killMonster` (~10179) has no already-dead re-entry guard. `meleeAttack` fires `applyHit(m,…)` **twice** per dual-wield swing (main ~15553 + off-hand ~15555) with no hp check between, so a **killing main strike lets the off-hand strike re-kill the corpse** → doubled kill count, XP, gold, and a **full second loot roll**. Confirmed via the real dual-wield path (`tsb_dw.js`): 60 one-shot swings → **97 kills (37 excess), 62% double-counted, 2–6 extra drops each** (~1.6× loot/XP on killing blows). **Blast radius = dual-wield only** — AoE/ring/projectile/splash/wall re-query `monsters[]` and splice the corpse, so a localized fix resolves it. **FIXED (Lane A, PR #155):** `if(!m||m.hp<=0) return;` atop `applyHit`. Verified live: killing blow = 1 kill, off-hand follow-up on the corpse = **0 extra kills/XP/loot**. *(TestSystemB pass 8–9)* | S | **done** |
| 28 | A | **TestSystemB TSB-2 — `drinkPotion` doesn't clamp a negative heal amount (LOW, not player-reachable)** | `player.hp = Math.min(player.mhp, player.hp + healScale(amt))` — a negative `amt` *reduces* hp. Every in-game potion call passes a positive constant, so this is a defensive/robustness gap, not a live bug. **FIXED (Lane A, PR #155):** `amt=Math.max(0,+amt||0)` atop `drinkPotion`. Verified live: −50 potion leaves hp unchanged, positive heals still work. *(TestSystemB pass 10)* | S | **done** |
| 29 | A | **TestSystemC TSC-1 — weapon-only offense buffs leak onto SPELL damage (3 sources, one root cause)** | `applyHit` (index.html:10084) applies the weapon-damage multiplier `if(player.buffMightT>0)base*=1.5;` **and** `base*=(1+itemBuff("dmg")+aetheriaBonus("dmg"))` **before** it branches on `opts.spell` (:10087) vs `opts.phys`. So three weapon-only sources boost war-magic spell damage: **(a)** weapon **oil** (`itemBuff("dmg")`, set at :14448) → runtime-proven a 1000 spell hit → **1120** with a +12% Honing Oil; **(b)** **Might elixir** (`buffMightT`, +50% *weapon* dmg) → spell 1000 → **1500**; **(c)** low-sev: **Heart Seeker** weapon crit enchant (`gearFx.crit`, from :790) feeds the crit line :10105 → raises **spell crit chance** (pinned-random spell 1000 → 2000/crit only with Heart Seeker). The correctly-gated sibling is `gearFx.dmg`/Blood Drinker at :10086 (behind `opts.phys`). **Defensive side audited CLEAN** — no mirror leak (Life Prot cuts both; fire bane cuts ONLY fire). **NOTE: touches the same `applyHit` head as #27 (TSB-1) — coordinate the two edits.** Fix: apply buffMightT×1.5, the oil portion of `itemBuff("dmg")`, and the Heart-Seeker portion of `gearFx.crit` inside the `opts.phys` path — but KEEP Damage Rating, Life Vulnerability, and the Aetheria "Destruction"/Fury surges universal (those write the same slots and may be intended all-damage; design call). **FIXED (Lane A, PR #155):** buffMightT×1.5 + Heart-Seeker `gearFx.crit` gated behind `opts.phys`; weapon oil split onto its own `oildmg` slot (also `opts.phys`-only) so it no longer shares the universal Aetheria-Destruction `dmg` slot. Damage Rating / Life Vulnerability / Aetheria surges kept universal per the design call. Verified live: spell min 100 / crit 0.15 vs phys min 180 / crit 0.44. *(TestSystemC passes 16–19)* | S–M | **done** |
| 30 | A | **TestSystemC TSC-2 — focus-mana battery not refunded on fizzle/abort (silent battery→pool transfer)** | In `executeSpell` (index.html:15128+), when `player.mn < cost` the H15 focus-mana battery covers the shortfall: :15152 does `player.focusMana -= need; focusPaid = need; player.mn = cost;` then :15159 `player.mn -= cost`. On a **fizzle** (:15169) or any **target-required abort** (the `player.mn += cost` refunds at :15184/15192/15201/15228/15246–15260) the FULL `cost` is refunded to `player.mn` but `player.focusMana` is **never restored**. Smoking gun: `focusPaid` is captured (:15152) but used NOWHERE, and there is NO `player.focusMana +=` anywhere in the file — the refund was intended but unwired. Runtime-proven: forced fizzle with battery engaged → personal mana **2→5** (+3), battery **1400→1397** (−3): total conserved but a free battery→pool transfer, violating the code's own "a fizzle consumes NO mana" comment (:15165). Low severity (mana-conserving, not infinite). Fix: on every fizzle/abort refund, restore the battery using the captured `focusPaid`, e.g. `if(focusPaid>0){ player.focusMana+=focusPaid; player.mn-=focusPaid; }`. **FIXED (Lane A, PR #155):** snapshot `mn`+`focusMana` before paying and restore **both** via `refundCast()` at the fizzle site and all ~15 no-target/abort returns (scarab reagent stays consumed by design). Verified live: forced fizzle with battery engaged now leaves both pool and battery unchanged. *(TestSystemC pass 21)* | S | **done** |
| 31 | A | **AgentC AC-1 — attribute gems raise attributes with NO cap (loot-farmable, balance/authenticity)** | `applyItem` (index.html:14435) handles the attribute gems with a bare `else if(player.attr[it.stat]!==undefined){ player.attr[it.stat]+=it.v; derive(); … }` — **no ceiling of any kind**. These gems (**Strength / Endurance / Coordination / Quickness Gem, Focusing Stone, Willpower Gem**) sit in the `ITEM_BASE` **loot** pool (index.html:11828–11831, `v:[1,1]`), so they roll from normal drops (confirmed in a 4,000-roll scan) and also drop as quest rewards — fully player-reachable via the Use action. Every OTHER attribute-raise path is capped: the XP path stops at `attrMaxRanks()` (~190 ranks, `attrCost`→Infinity), and **augmentation** attribute gems cap at +50 total innate (index.html:14351 returns `"keep"` at the cap). Only this loot-gem branch is unguarded, so a player can farm gems and inflate a core attribute without bound — cascading into skills, vitals (End→hp/stam, Self→mana), to-hit, and damage, and blowing past AC's 100-base attribute ceiling. **Runtime-proven:** applying 50 gems (×5) raised Strength 13→**263** (+250) and it **survives `derive()`** (no clamp). Severity low-moderate (permanent unbounded stat inflation; player-reachable but slow at +1/gem from loot; not a crash). **Fix:** gate the gem-apply branch like the others — e.g. refuse past a ceiling (`if(player.attr[it.stat]>=100){ log("…already at its natural peak"); return "keep"; }`) or track a gem-attribute total cap, mirroring the augment path's `attrTotal` guard. *(AgentC pass 3)* | S | open |
| 32 | A | **AgentC AC-2 — `playerHurt` has no NaN/non-finite input guard → NaN-HP soft-brick (network-reachable)** | `playerHurt(dmg,…)` (index.html ~14857) runs its evade/armor/mitigation math on the raw `dmg` with **no input clamp** — unlike `damageMonster`, which opens with `dmg=Math.max(0,+dmg||0)` (explicitly labeled "#16: guard NaN/negative damage — never … leave it at un-killable NaN HP"). The player side never got the twin guard. `playerHurt`'s internal `Math.max(1,…)` steps catch *negative* damage but **not** NaN (`Math.max(1,NaN)===NaN`), so a NaN/non-numeric `dmg` propagates straight to `player.hp`. **Reach (network, player-facing):** `onMobDmg(msg){ playerHurt(msg.amt, msg.x, msg.z); }` (index.html:25123) passes an **unvalidated** server `dmg`-message field in — so in online/server-authoritative combat a message with a missing / NaN / non-numeric `amt` NaNs the client's HP. Runtime-proven: `onMobDmg({t:"dmg"})` (no amt), `{amt:NaN}`, and `{amt:"oops"}` all set `player.hp = NaN` (only `{amt:-50}` is safely clamped). **Consequence = soft-brick:** NaN HP is **unhealable** (`Math.min(mhp, NaN+h) === NaN`) and **undyable** (`NaN<=0` is false, so the death check never fires) — recovery needs a save/reload. Severity low-moderate (needs a bad/malformed server message or edge-case local debuff state to trigger, but the consequence is a permanent stuck state; trivial fix; the monster side is already guarded so this is a clear omission). **Fix:** mirror `damageMonster` at the top of `playerHurt` — `dmg=+dmg; if(!isFinite(dmg)||dmg<=0) return;` — and/or validate `msg.amt` in `onMobDmg`. *(AgentC pass 5)* | S | open |
| 33 | A | **AgentC AC-3 — `onLoot` gold path corrupts `player.gold` to NaN (unvalidated server field; economy soft-brick)** | Same class as #32, in the loot netcode. `onLoot(msg)` (index.html:25032) does `if(msg.type==="gold"){ player.gold+=msg.amt; … }` with **no validation of `msg.amt`** — so a server `loot`-message of type `gold` with a missing / NaN / non-numeric `amt` sets `player.gold = NaN`. It's a clear omission because its two sibling handlers DO guard: `onCorpseLoot` gates on `msg.amt>0` (index.html:25038) and `onReward` gates on `if(msg.gold)` (index.html:25126) — both reject NaN — but `onLoot` guards neither. **Runtime-proven:** `onLoot({type:"gold"})` (no amt), `{amt:NaN}`, and `{amt:"oops"}` all NaN `player.gold`; only a numeric amt works (`{amt:50}`→+50). `onCorpseLoot({amt:NaN})` / `{amt:-500}` correctly leave gold unchanged. **Consequence = economy soft-brick:** NaN gold breaks buy/sell (`player.gold<cost` is `false` for NaN, and `NaN-cost` stays NaN), renders "NaN" in the HUD, and persists to the save. Player-reachable in online play (the server sends the loot-gold message). Severity low-moderate. **Related lower-likelihood edge:** `onReward` and `onLoot` also accept a *string* gold — `onReward({gold:"oops"})` produced the corrupted string `"1000oops"` (its truthy guard catches NaN but not strings). **Fix:** validate the amount in `onLoot` (and coerce/reject in the reward/loot handlers): `const amt=+msg.amt; if(isFinite(amt)&&amt>0){ player.gold+=amt; … }`. *(AgentC pass 7)* | S | open |
| 34 | A | **AgentC AC-4 — one malformed snapshot mob aborts the WHOLE snapshot tick (M3 multiplayer resilience)** | The snapshot handler runs all three reconcilers **unguarded on one line**: `if(m.t==="snapshot"){ reconcileRemotes(m.players||[]); reconcileMobs(m.mobs||[]); reconcileEvent(m.event||null); return; }` (index.html:24951). If `reconcileMobs` throws, `reconcileEvent` (and any later reconciler) **never runs that tick** — and since snapshots repeat, it throws every tick the bad entry persists, freezing all networked entity + event-beacon updates. Two reproducible throw sources: **(1) unknown mob kind** — `spawnSharedMob` (index.html:~25092) guards `const b=BESTIARY[sm.kind]||BESTIARY.drudge` but then passes the RAW `sm.kind` to `acCreatureMesh(sm.kind)`, which reads `.size` on the undefined bestiary entry → `TypeError: reading 'size'`; the fallback is incomplete. **(2) a `null` array entry** → `reconcileMobs`/`reconcileRemotes` do `seen[sm.id]` → `reading 'id' of null`. **Runtime-proven:** a snapshot with `mobs:[{kind:"__nope__",…}]` + a valid `event` → handler throws and `NET.event` is never set (event beacon never updates); `reconcileMobs([null])` throws on `.id`. (NaN/±Infinity coords do NOT throw — they slip through as NaN mob positions, a separate minor render concern.) Reach: server-authoritative multiplayer (M3, in active dev) — a version-skewed server sending a monster kind the client build lacks, or any malformed snapshot entry. Severity low-moderate (needs server malformation/skew, not normal play; but the consequence is a repeating client-side desync/freeze of the whole networked layer). **Fix:** (a) wrap each reconciler in its own try/catch at index.html:24951 so one can't abort the others; and (b) complete the spawn fallback — skip null entries and pass a known kind to `acCreatureMesh` when `BESTIARY[sm.kind]` is missing. *(AgentC pass 8)* | S | open |
| 35 | A | **AgentC AC-5 — secure-trade "done" is NOT idempotent → duplicate message dupes items + coin (economy-critical)** | The trade-`done` handler in `netHandle` (index.html:24937, the `m.act==="done"` branch) has **no idempotency guard**: it removes `TRADE.mine` from your inventory, then `for(const it of (m.give||[])) player.inv.push(it)` and `player.gold += (m.coin|0)`, then `closeTradeWin()` (which sets `TRADE.open=false` and empties `TRADE.mine`). A **second `done`** finds `TRADE.mine` already empty (so nothing more leaves you) but **re-adds `m.give` and re-credits `m.coin` again** — a clean item+gold **duplication**. **Runtime-proven:** legit `done` → your sword leaves, +1 gem, +100p (gold 1000→1100); a duplicate `done` → **+1 more gem (count 1→2) and +100p again (1100→1200)** while you give nothing. This is the highest-severity class (item/gold dupe = economy break). The guard state already EXISTS — `TRADE.open` is set false by `closeTradeWin` — it's just not checked. Reach: server/network sends a duplicate/replayed `done` (server bug, resync/replay, or malicious server) in online trade; unlike the NaN findings this doesn't need bad *values*, only a repeated *message*. Severity **moderate–high** (economy-critical consequence; needs a duplicate message to trigger; trivial fix). **Fix:** idempotency guard at the top of the `done` branch — `if(!TRADE.open) return;` — and/or a per-trade nonce/id the client accepts once. *(AgentC pass 9)* | S | open |

## 2 · Blocked — needs the excluded Agent-1 terrain re-extraction lane

*(Agent-1 terrain lane — not assigned to the 3 working agents.)*

| # | Item | What's undone | Size | Status |
|---|------|---------------|------|--------|
| 7 | **Authored landmarks + terrain barriers** | Named mountains that gate travel; terrain is currently procedural trig-noise. Needs the landblock heightmap. *(auth-gaps W12)* | L | blocked |
| 8 | **Irregular continent + real ocean rim** | The terrain-*shape* sliver of Named Regions — region names/lore/HUD already shipped; only the true coastline & island landmasses remain (underwater structures currently mitigated by `islandLift`). *(auth-gaps W8)* | L | blocked |
| 9 | **Geometry-driven *town* renderer** | Largely closed — dungeons render real EnvCell meshes and towns already stream real AC building meshes (`tbBuildMesh`). Only true EnvCell *town-layout* geometry remains, and it overlaps the A1 lane. *(ac-data-extraction-roadmap)* | follow-up | blocked |

## 3 · On-hardware — visual changes that can't be verified headless (SwiftShader); need a real-GPU eyeball

| # | Lane | Item | What's undone | Size | Status |
|---|------|------|---------------|------|--------|
| 10 | C | **Real UI window-frame chrome** ✅ | ~~9-slice on draggable panels.~~ `acChromeInit` composes the 9-slice sheet at boot and border-images all 11 draggable panels (graceful fallback). *(Corrected: `06001b14`/`06001343` are the Turbine/AC logos and `060011bb` a stone field — the real frame family is `06001920`/`21`/`22`.)* Final look: on-hardware eyeball. *(wrapup §7; PR #137)* | S–M | **done** (final look: on-hardware eyeball) |
| 11 | C | **Paperdoll panel layout** (PAGE-99 manual) ✅ | ~~Arrangement differs.~~ Held/Body/Adornment rails, Containers row (satchel + side packs), live Examine box on hover, persisted icon-rail toggle. *(ac-remaining-gaps A2; PR #137)* | M | **done** |
| 12 | B | **Dungeon lighting final tune** | Eyeball torch/ambient intensity live; nudge amb/hemi or the ×1.35 factor. Pass 2 already landed. *(REMAINING-WORK)* | S | done (user-tunable slider) |
| 13 | B | **Equipped-shield arm mount + doorway/window transparency** | Real shield *meshes* now show on drops/examine, but the equipped-avatar shield stays procedural (arm orientation), the opaque-doorway recess is a warm-glow card, and windows are still baked-opaque — all need on-hardware iteration. *(playtest #21)* | S each | done (shield mount verified structurally — real mesh engages on async load, procedural hides; window/doorway + arm-orientation look = on-hardware iteration w/ brightness knob) |

## 4 · Design decisions — a human product call, not code

| # | Item | The question |
|---|------|--------------|
| 14 | ✅ **Level-8 Incantation skill-300 gate — DECIDED: keep it (authentic).** | In retail AC, level-8 "Incantations" had a difficulty ≈ **350–400** — *higher* than an unbuffed specialized caster's cap (~290–300). Casting the top tier **required** stacking Item-Enchantment **Aptitude** self-buffs + gear **cantrips** ("buff up, then cast") — a core, intended part of AC endgame magic. The game's **300** gate vs a ~272 unbuffed max is therefore authentic *and gentler* than retail, and reachable via the existing `skillBuffs` (Aptitude), `gearSkill` (cantrips), Five Fold Path (+10) and Enlightenment — **not a dead end**. **No req change.** Added a UX nudge: high-tier cast-blocks (req ≥ 240) now tell you to buff/gear your skill to reach the incantation. |
| 15 | ✅ **Full component-casting formula — DECIDED: keep the soft model.** | Stays on the current scarab/taper reagent model; the authentic exact-component "wrong → fizzle" formula is intentionally *not* adopted (too punishing for a homage). No change. *(auth-gaps Mg2)* |
| 16 | ✅ **Vitals-formula purity — DECIDED: keep the current model.** | Confirmed players already raise Health/Stamina/Mana **directly with XP** (`player.vitals` on the character sheet — `vitalCost`/`xpUnspent`, added to max in `derive()`), *plus* the attribute-derived base *plus* the +level term. Since players have direct control, the pure End/2·End·Self change (which would force a full creature rebalance) is **not pursued**. No change. *(auth-gaps C3)* |
| 17 | ✅ **Timers — DECIDED: go retail. DONE.** | **PK oath → the authentic 3-day key** (`PK_LOCK_MS`=3d; messages show d/h via `fmtLockLeft`). **Quest lockouts already retail-tiered** (`questCdMs`: L45+ = 7-day weekly, L30+ = 20-hour daily — no change needed). **Mana stone reworked to the retail battery:** *release into gear* + *consume an enchanted item to bank its mana* (retail destroy-to-store) + a Dereth-only *draw from your own reserve* (amount = stone quality/`cap` × your Mana-Conversion rate) — via a new `openManaStone` action menu; legacy `charge` stones migrate. |

---

## Confirmed shipped (do NOT re-open — verified in code 2026-07-06)

Old lists still mark many of these open; they are done:

- **Onboarding & creation:** ToD staged creation wizard, starting gear + Welcome Letter + heritage
  clothing, post-Academy greeter chain (7k/9.3k+500p/12.5k + Pathwarden suit + +4% XP), **Training
  Academy tutorial-dungeon interior** (`buildAcademyHall` — Great Hall / Courtyard / Workshop, Sentry-gated).
- **Tier-1/2 flagships:** combine/tradeskill engine (1,500 recipes), Aetheria slots/levels/sets/surges,
  multi-level dungeons (`buildDungeonReal`, y-aware storeys, descend portals), weapon-skill tree,
  melee+missile to-hit contests, **per-element armor** (`ARMOR_MAT_RESIST`), **weapon damage types**
  (`dt` pierce/slash/bludgeon), loot item-spells + Spellcraft + item mana, tinker per-item caps/imbue-once,
  **Enlightenment gates** (L275 + Society Master + auras), full recall set (Tie/Recall/Secondary/Sending),
  fellowship cap-9 proportional XP, secure trade + coin.
- **Combat/magic depth:** per-body-part combat, ballistic projectiles + terrain collision, missile range
  falloff, per-swing variance, stamina scaling, burden/encumbrance, Mana Conversion/fizzle, Void DoT
  curses, wield/level/skill loot requirements, banes.
- **Social:** allegiance patron/vassal tree + XP pass-up + NPC vassals, monarchy + `/allegiance` chat,
  3-state PK + altars + PK-loot, corpse/item-loss death (+ shared corpses), society ranks/ribbons/Test
  quests, live Town Crier feed.
- **World:** region-aware weather, **level-gated overworld portals** (Facility Hub + frontier), **named
  regions** (`regionNameAt` + HUD + `/where`), ambient fauna, organic roads, drowning, portal storms,
  Colosseum/instanced events.
- **Content:** **Chess** (perft-validated engine + AI), book placement + library stands, **"A History of
  Dereth"** (Dereth runs its *own* live events — retail GameEventDefDB extraction intentionally dropped),
  starter quests aligned to retail, all armor sets + named bosses (Aerbax/Gaerlan/Martine) + creature roster,
  **the full 10-year Kilmer Saga incl. Years 7–10** (Year of Bone … Year of the Crown).
- **Extraction/assets:** high-res textures (1,224), non-PCM music, **shield & clothing item models**,
  server-side item/spell mirror, building/clothing GfxObjs wired, all 66 creature kinds, 873 retail titles,
  **NPCs on real AC bodies + heads** (`buildPerson` retired to fallback).
- **Item icons — 100% coverage (PR #143, Lane A):** every item (normal loot, named/epic weapons, quest
  rewards, spell scrolls, attunement stones) renders its real AC icon, never an emoji. `itemIconHTML`
  gained a category-type fallback (`_ICON_CAT_KW`/`catIconDID`/`itemCat`) after own-icon and name lookup;
  fixed the two-handed-weapon, scroll (`.scroll`/`.spellId`), and attribute-stone gaps. Verified 2091/2091
  by the `iconaudit` harness (named/epic 1353/1353, quest 226/226, 0 emoji).
- **Heads/UI:** WYSIWYG creator preview, explicit head-choice rows (creator + barber), face tone/AO,
  **female forehead-band fix**, barber restrictions, circular radar.
- **Ships:** ownable/boardable skiff/cog/caravel water travel.
- **Lane B (Agent 2, 2026-07-06):** authentic dye subpalettes on base clothing (tools/ac_clothing_dyes.py
  + acClothDyeTex canvas remaps — ~half of all NPC cloth meshes dyed) · real part-16 hats (tools/
  ac_hat_export.py, 9 hats ×m/f, head swapped beneath — retail bald-under-hat) · equipped shields show
  the RETAIL mesh on the forearm (plus the fix for aShield never being registered — the equipped shield
  had silently never displayed) · Settings "Dungeon brightness" slider (persisted, live-retunes the delve).

*Maintenance note: when an item here is completed, move it into "Confirmed shipped" with a one-line code
reference — keep this the single, honest, reconciled list. Do not resurrect the retired docs.*
