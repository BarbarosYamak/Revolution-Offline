#include "uo/vendor_errand.h"

#include "Client.h"
#include "uo/life.h"
#include "uo/interaction/progress.h"

#include <cstdio>

namespace uo::life {

namespace {

// Arm's length. The shop LIST opens from speech, which carries across a room;
// the PURCHASE needs touch. Conflating the two is what produced "You can't
// reach the Vendor" on every buy while the window sat open
// (run_m7/z_Corwyn.console.txt:1356 onward).
// Live Source-X rejects the actual purchase at three tiles even though the
// spoken "buy" command opens the list. Stand adjacent before committing gold.
constexpr i32 kReach = 1;
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
// How long to wait for the pack and purse to confirm a purchase before
// calling it NoProgress. Generous: Sphere sends the container update and the
// gold change as separate packets, and a busy shard delays both.
constexpr i64 kVerifyWindowMs = 12000;
// What the CLIENT allows a vendor action, mirrored here so the handshake
// derives its retry gap from the real number. Client.cpp's kVendorTimeoutMs.
constexpr i64 kVendorActionDeadlineMs = 8000;

i32 TileDistance(i32 ax, i32 ay, i32 bx, i32 by) {
    const i32 dx = ax > bx ? ax - bx : bx - ax;
    const i32 dy = ay > by ? ay - by : by - ay;
    return dx > dy ? dx : dy;
}

VendorErrandResult Working(Wake w, i64 delayMs, std::string why) {
    VendorErrandResult r;
    r.status = ActivityStatus::Waiting;
    r.wake = w;
    r.delayMs = delayMs;
    r.why = std::move(why);
    return r;
}

// The same Working(), for a tick that ISSUED a request rather than waited on
// one. Only these count as attempts to the caller -- see
// ActivityTickResult::acted and the Bruin evidence quoted there.
VendorErrandResult Acted(VendorErrandResult r) {
    r.acted = true;
    return r;
}

VendorErrandResult Failed(std::string why) {
    VendorErrandResult r;
    r.status = ActivityStatus::Failed;
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

    // The deadline is the CLIENT's, not a guess: kVendorTimeoutMs is what
    // Client applies to a vendor action, and the retry gap is derived from
    // it rather than chosen. That derivation is the entire reason the four
    // hand-written 2.5-second retries could exist at all.
    RetryPolicy rp;
    rp.actionDeadlineMs = kVendorActionDeadlineMs;
    rp.maxAttempts = 3;
    rp.backoffMs = 2000;
    open_.Configure(rp);
    open_.Reset();
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
                return Acted(Working(Wake::AfterDelay, kScanMs,
                                     Fmt("at the shop, asking who is here "
                                         "(scan %d of %d)", scans_, kMaxScans)));
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
                open_.Note(Outcome::Succeeded, obs.nowMs);
                step_ = Step::Buy;
                return Working(Wake::Now, 0, "the shop is open");
            }

            // THE SERVER'S ANSWER, READ RATHER THAN DISCARDED. A rejection is
            // definitive and ends this door now; a timeout says nothing about
            // the world and only backs off.
            if (!client.ActionBusy() &&
                client.ActionKind() == act::Kind::VendorBuy) {
                const act::Result res = client.ActionResult();
                if (res == act::Result::Rejected ||
                    res == act::Result::Unavailable ||
                    res == act::Result::ServerFailure) {
                    open_.Note(Outcome::Refused, obs.nowMs,
                               act::ResultName(res));
                }
            }
            if (open_.Expired(obs.nowMs)) open_.NoteExpiry(obs.nowMs);

            // THE DEADLINE RULE, ENFORCED BY A TYPE. This branch used to be
            // "issue, then nextActionMs_ = now + 9000", a number chosen by
            // hand in four separate files and got wrong in three of them.
            const char* whyNot = "";
            if (!open_.MayIssue(obs.nowMs, &whyNot)) {
                if (open_.Exhausted() ||
                    open_.State() == HandshakeState::ConfirmedFailure) {
                    // This shopkeeper will not open. Another town might, and
                    // the seller list is how the errand gets there -- so this
                    // is retryable, not a flat failure.
                    const char* said = open_.Refusal();
                    VendorErrandResult r;
                    r.status = ActivityStatus::RetryableFailure;
                    r.wake = Wake::Now;
                    r.why = Fmt("the '%s' would not open a shop%s%s",
                                spec_.sellers[seller_].trade,
                                (said && said[0]) ? ": " : "",
                                (said && said[0]) ? said : "");
                    running_ = false;
                    return r;
                }
                return Working(Wake::ActionResolves, kShortMs, whyNot);
            }

            client.ActionVendorOpen(keeper_);
            open_.NoteIssued(obs.nowMs);
            return Acted(Working(Wake::ActionResolves, 0,
                                 Fmt("asking the '%s' to show %s (attempt %d)",
                                     spec_.sellers[seller_].trade, spec_.what,
                                     open_.Attempts())));
        }

