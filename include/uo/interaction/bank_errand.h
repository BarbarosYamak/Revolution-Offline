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

    BankErrandResult Tick(Client& client, const Observation& obs);

private:
    enum class Step : u8 { Find, Approach, Ask, Done };

    Step        step_ = Step::Find;
    bool        running_ = false;
    u32         banker_ = 0;
    i32         shouts_ = 0;
    i64         scannedAtMs_ = 0;
    Handshake   ask_;
    NpcRotation rotation_;
};

}  // namespace uo::life
