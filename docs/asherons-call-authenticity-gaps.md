# Asheron's Call — Authenticity Gap Analysis

**Compiled:** 2026-07-01 · **Audited build:** `index.html` (~11,034 lines) + `server/dereth_server.py`
**Method:** six parallel domain audits, each cross-referencing authentic AC mechanics
(acpedia.org / asheron.fandom.com / Drunkapedia mirror) against the current implementation.
Sources block automated fetch (403/402), so figures come from indexed wiki extracts + the
Drunkapedia mirror; recall-spell level reqs and per-town rosters should be browser-verified
before hard-coding. Figures reflect **end-of-retail AC** (post-Throne of Destiny / Master of Design).

Legend — Effort: **S** small (data/tuning) · **M** medium (new subsystem) · **L** large (structural).

> **What the game already gets right** (do not touch): six attributes by name; skill tiers
> Untrained/Trained/Specialized with 52 starting credits; Vitae death penalty (+5%/death, 40% cap,
> XP-recovered); six magic schools with correct School-of-the-Arm/Heart/Left/Right/Shadow subtitles;
> 8 spell levels (Incantation at VIII); 7 damage types + elemental affinity; full War geometry
> (bolt/streak/blast/volley/ring/wall/arc); CE attribute+skill buffs, Life heals/protection/vuln/dispel,
> Item weapon buffs, Void nether lines; foci lend skill to all schools; ~51 towns at authentic coords;
> ~230 canonical dungeons; Town Network + Facility Hub (level-gated wings); capital/Foundry/Residential
> portal lines; per-town lifestones + bind/recall; day/night arc; dynamic rain; seasons; region+distance
> monster tiers; Direlands as deadliest frontier; workmanship-per-tier salvage; material-named loot;
> imbue-as-weapon-brand; trade notes; augmentation gems; three correctly-named societies; housing tiers;
> arc/quest naming; shields as off-hand AL with active frontal block; heavy/medium/light armor tradeoff.

---

## Cross-cutting themes (the biggest structural misses)

These recur across domains and, if addressed, would move authenticity the most:

1. **No "why is this drop good" loop.** Loot has no wield/level/skill requirement and no mutated
   item-spells/mana/spellcraft — AC's entire itemization gate is absent. (Items #1/#2)
