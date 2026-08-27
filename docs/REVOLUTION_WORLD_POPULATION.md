# Revolution World Population — Evidence & Spawn-Function Audit (M3.9 Phases 5+6)

Date: 2026-08-27. Findings only; no code or world state was changed.

Evidence discipline: every claim below is tagged (a) RUNTIME = what the scripts in this repo actually do,
(b) REVOLUTION = sourced Revolution evidence with URL + confidence, or (c) INFERENCE = our reasoning.
Anything unsourced is marked **UNKNOWN** rather than guessed.

---

## Part 1 — Revolution historical evidence (target window 2008-2010)

Sources actually consulted (all publicly readable, no login needed):

| Source | URL |
|---|---|
| Official map page | https://www.revolutionuo.net/revolution_harita |
| Game guide | http://www.revolutionuo.net/oyun_rehberi |
| Changelog (guncellemeler) | http://www.revolutionuo.net/guncellemeler |
| Treasure system | https://www.revolutionuo.net/hazine_sistemi |
| Spawn-tracking system | https://www.revolutionuo.net/spawntakip_sistemi |
| Champion system | https://www.revolutionuo.net/champion_sistemi |
| Mount info | https://www.revolutionuo.net/binek_bilgileri |
| Forum (spot checks) | http://www.revolutionuo.net/forum/ (e.g. topic=24562) |

### 1.1 World structure

| Claim | Source | Date | Confidence |
|---|---|---|---|
| Revolution's world = **Sosaria + Ilshenar**; "all city and dungeon names in Sosaria and Ilshenar are indicated" on their map. No Trammel/Malas/Tokuno/Ter Mur is ever mentioned anywhere on site or changelog. | revolution_harita | page undated | HIGH |
| Dungeon entrances on their map show where they lead ("nereye gideceği belirtilmiştir") — i.e. standard dungeon layout, entrances on the overworld. | revolution_harita | — | HIGH |
| Custom village **Mintain** at the Britain north bridge, founded in-fiction by "Graham Ogilvy". Head Hunters HQ later moved there. | guncellemeler | 10.11.2008, 06.12.2010 | HIGH |
| Craftsmen city ("Zanaatkarlar"), moongate access, guard zone, requires 100.0 crafting skill. | guncellemeler | 04.03.2010 | HIGH |
| A brand-new dungeon was only added 05.05.2016 — i.e. during 2008-2010 the dungeon set was the stock Sosaria/Ilshenar set. | guncellemeler | 05.05.2016 | MEDIUM (inference from absence) |

### 1.2 Hostile creatures that demonstrably existed (named in official text)

| Creature | Evidence | Date | Confidence |
|---|---|---|---|
| **Dragon, Balron** | "Dragon, balron gibi güçlü yaratıkların ganimetleri büyük oranda arttırıldı" (loot greatly increased) | 14.05.2009 | HIGH |
| **Infernal** (Sphere-only creature, not OSI) | Infernal loot/HP doubled; carries 2×+18 weapons | 12.02.2011, 20.02.2011 | HIGH (existence pre-2011 implied; in-window status MEDIUM) |
| **Daemon, Wyvern, Drake, Ice Dragon, Wyrm** | "Daemon, Wyvern, Drake, Ice Dragon, Wyrm, Dragon, Balron, Infernal loot +100%" | 04.04.2012 | MEDIUM for 2008-2010 (roster listed in 2012) |
| **Energy Vortex** | EV HP reduction made permanent | 19.02.2009 | HIGH |
| **Ore elementals** | While mining you can encounter elementals from the ore you dig (oyun_rehberi) | — | MEDIUM |
| **Silver serpents, ratmen, hellhounds, direwolves, slimes, giant rats, ratman mages/archers** | Barracoon champion waves (Despise) | champion_sistemi (champion activated 19.02.2009) | HIGH |
| **Spectres, ghouls, wraiths, skeleton mages, mummies, liches, skeletal knights, lich lords, rotting corpses** | Neira champion waves (Deceit) | champion_sistemi (activated 07.04.2016) | HIGH that these creatures exist; **UNKNOWN whether Deceit champion existed 2008-2010** |
| **Mongbats, imps, gargoyles, harpies, stone/fire gargoyles, scorpions** | Semidar champion waves (Fire dungeon) | champion_sistemi (activated 07.04.2016) | same caveat as Neira |
| **Golem** | Golem spawn added to Sosaria | 29.03.2016 | HIGH it did NOT exist as world spawn in-window |
| Tameable mounts in the wild: Horse, Llama, Desert/Forest/Mid/Frenzied Ostard, **Mustang, Shire** (custom), Kiirin, Unicorn, Nightmare, **Steed** (custom, STR 950), **Pegasus, Chyrsoar** (custom) | binek_bilgileri + spawntakip | spawntakip quotas dated 15.04.2008 | HIGH |

