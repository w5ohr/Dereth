# Dereth — Build Roadmap

Master execution plan for the game in `index.html`. Ordered by dependency and impact.
Each item: **what**, **why/source**, and **verify**. Workflow for every change =
`jsc` syntax check → preview reload → `preview_eval` asserts → `preview_console_logs level:error`
→ screenshot → open in browser. Facts/lore only from AC research (see the plan file's Phase 6);
all assets stay original geometry + procedural textures.

Legend: ✅ done · ⏳ next · ☐ planned · ◇ optional/later · 🔎 needs research first

---

## ✅ DONE (shipped & verified)
- First-person 3D engine (Three.js r128, local, offline), huge world (16000u) on real AC coordinates.
- 6 attributes + vitals, War/Life magic + Creature/Item self-buffs, melee/bow/wand, crits/status.
- Lifestone bind/respawn, XP→skills, loot→satchel→tinkering/salvage, quests, vendors/potions.
- 24 real towns, 3 heritages (Aluvian/Sho/Gharu'ndim), Direlands, Town Network hub, 50 dungeons.
- Bestiary incl. Olthoi/Lugian/Tumerok/Mattekar/Virindi/Skeleton + Gnawvil & Bael'Zharon bosses.
- Terrain (mountains/valleys/ocean), cobblestone contour roads, sky (sun+halo, moon, clouds, stars,
  warm horizon), reflective water, day/night (slow).
- Vitae death penalty, allegiance/patron, achievements/titles.
- Enterable buildings, class-specific viewmodels, character material textures, realistic swing/shoot/
  cast animations.
- Visible avatar + 1st/3rd-person toggle (V), smooth camera follow + camera collision, body detail
  (hands/feet/pauldrons/cape/quiver).
- **Portal proximity fix** (run-into-them now works).

---

## MILESTONE A — Authenticity quick wins  ✅ DONE
Low risk, high faithfulness; all from verified research (Phase 6.6).
- ✅ **A1. Real vital formulas** in `derive()`: Health = Endurance/2, Stamina = Endurance, Mana = Self
  (confirmed in ACEmulator source). *Verify:* eval attributes→HP/Stam/Mana match formulas; rebalance
  enemy damage if needed so it stays playable.
- ✅ **A2. Name the four magic schools** in the spell bar/character sheet/codex: War (Arm), Life (Heart),
  Creature Enchantment (Left Hand), Item Enchantment (Right Hand). *Verify:* UI shows groupings.
- ✅ **A3. Bestiary threat tiers** (Feeble/Weak/Dangerous/Deadly/Lethal) as a codex label per creature.
  *Verify:* codex (B) shows tier; tiers map sensibly to hp/dmg.
- ✅ **A4. Lore flavor** in intro + codex: Dereth = volcanic isle of the planet **Auberean**;
  Bael'Zharon = the fallen Empyrean **Ilservian Palacost**. *Verify:* text appears, no overflow.

## MILESTONE B — Bestiary & world-lore accuracy  ✅ DONE
- ✅ **B1. Named Shadow generals** as mini-bosses: Ler Rhan, Black Ferah, Isin Dule (Direlands spawns,
  tougher than elites, unique loot). *Verify:* spawn + kill + reward.
- ✅ **B2. Olthoi variants**: Soldier (acid ranged attack) + Worker; per-family **chief/champion** elites
  for Drudge/Mosswart/Banderling/Tumerok. *Verify:* builders + AFFINITY + spawn.
- ✅ **B3. Lore-accurate region clustering**: Lugians → southern **Linvak** mountains; Tumeroks/Virindi →
  **Direlands**; Mosswarts → a **Blackmire** swamp band between Aluvian & Sho lands. *Verify:* `pickKind`
  region pools + a swamp biome tint/water patch.

## MILESTONE C — Graphics-fidelity pass  ◑ (C1–C3 done; C4 optional) (continues the "improve graphics" thread)
- ✅ **C1. Textured ground**: procedural grass/dirt/rock/sand that blends by biome + slope (replace flat
  vertex color). Highest visual ROI. *Verify:* screenshots in each region; fps ok.
- ✅ **C2. Foliage**: grass tufts (instanced/billboard) + trees/bushes clustered around hubs; sway.
  *Verify:* density near towns, none in water, fps ok.
- ✅ **C3. Interior furnishings** for enterable buildings: tables, hearths, shelves, beds, vendor counters,
  rugs, a light source that reads as warm. *Verify:* walk inside, props present, still passable.
- ◑ **C4. Render polish** (mostly done): ✅ softer shadow edges (`sun.shadow.radius`), ✅ blade motion-trail
  swoosh on slash, ✅ FOV kick on sword hit & bow release (eased back to base FOV). ◇ ambient-occlusion fake
  deferred (muddies more than it helps without a real SSAO pass).
- ✅ **C5. Cinematic render pipeline** (jsc + preview verified, 0 console errors). Decision: stay on
  HTML+local Three.js (a C++ engine would break the zero-install/offline constraint and discard a deep,
  working codebase; WebGL2 was nowhere near its ceiling). Added **ACES Filmic tone-mapping** +
  **sRGBEncoding** output + exposure 1.04 (correct modern colour pipeline; every procedural colour
  texture tagged `encoding=sRGBEncoding` via `markSceneSRGB()`, called at init/startGame/enterDungeon/
  enterNetwork). Built a **self-contained inline post-processing chain** (no extra files): render scene →
  full-res target → luminance bright-pass (threshold 0.84) → two 9-tap separable Gaussian blur iterations
  at half-res → composite (additive **bloom** strength 0.7 + mild saturation lift + cool-shadow/warm-
  highlight grade + **vignette** 0.36) to screen. `POST` globals; `initPost`/`resizePost`/`renderComposite`
  drive it; the rAF loop, resize handler, and title render all call `renderComposite()`. Result: glowing
  magic/stars/lights/clouds, filmic contrast, framed vignette — a dramatic upgrade from the old flat look.
- ✅ **C6. Surface & water detail** (jsc + preview verified, 0 console errors). `makeNormalTex(size,hf,
  strength,repeat)` builds tiling tangent-space normal maps from a height function (central-difference
  gradient; kept LINEAR, never sRGB-tagged). **Ground** → `MeshPhongMaterial` (was Lambert) with a
  procedural `groundNormalTex()` + low specular so terrain catches raking sunlight (per-fragment shading;
  colour map still sRGB). **Water** → animated `waterNormalTex()` ripples (normalMap `offset` scrolled each
  frame in `updateDayNight`) + brighter specular sheen. **Foliage** density 2400 → 3600 tufts. Also lifted
  near-black roof palette (Sho `0x2c3a2c`→`0x46604a`, Aluvian `0x5a2a20`→`0x7a3a2c`) that the C5
  tone-mapper was crushing to black. Verified midday grass relief, scrolling water normals, readable roofs.
  ◇ Further (optional): real fresnel water shader, screen-space reflections, MeshStandard PBR everywhere.

## MILESTONE D — Third-person polish  ◇
- ✅ **D1.** Over-the-shoulder camera offset — pivot shifted along camera-right (`SHOULDER=0.7`) so the
  avatar sits to one side and the crosshair stays clear (verified: avatar projects off-center, on-screen).
- ◇ **D2.** Idle breathing/sway when standing; **D3.** foot/leg plant on uneven ground (light IK).

## MILESTONE E — Deeper RPG systems  🔎 (research-gated)
- ✅ **E0. Research pass #2** (105-agent deep-research, adversarially verified, 2026-06-29). Verified &
  appended to plan **Phase 6.7**: skill→attribute base formulas, train/specialize credit costs, 52→102
  credits, Trained-vs-Specialized XP curves, fellowship cap 9 + XP-share tiers. **Still open** (do not
  guess): per-level XP curve & max level, attribute-raise XP, allegiance passup %, all itemization.
- ✅ **E1. Skill system expansion** (full retail ~37 skills, authentic two-currency economy; jsc+preview
  verified, 0 console errors). `SKILLS_DEF` grouped by family (Magic/Defense/Melee/Missile/Trade/
  Tinkering/Lore) with verified `(attrA+attrB)/N` base formulas + train/specialize credit costs.
  Untrained/Trained/Specialized tiers; **skill credits** (52→~100 by level + L35/L90 quest credits;
  70-credit spec cap) to train/specialize; **unspent XP** spent to raise ranks on the verified Trained-
  vs-Specialized curves (pt1=58/23 confirmed). Magic schools require Trained to cast; Specialized adds
  +10 value. Combat rewired to skill values (melee=best weapon skill, missile, war, life+healing,
  meleed+shield defense, Mana Conversion cost cut, Run speed). Heritage creation loadout. Old saves
  migrate (melee→heavy, defense→meleed, etc.). New character-sheet UI with Train/Spec/raise controls.
- ✅ **E0b. Research pass #3** (106-agent deep-research, adversarially verified, 2026-06-29) — appended to
  plan **Phase 6.8**. Un-gated E2 (tinkering/salvage/workmanship mechanics) and E3 (allegiance pass-up
  formula confirmed verbatim). XP-per-level (classic 126 / cumulative 4.28B) and attribute-raise curves
  verified but judged **reference-only** — MMO-grind magnitudes unfit for a single-player homage; keep our
  scaled curves. Still gated: full loot-tier ladder, tinkering success-chance formula, retail-275 table,
  innate attr cap >100.
- ◑ **E2. Itemization depth — workmanship & salvage/tinker core SHIPPED** (jsc + preview verified, 0 console
  errors). Every loot item now rolls a **material** (first word of its name, AC convention) + **workmanship
  1–10** within a loot-tier range (`WORK_TIER`, `MATERIALS`, `rollItem(rare,tier)`; drop sites pass tier by
  source — overworld 1 / elite 3 / dungeon = delve tier / boss & Incursion 5). **Salvaging** gear yields
  units of its material with **units-weighted-average workmanship** (`player.salvage{mat:{units,work}}`;
  Salvaging skill boosts yield). **Tinkering Application**: 100 units of one material + the matching
  tinkering skill trained applies a workmanship-scaled bonus to weapon damage / armor (feeds existing
  `weaponTink`/`armorTink`), with a success chance (our own formula — real one gated) that rises in
  difficulty as bonuses stack; failure costs the salvage (gear spared — player-friendly deviation from
  AC's destroy-both). New Tinker-panel "Salvaged Materials" section; satchel shows workmanship; saved/
  loaded. ◑ *Remaining for full E2:* loot-tier ladder, imbue/rare item classes — still partly gated.
- ✅ **E2b. Distinct weapon types** (jsc + preview verified, 0 console errors). `WEAPON_TYPES` (dagger/
  sword/spear/axe/mace = melee; bow/crossbow = missile), each with a speed (cd mult) / reach / damage /
  stamina profile so the equipped weapon's TYPE drives a real DPS-vs-burst-vs-reach tradeoff (dagger
  fastest+weakest, mace slow+crushing, spear long-reach, crossbow ranged burst). `ITEM_BASE` weapons
  tagged with `wt`; `rollItem` carries it; `equipItem` sets stance from the type (melee→sword viewmodel,
  missile→bow) and logs the blurb; `meleeAttack`/`fireArrow` read `weaponProfile()` for cd/reach/dmg/stam
  (bare fists & mismatched stance fall back to sword/bow defaults). Verified in live combat: spear hits a
  target at 4.6u that a sword (reach 3.4) misses; crossbow fires & auto-switches to bow stance. (Profile
  numbers are our design; AC's exact per-type values stay research-gated.)
- ✅ **E2c. Rare item affixes** (jsc + preview verified, 0 console errors). `WEAPON_AFFIXES` (elemental
  brands fire/frost/shock/nether → element + status + ~25% bonus dmg; crit +12%; lifesteal 22%) and
  `WORN_AFFIXES` (+28 max HP "of Vigor" / +34 stam / +28 mana / +9 armor "of Warding"). `rollItem` rolls
  one affix on weapon/worn/armor drops (85% on rares, 40% on tier≥4) and appends the suffix to the name.
  `applyHit(m,base,opts)` now takes {element,brandDmg,crit,lifesteal} — applies elemMult + weak/resist
  floaters + burn/chill/stun + crit-bonus + life-steal heal; `meleeAttack` builds opts from the weapon's
  affix; `fireArrow` rides the projectile's existing element/burn/slow/stun/drain channels. Worn vital
  affixes fold into `derive()` (non-cumulative); `wornArmorV()` + armor affix; equipping worn gear calls
  `derive()`. Verified: fire-brand burns a fire-weak mosswart for boosted dmg, lifesteal heals, "of Vigor"
  raised max HP 86→114, "of Warding" stacked +9 armor.
- ✅ **E2d. Armor types** (jsc + preview verified, 0 console errors). `ARMOR_TYPES` light/medium/heavy with
  armor × speed × stamina-regen tradeoffs; worn bases tagged `at`; `rollItem` carries it; `wornArmorV()`
  scales protection by `armorMul`, `derive()` scales `player.speed` by `speedMul`, stamina regen scales by
  `stamMul`. Verified: heavy = +40% armor / −10% speed / −28% stam-regen; light = −20% armor / +6% speed /
  +25% stam-regen. (Profile numbers our design.)
- ✅ **C7. Fresnel water** (jsc + preview verified, 0 console errors). Kept MeshPhong (preserving C6 ripple
  normals + specular + shadows + FOG) but injected a fresnel term via `onBeforeCompile` before
  `<fog_fragment>`: deep/dark looking straight down, bright sky-reflection at grazing angles, fog-correct
  at distance. Verified at an ocean shore — clear depth gradient.
- ✅ **E2e. Imbue tinkering** (jsc + preview verified, 0 console errors). `IMBUE_MAT` (Jet→nether,
  Amber→fire, Diamond→shock): applying 100 units of an imbue-type salvage bag (with Weapon Tinkering)
  **brands your equipped weapon** with that element instead of adding flat damage — ties the salvage,
  affix, and weapon-type systems together. Strips any prior brand suffix, sets `weapon.affix`, renames the
  weapon; tinker-panel bag row shows "→ imbue weapon (element)". Verified: plain Steel Sword + Jet →
  "Steel Sword of the Void", lands nether hits.
- ◇ **E3. Allegiance/monarchy depth** — pass-up formula now VERIFIED (plan 6.8) and ready to build, BUT in a
  single-player game pass-up to a patron has no payoff loop. Build only if we add an **NPC-vassal allegiance
  tree** (player as patron receiving pass-up income). Current Loyalty-tier XP bonus in `gainXP` stays as the
  flavor stand-in.

## MILESTONE MMO — Multiplayer pivot  ✅ (M1–M5 done; shared world live, deploy is the user's step)
Decision (2026-06-29): going from single-player offline to a real shared-world MMORPG. This intentionally
waives the original "fully offline, zero-install" constraint for hosting (the client keeps an **offline
solo fallback**). Stack: a **dependency-free Python 3 authoritative server** (stdlib asyncio + hand-rolled
WebSocket + sqlite3) — chosen over Node because it's testable/runnable here (no Node installed) and deploys
anywhere.
- ✅ **M1. Server foundation** (`server/dereth_server.py`, +`test_client.py` harness, +README; 13/13 e2e
  checks pass). Accounts (scrypt + sqlite3), resumable session tokens, per-account persistent character
  JSON blob, chat relay, presence (join/leave), 10 Hz world snapshots. Runs with just `python3`.
- ✅ **M2. Client netcode + auth UI + remote players** (jsc + live-server browser-integration verified, 0
  console errors). Title screen gained an online **Log In / Register** panel (offline solo path preserved).
  `NET` module: WebSocket connect, `auth_ok`/`auth_err`/`snapshot`/`chat`/`system` handling, 10 Hz `input`
  send, server-side `save` (replaces localStorage when online). New accounts pick a heritage to create
  their character server-side; existing characters load from the server. **Remote players** render as
  name-tagged avatars built from snapshots (`reconcileRemotes`/`updateRemotes`: build, ground-plant, lerp,
  cull on leave; hidden inside dungeon/network instances until M3). Verified in a real browser against the
  live server: register→enter world, a simulated 2nd player appears as a remote avatar (eval-confirmed
  visible/positioned/named) and its join/leave shows in the client log.
- ✅ **M3. Server-authoritative shared world** — the overworld is now one shared, server-owned sim
  (test_client 40/40 + browser-verified, 0 console errors). Slices:
  - **M3a** shared monsters: server pool clustered at real towns, 10 Hz wander/chase AI w/ leash + capital
    safe-zones, monster melee → `dmg` events, range-checked `attack` intent (server owns mob HP/death),
    shared XP to all damagers, 8 s respawn, mobs in the snapshot. Client renders them in `monsters[]`
    (`shared:true`) so all SP combat/visuals reuse; `damageMonster` routes hits to the server.
  - **M3b/M3e** shared world bosses via a `BOSS_DEFS` table: Olthoi Queen + Bael'Zharon (apex) + 3 tinted
    Shadow Generals, global slay/respawn announcements; client renders any boss generically (scale/
    nameplate/tint). Local per-client bosses now spawn only offline.
  - **M3c** shared FFA ground loot: `roll_item` ported to Python (client item schema), kills drop gold +
    items, range-checked first-come `pickup` → `loot` grant + `drop_gone`; 90 s decay; replayed to late
    joiners.
  - **M3d** shared Incursions/world events: finite boosted wave at a town anchor with a beacon (carried in
    the snapshot so it self-heals), cleared → shared `event_reward` + spoils; timeout → fade.
- ✅ **M5. Accounts own up to 8 characters** (test_client incl. 8-slot/occupied/invalid checks +
  migration; browser-verified). New `characters` table (account/slot/name/data); login → character-select
  screen (8 slots: Play/Delete/Create, name + heritage); world identity is the active character; legacy
  single-char saves migrate to slot 0. **Position persists across relogin** (x/z/yaw saved; fresh chars
  spawn at their heritage capital).
- ✅ **Social: chat + /who + parties.** Added an in-game chat input (the client could receive but not send).
  `/who` lists online characters; **parties** (fellowships ≤6) via `/party invite|accept|leave|list` with
  party chat `/p` (server-routed to members; green `[Party]` styling).
- ◇ **M3 deferred (by design): server-authoritative player HP/death.** Would require porting the client's
  derive()/armor/heal/regen/vitae math to Python — large/risky for negligible benefit in a friendly
  homage; the server already owns mob HP/positions/combat, damage dealt to players, loot, XP, and events.
- ✅ **M4. Cloud deploy (DigitalOcean droplet, Ubuntu 24.04)** — target chosen by user. No Docker/runtime
  needed (Ubuntu 24.04 ships Python 3.12). `deploy/`: `dereth.service` (systemd, binds 127.0.0.1:8787, DB at
  /var/lib/dereth, hardened), `nginx-dereth.conf` (serves the static client + proxies `/ws`→game server,
  denies source dirs), `DEPLOY.md` (full Ubuntu 24.04 runbook: apt, service user, clone, systemd, nginx,
  certbot TLS, ufw, updates, backups), `update.sh`. Client `serverUrl()` now picks `wss://<host>/ws` on
  https (matches the nginx proxy) and `ws://<host>:8787` for dev/LAN — unit-tested for cloud/dev/LAN cases.
  *(Live deploy is the user's step on the droplet via DEPLOY.md; artifacts + client wiring done & verified.)*

## MILESTONE G — Combat depth  ◑
- ✅ **G1. Active shield block** (jsc + preview verified, 0 console errors). Hold **right-mouse** to raise
  the shield: cuts **frontal** incoming damage (45% base, +15% with Shield trained, scaling with the
  Shield skill value, cap ~80%), drains stamina per blocked blow, and halves move speed + disables sprint
  while raised. Blocks only within the front arc (rear attacks land full). `blocking` global; mousedown/up
  (button 2) + contextmenu-prevented; cleared on pointer-lock loss. Verified: 40→22 dmg blocking, 40→16
  trained, rear hit unblocked, stamina drains.
- ✅ **G2. Enemy enrage** (jsc + preview verified, 0 console errors). A non-boss creature that drops below
  30% HP enrages once: +35% move speed, +40% attack damage (melee & ranged), and a red emissive glow +
  "ENRAGED" floater. Triggered in `damageMonster` (`m.enraged`), multipliers applied at the AI chase/melee/
  projectile sites. Bosses exempt. Verified: no enrage at 50%, enrages at 28%, glow applied, fires once,
  bosses never enrage.
- ✅ **G3. Dodge dash** (jsc + preview verified, 0 console errors). Tap **Space** for a quick burst (~0.18s
  at 46u/s) in the movement direction, or backward with no input; grants 0.32s i-frames, costs 16 stamina,
  ~1.1s cooldown. `player.dashT/dashCd/dashX/dashZ`; `dodge()` bound to Space; dash displacement (collision-
  aware) + i-frames applied in `update`. Verified: i-frames set, ~8.8u travel, backward default, stamina
  cost, low-stamina & cooldown gating.

## MILESTONE F — Optional later-era / stretch content  ◇
- ✅ **F1. Void magic** — "Nether Bolt" spell (key Z, school `void`, requires Void Magic trained):
  corruption projectile, burn DoT + 35% life-drain, scales with the Void skill. Also shipped the
  **Summoning** spell ("Summon Wisp", key X, requires Summoning trained): conjures up to 2 allied wisps
  that float beside the player and fire nether bolts at nearby foes; damage/lifetime scale with the
  Summoning skill; despawn on expiry/instance-change/death. (Both close the loop on the E1 skills that
  had no behaviour yet. jsc+preview verified, 0 console errors.)
- ◇ **F2. Spell components** as collectible boosters (scarab/herb/powder/potion/talisman/taper) + Foci;
  **Augmentation/Transfer gems** for attributes. 🔎 research-gated (covered by the running pass #3).
- ◇ **F3. Outlying islands** (e.g. Aerlinthe-style) as special high-tier zones.
- ✅ **F4. Live-event flavor — Incursions** (jsc + preview verified, 0 console errors): every ~3 min a
  themed horde (Shadow Incursion / Olthoi Swarm / Undead Rising / Banderling Raid) besieges one of the
  towns nearest the player. A pulsing additive light **beacon pillar** marks the town (visible from afar)
  plus an orange compass marker and a HUD "Incursion" countdown row (town · kills/total · seconds). Clear
  all invaders before the 240s timer to **repel** it for a scaled bounty (gold + XP + 2 rolled items);
  let it expire and the horde fades. Mob HP/XP scale with player level. Events pause inside dungeons/Town
  Network and reset on new game/load. `worldEvent`/`eventCd` globals; `EVENT_TYPES`, `startEvent`,
  `eventSuccess`, `eventFail`, `endEvent`, `updateEvents` (called in `update()`).

## MILESTONE H — AC long-tail systems  ☐ (research done, all ☐ planned)
From the Round-2 gap sweep (`docs/asherons-call-longtail-gaps.md`, 2026-07-01). **Headline:** the
`SKILLS_DEF` table (index.html:457) already *lists* nearly every AC skill (Alchemy, Cooking, Fletching,
Lockpick, Healing, Salvaging, Summoning, Deception, Run, Jump, Leadership, Loyalty, Arcane Lore, Assess…)
but most are **skills-in-name-only** — the systems they drive don't exist (grep-verified:
recipe/combine/healkit/dye/colosseum/scroll/locked/keyring all 0). Effort: **S/M/L** as in the doc.

### H-A. Crafting / tradeskill loop (the biggest miss)
- ☐ **H1. Combine engine** (L) — the core AC crafting verb: drag one inventory item onto another → skill
  check vs difficulty → consume inputs, produce output (failure may consume inputs). Everything below
  rides on this. *Verify:* a test recipe (e.g. two partial salvage bags → one bigger bag) resolves via a
  skill roll; success/fail paths both fire; inventory updates.
- ☐ **H2. Alchemy** (M) — grind gems/stones → powders; brew Health/Mana potions; craft oils + Alchemy
  Gems (cast self-buffs). Turns the inert Alchemy skill live. *Verify:* combine reagents → potion; drink
  → heals; Alchemy skill gates difficulty.
- ◑ **H3. Healing skill scaling** (S) — *Healing Kits + health/mana/stamina potions already exist*
  (`ITEM_BASE` @4004, used @5760) but heal a **fixed** amount. Remaining: make kit/potion potency scale
  with the (currently-inert) Healing skill + apply a combat-state penalty. *Verify:* same kit heals more
  at higher Healing skill; heals less while in combat.
- ✅ **H4. Locked caches + Lockpick** — *already implemented*: `openLockedCache()` (@6515), pick/key
  drops (@3550), `dungeonLock`, prompt "Pick the locked cache (E)". ◇ only **Keyrings** (S) remain (a
  container that holds a key set).
- ☐ **H5. Cooking** (M) — rations/food + Beer (stamina + attribute buffs) + mushroom food; applies dye
  pots. *Verify:* cook food → eat → timed buff; Cooking skill gates.
- ☐ **H6. Fletching** (M) — craft ammo (arrows/bolts/darts); apply Alchemy oils to arrowheads → elemental
  damage. *Verify:* fletch arrows from components; oiled arrows carry an element into `fireArrow`.
- ☐ **H7. Dyeing armor** (M) — Alchemy dye pot + Cooking apply; dye plants spawn wild / grown by
  Herbalist; fail chance costs AL (−20 minor / −50 crit; metal→orange, cloth→pink). *Verify:* dye recolors
  a worn piece; fail path applies the AL penalty + wrong-color.
- ✅ **H8. Partial salvage bags** — *already implemented*: `player.salvage[mat]={units,work}` bags
  accumulate units toward 100 (units-weighted-avg workmanship) before `applyTinker` (@5820).

### H-B. Spell acquisition & economy
- ✅ **H9. Spells learned from Scrolls / quests** — *already implemented*: `player.knownSpells`, scribe
  Scroll items with **T** (@623/846), Scriveners sell leveled scrolls (@10052), casting blocked until the
  spell is learned (@6050).
- ☐ **H10. Component casting** (L full / S soft) — school Foci + level Scarab + Prismatic Tapers consumed
  per cast (soft version: tapers as a soft requirement). *Verify:* casting draws & consumes components;
  out-of-components blocks the cast. (Overlaps prior audit Mg2.)
- ◇ **H11. Spell economy** (M) — frequently-cast spells weaken / rare ones strengthen. Optional flavor.
- ◇ **H12. Level VIII scroll crafting** (M) — Quill + Mana Scarab → infused quill → +ink → +glyph → L8
  scroll. Endgame, build after H1/H9.

### H-C. World / immersion
- ☐ **H13. Portal Storms** (S) — crowded area → random teleport to town outskirts (anti-zerg; occasionally
  into danger). *Verify:* N players/mobs within radius triggers a scatter-teleport with a warning.
- ☐ **H14. Recall Contracts** (S) — inventory items granting recall to fixed spots (Aphus Lassel etc.),
  distinct from recall spells. *Verify:* using a Contract recalls to its bound location.
- ◑ **H15. Mana Stones / Portal Gems** (S) — Portal Gems (`portalgem` @5801) + recall stones already
  exist; only the Mana-Stone *refill an item's mana* mechanic is missing (mana stones currently appear
  as quest reward flavor only).

### H-D. Endgame & repeatable event content
- ☐ **H16. Colosseum arena** (M) — 18-room ticketed gauntlet w/ 1-hr timer; Ticket (5 MMD / any Rare);
  Advanced entrance 80+; clear → Vault Key → Tier-7 loot + one of five Empyrean Rings. Marquee repeatable
  endgame; reuses the Incursion (F4) wave tech. *Verify:* buy ticket → enter → clear rooms under timer →
  vault key → tiered reward.
- ☐ **H17. Full augmentation tree** (M) — beyond attr/vital gems: Jack of All Trades (+5 all skills),
  skill/spec-credit augs, XP augs, Innate-attribute reinforcements (Enduring Calm…), Critical Protection,
  Clutch of the Miser, Master of the Five Fold Path, Frenzy of the Slayer, etc., with a hard total-aug
  cap. *Verify:* applying an aug grants its effect once and counts against the cap.
- ◇ **H18. Instanced event dungeons** (L) — repeatable timed instances w/ tickets & vault keys (Colosseum
  is the archetype); the template for future monthly live content.

**H build order (impact ÷ effort), after the code-audit corrections:** H13 portal storms + H14 recall
Contracts + H3 Healing-skill scaling (quick wins) → **H1 combine engine** (the confirmed central gap) →
H2 Alchemy + H5 Cooking + H6 Fletching (make 3 inert trade skills craft their already-existing loot) →
H7 dyeing → H16 Colosseum gauntlet run → H17 augment breadth → H10/H12 component/L8 casting.
(H4 locked caches, H8 salvage bags, H9 spell scrolls, H15 portal gems, emotes = already done.)

---

## Suggested order of execution
**A (all) → C1 → C2 → B1 → B2 → B3 → C3 → A-leftovers polish → E0 (research) → E1/E2/E3 → D → F → H.**
Rationale: finish the cheap authenticity wins first (A); then the biggest visual upgrades (C1/C2) since
"improve graphics" is a standing request; interleave bestiary/lore (B) which is mostly data; do interiors
(C3) once furnishings have a lit space; gate the deep RPG systems (E) behind a second research pass so we
build them on real numbers, not guesses; camera/stretch polish (D/F) last.
