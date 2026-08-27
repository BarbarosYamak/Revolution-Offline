#include "uo/trade.h"

#include <algorithm>

namespace uo::trade {

namespace {

const char* const kPhaseNames[] = {
    "none", "open", "accepted", "completed", "cancelled",
};
static_assert(sizeof(kPhaseNames) / sizeof(kPhaseNames[0]) ==
                  static_cast<usize>(Phase::Count),
              "kPhaseNames is out of step with Phase");

const char* const kCloseReasonNames[] = {
    "none", "both_accepted", "we_cancelled", "partner_cancelled",
    "partner_gone",
};
static_assert(sizeof(kCloseReasonNames) / sizeof(kCloseReasonNames[0]) ==
                  static_cast<usize>(CloseReason::Count),
              "kCloseReasonNames is out of step with CloseReason");

void Remove(std::vector<u32>& v, u32 item) {
    v.erase(std::remove(v.begin(), v.end(), item), v.end());
}

bool Holds(const std::vector<u32>& v, u32 item) {
    return std::find(v.begin(), v.end(), item) != v.end();
}

} // namespace

const char* PhaseName(Phase p) {
    const usize i = static_cast<usize>(p);
    return i < static_cast<usize>(Phase::Count) ? kPhaseNames[i] : "?";
}

const char* CloseReasonName(CloseReason r) {
    const usize i = static_cast<usize>(r);
    return i < static_cast<usize>(CloseReason::Count) ? kCloseReasonNames[i]
                                                      : "?";
}

void TradeState::Reset() {
    phase_ = Phase::None;
    reason_ = CloseReason::None;
    partnerSerial_ = 0;
    partnerName_.clear();
    myContainer_ = 0;
    theirContainer_ = 0;
    myCheck_ = false;
    theirCheck_ = false;
    checkSent_ = false;
    acceptResets_ = 0;
    bothAccepted_ = false;
    openedMs_ = 0;
    myOffer_.clear();
    theirOffer_.clear();
}

void TradeState::OnOpened(u32 partnerSerial, const char* partnerName,
                          u32 myContainer, u32 theirContainer, i64 nowMs) {
    Reset();
    phase_ = Phase::Open;
    partnerSerial_ = partnerSerial;
    partnerName_ = partnerName ? partnerName : "";
    myContainer_ = myContainer;
    theirContainer_ = theirContainer;
    openedMs_ = nowMs;
}

void TradeState::OnCheckChanged(bool mine, bool theirs, i64 nowMs) {
    (void)nowMs;
    if (phase_ != Phase::Open && phase_ != Phase::Accepted) return;

    myCheck_ = mine;
    theirCheck_ = theirs;
    checkSent_ = false;   // the server has answered; a new request is allowed
    phase_ = mine ? Phase::Accepted : Phase::Open;

    // Both checks set means the server is completing the trade; the CLOSE
    // that follows carries the outcome, so nothing is concluded here -- but
    // it is latched, because from this instant the goods start moving and the
    // checks get cleared, and by the time the CLOSE arrives there would be
    // nothing left to tell a completed trade from an abandoned one.
    if (mine && theirs) bothAccepted_ = true;
}

void TradeState::OnClosed(CloseReason reason, i64 nowMs) {
    (void)nowMs;
    if (phase_ == Phase::None) return;
    phase_ = (reason == CloseReason::BothAccepted) ? Phase::Completed
                                                   : Phase::Cancelled;
    reason_ = reason;
    myCheck_ = false;
    theirCheck_ = false;
    checkSent_ = false;
}

void TradeState::OnItemAdded(u32 container, u32 item) {
    if (!Active() || !item) return;
    if (container == myContainer_) {
        if (!Holds(myOffer_, item)) {
            myOffer_.push_back(item);
            ContentsChanged();
        }
    } else if (container == theirContainer_) {
        if (!Holds(theirOffer_, item)) {
            theirOffer_.push_back(item);
            ContentsChanged();
        }
    }
}

void TradeState::OnItemRemoved(u32 item) {
    if (!Active() || !item) return;
    const bool wasMine = Holds(myOffer_, item);
    const bool wasTheirs = Holds(theirOffer_, item);
    if (!wasMine && !wasTheirs) return;
    Remove(myOffer_, item);
    Remove(theirOffer_, item);
    ContentsChanged();
}

void TradeState::ContentsChanged() {
    // The client's own caution: what we agreed to is no longer what is on the
    // table, so our acceptance should not stand. Sphere does not enforce this
    // -- Trade_Status only clears checks when someone un-checks -- so this is
    // a local retraction that the caller must actually send.
    //
    // Once both boxes are ticked this must not fire: the contents are changing
    // because the server is completing the trade, and retracting there both
    // invents a safety event that never happened and destroys the evidence
    // that the trade succeeded. Found live in m3_trade4, where a completed
    // sale was reported as `partner_cancelled`.
    if (bothAccepted_) return;
    if (phase_ == Phase::Accepted || myCheck_) {
        ++acceptResets_;
        myCheck_ = false;
        checkSent_ = false;
        phase_ = Phase::Open;
    }
}

void TradeState::NoteCheckSent(bool accepted, i64 nowMs) {
    (void)accepted;
    (void)nowMs;
    if (!Active()) return;
    checkSent_ = true;
}

bool TradeState::Offering(u32 item) const {
    return Holds(myOffer_, item) || Holds(theirOffer_, item);
}

} // namespace uo::trade