### 1.3 Where hostile creatures were (locations)

| Claim | Source | Date | Confidence |
|---|---|---|---|
| **Despise** was an active hunting dungeon; Barracoon the Piper champion ran there from Feb 2009 (in-window). | guncellemeler + champion_sistemi | 19.02.2009 | HIGH |
| **Deceit** (Neira) and **Fire** (Semidar) are champion dungeons — activation announced 07.04.2016; their pre-2010 spawn content is **UNKNOWN**. | champion_sistemi | 07.04.2016 | HIGH for dungeon identity, UNKNOWN for 2008-2010 |
| Mount spawns appear anywhere on the map that is **not water, guard zone, or dungeon** (weekly calendar of 49 mounts). | spawntakip_sistemi | system dated to 15.08.2007/15.04.2008 | HIGH |
| Which specific classic dungeons (Covetous, Destard, Shame, Hythloth, Wrong, Ice, Khaldun...) were active and what spawned in them 2008-2010: **UNKNOWN** — forum searches surfaced no composition lists. | — | — | UNKNOWN |
| Sea/ocean hostile spawns: **UNKNOWN** (fishing yields S.O.S bottles and shells; no sea-monster statements found). | hazine_sistemi, guncellemeler | — | UNKNOWN |
| Graveyard spawns: **UNKNOWN** (no direct statement found). | — | — | UNKNOWN |

### 1.4 Spawn density / respawn timing

- **UNKNOWN.** The only quantified respawn data found anywhere is the mount system: **49 mounts per week (~7/day)** on a Monday-generated calendar (spawntakip_sistemi, HIGH). No statement about monster respawn rates, spawner counts, or dungeon density was found. Do not invent numbers.
- Related tuning signals (not rates): "Fixed animal spawn protections; main guardians spawn with creatures" (09.02.2009); "Spawn protector count reduced to 3, excluding Nightmare/Unicorn/Steed" (19.02.2009); camping dead at spawn points was bannable (04.05.2008) — spawns were contested. All HIGH, from guncellemeler.

### 1.5 Loot and drops that fed the economy

All from guncellemeler unless noted; confidence HIGH (dated official changelog).

| Drop | Evidence / date |
|---|---|
| **Gold** from creatures, repeatedly retuned: -10% (11.11.2007); non-gold loot +20-50% (23.04.2008); +30-40% creature gold (15.06.2009); powerful-creature gold +100% (30.06.2009) | multiple 2007-2009 |
| **Özel deri (special leather/hides)** from Dragon/Balron corpses: 2 → 3 pieces (13.02.2009); Dragon/Infernal 2 → 3 (19.03.2009) | 2009 |
| **Pagan reagents** (volcanic ash etc.) drop from powerful creatures; volcanic ash needed for spirit of nitre (alchemy) | 12.03.2008, 12.04.2009 |
| **Magic weapons** in creature/treasure loot with fixed school distribution: Fencing 25%, Swords 25%, Mace 25%, Archery 12.5%, Axe 12.5% | 12.04.2009 |
| **Treasure maps** (S.O.S bottles) drop from fishing/net, and from 18.05.2009 also mining/lumberjacking; Dragon loot's map-drop chance +200% | 18.05.2009 |
| Treasure chests drop **enchanted leather set pieces** | 09.02.2009 (also 02.02.2016) |
| **Magical quarterstaff & gnarled staff** added to creature loot | 22.02.2011 (post-window) |
| Head Hunters: bounty gold for player heads (monthly 1M/500k/250k prizes) | 10.11.2008, 03.07.2009 |
| Monks buy **fame** (2gp per fame) in Delucia/Britain/Mintain — killing monsters monetized via fame | 05.11.2008, 10.11.2008 |
| **Iron/silver ore** spawn balance actively tuned (iron up + silver down 14.02.2009; silver up 12.04.2009) — silver ore is an economy resource | 2009 |

