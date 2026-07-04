# Dereth — Wrap-Up: Remaining Undone Work

**Compiled 2026-07-04** (reconciled against `main` up to commit `b60536b`).

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
| **Starting gear + letters** | Grant on creation: Calling Stone, per-heritage Letter From Home, template-matched training weapon, creation clothing; casters get wand+3 spells+components; Life caster +2 heal kits; Soldier crossbow +30 quarrels. | S | ac-newplayer A2, REMAINING-WORK |
| **Post-Academy greeter chain** | Per starter town: Lifestone Greeter (7k XP + first Contract) → Bartender (9.3k XP + 500p) → named Pathwarden (12.5k XP + Supply Key → race armor + robe + +4% XP trinket). ~41k XP → ~L7. | M | ac-newplayer A3 |
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
| **Buff/debuff "Other" targeting** | CE/Item/Life/Protection spells need Self **and** Other forms for group support (most are self-only). | M | auth-gaps Mg1 |
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
| **Item models for shields/clothing** | Extend `ac_item_models.py` to export Clothing/shield models (watch pack size). | S–M | REMAINING-WORK |
| ✅ **Dye recipes via alchemy** | ~~Alchemy recipes: dye plants → dye pots.~~ DONE — the 8 retail dye pots (Colban…Berimphur) are now Alchemy recipes (10 mat each) in `RECIPES`, brewed from gathered materials instead of only the 850-pyreal outfitter. | S | REMAINING-WORK |

