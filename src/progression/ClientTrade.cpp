// ---------------------------------------------------------------------------
// Secure player-to-player trade, on the wire (M3).
//
// Everything here is the ordinary UO protocol a human player's client sends:
// a lift and a drop to open the window and to put goods in it, and 0x6F to
// tick the accept box. No item ever changes hands except by Sphere deciding
// it does.
// ---------------------------------------------------------------------------

#include "Client.h"

#include "uo/endian.h"

#include <cstdio>
#include <cstring>

namespace uo {

namespace {

// The server answers a trade request with 0x6F OPEN. Nothing is sent for a
// refusal (`REFUSETRADES`, an NPC, an offline player), so the action needs a
// deadline of its own.
constexpr i64 kTradeOpenTimeoutMs = 8000;

} // namespace

// --- outbound --------------------------------------------------------------

// 0x6F: cmd, len, action, container, flag. The container is always OUR side of
// the window -- Sphere refuses one that is not parented to us
// (`PacketSecureTradeReq::onReceive`, network/receive.cpp:1129-1133).
void Client::SendTradeAction(u8 action, u32 container, u32 flag) {
    u8 pkt[12];
    pkt[0] = 0x6F;
    pkt[1] = 0x00;
    pkt[2] = 0x0C;
    pkt[3] = action;
    StoreBE32(pkt + 4, container);
    StoreBE32(pkt + 8, flag);
    Send(pkt, sizeof(pkt), "0x6F SecureTrade");
}

void Client::ActionTradeStart(u32 partnerSerial, u32 itemSerial) {
    if (trade_.Active()) {
        LogWarn("[trade] already trading with 0x%08X\n", trade_.PartnerSerial());
        return;
    }
    BeginAction(act::Kind::TradeOpen, kTradeOpenTimeoutMs);
    action_.subject = itemSerial;
    action_.destination = partnerSerial;
    LogInfo("[trade] offering 0x%08X to 0x%08X to open a trade\n", itemSerial,
            partnerSerial);

    // Dropping an item on a player IS the trade request on this client
    // version. The lift is the same one every inventory move uses.
    if (!SendLift(itemSerial, 1)) {
        FinishAction(act::Result::Rejected, "could not lift the offered item");
        return;
    }
    SendDropToContainer(itemSerial, partnerSerial);
}

void Client::ActionTradeOffer(u32 itemSerial, u16 amount) {
    if (!trade_.Active()) {
        LogWarn("[trade] no trade window open\n");
        return;
    }
    BeginAction(act::Kind::MoveItem, 8000);
    action_.subject = itemSerial;
    action_.destination = trade_.MyContainer();
    LogInfo("[trade] putting 0x%08X into the trade window\n", itemSerial);
    if (!SendLift(itemSerial, amount ? amount : 1)) {
        FinishAction(act::Result::Rejected, "could not lift the item");
        return;
    }
    SendDropToContainer(itemSerial, trade_.MyContainer());
}

bool Client::ActionTradeAccept(bool accept) {
    if (!trade_.Active()) return false;
    LogInfo("[trade] %s the trade\n", accept ? "accepting" : "retracting");
    trade_.NoteCheckSent(accept, NowMs());
    SendTradeAction(/*SECURE_TRADE_CHANGE=*/2, trade_.MyContainer(),
                    accept ? 1u : 0u);
    return true;
}

bool Client::ActionTradeCancel() {
    if (!trade_.Active()) return false;
    LogInfo("[trade] cancelling\n");
    SendTradeAction(/*SECURE_TRADE_CLOSE=*/1, trade_.MyContainer(), 0);
    trade_.OnClosed(trade::CloseReason::WeCancelled, NowMs());
    return true;
}

// --- inbound ---------------------------------------------------------------

// 0x6F. Layouts, from Source-X's PacketTradeAction:
//   OPEN   : [3]=0 [4..7]partner [8..11]myCont [12..15]theirCont [16]1 [17..46]name
//   CHANGE : [3]=2 [4..7]cont1   [8..11]check1 [12..15]check2     [16]0
//   CLOSE  : [3]=1 [4..7]cont    [8..11]0      [12..15]0          [16]0
// The container in CHANGE/CLOSE is always OUR side, because the server sends
// each client its own view.
void Client::OnSecureTrade(const u8* data, usize size) {
    if (size < 17) return;
    const u8 action = data[3];
    const u32 a = LoadBE32(data + 4);
    const u32 b = LoadBE32(data + 8);
    const u32 c = LoadBE32(data + 12);

    switch (action) {
        case 0: {   // OPEN
            char name[31];
            std::memset(name, 0, sizeof(name));
            if (size >= 47) std::memcpy(name, data + 17, 30);
            trade_.OnOpened(a, name, b, c, NowMs());
            LogInfo("[trade] window open with '%s' (0x%08X); mine=0x%08X "
                    "theirs=0x%08X\n", name, a, b, c);
            char ev[160];
            std::snprintf(ev, sizeof(ev),
                          "partner=0x%08X name='%s' mine=0x%08X theirs=0x%08X",
                          a, name, b, c);
            LogEvent("trade_open", ev);
            if (action_.Active() && action_.kind == act::Kind::TradeOpen)
                FinishAction(act::Result::Success, "trade window opened");
            break;
        }
        case 2: {   // CHANGE
            const bool mine = b != 0;
            const bool theirs = c != 0;
            trade_.OnCheckChanged(mine, theirs, NowMs());
            LogInfo("[trade] accept state: mine=%d theirs=%d\n", mine ? 1 : 0,
                    theirs ? 1 : 0);
            char ev[96];
            std::snprintf(ev, sizeof(ev), "mine=%d theirs=%d", mine ? 1 : 0,
                          theirs ? 1 : 0);
            LogEvent("trade_accept_state", ev);
            break;
        }
        case 1: {   // CLOSE
            // Sphere sends CLOSE for both a completed trade and a cancelled
            // one. "Both boxes were ticked when it closed" is the only thing
            // that distinguishes them from here.
            const bool completed = trade_.MyCheck() && trade_.TheirCheck();
            trade_.OnClosed(completed ? trade::CloseReason::BothAccepted
                                      : trade::CloseReason::PartnerCancelled,
                            NowMs());
            LogInfo("[trade] window closed (%s)\n",
                    trade::CloseReasonName(trade_.Reason()));
            LogEvent("trade_close", trade::CloseReasonName(trade_.Reason()));
            break;
        }
        default:
            break;
    }
}

void Client::TradeNoteItemAdded(u32 container, u32 item) {
    if (!trade_.Active()) return;
    trade_.OnItemAdded(container, item);
    if (container == trade_.MyContainer() || container == trade_.TheirContainer())
        LogInfo("[trade] 0x%08X is now in the %s window (mine=%zu theirs=%zu)\n",
                item, container == trade_.MyContainer() ? "our" : "their",
                trade_.MyOffer().size(), trade_.TheirOffer().size());
}

void Client::TradeNoteItemRemoved(u32 item) {
    if (!trade_.Active()) return;
    const int before = trade_.AcceptResets();
    trade_.OnItemRemoved(item);
    if (trade_.AcceptResets() != before)
        LogInfo("[trade] contents changed after we accepted; acceptance "
                "retracted locally\n");
}

} // namespace uo