### 1.6 Treasure system & guardians (hazine_sistemi, HIGH unless noted)

- Two treasure tracks: **map treasures** (from S.O.S bottles) and **fixed treasure chests** ("sabit hazine sandıkları") placed in the world/dungeons (guncellemeler 11.11.2007, 15.11.2007).
- Map levels **2-5**; Lockpicking 40/60/80/100 to open; Cartography 40/60/80/100 to read (oyun_rehberi).
- "Her seviye hazine haritasında daha güçlü hazine koruyucuları mevcuttur" — each level has stronger **treasure guardians**; unlocking the chest wakes them; all must die before the chest opens; opening exposes you to a very strong poison. **Which creatures serve as guardians per level: UNKNOWN** (not listed).
- Fixed chests: locations randomized 15.11.2007; by 22.03.2012 there were **15 fixed chests** and their guardians were removed (post-window change). Count during 2008-2010: **UNKNOWN**, but the order of magnitude (~15, not hundreds) is a MEDIUM-confidence anchor.

### 1.7 Sphere-heritage signals

Creature names **Infernal**, **Ice Dragon**, **Steed**, custom mounts **Mustang/Shire/Pegasus/Chyrsoar**, ground "reagent crystal", mage robes (earth/ice/fire/energy) as combat gear — all Sphere-scene Turkish-shard idioms, none OSI. Confirms Revolution fidelity means **Sphere-era content**, not OSI publish history. (HIGH, aggregated from guncellemeler + binek_bilgileri.)

---

## Part 2 — Runtime spawn-function audit