        // ---------------------------------------------------------------
        case Step::Buy: {
            // The caller wants to choose its own row out of the offer.
            if (spec_.graphic == 0) {
                VendorErrandResult r;
                r.status = ActivityStatus::Waiting;
                r.wake = Wake::Now;
                r.offerOpen = true;
                r.keeper = keeper_;
                r.why = "the shop is open; the caller chooses";
                return r;
            }

            for (const Client::VendorItem& v : client.VendorOffer()) {
                if (v.graphic != spec_.graphic) continue;
                if (v.amount == 0) continue;
                // A PRICE CEILING (section 16). A bot with a full purse will
                // otherwise pay whatever number the shop says.
                if (spec_.maxPricePerUnit > 0 &&
                    static_cast<i32>(v.price) > spec_.maxPricePerUnit) {
                    running_ = false;
                    return Failed(Fmt("the '%s' wants %d for %s, above the %d "
                                      "this errand will pay",
                                      spec_.sellers[seller_].trade,
                                      static_cast<i32>(v.price), spec_.what,
                                      spec_.maxPricePerUnit));
                }

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

                // RECORD WHAT SUCCESS WILL LOOK LIKE, then attempt it.
                //
                // Section 18. Until this existed the errand reported "bought"
                // the instant Sphere accepted the packet, and a healer holding
                // 19 against an ask of 20 refused the whole order eight times
                // while the bot recorded eight purchases and its gold never
                // moved.
                packBefore_ = static_cast<i32>(
                    client.BackpackItemCount(spec_.graphic));
                goldBefore_ = client.PlayerGold();
                wantQty_ = want;
                unitPrice_ = unit;
                verifyDeadlineMs_ = obs.nowMs + kVerifyWindowMs;

                client.ActionVendorBuy(keeper_, v.serial,
                                       static_cast<u16>(want));
                step_ = Step::Verify;
                VendorErrandResult r;
                r.status = ActivityStatus::Waiting;
                r.wake = Wake::InventoryChanges;
                r.delayMs = kAfterAskMs;
                r.keeper = keeper_;
                r.offerOpen = true;
                r.acted = true;
                r.why = Fmt("buying %d %s at %d each from the '%s' "
                            "(shelf holds %u) -- waiting for the pack",
                            want, spec_.what, unit,
                            spec_.sellers[seller_].trade, v.amount);
                return r;
            }

            // A shop that does not stock it is a definitive answer about THIS
            // shop, not about the trade -- so the caller may try another town.
            running_ = false;
            return Failed(Fmt("this '%s' does not stock %s", spec_.sellers[seller_].trade,
                              spec_.what));
        }

        // ---------------------------------------------------------------
        case Step::Verify: {
            // THE WORLD, NOT THE PACKET, DECIDES (section 18).
            Expectation want;
            want.itemGraphic = spec_.graphic;
            want.itemBefore = packBefore_;
            want.itemGain = 1;                  // one is proof; the rest is luck
            want.goldBefore = goldBefore_;
            want.goldSpendMin = unitPrice_ > 0 ? 1 : 0;
            want.goldSpendMax = unitPrice_ > 0 ? unitPrice_ * wantQty_ : 0;

            Observed seen;
            seen.itemNow =
                static_cast<i32>(client.BackpackItemCount(spec_.graphic));
            seen.goldNow = client.PlayerGold();

            const ProgressCheck check = Verify(want, seen);
            switch (check.verdict) {
                case Verdict::Confirmed: {
                    running_ = false;
                    step_ = Step::Done;
                    VendorErrandResult r;
                    r.status = ActivityStatus::Success;
                    r.wake = Wake::Now;
                    r.keeper = keeper_;
                    r.offerOpen = true;
                    r.why = Fmt("%d %s in the pack, %d gold gone -- %s",
                                check.itemDelta, spec_.what, -check.goldDelta,
                                check.reason);
                    return r;
                }
                case Verdict::Contradicted:
                    // A DEFINITIVE no. Ending here is the point: the errand
                    // that could not tell this from "not yet" spent whole
                    // sessions asking again.
                    running_ = false;
                    step_ = Step::Done;
                    return Failed(Fmt("%s (pack %+d, purse %+d)", check.reason,
                                      check.itemDelta, check.goldDelta));
                case Verdict::NotYet:
                    if (obs.nowMs >= verifyDeadlineMs_) {
                        // Ran, nothing went wrong, and nothing moved. That is
                        // NoProgress -- not success, and not a fault to blame
                        // on the shop either.
                        running_ = false;
                        step_ = Step::Done;
                        VendorErrandResult r;
                        r.status = ActivityStatus::NoProgress;
                        r.wake = Wake::Now;
                        r.keeper = keeper_;
                        r.why = Fmt("asked for %d %s and %s after %llds",
                                    wantQty_, spec_.what, check.reason,
                                    static_cast<long long>(kVerifyWindowMs / 1000));
                        return r;
                    }
                    return Working(Wake::InventoryChanges, kShortMs,
                                   check.reason);
                case Verdict::NothingChecked:
                default:
                    // Cannot happen: the buy step always records an
                    // expectation. If it ever does, say so rather than
                    // inventing a yes.
                    running_ = false;
                    step_ = Step::Done;
                    return Failed("the purchase stated no expectation to check");
            }
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
