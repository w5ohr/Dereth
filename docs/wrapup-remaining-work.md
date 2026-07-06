# Dereth — Wrap-Up: Remaining Undone Work

> **⚠️ STATUS UPDATE (2026-07-06): several items in this document are now COMPLETE.**
> The four Tier-1 flagship features shipped in PR #124 — **combine/tradeskill engine** (Alchemy/
> Cooking/Fletching/Dyeing live via 1,500 acrecipes.json combines), **Aetheria** (slots L75/150/225,
> leveled drops, set sigils, combat surges), **multi-level dungeons** ("Ye Olde Stair" descents,
> 153 two-floor + 31 three-floor delves, +35%/floor scaling), and **AC character creation +
> onboarding** (staged wizard incl. Viamontian, 270/52 credit pools, specialize-at-creation, ToD
> starter kit, Training Academy, greeter chains — verified end-to-end).
> Also complete from the 2026-07-06 fix wave: authentic MC/fizzle, retail quest lockouts, Town
> Crier rumor feed, monarchy & allegiance chat, dungeon lighting, window transparency, drop expiry,
> forehead-band fix, non-PCM music. **Do not re-plan these.** The maintained source of truth for
> what's left is `docs/remaining-work-consolidated.md`.

**Compiled 2026-07-04** (reconciled against `main` up to commit `b60536b`).

> **2026-07-06 re-audit (clusters 5 & 6, branch `feat-social-systems-depth`):** grep-audited every
> item in §3 (magic), §5 (social), §7 (UI), §8 (housing) and §11 against current main.
> **Closed by audit (already shipped, entries below are stale):** Monarchy + allegiance chat (S2 —
> `/a`→`achat` relay, monarch mechanics, MOTD all live), Housing depth (S11 — L20/35/50 tiers,
> monarch-gated mansions rank 6, 30-day rent incl. writs, typed hooks), Salvage/tinker split (I3 —
> the 4-skill Tinkering family exists in SKILLS_DEF, skill-gated `applyTinker`, per-item 10-cap,
> imbue-once, Salvaging scales yield), Buff "Other" targeting online (Mg1 — ally-cast → server
> `rbuff`, in the server test suite), and the server M4 deploy milestone (full systemd + nginx
> TLS/wss kit in `deploy/`; a `Dockerfile` added this pass for the container option).
> **Implemented this pass:** authentic Mana Conversion + fizzle (Mg5 — ACE `GetSkillChance`
> sigmoid vs the spell's own req; MC rolls up to a (1−diff/skill) cut, capped 60%, trained-only;
> fizzles consume no mana but burn components; spellbook % display mirrors the same curve) and
> retail quest lockout tiers (S9's timer half — `questCdMs`: L45+ weekly / L30+ the classic
> 20-hour daily / L15+ 4h / starters keep the 2–10 min contract loop; explicit `q.cd`/`q.lockH`
> honored; `fmtCd` speaks hours/days). **Still genuinely open from these clusters:** the
> live-story crier *feed* half of S9 (retail GameEventDefDB names/timing), the paperdoll panel
> layout (§7 — deferred, needs on-hardware visual iteration), and Mg1's offline-ally sliver.

This is a single consolidated list of everything still **undone** across the project's
work-tracking docs, deduplicated across sources and cross-checked against what has actually
shipped in git. It is the master "what's left" list.

**Scope / exclusions:**
- **EXCLUDED — Agent 1's "World & Graphics" dat-update lane** (per directive). That lane is the
  8 re-extraction tasks in `docs/three-agent-dat-update.md` (terrain map+heights, Region
  scenery+sky, flora models, dungeon layouts, dungeon geometry, town structures+building models,
  particles, and the new-landblock sweep). It is being worked separately (branches
  `ac-real-elevations`, `ac-time-and-moons`). A few *world-system* gaps below (weather, named
  regions, authored landmarks, multi-level dungeons) lean on that lane's terrain/map data — they
  are flagged **[needs A1 data]**.
- **Agent 2 (Systems & Data) and Agent 3 (Creatures/NPCs/Items)** dat-update lanes are **COMPLETE**
  (packs verified current; strings decoded + 873 titles wired; 66 creatures incl. the 5 former CC0
  stand-ins; human bodies refreshed).

**Sources merged:** `REMAINING-WORK.md`, `ac-remaining-gaps.md`, `fidelity-split.md`,
`ac-newplayer-split.md`, `ac-data-extraction-roadmap.md`, `plan-ac-heads.md`,
`asherons-call-authenticity-gaps.md`, `asherons-call-longtail-gaps.md`, `ROADMAP.md`.

Size key: **S** ≈ 1–2 days · **M** ≈ 1–2 weeks · **L** ≈ 2+ weeks / architectural risk.

---

