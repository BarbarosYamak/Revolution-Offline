#include "uo/interaction/bank_errand.h"

#include "Client.h"
#include "uo/life.h"

#include <cstdarg>
#include <cstdio>

namespace uo::life {

namespace {

// Client.cpp's kBankTimeoutMs equivalent: the bank ask is a speech action and
// the box arrives as a container. Mirrored so the handshake derives its retry
// gap from the real deadline rather than a hand-picked pause -- the defect
// this whole layer exists to make unwritable.
constexpr i64 kBankActionDeadlineMs = 8000;

// HOW CLOSE TO STAND TO A BANKER, and why it is ONE tile rather than two.
//
// Sphere gates hearing on LINE OF SIGHT, not merely distance: Event_Talk
// skips any NPC failing CanSeeLOS unless sphere.ini's NPCDistanceHear is
// negative (CClientEvent.cpp:1892, :1949). That setting is GLOBAL -- there is
// no banker-only form of it, and the only banker-specific branch lives inside
// Source-X, which this project does not modify.
//
// So a banker in the next room of the same building simply cannot hear the
// word, however close in tiles the character stands. v3_Corwyn proved it:
// two bankers asked, 95 attempts, no box ever opened (2026-08-30 15:08-15:13).
//
// Standing ON the counter tile is what a player does, and it is the only
// thing that reliably puts a wall behind us rather than between us.
constexpr i32 kBankerReach = 1;

// How many times to ask ONE banker before deciding it is not listening, and
// how many bankers are worth trying before the errand fails honestly.
constexpr i32 kTriesPerBanker = 3;
constexpr i32 kBankersWorthTrying = 3;

// The keyword fallback, for a bank whose banker we cannot name.
constexpr i32 kMaxShouts = 3;

// How stale a mobile scan may be before asking again. Titles arrive only when
// requested, and "no banker here" read off an unpopulated table is not an
// answer -- it is a question never asked.
constexpr i64 kScanFreshMs = 20000;

std::string Fmt(const char* fmt, ...) {
    char buf[224];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return std::string(buf);
}

i32 TileDistance(i32 ax, i32 ay, i32 bx, i32 by) {
    const i32 dx = ax > bx ? ax - bx : bx - ax;
    const i32 dy = ay > by ? ay - by : by - ay;
    return dx > dy ? dx : dy;
}

BankErrandResult Working(Wake w, i64 delayMs, std::string why) {
    BankErrandResult r;
    r.status = ActivityStatus::Waiting;
    r.wake = w;
    r.delayMs = delayMs;
    r.why = std::move(why);
    return r;
}

// The same Working(), for a tick that ISSUED a request rather than waited on
// one. Only these count as attempts to the caller -- see
// ActivityTickResult::acted.
BankErrandResult Acted(BankErrandResult r) {
    r.acted = true;
    return r;
}

}  // namespace

void BankErrand::Begin() {
    step_ = Step::Find;
    running_ = true;
    banker_ = 0;
    shouts_ = 0;
    scannedAtMs_ = 0;

    RetryPolicy rp;
    rp.actionDeadlineMs = kBankActionDeadlineMs;
    rp.maxAttempts = kTriesPerBanker;
    rp.backoffMs = 2000;
    ask_.Configure(rp);
    ask_.Reset();
    rotation_.Configure(kTriesPerBanker);
    rotation_.Reset();
}

BankErrandResult BankErrand::Tick(Client& client, const Observation& obs) {
    if (!running_) {
        BankErrandResult r;
        r.status = ActivityStatus::Failed;
        r.why = "the errand was never begun";
        return r;
    }

    // SUCCESS IS THE BOX SERIAL, AND NOTHING MORE.
    //
    // An EMPTY bank box sends no 0x3C, so a check for "contents known" never
    // flips and the character re-opens the bank every 2.5 seconds forever.
    // You do not need to know what is in a container to put something in it.
    if (const u32 box = client.BankContainer()) {
        running_ = false;
        step_ = Step::Done;
        ask_.Note(Outcome::Succeeded, obs.nowMs);
        rotation_.NoteAnswered();
        BankErrandResult r;
        r.status = ActivityStatus::Success;
        r.wake = Wake::Now;
        r.box = box;
        r.banker = banker_;
        r.why = "the box is open";
        return r;
    }

    if (client.ActionBusy())
        return Working(Wake::ActionResolves, 0, "an ask is in flight");
    if (client.TravelBusy())
        return Working(Wake::TravelArrives, 0, "walking to the bank");

    switch (step_) {
        case Step::Find: {
            // Skip the ones that have already ignored us -- but only those.
            const u32 found =
                client.NearestMobileWithTrade("banker", rotation_.Skip());
            if (found) {
                rotation_.Aim(found);
                banker_ = found;
                step_ = Step::Approach;
                return Working(Wake::Now, 0, "found a banker");
            }

            // ASK WHO IS HERE before believing nobody is.
            if (!scannedAtMs_ || obs.nowMs - scannedAtMs_ > kScanFreshMs) {
                client.ActionScanMobiles();
                scannedAtMs_ = obs.nowMs;
                return Acted(Working(Wake::AfterDelay, 2000,
                                     "asking who is standing in the bank"));
            }

            // THE KEYWORD WORKS WITHOUT A NAMED BANKER. Sphere opens the box
            // from SPEECH, so a character standing in a bank whose banker it
            // cannot identify may still say the word and be served.
            if (obs.atBank && ++shouts_ <= kMaxShouts) {
                client.ActionOpenBank(0, "bank");
                return Acted(Working(Wake::AfterDelay,
                                     kBankActionDeadlineMs + 1000,
                                     Fmt("no banker recognised here -- saying "
                                         "'bank' aloud (%d of %d)", shouts_,
                                         kMaxShouts)));
            }

            running_ = false;
            BankErrandResult r;
            r.status = ActivityStatus::RetryableFailure;
            r.why = obs.atBank
                        ? Fmt("said 'bank' %d times where the box should be "
                              "and nobody answered", shouts_ - 1)
                        : std::string("no banker in sight");
            return r;
        }

        case Step::Approach: {
            i32 bx = 0, by = 0; i8 bz = 0;
            if (!client.MobilePosition(banker_, &bx, &by, &bz)) {
                banker_ = 0;
                step_ = Step::Find;
                return Working(Wake::Now, 0, "lost sight of the banker");
            }
            const i32 d = TileDistance(obs.x, obs.y, bx, by);
            if (d <= kBankerReach) {
                step_ = Step::Ask;
                return Working(Wake::Now, 0, "at the counter");
            }
            client.TravelToEntity(banker_, 1);
            return Working(Wake::TravelArrives, 0,
                           Fmt("the banker is %d tiles off -- stepping up", d));
        }

        case Step::Ask: {
            if (ask_.Expired(obs.nowMs)) {
                ask_.NoteExpiry(obs.nowMs);
                // A banker that took the words and produced no box has told
                // us something about ITSELF, not about banking.
                if (rotation_.NoteSilence()) {
                    ask_.Reset();
                    banker_ = 0;
                    step_ = Step::Find;
                    if (rotation_.OutOfDoors(kBankersWorthTrying)) {
                        running_ = false;
                        BankErrandResult r;
                        r.status = ActivityStatus::RetryableFailure;
                        r.why = Fmt("%d bankers asked, none opened a box",
                                    rotation_.Exhausted());
                        return r;
                    }
                    return Working(Wake::Now, 0,
                                   "that banker is not listening -- trying "
                                   "another");
                }
            }

            const char* whyNot = "";
            if (!ask_.MayIssue(obs.nowMs, &whyNot))
                return Working(Wake::ActionResolves, 0, whyNot);

            client.ActionOpenBank(banker_, "bank");
            ask_.NoteIssued(obs.nowMs);
            return Acted(Working(Wake::ActionResolves, 0,
                                 Fmt("asking the banker for the box (attempt %d)",
                                     ask_.Attempts())));
        }

        case Step::Done:
            running_ = false;
            BankErrandResult r;
            r.status = ActivityStatus::Failed;
            r.why = "the errand already finished";
            return r;
    }

    running_ = false;
    BankErrandResult r;
    r.status = ActivityStatus::Failed;
    r.why = "unreachable bank state";
    return r;
}

}  // namespace uo::life
