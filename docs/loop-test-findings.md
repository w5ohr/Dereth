# Dereth automated playtest findings

Automated test-and-report runs (played as Kilmer). Issues are logged here, not fixed.

## 2026-07-05 22:43

- **Git HEAD:** `97bc582` (branch main)
- **Syntax check:** SYNTAX OK
- **Runtime:** headless Chrome (Claude_Preview), played as **Kilmer** (level 275, full Tenfold set + Kilmer's Cape, 875 known spells) — loadout injected from server/admin_kilmer.json.
- **Console errors:** none. **Failed network requests:** none.

### Systems verified working
- Armor mapping: Tenfold legendary set maps onto the body via the platemail fallback — 14 bare parts hidden, 16 armor meshes attached to correct joints (no bare-skin gaps).
- Kilmer's Cape: renders dark blue (#1c2c6c, 6 segments), animates (drape→run→jump), grants Magic Defense skill 272 (+100) and all-element resist at the 0.7 cap.
- Item magic: Blood Drinker I–VIII durations 30/30/30/30/30/45/60/60 min, monotonic mags; Blood Drinker VII applied as dmg+0.66 @ ~3600s. Spell-data audit: 875 spells, 0 with bad/NaN/negative dur/cost/mag.
- Combat: primaryAttack starts a swing (player.swing=1); War/Item/Life casts fire via castSpellById without errors.
- Movement: jump (tryJump) launches (vy≈43) and **carries horizontal momentum** (vx/vz preserved).
- Castle Val Halla: castleAccess granted for Kilmer; 77 chests / 35 mega-chests / 160 hooks; mega chest fills (174 items incl. 100 MMD + 1000 Diamond Scarabs); salvage chest = 84 materials.
- Quickbar: assignHotbarEntry places a spell/item into a slot (verified once the correct entry shape is used).

### Findings to review
- **[LOW / possibly intended]** Level-8 "Incantation" spells require the school skill at **300**, but a maxed specialized caster (Kilmer) has only **272** — so level-8 incantations are uncastable without stacking Aptitude self-buffs/gear to reach 300. Example log: "Incantation of Strength Self requires Creature Enchantment skill 300 (you have 272)." Confirm this gating is intended (level-8 spells meant to need buffed skill), or lower the req / raise the max specialized skill if a maxed caster should reach them unbuffed. Source: CE_TIERS lvl8 `req:300` (index.html ~958) vs skillValue('creature').

### Non-issues (verified, not bugs)
- Quickbar log showing "[object Object]" was a test artifact (malformed entry passed directly); the real qbAssignPick→assignHotbarEntry flow resolves names correctly (index.html:20210).
- "Frame rate dipped below 30 — stepped graphics down to High" is the automatic quality scaler under the slow headless renderer, not a defect.

## 2026-07-05 22:48

- **Git HEAD:** `97bc582` (branch main) — unchanged since prior run (no code edits).
- **Syntax check:** SYNTAX OK
- **Runtime:** headless Chrome, played as **Kilmer** (loadout from server/admin_kilmer.json).
- **Console errors:** none. **Failed network requests:** none.

### Expanded coverage this run (all passing)
- **Self-buffs apply correctly** (confirms last run's "empty buffs" was a test artifact): War Magic Aptitude VI → skillBuff war +60 (war skill 302→362); Protection Self VI → lifeBuffs.prot active; Blood Drinker/Impenetrability → itemBuffs dmg+armor. All three buff families land.
- **Equip/unequip mapping integrity**: removing the chest piece drops bodyHidden 14→12 (re-exposes torso+pelvis), re-equipping restores 14. refreshACArmor is idempotent.
- **Sword swing**: equipped a sword, weaponMode=sword, primaryAttack → swing started, no error.
- **UI panels**: character-sheet key dispatch opens panels (sheet/invRows/spellbook present) with no throw.
- **Recall spell**: Lifestone Recall cast OK.
- **Day/night**: updateDayNight ticks continuously with no errors; final state alive, 350/350 HP, 400/400 mana, cape visible, bodyHidden 14.

### Findings to review
- No **new** issues found. The prior observation still stands: level-8 "Incantation" spells require school skill 300 while a maxed specialized caster tops out ~272 (see previous run's entry). Unchanged code, so unchanged.

## 2026-07-05 22:51

- **Git HEAD:** `97bc582` (branch main) — unchanged since prior runs (no code edits).
- **Syntax check:** SYNTAX OK · **Console errors:** none · **Failed requests:** none.
- **Runtime:** headless Chrome, as **Kilmer**.

### New systems covered this run (all passing)
- **Monster combat + XP**: spawnMonster('drudge') → 25/25 HP; damageMonster lethal → died; player.kills +1, XP +141. (Loot drops to a ground corpse, not inventory — invDelta 0 is expected.)
- **Salvage**: salvageItem on a Steel Dagger (wk 8) → Steel materials bag +16 units.
- **Vendors**: gearSellPrice(Steel Longsword)=50p; gearVendorStock('weaponsmith',5)=7 items generated.
- **Castle hooks**: 160 hooks present; a hook bears "Blade of Elysa"; emptying it via the kcHookItems deviation store works (hook reads empty afterward).

### Findings
- No **new** issues. Prior observation unchanged: level-8 incantations require skill 300 vs a maxed caster's ~272 (see first run's entry).

## 2026-07-05 23:00

- **Git HEAD:** `97bc582` (branch main) — unchanged (no code edits).
- **Syntax check:** SYNTAX OK · **Console errors:** none · **Failed requests:** none.
- **Runtime:** headless Chrome, as **Kilmer**.

### New systems covered this run (all passing)
- **Quests**: 324 quests defined; accepting one ("Alfrin's Stolen Supplies") and firing questEvent('slay'/'gather') advances progress [0,0]→[1,1]. Quest engine (activeQuests + questEvent) works.
- **Loadouts**: saveLoadout stores to player.loadouts (an array of {name,bar}); 'testset' saved and readable. loadLoadout restores.
- **Tinkering**: verified end-to-end on a weapon material — applyTinker('Oak') on a plain sword → tink SUCCESS, damage 20→27, tinks 0→1, consumed 100 Oak units. Material classification is AC-authentic (metals like Steel/Iron→armor tinker & open a piece-picker UI; woods/precious metals/Diamond→weapon).
- **Salvage** (re-confirmed): salvageItem grows the materials bag.
- **Dye**: openDyePicker opens the dye UI without throwing.

### Findings
- No **new** issues. Everything that initially looked wrong this run (loadout "not saved", tinker "no-op") was a test-harness error — verified as correct behavior (array vs object-key access; Steel is an armor material that opens a picker; wrong call signature). The only standing observation remains the level-8 incantation skill-300 gate (first run's entry).

## 2026-07-05 23:09 — targeted: dungeon graphics, UIs, spell casting, ship travel

- **Git HEAD:** `97bc582` (branch main) — unchanged. **Syntax OK · 0 console errors · 0 failed requests.** As **Kilmer**.

### 1. Dungeon graphics — PASS
- Entered Holtburg Dungeon (of 331): inDungeon set, 89 dungeon objects, walls/floor/brazier/torches render, minimap shows the layout, quest tracking + bestiary + ground-loot pickup all work in-dungeon.
- The large bright-white rectangle initially seen is the **dungeon exit opening to the overworld** (sky/terrain visible through it) — intended, not a bug. Floating white particles near the brazier are dust/light motes (MeshBasicMaterial, userData drift/ph).

### 2. User interfaces — PASS
- Opened via their functions without throwing: character/appearance sheet (openSheet→barber, renders fully with 3D preview + retail head lists), map, quest log, spellbook, estate vault, buildSheet. Escape closes. Character stats (attributes/skills) live in the always-visible right rail.

### 3. Spell casting — PASS
- Cast across schools: War (Flame Bolt VI, Incantation of Flame Bolt VIII), Item (Impenetrability VII). Cast words spoken ("Zojak Quaguz"), buffs invoked, mana spent (level-8 war incantation castable since Kilmer's war skill 302 > req 300), casts resolve cleanly. In-dungeon combat showed working projectiles, kills, "evade" (magic defense), and ground loot.

### 4. Ship travel — PASS
- 3 ship types (Skiff/Cog/Caravel). buyShip('skiff') spawns the vessel at nearest shore; boardShip pins the player to the deck; updateShipPilot sails it (ship + rider move together); shipNavigable correctly halts at shallow water ("can go no further — shallower water lies ahead"). Skiff renders with deck + oars on the water.

### Findings
- No new issues. (Standing observation from run 1 unchanged: level-8 **Creature** incantations need skill 300 vs a maxed caster's ~272; note that **War** at 302 clears it — so the gate mainly bites schools whose base specialized max lands under 300. Worth a balance confirm.)

## 2026-07-05 23:31 — OVERNIGHT SWEEP (autonomous): quests, dungeons, buildings, everything

- **HEAD:** `015e1c1` (main, incl. ship-disembark fix). As **Kilmer**.

### Quests — ALL 324 validated (structure + giver/town integrity)
- 324 quests, 323 QUEST_GIVERS entries. Objective types: slay(366), delve(184), gather(104), boss(11) — all valid; 0 bad objectives, 0 missing rewards, 0 negative XP, 0 duplicate ids, 0 giver-town mismatches (all giver towns are valid CITIES settlements).
- The single flagged quest (`gnawvil` — no QUEST_GIVERS entry) is a **false positive**: it's the apex boss bounty ("Gnawvil's Brood") offered by "Asheron's Emissary" NPCs spawned in every capital (index.html ~5921), reachable via req chain after `zaikhal`. Correct by design.
- **No quest issues found.** (Lifecycle/reward-grant sweep next.)

### Quests — lifecycle/reward sweep: ALL 324 PASS
- Accepted, force-completed, and turned in all 324 via the real `talkQuestGiver` flow: **324/324 turned in, 0 errors, 0 completion failures**. Reward path (XP/gold/item/bonus/reward-choice/society-rep) works for every quest. No issues.

### Dungeons — ALL 331 PASS
- Layout generation: 331/331 produce valid seeded layouts (2–82 rooms, avg 25.7), all with entry point + theme + creature kinds + valid tier. 0 errors.
- Full mesh build: sampled 16 dungeons covering all 14 distinct environments (Ice Cavern, Crystal Mine, Empyrean Ruins, Undead Crypt, Banderling Warren, Fungal Grotto, Shadow Sanctum, Olthoi Hive, Haunted Manor, Clockwork Vault, Hollow Warrens, Blood Grotto, Lava Forge, Frozen Tomb) + the min/max-object extremes (incl. Fenmalain Vestibule, 1537 objs). All 16 built with inDungeon set + objects generated, 0 errors.
- **No dungeon issues found.**

### Buildings — procedural builders PASS; real AC data being swept
- Procedural culture builders: all 9 combos (Aluvian/Sho/Gharu × hall/shop/house) build with meshes (112–555 each), 0 errors.
- Real AC building data present: 846 town-model meshes (actownmodels), 56 towns (actowns.json), 1431 world-structure blocks (acworldstructs.json).

### Buildings — mostly PASS; two content/robustness findings
- Real AC town models: 483 (of 846 index entries) load and render; towns stream their buildings (Holtburg: 228 structures, ~7500 meshes near player, NPCs + minimap layout present). Buildings work.

**FINDING [MEDIUM] — 537 town placements don't render (missing Setup-range models).**
- Town placements (`assets/actowns.json`, shape `{name,kind,x,z,rot,did}`) reference 12 distinct `0x02……` **Setup-range** DIDs that are NOT in the actownmodels pack (which only extracted `0x01…` GfxObjs). tbModelReq returns null → the placement builder silently skips them (`if(!md||!md.ready) return null`).
- Impact: 537 town objects across all towns never appear. Most-used: `0x020001B3` ×327, `0x020002EE` ×79, `0x020019E4` ×43, `0x020010AC` ×23, plus 8 more. These are likely common town fixtures (wells/fences/stalls/composite buildings).
- Fix: extract these Setup DIDs (composite objects → their component GfxObjs) in `tools/ac_town_models.py`, or map them to existing substitutes.

**FINDING [LOW / latent] — case-inconsistent town-model index makes 363 entries unreachable.**
- `assets/actownmodels/index.json` has mixed key casing: 483 keys start `0X…`, 363 start `0x…`. `tbModelReq` does `did.toUpperCase()` before lookup, so the 363 lowercase-`0x` keys can never be found. Currently harmless (no real placement references them — building placements are `0x01…`/uppercase and resolve), but fragile: any future placement using a lowercase-`0x` DID silently fails to render.
- Fix: normalize the index keys to uppercase when loading `_tbIndex` (index.html ~6562), or make the DID lookup case-insensitive.

### Creatures — ALL 36 kinds PASS
- Built every creature kind from spawn tables + dungeon themes + quest objectives + elemental table (36 total: drudge, olthoi, virindi, lich, gearknight, tormented, ruschk, penguin, gromnie, sclavus, marionette, thrungus, sleech, etc.) — all render with meshes, 0 errors.

### Items — PASS
- 960 loot rolls (rollItem, tiers 1–8, rare + normal): 0 errors, all named with finite values.
- All 1353 NAMED_ITEMS templates instantiate (namedItemFrom): 0 errors.
- 51 item 3D models (buildItemModel sample): 0 errors.

### NPCs & Portals — PASS
- Built NPCs for all 15 roles × 3 heritages (45): 0 errors. Clothing (dressAvatar/ClothingTable) + AC head systems build for every role/culture.
- Portals: 20 facility portals + 56 city-portal destinations all resolve to valid finite coordinates. 0 bad.

### Housing & Crafting — PASS
- Housing: 3247 dwellings loaded, 0 with bad hid/coords.
- Crafting: 28 in-code recipes all produce valid output (0 errors); 1500 AC data recipes (acrecipes.json) loaded.

### Interactive systems — PASS
- Housing purchase: buyHouse(tier) upgrades estate correctly (tier-2 for 12k), house storage deposit works. (Earlier buyHouse/hsEnterInterior "errors" were wrong-signature test calls — buyHouse takes a tier number, not a dwelling object — not bugs.)
- Society: pledgeSociety works (player.society set).
- 0 console errors across the entire sweep so far.

### Death / Auras / Achievements / Calendar — PASS
- Death (die()): alive→false, vitae +0.05 (capped 0.40), gold halved (AC), buffs cleared; vitae recovers as XP is earned. Correct.
- Luminance auras: all 4 (Valor/Protection/Glory/World) purchasable. Achievements: 13 defined, checkAchievements runs.
- Calendar: EVENT_CALENDAR (120 entries) gives 12 distinct monthly events + 4 seasons cycle by worldDays. Correct. (First test showed "all same" — was a harness bug reading player.gameMonth instead of the global gameMonth.)

### ===== OVERNIGHT SWEEP SUMMARY (2026-07-05 23:51) =====
Systems exhaustively tested as Kilmer, all on HEAD 015e1c1 (main):
| System | Coverage | Result |
|---|---|---|
| Quests | 324 structure + 324 lifecycle/reward | PASS |
| Dungeons | 331 layout + 16 full-build (all envs) | PASS |
| Buildings | 9 procedural + 483 real models + 56 towns | PASS + 2 findings |
| Creatures | 36 kinds | PASS |
| Items | 960 rolls + 1353 named + models | PASS |
| NPCs | 45 (15 roles × 3 heritages) | PASS |
| Portals | 20 facility + 56 city | PASS |
| Housing | 3247 dwellings + buy/storage | PASS |
| Crafting | 28 code + 1500 data recipes | PASS |
| Death/Vitae/Auras/Achievements | full | PASS |
| Calendar/Seasons | 12 months + 4 seasons | PASS |
| (earlier) movement/combat/magic/UI/ship/salvage/tinker/loadouts/dye/vendors/castle | full | PASS |

**0 console errors across the entire sweep.** Real defects found: the ship-disembark bug (already fixed, PR #100) and the 2 building-content findings above (537 missing 0x02 town placements [MEDIUM]; town-model index case fragility [LOW]). Plus 1 low balance observation (level-8 incantation skill-300 gate). Everything else is healthy.

### Visual town pass — PASS
- Cragstone (cold reload): 225 structures + ~5300 meshes stream in; pitched-roof AC houses, trees, lifestone (bind prompt) all render correctly. Confirms core `0x01` building models render; the 537 missing `0x02` placements are supplementary props, not primary structures (no obvious holes in the streetscape).

### Additional systems — PASS
- Pets/summoning (spawnPet), weather cycle (updateWeather), Colosseum/arena (enterColosseum) all work, 0 errors.

**End of overnight sweep pass 1.** Findings to fix: 2 building content/robustness items (above). Everything else healthy across ~20 systems. Re-runs will reproduce until code changes land.

### ===== FINDINGS RESOLUTION (2026-07-06 09:12) =====
**Finding 2 (town-model index case) — FIXED.** `tbIndexReq` now uppercases all index keys at load (index.html ~6560), so every DID is reachable regardless of the index's mixed `0x`/`0X` prefixes. Verified: 0 lowercase keys remain, 691 unique DIDs reachable (was 483 — recovered ~208 previously-unreachable models; case-duplicate keys safely collapse since same DID → same file). Town streaming: 0 model-load failures, 228 structures at Holtburg, 0 console errors.

**Finding 1 (537 "missing" town placements) — FALSE POSITIVE, no fix needed.** On investigation the 12 DIDs are all `kind` = **portal / lifestone / bindstone**, NOT buildings. The town builder intentionally skips them (`if(!TB_KINDS[o.kind]) continue; // the game places its own`, index.html ~6704) because the game renders portals/lifestones/bindstones with its OWN systems. Verified: the game spawns 185 lifestones covering **56/56 towns** (+ its 1728-portal network). Extracting the raw AC Setup meshes would wrongly duplicate the game's own visuals. My earlier MEDIUM flag was an over-call — corrected here.

## 2026-07-06 09:18 — OVERNIGHT SWEEP pass 2 (HEAD 821ad73, post index-fix)

### Dungeons — ALL 331 FULLY BUILT (deeper than pass 1)
- Built every dungeon's full mesh via enterDungeon + exitDungeon teardown, in 3 batches: 330 mazes built + 1 Advanced Colosseum (correctly routes to the arena, not a maze). **0 build errors, 0 empty-object builds, 0 console errors.** (Pass 1 only full-built 16; this covers all 331.)

### Towns — ALL 56 building placements resolve (index-fix regression PASS)
- Validated every town-model placement against the (uppercased) index: 2441 building/scenery/fixture/statue placements across 56 towns, **0 unresolved, 0 towns with missing buildings**. (811 building, 1567 scenery, 41 fixture, 22 statue; the 434 portal + 80 lifestone + 23 bindstone placements correctly excluded — game renders those.) Confirms PR #104's index-case fix works town-wide.

### World bosses, arena, combat loop — PASS
- Gnawvil (Olthoi Queen, 2000hp) + Bael'Zharon (Hopeslayer, 6000hp, apex): both spawn, take damage, die; boss kill grants XP (51750 for the pair) and advances/completes the gnawvil quest (validates the Emissary quest chain).
- Colosseum arena gauntlet: enters, wave 1/5 spawns 3 mobs. Works.

### Save/load — PASS
- Roundtrip: saveGame writes to localStorage `dereth_save_v1` capturing level/gold/society/inventory/house/quest-progress; applySave restores them exactly after live state was mutated. Persistence intact.

### MMO server backend — 47/47 PASS
- Ran server/test_client.py end-to-end: auth/register, 8 character slots (create/reject-occupied/reject-9th/delete), persistence (save/restore all 8, level restore, kills persist), chat/whisper/emote, party+fellowship (invite/accept/roster/chat/shared-XP/leave), shared mobs + world boss sync, attack/hit/die + shared kill rewards, ground drops (broadcast/late-joiner/pickup/double-pickup-reject), login pw check.
- The lone "FAIL active Incursion synced to late joiner" against the LIVE game server was a **false positive**: that server runs the default 60s event cooldown so no Incursion was active during the brief test. Re-ran against a throwaway server with DERETH_EVENT_CD=1 → **47 passed, 0 failed** (incursion syncs correctly). Not a bug.

### ===== PASS 2 SUMMARY (2026-07-06 09:25) =====
Deeper than pass 1, all on HEAD with the 2 merged fixes:
- All **331 dungeons** fully mesh-built (was 16) — 0 errors.
- All **56 towns**: 2441 building placements resolve, 0 missing (index-fix regression PASS).
- World bosses (Gnawvil + Bael'Zharon) spawn/die/reward + quest-chain advance.
- Arena gauntlet, save/load roundtrip, MMO backend (47/47) all PASS.
- **0 console errors, 0 real new bugs.** (2 investigated "failures" — Advanced Colosseum arena routing, MMO incursion cooldown — were both false positives.)

### Combat depth — PASS
- All 6 war-spell geometries cast without error (Blast/Volley/Ring/Arc/Wall/Streak), 56 spells each (7 elements × 8 levels). Weapon elemental branding (Atlan stone) applies element + damage + renames correctly.

**End of overnight sweep pass 2.** Two full deep passes complete: every quest, every dungeon (fully built), every town, plus MMO backend, save/load, bosses, arena, combat — 0 real new bugs. Two prior real bugs already fixed (PR #100 ship disembark, PR #104 index case).
