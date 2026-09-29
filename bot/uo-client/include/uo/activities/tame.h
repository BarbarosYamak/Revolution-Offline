#pragma once

// ---------------------------------------------------------------------------
// TAME -- when a tamer is allowed to say a place is empty, and which flock to
// walk to next.
//
// Two facts DoTameAnimal got wrong, both proved by the wave 2026-09-02 run
// (artifacts/wave_2026-09-02_verdict.md section d, Rhea, Taming 50.0):
//
//   A JUDGEMENT BEFORE THE NAMES ARE IN IS NOT A JUDGEMENT. The handler
//   filtered ScanMobiles hits with `if (m.name.empty()) continue;`, but a
//   mobile's name is only known after the 0x98 AllNames query that
//   Client::ActionScanMobiles issues (Client.cpp PrintNearbyMobiles). Nothing
//   in the taming path ever issued it, so every sheep was nameless, every
//   pasture read as empty, and the goal failed 60 ms after each arrival with
//   `tame: nothing tamable here` while eight woolly sheep grazed inside ten
//   tiles. The names are the observation; asking for them is a step, and the
//   emptiness verdict has to wait for it.
//
//   THE PASTURES ARE DATA, AND THE NEAREST ONE COMES FIRST. Three literal
//   coordinates were compiled in, walked in a fixed order. The flocks are
//   already read out of the world save into data/revolution_pastures.tsv
//   (tools/pasturegen.py, loaded by Runner.cpp LoadPastures); all that was
//   missing was ordering them from where the character actually stands.
//
// Both answers are pure and live here so a test can hold them still without a
// shard.

#include <vector>

#include "uo/life.h"
#include "uo/types.h"