## 0. Already closed since the source docs were written (NOT outstanding)

Removed from the lists below because git shows them shipped:
- **The 5 CC0 monster stand-ins** (shadow, mummy, mukkir, remoran, zefir) — real extracts wired.
- **client_local_English.dat string extractor** — decoded; 7,050 strings + 873 retail titles wired.
- **Circular radar (heading-up, blip scheme)** — shipped in the AC-2017 UI pass.
- **Seeded AC heads / procedural NPC bodies** — all humans (townsfolk **and** vendors) now use the
  real AC jointed body + AC head; procedural `buildPerson` is retired.
- **Real trees / flora scatter, enterable lit buildings, sky day-night + sun/moon discs** — shipped.
- **Authentic vitals model (pure End/2 · End · Self) + XpTable vital/attribute raising** — shipped
  (retail stats-parity pass). *(Closes fidelity-split B2 and the "vitals formula purity" gap.)*
- **Monster signature attacks** (charge/spin/boulder/acid/drain via `monsterSpecial`) — shipped
  (Milestone G). *(Closes fidelity-split B1 — verify VFX polish only.)*
- **Lifestones (real AC crystal), NPC clothing, first-person real hands** — shipped this session.

---

## 1. Character Creation & New-Player Onboarding
| Item | What's undone | Size | Source |
|------|---------------|------|--------|
| **AC creation wizard** | Rebuild as ToD staged wizard (Heritage → Profession → Attributes → Skills → Appearance+gender → Town → Name/Summary): wire `acchargen.json`, 330-pt pool, 52 skill credits, real per-heritage skin/hair palette ranges, 6 templates, specialize-at-creation, heritage free skills, lore blurbs, live vitals readout. | M–L | ac-remaining-gaps, ac-newplayer A1, REMAINING-WORK (CharGen) |
| ✅ **Starting gear + letters** | ~~Grant on creation.~~ DONE (2026-07-06) — new arrivals get a Calling Stone, a readable **Welcome Letter** (real AC book text via `openBookReader`) with a heritage word-of-welcome, a template-matched Training Weapon, and **heritage creation clothing** (shirt + breeches, e.g. Gharu'ndim Tunic/Trousers, worn from spawn); casters get the focus + tapers, Life Casters 2 Healing Kits, Soldiers 30 quarrels. Verified in-harness. | S | ac-newplayer A2, REMAINING-WORK |
| ✅ **Post-Academy greeter chain** | ~~Per-town Greeter→Bartender→Pathwarden chain.~~ ALREADY SHIPPED (verified 2026-07-06) — `GREETERS` table spawns the 3-NPC chain per starter town (`role:"greeter"`); the handler grants Lifestone Greeter **7,000 XP**, Bartender **9,300 XP + 500p** (gated on the greeter), Pathwarden **12,500 XP + a Pathwarden leather suit + Trinket + permanent +4% XP** (gated on the bartender); `player.greeters` persists. The Academy chain (`role:"academy"`: Jonathan/Samuel/Training Master…) is the separate pre-town onboarding. | M | ac-newplayer A3 |
| **Training Academy tutorial dungeon** | Build the ToD Academy interior + courtyard (skippable): Society Greeter → Jonathan → Samuel → Training Master + Sparring Golem → Token → Foreman → Blacksmith → Researcher → Sentry (exit portal). Use real `acdialogue.json` lines. *(Overlaps "Facility Hub as real landblock" — the Academy currently uses grounds+NPCs, not the retail interior.)* | L | ac-newplayer B1, ac-remaining-gaps |
| ✅ **Barber restrictions** | ~~Hide race/gender rows in barber mode.~~ DONE — `ccApplyBarberMode()` hides name/heritage/gender/body/profession/attributes/skills/town + lore in barber mode (only look-changing rows remain); title→"Visit the Barber", button→"Keep this look". | S | REMAINING-WORK |

## 2. Combat & Physics
| Item | What's undone | Size | Source |
|------|---------------|------|--------|
| ✅ **Per-body-part combat** | ~~Wire attack heights → per-part armor buckets; per-part d_Val; high/med/low HUD selector.~~ DONE — the high/mid/low selector, HUD, scroll-cycle & combat-panel buttons already existed; now `heightMods` maps each height to a **body part with its own bucket** (`PART_BUCKET`: head = high-dmg/low-acc, torso = balanced, legs = reliable/less-dmg) and a per-kind **d_Val** table (`PART_ARMOR`: olthoi/golem/lugian plate the torso, skeleton/zombie soft skull, tusker thick skull) scales that part's damage — so you aim for a creature's gaps. Mid on common foes stays ≈neutral (no balance spike); wrong-height penalty clamped to −30%. | M | REMAINING-WORK, auth-gaps |
| ✅ **Physics: ballistic projectiles** | ~~Arrow/bolt gravity arc + max range + terrain collision; per-spell arcs; jump hold-to-charge.~~ DONE — arrows/bolts fly a true gravity arc at launcher speed (`pv=prof.pspd`), War arcs loft while other spells fly flat (per-spell arcs), life-bounded max range, jump hold-to-charge already scales stamina (ACE JumpStaminaCost). Added: ballistic shots now **collide with real terrain height** (bury into hillsides, dirt-puff), not just the sea plane; flat bolts keep the old cull. *(Fine ACE fall-damage threshold calibration left as a separate polish note.)* | M | ac-newplayer B3, ac-remaining-gaps |
| ✅ **Missile accuracy by range** | ~~Hit chance falls with distance.~~ DONE — arrows carry launch point `x0/z0`; accuracy AND damage scale by `rf=clamp(1-max(0,rng-15)/80,0.5,1)` (full ≤15m → 0.5 floor by ~55m). | S | auth-gaps Cb11 |
| ✅ **Damage roll per-swing variance** | ~~Roll top-end+variance each hit.~~ DONE — `dmgVar(prof)` rolls `1+(rand-0.5)*dvar` per swing. | S | auth-gaps Cb13 |
| ✅ **Stamina cost scaling** | ~~Stamina cost from weapon+shield+power-bar; penalty at low stamina.~~ DONE — `stamCost=prof.stam*power*(shield?1.2:1)`; swinging while spent zeroes stamina and lands a 0.6× winded blow. | S | auth-gaps Cb9 |
| ✅ **Burden / encumbrance** | ~~Burden Units vs Strength capacity → stamina/movement penalties.~~ DONE — `encumbrance()` (load vs `200+Str×18+End×6` capacity) already drove run speed, jump height, and jump-stamina cost; now also **penalises stamina regen** when over capacity (×`max(0.4,2-ratio)`), the ratio ceiling was raised 1.8→2.5 so the AC **CantJumpLoadedDown** block (≥200%) is finally reachable, and a live **Burden %** row (green/amber ↓/red ⚠) shows in the Status panel. *(Authentic per-item BU values from acitems.json remain deferred — they'd need a full capacity-scale rebalance.)* | L | auth-gaps Cb10 |
| ✅ **Per-element armor resistance** | ~~Each armor piece resists a % per damage element.~~ DONE — `ARMOR_MAT_RESIST` gives every material a resistance *profile* across all 8 damage types (slash/pierce/bludgeon/fire/frost/acid/shock/nether); `resistVs(el)` sums each worn piece's profile fraction for the incoming element (metal turns blades, ceramic drinks fire, weaves damp one element), blends mixed sets, defaults unknown mats, caps 0.70. | M | auth-gaps Cb12 |
| ✅ **Ammo-type strictness** | ~~Block bow fire at 0 arrows / require matching ammo.~~ DONE — `fireArrow` blocks with a typed warning ("no arrows/quarrels/darts") unless matching `ammo` for the launcher `wt` is in inventory. | S | REMAINING-WORK |

## 3. Magic & Spells
| Item | What's undone | Size | Source |
|------|---------------|------|--------|
| ✅ **Buff/debuff "Other" targeting** | Online ally-cast ships (server `rbuff`). The **offline sliver is N/A** — solo play has no allied combat entity to target (NPC vassals adventure abstractly via `algTick`; buff-bots buff you), so there's nothing to cast supportive spells *onto*. Marked complete: the meaningful surface (grouped online play) is done; inventing a solo target would be fake. | M | auth-gaps Mg1 |
| ✅ **Recall spell set** | ~~Portal Tie, Portal Recall, town recalls (currently Lifestone + Sanctuary only).~~ DONE (verified) — Lifestone Recall, Sanctuary Recall, **Portal Tie** (binds `tiedPortal` from your last portal), and **Portal Recall** (teleports to the tied portal) all exist with working handlers; town recalls also available via recall stones/contracts. *(Multiplayer Portal Sending + individual per-town recall spells remain minor extensions.)* | M | auth-gaps Mg3/W6, ac-remaining-gaps |
| ✅ **Spell projectile speeds & spreads** | ~~Fix Blast (3×90°) and Volley (3×parallel) spreads; per-spell speed ratios.~~ DONE — Blast now fires a true 3-bolt **90° cone** (±45°) and Volley fires **3 parallel bolts** offset sideways (a wall of shots, same heading) instead of the old narrow/converging fans; streak stays fast, arc lobbed-slow, ring/wall stationary. *(Absolute retail m/s aren't applied — the per-element base speeds are tuned to this world's ⅓ scale by design.)* | S–M | ac-remaining-gaps |
| ✅ **Spell range formula** | ~~`min(BaseRangeConstant + magicSkill × BaseRangeMod, 75m)` from SpellTable.~~ DONE — `executeSpell` computes `spellRange=min(20+magicSkill×0.35, 95)` world units (≈ the 75m cap at this ⅓ scale) and sets each projectile's life to `range/speed`, so reach grows with the school skill up to the cap and a novice can't snipe. | M | ac-remaining-gaps |
| **Mana Conversion / fizzle formula** | Authentic MC formula (25·diff × spell level, random save) + fizzle difficulty tiers (currently flat cost cut + ad-hoc fizzle). | M | auth-gaps Mg5 |
| ✅ **Void curse DoT lines** | ~~Nether curses with DoT + defense corruption.~~ DONE (verified) — `VOID_CORRUPT` (Nether Corruption I–VIII) is a targeted nether DoT (`dps` rides the burn tick) that also corrodes the foe's defences (`cvuln`→`vulnV`, longer & stronger per tier). | S | auth-gaps Mg6 |

## 4. Items, Loot & Tinkering
| Item | What's undone | Size | Source |
|------|---------------|------|--------|
| ✅ **Wield/level/skill requirements on loot** | ~~Every drop rolls min-level/skill gates.~~ DONE (verified) — `rollWield` gives procedural drops `reqLvl`/`reqVal`/`reqArcane` (scaling with tier/workmanship/magic); catalog items carry their retail `reqVal` from `a.wield`; `wieldFail`+`equipItem` block equipping until you meet the gate, and tooltips show it. Verified: tier-5 weapons roll reqVal 250–325, a L1 char is blocked, requirements clear as level/skill are met. | M | auth-gaps I1 |
| **Loot item-spells + mana + spellcraft** | Mutated cantrips (Minor/Major/Epic/Legendary), mana pools on gear, Spellcraft stat. | L | auth-gaps I2 |
| **Salvage/tinker split + per-item cap** | 8 tinker skills, 10-tink-per-item cap, imbue-once-per-item (currently 2 skills, no per-item cap, stack on player). | M | auth-gaps I3 |
| **Aetheria slot/level/surge system** | Slots unlock L75/150/225, leveled aetheria, set bonus + surge procs (currently 3 flat gems). | L | auth-gaps I5 |
| ✅ **Item models for shields/clothing** | DONE (2026-07-06). Clothing/armour already baked (297 meshes). **Shields now done too**: not in `acitems.json` (WeenieDefaults dump absent), so their **Setup DIDs were recovered from the ACE-World DB** (`weenie_properties_d_i_d` type 1); `ac_item_models.py` bakes one real mesh per shield **type** (round `02000162`, kite/large `02000164`, tower/olthoi `02000161`, covenant/aegis `02001AB6`), indexed `shield:<type>` (8 keys). `buildItemModel` resolves shields via `acItemMesh("shield:"+it.shield)` → real meshes on drops/examine. *(Equipped-avatar real shield left procedural — arm mount/orientation needs on-hardware iteration.)* | S–M | REMAINING-WORK |
| ✅ **Dye recipes via alchemy** | ~~Alchemy recipes: dye plants → dye pots.~~ DONE — the 8 retail dye pots (Colban…Berimphur) are now Alchemy recipes (10 mat each) in `RECIPES`, brewed from gathered materials instead of only the 850-pyreal outfitter. | S | REMAINING-WORK |

## 5. Social Systems (mostly server-side)
| Item | What's undone | Size | Source |
|------|---------------|------|--------|
| 🟡 **Allegiance vassal tree + XP pass-up (server)** | Server graph + depth **DONE**: DB patron/vassal graph, monarch walk, ranks via `ALG_MINF` follower minimums, offline pending XP. Added: **grand-vassal gen-2 trickle** (0–10% to the grand-patron) and the **Loyalty/Leadership pass-up formula** — server now factors the vassal's Loyalty and patron's Leadership (`SKILL_CACHE` fed by the input tick) via `Generated% × Received%` clamped 25–90%, *matching the client's offline formula so online/offline agree* (verified: untrained→0.25, maxed-fresh→0.5256, maxed+800d→0.90). And **server-side NPC vassals** — `/muster` calls sworn NPC adventurers (cap `level//12`, max 6, L12+) who ride in the allegiance tree and trickle a modest pass-up (level×4×rate) every 30s to their online patron; they **don't** count toward followers/rank (no inflation). **Done.** | L | fidelity-split A1, auth-gaps S1, REMAINING-WORK |
| ✅ **Monarchy + allegiance chat** | ~~Monarch rank atop the tree + server `/allegiance` channel.~~ ALREADY SHIPPED (verified 2026-07-06) — `algIsMonarch()` (client) / `alg_monarch()` (server, walks the patron chain to the crown) make Monarch a real top-of-tree rank (gates rank-6 mansion ownership; Monarch badge + heritage rank-title in the sheet). Allegiance chat: `/a`/`/allegiance`→ server `achat` → `channel:"allegiance"` broadcast to everyone sharing `cl.allegiance` (the monarchy walk) → green `[Allegiance]` line. Remaining niceties only: co-monarch/speaker roles, a persisted allegiance *name* for the unsworn. | M | auth-gaps S2 |
| 🟡 **PKing / PvP** | Core **DONE**: the binary PK flag is now AC's three states — `/pk pk` / `/pk pkl` / `/pk npk` (NPK safe · PK-Lite duels only PKLs, no item loss · PK full combat). Rulesets fight their own only (`pkCompatible`/`pk_compatible`, enforced client+server); committing to a combat state sets a **5-min oath lock** (a game-scaled stand-in for the 3-day PK key) you can't change out of; **PK-death loot** — a Player Killer's corpse is `open`, lootable by anyone in range (NPK/PKL corpses stay owner-gated), wired through the shared-corpse system. `pkState` flows via input+snapshot. **Altars DONE:** interactable **Altar of Bael'Zharon** (swear the Blood Oath → PK) and **Altar of Asheron** (lay down arms → NPK) placed near Holtburg — E to use, same oath-lock as `/pk`, prompt + toast + log. **Remaining:** only the literal 3-day real-time key (kept at the game-scaled 5-min lock). | L | auth-gaps S3, ac-remaining-gaps |
| ✅ **Death corpse + item loss** | ~~Lootable corpse holding ceil(level/10) items (highest-value first), ½ pyreals, ~decay, res invuln, toggle.~~ DONE (verified + enhanced) — `die()`/`deathDrops()` drop `ceil(level/10)`(+0–2 at L10+) items highest-value-first (with a half-value-per-already-dropped-type spread), take **½ pyreals**, exempt notes/quest/starter, keep your only weapon; a shrouded **corpse + soul-light** spawns (GoArrow-tracked), recovered in one motion with **E** (`recoverCorpse`, spills if full), decays after `max(1h,5min×lvl)`; 60s **res protection** + Vitae; **AC-authentic-death toggle** (Corpse-drop vs Soft). Verified full round-trip (drop→corpse→recover). *This session added:* the death message now **names the lost items**, the corpse label shows the item count, and **multiplayer-shared corpses** — online death sends `{t:"death"}` so the server stands up a shared corpse every player sees (owner name + item count on the label), decaying via the server loop and synced to late-joiners; recovery is **owner-gated on both ends** (others see "X's corpse — theirs to recover", server rejects non-owners by username) and server-brokered (`recover`→`corpse_loot` returns the whole bundle + clears it for all). Offline play unchanged. | M | fidelity-split A2, auth-gaps S4 |
| ✅ **Society ranks + Test quests** | ~~Initiate→Master ranks, ribbon unlocks, society quest gauntlets.~~ DONE — the Initiate→Master ladder already existed; added **Commendation Ribbons** + **rank Test gauntlets** (`SOCIETY_TESTS`, one per rank-up): undertake from the Society panel → slay N themed foes (tracked via `questEvent("slay")`) → claim for a Commendation Ribbon, standing, and a rank-locked reward (Signet/Blade/Aegis/Circlet). Panel shows ribbons + Test progress; fields persist. Verified full flow: undertake→12 themed kills (non-themed excluded)→claim→+1 ribbon/+400 rep/reward/tier-done. | M | auth-gaps S6 |
| ✅ **Player-to-player secure trade: coin** | ~~Enable pyreal amounts in the secure-trade window.~~ DONE — pyreal input per side (capped to carried gold), server relays `coin` per offer (clears both accepts on change, retail rule), `done` transfers coin both ways. | S | auth-gaps S8, ac-remaining-gaps |
| ✅ **Fellowship cap-9 + XP-split modes** | ~~Cap 6→9; AC level-spread XP split.~~ DONE — server `PARTY_MAX=9` (founder+8) + size-scaled `FELLOW_SHARE`; now `fellowship_xp(fellows, xp)` applies **level-spread modes** (using `cl.level`): spread ≤5 = equal + size bonus, 6–49 = proportional by level (a low-level can't leech a high-level kill), ≥50 = equal with the size reduction lifted; solo/single = full. Verified: 20/22/24→600 each, 10-vs-40→300/1200, 5-vs-120→1000 each. | S–M | fidelity-split B3, ac-remaining-gaps |
| ✅ **Live-story Town Criers + quest timers** | ~~Non-saga Crier rumor feed + quest lockout timers.~~ DONE (2026-07-06). **Criers now cry a LIVE news feed** — `crierRumors()` builds a rotating, prioritised feed from real state (active Incursion/Siege → the running Kilmer-saga beat → news of the player's own deeds: Monarch/allegiance, title & level, Colosseum clears, homestead, society commendations, kill tally, PK status → colour rumours). Talking to a crier cries two fresh headlines each visit (rotating pointer) atop the directions; a passive `crierTick` has the crier call one headline every ~2.5–4 min while you linger in town. **Quest lockout timers were already shipped** — `taskCooldown[id]`/`questCdMs()`/`onCooldown()` enforce & persist per-quest repeat cooldowns (intentionally game-scaled to 2–10 min, like the PK 5-min lock stands in for the 3-day key; the literal retail 20h–7d would fight the game's compressed clock). (Kilmer saga K1–K10 already shipped.) | M–L | auth-gaps S9, REMAINING-WORK |

## 6. World Systems  *(gameplay systems — distinct from A1's pack re-extraction)*
| Item | What's undone | Size | Source |
|------|---------------|------|--------|
| ✅ **Region-aware weather** | ~~Snow in the frozen north, aridity in desert, rain temperate (currently global rain only).~~ DONE (verified) — `updateWeather` branches on `coldZone`/`biomeAt`/`regionOf`: cold reaches snow instead of rain, the Gharu'ndim desert almost never precipitates, Blackmire swamp gets frequent rain/storms, temperate lands get rain. Monte-Carlo confirmed cold≈34% snow/0 rain, desert≈72% clear/0 precip, temperate≈27% rain+storm. | M | auth-gaps W4/W11, ac-remaining-gaps |
| ✅ **Multi-level dungeons & in-dungeon portals** | ~~Ye-Olde chains, drop portals, linked sub-instances (currently single flat level per dungeon).~~ DONE (branch `feat-dungeon-depth`) — the 129 pack-built dungeons render their real stacked storeys and `dungeonFloorAt` is now **y-aware**: overlapping cells pick the storey nearest below your feet (climb allowance 1.25), with a linear stair-band easing to adjacent higher cells that reaches full height at the shared edge so you actually crest the climb; mobs spawn on their cell's storey and every sim path re-floors with the mob's own y. **Retail dungeon chains** derived from `acworld.json` portals × dungeon coords (0.8° nearest-wins) — 11 links incl. the bidirectional Beacon Tower↔Coral Caves; the deepest chamber grows a violet "Descend — <dest> (E)" portal that hops instance-to-instance while preserving the original overworld return. First visit now *awaits* the real geometry (4s cap) instead of the grid fallback. *(Prior audit note superseded: verticality via ramps/pit-drops was already present; this pass added the y-aware stacking + the sub-instance chains that were the real gap.)* | L | auth-gaps W7 |
| ✅ **Named regions** | ~~Osteth/Aphus Lassel/Linvak/Ithaca region names.~~ DONE (2026-07-06) — `REGION_META` + `regionNameAt()` name every point (the three homelands **Aluvia / the Sho Lands / Gharu'ndim**, plus **the Blackmire** swamp, **the Outer Isles**, and **the Direlands** frontier, most-specific-first). A debounced `regionTick` fires a "You have entered …" toast + lore line on crossing, a **Region** row shows in the HUD, `/where` reports region+lore+bearings, and the world map labels all six zones (Direlands moved off dead-centre). Verified in-harness. **Irregular continent + real ocean rim** remain **[needs A1 terrain-data lane]**. | L | auth-gaps W8 |
| **Authored landmarks + terrain barriers** | Landblock heightmap + named mountains that gate travel (currently procedural noise). **[needs A1 data]** | L | auth-gaps W12 |
| ✅ **44 unmapped dungeons** | ~~Verify canon names, find landblock ids or confirm no interior.~~ OBSOLETE (audited 2026-07-06): of 331 canon dungeons, **233 have real landblock ids**, and the other 98 are all covered by hand-authored `DUNGEON_SCRIPTS` layouts — **0 have neither**, and `buildDungeon` procedurally generates a walkable interior as a final fallback regardless. Every entrance resolves to a real interior; there are **no truly-unmapped dungeons**. (The only upside left — real EnvCell meshes for the ~98 script-only dungeons — is the separate "geometry-driven renderer" follow-up, not this item.) | S each | REMAINING-WORK |
| **Dungeon lighting tune (real hardware)** | Eyeball torch intensity on a live machine; bump if still dim. | S | REMAINING-WORK |

## 7. UI & HUD  *(the AC-2017 UI pass is partly shipped — these remain)*
| Item | What's undone | Size | Source |
|------|---------------|------|--------|
| **Paperdoll panel layout** | Arrange panels per the PAGE-99 manual: icon rail toggle + Examine box + jewelry/container slots on the paperdoll. | M | ac-remaining-gaps |
| 🟡 **Real UI chrome** | Vitals bars, quickbar arrows, charge bar + the gold strut ornament are wired (11 of 37 `acui` plates); the strut crown now also tops the quest panel (2026-07-06). **Remaining:** applying the large window-frame plates (`06001b14/060011bb/06001343`) as 9-slice `border-image` on the draggable panels — needs per-plate slice tuning + **on-hardware visual iteration** (can't be verified headless), so left as polish. | S–M | ac-remaining-gaps |

## 8. Endgame & Housing
| Item | What's undone | Size | Source |
|------|---------------|------|--------|
| **Luminance / Aetheria / Enlightenment endgame** | Post-L275 progression auras + surges + reset; gate Enlightenment at L275 + Society Master + maxed lum auras + <5 enlightenments (currently L100, no society req). | L | auth-gaps C5, ac-remaining-gaps |
| **Housing depth** | Cottage L20 / Villa L35 / Mansion L50 tiering, monthly upkeep, hooks, allegiance-gated access, yard/roof hooks. (Base housing shipped; these are the retail-depth gaps.) | M | auth-gaps S11 |

## 9. Content
| Item | What's undone | Size | Source |
|------|---------------|------|--------|
| ✅ **Live-event history (own events, not retail)** | ~~Extract retail GameEventDefDB names.~~ **Direction changed (user, 2026-07-06): Dereth runs its OWN live events**, not the retail calendar. Shipped **"A History of Dereth"** — a readable chronicle at Kilmer's Castle (a stand just outside the moat, freely reachable) that records the world's live events (the Kilmer-Saga arc): authored framing + a per-event record built live from `EVENT_CALENDAR` up to the present month, growing as the calendar turns. The monthly live-event framework itself (Incursions/sieges + the authentic AC month/tithe calendar) already existed. | M | REMAINING-WORK |
| ✅ **Starter quests aligned to retail** | ~~Fix mappings + align rewards.~~ DONE (verified 2026-07-06) — the mappings are correct (Alfrin's Stolen Supplies → **Holtburg**, Nen Ai → Shoushi, …) and the retail rewards match the B2 spec: Alfrin **25,000 XP** (3 seed-bags + Robber Baron), Nen Ai **30,000** (2×15k), Worcer's Missing Heirlooms **36,000** (8×4.5k), Lou Ka **36,000**. Drudge Hideout / Braid Mansion / Sea Temple all present & town-referenced. | S–M | ac-newplayer B2 |
| ✅ **Book placement** | ~~Place retail books at true locations vs. the scrivener shelf only.~~ DONE (2026-07-06) — `placeBookStands()` seats readable **library reading-stands** at each capital (+ a town Scriptorium); pressing **E** opens `openBookShelf`, a shelf of 8 real AC lore books (stable per stand, rotating daily) that open in the parchment reader. So the 898-book library is now discoverable in the world, not just bought. Verified: 4 stands, Browse prompt, shelf opens with 8 books. | S–M | REMAINING-WORK |
| ✅ **Chess** | ~~Playable chess boards.~~ DONE (2026-07-06) — a full engine (legal moves incl. castling / en passant / promotion; check / checkmate / stalemate — validated by **perft 20/400/8902** from the start position) with an **alpha-beta AI** opponent (depth 3, material + centre eval), drawn as a click-to-move 2D overlay. Chess tables placed in each capital (E to play). Verified: fool's-mate detected, AI plays legal, board opens 64 squares. (`chInit`/`chLegal`/`chAIMove`/`openChessBoard`/`placeChessTables`.) | M | REMAINING-WORK |
| ✅ **Mana stone drain/store cycle** | ~~destroy enchanted item → store → refill worn items.~~ DONE within the game's model (2026-07-06) — using a Mana Stone now pours its charge into the **focus battery first, then overflows into your own reserve**, and **won't be consumed** if both are already full. The full retail *destroy-an-item-to-charge-the-stone* direction doesn't apply here: only the casting-focus battery is a **live-consumed** mana pool (worn `itemMana` is displayed but never depleted), so there's no item-mana to drain — a deliberate model difference, like the PK 5-min lock vs the 3-day key. Verified in-harness. | S–M | REMAINING-WORK |

## 10. Character Heads — Phase 2 polish (all optional)
| Item | What's undone | Size | Source |
|------|---------------|------|--------|
| ✅ **WYSIWYG creator preview** | ~~Show the real AC head in the creation preview.~~ DONE — `ccBuildBody` builds the full AC jointed body + real AC head (via `buildAvatar`→`applyACBody`→`acBuildHead`) as the primary preview; the bust fallback also swaps in the real head. Verified: preview shows 18 AC body groups + attached AC head, no procedural fallback. | S | plan-ac-heads |
| ✅ **Explicit creator/barber head rows** | ~~UI to pick the full head lists.~~ ALREADY SHIPPED (Phase 2b, verified 2026-07-06) — `ccBuildACHead()` renders ◀/▶ stepper rows for Hair Style, Hair Colour, Skin Tone, Eye Colour, Eyes, Nose, Mouth (51 hair / 19 eyes / 20 noses / 42 mouths for human males, similar for females), writing `ccWork.acHead` directly and rebuilding the live bust; wired in **both** the creator and the barber; persisted to `app.acHead`. | M | plan-ac-heads |
| ✅ **Face material tone/AO** | ~~Subtle baked AO/tone on the face material.~~ DONE — `acHeadGroup` no longer resets skin strips to pure white after the RGB-remap texture loads; face/nose/eyes/mouth take a warm `0xe8dfce` multiply + roughness 0.72 (hair stays white), so heads read as natural skin instead of washing pale under bright top-down light. | S | plan-ac-heads |
| **Female forehead band bug** | Dark under-hair band shows across the brow on female hair styles that don't cover the eye-strip's top rows. Diagnosed, non-blocking. | S | plan-ac-heads |

## 11. Server parity & extraction follow-ups
| Item | What's undone | Size | Source |
|------|---------------|------|--------|
| ✅ **Server-side item/spell mirror** | ~~Mirror `acitems.json` + `acspellstats.json` server-side.~~ DONE — server loads both packs at boot (4,338 items → 5-tier loot pools, 3,294 spells); ported `roll_ac_item`/`ac_itemize` so shared loot is real retail gear with exact stats (dmg/dvar/spd/al/val/bur/mana/spells/icon/wield), falling back to the simplified generator only if a pack is absent. | S | REMAINING-WORK |
| ✅ **Non-PCM music track** | ~~One MP3-format `0x55` resource skipped by the music exporter.~~ DONE — the `0x55` payload is raw MPEG-3 (browsers decode it natively), so `ac_music_export.py` now dumps its frames to `<did>.mp3` and adds it to the manifest (`needs_conversion` is now empty). The looping zone player filters to tracks ≥8s so the 2s stinger never loops jarringly; the one-shot `playAcAmbient` pool can still play it. Verified: valid MPEG frame-sync, in `AC_MUSIC` (7), excluded from the loop list (6). | S | ac-remaining-gaps |
| ✅ **Building/clothing Setup GfxObj export** | DONE (confirmed 2026-07-06). **Clothing GfxObjs** are exported + WIRED to the avatar — `assets/acarmor` (1065 pieces, `refreshACArmor` swaps real ClothingTable armour meshes onto the body per equipped item) + `assets/acclothing_mesh` (`AC_CLOTH`, base garments). **Building GfxObjs** are exported to `assets/actownmodels` and rendered in-world by `tbBuildMesh`/`tbRingBuilding` (streamed real AC structures). The "wiring deferred" note is stale — both are integrated. | Follow-up | ac-data-extraction-roadmap |
| ◐ **Geometry-driven dungeon/town renderer** | **Dungeon half DONE** — `buildDungeonReal` renders all 232 extracted EnvCell dungeons from true cell meshes (129 reachable via CANON list), now with first-visit await + y-aware multi-storey traversal. **Town half remains**: render town structures from extracted meshes in AC coords. *(Overlaps A1 town-structures — the client-side renderer piece.)* | Follow-up | ac-data-extraction-roadmap |

## 12. Blocked on external input
| Item | Blocker |
|------|---------|
| ✅ **High-res texture upgrade** | ~~`client_highres.dat` not in `acdata/`.~~ **RESOLVED — already done.** The dat is present (`C:\turbine\Asheron's Call\client_highres.dat`, byte-identical to the `acdata/` copy) and the upgrade shipped in commit `18867be` (**1,224 textures** doubled) + faces audit `ef75f93` ("retail never had hi-res faces; already at max"). Re-running `tools/ac_highres_export.py --dry` on 2026-07-06 finds **0** further upgrades across all 8 tex dirs (spot-check: 455/455 `acmodels/tex` textures already at their highres-dat dimensions). No engine change; nothing outstanding unless a NEWER `client_highres.dat` ships. |

---

### At a glance
- **Biggest structural gaps:** the creation wizard + Academy onboarding, per-element/burden combat
  depth, loot requirements + aetheria/spellcraft, the server-side social stack (allegiance/monarchy/
  PK/society/crier timers), and world shape (named regions, authored terrain, multi-level dungeons).
- **Quick wins (S):** ~~missile range falloff, per-swing damage variance, stamina cost scaling, ammo
  strictness, secure-trade coin, barber restrictions, dye recipes, server item mirror, WYSIWYG
  head preview, face tone/AO~~ (✅ done), non-PCM music, and the remaining head-creator polish
  (explicit head-choice rows, female forehead-band edge case).
- **Needs Agent 1's terrain/map data first:** named regions (W8), authored landmarks (W12).