## 5. Social Systems (mostly server-side)
| Item | What's undone | Size | Source |
|------|---------------|------|--------|
| 🟡 **Allegiance vassal tree + XP pass-up (server)** | Server graph + depth **DONE**: DB patron/vassal graph, monarch walk, ranks via `ALG_MINF` follower minimums, offline pending XP. Added: **grand-vassal gen-2 trickle** (0–10% to the grand-patron) and the **Loyalty/Leadership pass-up formula** — server now factors the vassal's Loyalty and patron's Leadership (`SKILL_CACHE` fed by the input tick) via `Generated% × Received%` clamped 25–90%, *matching the client's offline formula so online/offline agree* (verified: untrained→0.25, maxed-fresh→0.5256, maxed+800d→0.90). **Remaining:** server-side **NPC vassals** padding the online graph (client already simulates them offline). | L | fidelity-split A1, auth-gaps S1, REMAINING-WORK |
| **Monarchy + allegiance chat** | Monarch rank atop the tree + server `/allegiance` channel. | M | auth-gaps S2 |
| 🟡 **PKing / PvP** | Core **DONE**: the binary PK flag is now AC's three states — `/pk pk` / `/pk pkl` / `/pk npk` (NPK safe · PK-Lite duels only PKLs, no item loss · PK full combat). Rulesets fight their own only (`pkCompatible`/`pk_compatible`, enforced client+server); committing to a combat state sets a **5-min oath lock** (a game-scaled stand-in for the 3-day PK key) you can't change out of; **PK-death loot** — a Player Killer's corpse is `open`, lootable by anyone in range (NPK/PKL corpses stay owner-gated), wired through the shared-corpse system. `pkState` flows via input+snapshot. **Remaining:** interactable **Altars of Bael'Zharon/Asheron** to change state in-world (currently `/pk`), and the literal 3-day real-time key. | L | auth-gaps S3, ac-remaining-gaps |
| ✅ **Death corpse + item loss** | ~~Lootable corpse holding ceil(level/10) items (highest-value first), ½ pyreals, ~decay, res invuln, toggle.~~ DONE (verified + enhanced) — `die()`/`deathDrops()` drop `ceil(level/10)`(+0–2 at L10+) items highest-value-first (with a half-value-per-already-dropped-type spread), take **½ pyreals**, exempt notes/quest/starter, keep your only weapon; a shrouded **corpse + soul-light** spawns (GoArrow-tracked), recovered in one motion with **E** (`recoverCorpse`, spills if full), decays after `max(1h,5min×lvl)`; 60s **res protection** + Vitae; **AC-authentic-death toggle** (Corpse-drop vs Soft). Verified full round-trip (drop→corpse→recover). *This session added:* the death message now **names the lost items**, the corpse label shows the item count, and **multiplayer-shared corpses** — online death sends `{t:"death"}` so the server stands up a shared corpse every player sees (owner name + item count on the label), decaying via the server loop and synced to late-joiners; recovery is **owner-gated on both ends** (others see "X's corpse — theirs to recover", server rejects non-owners by username) and server-brokered (`recover`→`corpse_loot` returns the whole bundle + clears it for all). Offline play unchanged. | M | fidelity-split A2, auth-gaps S4 |
| **Society ranks + Test quests** | Initiate→Adept→Knight→Lord→Master ranks, ribbon unlocks, society quest gauntlets. | M | auth-gaps S6 |
| ✅ **Player-to-player secure trade: coin** | ~~Enable pyreal amounts in the secure-trade window.~~ DONE — pyreal input per side (capped to carried gold), server relays `coin` per offer (clears both accepts on change, retail rule), `done` transfers coin both ways. | S | auth-gaps S8, ac-remaining-gaps |
| ✅ **Fellowship cap-9 + XP-split modes** | ~~Cap 6→9; AC level-spread XP split.~~ DONE — server `PARTY_MAX=9` (founder+8) + size-scaled `FELLOW_SHARE`; now `fellowship_xp(fellows, xp)` applies **level-spread modes** (using `cl.level`): spread ≤5 = equal + size bonus, 6–49 = proportional by level (a low-level can't leech a high-level kill), ≥50 = equal with the size reduction lifted; solo/single = full. Verified: 20/22/24→600 each, 10-vs-40→300/1200, 5-vs-120→1000 each. | S–M | fidelity-split B3, ac-remaining-gaps |
| **Live-story Town Criers + quest timers** | Non-saga Crier rumor feed + server-side quest flagging/lockout timers (20h–7d) via `QuestDefDB` min_Delta, bridged to custom quest ids. (Kilmer saga K1–K10 already shipped.) | M–L | auth-gaps S9, REMAINING-WORK |

## 6. World Systems  *(gameplay systems — distinct from A1's pack re-extraction)*
| Item | What's undone | Size | Source |
|------|---------------|------|--------|
| ✅ **Region-aware weather** | ~~Snow in the frozen north, aridity in desert, rain temperate (currently global rain only).~~ DONE (verified) — `updateWeather` branches on `coldZone`/`biomeAt`/`regionOf`: cold reaches snow instead of rain, the Gharu'ndim desert almost never precipitates, Blackmire swamp gets frequent rain/storms, temperate lands get rain. Monte-Carlo confirmed cold≈34% snow/0 rain, desert≈72% clear/0 precip, temperate≈27% rain+storm. | M | auth-gaps W4/W11, ac-remaining-gaps |
| **Multi-level dungeons & in-dungeon portals** | Ye-Olde chains, drop portals, linked sub-instances (currently single flat level per dungeon). | L | auth-gaps W7 |
| **Named regions + irregular continent** | Osteth/Aphus Lassel/Linvak/Ithaca regions + real ocean rim (currently a square 16000² map with trig noise terrain). **[needs A1 data]** | L | auth-gaps W8 |
| **Authored landmarks + terrain barriers** | Landblock heightmap + named mountains that gate travel (currently procedural noise). **[needs A1 data]** | L | auth-gaps W12 |
| **44 unmapped dungeons** | Verify canon names, find landblock ids or confirm no interior. | S each | REMAINING-WORK |
| **Dungeon lighting tune (real hardware)** | Eyeball torch intensity on a live machine; bump if still dim. | S | REMAINING-WORK |

## 7. UI & HUD  *(the AC-2017 UI pass is partly shipped — these remain)*
| Item | What's undone | Size | Source |
|------|---------------|------|--------|
| **Paperdoll panel layout** | Arrange panels per the PAGE-99 manual: icon rail toggle + Examine box + jewelry/container slots on the paperdoll. | M | ac-remaining-gaps |
| **Real UI chrome** | Wire the remaining extracted chrome: framed-bar variants, portrait heads, panel corner/strut gold pieces (~50 uninspected contact sheets). | S–M | ac-remaining-gaps |

## 8. Endgame & Housing
| Item | What's undone | Size | Source |
|------|---------------|------|--------|
| **Luminance / Aetheria / Enlightenment endgame** | Post-L275 progression auras + surges + reset; gate Enlightenment at L275 + Society Master + maxed lum auras + <5 enlightenments (currently L100, no society req). | L | auth-gaps C5, ac-remaining-gaps |
| **Housing depth** | Cottage L20 / Villa L35 / Mansion L50 tiering, monthly upkeep, hooks, allegiance-gated access, yard/roof hooks. (Base housing shipped; these are the retail-depth gaps.) | M | auth-gaps S11 |

## 9. Content
| Item | What's undone | Size | Source |
|------|---------------|------|--------|
| **Retail monthly live events** | Extract real monthly world-event names/timing from portal.dat GameEventDefDB + ACE event table onto the existing monthly framework. | M | REMAINING-WORK |
| **Starter quests aligned to retail** | Fix mappings (Drudge Hideout=Holtburg, Braid=Shoushi, Sea Temple=Yaraq) + align rewards for ~10 quests. | S–M | ac-newplayer B2 |
| **Book placement** | Place the 898 retail books at true locations (libraries, quest dungeons) vs. the scrivener shelf only. | S–M | REMAINING-WORK |
| **Chess** | Playable chess boards in taverns/houses (Game/GamePiece weenies + 2D overlay). | M | REMAINING-WORK |
| **Mana stone drain/store cycle** | Change to: destroy enchanted item → store its mana in the stone; use stone → refill worn items (currently a focus-battery recharge). | S–M | REMAINING-WORK |

## 10. Character Heads — Phase 2 polish (all optional)
| Item | What's undone | Size | Source |
|------|---------------|------|--------|
| ✅ **WYSIWYG creator preview** | ~~Show the real AC head in the creation preview.~~ DONE — `ccBuildBody` builds the full AC jointed body + real AC head (via `buildAvatar`→`applyACBody`→`acBuildHead`) as the primary preview; the bust fallback also swaps in the real head. Verified: preview shows 18 AC body groups + attached AC head, no procedural fallback. | S | plan-ac-heads |
| **Explicit creator/barber head rows** | UI to pick from the full 50+ hair styles + eye/nose/mouth strips + exact skin/hair/eye colours (writes `app.acHead`); currently only derived from legacy rows. | M | plan-ac-heads |
| ✅ **Face material tone/AO** | ~~Subtle baked AO/tone on the face material.~~ DONE — `acHeadGroup` no longer resets skin strips to pure white after the RGB-remap texture loads; face/nose/eyes/mouth take a warm `0xe8dfce` multiply + roughness 0.72 (hair stays white), so heads read as natural skin instead of washing pale under bright top-down light. | S | plan-ac-heads |
| **Female forehead band bug** | Dark under-hair band shows across the brow on female hair styles that don't cover the eye-strip's top rows. Diagnosed, non-blocking. | S | plan-ac-heads |

## 11. Server parity & extraction follow-ups
| Item | What's undone | Size | Source |
|------|---------------|------|--------|
| ✅ **Server-side item/spell mirror** | ~~Mirror `acitems.json` + `acspellstats.json` server-side.~~ DONE — server loads both packs at boot (4,338 items → 5-tier loot pools, 3,294 spells); ported `roll_ac_item`/`ac_itemize` so shared loot is real retail gear with exact stats (dmg/dvar/spd/al/val/bur/mana/spells/icon/wield), falling back to the simplified generator only if a pack is absent. | S | REMAINING-WORK |
| **Non-PCM music track** | One MP3-format `0x55` resource skipped by the music exporter. | S | ac-remaining-gaps |
| **Building/clothing Setup GfxObj export** | Export building + clothing Setup GfxObjs through the `ac_env` pipeline for deeper integration (data extracted; wiring deferred). | Follow-up | ac-data-extraction-roadmap |
| **Geometry-driven dungeon/town renderer** | Render dungeons/towns from the extracted real meshes in AC coords (not the room-graph). Data is extracted. *(Overlaps A1 town-structures — the client-side renderer piece.)* | Follow-up | ac-data-extraction-roadmap |

## 12. Blocked on external input
| Item | Blocker |
|------|---------|
| **High-res texture upgrade** | `client_highres.dat` not in `acdata/`. Staged: user runs `acdata/ac1install.exe`; whoever's active copies the produced dat in and re-runs all `tex/` exports at the same TIDs (no engine change). |

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
