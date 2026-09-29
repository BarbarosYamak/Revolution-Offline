#pragma once

// ---------------------------------------------------------------------------
// ACQUIRE AND EQUIP -- "end up holding this" and "end up WEARING this"
// (docs/BOT_ARCHITECTURE.md sections 5, 15 and 18).
//
// THE BUG THIS EXISTS TO MAKE UNWRITABLE.
//
// Corwyn's backpack held SIX i_shield_heater. Every one was bought by the
// same reasoning: the hand slot is empty, an armorer sells shields, buy one.
// Nothing ever asked whether he could wear it -- i_shield_heater needs STR 90
// and Corwyn has 56 -- so the slot stayed empty, and the next tick bought
// another. Across the recorded runs that shape produced 11,645 "buying"
// lines, 5,140 of them on Cassia alone.
//
// Two rules fall out of it, and they are the whole file:
//
//   1. NEVER BUY WHAT THIS CHARACTER COULD NOT WEAR. Wearability is asked
//      BEFORE the shop, not after the purchase.
//   2. A PIECE IN THE PACK IS NOT A PIECE ON THE PAPERDOLL. Equipping is an
//      action with a server confirmation, and "I sent the equip packet" is
//      not "the gorget is on the layer" -- section 18 again.
//
// Deliberately NOT a gear-shopping policy. Whether a mage may wear plate is
// M5's question, answered by the profession catalogue and handed in here as
// a single bool. This activity only refuses to do the arithmetic that made
// the answer irrelevant.
// ---------------------------------------------------------------------------

#include "uo/activities/buy.h"
#include "uo/interaction/activity_result.h"
#include "uo/interaction/handshake.h"
#include "uo/types.h"

#include <string>

namespace uo {
class Client;
}

namespace uo::life {

struct Observation;   // uo/life.h

struct AcquireRequest {
    u16         graphic = 0;
    const char* item = "the item";

    // HOW MANY TO END UP HOLDING. For equipment this is 1 -- and one is the
    // number that matters, because the second shield was never the point.
    i32 desiredTotal = 1;

    // EQUIPMENT, or merely stock? A non-zero layer means this is worn, and
    // the activity is not finished until the paperdoll says so.
    u8   layer = 0;
    bool mustWear = false;

    // MAY THIS CHARACTER WEAR IT AT ALL? The caller answers -- strength,
    // profession class, the M5 catalogue's `wears`. Handed in rather than
    // re-derived, because this activity has no business knowing that a mage
    // does not wear plate, and every business refusing to buy what will sit
    // in the pack forever.
    bool wearable = true;
    const char* unwearableReason = "this character cannot wear it";

    // Buying, if it comes to that.
    i32 minimumGoldReserve = 0;
    i32 maxPricePerUnit = 0;
    VendorErrandSpec::Seller sellers[3] = {};
    i32                      sellerCount = 0;

    void Sell(const char* trade, wm::Service service) {
        if (sellerCount >= 3) return;
        sellers[sellerCount].trade = trade;
        sellers[sellerCount].service = service;
        ++sellerCount;
    }
};

// What to do next, decided from the world rather than from what was last
// attempted.
enum class AcquireStep : u8 {
    Done = 0,     // the request is satisfied; nothing to do
    Wear,         // it is in the pack and belongs on the paperdoll
    Buy,          // it is not held and may legitimately be bought
    Refuse,       // held or buyable, but this character cannot wear it
};

const char* AcquireStepName(AcquireStep s);

struct AcquirePlan {
    AcquireStep step = AcquireStep::Done;
    const char* reason = "";
};

// The pure half. `worn` is the graphic currently on the request's layer (0
// for empty); `held` is how many are in the pack.
AcquirePlan DecideAcquire(const AcquireRequest& req, i32 held, u16 worn);

// Put a carried item on. Small, but it is the step that never happened.
class EquipActivity {
public:
    void Begin(u16 graphic, u8 layer, const char* what);
    bool Running() const { return running_; }
    void Cancel() { running_ = false; }
    ActivityTickResult Tick(Client& client, const Observation& obs);

private:
    u16         graphic_ = 0;
    u8          layer_ = 0;
    const char* what_ = "the item";
    bool        running_ = false;
    Handshake   ask_;
    std::string lastWhy_;
};

// Hold it, and wear it if that is what was asked. Composes BuyActivity and
// EquipActivity; owns neither's rules.
class AcquireItemActivity {
public:
    void Begin(const AcquireRequest& req);
    bool Running() const { return running_; }
    void Cancel();
    ActivityTickResult Tick(Client& client, const Observation& obs);

    u32 Keeper() const { return buy_.Keeper(); }

private:
    AcquireRequest req_{};
    BuyActivity    buy_;
    EquipActivity  equip_;
    bool           running_ = false;
    std::string    lastWhy_;
};

}  // namespace uo::life
