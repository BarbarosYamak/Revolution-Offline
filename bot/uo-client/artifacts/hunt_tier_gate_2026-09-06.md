# Hunting-ground tiers: can any bot clear the strong ring? (2026-09-06)

Verdict: **UNKNOWN — the strong tier is gated OFF for every character.**
The tier *plumbing* exists (`world_atlas::HuntTierOf`,
`Atlas::NearestHuntingGroundOfTier`); the *policy* that would let a character
through is not supported by evidence yet, so `ClearsStrongHuntTier`
(src/life/runner/Train.cpp) returns false for everyone.

## 1. What the two tiers actually contain

Source: `runtime/scripts/functions/worldgen/spawns/felucca/Graveyards_spawns_felucca.scp`
(the shard's own graveyard spawn table, rewritten 2026-09-02 on the TNS mix and
re-laid 2026-09-06 for the tier split). Chardef numbers:
`runtime/scripts/npcs/c_monster_classic.scp`.

| creature | tier | DAM | ARMOR | MAXHITS | STR | melee / magery |
|---|---|---|---|---|---|---|
| `c_skeleton` (2091) | weak band | 3,7 | 16 | 34-48 | 56-80 | Wrestling 45-55, Tactics 45-60 |
| `c_skeleton_w_axe` (2357) | weak band | 6,12 | 8 | 34-48 | 56-80 | Wrestling 45-55, Tactics 45-60 |
| `c_zombie`, `c_spectre` | weak band | — | — | — | — | same weak class |
| `c_skeleton_knight` (3967) | STRONG ring | **18,43** | 35 | **118-150** | 196-250 | Swords/Tactics/Wrestling **85-100**, Parry 85-100 |
| `c_lich` (018) | STRONG ring | 24,26 | 50 | 103-120 | 171-200 | Magery 70-80, EvalInt 100, Necro 89-99, MR 80-100 |
| `c_lich_lord` (04f) | STRONG ring | **30,38** | 40 | **250-303** | 416-505 | Magery 90-100, EvalInt 90-100, MR **150-200** |

Britain's rings, from the same file: knights x2 @1385,1459; lich x1 @1385,1452;
lich lord x1 @1385,1446. Ring centres are 6 and 7 tiles apart, so a pull in one
ring is inside walking range of the next: a strong engagement is not one
creature.

## 2. What the bots are, measured today

`runtime/logs/sphere2026-09-06.log`, `grep "was killed by"` (17 deaths):

- 02:16 Hector killed by **two `c_skeleton`** (DAM 3,7 each) — fencer, 51 hp,
  ~50 weapon skill.
- 03:07 Faustus killed by two Skeletons. 03:53 Aldron by one Skeleton.
- 03:28 Aurir by a **skeletal knight**. 17:57 Aurelius by a skeletal knight +
  zombie. 18:40 Vorar by a **Lich**. 19:07 Vorar by a skeletal knight
  (`run_gates/g_Vorar.console.txt:641-667` — he could not even close on it).

So the *weak* band is already at or past the current fleet's limit, and every
recorded contact with a strong creature ended in a bot corpse. There is no
recorded instance of any bot killing a lich, a lich lord or a skeletal knight.

## 3. Why no numeric threshold is written

To turn "18,43 damage vs 51 hp" into a survivable-skill number the gate would
need, at minimum:

1. this shard's damage-after-armour rule (DAM is the raw roll; the mitigation
   formula is `sphere-expert` territory and is not verified here), and
2. the defender's own AR — which `Observation` does not carry at all. It has
   `hp`, `hpMax`, per-skill tenths and a boolean `hasBasicArmor`
   (include/uo/life.h:583,631), no armour value.

Without (1) and (2) any "strong tier needs skill >= N and hp >= M" is a guess,
and the failure mode of a wrong guess here is a dead character with full loot
loss. Per the brief and CLAUDE.md ("If evidence is uncertain, mark it UNKNOWN
instead of inventing behavior"), the gate stays shut.

## 4. What would open it

Evidence, in this order, would justify a threshold:

- a verified Sphere damage-vs-AR formula for this shard, plus an AR field on
  `Observation`; or
- a measured survival: one bot engaging a lone `c_skeleton_knight` in a
  controlled run and winning, with its hp/skill/armour recorded — then the gate
  can be set at that character's numbers plus margin.

Until then the weak band is the only tier any character resolves.

## 5. Known data gap (not fixed here — navigation-world owns it)

`jhelom_cemetary_graveyard` is a weak-band row (radius 12) but the spawn table
puts a `c_skeleton_knight` @1286,3717, a `c_lich_lord` @1277,3722 and a `c_lich`
@1288,3733 inside that same yard, and the atlas has **no** Jhelom strong-ring
rows. Same shape for `city_of_the_dead_graveyard` and the T2A
`exit_from_*`/`entrance_from_*`/`passage_from_*` graveyard rows, whose contents
were never split. From the atlas alone Jhelom is indistinguishable from Yew
(genuinely weak: "NEWBIE YARD, KEEP IT WEAK", same file), so the tier resolver
cannot tell them apart and reports Jhelom as weak.
