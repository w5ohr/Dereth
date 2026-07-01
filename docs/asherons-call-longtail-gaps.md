# Asheron's Call — Long-Tail Gap Analysis (Round 2)

**Compiled:** 2026-07-01 · **Method:** fresh internet sweep (acpedia / asheron.fandom / RPGFan /
Massively OP / Wikipedia) specifically hunting for AC systems the original six-domain audit
(`asherons-call-authenticity-gaps.md`) did **not** cover, then each candidate verified against the
actual code in `index.html` via grep before being called a gap.

**Headline finding:** the game's `SKILLS_DEF` table (index.html:457) already *lists* nearly every AC
skill — Alchemy, Cooking, Fletching, Lockpick, Healing, Salvaging, Two Handed, Dual Wield, Dirty
Fighting, Recklessness, Sneak Attack, Summoning, Deception, Run, Jump, Leadership, Loyalty, Arcane
Lore, Assess Creature/Person. **But the gameplay systems those skills drive are almost entirely
absent.** They are skills-in-name-only: you can train them, they do nothing. Verified by grep —
`recipe` 0, `combine` 0, `healkit` 0, `dye` 0, `portal storm` 0, `colosseum/arena` 0, `keyring` 0,
`scroll`/`learnSpell` 0, `locked`/`pickLock` 0, `craftArrow` 0. (Emotes, by contrast, ARE implemented
— `EMOTE_CMDS` at 11391: wave/cheer/dance/bow/laugh/point/salute/flex/kneel/clap — so not a gap.)

Legend — Effort: **S** small · **M** medium subsystem · **L** large structural.

---

## A. The crafting / tradeskill loop — the biggest uncaptured system

AC's crafting is an **item-on-item "combine" engine**: drag one item onto another (a tool/reagent onto
a target), the game runs a skill check against a difficulty, and on success consumes inputs and yields
an output (on failure it may destroy inputs). Four professions interlock — outputs of one feed another.
None of this combine engine exists in the game (`recipe`/`combine` = 0 hits).

| # | System | Authentic AC | Game now | Effort |
|---|--------|--------------|----------|--------|
| L1 | **Combine engine itself** | drag item→item → skill roll vs difficulty → consume/produce; failure can destroy | no combine action exists at all | L |
| L2 | **Alchemy** | grind gems/stones→powders; brew Health/Mana potions; craft oils, Trade Elixirs, Alchemy Gems (cast self-buffs); make dye pots | skill trains, zero effect | M |
| L3 | **Cooking** | rations & food, Beer (big stamina + attribute buffs), Tiriun Mushroom food; applies dye pots to armor | skill trains, zero effect | M |
| L4 | **Fletching** | craft Ammo for bows/crossbows/atlatls; apply alchemy oils to arrowheads → elemental damage + more dmg | arrows are just loot; skill inert | M |
| L5 | **Healing skill + Healing Kits** | consumable kit, double-click→target; difficulty = missing-HP×2, harder in combat state; kits from loot/Healers/quests/craft; also health/mana potions | Healing skill inert; no kits, no potions-as-consumable | M |
| L6 | **Lockpick + locked chests/doors + Keyrings** | locked reward chests & doors need Lockpick (or a key); Keyrings carved with Intricate Carving Tool hold key sets | no locked containers; reward chest opens freely | M |
| L7 | **Dyeing armor** | Alchemy makes a dye pot (crush dye plant w/ Mortar & Pestle + Neutral Balm), Cooking applies it; dye plants spawn wild or grown by Herbalist; fail chance costs AL (−20 minor / −50 crit; metal→orange, cloth→pink) | no dyeing; armor colour fixed | M |
| L8 | **Salvage → 100-unit bags → tinker** | salvaging yields *partial* bags; combine partials up to a full 100-unit bag; then tinker | salvaging simplified; no partial-bag combine (see prior audit I3) | S |

Cross-dependencies to preserve if built: **dyeing = Alchemy + Cooking**; **fletching oils = Alchemy**;
**tinkering consumes salvage bags**. This interconnection is the point — it's what made AC crafting an
economy rather than a menu.

---

## B. Spell acquisition & the spell economy

