# Item Fidelity Audit — Dereth vs. real Asheron's Call

**Date:** 2026-07-09 · **Method:** local data + ACE-World source + a 49-agent web-verification
sweep (acpedia.org / asheron.fandom.com). Covers all catalog + hand-authored items.

## TL;DR

- **The retail catalog (`assets/acitems.json`, 4,338 items) is faithful.** It is extracted from the
  **ACE-World** database (the AC-emulator's retail recreation) via `tools/ace_item_export.py`, and
  `acItemize()` stamps its exact specs onto any matching item. ~65% of gear drops are real retail items.
  A 9-item spot-check vs acpedia scored **7/9 exact** (matching *pre-Master-of-Arms* retail — a valid
  canonical baseline). Two catalog flags to verify against the ACE source SQL:
  - **`staff` — `dt:1` (Slashing) is wrong**; every AC staff is **Bludgeoning (`dt:4`)** (the light
    burden 50 also suggests this entry is really the *caster* staff mis-typed as a melee weapon).
  - **`club` — value 100 / burden 350** vs acpedia's **260 / 675** (damage/type/speed are correct).

- **The hand-authored named items (`NAMED_ITEMS`, 1,353) are the problem.** The full sweep verified
  1,317/1,353 and **confirmed ~264 discrepancies (~20%)**. This is overwhelmingly a **fabrication**
  problem, not a mislabeling one: **~251 (~95% of findings) are items that never existed in retail AC.**

## Where the fabrications come from

| Source | Count | What |
|--------|-------|------|
| **6 invented equipment "lines"** | ~129 | Real AC *names* that were **armor-only or ammo-only**, stamped across every weapon/armor/jewelry slot: **Gelidite** (armor set → 19 weapons), **Umbral** (rift event → 27), **Hallowed** (nonexistent → 27), **Radiant** (Radiant *Blood* society armor → 28 weapons + bare armor), **Singularity** (only the real *Caul* exists → 26), **Prismatic** (ammunition only → 2 melee) |
| **Generic invented loot** | ~80 | Proper-name loot that doesn't follow AC's `<material> <type>` convention: Emberfang, Voidpiercer, "…of Ruin/of the Adept/of the Titan" gear, etc. |
| **Cross-franchise borrows** | ~14 | Names lifted from other games — **Ashbringer, Frostmourn(e), Glacius, Reaper of Souls, Riftmaker, Rimeclaw, Earthshaker, Dawnbreaker, Stormpike, Frostbrand, Frostreaver**, plus Worldcleaver/Kingslayer |
| **Lore-hijacking fabrications** | ~15 | Famous AC proper nouns on items that don't exist — **Bael'Zharon's Hope** (real: *Sword of Lost Hope*), **Sword of Jojii** (real: *Koji's Sword*), **Oswald's Nightmare** (real: *Oswald's Dirk*), **Chef's Knife** (real: *Moriharu's Kitchen Knife*), Gaerlan's Citadel Key, Shard of the Herald, Corruptor, etc. |

## Real AC items that are MIS-TAGGED (unambiguous bugs — small fixes)

| Item | In game | Should be | Source |
|------|---------|-----------|--------|
| **Death's Grip** | mace · nether | **staff · frost** (Death's Grip Staff, Rare #287, Cold) | acpedia |
| **Sword of Lost Light** | shock | **fire** (volcanic infusions of Lethe/Esper/Tenkarrdun) | acpedia |
| **Fist of Three Principles** | unarmed · shock | **unarmed · lifesteal** (piercing + Blood Drinker; Rare #204) | acpedia |
| **Spectral Ghost-Slayer family** (6: Longsword/Broadblade/Poniard/Dirk/Warhammer/Cudgel) | frost | **physical** (Slash/Pierce + Crushing Blow + Armor Cleaving) | acpedia |
| **Shroud of the Virindi** | worn · head slot | **body robe** (full-body cloth; Virindi Shroud) | acpedia |
| **Atlan War Mace** | name | **Atlan Mace** (the "War" is extra) | acpedia |
| **Dericost Brand** | name | **Dericost Blade** | fandom |
| **Atlan Dagger** | affix `crit` | retired 2012; Atlan lines are element-craftable — **"crit" is not an Atlan element** | acpedia |
| **Ace of Eyes** | dagger · lifesteal | a **playing card** (Deck of Eyes trophy), not a weapon | fandom |

Minor/debatable (real items, arguable type): Coraline Pike / Smoldering Halberd / Ancient Maul &
Halberd tagged `spear`/`twohand` where AC classes polearms as Two-Handed Combat.

## Design nuance (not bugs)

- **Atlan/Isparian element assignments** (Atlan Sword=fire, Bow=frost…) are a simplification: in real
  AC these lines are craftable in *all four* elements, so no single element is "canon" per type. Each
  assigned element (except the Atlan Dagger's "crit") is a genuine element for the line — acceptable.
- The catalog's pre-Master-of-Arms values (e.g. some max-damage 1 lower than current acpedia) are a
  consistent, legitimate retail era, not errors.

## Recommendation

The catalog layer is sound. The named-item layer needs a **strategy decision** (below), because ~250
of the fabrications are a content call — some may be intentional Dereth-original loot, but the
cross-franchise borrows and lore-hijacking names misrepresent AC either way. The unambiguous
real-item mis-tags should be fixed regardless.

*Full per-item evidence: workflow run `wf_518d8ce6-aa0`; spot-check agent transcripts; scraped acpedia
pages under `.firecrawl/`.*

## Resolution (issue #513)

- **Part A — real-item mis-tags** (PR #514, merged): all 9 unambiguous fixes from the table above
  applied to `NAMED_ITEMS` (Death's Grip, Sword of Lost Light, Fist of Three Principles, the 7
  Spectral Ghost-Slayers, Shroud of the Virindi, Atlan Mace, Dericost Blade, Atlan Dagger, and the
  Ace of Eyes removal).
- **Part B — option 2** ("fix clearly-wrong, keep generic Dereth flavor"):
  - **Removed 14 cross-franchise borrows** — Ashbringer, Frostmourn, Glacius, Reaper of Souls,
    Riftmaker, Rimeclaw, Earthshaker, Dawnbreaker, Stormpike, Frostbrand, Frostreaver, Worldcleaver,
    Kingslayer, Stormcaller Staff.
  - **Renamed 3 lore-hijacks to their verified real AC items** (type/element corrected from acpedia):
    Bael'Zharon's Hope → **Sword of Lost Hope** (acid sword), Oswald's Nightmare → **Oswald's Dirk**
    (physical multi-strike dagger), Chef's Knife → **Moriharu's Kitchen Knife** (rare dagger, Crushing Blow).
  - **Removed 4 lore-hijacks of specific famous figures / mis-classified items** — Gaerlan's Citadel
    Key (real item is a quest *key*), Shard of the Herald, Blade of Elysa, Sliver of Bael'Zharon.
  - **Kept as Dereth-original flavor** — Sword of Jojii ("Koji's Sword" unverifiable; "Jojii" is not a
    real AC figure) and Corruptor (generic name), plus the ~80 generic invented items and ~129
    invented-set items, which option 2 preserves. Option 3 (remove + regenerate the invented sets from
    a verified AC list) remains available as a follow-up if desired.