Source tree: `C:\Projects\RevolutionOffline\runtime\scripts\functions\worldgen\spawns\`
Engine: `world_spawner.scp` (Soulless v4.0, Nerun's-Distro-derived) → `f_create_spawner` places `i_worldgem_bit` spawners.
Line format: creature defname lists ×6, x/y/z, facet, **TIMELO/TIMEHI in minutes**, walk/home range, then per-list amounts.
Typical runtime values: dungeon mobs respawn 2-10 min, treasure chests 60-120 min. **(RUNTIME fact — this is Nerun's Distro's timing, NOT Revolution evidence.)**
`TreasureLevelN` maps to `i_dungeon_chest1..4` — fixed dungeon treasure chests (matches Revolution's "sabit hazine" concept, but the runtime places **hundreds** vs Revolution's ~15 in 2012 → density mismatch, INFERENCE/MEDIUM).

Classifications: REVOLUTION_CLOSE_MATCH / GENERIC_SPHERE_ACCEPTABLE_BASE (GSAB) / TOO_LATE_ERA / WRONG_CONTENT / UNKNOWN.
Note: nothing in the tree is a verified REVOLUTION_CLOSE_MATCH — all spawn data is Nerun's Distro OSI-like data, so the best rating earned is GSAB (with Revolution corroboration noted where it exists).

### 2.1 Felucca (facet the project actually uses)

| Function | File | Places (summary) | Spawner calls | Region | Classification |
|---|---|---|---|---|---|
| `spawn_covetous_felucca` | Covetous_spawns_felucca.scp | Harpies, headless, gazers, corpsers, spiders, water eles; L3 undead (mummy/lich/bone knight); Dragon+Drake lair; 12 treasure chests | 46 | Covetous L1-3 (5387-5617, 1799-2035) | GSAB |
| `spawn_deceit_felucca` | Deceit_spawns_felucca.scp | All-undead: skeletons, zombies, ghouls, shades, wraiths, bone magi, liches, lich lord, poison/fire eles, silver serpent | 36 | Deceit L1-4 (5139-5342, 531-748) | GSAB — dungeon identity corroborated by Revolution (Neira in Deceit; undead waves match) |
| `spawn_despise_felucca` | Despise_spawns_felucca.scp | Lizardmen (L1), ettins+earth eles (L2), trolls/ogres/ogre lord/cyclops/acid ele (L3) | 43 | Despise (5386-5617, 525-996) | GSAB — Despise confirmed active on Revolution (Barracoon 2009); exact composition UNKNOWN |
| `spawn_destard_felucca` | Destard_spawns_felucca.scp | Dragons, drakes, wyverns, giant serpents, water/fire eles, daemon, shadow wyrm, **Ancient Wyrm** | 28 | Destard L1-3 (5141-5352, 778-1007) | GSAB — matches Revolution's dragon-centric economy (özel deri), placement evidence UNKNOWN |
| `spawn_fire_dungeon_felucca` | Fire_spawns_felucca.scp | Fire eles, hellhounds/hellcats, efreets, lava fauna, evil mages, liches, daemons + 8 chests | 50 | Fire dungeon (5638-5867, 1291-1471) | GSAB — Fire is Semidar's dungeon on Revolution (2016 evidence; in-window UNKNOWN) |
| `spawn_hythloth_felucca` | Hythloth_spawns_felucca.scp | Imps, hellhounds, gargoyles (stone/fire), gazers, daemons, 2 balrons + ~40 chests | 81 | Hythloth L1-4 (5911-6125, 25-235) | GSAB |
| `spawn_ice_dungeon_felucca` | Ice_spawns_felucca.scp | Frost fauna, ice/snow eles, frost trolls, ratmen room, white wyrm, arctic ogre lords, ice fiends + 20 chests | 34 | Ice dungeon (5666-5867, 139-363) | GSAB |
| `spawn_shame_felucca` | Shame_spawns_felucca.scp | Earth/air/water/fire/poison/blood/acid/dull-copper eles, scorpions, krakens, evil mages, elder gazers + ~60 chests | 114 | Shame L1-4 (5390-5875, 10-235) | GSAB |
| `spawn_wrong_felucca` | Wrong_spawns_felucca.scp | **Juka** lord/mage/warrior, golem+controller(→evilmage), brigands | 9 | Wrong (5659-5857, 530-591) | GSAB with caveat — Juka/golem are LBR (2001-02) invasion content; pre-AoS renderable, Revolution evidence UNKNOWN |
| `spawn_orc_caves_felucca` | OrcCaves_spawns_felucca.scp | Orcs, orc lords/captains/mages/bombers/brute, dire wolves | 19 | 3 orc caves (5141-5356, 1304-2029) | GSAB |
| `spawn_khaldun_felucca` | Khaldun_spawns_felucca.scp | Khaldun undead: shadow fiends, cursed, zealots/summoners, ancient liches, spectral armour, Harrower tentacles | 47 | Khaldun (5393-5605, 1294-1485) | GSAB (1999 T2A content) — Revolution usage UNKNOWN |
| `spawn_britain_sewer_felucca` | BritainSewer_spawns_felucca.scp | Sewer rats, bullfrogs, alligators | 21 | Britain sewer (6034-6115, 1438-1494) | GSAB |
| `spawn_solen_hive_felucca` | SolenHive_spawns_felucca.scp | Black solen workers/warriors/queens, ant lions, beetles, red solen town infiltrators | 42 | Solen hive (5665-5920, 1793-2024) + 3 town spots | GSAB with caveat — LBR (2002); Revolution evidence UNKNOWN |
| `spawn_terathan_keep_felucca` | TerathanKeep_spawns_felucca.scp | Terathans + Ophidians, and **Balron+Dragon+Nightmare at every point** | 34 | Terathan Keep (5132-5365, 1551-1757) | GSAB with caution — the balron/dragon/nightmare overlay at all 11 points is a Nerun quirk, very high-end density; UNKNOWN vs Revolution |
| `spawn_lost_lands_felucca` | LostLands_spawns_felucca.scp | Full T2A biome set: desert (orcs/ostards/wyverns), ice field, lava, swamp (incl. 2 plague beasts), savages, ophidian/terathan surface, wildlife | 167 | Lost Lands (5132-6118, 2359-4005) | GSAB (T2A 1998; savages are UOR 2000) |
| `spawn_graveyards_felucca` | Graveyards_spawns_felucca.scp | Skeletons, zombies, spectres/wraiths/shades, liches (Cove, Moonglow) | 8 | Britain, Jhelom, Cove, Moonglow, Vesper, Yew, Haven graveyards | GSAB |
| `spawn_outdoors_felucca` | Outdoors_spawns_felucca.scp | Overland hostiles: orcs (221), ettins, trolls, lizardmen, ogres, ratmen, harpies, gazers, brigand/orc/rat camps, Yew liches, Fort of the Damned; small bog section with boglings/plague spawns | 445 | Whole Sosaria overworld | GSAB — bog/plague sub-section is LBR-flavored; a few AcidElemental placements |
| `spawn_wild_life_felucca` | WildLife_spawns_felucca.scp | 19 tame/wild animals + WanderingHealer per point | 178 | Whole map grid | GSAB |
| `spawn_sea_life_felucca` | SeaLife_spawns_felucca.scp | Water ele, sea serpent, deep sea serpent, kraken, dolphin, sea horse per point | 115 | All seas | GSAB — Revolution sea spawn UNKNOWN |
| `spawn_reagents_felucca` | Reagents_spawns_felucca.scp | Ground reagents (8 kinds), 60 spots × up to 160 items, 11-23 min | 60 | Overworld | GSAB — Sphere-era shards commonly had ground reagents; Revolution evidence for ground spawns UNKNOWN (Revolution had NPC reagents + reagent crystal) |
| `spawn_towns_life_felucca` | TownsLife_spawns_felucca.scp | Town animals, criers, escorts, guards... **but includes a Heartwood section with 20+ ML elf NPCs** | 113 | All towns | GSAB except Heartwood block = WRONG_CONTENT (ML elves, client cannot render elf bodies) |
| `spawn_vendors_felucca` | Vendors_spawns_felucca.scp | ~345 shop vendors in all towns; includes a **"Sea Market"** section (High Seas, 2010+) | 345 | All towns | GSAB except Sea Market block = TOO_LATE_ERA |
| `spawn_quest_npcs_felucca` | Quest_spawns_felucca.scp | 327 ML-era quest NPCs — Heartwood/Sanctuary elves (Lorekeepers, Arcanists...), apprentice **necromancer** trainer, New Haven trainers | 327 | Heartwood (7000,370±), Sanctuary, towns | **WRONG_CONTENT** — ML/AoS quest layer, elf bodies + necromancy NPC; client ships skills 0-48 |
| `spawn_blighted_grove_felucca` | BlightedGrove_spawns_felucca.scp | Boglings, bog things, **changeling, abscess (hydra), hydra, thrasher, insane dryad, saliva** | 8 | Blighted Grove (6489-6585, 843-897) | **TOO_LATE_ERA** — ML (2007) dungeon; hydra/changeling/dryad bodies are ML art the client cannot render |
| `spawn_painted_caves_felucca` | PaintedCaves_spawns_felucca.scp | **Troglodytes, Lurg, Grobu** + pets | 2 | Painted Caves (6275-6279, 879) | **TOO_LATE_ERA** — ML; troglodyte body unrenderable |
| `spawn_palace_of_paroxysmus_felucca` | PalaceOfParoxysmus_spawns_felucca.scp | Plague beasts/lords/spawns, corrosive slimes, **interred grizzles**, poison eles, balrons, succubi, chaos daemon, moloch | 14 | PoP (6249-6466, 348-616) | **TOO_LATE_ERA** — ML dungeon; interred grizzle/plague beast lord ML bodies |
| `spawn_prism_of_light_felucca` | PrismOfLight_spawns_felucca.scp | Crystal daemon/hydra/wisp/vortex/lattice seeker (custom defs; lattice seeker uses **Lord Oaks champ body**, crystal hydra uses ML hydra body), unfrozen mummy, protectors | 21 | Prism of Light (6474-6578, 74-183) | **TOO_LATE_ERA** — ML (2007) dungeon; several bodies unrenderable |
| `spawn_dungeons_felucca` (aggregate) | Groups_spawns_felucca.scp | Calls the **18** functions listed in 2.3 | — | — | mixed, see 2.3 |
| `f_spawn_magincia` / `f_spawn_npcs_oldmagincia` | spawn_magincia.scp (spawns root dir) | Magincia vendors + escorts + town animals | 18 / 28 | Magincia | GSAB |
| `spawn_uoclassic` | uoclassic/UOClassic_spawns.scp | UO Rebirth **pre-T2A** spawn set (rats, townsfolk, rangers, animals, some monsters) — 1,770 spawners | 1770 | Whole Sosaria | GSAB — era-safe but replicates 1997 OSI, not Revolution; do not stack on top of the Nerun set (massive double-population) |

Supporting (not spawn functions): `spawner_defs.scp` maps friendly names → chardefs (several ML gaps papered over with substitutes: Putrefier→balron, Golemcontroller→evilmage, Shade→wraith, protector→wisp). `scripts/spawns/*.scp` are stock Sphere `[SPAWN]` group resources for hand-placed spawn gems, not callable functions; they include SA-era ids (c_boura, c_skittering_hopper) — harmless unless used.

### 2.2 Other facets

| Group function | Files | Content | Classification |
|---|---|---|---|
| `spawn_dungeons_trammel`, `spawn_*_trammel` (mirror set, 26 files) | trammel/ | Byte-for-byte mirror of the felucca set on facet 1 | **WRONG_CONTENT** for this project — Revolution had no Trammel facet (single Sosaria); running it doubles the world onto a facet bots/humans never use |
| `spawn_dungeons_ilshenar` (ankh, blood, exodus, rock, sorcerers, spectre, twisted_weald, wisp), `spawn_caves_ilshenar` (ancient lair, mushroom, ratman cave), `spawn_outdoors_ilshenar`, `spawn_towns_ilshenar`, `spawn_vendors_ilshenar` | ilshenar/ (15 files, ~430 spawners) | LBR-era Ilshenar: Exodus minions/overseers, Meer, blood daemons, ancient wyrm lair, ratman cave, wisp dungeon, serpentine dragons | GSAB — Revolution DID have Ilshenar (map page, HIGH); LBR content renders in a 2002+ client. **Exception: `spawn_twisted_weald_ilshenar` = TOO_LATE_ERA** (ML: changelings, satyrs, Cu Sidhe, dryads, ML named spiders) |
| `spawn_dungeons_malas` (Doom, Bedlam, Citadel, Labyrinth), north/south/orcforts/vendors | malas/ | AoS/ML Malas facet incl. Doom gauntlet | **WRONG_CONTENT** — AoS (2003)+ facet, Revolution never had Malas; client cannot render much of it |
| `spawn_dungeons_tokuno`, outdoors/towns/vendors, Fan Dancers Dojo, Yomotsu Mines | tokuno/ | SE (2004) facet, yomotsu/fan dancers | **WRONG_CONTENT** — SE era + unrenderable bodies |
| `spawn_outdoors_ter_mur`, underworld, abyss, vendors | termur/ | SA (2009) gargoyle facet | **WRONG_CONTENT** — gargoyle race/SA bodies, era and client both wrong |

### 2.3 The already-executed `spawn_dungeons_felucca` — forensic review

`Groups_spawns_felucca.scp` actually calls **18 functions** (milestone notes said "14 dungeons"; the extra entries are sewer/caves/sub-areas): blighted_grove, britain_sewer, covetous, deceit, despise, destard, fire, hythloth, ice, orc_caves, painted_caves, khaldun, palace_of_paroxysmus, prism_of_light, shame, solen_hive, terathan_keep, wrong.

**Era-wrong content now live in the world (prune candidates):**

1. **Blighted Grove** (8 spawners, ~6490-6590 / 840-900) — ML 2007. Changeling, hydra, insane dryad bodies do not exist in a skills-0-48-era client.
2. **Painted Caves** (2 spawners, 6275/879) — ML 2007. Troglodyte body unrenderable.
3. **Palace of Paroxysmus** (14 spawners, 6249-6466 / 348-616) — ML 2007. Interred grizzle, plague beast lord, corrosive slime bodies unrenderable; also spawns ~9 balrons/succubi/daemons per point (economy-distorting).
4. **Prism of Light** (21 spawners, 6474-6578 / 74-183) — ML 2007. Crystal-creature customs partly re-skin classic bodies but crystal hydra (ML hydra body) and lattice seeker (Lord Oaks champion body) are wrong; whole dungeon is post-era.
5. Watch item, not prune: **Terathan Keep** overlays a Balron+Dragon+Nightmare spawner at all 11 points — legitimate classic creatures but an unusually rich farming spot; Revolution comparison UNKNOWN.
6. Acceptable-with-caveat: **Wrong** (Juka/golems, LBR) and **Solen Hive** (LBR) are pre-AoS and renderable but have zero Revolution evidence; keep or prune on taste, they are not client-breaking.

The remaining 12 (Covetous, Deceit, Despise, Destard, Fire, Hythloth, Ice, Khaldun, Shame, Britain Sewer, Orc Caves, Terathan Keep) are classic pre-2000 dungeon sets and are safe **as a generic base**; their fidelity to Revolution's actual numbers remains UNKNOWN.

### 2.4 Functions I would NOT run (and why)

| Function | Reason |
|---|---|
| `spawn_quest_npcs_felucca` | 327 ML Heartwood/Sanctuary elf quest NPCs + necromancer trainer — wrong era, unrenderable race |
| `spawn_blighted_grove_felucca`, `spawn_painted_caves_felucca`, `spawn_palace_of_paroxysmus_felucca`, `spawn_prism_of_light_felucca` | ML dungeons, unrenderable bodies (already executed via the group — prune candidates) |
| Everything under `trammel/` | Facet Revolution never had; pure duplication |
| Everything under `malas/`, `tokuno/`, `termur/` | AoS/SE/SA facets and creatures — wrong era, wrong client |
| `spawn_twisted_weald_ilshenar` (and via it, be careful with `spawn_dungeons_ilshenar`, which includes it) | ML dungeon inside otherwise-usable Ilshenar set |
| `spawn_uoclassic` *on top of* the Nerun set | Not era-wrong, but 1,770 additional spawners duplicating town/animal population — choose one baseline, not both |
| The "Sea Market" block inside `spawn_vendors_felucca` and the Heartwood block inside `spawn_towns_life_felucca` | High Seas (2010+) and ML sub-sections inside otherwise-fine functions — edit before running |

---

## Biggest UNKNOWNs (do not fill by guessing)

1. Per-dungeon creature composition and counts on Revolution 2008-2010 (only Despise/Barracoon is directly evidenced in-window).
2. Monster respawn timing and spawner density — no Revolution numbers exist anywhere found; runtime 2-10 min values are Nerun defaults.
3. Which guardians defend each treasure-map level, and the in-window count/locations of fixed treasure chests (~15 by 2012).
4. Sea, graveyard, and mine hostile spawns on Revolution.
5. Whether Neira (Deceit) and Semidar (Fire) champions existed before 2016.
6. Stats/loot tables of Revolution's custom top-tier creature "Infernal" and of "Ice Dragon" (names confirmed; numbers unknown).