| # | System | Authentic AC | Game now | Effort |
|---|--------|--------------|----------|--------|
| Sp1 | **Spells learned from Scrolls / quests** | you don't auto-know spells — learn them from Scrolls (creature loot, Scriveners, Steel Chests) or quest rewards | all spells effectively available; no scroll items or learn step (`scroll` = 0) | M |
| Sp2 | **Component casting formula** | modern: school Foci + level Scarab + Prismatic Tapers; old: Scarab→Taper→Herb→Taper→Powder→Potion→Talisman (incl. 2 *personal* tapers) | components exist as vendor trash, only tapers consumed (prior audit Mg2) | L(full)/S(soft) |
| Sp3 | **Spell economy** | frequently-cast spells lose power / rarely-cast spells gain power; component scarcity as sink | no dynamic spell power | M |
| Sp4 | **Level VIII scroll crafting** | Quill + Mana Scarab → infused quill → +ink → +glyph → L8 scroll (Ancient Powers event) | none | M |

---

## C. World / immersion mechanics

| # | System | Authentic AC | Game now | Effort |
|---|--------|--------------|----------|--------|
| Wl1 | **Portal Storms** | when an area gets too crowded, players are randomly teleported to the town outskirts (anti-lag / anti-zerg) — occasionally into danger | none (`portal storm` = 0) | S |
| Wl2 | **Recall Contracts** | inventory items granting recall to fixed spots (e.g. Aphus Lassel Contract), distinct from recall spells | recall is spell/gem only; no Contract items | S |
| Wl3 | **Mana Stones / Portal Gems** | Mana Stones drain into / refill an item's mana; single-use Portal/recall Gems | no mana-item economy; gems are stat buffs only | S |

---

## D. Endgame & repeatable event content

| # | System | Authentic AC | Game now | Effort |
|---|--------|--------------|----------|--------|
| E1 | **Colosseum arena** | 18-room gauntlet W of Yanshi, 1-hr timer; Ticket (5 MMD / any Rare); Advanced entrance 80+; clear → Vault Key → Tier-7 loot + one of five Empyrean Rings | no arena / gauntlet content (`colosseum`/`arena` = 0) | M |
| E2 | **Full augmentation tree** | beyond attr/vital gems: Jack of All Trades (+5 all skills), skill/spec-credit augs, XP augs, Innate-attribute reinforcements (Enduring Calm…), Critical Protection, Clutch of the Miser, Master of the Five Fold Path, Frenzy of the Slayer, Archmage's Endurance, Siraluun's Blessing; hard total-aug cap | a handful implemented (prior audit C6); most missing, no cap | M |
| E3 | **Instanced event dungeons generally** | repeatable timed instances w/ tickets & vault keys (Colosseum is the archetype) — the template for monthly live content | static single-instance dungeons + chest | L |

---

## E. Already-covered — do NOT re-file

These were flagged here during the sweep but are already in the prior audit or already implemented, so
they are **not** new gaps: emotes (implemented), secure player trade (S8), fellowship XP modes (S7),
allegiance/monarchy tree (S1/S2), PvP (S3), corpse/item-loss (S4), tinkering skill split (I3), rares +
MMD (I6/I11), Aetheria slots/surges (I5), Luminance/Enlightenment (C5/S12), recall spell suite (Mg3),
weapon damage types / to-hit / power bar (Cb1–Cb4), fall damage & weather & ambient fauna (W2/W4/W9).

---

## Suggested build order for Round-2 gaps (impact ÷ effort)

**Quick wins (S):** Wl1 portal storms · Wl2 recall Contracts · L8 partial salvage bags.
**High-impact subsystems (M):** L1+L2+L5 a minimal **combine engine** wiring Alchemy (potions) →
Healing (kits) as the first vertical slice — it makes 5 dead skills real at once · L6 locked chests +
Lockpick (turns Lockpick live and adds dungeon texture) · Sp1 spell scrolls (makes spell acquisition a
loop) · E1 Colosseum (marquee repeatable endgame) · E2 augmentation tree.
**Structural (L):** full component casting (Sp2) · L3/L4/L7 the rest of the crafting web (cooking/
fletching/dyeing) once the combine engine exists.

**Sources:** acpedia.org (Crafting, Skills, Healing, Salvaging, Recall Spells), asheron.fandom.com
(Alchemy, Fletching, Dyeing, Colosseum, Portal Storm, Jack of All Trades, Spell Research, Emotes),
RPGFan & Massively OP retrospectives, en.wikipedia.org/wiki/Asheron's_Call.
</content>
</invoke>
