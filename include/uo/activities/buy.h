#pragma once

// ---------------------------------------------------------------------------
// BUY -- one activity, every purchase (docs/BOT_ARCHITECTURE.md section 15).
//
// NOT BuyBandages, BuyReagents, BuySword, BuyTamerFood. There is exactly one
// question a buyer asks -- "how many of this do I still need, and can I
// afford them" -- and exactly one sequence for getting them out of an NPC.
// This project wrote that twice by hand and got a different subset of it
// right each time:
//
//   DoReplaceEquipment  had a stock clamp and no gold floor
//   DoBuySupplies       had a gold floor and no stock clamp
//   BuyScrollFrom       had neither, plus a trip counter shared between
//                       three goals, so a gear trip spent the spellbook's
//                       allowance
//
// THE SPLIT OF RESPONSIBILITY, which is the whole point:
//
//   BuyActivity   PURCHASE SEMANTICS. How many do I still need? Can I afford
//                 them without eating the reserve that replaces my tools
//                 after a death? Is this price one I am willing to pay?
//   VendorErrand  THE HANDSHAKE. Find a shopkeeper who is not a guildmaster,
//                 ask who is present, walk into reach, open, clamp to stock,
//                 verify the pack and the purse actually moved.
//
// Neither knows the other's business, and neither knows what a bandage is
// for. A mage buying reagents, a warrior buying a katana and an alchemist
// buying a hundred empty bottles are the same code with different numbers.
//
// THE DECISION HALF IS PURE. `Decide()` takes plain numbers and returns a
// plan, so ctest exercises the affordability and reserve rules directly --
// which matters because those rules have deadlocked characters before: a
// scribe with 781 gold and a 900 reserve stood in a mage shop refusing to
// buy three 3-gold scrolls, every fifteen seconds, for a whole session.
// ---------------------------------------------------------------------------

#include "uo/interaction/activity_result.h"
#include "uo/vendor_errand.h"
#include "uo/types.h"

#include <string>

namespace uo {
class Client;
}

namespace uo::life {

struct Observation;   // uo/life.h

struct BuyRequest {
    // WHAT. The graphic is the item; the name is for the log line a human
    // reads at 3am.
    u16         graphic = 0;
    const char* item = "goods";

    // HOW MANY THIS LIFE WANTS TO END UP HOLDING -- not how many to buy.
    // The difference is the bug that bought six heater shields: a request
    // phrased as "buy one" cannot notice that one is already in the pack.
    i32 desiredTotal = 1;

    // WHAT MUST NOT BE SPENT. The reserve that replaces a tool after a death.
    // Deliberately separate from "how much gold is there": working capital
    // and the death reserve are different money, and charging inputs against
    // the reserve is what deadlocked the scribe.
    i32 minimumGoldReserve = 0;

    // A CEILING, because a bot with a full purse will otherwise accept any
    // number a vendor says. Zero means no ceiling.
    i32 maxPricePerUnit = 0;

    // Who might sell it, in preference order -- baker then provisioner,
    // scribe then mage shop.
    VendorErrandSpec::Seller sellers[3] = {};
    i32                      sellerCount = 0;

    void Sell(const char* trade, wm::Service service) {
        if (sellerCount >= 3) return;
        sellers[sellerCount].trade = trade;
        sellers[sellerCount].service = service;
        ++sellerCount;
    }
};

// What the arithmetic says, before any packet is sent.
struct BuyPlan {
    // How many to actually ask for. Zero with `satisfied` means the errand
    // is already over and nothing needs buying.
    i32 quantity = 0;
    // Nothing to do: this life already holds what it wanted.
    bool satisfied = false;
    // Cannot proceed until the world changes -- not enough spendable gold,
    // or the price is above what this request will pay. Blocked is NOT
    // failure: it becomes possible again when the purse or the shop does.
    bool blocked = false;
    // Always populated.
    const char* reason = "";
};

// The pure half. `held` is what is already in the pack, `gold` the purse,
// `unitPrice` the shop's quoted price (0 when not yet known).
BuyPlan Decide(const BuyRequest& req, i32 held, i32 gold, i32 unitPrice);

// The activity. Owns a VendorErrand and answers in the shared vocabulary.
class BuyActivity {
public:
    void Begin(const BuyRequest& req);
    bool Running() const { return running_; }
    void Cancel();

    // Section 14's contract: never blocks, and never reports success for an
    // action that was merely attempted.
    ActivityTickResult Tick(Client& client, const Observation& obs);

    // Populated whenever Tick returns a terminal status, so the caller can
    // log one line rather than reconstructing what happened.
    const char* Why() const { return why_; }
    // The shopkeeper that served us, for the caller's supplier memory. The
    // activity deliberately does not write memory itself -- remembering a
    // shop is a life's business, not a purchase's.
    u32 Keeper() const { return keeper_; }

private:
    BuyRequest   req_{};
    VendorErrand errand_;
    bool         running_ = false;
    u32          keeper_ = 0;
    const char*  why_ = "";
    // Storage for the errand's per-tick reason, so the const char* handed
    // out in ActivityTickResult points at something that outlives the call.
    std::string  lastWhy_;
};

}  // namespace uo::life
