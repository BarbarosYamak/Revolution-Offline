#include "uo/vendor_errand.h"

#include "Client.h"
#include "uo/life.h"

#include <cstdio>

namespace uo::life {

namespace {

// Arm's length. The shop LIST opens from speech, which carries across a room;
// the PURCHASE needs touch. Conflating the two is what produced "You can't
// reach the Vendor" on every buy while the window sat open
// (run_m7/z_Corwyn.console.txt:1356 onward).
constexpr i32 kReach = 3;
// A shopkeeper may stand on a different floor of its own shop.
constexpr i32 kReachZ = 3;

// How long to wait after speaking to a vendor. MUST exceed the action's own
// deadline (Client's kVendorTimeoutMs, 8s): a retry issued inside it cannot
// resolve, only supersede itself. This lesson has now been learned separately
// by the bank path, the supplies path and the bandage path.
constexpr i64 kAfterAskMs = 9000;
constexpr i64 kShortMs    = 2000;
constexpr i64 kScanMs     = 3000;

// How many times to ask who is standing here before believing nobody is.
// Titles arrive only when requested, so "nobody with that trade" read off an
// unpopulated table is not an answer -- it is a question never asked.
constexpr i32 kMaxScans = 3;
// How far to chase a shopkeeper that wanders mid-errand before asking from
// where we stand and letting the server give the definitive no.
constexpr i32 kMaxChases = 3;

i32 TileDistance(i32 ax, i32 ay, i32 bx, i32 by) {
    const i32 dx = ax > bx ? ax - bx : bx - ax;
    const i32 dy = ay > by ? ay - by : by - ay;
    return dx > dy ? dx : dy;
}

VendorErrandResult Working(Wake w, i64 delayMs, std::string why) {
    VendorErrandResult r;
    r.state = ErrandState::Working;
    r.wake = w;
    r.delayMs = delayMs;
    r.why = std::move(why);
    return r;
}

VendorErrandResult Failed(std::string why) {
    VendorErrandResult r;
    r.state = ErrandState::Failed;
    r.wake = Wake::Now;
    r.why = std::move(why);
    return r;
}

std::string Fmt(const char* fmt, ...) {
    char buf[224];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return std::string(buf);
}

}  // namespace

const char* WakeName(Wake w) {
    switch (w) {
        case Wake::Now:            return "now";
        case Wake::AfterDelay:     return "after a delay";
        case Wake::ActionResolves: return "the action to resolve";
        case Wake::TravelArrives:  return "arrival";
    }
    return "?";
}

const char* ErrandStateName(ErrandState s) {
    switch (s) {
        case ErrandState::Working: return "working";
        case ErrandState::Bought:  return "bought";
        case ErrandState::Failed:  return "failed";
    }
    return "?";
}

void VendorErrand::Begin(const VendorErrandSpec& spec) {
    spec_ = spec;
    step_ = Step::Find;
    running_ = true;
    keeper_ = 0;
    trips_ = 0;
    chases_ = 0;
    scans_ = 0;
    seller_ = 0;
    travelInFlight_ = false;
}

VendorErrandResult VendorErrand::Tick(Client& client, const Observation& obs) {
    if (!running_) return Failed("the errand was never begun");

    // ONE ACTION IN FLIGHT AT A TIME, everywhere. Every step below issues at
    // most one, and none of them may be issued while another is outstanding:
    // that is the whole superseding defect, expressed once.
    if (client.ActionBusy())
        return Working(Wake::ActionResolves, 0, "an action is already in flight");
    if (client.TravelBusy())
        return Working(Wake::TravelArrives, 0, "still walking");

    switch (step_) {
        // ---------------------------------------------------------------
        case Step::Find: {
            if (spec_.sellerCount <= 0) {
                running_ = false;
                return Failed("the errand names nobody who might sell it");
            }
            const VendorErrandSpec::Seller& who = spec_.sellers[seller_];

            // A shopkeeper, explicitly NOT a guildmaster: the client's trade
            // lookup cuts the paperdoll job at the first space, so "Riley,
            // the healer guildmaster" reduces to "healer" and wins on
            // distance -- and a guildmaster keeps no shop. Corwyn asked
            // Riley to open a bandage list eight seconds at a time, twice,
            // and got silence, while the real healer stood four tiles on.
            keeper_ = client.NearestMobileWithTrade(who.trade);
            if (keeper_) {
                scans_ = 0;
                step_ = Step::Approach;
                return Working(Wake::Now, 0, Fmt("found a '%s'", who.trade));
            }

            // ASK BEFORE CONCLUDING NOBODY IS THERE. A title exists only once
            // it has been requested; reading "nobody" off a table nothing has
            // populated is not evidence.
            if (travelInFlight_ && ++scans_ <= kMaxScans) {
                client.ActionScanMobiles();
                return Working(Wake::AfterDelay, kScanMs,
                               Fmt("at the shop, asking who is here "
                                   "(scan %d of %d)", scans_, kMaxScans));
            }

            if (++trips_ > spec_.maxTrips) {
                // THIS seller is exhausted, which is not the same as the
                // errand being impossible. A town without a baker usually has
                // a provisioner; a town without a scribe has a mage shop.
                if (seller_ + 1 < spec_.sellerCount) {
                    ++seller_;
                    trips_ = 0;
                    scans_ = 0;
                    travelInFlight_ = false;
                    return Working(Wake::Now, 0,
                                   Fmt("no '%s' answered -- trying a '%s'",
                                       who.trade,
                                       spec_.sellers[seller_].trade));
                }
                running_ = false;
                return Failed(Fmt("no '%s' answered after %d trip(s)",
                                  who.trade, trips_ - 1));
            }
            scans_ = 0;
            travelInFlight_ = client.TravelToService(who.service, nullptr);
            if (!travelInFlight_) {
                return Working(Wake::AfterDelay, kShortMs,
                               Fmt("no route to a '%s': %s", who.trade,
                                   client.TravelFailureText()));
            }
            return Working(Wake::TravelArrives, 0,
                           Fmt("walking to a '%s' (trip %d)", who.trade,
                               trips_));
        }

        // ---------------------------------------------------------------
        case Step::Approach: {
            i32 vx = 0, vy = 0; i8 vz = 0;
            if (!client.MobilePosition(keeper_, &vx, &vy, &vz)) {
                // It was in the cache a moment ago and is not now. Ask again
                // rather than assuming it died or that we imagined it.
                keeper_ = 0;
                step_ = Step::Find;
                return Working(Wake::Now, 0, "lost sight of the shopkeeper");
            }
            const i32 d = TileDistance(obs.x, obs.y, vx, vy);
            const i32 dz = obs.z > vz ? obs.z - vz : vz - obs.z;
            if (d <= kReach && dz <= kReachZ) {
                step_ = Step::Open;
                return Working(Wake::Now, 0, "within reach");
            }
            // SHOPKEEPERS WALK, and they walk mid-errand. Chase, but not
            // forever: after a few laps ask from here and let the server give
            // a definitive refusal, because a refusal ends the errand
            // honestly where another lap ends nothing.
            if (++chases_ > kMaxChases) {
                step_ = Step::Open;
                return Working(Wake::Now, 0,
                               Fmt("the '%s' keeps moving (%d tiles after %d "
                                   "chases) -- asking from here",
                                   spec_.sellers[seller_].trade, d, kMaxChases));
            }
            travelInFlight_ = client.TravelToEntity(keeper_, 1);
            return Working(Wake::TravelArrives, 0,
                           Fmt("the '%s' is %d tiles off (dz %d) -- walking "
                               "into reach (chase %d of %d)",
                               spec_.sellers[seller_].trade, d, dz, chases_, kMaxChases));
        }

        // ---------------------------------------------------------------
        case Step::Open: {
            // Whose window is open matters: an offer outlives the errand that
            // opened it, so buying out of somebody else's list is buying from
            // a shop we never walked to.
            if (client.VendorOfferFrom() == keeper_ &&
                !client.VendorOffer().empty()) {
                step_ = Step::Buy;
                return Working(Wake::Now, 0, "the shop is open");
            }
            client.ActionVendorOpen(keeper_);
            return Working(Wake::AfterDelay, kAfterAskMs,
                           Fmt("asking the '%s' to show %s", spec_.sellers[seller_].trade,
                               spec_.what));
        }

        // ---------------------------------------------------------------
        case Step::Buy: {
            // The caller wants to choose its own row out of the offer.
            if (spec_.graphic == 0) {
                VendorErrandResult r;
                r.state = ErrandState::Working;
                r.wake = Wake::Now;
                r.offerOpen = true;
                r.keeper = keeper_;
                r.why = "the shop is open; the caller chooses";
                return r;
            }

            for (const Client::VendorItem& v : client.VendorOffer()) {
                if (v.graphic != spec_.graphic) continue;
                if (v.amount == 0) continue;

                // Quantity is clamped in Client::ActionVendorBuy, where it
                // belongs -- Sphere refuses the WHOLE order when the ask
                // exceeds stock, which is a fact about the server and not a
                // decision any errand gets to make. What is decided HERE is
                // affordability, which is the activity's money and the
                // activity's floor.
                i32 want = spec_.qty;
                if (want > static_cast<i32>(v.amount))
                    want = static_cast<i32>(v.amount);
                const i32 unit = static_cast<i32>(v.price);
                if (unit > 0) {
                    const i32 spendable = obs.gold - spec_.goldFloor;
                    if (spendable < unit) {
                        running_ = false;
                        return Failed(
                            Fmt("%d gold with a floor of %d cannot buy one %s "
                                "at %d", obs.gold, spec_.goldFloor,
                                spec_.what, unit));
                    }
                    if (want * unit > spendable) want = spendable / unit;
                }
                if (want <= 0) {
                    running_ = false;
                    return Failed(Fmt("nothing affordable in this '%s'",
                                      spec_.sellers[seller_].trade));
                }

                client.ActionVendorBuy(keeper_, v.serial,
                                       static_cast<u16>(want));
                step_ = Step::Done;
                VendorErrandResult r;
                r.state = ErrandState::Bought;
                r.wake = Wake::AfterDelay;
                r.delayMs = kAfterAskMs;
                r.keeper = keeper_;
                r.offerOpen = true;
                r.why = Fmt("buying %d %s at %d each from the '%s' "
                            "(shelf holds %u)", want, spec_.what, unit,
                            spec_.sellers[seller_].trade, v.amount);
                running_ = false;
                return r;
            }

            // A shop that does not stock it is a definitive answer about THIS
            // shop, not about the trade -- so the caller may try another town.
            running_ = false;
            return Failed(Fmt("this '%s' does not stock %s", spec_.sellers[seller_].trade,
                              spec_.what));
        }

        // ---------------------------------------------------------------
        case Step::Done:
            running_ = false;
            return Failed("the errand already finished");
    }

    running_ = false;
    return Failed("unreachable errand state");
}

}  // namespace uo::life
