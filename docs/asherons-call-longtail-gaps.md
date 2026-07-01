# Asheron's Call — Long-Tail Gap Analysis (Round 2)

**Compiled:** 2026-07-01 · **Method:** fresh internet sweep (acpedia / asheron.fandom / RPGFan /
Massively OP / Wikipedia) specifically hunting for AC systems the original six-domain audit
(`asherons-call-authenticity-gaps.md`) did **not** cover, then each candidate verified against the
actual code in `index.html` via grep before being called a gap.

**Headline finding:** the game's `SKILLS_DEF` table (index.html:457) already *lists* nearly every AC
skill — Alchemy, Cooking, Fletching, Lockpick, Healing, Salvaging, Two Handed, Dual Wield, Dirty
Fighting, Recklessness, Sneak Attack, Summoning, Deception, Run, Jump, Leadership, Loyalty, Arcane
Lore, Assess Creature/Person. The **trade-crafting** subset (Alchemy/Cooking/Fletching) is
skills-in-name-only: their outputs (potions, food, ammo) exist as *loot data* but there is no way to
*craft* them, because **no item-combine engine exists** (`recipe` 0, `combine` 0, no craft panel).

> **CORRECTION (verified against code 2026-07-01, after a grep-methodology bug):** the first draft of
> this doc claimed Healing Kits, Locked Caches/Lockpick, and Spell Scrolls were *also* missing. That was
> wrong — a grep using `\|` under `grep -E` matched a literal pipe and returned false zeros. On direct
> code inspection **all three are already implemented**: tiered Healing Kits + health/mana/stamina
> potions (`ITEM_BASE` @4004, used @5760); Locked Caches with the Lockpick skill, pick/key drops and
> `openLockedCache()` (@6515, prompt "Pick the locked cache (E)"); and spells learned from Scrolls into
> `player.knownSpells` (scribe with **T** @623/846, Scriveners sell leveled scrolls @10052). Mana stones
> exist as quest rewards. Emotes are implemented (`EMOTE_CMDS` @11391). Those rows below are struck
> through / re-scoped to the small remaining sub-gaps.

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
| L5 | ~~Healing skill + Healing Kits~~ **DONE — remaining: skill-scaling** | kit heal amount scales with Healing skill; harder (or blocked) in combat state | tiered kits + potions exist (@4004/5760) but heal a **fixed** amount — Healing skill does nothing; no combat penalty | S |
| L6 | ~~Lockpick + locked chests/doors~~ **DONE — remaining: Keyrings only** | Keyrings carved with Intricate Carving Tool hold key sets | Locked Caches + Lockpick + pick/key drops fully implemented (`openLockedCache` @6515); only Keyrings absent | S |
| L7 | **Dyeing armor** | Alchemy makes a dye pot (crush dye plant w/ Mortar & Pestle + Neutral Balm), Cooking applies it; dye plants spawn wild or grown by Herbalist; fail chance costs AL (−20 minor / −50 crit; metal→orange, cloth→pink) | no dyeing; armor colour fixed | M |
| L8 | **Salvage → 100-unit bags → tinker** | salvaging yields *partial* bags; combine partials up to a full 100-unit bag; then tinker | salvaging simplified; no partial-bag combine (see prior audit I3) | S |

Cross-dependencies to preserve if built: **dyeing = Alchemy + Cooking**; **fletching oils = Alchemy**;
**tinkering consumes salvage bags**. This interconnection is the point — it's what made AC crafting an
economy rather than a menu.

---

## B. Spell acquisition & the spell economy

| # | System | Authentic AC | Game now | Effort |
|---|--------|--------------|----------|--------|
| Sp1 | ~~Spells learned from Scrolls / quests~~ **DONE** | — | fully implemented: `player.knownSpells`, scribe Scroll items with **T** (@623/846), Scriveners sell leveled scrolls (@10052), cast blocked until learned | — |
| Sp2 | **Component casting formula** | modern: school Foci + level Scarab + Prismatic Tapers; old: Scarab→Taper→Herb→Taper→Powder→Potion→Talisman (incl. 2 *personal* tapers) | components exist as vendor trash, only tapers consumed (prior audit Mg2) | L(full)/S(soft) |
| Sp3 | **Spell economy** | frequently-cast spells lose power / rarely-cast spells gain power; component scarcity as sink | no dynamic spell power | M |
| Sp4 | **Level VIII scroll crafting** | Quill + Mana Scarab → infused quill → +ink → +glyph → L8 scroll (Ancient Powers event) | none | M |

---

## C. World / immersion mechanics

| # | System | Authentic AC | Game now | Effort |
|---|--------|--------------|----------|--------|
| Wl1 | **Portal Storms** | when an area gets too crowded, players are randomly teleported to the town outskirts (anti-lag / anti-zerg) — occasionally into danger | none (`portal storm` = 0) | S |
| Wl2 | **Recall Contracts** | inventory items granting recall to fixed spots (e.g. Aphus Lassel Contract), distinct from recall spells | recall is spell/gem only; no Contract items | S |
| Wl3 | Mana Stones / Portal Gems | Mana Stones drain into / refill an item's mana; single-use Portal/recall Gems | Portal Gems (`portalgem` @5801) + recall stones exist; mana stones appear only as quest rewards, no refill mechanic | S |

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

**Genuinely-missing, ordered (after the corrections above):**
**Quick wins (S):** Wl1 portal storms · Wl2 recall Contracts · L5 make Healing skill scale kit potency +
combat penalty · L6-remainder Keyrings.
**High-impact subsystems (M):** **L1 combine engine** — the confirmed central gap — wiring L2 Alchemy
(potions), L5-craft (kits), L3 Cooking (food) and L6 Fletching (arrows) so 3 inert trade skills produce
their already-existing loot items · L7 Dyeing (rides on the combine engine) · E1 Colosseum **gauntlet
run** (the location dungeon exists; add the 18-room ticketed timer + Empyrean Ring reward) · E2
augmentation-tree breadth.
**Structural (L):** full component casting (Sp2) · H18 instanced event-dungeon template.
**Already done (do NOT rebuild):** Healing Kits & potions · Locked Caches + Lockpick · Spell Scrolls +
learning · Portal Gems/recall stones · Emotes · partial salvage bags (bags already accumulate to 100).

**Sources:** acpedia.org (Crafting, Skills, Healing, Salvaging, Recall Spells), asheron.fandom.com
(Alchemy, Fletching, Dyeing, Colosseum, Portal Storm, Jack of All Trades, Spell Research, Emotes),
RPGFan & Massively OP retrospectives, en.wikipedia.org/wiki/Asheron's_Call.
</content>
</invoke>