namespace uo::life {

// Nearest-first from where the character stands. `P` is any pasture record
// with i32 x, y and count -- Runner.cpp's own table row, or a test's stand-in.
// Ties break on the bigger flock: with two pastures the same distance away,
// the one the save counted more animals in is the better bet.
template <class P>
void OrderPasturesNearest(std::vector<P>& list, i32 x, i32 y) {
    auto dist = [x, y](const P& p) {
        const i64 dx = static_cast<i64>(p.x) - x;
        const i64 dy = static_cast<i64>(p.y) - y;
        return dx * dx + dy * dy;                 // squared: ordering only
    };
    for (usize a = 1; a < list.size(); ++a) {     // insertion sort, tiny list
        P cur = list[a];
        usize b = a;
        while (b > 0 && (dist(list[b - 1]) > dist(cur) ||
                         (dist(list[b - 1]) == dist(cur) &&
                          list[b - 1].count < cur.count))) {
            list[b] = list[b - 1];
            --b;
        }
        list[b] = cur;
    }
}

// THE SEAM. What the handler knows about its own name scan when it is about
// to decide "there is nothing tamable here".
struct TameScanSight {
    bool scanIssued = false;    // ActionScanMobiles called since arriving here
    bool namesPending = false;  // 0x98 replies still outstanding
    i64  msSinceScan = 0;       // wall time since the scan was issued
};

// May the handler conclude a place is empty? Only with the names in hand:
// the scan issued, its replies no longer pending, and a settle window past so
// late arrivals (and animals grazing back into range) are counted too.
inline bool MayJudgeEmpty(const TameScanSight& s, i64 settleMs) {
    if (!s.scanIssued) return false;
    if (s.namesPending) return false;
    return s.msSinceScan >= settleMs;
}

// ---------------------------------------------------------------------------
// WHICH HERD TO WALK TO. "rhea can tame a lot of things not just sheep"
// (project owner, 2026-09-02).
//
// The pasture table is sheep only -- it was generated for the tailor's wool
// chain, every flock of it sits in the Yew farmland, and Yew is not a legal
// home city; Rhea died on that road. data/revolution_tamables.tsv
// (tools/tamablegen.py) is the whole tamable population of the save,
// clustered per species, each row carrying the chardef's TAMING requirement.
//
// SPHERE'S OWN TWO RULES decide which of them is worth the walk:
//
//   SUCCESS. CChar::Skill_CheckSuccess (Source-X CCharSkill.cpp:514) takes
//   Skill_Taming's difficulty -- iTameBase/10, the animal's own TAMING base
//   (CCharSkill.cpp:2344) -- and rolls Calc_GetSCurve(skill - difficulty,
//   SKILL_VARIANCE=100). Equal skill and requirement is a coin toss; above
//   the requirement it climbs. So a requirement OVER this character's skill
//   is not a target, it is a mauling waiting to happen.
//
//   GAIN. CChar::Skill_Experience (CCharSkill.cpp:402-412) refuses gain when
//   `difficulty + GAINRADIUS < max(skill, 5.0)` -- work too easy teaches
//   nothing. skill35_taming.scp sets no GAINRADIUS; skill.scp's [COMMENT
//   SKILL x] template documents 100.0 (10.0 skill points) as "original
//   behaviour", so ten points below the character's skill is the conservative
//   floor. Under a default of 0 the check is disabled and preferring the
//   window merely costs nothing.
//
// Hence: never above skill, prefer within kTameGainWindow below it, and among
// the survivors take the nearest -- distance is the tamer's real cost.
inline constexpr double kTameGainWindow = 10.0;
// Skill_Experience's own floor: below 5.0 skill the gain test treats the
// character as if it had 5.0 (uiSkillLevelFixed, CCharSkill.cpp:404).
inline constexpr double kTameGainSkillFloor = 5.0;

// True when taming this creature can still teach this character something.
inline bool TameCanGain(double req, double skill) {
    const double floored = skill < kTameGainSkillFloor ? kTameGainSkillFloor : skill;
    return req + kTameGainWindow >= floored;
}

// HOW FAR A TAMER MAY WALK TODAY. Not a constant: the same herd is worth the
// trip with twenty minutes left and absurd with three. Inverts
// EstimateTripTimeMs (uo/life.h) -- the tiles that fit in the session once
// wind-down and the work at the far end are both reserved.
inline i32 TameTravelBudgetTiles(i64 remainingMs, i64 windDownBudgetMs,
                                 i64 workReserveMs) {
    const i64 spare = remainingMs - windDownBudgetMs - workReserveMs;
    if (spare <= 0) return 0;
    const i64 perTile = kTripMsPerTile * 3 / 2;      // EstimateTripTimeMs
    const i64 tiles = spare / (perTile > 0 ? perTile : 1);
    return tiles > 0x7fffffff ? 0x7fffffff : static_cast<i32>(tiles);
}

// The work still to do after arriving: one name scan plus the several taming
// strokes Sphere rolls (kMaxTameAttempts x DELAY=2.0 and the runner's own
// 6 s spacing). Walking somewhere with no time left to tame is a wasted trip.
inline constexpr i64 kTameWorkReserveMs = 60000;

// Pick the herd. `C` is any record with i32 x, y, count and double req --
// Runner.cpp's table row or a test's stand-in. Returns an index into `list`,
// or -1 when nothing qualifies. Callers drop herds already visited before
// asking, so this stays pure.
template <class C>
int ChooseTameCluster(const std::vector<C>& list, i32 x, i32 y,
                        double skill, i32 budgetTiles) {
    int best = -1;
    int bestTier = 99;
    i64 bestDist = 0;
    i32 bestCount = 0;
    for (usize i = 0; i < list.size(); ++i) {
        const C& c = list[i];
        if (c.req > skill) continue;                  // beyond this character
        const i64 dx = static_cast<i64>(c.x) - x;
        const i64 dy = static_cast<i64>(c.y) - y;
        const i64 cheb = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy)
                             ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
        if (budgetTiles >= 0 && cheb > budgetTiles) continue;   // no time
        const int tier = TameCanGain(c.req, skill) ? 0 : 1;
        const i64 d2 = dx * dx + dy * dy;
        const bool better =
            best < 0 || tier < bestTier ||
            (tier == bestTier && (d2 < bestDist ||
                                  (d2 == bestDist && c.count > bestCount)));
        if (better) {
            best = static_cast<int>(i);
            bestTier = tier; bestDist = d2; bestCount = c.count;
        }
    }
    return best;
}

}  // namespace uo::life
