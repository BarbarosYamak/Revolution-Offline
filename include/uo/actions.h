#pragma once

#include "uo/types.h"

// ---------------------------------------------------------------------------
// M2 action layer — result model and the pure state machines behind the
// player action primitives.
//
// The split matters: everything in this header is protocol-free and
// session-owned, so it can be unit tested against the exact code the client
// runs. Packet construction and parsing stay in Client/Builders underneath.
//
// Nothing here is static or global. Every type is meant to be a member of a
// Client, so two sessions in one process cannot share targeting, action or
// drag state (the M1.5 rule).
// ---------------------------------------------------------------------------

namespace uo::act {

// --- result model ----------------------------------------------------------
// The point of this enum is to let a scenario tell "the packet went out" from
// "the server confirmed it". Anything that is not Success is a failure the
// caller can branch on.
enum class Result : u8 {
    Pending = 0,     // started, still waiting on the server
    Success,         // server-confirmed
    Timeout,         // no confirming packet inside the deadline
    Rejected,        // the server explicitly refused (e.g. 0x27 drag cancel)
    InvalidState,    // the client could not even start (no cursor, no item)
    Unavailable,     // the world does not offer it (no vendor, no bank, ...)
    ServerFailure,   // the server answered, but with an error/failure result
};

inline const char* ResultName(Result r) {
    switch (r) {
        case Result::Pending:       return "pending";
        case Result::Success:       return "success";
        case Result::Timeout:       return "timeout";
        case Result::Rejected:      return "rejected";
        case Result::InvalidState:  return "invalid_state";
        case Result::Unavailable:   return "unavailable";
        case Result::ServerFailure: return "server_failure";
    }
    return "?";
}

inline bool Finished(Result r) { return r != Result::Pending; }

// --- what the player asked for --------------------------------------------
enum class Kind : u8 {
    None = 0,
    UseObject,      // double-click anything
    OpenContainer,  // double-click expecting container contents
    MoveItem,       // lift + drop into a container
    DropGround,     // lift + drop at a world position
    Equip,          // lift + wear on a layer
    Unequip,        // lift off a layer + drop into the backpack
    UseSkill,
    CastSpell,
    Attack,
    OpenBank,       // speech to a banker, expecting a container
    VendorBuy,
    VendorSell,
    Bandage,        // use a bandage, then target a character
    Resurrect,
    TradeOpen,      // offer an item to a player, expecting a trade window
    NpcTrain,       // ask an NPC to teach a skill, expecting its price
};

inline const char* KindName(Kind k) {
    switch (k) {
        case Kind::None:          return "none";
        case Kind::UseObject:     return "use_object";
        case Kind::OpenContainer: return "open_container";
        case Kind::MoveItem:      return "move_item";
        case Kind::DropGround:    return "drop_ground";
        case Kind::Equip:         return "equip";
        case Kind::Unequip:       return "unequip";
        case Kind::UseSkill:      return "use_skill";
        case Kind::CastSpell:     return "cast_spell";
        case Kind::Attack:        return "attack";
        case Kind::NpcTrain:      return "npc_train";
        case Kind::OpenBank:      return "open_bank";
        case Kind::VendorBuy:     return "vendor_buy";
        case Kind::VendorSell:    return "vendor_sell";
        case Kind::Bandage:       return "bandage";
        case Kind::Resurrect:     return "resurrect";
        case Kind::TradeOpen:     return "trade_open";
    }
    return "?";
}

// --- one in-flight action --------------------------------------------------
// A player does one deliberate thing at a time, so a single slot is enough for
// M2. Movement is deliberately NOT an action: it keeps its own controller
// (SubmitStep) so a bot can walk while an action is outstanding.
struct Action {
    Kind   kind = Kind::None;
    Result result = Result::Pending;

    u32 subject = 0;        // item or mobile the action is about
    u32 destination = 0;    // destination container / target serial
    u16 amount = 0;         // stack quantity where relevant
    u8  layer = 0;          // equipment layer where relevant
    int id = 0;             // spell id / skill id
    i32 x = 0, y = 0;       // ground destination where relevant
    i8  z = 0;

    i64 startedMs = 0;
    i64 deadlineMs = 0;
    // Set when the action armed a target cursor, so the reply can be matched
    // to the cursor this action caused rather than a stale one.
    u32 targetGeneration = 0;
    bool awaitingTarget = false;

    bool Active() const { return kind != Kind::None && result == Result::Pending; }

