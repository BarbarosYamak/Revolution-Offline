#pragma once

// ---------------------------------------------------------------------------
// Economic knowledge (M3.5).
//
// M3 discovered, live, that cutting a fish with a blade multiplies the stack
// by four (Source-X CClientTarg.cpp:1950) and that the steaks sell for 2gp
// against the whole fish's 1gp -- eight times the money at a twelfth of the
// weight. That finding currently lives in a scenario comment and a report.
// This turns it into something a bot can reason with, and gives the same shape
// to every other transformation the shard supports.
//
// TWO RULES THIS FILE EXISTS TO ENFORCE
//
//   1. A price has a PROVENANCE. The RevolutionUO forum records fish nets
//      selling around 1.5k and yielding around 2k of fish in some period; that
//      is historical evidence about a shard that stopped running, not a price
//      on ours. Historical figures may inform a guess and must never silently
//      become the number a bot trades on. `PriceBook` will not return one
//      unless the caller asks for it by name.
//
//   2. Profit is per-unit AND per-stone. A STR 30 fisher discovered the hard
//      way that value it cannot carry is not value -- eleven of its catches
//      ended up on the dock. Any estimate that ignores weight will keep
//      recommending the thing that gets left behind.
//
// No ML, no inference, no price prediction. These are deterministic recipes
// and recorded observations.
// ---------------------------------------------------------------------------

#include "uo/types.h"

#include <string>
#include <vector>

namespace uo::econ {

// Where a number came from. Ordered by how much a bot should trust it.
enum class PriceSource : u8 {
    None = 0,
    VendorObserved,       // this shard, this session, an actual vendor window
    PlayerTradeObserved,  // this shard, an actual completed secure trade
    ItemdefValue,         // the ruleset's VALUE -- true, but not what you get paid
    HistoricalForum,      // RevolutionUO archive. Evidence, never a live price.
    Count,
};

const char* PriceSourceName(PriceSource s);

// True when a bot may act on this number without further qualification.
bool IsLivePrice(PriceSource s);

struct Price {
    u16         graphic = 0;
    i32         gold = 0;          // per single unit
    PriceSource source = PriceSource::None;
    i64         atMs = 0;
    std::string note;              // e.g. "Cassiel the cook, Britain"
};

// A deterministic, known conversion. Not a discovery to be re-made every time.
struct Transformation {
    std::string name;
    u16  inputGraphic = 0;
    u16  outputGraphic = 0;
    // How many outputs one input yields. Carving a fish is 4.
    i32  outputPerInput = 1;
    // Any one of these graphics works as the tool. Empty = no tool needed.
    std::vector<u16> toolGraphics;
    // Skill gate, in tenths. -1 skill id means the shard imposes none --
    // carving is a plain item-use with no check at all.
    i32  skillId = -1;
    i32  requiredSkillTenths = 0;
    // Weight of one unit, in tenths of a stone, before and after.
    i32  inputWeightTenths = 0;
    i32  outputWeightTenths = 0;
    std::string evidence;   // where this is written down
};

struct ProfitEstimate {
    bool valid = false;         // false when a needed price is missing
    bool usesHistorical = false;// true if any input price was forum-era
    i32  revenue = 0;
    i32  cost = 0;
    i32  margin = 0;
    // The two numbers that actually decide what a bot does.
    i32  marginPerInputUnit = 0;
    i32  marginPerStone = 0;    // margin per stone of carried weight, x10
};

// What a bot knows about prices. Observations accumulate; historical baselines
// are kept apart so they cannot leak into a live decision by accident.
class PriceBook {
public:
    void Observe(const Price& p);

    // Best live price for a graphic, or nullptr. Historical entries are never
    // returned here however little else is known.
    const Price* Best(u16 graphic) const;

    // Explicitly asks for the archive baseline. Named so it cannot be called
    // by accident and so it shows up in review.
    const Price* HistoricalBaseline(u16 graphic) const;

    usize Size() const { return prices_.size(); }
    void  Clear() { prices_.clear(); }

private:
    std::vector<Price> prices_;
};

// Compare selling `amount` of the raw good against transforming it first.
// `sellRaw` and `sellProduct` are per-unit prices.
ProfitEstimate EstimateTransformation(const Transformation& t, i32 amount,
                                      const PriceBook& book);

// The transformations M3 actually established on this shard. Kept in code so
// they are versioned and reviewable rather than rediscovered per scenario.
const std::vector<Transformation>& KnownTransformations();

} // namespace uo::econ
