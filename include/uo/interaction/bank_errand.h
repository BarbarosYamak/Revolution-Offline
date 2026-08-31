#pragma once

// ---------------------------------------------------------------------------
// BANK ERRAND -- get this character's bank box open
// (docs/BOT_ARCHITECTURE.md sections 16, 17).
//
// The same six steps as VendorErrand, against a different NPC and a different
// definition of success, and every one of them was hand-written in DoBank
// with its own counters living on the Runner: bankerAsked_, bankerCounted_,
// bankOpenTries_, bankerSilent_, bankShouts_, bankTitlesAskedMs_. Six members
// of the runner to open one container -- the same counters-on-the-runner
// pattern that let a gear trip spend the spellbook's trip allowance.
//
// TWO THINGS ABOUT BANKS THAT ARE NOT TRUE OF SHOPS, both learned live:
//
//   1. SUCCESS IS THE BOX SERIAL, NOT ITS CONTENTS. An EMPTY bank box sends
//      no 0x3C at all, so a check for "contents known" never flipped and the
//      character re-opened the bank every 2.5 seconds forever. You do not
//      need to know what is in a container to put something into it.
//
//   2. THE KEYWORD WORKS WITHOUT A NAMED BANKER. Sphere opens the box from
//      SPEECH, so a character standing in a bank whose banker it cannot
//      identify by paperdoll title can still say "bank" aloud and be served.
//      That fallback is why bankShouts_ existed, and it is kept.
//
// Everything else -- the deadline rule, the refusal rule, the silent-NPC
// rotation -- is the shared machinery, not another copy of it.
//
// AT A KNOWN LOCATION, SPEAK -- DO NOT HUNT FOR EYE CONTACT (project owner,
// 2026-08-31): "we know the bank location, you don't need to see the banker
// itself". NPCBRAIN_BANKER waives Sphere's usual LOS-gated hearing
// (Source-X e6e77557) -- a banker answers through a wall -- so a caller that
// already knows (from the atlas or its own memory) it is standing where a
// bank is should set SetAtKnownBank(true) before Tick(). Step::Find then
// skips NearestMobileWithTrade and ActionScanMobiles entirely and goes
// straight to the keyword ask: hunting for the specific mobile is
// unnecessary work with its own failure mode (v3_Corwyn: two visible
// bankers, 95 attempts, no box ever opened). Defaults to false and resets
// on every Begin(), so callers that never call it -- the market/trade and
// wind-down errands -- keep the original find-a-mobile behaviour unchanged.
// ---------------------------------------------------------------------------

#include "uo/interaction/activity_result.h"
#include "uo/interaction/handshake.h"
#include "uo/interaction/npc_rotation.h"
#include "uo/types.h"

#include <string>

namespace uo {
class Client;
}

namespace uo::life {

struct Observation;   // uo/life.h

struct BankErrandResult {
    ActivityStatus status = ActivityStatus::Waiting;
    Wake           wake = Wake::Now;
    i64            delayMs = 0;
    // True only on a tick that ISSUED a request to the server. Same rule and
    // same reason as ActivityTickResult::acted.
    bool           acted = false;
    std::string    why;
    // The open box, once there is one. This is the errand's whole output.
    u32            box = 0;
    u32            banker = 0;
};

class BankErrand {
public:
    void Begin();
    bool Running() const { return running_; }
    void Cancel() { running_ = false; }

    // Tell the errand it is already standing where a bank is -- see the
    // header comment. Call this every tick before Tick() when it applies;
    // it is read once per Tick() and defaults (and resets on Begin()) to
    // false, so a caller that never calls it gets the original behaviour.
    void SetAtKnownBank(bool v) { atKnownBank_ = v; }

    BankErrandResult Tick(Client& client, const Observation& obs);

private:
    enum class Step : u8 { Find, Approach, Ask, Done };

    Step        step_ = Step::Find;
    bool        running_ = false;
    bool        atKnownBank_ = false;
    u32         banker_ = 0;
    i32         shouts_ = 0;
    i64         scannedAtMs_ = 0;
    Handshake   ask_;
    NpcRotation rotation_;
};

}  // namespace uo::life
