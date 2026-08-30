#pragma once

// ---------------------------------------------------------------------------
// PROGRESS VERIFICATION -- what "it worked" means, stated before the attempt
// and checked against the world after it
// (docs/BOT_ARCHITECTURE.md section 18).
//
// THE RULE: `sent packet = success` is never true.
//
// THE EVIDENCE, all of it from this shard rather than from principle:
//
//   * A purchase was reported as progress the moment ActionVendorBuy returned.
//     Sphere then refused the WHOLE order because the ask exceeded stock --
//     "Your order cannot be fulfilled" -- and the bot recorded eight
//     successful buys while its gold never moved and no bandage ever arrived
//     (run_m7/r1a_Corwyn.console.txt, 11:16-11:18 on 2026-08-30).
//   * `training_unverified`: gold left the ledger as GOLD_DESTROYED_TRAINER
//     with no confirmed skill gain to show for it, a dozen times in one fleet
//     run. Nobody checked the 0x3A that follows.
//   * A gear purchase that never reached the paperdoll left the slot empty,
//     so the next tick bought another -- six heater shields, and 11,645
//     "buying" lines across the recorded runs.
//
// Every one of those is the same missing step: nothing said, in advance, what
// the world would look like if the action had actually worked.
//
// DELIBERATELY PURE. No Client, no packets. The caller reads the numbers off
// the world and hands them over, which is what lets ctest exercise this
// directly -- and what stops the verification itself from depending on the
// layer it is verifying.
// ---------------------------------------------------------------------------

#include "uo/types.h"

namespace uo::life {

// What this activity expects the world to look like afterwards. Fields left
// at their defaults are simply not checked -- an expectation nobody stated is
// not a test that silently passes.
struct Expectation {
    // ITEMS. `itemGraphic` is what to count; the check wants at least
    // `itemGain` more of it than there were before.
    u16 itemGraphic = 0;
    i32 itemBefore = -1;      // -1 = not measured, so not checked
    i32 itemGain = 0;         // how many more we expect to hold
    // ...or how many FEWER. Selling is the mirror of buying and needs both
    // halves checked: gold rising on its own is not a sale, because gold also
    // rises from loot, a player trade, and a bank withdrawal. The goods have
    // to have LEFT. Set one of itemGain/itemLoss, never both.
    i32 itemLoss = 0;

    // GOLD. A purchase must COST something: a buy that leaves the purse
    // untouched did not happen, whatever the packet said. A SALE is the
    // mirror -- set goldGainMin instead.
    i32 goldBefore = -1;      // -1 = not measured
    i32 goldSpendMin = 0;     // at least this much must have left
    i32 goldSpendMax = 0;     // and no more than this (0 = no ceiling)
    i32 goldGainMin = 0;      // at least this much must have ARRIVED

    // EQUIPMENT. A piece that never reached the layer is still in the pack,
    // and buying another will not change that.
    u8  equipLayer = 0;       // 0 = not checked
    u16 equipGraphic = 0;

    // SKILL, in tenths, as the server's own 0x3A reports it.
    int skillId = -1;         // -1 = not checked
    i32 skillBefore = -1;
    i32 skillGainMin = 1;     // a tenth is a real gain; zero is not

    bool ChecksAnything() const {
        return (itemBefore >= 0 && (itemGain > 0 || itemLoss > 0)) ||
               (goldBefore >= 0 && (goldSpendMin > 0 || goldGainMin > 0)) ||
               (equipLayer != 0) ||
               (skillId >= 0 && skillBefore >= 0);
    }
};

// What the world actually looks like now. The caller reads these off the
// client; this header stays ignorant of how.
struct Observed {
    i32 itemNow = -1;
    i32 goldNow = -1;
    u16 equippedAtLayer = 0;
    i32 skillNow = -1;
};

enum class Verdict : u8 {
    // Everything that was checked, held.
    Confirmed = 0,
    // Nothing that was checked has moved yet. Not a failure -- packets take
    // time -- but emphatically not success either.
    NotYet,
    // Something moved the WRONG way, and that is a definitive answer: gold
    // left and no goods arrived, or goods arrived free.
    Contradicted,
    // Nothing was asked of it. A caller that verifies nothing gets told so
    // rather than being handed a cheerful yes.
    NothingChecked,
};

const char* VerdictName(Verdict v);

struct ProgressCheck {
    Verdict     verdict = Verdict::NothingChecked;
    // Always populated, and phrased for a log line a human will read at 3am.
    const char* reason = "";
    // What actually moved, for the ledger and the session summary.
    i32 itemDelta = 0;
    i32 goldDelta = 0;
    i32 skillDelta = 0;
};

// The whole point of the file.
ProgressCheck Verify(const Expectation& expect, const Observed& seen);

}  // namespace uo::life
