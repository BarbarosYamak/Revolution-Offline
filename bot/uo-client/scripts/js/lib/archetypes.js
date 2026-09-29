'use strict';
// Every character type the bots can be: fighters, crafters and gatherers.
// ONE table, read by the fighter/crafter/gatherer engines (lib/fighter.js,
// lib/crafterbot.js, lib/gatherer.js) and exported to
// data/revolution_archetypes.tsv for the C++ persistent record -- the smoke
// test fails if that file drifts from this one.
//
// EVIDENCE, stated per row, never upgraded:
//   build.ref / build.cls   the Build Compendium entry and its classification
//                           (HISTORICAL_EXACT > HISTORICAL_NEAR_EXACT >
//                            HISTORICAL_FAMILY > REVOLUTION_DERIVED > UNSOURCED)
//   A FAMILY/DERIVED build names its core skills only; those go to 100 and the
//   rest of the 700 stays UNALLOCATED -- it is decided later on evidence, not
//   filled with a guess.
//   stats                   one of the ten ATTESTED 225-point splits
//                           (REVOLUTION_RULESET_PROFILE.md s.4). None is
//                           crafter-specific; that is noted, not hidden.
//   craft.flow              PROVEN (a live scenario did it) or UNVERIFIED (the
//                           Sphere-standard flow, not yet run on this shard).
//   craft.material.policy   what data/revolution_vendor_policy.tsv says about
//                           buying the input from an NPC. BLOCKED means the
//                           policy refuses it and the input must be gathered or
//                           bought from a player.
(function (g) {
    // Sphere SKILL_TYPE indices -- the same order rules.h uses.
    const SK = {
        ALCH: 0, ANAT: 1, LORE: 2, ITEMID: 3, ARMS: 4, PARRY: 5, BEG: 6, BS: 7, BOW: 8,
        CAMP: 10, CARP: 11, CARTO: 12, COOK: 13, DETECT: 14, EVAL: 16, HEAL: 17, FISH: 18,
        HIDING: 21, INS: 23, LOCK: 24, MAGERY: 25, TACT: 27, SNOOP: 28, MUSIC: 29, POI: 30,
        ARCH: 31, STEAL: 33, TAILOR: 34, TAMING: 35, TINK: 37, TRACK: 38, VET: 39, SW: 40,
        MACE: 41, FENC: 42, WREST: 43, LUMBER: 44, MINING: 45, MEDI: 46, STEALTH: 47,
    };
    const INACTIVE = [9, 15, 19, 20, 22, 26, 32, 36, 48];   // rules.h InactiveSkills

    // The ten attested 225-point splits, as [STR, DEX, INT].
    const STATS = {
        warrior: [100, 100, 25],   // appears twice (warlock + thief threads)
        warlock: [90, 100, 35],    // appears twice
        warlock_int: [85, 100, 40],
        warlock_balanced: [98, 97, 30],
        thief: [50, 100, 75],
        caster: [25, 100, 100],
    };

    const b = (ref, cls, skills) => ({ ref, cls, skills });

    // ---- where things are (data/revolution_atlas.txt PLACEs / REGIONs) ----
    const TOWNS = {
        britain: { bank: { x: 1425, y: 1690 }, healer: { x: 1471, y: 1611 },
            vendors: { blacksmith: { x: 1418, y: 1547 }, armorer: { x: 1481, y: 1584 }, tailor: { x: 1547, y: 1659 },
                bowyer: { x: 1470, y: 1578 }, mage: { x: 1485, y: 1550 }, alchemist: { x: 1498, y: 1659 },
                provisioner: { x: 1469, y: 1668 }, carpenter: { x: 1430, y: 1597 }, tanner: { x: 1431, y: 1612 } } },
        yew: { bank: { x: 652, y: 820 }, healer: { x: 540, y: 966 },
            vendors: { bowyer: { x: 570, y: 969 }, tanner: { x: 517, y: 986 }, provisioner: { x: 535, y: 867 }, carpenter: { x: 564, y: 1011 } } },
        vesper: { bank: { x: 2881, y: 684 }, healer: { x: 2920, y: 856 },
            vendors: { blacksmith: { x: 2835, y: 805 }, tailor: { x: 2961, y: 621 }, bowyer: { x: 2861, y: 812 }, mage: { x: 2890, y: 651 },
                alchemist: { x: 2992, y: 844 }, provisioner: { x: 2986, y: 637 }, carpenter: { x: 2917, y: 798 }, tinker: { x: 2899, y: 790 }, tanner: { x: 2860, y: 999 } } },
        minoc: { bank: { x: 2503, y: 552 }, healer: { x: 2577, y: 599 },
            vendors: { blacksmith: { x: 2471, y: 564 }, armorer: { x: 2533, y: 572 }, tinker: { x: 2461, y: 457 }, provisioner: { x: 2526, y: 546 },
                carpenter: { x: 2513, y: 477 }, tanner: { x: 2522, y: 524 } } },
        trinsic: { bank: { x: 1813, y: 2825 }, healer: { x: 1911, y: 2805 },
            vendors: { provisioner: { x: 1852, y: 2831 }, mage: { x: 1846, y: 2715 }, tanner: { x: 1991, y: 2867 } } },
        skara: { bank: { x: 587, y: 2146 }, healer: { x: 620, y: 2218 },
            vendors: { blacksmith: { x: 630, y: 2194 }, tailor: { x: 650, y: 2178 }, bowyer: { x: 590, y: 2205 }, mage: { x: 602, y: 2180 },
                provisioner: { x: 578, y: 2227 }, carpenter: { x: 627, y: 2163 } } },
        jhelom: { bank: { x: 1317, y: 3773 }, healer: { x: 1416, y: 3779 },
            vendors: { blacksmith: { x: 1419, y: 3859 }, tailor: { x: 1356, y: 3780 }, tinker: { x: 1404, y: 3802 }, provisioner: { x: 1442, y: 3802 },
                carpenter: { x: 1435, y: 3820 }, mage: { x: 1425, y: 3981 } } },
    };

    // Hunting grounds, easiest first. Graveyards: fixed M3.9 spawners
    // (f_m39_graveyards.scp) with the AREADEF rects. Wilds: atlas
    // resource_area "hunting" points (wandering wildlife), patrolled by radius.
    const GROUNDS = {
        trinsic_wilds:     { town: 'trinsic', level: 1, prey: 'wildlife', centre: { x: 1894, y: 2942 }, radius: 14 },
        skara_wilds:       { town: 'skara',   level: 1, prey: 'wildlife', centre: { x: 559, y: 2081 },  radius: 14 },
        britain_wilds:     { town: 'britain', level: 1, prey: 'wildlife', centre: { x: 1315, y: 2192 }, radius: 14 },
        yew_graveyard:     { town: 'yew',     level: 2, prey: 'undead', centre: { x: 722, y: 1119 },
            rects: [{ x1: 711, y1: 1103, x2: 735, y2: 1135 }] },
        britain_graveyard: { town: 'britain', level: 3, prey: 'undead', centre: { x: 1369, y: 1475 },
            rects: [{ x1: 1336, y1: 1443, x2: 1390, y2: 1493 }, { x1: 1336, y1: 1494, x2: 1375, y2: 1510 }] },
        vesper_graveyard:  { town: 'vesper',  level: 3, prey: 'undead', centre: { x: 2758, y: 867 },
            rects: [{ x1: 2727, y1: 839, x2: 2784, y2: 895 }] },
        jhelom_graveyard:  { town: 'jhelom',  level: 3, prey: 'undead', centre: { x: 1285, y: 3731 },
            rects: [{ x1: 1271, y1: 3711, x2: 1292, y2: 3742 }, { x1: 1279, y1: 3736, x2: 1295, y2: 3751 }] },
        cove_graveyard:    { town: 'minoc',   level: 4, prey: 'undead', centre: { x: 2438, y: 1100 },   // Cove has liches
            rects: [{ x1: 2423, y1: 1098, x2: 2454, y2: 1119 }, { x1: 2431, y1: 1080, x2: 2454, y2: 1098 }] },
    };

    // Bodies (lib/threat.js aggressive list + wild grays).
    const PREY_BODIES = {
        undead: [0x03, 0x32, 0x38, 0x18, 0x1a],
        wildlife: [0xe1, 0x19, 0xd3, 0xd4, 0xd5, 0x3f, 0x40, 0xd7, 0xee, 0x06, 0xcd],
    };

    // Weapons by skill (lowercased tiledata name substrings).
    const WEAPONS = {
        [SK.SW]: ['katana', 'broadsword', 'longsword', 'scimitar', 'cutlass', 'viking', 'axe', 'hatchet', 'bardiche', 'halberd'],
        [SK.FENC]: ['kryss', 'spear', 'war fork', 'short spear', 'dagger', 'pitchfork'],
        [SK.MACE]: ['mace', 'maul', 'war hammer', 'hammer pick', 'club', 'black staff', 'quarter staff', 'war axe'],
        [SK.ARCH]: ['bow', 'crossbow', 'heavy crossbow'],
        [SK.WREST]: [],
    };

    // ---- crafts ------------------------------------------------------------
    // tool:     backpack name substring of the tool (double-clicked)
    // target:   what the tool asks for after the double-click: null = the menu
    //           opens directly; {material:'x'} = target that pack material;
    //           {station:['oven','fire']} = target a nearby world item
    // station:  world items that must be within 3 tiles (Sphere IsItemTypeNear)
    // material: the input; policy per revolution_vendor_policy.tsv
    const CRAFTS = {
        blacksmith: { skill: SK.BS, tool: 'hammer', target: { material: 'ingot' }, station: ['forge', 'anvil'],
            flow: 'PROVEN', evidence: 'm37_slice_b_minoc.txt: equip hammer, use, target ingots, menu',
            material: { name: ['ingot'], policy: 'ALLOWED', vendor: 'blacksmith', target: 60, low: 12, perPiece: 12, alt: 'mine+smelt' },
            sets: { ringmail: ['ringmail tunic', 'ringmail sleeves', 'ringmail leggings', 'ringmail gloves'],
                    chainmail: ['chainmail tunic', 'chainmail leggings', 'chainmail coif'],
                    platemail: ['platemail', 'platemail arms', 'platemail legs', 'platemail gloves', 'platemail gorget', 'plate helm'] },
            customers: ['swordsman', 'fencer', 'macer', 'warlock', 'fencing_warlock', 'double_warlock', 'pk', 'archer'] },
        tinker: { skill: SK.TINK, tool: 'tinker', target: null, station: [],
            flow: 'PROVEN', evidence: 'm38_tinker_craft.txt, m391_craft_oracle.txt: use tools, menu',
            material: { name: ['ingot'], policy: 'ALLOWED', vendor: 'blacksmith', target: 40, low: 8, perPiece: 4 },
            sets: { 'tinker tools': ['tinker tools', 'scissors', 'pickaxe'] },
            customers: ['miner', 'tailor', 'lumberjack'] },
        tailor: { skill: SK.TAILOR, tool: 'sewing', target: { material: 'cloth' }, station: [],
            flow: 'PROVEN', evidence: 'm37_slice_a_sew.txt, m37_slice_a_finish.txt: shears on bolt -> cloth; kit, target cloth, menu',
            material: { name: ['cloth'], policy: 'ALLOWED_AS_BOLT', vendor: 'tailor', buy: ['bolt'], process: { tool: 'scissors', from: 'bolt' },
                target: 40, low: 10, perPiece: 10 },
            sets: { 'cloth outfit': ['shirt', 'pants', 'cloak'], leather: ['leather tunic', 'leather sleeves', 'leather leggings', 'leather gloves', 'leather gorget', 'leather cap'] },
            customers: ['pure_mage', 'warlock', 'tamer', 'archer'] },
        carpenter: { skill: SK.CARP, tool: 'saw', target: null, station: [],
            flow: 'PROVEN', evidence: 'm37_slice_d_carpenter.txt: chop logs, use saw, menu',
            material: { name: ['log'], policy: 'BLOCKED_GATHER', gather: 'lumber', target: 60, low: 10, perPiece: 10 },
            sets: {}, customers: [] },
        bowyer: { skill: SK.BOW, tool: 'dagger', target: { material: 'log' }, station: [],
            flow: 'UNVERIFIED', evidence: 'Sphere standard: a blade on logs opens Bowcraft/Fletching. Not yet run live.',
            material: { name: ['log'], policy: 'BLOCKED_GATHER', gather: 'lumber', target: 60, low: 10, perPiece: 10 },
            sets: { 'archer kit': ['bow', 'crossbow'] }, customers: ['archer', 'swordsman'] },
        alchemist: { skill: SK.ALCH, tool: 'mortar', target: { material: 'reag' }, station: [],
            flow: 'UNVERIFIED', evidence: 'Sphere standard: mortar & pestle, target a reagent, menu. Not yet run live.',
            material: { name: ['ginseng', 'garlic', 'mandrake', 'nightshade', 'black pearl', 'blood moss', 'spider', 'sulfur'],
                policy: 'ALLOWED', vendor: 'alchemist', target: 30, low: 6, perPiece: 3,
                blocker: 'empty bottles are UNKNOWN in the vendor policy (refused): potions need a bottle source -- players, or loot' },
            sets: { 'pvp kit': ['greater heal', 'greater cure', 'refresh'] }, customers: ['warlock', 'swordsman', 'fencer', 'macer', 'pure_mage'] },
        scribe: { skill: SK.INS, tool: 'pen', target: null, station: [],
            flow: 'UNVERIFIED', evidence: 'Sphere standard: pen & ink opens the Inscription menu. Not yet run live.',
            material: { name: ['scroll'], policy: 'BLOCKED_UNKNOWN', vendor: 'mage', target: 40, low: 10, perPiece: 1,
                blocker: 'blank scrolls are UNKNOWN in the vendor policy (refused): a scribe needs a player supplier' },
            sets: { 'travel kit': ['recall', 'mark'] }, customers: ['pure_mage', 'warlock', 'tamer'] },
        cook: { skill: SK.COOK, tool: 'raw', target: { station: ['oven', 'fire', 'forge'] }, station: [],
            flow: 'UNVERIFIED', evidence: 'Sphere standard: raw food on a heat source. Not yet run live.',
            material: { name: ['raw fish', 'raw', 'fish steak'], policy: 'BLOCKED_GATHER', gather: 'fish', target: 20, low: 5, perPiece: 1 },
            sets: {}, customers: [] },
    };

    // ---- the archetypes ----------------------------------------------------
    // kind: fighter | crafter | gatherer. style: melee | ranged | mage |
    // warlock | tamer | pk (fighters); craft key (crafters); gather key.
    const A = {
        // fighters
        swordsman: { kind: 'fighter', style: 'melee', primary: SK.SW, stats: 'warrior', home: 'britain',
            build: b('PW-01', 'HISTORICAL_EXACT', { TACT: 100, ANAT: 100, HEAL: 100, POI: 100, SW: 100, ARCH: 100, PARRY: 100 }) },
        archer: { kind: 'fighter', style: 'ranged', primary: SK.ARCH, secondary: SK.SW, stats: 'warrior', home: 'yew',
            build: b('PW-02', 'HISTORICAL_NEAR_EXACT', { MAGERY: 25, SW: 100, ARCH: 100, TACT: 100, PARRY: 100, POI: 80, HEAL: 95, ANAT: 100 }) },
        fencer: { kind: 'fighter', style: 'melee', primary: SK.FENC, stats: 'warlock', home: 'jhelom',
            build: b('PW-03', 'HISTORICAL_FAMILY', { FENC: 100, TACT: 100, ANAT: 100, HEAL: 100, POI: 100, PARRY: 100 }) },
        macer: { kind: 'fighter', style: 'melee', primary: SK.MACE, stats: 'warrior', home: 'minoc',
            build: b('PW-04', 'HISTORICAL_FAMILY', { MACE: 100, TACT: 100, ANAT: 100, HEAL: 100, PARRY: 100 }) },
        warlock: { kind: 'fighter', style: 'warlock', primary: SK.SW, stats: 'warlock', home: 'britain',
            build: b('WL-01', 'HISTORICAL_EXACT', { MAGERY: 100, SW: 100, TACT: 100, EVAL: 80, POI: 80, HEAL: 80, ANAT: 80, MEDI: 80 }) },
        fencing_warlock: { kind: 'fighter', style: 'warlock', primary: SK.FENC, stats: 'warlock_int', home: 'vesper',
            build: b('WL-03', 'HISTORICAL_EXACT', { FENC: 100, TACT: 100, ANAT: 100, POI: 100, MAGERY: 85, HEAL: 80, EVAL: 75, MEDI: 60 }) },
        double_warlock: { kind: 'fighter', style: 'warlock', primary: SK.SW, secondary: SK.MACE, stats: 'warlock_balanced', home: 'britain',
            build: b('WL-07', 'HISTORICAL_EXACT', { SW: 100, MACE: 100, TACT: 100, POI: 90, HEAL: 90, ANAT: 90, MAGERY: 80, MEDI: 50 }) },
        pure_mage: { kind: 'fighter', style: 'mage', primary: SK.MAGERY, stats: 'caster', home: 'vesper',
            build: b('PM-01', 'HISTORICAL_EXACT', { MAGERY: 100, MEDI: 100, EVAL: 100, POI: 100, HEAL: 100, ANAT: 100, INS: 100 }) },
        tamer: { kind: 'fighter', style: 'tamer', primary: SK.TAMING, stats: 'thief', home: 'britain',
            build: b('TM-02', 'REVOLUTION_DERIVED', { TAMING: 100, LORE: 100, VET: 100, MAGERY: 100, MEDI: 100, HEAL: 100 }) },
        pk: { kind: 'fighter', style: 'pk', primary: SK.SW, stats: 'warrior', home: 'britain',
            build: b('PK-02', 'HISTORICAL_FAMILY', { TACT: 100, ANAT: 100, HEAL: 100, POI: 100, SW: 100, ARCH: 100, PARRY: 100 }) },

        // crafters
        blacksmith: { kind: 'crafter', craft: 'blacksmith', stats: 'warrior', home: 'minoc',
            build: b('CR-01', 'REVOLUTION_DERIVED', { MINING: 100, BS: 100, ARMS: 100 }) },
        tinker: { kind: 'crafter', craft: 'tinker', stats: 'thief', home: 'britain',
            build: b('CR-06', 'REVOLUTION_DERIVED', { TINK: 100, MINING: 100 }) },
        tailor: { kind: 'crafter', craft: 'tailor', stats: 'caster', home: 'britain',
            build: b('CR-04', 'HISTORICAL_FAMILY', { TAILOR: 100, MAGERY: 100, EVAL: 100 }) },
        carpenter: { kind: 'crafter', craft: 'carpenter', stats: 'warrior', home: 'britain',
            build: b('CR-08', 'REVOLUTION_DERIVED', { CARP: 100, LUMBER: 100 }) },
        bowyer: { kind: 'crafter', craft: 'bowyer', stats: 'warrior', home: 'yew',
            build: b('CR-07', 'REVOLUTION_DERIVED', { LUMBER: 100, BOW: 100, ARCH: 100 }) },
        alchemist: { kind: 'crafter', craft: 'alchemist', stats: 'caster', home: 'britain',
            build: b('CR-03', 'REVOLUTION_DERIVED', { ALCH: 100, MAGERY: 100, POI: 100 }) },
        scribe: { kind: 'crafter', craft: 'scribe', stats: 'caster', home: 'vesper',
            build: b('CR-05', 'HISTORICAL_FAMILY', { INS: 100, MAGERY: 100, MEDI: 100 }) },
        cook: { kind: 'crafter', craft: 'cook', stats: 'thief', home: 'britain',
            build: b('-', 'UNSOURCED', { COOK: 100, FISH: 100 }) },

        // gatherers
        lumberjack: { kind: 'gatherer', gather: 'lumber', stats: 'warrior', home: 'trinsic',
            build: b('M4-plan', 'REVOLUTION_DERIVED', { SW: 100, TACT: 100, LUMBER: 100, HEAL: 100, ANAT: 100 }) },
        miner: { kind: 'gatherer', gather: 'mine', stats: 'warrior', home: 'minoc',
            build: b('CR-01', 'REVOLUTION_DERIVED', { MINING: 100, BS: 100 }) },
        fisher: { kind: 'gatherer', gather: 'fish', stats: 'thief', home: 'britain',
            build: b('FI-01', 'HISTORICAL_FAMILY', { FISH: 100 }) },
    };

    // ---- validation (a JS mirror of rules.h ValidateBuild + the stat cap) --
    function validate(id) {
        const a = A[id];
        if (!a) return { ok: false, why: `unknown archetype ${id}` };
        let total = 0;
        for (const [name, v] of Object.entries(a.build.skills)) {
            const skill = SK[name];
            if (skill === undefined) return { ok: false, why: `${id}: unknown skill ${name}` };
            if (INACTIVE.includes(skill)) return { ok: false, why: `${id}: ${name} is inactive on Revolution` };
            if (v < 0 || v > 100) return { ok: false, why: `${id}: ${name} ${v} outside 0..100` };
            total += v;
        }
        if (total > 700) return { ok: false, why: `${id}: ${total} over 700` };
        const s = STATS[a.stats];
        if (!s) return { ok: false, why: `${id}: unknown stat split ${a.stats}` };
        if (s.some((v) => v > 100) || s.reduce((x, y) => x + y, 0) > 225) return { ok: false, why: `${id}: stats over cap` };
        // L4: a Magery > 40 character must not WIELD a poisoned weapon. The
        // fighter engine never applies poison to its own weapon when this is
        // false (it may still train Poisoning and cast the Poison spell).
        return { ok: true, total, unallocated: 700 - total, canUsePoisonedWeapon: (a.build.skills.MAGERY || 0) <= 40 };
    }

    // The TSV the C++ record reads. Columns documented in the header line.
    function toTsv() {
        const lines = ['id\tkind\tstyle_or_trade\tref\tclass\tskills\tstr\tdex\tint\thome'];
        for (const [id, a] of Object.entries(A)) {
            const skills = Object.entries(a.build.skills).map(([n, v]) => `${SK[n]}:${v * 10}`).join(',');
            const [s, d, i] = STATS[a.stats];
            lines.push([id, a.kind, a.style || a.craft || a.gather, a.build.ref, a.build.cls, skills, s, d, i, a.home].join('\t'));
        }
        return lines.join('\n') + '\n';
    }

    g.Archetypes = { SK, INACTIVE, STATS, TOWNS, GROUNDS, PREY_BODIES, WEAPONS, CRAFTS, A, validate, toTsv };
})(globalThis);
