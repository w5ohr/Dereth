# The Kilmer Saga — Monthly Event Specifications ("what ships each month")

The implementation bible for `docs/kilmer-saga-storyline.md`. One section per month: what the player
does, the **Town Crier lines** (rumor + clue, exactly as they'll appear in the `EVENT_CALENDAR` data),
the engine goal, rewards, and world changes. **Year 1 shipped 2026-07** on the saga engine in
`index.html` (progression-gated chapters; see ROADMAP milestone K).

## The monthly cadence (our working agreement)
Each real month we ship the next chapter:
1. Read this doc's row for the month; adjust names/numbers if the design has drifted.
2. Add the `EVENT_CALENDAR` entry (crier lines are the `rumor`/`clue` fields verbatim).
3. Build anything in its **Needs** line (new goal types land the month they're first used).
4. Verify (syntax → preview e2e via real code paths → 0 console errors → screenshot) → PR.

## Engine goal vocabulary
Shipped: `visit` · `repel` (featured Incursion) · `agitator` (planted named hunt) · `clear` (delve) ·
`general` (Shadow General) · `echo`/`boss` (saga world boss) · `kills` (family cull).
To build, the month they first appear: `visits:N` (multi-stop) · `deliver` (carry relic to a point) ·
`siege` (town-under-siege wave defense, Y4) · `contribute` (spend N materials at a site) ·
`relics` (possess/present saga items) · `survive` (live through a timed onslaught, Y10) ·
`stages` (chained sub-goals, Y10 finale). The "Hear ye!" prefix is added by the crier itself.

---

## YEAR 1 — YEAR OF EMBERS ✅ SHIPPED (reference)
Crier lines live in `EVENT_CALENDAR` (index.html). Chapters: Coronation (visit Valstead, title
**Witness of the Crown**) · Ash on the Wind (Glenden shrines → *Crimson Star Cipher*) · The Crimson
Star Rises (repel) · Thistledown (agitator hunt → *Agitator's Crimson Note*) · The Ember Unearthed
(clear Trothyr's Rest) · Whispers in Valstead (visit) · The Generals Walk (slay a Shadow General →
*Broken Seal Fragment*) · **Reign of Embers** (Echo of Bael'Zharon; **Bane of the Ember**,
*Shard-Scarred Pauldrons*) · The Unmasking (trial at the castle) · Sweeping the Ashes (kills:8) ·
Frostfell (*Castle-Forged Keepsake*) · A Mask in the Snow (Holtburg notice board → seeds Year 2).

---

## YEAR 2 — YEAR OF WHISPERS (Aerbax's Audit) — months 13–24 ✅ SHIPPED
*Aerbax audits the new monarchy: counting, abducting, replacing. The year of paranoia — trust no face.*
**Needs (built once, used all year):** `visits:N` goal · a "Virindi Abduction" `EVENT_TYPES` row
(kinds: virindi/shadow) · the saga-boss spawner generalized from the Echo (any kind/scale/name/tint).

### M13 · Morningthaw — The Empty-Faced Census (beat)
- **Crier:** "Empty-faced Virindi drift through our streets counting citizens like coin. They take nothing. They touch nothing. They only count."
- **Clue:** "Find the counters at their work — three towns will show you what they study."
- **Play:** `visits:3` — census-Virindi props stand in the three nearest towns; approach each.
- **Reward:** 1,300 XP · 180p. **World:** census-Virindi props remain all year.

### M14 · Seedsow — Hollow Abductions (invasion)
- **Crier:** "Snatch-squads took nine souls from Eastham and Rithwic in the night. The taken walk home days later — and their neighbours say they came back wrong."
- **Clue:** "Break an abduction raid while it happens, and the taken may yet come home themselves."
- **Play:** `repel:1` featured **Virindi Abduction** Incursion.
- **Reward:** 1,400 XP · 220p · a rescued villager's thanks (flavor log).

### M15 · Leafdawning — The Prodigal Drudge (boss)
- **Crier:** "A drudge — but wrong. Tall as a man, cold-eyed, and it SPEAKS. It has broken the Nest's tribes to its will and wears a white sigil."
- **Clue:** "Face the Prodigal Drudge at the Drudge Nest before its army learns to march in step."
- **Play:** saga boss — "Aerbax's Prodigal Drudge" (drudge build, ×2.6 scale, white emissive).
- **Reward:** 2,600 XP · 400p · *Aerbax's Audit-Ledger Page* (quest relic — used again M116).

### M16 · Thistledown — Thistledown Festival (festival)
- **Crier:** "The spring festival blooms in every capital! Yet in Zaikhal a juggler juggles a heartbeat too perfectly, and never once blinks."
- **Clue:** "Watch the fair at Zaikhal. When the mask slips — strike."
- **Play:** `agitator` hunt at Zaikhal — "Simulacrum Juggler" (virindi kind).
- **Reward:** 1,200 XP · 160p · *Cracked Simulacrum Mask* (quest relic).

### M17 · Verdantine — The White Laboratory (explore)
- **Crier:** "The rescued all speak of the same dream: a white laboratory, glass wombs, and half-made people who wear familiar faces."
- **Clue:** "The Virindi hold a complex near Neydisa. Break its laboratory open and free what you find."
- **Play:** `clear` the nearest Virindi-themed delve (implementation picks it; else add an event wing).
- **Reward:** 1,800 XP · 300p. **World:** freed-simulacra camp prop outside the delve.

### M18 · Solclaim — A Question of Faces (beat)
- **Crier:** "The vendor sold me the wrong nails and called me by my dead father's name. Friends — that was not our vendor."
- **Clue:** "Three towns harbor false faces. Expose them one by one."
- **Play:** `agitator`×3 — named simulacrum plants ("The False Vendor / Crier / Barkeep") at three towns.
- **Reward:** 1,700 XP · 280p.

### M19 · Leaffall — The Puppet Court (beat)
- **Crier:** "A Concord envoy nearly set Yaraq and Shoushi at war with three sentences. When they cut the envoy's arm — no blood. Wax."
- **Clue:** "Carry the TRUE letters yourself: Yaraq first, then Shoushi, before the week turns."
- **Play:** `visits:2` in order — Yaraq envoy stand, then Shoushi envoy stand.
- **Reward:** 1,700 XP · 300p. **World:** both capitals' criers thank you by name all month.

### M20 · Harvestgain — THE PRODIGAL MONARCH (boss · climax)
- **Crier:** "In a crumbling Empyrean mansion sits a second Kilmer on a false throne — and it smiles our Monarch's own smile. The court it keeps does not breathe."
- **Clue:** "Destroy the Prodigal Monarch. And afterward, adventurer — be certain which one you kneel to."
- **Play:** saga boss — "The Prodigal Monarch" (crowned figure, virindi-white glow) at a ruin site, with simulacrum adds.
- **Reward:** 4,500 XP · 700p · title **True-Sighted** · unique *Mask of the Unmade* (head, medium, v:30).

### M21 · Leafcull — The Audit Ends (beat)
- **Crier:** "The Virindi are gone from our streets — every one, everywhere, in a single night. The Concord does not celebrate. An audit that ends has reached its total."
- **Clue:** "Report the year's findings at Castle Val Halla. Bring your proofs."
- **Play:** `visit` Valstead/castle.
- **Reward:** 1,900 XP · 320p. **World:** Virindi watch-crystals seeded across Dereth (permanent props).

### M22 · Coldeve — Crystal Harvest (item)
- **Crier:** "The watch-crystals dot the land like frozen dew, and the Arcanum pays honest coin for shards — if you can quiet their guardians."
- **Clue:** "Cull the crystals' virindi guardians and bring the shards to any Agent of the Arcanum."
- **Play:** `kills:6` virindi (each drops *Watch-Crystal Shard*); adds crafting recipe **Scrying Trinket**.
- **Reward:** 1,600 XP · 260p · the recipe stays forever.

### M23 · Frostfell — Frostfell Festival (festival)
- **Crier:** "Frostfell comes! Kilmer keeps the feast — though on the castle walls a single Virindi stands in the snow, watching, touching nothing. The Monarch commands cheer anyway."
- **Clue:** "Keep the feast at any capital. Let the watcher see us unafraid."
- **Play:** `visit` any capital.
- **Reward:** 1,000 XP · 150p · *Hollow-Glass Snowflake* (keepsake).

### M24 · Snowreap — Chitin in the Mines (beat · seeds Year 3)
- **Crier:** "Miners at Arwic broke into a tunnel of grey wax at the deep face — and the tunnel, they swear on their lamps, was BREATHING."
- **Clue:** "See the breathing tunnel at Arwic before the Concord seals the shaft."
- **Play:** `visit` the Arwic mine-mouth point.
- **Reward:** 1,400 XP · 200p. **Seeds:** "The wax flexes. Somewhere below, something vast inhales. Year 3 — the Year of Chitin — will continue the saga."

---

## YEAR 3 — YEAR OF CHITIN (The Deep Brood) — months 25–36 ✅ SHIPPED
*A new Brood-Empress has grown under the Direlands for a generation. Her tunnels reach every realm.*
**Needs:** hive-spire / hive-crater world props · a Tumerok envoy camp prop (Timaru stand-in if the
town isn't mapped).

### M25 · Morningthaw — The Breathing Tunnels (explore)
- **Crier:** "The Concord unsealed the Arwic shaft with soldiers this time. The wax tunnel goes DOWN, friends — farther than any lamp has ever come back from."
- **Clue:** "Clear the tunnel-mouth delve at Arwic and map what breathes below."
- **Play:** `clear` the mine-adjacent delve; olthoi-flavored spawns; drops *Hive-Map Fragment* (relic).
- **Reward:** 1,800 XP · 300p.

### M26 · Seedsow — Swarm at the Walls (invasion)
- **Crier:** "Olthoi soldiers hit Plateau Village at dusk and Danby's Outpost by dawn. Not raids — probes. Something below is measuring our walls."
- **Clue:** "Repel an Olthoi Swarm and thin the probing castes."
- **Play:** `repel:1` featured **Olthoi Swarm**.
- **Reward:** 1,500 XP · 240p.

### M27 · Leafdawning — The Matron Triangle (beat)
- **Crier:** "Three waxen spires now stand on the Direlands skyline where last month there was only scrub. They grow by the WEEK."
- **Clue:** "Scout all three Matron spires and chart the triangle they make."
- **Play:** `visits:3` — hive-spire props at three Direlands points.
- **Reward:** 1,700 XP · 280p. **World:** the three spires persist until M33.

### M28 · Thistledown — Thistledown Festival (festival)
- **Crier:** "The festival is held under guard this spring — and for the first time in living memory, Aun Tumerok envoys attend. They know Olthoi war better than any of us."
- **Clue:** "Join the guarded fair at any capital and hear the Aun drums."
- **Play:** `visit` any capital (Tumerok envoy props at the fair).
- **Reward:** 1,100 XP · 160p · *Aun War-Drum Charm* (keepsake).

### M29 · Verdantine — The Slave Pens Again (beat)
- **Crier:** "They are taking PEOPLE below — to pens, like the old arrival-days our grandmothers sang of. The Concord will not leave them there."
- **Clue:** "Fight into the Olthoi warrens and break the captives out."
- **Play:** `clear` an Olthoi-themed delve + `kills:6` olthoi-family en route.
- **Reward:** 2,000 XP · 340p · title **Pen-Breaker**.

### M30 · Solclaim — Acid Summer (seasonal)
- **Crier:** "The Direlands are POCKED with acid craters overnight — the hive vents its bile before it marches. Even the rain off that waste burns."
- **Clue:** "Cull the venting workers before the scars spread past the Direlands."
- **Play:** `kills:8` olthoi-family. **World:** hive-crater props scar the Direlands permanently.
- **Reward:** 1,700 XP · 280p · Acid Bane spell tier highlighted at Scriveners (existing F5 banes).

### M31 · Leaffall — March of the Soldiers (invasion)
- **Crier:** "The soldiers march in COLUMN now, friends — on more towns than one, with the Concord's own armies in the field beside us. The largest muster since the Fading."
- **Clue:** "Repel the rolling swarm — twice, if you have the mettle."
- **Play:** `repel:2` featured **Olthoi Swarm** Incursions.
- **Reward:** 2,200 XP · 380p.

### M32 · Harvestgain — THE BROOD-EMPRESS (boss · climax)
- **Crier:** "Below the northern spire she waits — EMPRESS GNAWVIL-RAX, last clutch of the Ancient Queen, grown vast in a generation of dark. The Concord sounds the horn."
- **Clue:** "Descend beneath the north Matron spire and end the Brood-Empress."
- **Play:** saga boss — "Empress Gnawvil-Rax" (olthoi build, ×3.4, acid-green tint) at the north spire; soldier adds.
- **Reward:** 5,000 XP · 800p · title **Hivebane** · unique *Empress-Carapace Shield* (offhand, v:44, acid-resist flavor).

### M33 · Leafcull — Burning the Combs (beat)
- **Crier:** "Torch-teams and Tumerok fire-drums move on the spires. Every comb we burn now is a war our children never fight."
- **Clue:** "Fire all three Matron spires and collapse the tunnels beneath them."
- **Play:** `visits:3` — the M27 spires, now torchable (interact swaps prop to burnt stump).
- **Reward:** 1,900 XP · 320p. **World:** spires become burnt stumps, permanently.

### M34 · Coldeve — The Aun Accord (beat)
- **Crier:** "It is signed! Timaru pledges the Aun war-bands to the Concord — the first nation not of Ispar to swear. The drums in the passes tonight are FRIENDLY."
- **Clue:** "Visit the Aun envoy camp and witness the Accord."
- **Play:** `visit` the Aun envoy camp prop (Direlands edge).
- **Reward:** 1,600 XP · 260p. **World:** the camp persists; its vendor sells Tumerok-flavor gear.

### M35 · Frostfell — Frostfell Festival (festival)
- **Crier:** "Frostfell! This year's keepsake is a candle of hive-wax — burn the enemy's house to light your own, says the Monarch."
- **Clue:** "Keep the feast at any capital."
- **Play:** `visit` any capital.
- **Reward:** 1,100 XP · 160p · *Hive-Wax Candle* (keepsake).

### M36 · Snowreap — Sails off Sanamar (beat · seeds Year 4)
- **Crier:** "Fisherfolk off the Halaetan coast swear they saw SAILS — royal purple, the old invader colors, not flown since Varicci the Second. The Concord says: weather. The fisherfolk say: fleet."
- **Clue:** "Watch the western sea from the coastal overlook and judge for yourself."
- **Play:** `visit` a west-coast overlook point.
- **Reward:** 1,500 XP · 220p. **Seeds:** "Purple sails, hull after hull, riding the horizon. Year 4 — the Year of Banners — will continue the saga."

---

## YEAR 4 — YEAR OF BANNERS ✅ SHIPPED (The Pretender's War) — months 37–48
*Corsair-Prince Varicci IV lands with a fleet, a claim, and a leashed horror: the bindings of Grael.*
**Needs:** `siege` goal (town-under-siege wave defense — walls manned, gates-shut flavor, longer waves)
· `deliver` goal · a "Viamontian Corsair" hostile (human-silhouette mob or banderling reskin in purple)
· coastal spire-town stand-in props (Sanamar governor camp).

### M37 · Morningthaw — The Pretender's Proclamation (beat)
- **Crier:** "A herald in purple read it on our own square: 'Dereth belongs to Viamont by right of conquest, and the commoner Kilmer will kneel or burn.' Bluespire's garrison has FLIPPED to his banner."
- **Clue:** "Hear the proclamation read at two towns and take the measure of this Pretender."
- **Play:** `visits:2` — herald props at two coastal-side towns.
- **Reward:** 1,600 XP · 260p.

### M38 · Seedsow — Blockade Running (beat)
- **Crier:** "Greenspire starves behind the Pretender's blockade. The Concord pays runners' rates for any soul who can put supplies through the net."
- **Clue:** "Buy or gather supplies and run them to the besieged coast camp."
- **Play:** `deliver` — carry *Concord Supplies* (5, from any provisioner) to the relief camp point; corsair ambush en route.
- **Reward:** 2,000 XP · 350p.

### M39 · Leafdawning — The Siege of Redspire (invasion · siege debut)
- **Crier:** "REDSPIRE IS UNDER SIEGE — real siege, ladders and fire, the first since the Fading. The garrison mans the walls and prays for hands."
- **Clue:** "Stand the walls at the siege and break the assault."
- **Play:** `siege:1` — the new siege event at a coastal town (3 waves, gate objective).
- **Reward:** 2,400 XP · 420p.

### M40 · Thistledown — Thistledown Festival (festival)
- **Crier:** "The festival is held in DEFIANCE this year at Holtburg — and the Pretender has sent our Monarch a gift. The taster who sniffed it has not woken."
- **Clue:** "Walk the Holtburg fair and find who carried the poisoned gift."
- **Play:** `agitator` hunt at Holtburg — "The Pretender's Gift-Bearer".
- **Reward:** 1,400 XP · 200p · *Poisoned Gift Ribbon* (quest relic).

### M41 · Verdantine — Letters of Marque (item)
- **Crier:** "The Concord issues LETTERS OF MARQUE: captured Viamontian steel is yours to keep, privateer, if you take it off their armories."
- **Clue:** "Raid the corsair armories and claim their rapiers and boarding axes."
- **Play:** `kills:6` corsairs; adds the **privateer weapon line** (Viamontian Rapier — dagger-speed sword; Boarding Axe) to loot tables permanently.
- **Reward:** 1,900 XP · 320p · one guaranteed privateer weapon.

### M42 · Solclaim — The Duel of Envoys (beat)
- **Crier:** "At Silyun the two courts met — and settled Westwatch's allegiance the old way: one champion apiece, first yield. The Pretender's man is a mountain in plate."
- **Clue:** "Stand as the Concord's champion at Silyun and win the duel."
- **Play:** saga boss (duel-flavored, no adds) — "Champion of the Pretender".
- **Reward:** 2,800 XP · 450p. **World:** Westwatch flies Concord colors.

### M43 · Leaffall — Grael Unbound (boss)
- **Crier:** "He is LOSING — so he has loosed it. GRAEL, THE ARM OF DESTRUCTION, walks the Halaetan Isles unbound, and even the Pretender's own men flee it."
- **Clue:** "Hunt the Arm of Destruction across the isles before nothing is left to claim."
- **Play:** roving saga boss — "Grael, the Arm of Destruction" (golem build, ×3.6, molten tint).
- **Reward:** 3,200 XP · 550p · *Empyrean Binding-Link* (quest relic).

### M44 · Harvestgain — THE BATTLE OF THE HALAETAN ISLES (boss · climax)
- **Crier:** "FLEET AND FIELD, friends — the Concord sails against Sanamar at dawn. Varicci the Fourth will yield his flagship or the sea can have it."
- **Clue:** "Board the flagship: break the siege, then take the Pretender himself."
- **Play:** `siege:1` at the coast camp, then saga boss "Corsair-Prince Varicci IV" aboard the flagship prop; Grael re-bound with the M43 link (relic consumed).
- **Reward:** 5,500 XP · 900p · titles **Isles-Sworn** (completion) + **Grael's Jailer** (if M43 done) · unique *The Pretender's Sabre* (sword, v:38, bleed affix).

### M45 · Leafcull — The Spire Compact (beat)
- **Crier:** "New Viamont signs! Sanamar takes a Concord governor, purple hauls down, and spire-wine flows for FREE in the plazas. Well. For an hour."
- **Clue:** "Visit the new governor's camp at Sanamar and see the Compact sworn."
- **Play:** `visit` the Sanamar governor camp.
- **Reward:** 1,800 XP · 300p. **World:** camp persists; a Viamontian vendor (rapiers, spire-wine) opens.

### M46 · Coldeve — Prisoners & Pardons (beat)
- **Crier:** "The cells hold three of the Pretender's officers, and the Concord — curse its fairness — asks the PEOPLE to judge them: pardon, ransom, or the quarry."
- **Clue:** "Attend the judgments at the prison camp and speak your verdicts."
- **Play:** `visit` the prison camp; three captive NPCs, three choices (choices persist as NPC fates: freed guard / ransom gold / quarry laborer prop).
- **Reward:** 1,700 XP · 280p (+200p if any officer ransomed).

### M47 · Frostfell — Frostfell Festival (festival)
- **Crier:** "Frostfell in SANAMAR, under Concord banners, with Viamontian mulled wine — the first festival that town has thrown for anyone but a king across the sea."
- **Clue:** "Keep the feast at Sanamar's camp or any capital."
- **Play:** `visit` Sanamar camp or any capital.
- **Reward:** 1,200 XP · 170p · *Spire-Wine Flask* (keepsake).

### M48 · Snowreap — The Red Spring (beat · seeds Year 5)
- **Crier:** "Valstead's wells ran RED for a morning — iron-red, then clear, as if the land blinked. By royal order the castle cellars are sealed. No one says why."
- **Clue:** "See the stained well-stones of Valstead yourself, before the masons scrub them."
- **Play:** `visit` the Valstead well point.
- **Reward:** 1,600 XP · 240p. **Seeds:** "Under the well-water: a taste of copper, and a warmth that has no business in snowmelt. Year 5 — the Year of Blood — will continue the saga."

---

## YEAR 5 — YEAR OF BLOOD (The Falatacot Root) — months 49–60 · *the mid-saga twist*
*The Burun rise, and the Blood Matriarchs name Castle Val Halla "the Stolen Crown of Ley". The truth
under the plateau comes out — and Kilmer keeps his crown by a single vote.*
**Needs:** "Burun Tide" `EVENT_TYPES` row (kinds: burun/mosswart/moarsman) · the **Val Halla
Under-Temple** delve (a Falatacot event wing beneath the castle — warm stone, blood-leyline glow) ·
`relics` goal (present held saga items) · `contribute` goal.

### M49 · Morningthaw — The Croaking Tide (invasion)
- **Crier:** "The swamps have EMPTIED — Burun in their hundreds out of Yanshi and Sawato, and drums in the Blackmire that no Isparian ever taught them."
- **Clue:** "Repel the Burun tide before it reaches the dry towns."
- **Play:** `repel:1` featured **Burun Tide**.
- **Reward:** 1,800 XP · 300p.

### M50 · Seedsow — The Matriarch's Claim (beat)
- **Crier:** "A Blood Matriarch stood in PARLEY at the Swamp Temple and spoke Isparian: 'The castle on the flattened hill is the Stolen Crown of Ley. Return our heart, or we return for it.'"
- **Clue:** "Hear the claim yourself at the Blackmire temple stones."
- **Play:** `visit` the Blackmire/Swamp Temple point.
- **Reward:** 1,700 XP · 280p. **World:** the accusation is public; Valstead criers repeat it uneasily.

### M51 · Leafdawning — The Sealed Cellars (explore)
- **Crier:** "By royal WRIT — the Monarch himself asks it — the sealed cellars of Castle Val Halla open to adventurers of proven name. What the masons found down there needs witnesses, not soldiers."
- **Clue:** "Descend the castle cellars and see what the plateau was built upon."
- **Play:** `clear` the new **Val Halla Under-Temple** delve — an intact Falatacot temple, warm to the touch.
- **Reward:** 2,400 XP · 400p. **World:** the under-temple stays enterable (the saga's endgame site).

### M52 · Thistledown — Thistledown Festival (festival)
- **Crier:** "A muted festival this spring. Protest banners hang in Valstead — 'WHOSE LUCK?' — and the Monarch walks the fair UNGUARDED, meeting every eye that will meet his."
- **Clue:** "Walk the Valstead fair beside him."
- **Play:** `visit` Valstead.
- **Reward:** 1,300 XP · 180p · *Protest Ribbon* (keepsake — yes, really; the smiths sell both sides).

### M53 · Verdantine — The Ledger of Luck (beat)
- **Crier:** "The Concord's inquest wants EVIDENCE: a generation of Valstead's harvests, dice, and dodged lightning, all written down. If the luck was borrowed, the ledger will show the loan."
- **Clue:** "Gather testimony pages around Valstead for the inquest."
- **Play:** `visits:3` — testimony points (mill, well, granary) each yields *Ledger of Luck Page* (relic).
- **Reward:** 1,900 XP · 320p.

### M54 · Solclaim — Blood Rites at Dusk (invasion)
- **Crier:** "Ritual fires at Amiantos Bethel and the Swamp Ruin — the Matriarchs BEND the leyline toward the swamps every night at dusk. You can feel it pull in your back teeth."
- **Clue:** "Raid the dusk rites and break the ritual circles."
- **Play:** `repel:1` Burun Tide + `kills:6` burun-family (the circle-tenders).
- **Reward:** 2,200 XP · 380p.

### M55 · Leaffall — The Vote of the Concord (beat)
- **Crier:** "The Concord votes on the CROWN ITSELF tomorrow. Holtburg wavers, Yaraq is stone, Shoushi weeps — and every scrap of testimony an adventurer carried matters tonight."
- **Clue:** "Present your evidence at Castle Val Halla before the vote is called."
- **Play:** `relics` — present the M53 Ledger Pages (and M48 memory) at the castle.
- **Reward:** 2,300 XP · 400p. **World:** Kilmer keeps the throne **by one vote**, pledging the root will never again feed the treasury.

### M56 · Harvestgain — THE BLOOD MATRIARCH ASCENDANT (boss · climax)
- **Crier:** "Denied the root, the eldest Matriarch has DRUNK HER OWN LEYLINE. She is in the under-temple — beneath the Monarch's own floor — and she is still swelling."
- **Clue:** "Descend beneath Val Halla and end the Ascendant before she finishes becoming."
- **Play:** saga boss — "The Blood Matriarch Ascendant" (mosswart/burun build, ×3.2, arterial-red glow) in the Under-Temple; burun adds.
- **Reward:** 5,500 XP · 900p · title **Root-Warden** · unique *Leyline Athame* (dagger, v:36, lifesteal affix) · *Matriarch's Heart-Bead* (quest relic — M116).

### M57 · Leafcull — The Warded Heart (beat)
- **Crier:** "The scholars are UNANIMOUS, which frightens me more than the Matriarch did: the root cannot be destroyed. Sever a leyline this old and you tear the Veil itself. So — we ward it."
- **Clue:** "Bring materials to the under-temple wardstones and help raise the ward."
- **Play:** `contribute` — spend 20 crafting materials at the wardstone site.
- **Reward:** 2,000 XP · 340p. **World:** the warded heart glows behind its lattice *(remember this — Y8/Y10)*.

### M58 · Coldeve — Burun Ebb (beat)
- **Crier:** "The tide recedes. Burun tribes without Matriarchs sue for SWAMP-TRUCE at Green Mire — they want the fighting done more than they want the heart back."
- **Clue:** "Visit the Green Mire truce camp and witness the peace."
- **Play:** `visit` the Green Mire neutral camp prop.
- **Reward:** 1,700 XP · 280p. **World:** truce camp persists (neutral burun idle there).

### M59 · Frostfell — Frostfell Festival (festival)
- **Crier:** "Frostfell at the castle, and the Monarch's toast rings the hall: 'To a crown that owes no debt it will not NAME.' The cheering took a moment. Then it didn't stop."
- **Clue:** "Keep the feast at any capital or Valstead."
- **Play:** `visit` any capital or Valstead.
- **Reward:** 1,300 XP · 180p · *Debt-Named Goblet* (keepsake).

### M60 · Snowreap — The Weather Breaks Wrong (beat · seeds Year 6)
- **Crier:** "HALFWAY through our Monarch's saga, and the sky forgets its seasons: lightning from cloudless blue, hail in Solclaim heat, and over Mount Esper a stormfront that DOES NOT MOVE."
- **Clue:** "Stand in one of the rogue storms and feel what rides inside it."
- **Play:** `visit` an active storm cell (the live weather system marks one).
- **Reward:** 1,800 XP · 300p. **Seeds:** "Inside the storm: voices on the wind, giving orders. Year 6 — the Year of Storms — will continue the saga."

---

## YEAR 6 — YEAR OF STORMS (Gaerlan's Heirs) — months 61–72
*Gaerlan's students shatter the sky-citadel's weather-seals; the sky itself is the battlefield.
Built on the live weather system.*
**Needs:** storm-elemental spawns at storm-cell eyes (wisp/golem storm-tint) · storm-rod world props ·
a Citadel event delve (Mount Esper) · multi-boss month support (4 concurrent saga bosses).

### M61 · Morningthaw — Lightning Out of Season (seasonal)
- **Crier:** "The rogue storms have TEETH now — things of living wind and charge walk at each storm's eye, and they do not scatter when it passes."
- **Clue:** "Hunt the elementals at the storm-eyes."
- **Play:** `kills:5` storm elementals (spawned at live weather cells).
- **Reward:** 1,900 XP · 320p.

### M62 · Seedsow — The Broken Seals (beat)
- **Crier:** "Seal-stones fall like METEORS across the realms — and the Wandering Caravan was caught smuggling one east under grain sacks. Its keeper is... talkative, now."
- **Clue:** "Find the Wandering Caravan on the roads and hear the keeper's confession."
- **Play:** `visit` the roving caravan (existing system) while the month is live.
- **Reward:** 1,800 XP · 300p · *Weather-Seal Fragment* (relic).

### M63 · Leafdawning — Children of the Citadel (beat)
- **Crier:** "Storm-cults gather on the rooftops of Qalaba'r and Neydisa, calling the lightning down BY NAME. Their heralds recruit openly: 'Cyrannic pays in wind and fire.'"
- **Clue:** "Silence the cult heralds in both towns."
- **Play:** `agitator`×2 — "Herald of the Four Winds" at Qalaba'r and Neydisa.
- **Reward:** 2,000 XP · 340p.

### M64 · Thistledown — Thistledown Festival (festival)
- **Crier:** "A festival under a PERFECT sky — too perfect. The calm was bought, friends: one of the recovered seals now hangs spent over the fairground. Enjoy what it cost."
- **Clue:** "Attend the fair and see the price of a clear day."
- **Play:** `visit` any capital.
- **Reward:** 1,300 XP · 180p · *Spent Seal Sliver* (keepsake).

### M65 · Verdantine — The Four Winds (boss ×4)
- **Crier:** "FOUR avatars ride the four realms' skies — North Gale, South Scorch, East Deluge, West Frost — each a season gone to war. Cyrannic tests his cardinal points."
- **Clue:** "Fell all four Wind avatars, in any order."
- **Play:** four concurrent roving saga bosses (elemental tints); each drops a *Cardinal Sigil* (relic ×4).
- **Reward:** 3,600 XP · 600p (paid on the fourth).

### M66 · Solclaim — Eye of the Tempest (explore)
- **Crier:** "The stormfront over Mount Esper has OPENED — inside its eye hangs a rebuilt wing of Gaerlan's citadel, stair-stones of cloud and all. It will not stay open long."
- **Clue:** "Climb into the tempest's eye and clear the citadel wing."
- **Play:** `clear` the Citadel event delve.
- **Reward:** 2,600 XP · 440p.

### M67 · Leaffall — Grounding the Grid (beat)
- **Crier:** "The scholars' answer to a weaponized sky: STORM-RODS over every capital, raised by common hands with common iron. Bring what you can spare; the sky is everyone's roof."
- **Clue:** "Contribute materials to the storm-rod at each capital."
- **Play:** `contribute` — 8 materials at each of the three capitals (24 total).
- **Reward:** 2,200 XP · 380p. **World:** three storm-rod props stand permanently.

### M68 · Harvestgain — WARLORD OF THE FOUR WINDS (boss · climax)
- **Crier:** "CYRANNIC sits the tempest-throne in his master's rebuilt hall, wearing the storm like a cloak. The eye is collapsing — this month or never, friends."
- **Clue:** "Ascend the citadel and unseat the Warlord of the Four Winds."
- **Play:** saga boss — "Cyrannic, Warlord of the Four Winds" in the Citadel delve (shock brand attacks).
- **Reward:** 5,800 XP · 950p · title **Stormbreaker** · unique *Crown of the Calm Eye* (head, v:32, shock-protect).

### M69 · Leafcull — Setting the Seals (beat)
- **Crier:** "Four seals, four realms, one RELAY — the recovered weather-seals go back into the earth this month, and the sky goes back on its wheel."
- **Clue:** "Carry your Cardinal Sigils to the four realm seal-stones."
- **Play:** `relics` + `visits:4` — present a Sigil at each realm's seal-stone point.
- **Reward:** 2,400 XP · 400p. **World:** rogue weather ends; the seasonal wheel resumes.

### M70 · Coldeve — The Salvage Court (item)
- **Crier:** "The citadel's wreckage is a MINE of storm-steel. The Salvage Court convenes: bring citadel salvage, learn the lightning-work, keep what you forge."
- **Clue:** "Learn storm-forged tinkering from the Salvage Court."
- **Play:** adds **storm-forged imbue** (citadel salvage → shock brand, rides the existing imbue system); goal: perform one storm-forge.
- **Reward:** 1,900 XP · 320p · the technique stays forever.

### M71 · Frostfell — Frostfell Festival (festival)
- **Crier:** "SNOW! Real snow, ON TIME, falling because the calendar says so and for no other reason — and grown adults are dancing in it like children. Let them."
- **Clue:** "Keep the feast under the first honest snowfall in a year."
- **Play:** `visit` any capital.
- **Reward:** 1,300 XP · 180p · *First-Snow Vial* (keepsake).

### M72 · Snowreap — The Winter That Stays (beat · seeds Year 7)
- **Crier:** "Frostfell's snow lies deeper each week and will not melt — and Stonehold reports LIGHTS in the old Dericost Vault. Blue lights. Patient lights."
- **Clue:** "See the vault-lights from Stonehold's walls."
- **Play:** `visit` Stonehold.
- **Reward:** 1,800 XP · 300p. **Seeds:** "Below the ice, something keeps court. Year 7 — the Year of Bone — will continue the saga."

---

## YEAR 7 — YEAR OF BONE (The Dericost Winter) — months 73–84
*The undead winter kings wake, and their season comes with them: snow that will not end until the
Lich-King of Frore is put down. Three story-seasons pass under snow.*
**Needs:** **season lock** (the world's snow reskin persists M73–M80 regardless of the wheel — the
SEASONS system gains an override) · the **Frore** ice-city delve (multi-wing) · named undead-noble
hunts.

### M73 · Morningthaw — No Thaw (seasonal)
- **Crier:** "Morningthaw came and went and NOTHING THAWED. The roads are white, the wells crack, and the old folk use a word I had to ask about: Dericost."
- **Clue:** "Walk any capital and see the winter that stayed."
- **Play:** `visit` any capital. **World:** snow reskin locks until M80.
- **Reward:** 1,600 XP · 260p.

### M74 · Seedsow — The Waking Vaults (invasion)
- **Crier:** "The Dericost Vault at Stonehold stands OPEN from the inside, and the Gelidite Library answers it. The dead of the winter kingdom are punctual guests."
- **Clue:** "Repel the risen dead."
- **Play:** `repel:1` featured **Undead Rising**.
- **Reward:** 1,900 XP · 320p.

### M75 · Leafdawning — The Pale Court (beat)
- **Crier:** "In three frozen villages the Dericost nobles hold COURT — crowned skeletons on borrowed thrones, demanding fealties older than Ispar. The villagers kneel because the cold makes them."
- **Clue:** "Break the Pale Court: unseat all three nobles."
- **Play:** `agitator`×3 — named undead nobles ("Margrave of Sleet", "Countess of the Long Dark", "the Winter Chamberlain") at three towns.
- **Reward:** 2,300 XP · 400p.

### M76 · Thistledown — Frostbound Festival (festival)
- **Crier:** "No thaw, no flowers? WRONG. The greenhouses force the thistle blooms by lamplight, and the festival goes ahead IN the snow — defiance you can smell over the frost."
- **Clue:** "Keep the frostbound festival at any capital."
- **Play:** `visit` any capital.
- **Reward:** 1,400 XP · 200p · *Lamplight Thistle* (keepsake).

### M77 · Verdantine — The Road to Frore (explore)
- **Crier:** "The ice-city is FOUND: Frore, whole and frozen, its gates cleared by Concord sappers. Every hall below is older than the arrival and colder than the grave it is."
- **Clue:** "Enter Frore and clear its outer halls."
- **Play:** `clear` the new **Frore** delve (wing 1). **World:** Frore stays open permanently (the promised ice-delve).
- **Reward:** 2,700 XP · 460p.

### M78 · Solclaim — The Gelidite Choir (beat)
- **Crier:** "You can HEAR it on a still night now — the Gelidite priests singing the winter deeper, verse by verse. Every choir-hall silenced buys back a week of spring."
- **Clue:** "Silence the choir in Frore's halls."
- **Play:** `kills:8` undead within Frore/undead family.
- **Reward:** 2,200 XP · 380p.

### M79 · Leaffall — Bones of the Three Realms (invasion)
- **Crier:** "The dead of EVERY realm rise at once — Aluvian barrow-wights, Gharu'ndim sand-shrouds, Sho ancestor-husks — each at their own capital. The Lich-King calls in every grave's debt."
- **Clue:** "Repel the risen at the capitals — twice proves the realms can hold."
- **Play:** `repel:2` featured **Undead Rising**.
- **Reward:** 2,500 XP · 420p.

### M80 · Harvestgain — THE LICH-KING OF FRORE (boss · climax)
- **Crier:** "The throne-hall is REACHED. The Lich-King of Frore sits a glacier throne with three seasons of our stolen springtime around his shoulders — and his phylactery, they say, is a single snowflake."
- **Clue:** "Slay the Lich-King, take the snowflake — then melt it at Castle Val Halla's hearth."
- **Play:** saga boss in Frore's throne wing → drops *The Phylactery Snowflake* → `deliver` it to the castle hearth (melts on arrival).
- **Reward:** 6,000 XP · 1,000p · title **Winter's End** · unique *Tremblant's Ivory Staff* (staff, v:36, frost brand). **World:** the snow-lock breaks — instant, glorious spring.

### M81 · Leafcull — The Late Harvest (seasonal)
- **Crier:** "A MIRACLE HARVEST — three seasons of growth in one, the fields frantic and golden, and every granary paying double for hands. The land is making up for lost time."
- **Clue:** "Join the late harvest at any capital fair."
- **Play:** `visit` any capital. **World (month-long):** vendors pay +50% for goods sold.
- **Reward:** 1,800 XP · 300p.

### M82 · Coldeve — Laying the Dead (beat)
- **Crier:** "The war is over; the WARDING begins. Grave-goods looted in the winter go home to their vaults, and the Dericost sleep on our terms this time."
- **Clue:** "Recover grave-goods from the risen and return them to the Stonehold vault."
- **Play:** `kills:5` undead (each drops *Dericost Grave-Good*) + `deliver` to the Stonehold vault point.
- **Reward:** 2,300 XP · 400p · title **Quiet-Bringer**.

### M83 · Frostfell — Frostfell Festival (festival)
- **Crier:** "Frostfell — and nobody quite trusts the snow this year, do they? The Monarch names it 'winter on OUR terms' and lights the bonfires himself."
- **Clue:** "Keep the feast, warily, at any capital."
- **Play:** `visit` any capital.
- **Reward:** 1,400 XP · 200p · *Our-Terms Bonfire Coal* (keepsake).

### M84 · Snowreap — The Torn Horizon (beat · seeds Year 8)
- **Crier:** "Over the Vesayen Isles a portal OPENED IN MID-AIR — and it SCREAMED, friends, a sound the fisherfolk will not describe twice. It did not close. Others are opening."
- **Clue:** "Watch the torn horizon from the coastal overlook."
- **Play:** `visit` the coastal overlook.
- **Reward:** 1,900 XP · 320p. **Seeds:** "The tear hangs there still, humming. Portalspace itself is wounded. Year 8 — the Year of Rifts — will continue the saga."

---

## YEAR 8 — YEAR OF RIFTS (The Sundered Veil) — months 85–96 · *the revelation*
*Portalspace is failing. The trail leads to Asheron — sealed into the Veil's edge the whole
Interregnum, holding it shut. His warning: T'thuun never stopped rising.*
**Needs:** **rift spawner** (random portal props spilling mixed-era creatures; high-variance loot) ·
survey-anchor interact props · a **portalspace ascent delve** (the Veil's edge) · **Asheron NPC**
(permanent, at his castle courtyard from M93).

### M85 · Morningthaw — Rift-Storm Season (seasonal)
- **Crier:** "RIFTS tear open where they please now — one spat snow-era drudges into a Verdantine orchard; another rained fish. Do not laugh. The next may not be fish."
- **Clue:** "Close three rifts by culling what holds them open."
- **Play:** `kills` at rift props — clear the 4 guardian spills at 3 separate rifts (rift closes each time).
- **Reward:** 2,400 XP · 400p (rift loot rolls +1 tier all month).

### M86 · Seedsow — The Cartographers (beat)
- **Crier:** "The Arcanum maps the madness: survey-anchors, planted at every stable tear. Early word is STRANGE, friends — the rifts don't point inward at Dereth. They point OUT."
- **Clue:** "Plant survey-anchors at three rift sites."
- **Play:** `visits:3` — interact at three rift survey points.
- **Reward:** 2,200 XP · 380p.

### M87 · Leafdawning — Through the Tear (explore)
- **Crier:** "A rift held STABLE long enough to walk through — into a pocket of old Empyrean portalspace, preserved like a fly in amber. Lamps still lit. Table still set."
- **Clue:** "Walk the stable tear and clear the preserved pocket."
- **Play:** `clear` the Empyrean-pocket event delve.
- **Reward:** 2,700 XP · 460p.

### M88 · Thistledown — Thistledown Festival (festival)
- **Crier:** "A rift opened over the fairground — harmless, high, and showing ANOTHER DERETH'S festival: same music, same lights, dancers we almost knew. It closed at moonrise. Nobody clapped."
- **Clue:** "Attend the fair beneath the window into elsewhere."
- **Play:** `visit` any capital.
- **Reward:** 1,500 XP · 220p · *Elsewhere Mirror-Shard* (keepsake).

### M89 · Verdantine — Echoes of the Architect (beat)
- **Crier:** "Asheron's old constructs woke in the night — mnemosynes, workshop golems, every last one — and began WALKING. Different roads, one direction. They are going somewhere, friends."
- **Clue:** "Follow the constructs' road to where they gather."
- **Play:** `visits:2` — construct waypoints along their march.
- **Reward:** 2,300 XP · 400p.

### M90 · Solclaim — The Edge of the Veil (explore)
- **Crier:** "The constructs led TRUE: an ascent through raw portalspace, step over step, to the Veil's very edge — and set into that edge like a keystone, a SEAL, and in the seal a sleeping shape we all know from statues."
- **Clue:** "Climb the ascent to the Veil's edge and look on the seal."
- **Play:** `clear` the portalspace ascent delve (wing 1, up to the seal).
- **Reward:** 2,900 XP · 500p. **Seeds:** "Asheron did not abandon Dereth. He IS the lock."

### M91 · Leaffall — The Price of the Door (beat)
- **Crier:** "To free the Architect, the Veil needs a NEW anchor — and in open council someone finally said the words: 'the root under Val Halla.' The Monarch, for the first time any crier remembers, refused to decide alone."
- **Clue:** "Attend the council at Castle Val Halla and hear the price named."
- **Play:** `visit` Valstead/castle.
- **Reward:** 2,200 XP · 380p.

### M92 · Harvestgain — THE UNSEALING (boss · climax)
- **Crier:** "It is TODAY. Not the root — ten thousand lesser anchors instead: every lifestone every soul has ever touched, braided into one rope. But portalspace itself guards its wound, and it has grown TEETH."
- **Clue:** "Defeat the Warden of the Sundered Veil at the ascent's peak, and pull the Architect free."
- **Play:** saga boss — "Warden of the Sundered Veil" (virindi/wisp hybrid build, prismatic tint) atop the ascent delve.
- **Reward:** 6,200 XP · 1,050p · title **Veil-Walker** · unique *Sliver of the Old Door* (jewelry, +magic; quest relic — M116).

### M93 · Leafcull — The Architect Returns (beat)
- **Crier:** "ASHERON WALKS DERETH AGAIN. He came down the castle road on foot, thinner than his statues, and met the Monarch at the gates. Neither bowed. All Dereth held its breath — and then both men laughed."
- **Clue:** "Witness the meeting at Asheron's castle courtyard."
- **Play:** `visit` Asheron's Castle courtyard (Asheron NPC now permanent).
- **Reward:** 2,400 XP · 420p. **World:** Asheron stands in his courtyard from now on (dialogue NPC).

### M94 · Coldeve — The Warning (beat)
- **Crier:** "The Architect's testimony, plain as frost: 'T'thuun has been rising through the whole of the Fading. The Veil will not hold a second siege. Look at your tide-marks.' ...Friends, the tide-marks ARE wrong."
- **Clue:** "Hear the warning from Asheron himself."
- **Play:** `visit` Asheron (dialogue beat).
- **Reward:** 2,200 XP · 380p.

### M95 · Frostfell — Frostfell Festival (festival)
- **Crier:** "Frostfell with the ARCHITECT at table — two eras toasting each other over hive-wax candles and spire-wine. He asked for the old Yalain carol. The choir faked it beautifully."
- **Clue:** "Keep the feast at any capital."
- **Play:** `visit` any capital.
- **Reward:** 1,500 XP · 220p · *Two-Eras Candle* (keepsake).

### M96 · Snowreap — Black Water (beat · seeds Year 9)
- **Crier:** "Fishermen off Mayoi hauled up SLITHIS-SIGN — deep-coral growths on their anchors that should not exist above the abyss. And mark this: the Moarsmen have stopped raiding. They are moving INLAND. Away from their own sea."
- **Clue:** "See the black water for yourself at Mayoi's docks."
- **Play:** `visit` Mayoi docks.
- **Reward:** 2,000 XP · 340p. **Seeds:** "When the sea's own children flee the sea, believe them. Year 9 — the Year of Tides — will continue the saga."

---

## YEAR 9 — YEAR OF TIDES (The Gathering Dark) — months 97–108
*T'thuun's Herald rises to prepare the drowning. The year of alliances: everything with a spine —
or without one — chooses a side.*
**Needs:** **coastal drowned-shore zones** (visual water-rise bands at three coasts, slithis/moarsman
spawns) · "Slithis Tide" `EVENT_TYPES` row (kinds: sclavus/moarsman reskin-tint) · alliance
contribution tracking (reuses `contribute`) · an **underwater-approach delve** (rides F6 drowning).

### M97 · Morningthaw — The Wrong Tide (seasonal)
- **Crier:** "The tide came in at Mayoi and DID NOT GO OUT. Kryst and the Dryreach coast report the same: a hand's width of sea that stays, and stays, and stays."
- **Clue:** "Walk a drowned shore and see the sea that won't retreat."
- **Play:** `visit` any drowned-shore zone. **World:** the three drowned shores persist through Y10.
- **Reward:** 2,000 XP · 340p.

### M98 · Seedsow — The Moarsman Exodus (beat)
- **Crier:** "Moarsmen at the GATES — not raiding: KNEELING. They beg sanctuary inland, whole spawning-schools of them, and they will not say the name of what they flee. They gesture at the sea and shudder."
- **Clue:** "Visit the refugee camps and keep the peace of three uneasy towns."
- **Play:** `visits:3` — moarsman refugee camps at three coastal towns.
- **Reward:** 2,300 XP · 400p. **World:** the camps persist (neutral moarsmen).

### M99 · Leafdawning — Slithis Landfall (invasion)
- **Crier:** "SLITHIS on the beaches — the drowned god's own brood, first landfall since the T'thuun war our great-grandmothers lost sleep to. The source is found: a drowned rift off the Vesayen Isles."
- **Clue:** "Repel the Slithis beach-head."
- **Play:** `repel:1` featured **Slithis Tide**.
- **Reward:** 2,400 XP · 420p.

### M100 · Thistledown — Thistledown Festival (festival · war-muster)
- **Crier:** "The hundredth month of the saga, and the festival DOUBLES as a war-muster: every realm's fair raises fleet-funds, every ribbon buys a plank. Dance, then donate."
- **Clue:** "Attend any fair and contribute to the fleet."
- **Play:** `visit` any capital + `contribute` 10 materials to the fleet-fund stand.
- **Reward:** 2,000 XP · 350p · *Fleet-Fund Ribbon* (keepsake).

### M101 · Verdantine — The Grand Alliance (beat)
- **Crier:** "SWORN AT VAL HALLA: the Concord, the Architect, Timaru's Aun, the Lugian nations, the truce-Burun of Green Mire — every banner of Dereth under ONE war-standard, for the first time in the history of this world. Your name belongs on that field."
- **Clue:** "Attend the signing of the Grand Alliance at Castle Val Halla."
- **Play:** `visit` Valstead/castle (all nations' banner props raised).
- **Reward:** 2,600 XP · 450p. **World:** Alliance banners fly at every capital through M120.

### M102 · Solclaim — Fleets of the Free Realms (item)
- **Crier:** "Every nation forges for the fleet: Aluvian oak, Gharu'ndim brass, Sho lacquer, Lugian stone-rivets, Aun bone-charms. ALLIANCE WAR-GEAR is in the armories — earn yours."
- **Clue:** "Claim a piece of Alliance war-gear from the shipwright quartermasters."
- **Play:** adds the **Alliance war-gear line** to loot/vendors; goal: obtain any Alliance piece.
- **Reward:** 2,200 XP · 380p · one guaranteed Alliance item.

### M103 · Leaffall — The Drowned Rift (explore)
- **Crier:** "The fleet SAILS. Marines hold the Vesayen approach while divers walk the sea-floor to the rift's rim — under the waves, under the pressure, under things with too many rows of teeth."
- **Clue:** "Take the underwater approach to the drowned rift's rim."
- **Play:** `clear` the underwater-approach delve (breath mechanics from F6; air-pocket rooms).
- **Reward:** 3,000 XP · 520p.

### M104 · Harvestgain — THE HERALD OF T'THUUN (boss · climax)
- **Crier:** "AT THE RIM IT WAITS — the Herald, a mountain of tide and shell that speaks its master's coming in breakers. The Alliance strikes this month. All of it. You too."
- **Clue:** "Slay the Herald of T'thuun at the drowned rift's rim."
- **Play:** saga boss — "The Herald of T'thuun" (×3.8, tidal-green, at the rift rim site) with slithis adds.
- **Reward:** 6,500 XP · 1,100p · title **Tide-Turner** · unique *The Herald's Hooked Crown* (head, v:34, frost-protect). **Seeds:** "Its death-cry rolls out to sea. And something below ANSWERS."

### M105 · Leafcull — The Answer From Below (beat)
- **Crier:** "Seaquakes every night since the Herald fell — and the rift is WIDER, friends, not narrower. The scholars say it plainly: the Herald was a doorman. The door is opening on schedule."
- **Clue:** "Witness the widening rift from the Vesayen overlook."
- **Play:** `visit` the Vesayen overlook.
- **Reward:** 2,300 XP · 400p.

### M106 · Coldeve — The Root Question (beat)
- **Crier:** "The Architect confirms what the Matriarchs always knew: only a FALATACOT LEYLINE ROOT can seal a god — and Dereth has exactly one left. You know where it grows. So does the man whose castle grows from it. He heard the news in silence."
- **Clue:** "Hear it from Asheron; then stand with Kilmer at the castle."
- **Play:** `visits:2` — Asheron's courtyard, then Castle Val Halla.
- **Reward:** 2,400 XP · 420p.

### M107 · Frostfell — Frostfell Festival (festival)
- **Crier:** "The quietest Frostfell of the saga. No games this year — just the feast, the folk, and candles. Everyone knows what next year is. This year's keepsake is a candle 'for the last light.'"
- **Clue:** "Keep the quiet feast at any capital."
- **Play:** `visit` any capital.
- **Reward:** 1,600 XP · 240p · *Candle for the Last Light* (keepsake).

### M108 · Snowreap — The Monarch's Walk (beat · seeds Year 10)
- **Crier:** "The Monarch walks ALONE — Holtburg, then Yaraq, then Shoushi, on foot, no crown, asking each square the same question: 'If the crown must burn, will you still stand?' Walk with him if your legs and heart can match his."
- **Clue:** "Escort Kilmer's walk: stand with him at all three capitals."
- **Play:** `visits:3` — the three capitals in order (Kilmer walker prop at each).
- **Reward:** 2,800 XP · 480p. **Seeds:** "Three squares. Three answers, roared. Year 10 — the Year of the Crown — will end the saga."

---

## YEAR 10 — YEAR OF THE CROWN (The Final Sending) — months 109–120
*T'thuun rises. Every month is a front. Kilmer must feed the root everything it ever gave him.*
**Needs:** multi-front invasions (3 simultaneous incursions) · `survive` goal (timed onslaught with
an unkillable boss) · `relics` ceremony (checks veterans' saga items from Y1/Y2/Y5/Y6/Y8) · the
3-stage finale (`stages` goal) · castle-lights-out world state (the plateau's glow removed after M120).

### M109 · Morningthaw — The Rising (invasion)
- **Crier:** "IT BEGINS. T'thuun's vanguard storms every coast at once — the drowned shores are war-fronts now, and the Alliance standard is UP. To your posts, friends. All of you."
- **Clue:** "Repel the vanguard — twice; the coasts cannot trade blows alone."
- **Play:** `repel:2` featured **Slithis Tide**.
- **Reward:** 2,800 XP · 480p.

### M110 · Seedsow — The Sunken Roads (beat)
- **Crier:** "The Slithis are IN THE TOWN NETWORK — the under-passages run black with them, and the hub lamps gutter wing by wing. If the roads drown, every realm fights alone."
- **Clue:** "Purge the Network's under-passages and re-light the hub."
- **Play:** `kills:8` slithis-family within the Town Network instance (instance-kill support).
- **Reward:** 2,900 XP · 500p. **World:** the hub lamps burn again.

### M111 · Leafdawning — The First Capital Falls (invasion)
- **Crier:** "SHOUSHI HAS FALLEN. Say it plainly, because it has never been said: a Concord capital is TAKEN — siege-state, gates lost, garrison driven to the gardens. The Alliance marches to take it back. NOW."
- **Clue:** "Break the siege of Shoushi and liberate the capital."
- **Play:** `siege:1` at Shoushi (liberation variant — the waves defend, you assault).
- **Reward:** 3,400 XP · 580p.

### M112 · Thistledown — The Last Festival (festival)
- **Crier:** "Thistledown is held IN SHOUSHI'S RUBBLE — flowers on the broken gates, music in the burned gardens, the whole Alliance dancing in a liberated ruin. If the saga has one image, friends, this is it. Be in it."
- **Clue:** "Keep the feast in liberated Shoushi."
- **Play:** `visit` Shoushi.
- **Reward:** 2,400 XP · 400p · title **Kept the Feast**.

### M113 · Verdantine — The Weeping Root (beat)
- **Crier:** "The under-temple RESONATES with the rising god — the castle lights gutter in time with the sea now, and Valstead is ordered EMPTY. The Monarch carried the first evacuee's trunk himself."
- **Clue:** "Help empty Valstead: see the evacuation through."
- **Play:** `visits:2` — Valstead muster point, then the refugee camp on the Holtburg road.
- **Reward:** 2,700 XP · 460p. **World:** Valstead stands empty; the castle's glow flickers (until M120).

### M114 · Solclaim — Three Fronts (invasion)
- **Crier:** "THREE GRAND-INCURSIONS AT ONCE — Holtburg, Yaraq, Sanamar, simultaneous, coordinated, meant to split us. The Alliance splits INSTEAD OF BREAKING. Pick your front and HOLD IT."
- **Clue:** "Hold a front — break at least one of the three assaults (break all three for honor)."
- **Play:** three simultaneous incursions; `repel:1` required (+bonus reward at 3).
- **Reward:** 3,000 XP · 520p (+1,500 XP · 300p if all three are broken).

### M115 · Leaffall — The God Breaches (survival)
- **Crier:** "T'THUUN'S TRUE BODY BREACHED AT THE RIFT — no, we did not kill it; you do not kill the sea. Today the Alliance fought for ONE thing: everyone comes home. Everyone came home. Next time we fight for keeps."
- **Clue:** "Survive first contact at the rift zone and cover the fleet's withdrawal."
- **Play:** `survive` — 120 seconds in the rift zone while the unkillable T'thuun avatar rampages, then reach the evacuation point.
- **Reward:** 3,600 XP · 620p.

### M116 · Harvestgain — The Rite Prepared (beat · the veterans' ceremony)
- **Crier:** "The Falatacot rite is REBUILT — from ten years of relics: a broken seal, an audit page, a heart-bead, cardinal sigils, a sliver of the old door. If you carried the saga, adventurer, the rite has a ROLE for you. Bring what you kept."
- **Clue:** "Present your saga relics at the under-temple."
- **Play:** `relics` at the under-temple — each held saga relic (Y1 Seal Fragment, Y2 Ledger Page, Y5 Heart-Bead, Y6 Sigil, Y8 Sliver) grants a named ceremony role + bonus; missing relics can be re-earned from a relic-errand board (fallback quests).
- **Reward:** 3,200 XP · 550p (+400 XP per relic presented).

### M117 · Leafcull — The Debt Named (beat)
- **Crier:** "The Monarch spoke the WHOLE truth at last, in open square: the root, the borrowed luck, the plateau that was flat before any mason touched it — and then he signed the crown's entire treasury over to the realms. The Concord's vote to stand with him was UNANIMOUS. It took eleven seconds."
- **Clue:** "Stand in the square at Castle Val Halla as the debt is named."
- **Play:** `visit` Valstead/castle.
- **Reward:** 2,900 XP · 500p.

### M118 · Coldeve — The March to the Sea (invasion)
- **Crier:** "THE GRAND ALLIANCE MARCHES — a rolling, month-long escort war from the heartland to the Vesayen shore, every nation in column, every step contested. This is the largest thing our world has ever done ON PURPOSE. March."
- **Clue:** "Fight the column through: break the assaults, then reach the shore."
- **Play:** `repel:2` + `visit` the shore muster point.
- **Reward:** 3,800 XP · 650p.

### M119 · Frostfell — The Vigil (festival)
- **Crier:** "No games. No feast. Tonight every lifestone on Dereth burns with candles — a world-wide VIGIL for tomorrow. Attend one. Take a flame. You will want it where we are going."
- **Clue:** "Hold vigil at any lifestone."
- **Play:** `visit` any lifestone.
- **Reward:** 2,000 XP · 300p · *The Last Light* (candle — required to witness the M120 finale up close).

### M120 · Snowreap — ★ THE FINAL EVENT — THE LAST LIGHT OF VAL HALLA ★ (boss · finale)
- **Crier:** "IT IS TONIGHT. The Alliance holds the god at the rift — the champions hold the under-temple — and the Monarch feeds the root EVERYTHING it ever gave him, castle-light and all, while the Architect guides the lash across portalspace. One night, three battles, ten years. LAST LIGHT, FRIENDS. MAKE IT COUNT."
- **Clue:** "Stand your stage: the rift, the under-temple, or the leyline — and see the sealing done."
- **Play:** `stages` ×3, in sequence:
  1. **Hold the rift** — `repel` a maximum-scale Slithis assault at the shore;
  2. **Defend the under-temple** — `kills` waves at the castle as Kilmer feeds the root (the castle's
     lights visibly stream off the plateau like a comet's tail);
  3. **The sealing** — scripted climax at the leyline: the lash crosses portalspace, T'thuun is sealed
     beneath the Vesayen deeps under a Falatacot knot no key can turn.
- **Reward:** 8,000 XP · 1,500p · titles **Light of the Last Sending** (participant) and **The Tenfold**
  (all 10 Harvestgain climaxes complete — `calendarDone` check) · uniques *The Unlit Crown* (head, v:40)
  and *Rootless Blade* (sword, v:42, no affix — pointedly).
- **World (permanent):** Castle Val Halla goes DARK and ordinary — and stands. The plateau's golden
  kindness is gone; Valstead repopulates as just a town, a well-loved one. Kilmer descends to abdicate;
  the assembled realms refuse it by acclamation. The crown is re-given, owed to no root.

### M121 · Morningthaw — Epilogue: The Age of the Free Realms (festival · optional)
- **Crier:** "A coronation-anew at a dark, standing castle — and memorial stones at every arc's field. And hear this last: from today the criers' books carry 'REMEMBER WHEN' — the deeds of the adventurers who walked all ten years, by NAME. That's you, friend. That was always you."
- **Clue:** "Attend the coronation-anew, and hear yourself remembered."
- **Play:** `visit` Valstead/castle. **World:** crier rumor tables permanently gain "remember when" lines citing the player's title and world-firsts.
- **Reward:** 2,500 XP · 500p · *Memorial Stone Chip* (keepsake).

---

## Appendix — running totals the engine must track
- **Relic ledger (for M116):** Y1 *Broken Seal Fragment* · Y2 *Audit-Ledger Page* / *Cracked Simulacrum
  Mask* · Y5 *Ledger of Luck Pages* / *Matriarch's Heart-Bead* · Y6 *Cardinal Sigils* · Y8 *Sliver of the
  Old Door*. Keep them `stat:"quest"` and never vendor-purge them.
- **Title ledger:** Witness of the Crown · Bane of the Ember · True-Sighted · Pen-Breaker · Hivebane ·
  Isles-Sworn · Grael's Jailer · Root-Warden · Stormbreaker · Winter's End · Quiet-Bringer · Veil-Walker ·
  Tide-Turner · Kept the Feast · The Last Light (item) · Light of the Last Sending · The Tenfold.
- **Permanent world changes by year:** Y2 watch-crystals · Y3 hive-craters + burnt spires + Aun camp ·
  Y4 Sanamar governor camp + privateer loot line · Y5 under-temple + warded heart + Green Mire camp ·
  Y6 storm-rods + storm-forge imbue · Y7 Frore open · Y8 Asheron NPC · Y9 drowned shores + Alliance
  banners + war-gear line · Y10 dark castle + repopulated Valstead + "remember when" crier lines.
- **The Tenfold check:** `calendarDone` for y1m8 · y2m20 · y3m32 · y4m44 · y5m56 · y6m68 · y7m80 ·
  y8m92 · y9m104 · y10m120.