2. **Attacks can't miss and weapons have no damage type.** No attack-skill-vs-defense evade contest;
   plain weapons deal typeless damage; no power/accuracy bar, no attack heights. (Combat #1–#4)
3. **Progression model inverted.** Attributes bought free with level-points (no 100 cap, no 275 level
   cap) instead of XP-bought against caps; vitals not independently raisable. (Character #1–#3)
4. **No social interdependence.** Allegiance is a solo +15% XP flag — no patron/vassal tree, no
   XP pass-up, no monarchy, no PvP, no player trade, no real fellowship XP-share. (Social #1–#8)
5. **No post-cap endgame.** Luminance / Aetheria (slots+surges) / Enlightenment are absent or
   reduced to flat stat gems. (Character #5, Items #5, Social #12)
6. **No corpse / item-loss on death** — AC's most iconic risk mechanic is gone. (Social #4)

---

## 1. Character progression

Authoritative numbers (Drunkapedia/acpedia): creation pool **270** distributable (floor 10 each);
innate cap **100**, **+50** from augmentation gems, ~**290** fully buffed; Health = **End/2**,
Stamina = **End** (1:1), Mana = **Self** (1:1), each separately XP-raisable (secondary track,
cheaper: pt1 = 73 XP); primaries pt1 = 110 XP; trained skills raise to **208**, specialized to
**226** (incl. +10 inherent bonus), spec **credit** cap **70**; ~**102** lifetime skill credits;
level cap **275** (~191.2 B total XP).

| # | Gap | Authentic | Game now | Effort |
|---|-----|-----------|----------|--------|
| C1 | Attributes bought free with level points, no caps | XP-bought on steep curve; innate cap 100 (+50 aug) | `+4 skillPts`/level → +1 attr free, no cap (`derive`/sheet ~6129) | M |
| C2 | No max level; invented XP curve | Hard cap **275** | `xpForLevel=450*l^1.55`, uncapped (556) | M |
| C3 | Vitals not separately raisable; formulas inverted | Health=End/2, Stam=End, Mana=Self + own XP track | `mhp=60+End*2+lvl*6` etc. (541–543), no vital XP | M |
| C4 | Only 3 heritages; no Viamontian, no templates, no creation pool | 4 heritages, class templates, 270-pt distribution, 4 choices | 3 presets (560), fixed tiny bonus (10855) | L |
| C5 | **No Luminance / Aetheria / Enlightenment endgame** | Luminance auras; Aetheria slots+surges; Enlightenment reset | none (Aetheria = 3 flat gems 5097) | L |
| C6 | Augment = random loot gem, no caps/quest | Asheron's Castle quest set; +10 attr augs; total-aug limit | random +25 vital/+1 credit, no cap (5091) | M |
| C7 | Attribute→governance descriptions oversimplified | Coord=melee+missile acc; Quick=defense+run | single tags "Attack speed"/"Move speed" (508) | S |
| C8 | Specialized = flat +10; spec bonus should scale | +10 inherent, raises to 226, cheaper/pt | flat +10 (487), 70-credit cap ✓ | S |
| C9 | Skill-credit growth invented (level trickle) | 52 start + level milestones + **quest** credits (~102) | `52+min(lvl-1,46)+…` (492) | S |
| C10 | Non-AC trainable Assess skills; tinker categorization | Assess is built-in later; tinker specced via aug | assesscreature/person trainable (467) | S |

---

## 2. Magic

Already unusually faithful. Remaining gaps:

| # | Gap | Authentic | Game now | Effort |
|---|-----|-----------|----------|--------|
| Mg1 | **Buffs/debuffs are self-only** — no "Other" targeting | every CE/Item/Life buff has Self + Other form; buffing fellows = the mage role | only Heal Self / attr-buffs route to allies (5666); Item/skill/protection can't target others | M |
| Mg2 | Components are vendor trash, never consumed | scarab/herb/powder/potion/talisman/taper formula; wrong = fizzle | `SPELL_COMPONENTS` sellable only; only taper consumed as a damage multiplier (5566) | L (full) / S (tapers as soft req) |
| Mg3 | Recall spell set mostly missing | Primary/Secondary Portal Tie + Recall, Portal Recall (last portal), Portal Sending, Aphus Lassel/town recalls | only Lifestone + non-authentic "Sanctuary Recall" (647) | M |
| Mg4 | Item Enchant can't target a specific item; 7 elemental **Banes** missing | Banes (Flame/Frost/Acid/Lightning/Blade/Bludgeon/Pierce), impen line applied to items | personal timed buffs on `player.itemBuffs`; only generic Impen | M |
| Mg5 | Mana Conversion / fizzle formulas ad hoc | MC = 25 diff × spell level, random save; fizzle diff 50/100/150…/level | flat linear cost cut vs mana skill (5631); ad hoc fizzle (5637) | M |
| Mg6 | Void lacks DoT corruption/curse lines | nether DoT + curses | 3 direct-damage lines only (656) | M |
| Mg7 | Non-authentic names / no Spellcraft on items / no Arcane-Lore item gate | Spellcraft stat; Arcane Lore activates cast-on-item | scrolls gate on school skill only (5608) | S |

---

## 3. Combat

Biggest divergences of any domain — AC's tactical feel is largely absent.

| # | Gap | Authentic | Game now | Effort |
|---|-----|-----------|----------|--------|
| Cb1 | **No power/accuracy bar (charge attack)** — the signature AC feel | slider trades speed↔power (~50–150%), mid-bar accuracy | fixed-damage click swing + flat cooldown (5864) | L |
| Cb2 | **No attack heights** (high/med/low) | three targeting zones vs armor coverage | none | M |
| Cb3 | **No evade / to-hit contest — attacks never miss** | attack skill vs defense skill → misses/evades | 100% hit in reach cone; defense only softens dmg (5504) | M |
| Cb4 | **Player weapons have no damage type** | slash/pierce/bludgeon per weapon vs per-element armor mod | element only on branded/magic hits; plain weapon = typeless (3126) | M |
| Cb5 | No Damage Rating / Crit-Damage Rating / Crushing Blow / Sneak Attack | DR multiplier, CDR, Crushing Blow, +20 DR from behind | none | M |
| Cb6 | Crit rate/mult off | 10% base, ×2, imbue-scalable | 7% base + Coord scaling, flat ×2 (3138) | S |
| Cb7 | Weapon-skill tree vestigial | old Heavy/Light/Finesse *or* new per-weapon skills, mapped to weapon | `bestMeleeEff` takes best of all → distinction meaningless (489) | M |
| Cb8 | Dual wield has no mechanic | off-hand extra swings | skill defined, off-hand = shield only, single swing (441) | M |
| Cb9 | Stamina cost flat; free swings at low stam | cost from weapon+shield burden+power bar; hit/miss drains stam | flat per-weapon; low-stam = free full swing (5869) | S |
| Cb10 | **No burden/encumbrance** | Burden Units vs Str capacity → stam/movement | none (`grep burden` empty) | L |
| Cb11 | Missile accuracy range-independent | hit chance degrades with range | fixed dmg/speed projectile (5833) | S |
| Cb12 | Armor mod not per-element % of AL | each piece resist % per element | single scalar AL + separate ward stack (3108) | M |
| Cb13 | Damage not rolled per-swing | top-end + variance rolled each hit | deterministic per weapon (roll at creation) | S |

---

## 4. World & exploration

Strong world layer. Gaps:

| # | Gap | Authentic | Game now | Effort |
|---|-----|-----------|----------|--------|
| W1 | Only Holtburg bindable; lifestones only in towns | bind at ANY lifestone incl. wilderness | `addCity` binds Holtburg (2934); town-only stones | S |
| W2 | **No fall damage** | scales with drop height, can kill | jump/gravity exist, landing free | S |
| W3 | Deep water = invisible wall, not drowning | no swimming; over-head water = sink+drown, tied to burden | movement blocked at depth (6168) | M |
| W4 | Weather lacks snow/fog states, not region-aware | rain/snow/fog/storm, region-tied | clear/cloudy/rain global (1464); snow=tint only | M |
| W5 | No level-gated overworld portals | town/dungeon portals gated (Eastwatch 80+, Olthoi 40/60/80/100) | only Facility Hub gated (10162) | S |
| W6 | Recall suite incomplete | Portal Recall / Portal Tie (last-portal loop) | Lifestone + Sanctuary + one random gem | M |
| W7 | No in-dungeon portals / portal-space / multi-level dungeons | "Ye Olde…" chains, drop portals, linked sub-instances | single flat instance + chest (10086) | L |
| W8 | Square 16000² map, ocean rim | irregular continent + named regions (Osteth/Aphus Lassel/Linvak) + real islands | distance-to-capital regions; islands only as coords | L |
| W9 | No ambient/non-hostile fauna | rabbits/sheep/cows/penguins populate world | only hostiles spawn (2982) | M |
| W10 | Roads only connect the 3 capitals | organic web linking neighbors | complete graph of capitals; towns get decorative cross (1076) | M |
| W11 | Weather/season not latitude-tied | frozen north snowy, desert arid | global (3295) | M |
| W12 | Terrain = trig noise, no authored landmarks/barriers | landblock heightmap, named mountains gate travel | 3 sine layers + bump (1217) | L |

---

## 5. Items & economy

Broad nominal coverage; mechanics simplified.

| # | Gap | Authentic | Game now | Effort |
|---|-----|-----------|----------|--------|
| I1 | **No wield requirements on loot** — AC's central gate | every drop rolls skill/level req; tinker/imbue raise it | items roll v/work/mat/tier/affix only, instantly equippable (5267) | M |
| I2 | **Loot carries no item-spells/mana/spellcraft** | mutated cantrips (Minor/Major/Epic/Legendary), mana, AL mutation | cantrips only as fixed jewelry bonuses; no mana/spellcraft (5230) | L |
| I3 | Salvage/tinker collapsed | 8 tink skills; 10-tink cap; imbue-once; units ≤ workmanship; failure destroys | 2 tink skills (5441); no caps; tinks stack on *player* not item; salvage can exceed workmanship (5432) | M |
| I4 | Vendor economy flat | buy/sell markup vs mutated value; rares = 50k; limited vendor pyreals | flat `price*0.4` sell (9426); hardcoded buy; infinite money | S |
| I5 | Aetheria = 3 flat gems | slots at L75/150/225, leveled, set bonus + surge procs | permanent stat gems, no slots/level/surge (5097) | L |
| I6 | No rares system | ~1/2500 pity-timer table, 6 tiers, MMD exchange | named items via normal rolls (3705) | M |
| I7 | Trophies = sell-junk | creature-part turn-ins for quests | sell for pyreals only (5387) | S |
| I8 | Material has no bearing on stats | material sets tink bonus + imbue element + value | cosmetic name+tint+salvage class only (5311) | S |
| I9 | Burden coarse/not per-item | per-item Burden vs Str capacity, overload penalties | slot-lookup burden (1276); only affects jump | S |
| I10 | Workmanship/tier range compressed | 8 loot tiers, finer bands | 5 tiers → work 1–10 (3668) | S |
| I11 | Missing MMD currency, coin burden | Pyreal ✓ + Trade Notes ✓ + MMD, coin stacks weigh | pyreal + trade notes only | S |
| I12 | Packs merge into one satchel | 102-slot main + separate side packs you open | 12 + stitched cap ~100 (5112) | S |

---

## 6. Social / quests / endgame

Captures flavor, misses nearly all interdependence.

| # | Gap | Authentic | Game now | Effort |
|---|-----|-----------|----------|--------|
| S1 | **Allegiance = solo XP flag, no patron/vassal tree** | swear to a player; XP pass-up via Loyalty/Leadership (`Passup%=Gen%×Rec%/100`) | `player.patron` boolean → +15% XP (6123); no tree; server has none | L |
| S2 | No monarchy / no allegiance chat channel | Monarch atop tree; `/allegiance` channel; villa/mansion access | none; chat = say/tell/party only (10686) | M/L |
| S3 | **No PKing / PvP** | PK/PKL altars, Darktide, arenas, corpse-loot | none; war magic never hits players (6263) | L |
| S4 | **Death drops no items, no corpse** | corpse holds `ceil(level/10)` items + ½ pyreals, decays | loses 10% unspent XP + Vitae only (5530) | M |
| S5 | Vitae doesn't penalize skills | reduces vitals AND all skills | vitals+damage only (550) | S |
| S6 | Society: rep-number, wrong ranks, no ribbon/test | Initiate→Adept→Knight→Lord→Master; ribbons 95/295/595/995 + Test quests; halls | Adherent/Sworn/… + rep counter (7614); strongholds = place-names | M |
| S7 | Fellowship cap 6, flat XP, no modes | cap 9; equal/proportional by level spread; equal-share bonus; over-level penalty | `PARTY_MAX=6`; full XP to all in range (server 671/287) | M |
| S8 | No player-to-player trade | secure trade window | NPC vendors + trade notes only | M |
| S9 | Quests = short repeatable bounties; no flag/timer persistence; no live story | flags+timers (20h–7d, server-side); great quests; serialized monthly story; Town Criers | 2–10 min client cooldowns (7599); static arcs; no Criers/story engine | L (story) / M (timers+Criers) |
| S10 | Titles single-slot; `/who` bare | many stackable, selectable; `/who` shows titles/levels | one overwritten title (9405); society title not wearable | S |
| S11 | Housing: gold-only, no gates/maintenance/hooks | Cottage L20 / Villa L35+Writs / Mansion L50+Rank6-monarch+10M+20Writs; monthly upkeep; hooks; allegiance access; 15-day age | pyreal-only tiers (9295), no gate/maintenance/hooks/allegiance | M |
| S12 | No luminance / Aetheria endgame | luminance auras + Aetheria slots (ToD/MoD era) | none; endgame = boss kills + legendaries | L |

**Housing reference (for S11):** Apartment L20 (1 chest, 3 hooks, upkeep/3mo) · Cottage L20
(1 Writ + 300k + trophy, 1–2 chests, 50 hooks/25 active, 30k/mo) · Villa L35 (5 Writs + 2M + trophy,
5 chests, 70 hooks/50 active) · Mansion L50 + Rank-6 Monarch (20 Writs + 10M + trophies, 5 chests +
basement, 100+ hooks, 1M + 10 Writs/mo; lost if rank drops to 5). Chest = 26 loose items + 1 pack.
Society halls: Celestial Hand → Hebian-to, Eldrytch Web → Cragstone, Radiant Blood → Zaikhal.

---

## Suggested build order (by impact ÷ effort)

**Quick authenticity wins (S, do first):** C7 attr descriptions · C8/C9 spec+credit tuning ·
S5 Vitae→skills · S6 society ranks (Initiate/Adept naming + ribbons) · S10 titles+/who ·
W1 wilderness lifestones · W2 fall damage · W5 level-gated portals · Cb6 crit 10%/×2 ·
I4 vendor markup · I7 trophy turn-ins · Mg7 spellcraft/Arcane-Lore item gate.

**High-impact subsystems (M):** I1 wield requirements · Cb3 evade contest · Cb4 weapon damage types ·
Mg1 buff/debuff "Other" targeting · S4 death corpse + item drop · S7 fellowship (cap 9 + XP modes) ·
C1 attribute XP-cost + 100 cap · C3 vital raising · W3 drowning · W4 snow/fog weather.

**Structural / marquee (L):** S1 allegiance patron/vassal tree + pass-up · C5/S12 Luminance+Aetheria+
Enlightenment endgame · S3 PvP · Cb1 power/accuracy bar · I2 loot item-spell mutation · S9 live-story engine.
