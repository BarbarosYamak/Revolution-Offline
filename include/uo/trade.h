#pragma once

// ---------------------------------------------------------------------------
// Secure player-to-player trade state (M3).
//
// The protocol, from Source-X:
//
//   * A trade is opened by DROPPING an item on another player -- the classic
//     way, and the only one a 2.0.x client has (`CClient::Event_Item_Drop` ->
//     `Cmd_SecureTrade`, src/game/clients/CClientEvent.cpp:325). The context
//     menu route needs a newer client.
//   * The server then creates two linked containers, equips one on each
//     character at LAYER_SPECIAL, and sends both clients 0x6F
//     SECURE_TRADE_OPEN carrying the partner's serial, both container serials
//     and the partner's name (`CClient::Cmd_SecureTrade`,
//     src/game/clients/CClientUse.cpp:1394-1425).
//   * Items are added by ordinary lift+drop INTO your own trade container.
//   * Each side accepts with 0x6F SECURE_TRADE_CHANGE carrying its own
//     container and a check flag. When both checks are set the server moves
//     the goods (`CItemContainer::Trade_Status`,
//     src/game/items/CItemContainer.cpp:129-157).
//   * Un-checking clears the PARTNER's check as well (:145-146).
//   * There is no virtual gold ledger for this client version, so gold is
//     traded as coin items in the window like anything else.
//
// One thing this models that the server does not: acceptance is cleared here
// whenever the contents of either window change. Sphere does not do that, so a
// partner can add or remove an item after you have accepted and the trade will
// still complete. Refusing to stay accepted across a change is the client
// being careful, and it is marked as such rather than presented as a rule.
//
// All state is per-session by construction -- one TradeState lives on Client.
// ---------------------------------------------------------------------------

#include "uo/types.h"

#include <string>
#include <vector>

namespace uo::trade {

enum class Phase : u8 {
    None = 0,
    Open,        // both windows are up, contents in flux
    Accepted,    // we have accepted; waiting on the partner
    Completed,   // the server closed it after both accepted
    Cancelled,   // closed without both accepting
    Count,
};

const char* PhaseName(Phase p);

// Why a trade ended, for logs and for a caller deciding what to do next.
enum class CloseReason : u8 {
    None = 0,
    BothAccepted,
    WeCancelled,
    PartnerCancelled,
    PartnerGone,
    Count,
};

const char* CloseReasonName(CloseReason r);

class TradeState {
public:
    // 0x6F SECURE_TRADE_OPEN.
    void OnOpened(u32 partnerSerial, const char* partnerName, u32 myContainer,
                  u32 theirContainer, i64 nowMs);
    // 0x6F SECURE_TRADE_CHANGE: the server's view of both check boxes.
    void OnCheckChanged(bool mine, bool theirs, i64 nowMs);
    // 0x6F SECURE_TRADE_CLOSE, or the partner vanished.
    void OnClosed(CloseReason reason, i64 nowMs);

    // Contents, learned from the ordinary container packets (0x25 / 0x3C /
    // 0x1D) rather than from anything trade-specific.
    void OnItemAdded(u32 container, u32 item);
    void OnItemRemoved(u32 item);

    // We sent an accept/retract. The authoritative answer still arrives as a
    // 0x6F CHANGE; this only stops us sending it twice.
    void NoteCheckSent(bool accepted, i64 nowMs);

    bool  Active() const { return phase_ == Phase::Open || phase_ == Phase::Accepted; }
    Phase CurrentPhase() const { return phase_; }
    CloseReason Reason() const { return reason_; }

    u32 PartnerSerial() const { return partnerSerial_; }
    const std::string& PartnerName() const { return partnerName_; }
    u32 MyContainer() const { return myContainer_; }
    u32 TheirContainer() const { return theirContainer_; }
    bool MyCheck() const { return myCheck_; }
    bool TheirCheck() const { return theirCheck_; }
    // Latched the moment the server reported both boxes ticked. From then on
    // the trade is committing, and the item movements that follow are the
    // server handing the goods over -- not a partner editing the table. The
    // CLOSE that ends a completed trade carries no reason code, so this latch
    // is the only thing that tells completion from cancellation.
    bool BothAccepted() const { return bothAccepted_; }
    bool CheckSent() const { return checkSent_; }

    const std::vector<u32>& MyOffer() const { return myOffer_; }
    const std::vector<u32>& TheirOffer() const { return theirOffer_; }
    bool Offering(u32 item) const;

    // How many times the contents changed after we had accepted -- the
    // client-side safety net described above, exposed so a caller can log it.
    int AcceptResets() const { return acceptResets_; }

    void Reset();

private:
    void ContentsChanged();

    Phase phase_ = Phase::None;
    CloseReason reason_ = CloseReason::None;
    u32 partnerSerial_ = 0;
    std::string partnerName_;
    u32 myContainer_ = 0;
    u32 theirContainer_ = 0;
    bool myCheck_ = false;
    bool theirCheck_ = false;
    bool checkSent_ = false;
    bool bothAccepted_ = false;
    int acceptResets_ = 0;
    i64 openedMs_ = 0;
    std::vector<u32> myOffer_;
    std::vector<u32> theirOffer_;
};

} // namespace uo::trade
