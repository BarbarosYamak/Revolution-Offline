#include "uo/interaction/handshake.h"

namespace uo::life {

const char* HandshakeStateName(HandshakeState s) {
    switch (s) {
        case HandshakeState::Idle:             return "idle";
        case HandshakeState::ActionIssued:     return "issued";
        case HandshakeState::WaitingForServer: return "waiting";
        case HandshakeState::ConfirmedSuccess: return "confirmed";
        case HandshakeState::ConfirmedFailure: return "refused";
        case HandshakeState::TimedOut:         return "timed out";
        case HandshakeState::Backoff:          return "backing off";
    }
    return "?";
}

void Handshake::Reset() {
    state_ = HandshakeState::Idle;
    issuedAtMs_ = 0;
    readyAtMs_ = 0;
    attempts_ = 0;
    refusal_ = "";
}

bool Handshake::MayIssue(i64 nowMs, const char** whyNot) const {
    auto no = [whyNot](const char* why) {
        if (whyNot) *whyNot = why;
        return false;
    };

    // THE RULE. An attempt inside its own deadline is not finished, and a
    // second ask would cancel it rather than reinforce it.
    if (state_ == HandshakeState::ActionIssued ||
        state_ == HandshakeState::WaitingForServer) {
        if (nowMs < issuedAtMs_ + policy_.MinimumGapMs())
            return no("an attempt is still inside its own deadline");
        // Past the deadline and still nothing: the caller ought to have
        // called NoteExpiry, but permitting the issue here would restart the
        // same race. Make it say so instead.
        return no("the last attempt expired and has not been closed out");
    }

    if (state_ == HandshakeState::Backoff && nowMs < readyAtMs_)
        return no("resting after a failure");

    if (attempts_ >= policy_.maxAttempts)
        return no("every attempt has been spent");

    // A definitive refusal is not something to try again at the same door.
    // The caller may Reset() and go elsewhere -- that is a decision about the
    // errand, and it belongs to the errand.
    if (state_ == HandshakeState::ConfirmedFailure)
        return no("the server already answered no");

    if (whyNot) *whyNot = "";
    return true;
}

void Handshake::NoteIssued(i64 nowMs) {
    issuedAtMs_ = nowMs;
    state_ = HandshakeState::ActionIssued;
    ++attempts_;
    refusal_ = "";
}

void Handshake::Note(Outcome outcome, i64 nowMs, const char* detail) {
    switch (outcome) {
        case Outcome::Pending:
            // Only meaningful while something is out there. Reported at any
            // other time it is noise, and treating it as a transition is how
            // a state machine drifts.
            if (state_ == HandshakeState::ActionIssued)
                state_ = HandshakeState::WaitingForServer;
            return;

        case Outcome::Succeeded:
            state_ = HandshakeState::ConfirmedSuccess;
            refusal_ = "";
            return;

        case Outcome::Refused:
            // AN ANSWER, AND THE MOST VALUABLE KIND. It ends the wait now
            // instead of at the deadline, and it carries a reason the goal
            // can act on -- walk closer, ask someone else, stand down.
            state_ = HandshakeState::ConfirmedFailure;
            refusal_ = (detail && detail[0]) ? detail : "the server said no";
            readyAtMs_ = nowMs + policy_.backoffMs;
            return;
    }
}

bool Handshake::Expired(i64 nowMs) const {
    if (state_ != HandshakeState::ActionIssued &&
        state_ != HandshakeState::WaitingForServer)
        return false;
    return nowMs >= issuedAtMs_ + policy_.actionDeadlineMs;
}

void Handshake::NoteExpiry(i64 nowMs) {
    if (!Expired(nowMs)) return;
    // A lost packet is not a refusal: nothing was learned about the world,
    // only about the connection. Backoff, and let the attempt counter decide
    // when to give up.
    state_ = HandshakeState::Backoff;
    readyAtMs_ = nowMs + policy_.backoffMs;
    refusal_ = "";
}

}  // namespace uo::life
