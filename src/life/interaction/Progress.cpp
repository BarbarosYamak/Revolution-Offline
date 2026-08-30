#include "uo/interaction/progress.h"
#include "uo/interaction/activity_result.h"

namespace uo::life {

const char* ActivityStatusName(ActivityStatus s) {
    switch (s) {
        case ActivityStatus::Waiting:          return "waiting";
        case ActivityStatus::Success:          return "success";
        case ActivityStatus::Failed:           return "failed";
        case ActivityStatus::NoProgress:       return "no_progress";
        case ActivityStatus::Blocked:          return "blocked";
        case ActivityStatus::Interrupted:      return "interrupted";
        case ActivityStatus::RetryableFailure: return "retryable_failure";
    }
    return "?";
}

bool IsTerminal(ActivityStatus s) {
    return s != ActivityStatus::Waiting;
}

bool IsProgress(ActivityStatus s) {
    // ONLY Success. This is the whole point of having seven states: a goal
    // that stood down blocked, failed, or achieved nothing must not feed the
    // planner the same "I did work" signal as one that bought the bandages.
    return s == ActivityStatus::Success;
}

const char* WakeName(Wake w) {
    switch (w) {
        case Wake::Now:              return "now";
        case Wake::AfterDelay:       return "after a delay";
        case Wake::ActionResolves:   return "the action to resolve";
        case Wake::TravelArrives:    return "arrival";
        case Wake::InventoryChanges: return "the pack to change";
        case Wake::GoldChanges:      return "the purse to change";
        case Wake::SkillChanges:     return "a skill report";
        case Wake::TargetCursor:     return "a target cursor";
    }
    return "?";
}

const char* VerdictName(Verdict v) {
    switch (v) {
        case Verdict::Confirmed:     return "confirmed";
        case Verdict::NotYet:        return "not yet";
        case Verdict::Contradicted:  return "contradicted";
        case Verdict::NothingChecked:return "nothing checked";
    }
    return "?";
}

ProgressCheck Verify(const Expectation& expect, const Observed& seen) {
    ProgressCheck out;

    if (!expect.ChecksAnything()) {
        out.verdict = Verdict::NothingChecked;
        out.reason = "this activity did not say what success would look like";
        return out;
    }

    const bool wantItems = expect.itemBefore >= 0 && expect.itemGain > 0;
    const bool loseItems = expect.itemBefore >= 0 && expect.itemLoss > 0;
    const bool wantGold  = expect.goldBefore >= 0 && expect.goldSpendMin > 0;
    const bool earnGold  = expect.goldBefore >= 0 && expect.goldGainMin > 0;
    const bool wantEquip = expect.equipLayer != 0;
    const bool wantSkill = expect.skillId >= 0 && expect.skillBefore >= 0;

    if ((wantItems || loseItems) && seen.itemNow >= 0)
        out.itemDelta = seen.itemNow - expect.itemBefore;
    if ((wantGold || earnGold) && seen.goldNow >= 0)
        out.goldDelta = seen.goldNow - expect.goldBefore;
    if (wantSkill && seen.skillNow >= 0)
        out.skillDelta = seen.skillNow - expect.skillBefore;

    // --- the contradictions first, because they are DEFINITIVE ------------
    //
    // A no is worth more than a not-yet: it ends the errand honestly instead
    // of leaving it to time out, which is how a bot spends a session on one
    // shop.

    // --- the SALE contradictions ------------------------------------------
    //
    // A sale is gold arriving AND goods leaving. Either half on its own is
    // something else entirely, and recording it as a sale corrupts both the
    // ledger and the price book the bot trades on.
    if (loseItems && earnGold && seen.itemNow >= 0 && seen.goldNow >= 0) {
        // THE GOODS WENT AND NOTHING CAME BACK. A give, a theft, a drop --
        // whatever it was, the character is poorer for it.
        if (out.itemDelta < 0 && out.goldDelta <= 0) {
            out.verdict = Verdict::Contradicted;
            out.reason = "the goods left and no gold arrived";
            return out;
        }
        // GOLD ARRIVED AND THE PACK IS UNTOUCHED. Gold rises from loot, from
        // a player trade, from a bank withdrawal -- crediting THIS sale for
        // it teaches the price book a number nobody paid.
        if (out.goldDelta >= expect.goldGainMin && out.itemDelta >= 0) {
            out.verdict = Verdict::Contradicted;
            out.reason = "gold arrived but nothing left the pack";
            return out;
        }
    }

    // GOLD LEFT AND NOTHING CAME BACK. The exact shape of the vendor bug:
    // Sphere took nothing and gave nothing, or took the money on a different
    // errand entirely. Either way this purchase is not what moved the purse.
    if (wantItems && wantGold && seen.itemNow >= 0 && seen.goldNow >= 0) {
        if (out.goldDelta < 0 && out.itemDelta <= 0) {
            out.verdict = Verdict::Contradicted;
            out.reason = "gold left the purse and no goods arrived";
            return out;
        }
        // GOODS ARRIVED FREE. Not a windfall to celebrate -- it means the
        // gold moved somewhere this check cannot see, and a ledger that
        // records a free purchase is a ledger that cannot audit the economy.
        if (out.itemDelta >= expect.itemGain && out.goldDelta == 0) {
            out.verdict = Verdict::Contradicted;
            out.reason = "goods arrived without the purse moving";
            return out;
        }
    }

    // SPENT MORE THAN THE ERRAND AUTHORISED. A price that moved between the
    // quote and the purchase, or the wrong row bought.
    if (wantGold && seen.goldNow >= 0 && expect.goldSpendMax > 0) {
        const i32 spent = -out.goldDelta;
        if (spent > expect.goldSpendMax) {
            out.verdict = Verdict::Contradicted;
            out.reason = "the purchase cost more than the quote allowed";
            return out;
        }
    }

    // --- then the confirmations -------------------------------------------
    if (wantItems) {
        if (seen.itemNow < 0) {
            out.verdict = Verdict::NotYet;
            out.reason = "the pack has not been read since the attempt";
            return out;
        }
        if (out.itemDelta < expect.itemGain) {
            out.verdict = Verdict::NotYet;
            out.reason = "the goods have not arrived in the pack";
            return out;
        }
    }

    if (loseItems) {
        if (seen.itemNow < 0) {
            out.verdict = Verdict::NotYet;
            out.reason = "the pack has not been read since the attempt";
            return out;
        }
        if (-out.itemDelta < expect.itemLoss) {
            out.verdict = Verdict::NotYet;
            out.reason = "the goods are still in the pack";
            return out;
        }
    }

    if (earnGold) {
        if (seen.goldNow < 0) {
            out.verdict = Verdict::NotYet;
            out.reason = "the purse has not been read since the attempt";
            return out;
        }
        if (out.goldDelta < expect.goldGainMin) {
            out.verdict = Verdict::NotYet;
            out.reason = "the buyer has not paid yet";
            return out;
        }
    }

    if (wantGold) {
        if (seen.goldNow < 0) {
            out.verdict = Verdict::NotYet;
            out.reason = "the purse has not been read since the attempt";
            return out;
        }
        const i32 spent = -out.goldDelta;
        if (spent < expect.goldSpendMin) {
            out.verdict = Verdict::NotYet;
            out.reason = "the server has not taken the gold yet";
            return out;
        }
    }

    if (wantEquip) {
        if (seen.equippedAtLayer != expect.equipGraphic) {
            out.verdict = Verdict::NotYet;
            out.reason = "the piece is not on the paperdoll";
            return out;
        }
    }

    if (wantSkill) {
        if (seen.skillNow < 0) {
            out.verdict = Verdict::NotYet;
            out.reason = "no skill report has arrived since the attempt";
            return out;
        }
        if (out.skillDelta < expect.skillGainMin) {
            // THE training_unverified CASE, named at last. Gold was spent and
            // the server's own number did not move; the caller decides
            // whether that is a refusal or merely a slow report, but it is
            // never a confirmed lesson.
            out.verdict = Verdict::NotYet;
            out.reason = "the server's skill value has not moved";
            return out;
        }
    }

    out.verdict = Verdict::Confirmed;
    out.reason = "the world confirms it";
    return out;
}

}  // namespace uo::life
