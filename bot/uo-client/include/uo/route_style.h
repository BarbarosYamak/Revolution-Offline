#pragma once

// ---------------------------------------------------------------------------
// Bounded, deterministic route variation (M3.5).
//
// WHY
//
// M2.5's navigation is correct and it is also perfectly repeatable, which is
// exactly what makes a population look wrong. Fifty characters asked for "the
// Britain bank" walk the same tiles in the same order at the same speed, and a
// human watching sees a conveyor belt. The historical record says Revolution
// players ran everywhere (2008 forum, topic 33941) -- it does not say they ran
// in single file.
//
// WHAT THIS IS NOT
//
//   * Not wandering. Every tile this layer proposes is one the caller has
//     already confirmed is legal and on the way.
//   * Not learning. A fixed seed gives a fixed answer, for ever. That is a
//     requirement, not a limitation: tests must be reproducible, and a bot
//     whose habits drift between sessions is harder to debug than one whose
//     habits are wrong.
//   * Not a pathfinder. It sits ABOVE the tile A* and only chooses between
//     equivalent options the planner already offered:
//
//         semantic goal -> world route -> [this] -> local A* -> SubmitStep()
//
//     SubmitStep remains the only thing that writes a 0x02.
//
// THE IDEA
//
// A character has a `Style`: a stable seed derived from its name, a route
// preference, and how far it is willing to drift sideways. From that seed the
// layer answers three questions deterministically:
//
//   1. "Of these equivalent approach tiles / entrances, which is mine?"
//   2. "How much do I dislike this particular cell?" -- a small, stable,
//      per-character spatial bias that makes two bots prefer different sides
//      of the same street without either being wrong.
//   3. "Have I just been here?" -- a decaying penalty on recently walked
//      tiles, so the same bot varies its own route between trips rather than
//      only differing from its neighbours.
//
// Biases are bounded in tiles and never make an illegal tile look legal, so
// reachability is unchanged by construction.
// ---------------------------------------------------------------------------

#include "uo/types.h"
#include "uo/world_model.h"

#include <string>
#include <vector>

namespace uo::routing {

// What a character optimises for when several routes are about equal. The
// planner supplies the underlying facts; this only says how much they matter.
enum class Preference : u8 {
    Shortest = 0,    // straight line, minimal drift -- couriers, the impatient
    RoadPreferred,   // sticks to open/travelled ground even if slightly longer
    LowCongestion,   // avoids other mobiles, takes the quieter parallel line
    LowRisk,         // avoids cells the character knows it has died in
    Mixed,           // a bit of each -- the commonest kind of person
    Count,
};

const char* PreferenceName(Preference p);

struct Style {
    u64        seed = 1;
    Preference preference = Preference::Mixed;
    // How many tiles of lateral drift this character will accept on a leg.
    // 0 means "walk the line"; 2 is a noticeable saunter. Bounded on purpose.
    u8         laneWidth = 1;
};

// Derive a stable style from a character name. Same character, same habits,
// every session and every process -- no stored state, no coordination.
Style StyleForCharacter(const char* name);

class Variation {
public:
    Variation() = default;
    explicit Variation(const Style& s) : style_(s) {}

    const Style& GetStyle() const { return style_; }
    void SetStyle(const Style& s) { style_ = s; }

    // A stable per-character dislike of a particular tile, in [0, maxBias].
    // Same character + same tile always yields the same number, so a route
    // costed with it is still deterministic and still repeatable.
    i32 CellBias(i32 x, i32 y, i32 maxBias) const;

    // Deterministically choose one of `count` equivalent options. `salt`
    // separates unrelated decisions (approach tile vs entrance vs lane) so a
    // character does not correlate every choice it ever makes.
    usize Choose(usize count, u32 salt) const;

    // Pick this character's approach tile from a set the caller has already
    // established are all legal and all acceptable arrivals. Returns
    // `candidates.size()` if the set is empty.
    usize PickApproach(const std::vector<wm::Point>& candidates, u32 salt) const;

    // --- recent-path memory ------------------------------------------------
    // So a bot varies against ITSELF over time, not just against its
    // neighbours. Bounded in size and in time; this is a habit, not a map.
    void NotePassed(i32 x, i32 y, i64 nowMs);
    i32  RecentPenalty(i32 x, i32 y, i64 nowMs) const;
    void ForgetOlderThan(i64 nowMs);
    usize RecentCount() const { return recent_.size(); }

    // How long a tile stays "recent", and the penalty it carries when fresh.
    static constexpr i64 kRecentWindowMs = 10 * 60 * 1000;
    static constexpr i32 kRecentPenalty  = 3;
    static constexpr usize kRecentMax    = 256;

    // Total extra cost this character assigns to a tile: its stable spatial
    // bias plus any recent-path penalty, scaled by preference. Bounded by
    // `maxBias` + kRecentPenalty so it can never dominate real distance.
    i32 TileCost(i32 x, i32 y, i64 nowMs, i32 maxBias) const;

private:
    Style style_;

    struct Recent { i32 x = 0; i32 y = 0; i64 atMs = 0; };
    std::vector<Recent> recent_;
};

} // namespace uo::routing
