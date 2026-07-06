# AC Alignment — Remaining Gaps (2026-07-03)

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

Everything still known to diverge from retail Asheron's Call, compiled from the four-domain
audit, the ACE physics research, the manual/wiki research packs, `docs/REMAINING-WORK.md`
(other session's handoff), and deferrals from this session's passes. Ordered by player impact.

## A. Interface & presentation
1. **Radar** — AC's circular radar (upper-right, colored blips by character type, FOV wedge) vs
   the current rectangular compass strip + square minimap. The PAGE-99 manual diagram is the
   layout reference. (M)
2. **Panel layout per the manual's PAGE 99** — panel-toggle icon rail (Magic / Attributes &
   Skills / Social / Map / Options...), Examine/Selected-Item box, jewelry+container slots on
   the paperdoll arrangement. Contents exist; arrangement differs. (M)
3. **More real UI chrome** — extracted but unwired: framed-bar variants, portrait heads, panel
   corner/strut gold pieces on the big modals (sheet/inventory/spellbook). ~50 uninspected
   contact sheets in `acdata/uiscan/` likely hold the paperdoll frame, spellbook tabs, examine
   border. (S–M per piece)
4. **Creation wizard presentation (A1b)** — staged full-screen steps (Heritage → Profession →
   Attributes → Skills → Appearance → Town → Name) with heritage lore blurbs + naming lore;
   real `acchargen.json` palettes (skin/hair/eye per heritage, 51/50 hairstyles); heritage free
   skills (unlowerable); exact template attribute/skill presets; live derived-vitals readout. (M–L)

## B. Combat & magic numbers
5. **Per-body-part combat** (other session's handoff #1) — attack heights hit body-part AL
   buckets from `weenie_properties_body_part`; creature attacks roll their part's d_Val/d_Var;
   high/med/low selector HUD like retail. (M)
6. **Spell projectile speed ratios** — retail: bolt 15 / streak 45 / arc 40 / ring 2 / wall 3–4
   m/s per the weenie data; the game's per-spell speeds don't follow these ratios. Also Blast =
   3 projectiles at 90° spread, Volley = 3 parallel — current spreads differ. (S–M)
7. **Spell range formula** — `min(BaseRangeConstant + magicSkill × BaseRangeMod, 75m)` checked
   at cast; per-spell constants live in portal.dat's SpellTable (extractable). (M)
8. **Ammo strictness** — retail REQUIRED matching ammo; bow currently fires at 0 arrows
   (ammo is bonus damage only). (S)
9. **PK states & altars** — /pk flag exists, but not NPK/PKL/PK states, Altar of Bael'Zharon /
   Asheron to switch, 3-day PK keys, PK-death loot rules (50% pyreals + all rares). (M)

## C. World & content
10. **~5 CC0 monster stand-ins** — shadow, mummy, mukkir, remoran, zefir still render as modern
    Ghost/Zombie/Demon/Snake/Bat models next to 42 authentic extracts. Extract or force
    procedural. (M, extraction lane)
11. **GameEventDefDB live events** (handoff #2) — retail monthly world-event names/timing from
    portal.dat + the ACE event table onto the existing monthly framework. (M)
12. **QuestDefDB timers** (handoff #5) — real `min_Delta` lockouts (20h/3d/1wk) where a giver
    matches a retail NPC key. (S–M)
13. **Mana-stone retail semantics** (handoff #4) — drain destroys the item and stores its mana;
    charged stone refills worn items. Currently recharges the focus battery. (S–M)
14. **Facility Hub as the real landblock** — currently a synthetic portal hall, not 8A02
    geometry; Training Academy uses grounds + NPCs rather than the retail interior. (L)
15. **44 unmapped dungeons** — canon names with no landblock id (likely surface camps);
    verify individually. (S each)
16. **Chess** (handoff #6) — playable tavern boards (Game/GamePiece weenies). Low priority,
    high charm. (M)
17. **Book placement** — 898 retail books ship via scrivener shelf; place named books at their
    true locations. (S–M)
18. **Dye recipes via alchemy** — retail brewed dye pots from dye plants; currently
    vendor-bought only. (S)

## D. Systems depth
19. **Fellowship level-proportional XP mode** — equal-split-with-bonus shipped; the
    within-10-levels proportional variant needs server-side level tracking. (S–M)
20. **Secure trade: pyreal offering** — items trade; coin amounts don't yet. (S)
21. **Vitals formula purity** — vital-raising track shipped, but max HP/stam/mana keep the
    game's tuned base (+level terms) rather than pure End/2 · End · Self. Changing it means
    rebalancing every creature — deliberate deferral. (L, risky)
22. **Enlightenment prerequisites** — retail gates (L275, Society Master, maxed lum auras,
    <5 enlightenments); the game gates at L100 with no society requirement. (S)
23. **Allegiance grand-vassal pass-up** — direct pass-up shipped (other client); the 0–10%
    grand-vassal→grand-patron trickle is not modeled. (S–M)

## E. Blocked / external
24. **client_highres.dat textures** — crisper terrain/creature/armor/icon art. The dat is NOT
    in acdata/; blocked until the user supplies it.
25. **client_local_English.dat string tables** — present locally, no extractor yet (UI strings,
    item descriptions beyond the emote/book packs). (M)
26. **Non-PCM music track** — one MP3-format 0x55 resource skipped by the music exporter. (S)

## Recently closed (do not re-open)
Human NPCs on the player's model w/ real AC bodies · lifestones (single crystal/dais/clearing) ·
road engineering (detours/bridges/visible beds) · real UI vitals bars + chrome · secure trade ·
AC default keys (Q autorun, arrow-turn, X/Z/C) · packs as side-pack containers + icon grid ·
portal purple-mist exterior + portal-space tube · ACE physics (ballistic arrows, arc lobs, jump
charge, fall damage) · Training Academy + greeter chains + starter kit + retail quest rewards ·
270/52 creation numbers + Specialize-at-creation · per-item tinkering · monster signature
attacks · allegiance pyramid + AC corpse death (other client) · saga years 1–10.
