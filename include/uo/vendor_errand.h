#pragma once

// ---------------------------------------------------------------------------
// THE VENDOR ERRAND -- one definition of "buy something from a shopkeeper".
//
// WHY THIS FILE EXISTS, in evidence rather than in principle.
//
// Buying from an NPC was implemented three separate times in Runner.cpp --
// BuyScrollFrom (six callers: scrolls, spellbooks, armour, tools),
// DoReplaceEquipment (bandages) and DoBuySupplies (craft inputs) -- and each
// copy learned the same lessons independently, late, and at the cost of live
// sessions. On 2026-08-30 alone:
//
//   * "ask who is standing here before concluding nobody is" -- the bandage
//     path had never called ScanMobiles, so it read "no healer" off a
//     paperdoll-title table nothing had populated, and re-walked to the tile
//     it was already standing on. 24 of Corwyn's 24 picks, 27 of Tarath's 27.
//   * "wait longer than the action's own deadline" -- fixed in the bank path,
//     then again in supplies, then again here. At 2.5s against an 8s deadline
//     a request can only supersede itself, forever.
//   * "walk into reach before buying" -- the shop list opens from speech,
//     which carries across a room; the purchase needs touch. Fixed in
//     supplies as a chase-back, absent from bandages until it cost a session.
//   * "never ask for more than the shelf holds" -- Sphere refuses the WHOLE
//     order, so one over-ask buys nothing. Present in neither path.
//
// Those are not four bugs. They are one missing layer, copied.
//
// WHAT THIS IS NOT. It holds no opinion about WHAT to buy or WHY -- that is
// the activity's business, and it stays there. This owns only the sequence
// every buyer must perform correctly to get an item out of an NPC, and the
// definitive answer at the end of it.
//
// THE WAKE CONTRACT. Tick() returns what it is waiting FOR, not merely that
// it is waiting. Today Runner turns that into its own nextActionMs_; when the
// scheduler lands, a bot blocked on a vendor reply costs nothing until the
// reply arrives, which is the difference between 20 bots and 500. Designing
// it in now is free; retrofitting it across every activity later is not.
// ---------------------------------------------------------------------------

#include "uo/interaction/activity_result.h"
#include "uo/interaction/handshake.h"
#include "uo/types.h"
#include "uo/world_model.h"

#include <string>

namespace uo {
class Client;
}

namespace uo::life {

struct Observation;   // uo/life.h

// The result of one tick. The STATUS and WAKE vocabularies are the shared
// ones (uo/interaction/activity_result.h) -- this errand used to define its
// own three-state enum, which could not express "ran, nothing wrong, nothing
// moved", the state every spinning goal in this project's history was
// actually in.
//
// `why` is a std::string rather than the shared struct's const char* because
// these reasons are built per tick ("the 'healer' is 7 tiles off (dz 0)"),
// and a dangling pointer in a log line is a bug that only shows up at 3am.
// What to buy, and from whom. Filled once by the activity; the errand does
// not modify it.
struct VendorErrandSpec {
    // WHO MIGHT SELL IT, in preference order.
    //
    // More than one, because the evidence says so twice over: the food goal
    // asks a baker and falls back to a provisioner, and the spellbook goal
    // prefers a scribe (who lets you choose the scroll) and falls back to a
    // mage shop (who does not). A town without the first seller is common and
    // is not a reason to fail an errand.
    //
    // `trade` is the paperdoll job, matched by the client's own lookup --
    // which knows about gendered titles, and which must never resolve to a
    // guildmaster, who keeps no shop. `service` is where to travel when none
    // is in sight.
    struct Seller {
        const char* trade = nullptr;
        wm::Service service = wm::Service::None;
    };
    Seller sellers[3] = {};
    i32    sellerCount = 0;

    void Sell(const char* trade, wm::Service service) {
        if (sellerCount >= 3) return;
        sellers[sellerCount].trade = trade;
        sellers[sellerCount].service = service;
        ++sellerCount;
    }
    // The item, by graphic. Zero means "the caller will choose from the
    // offer itself" and Tick stops at OfferOpen.
    u16 graphic = 0;
    // How many are wanted. The errand clamps this to what the shelf actually
    // holds -- see Client::ActionVendorBuy.
    i32 qty = 1;
    // For the log line only.
    const char* what = "goods";
    // How many towns to try before giving up. One silent shop is not
    // evidence about a whole trade.
    i32 maxTrips = 3;
    // Never spend below this. The activity owns the policy; the errand only
    // enforces the number it is given.
    i32 goldFloor = 0;
    // A price ceiling, section 16's "verify price". Zero means none. The
    // activity decides what it is willing to pay; the errand refuses rows
    // above it rather than buying whatever the shop happens to quote.
    i32 maxPricePerUnit = 0;
};

struct VendorErrandResult {
    ActivityStatus status = ActivityStatus::Waiting;
    Wake        wake = Wake::Now;
    i64         delayMs = 0;         // meaningful when wake == AfterDelay
    // Always populated, success or failure. This is what the activity logs;
    // a refusal nobody can read is the defect this whole layer exists to
    // stop repeating.
    std::string why;
    // Set once the shop window belongs to our keeper, so an activity that
    // wants to choose a row itself can read Client::VendorOffer().
    bool offerOpen = false;
    u32  keeper = 0;
};

// A tick-machine, because the runner is one. It keeps its own trip and chase
// counters, which is deliberate: those counters used to be Runner members
// shared between goals, and a gear trip spent the spellbook's allowance.
class VendorErrand {
public:
    void Begin(const VendorErrandSpec& spec);
    bool Running() const { return running_; }
    void Cancel() { running_ = false; }

    // One step. Never blocks, never sleeps, never loops.
    VendorErrandResult Tick(Client& client, const Observation& obs);

private:
    enum class Step : u8 { Find, Approach, Open, Buy, Verify, Done };

    VendorErrandSpec spec_;
    Step step_ = Step::Find;
    i32  seller_ = 0;      // index into spec_.sellers
    bool running_ = false;
    u32  keeper_ = 0;
    i32  trips_ = 0;
    i32  chases_ = 0;
    i32  scans_ = 0;
    bool travelInFlight_ = false;

    // WHAT THE WORLD SHOULD LOOK LIKE IF THE PURCHASE WORKED, recorded
    // BEFORE it is attempted -- section 18. Without these the errand could
    // only report that Sphere accepted the packet, which is precisely the
    // "sent packet = success" that let a bot record eight buys while its
    // gold never moved.
    i32 packBefore_ = -1;
    i32 goldBefore_ = -1;
    i32 wantQty_ = 0;
    i32 unitPrice_ = 0;
    i64 verifyDeadlineMs_ = 0;

    // The ask that opens the shop, disciplined by the shared rule rather
    // than by a hand-picked millisecond count. See interaction/handshake.h.
    Handshake open_;
};

}  // namespace uo::life