    void Begin(Kind k, i64 nowMs, i64 timeoutMs) {
        *this = Action{};
        kind = k;
        result = Result::Pending;
        startedMs = nowMs;
        deadlineMs = nowMs + timeoutMs;
    }

    // Returns true when this call is what finished the action.
    bool Finish(Result r) {
        if (kind == Kind::None || result != Result::Pending) return false;
        result = r;
        return true;
    }

    // Deadline check; call once per tick.
    bool ExpireIfDue(i64 nowMs) {
        if (!Active()) return false;
        if (nowMs < deadlineMs) return false;
        result = Result::Timeout;
        return true;
    }
};

// --- target cursor ---------------------------------------------------------
// Per-session cursor state with a generation counter. The generation is what
// makes a stale reply impossible: an action records the generation of the
// cursor it caused, and a reply carrying a different generation is refused
// instead of answering someone else's cursor.
class TargetState {
public:
    struct Cursor {
        u32 id = 0;         // cursor id the server sent; must be echoed back
        u8  type = 0;       // 0 = object, 1 = ground
        u8  subtype = 0;    // 0 neutral, 1 harmful, 2 beneficial
        u32 generation = 0; // bumped on every new cursor
        i64 armedMs = 0;
    };

    // A 0x6C arrived. A cursor that replaces an un-answered one invalidates
    // the old generation, which is exactly the "new target" case.
    void OnArmed(u32 id, u8 type, u8 subtype, i64 nowMs) {
        ++generation_;
        cur_.id = id;
        cur_.type = type;
        cur_.subtype = subtype;
        cur_.generation = generation_;
        cur_.armedMs = nowMs;
        active_ = true;
    }

    bool Active() const { return active_; }
    const Cursor& Current() const { return cur_; }
    u32 Generation() const { return generation_; }

    // May this reply go out? Only for the live cursor, and only once.
    // generation 0 means "whatever is current" (manual/console use).
    bool CanReply(u32 generation) const {
        if (!active_) return false;
        return generation == 0 || generation == cur_.generation;
    }

    // Consume the cursor after a reply has been sent.
    void OnReplied() { active_ = false; }

    // Local cancel (we sent a cancel reply) or the server dropped it.
    void OnCancelled() { active_ = false; }

    // True when `generation` refers to a cursor that has been superseded.
    bool IsStale(u32 generation) const {
        return generation != 0 && generation != cur_.generation;
    }

private:
    Cursor cur_{};
    u32  generation_ = 0;
    bool active_ = false;
};

// --- drag/drop transaction -------------------------------------------------
// A UO item move is two packets (0x07 lift, 0x08 drop) and the server may
// refuse either half. Tracking it explicitly is what keeps client state from
// drifting when a move is rejected: nothing is assumed moved until the server
// says so.
class DragState {
public:
    enum class Phase : u8 { Idle, Lifted, Dropped };

    void BeginLift(u32 serial, u16 amount, i64 nowMs) {
        serial_ = serial;
        amount_ = amount;
        phase_ = Phase::Lifted;
        startedMs_ = nowMs;
    }
    void OnDropSent(u32 destination) {
        destination_ = destination;
        phase_ = Phase::Dropped;
    }
    void Reset() { *this = DragState{}; }

    Phase phase() const { return phase_; }
    bool  InFlight() const { return phase_ != Phase::Idle; }
    u32   Serial() const { return serial_; }
    u16   Amount() const { return amount_; }
    u32   Destination() const { return destination_; }
    i64   StartedMs() const { return startedMs_; }

private:
    u32   serial_ = 0;
    u16   amount_ = 0;
    u32   destination_ = 0;
    Phase phase_ = Phase::Idle;
    i64   startedMs_ = 0;
};

// --- life state ------------------------------------------------------------
// Ghost bodies per the classic client: 0x192 male, 0x193 female.
inline bool IsGhostBody(u16 body) { return body == 0x0192 || body == 0x0193; }

enum class LifeState : u8 { Alive, Dead };

inline const char* LifeStateName(LifeState s) {
    return s == LifeState::Dead ? "dead" : "alive";
}

// Derive life state from the body the server gave us. The server is the only
// authority here; the client never sets this on its own.
inline LifeState LifeStateFromBody(u16 body) {
    return IsGhostBody(body) ? LifeState::Dead : LifeState::Alive;
}

// Vendor rows are Client::VendorItem (serial, graphic, amount, price, layer,
// name) -- the client already assembles them from the 0x3C + 0x74 pair, so
// there is deliberately no second vendor type here.

}  // namespace uo::act
