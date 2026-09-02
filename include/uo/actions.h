#pragma once

#include "uo/types.h"

#include <cctype>
#include <cstring>

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
    UseItemOn,      // double-click an item, then target an item or character
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
        case Kind::UseItemOn:     return "use_item_on";
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

// EQUIPPING WHAT IS ALREADY WORN TAKES IT OFF.
//
// Source-X answers a 0x13 wear for an item that is already on the character by
// bouncing it: the lift succeeds, the wear finds the layer occupied by that
// very item and the server drops it into the pack -- "You put the fishing pole
// in your pack." Wave 2026-09-02 shows the whole shape (run_gates/
// g_Ithion.console.txt:521-533): equip -> pole in pack -> drag_cancel -> the
// runner sees an empty hand and equips again, 643 times in thirty minutes.
//
// `wornLayer` is the layer the serial is CURRENTLY on, or -1 when it is not
// worn at all. Layer 0 means "server chooses", so any layer satisfies it.
inline bool EquipWouldBeNoOp(int wornLayer, u8 requestedLayer) {
    if (wornLayer < 0) return false;
    return requestedLayer == 0 || static_cast<u8>(wornLayer) == requestedLayer;
}

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

// --- system-message classification ------------------------------------------
// Pulled out of Client::ActionOnSysMessage as pure text matching so the
// classification itself can be unit tested without a live Client/socket.
// Sphere's own wording (core/messages.scp) is quoted in each caller.
inline bool ContainsCI(const char* text, const char* needle) {
    if (!text || !needle || !*needle) return false;
    const usize n = std::strlen(needle);
    for (const char* p = text; *p; ++p) {
        usize i = 0;
        while (i < n && p[i] &&
               std::tolower(static_cast<unsigned char>(p[i])) ==
               std::tolower(static_cast<unsigned char>(needle[i]))) ++i;
        if (i == n) return true;
    }
    return false;
}

// "You lack sufficient mana for this spell" (spell_try_nomana) and
// "You lack %s for this spell" (spell_try_noregs, %s = the reagent name) are
// Sphere's own refusal text for an unaffordable cast. Neither the literal
// reagent name nor "sufficient mana" is known in advance, so this matches on
// "you lack" rather than a fixed reagent list -- a cast refused this way
// otherwise never recognises its own refusal and sits pending until the cast
// deadline (wave2 2026-09-01: Illyria cast_spell x26, Selene x6, Leander/Lyra
// x2 each; run_gates/g_Illyria.console.txt:86, g_Selene.console.txt:91).
inline bool IsSpellCastRefusal(const char* text) {
    return ContainsCI(text, "more reagents") || ContainsCI(text, "not enough mana") ||
           ContainsCI(text, "lack the mana") || ContainsCI(text, "you lack") ||
           ContainsCI(text, "fizzle");
}

// "You are selling too fast." / "You are buying too fast."
// (npc_vendor_sellfast/buyfast) is Sphere's vendor-trade rate limit, sent
// instantly and otherwise unrecognised -- the action then sat pending for
// the full vendor deadline every time the rate limit was hit (wave2
// 2026-09-01: Dorvar vendor_sell x5; run_gates/g_Dorvar.console.txt:455).
inline bool IsVendorRateLimited(const char* text) {
    return ContainsCI(text, "selling too fast") || ContainsCI(text, "buying too fast");
}

// --- eating -----------------------------------------------------------------
// A double-click on food is answered by TEXT AND NOTHING ELSE. CChar::Use_Eat
// (Source-X src/game/chars/CCharUse.cpp:929-993) either refuses with one of
// four messages or eats and then sends exactly one of six food_full_N lines;
// no gump, no cursor, and -- for a stack that merely shrinks by one -- no
// 0x1D delete either. None of that was recognised, so every single eat sat
// pending for the whole use_object deadline and the goal re-issued it forever
// (2026-09-02 wave: run_gates/g_Halain.console.txt:139-150 -- "You eat the
// food, and begin to feel more satiated." at 09:09:25.563, use_object timeout
// at 09:09:29.571, and 351 `food: eating` lines in one session).
enum class EatOutcome : u8 {
    None = 0,     // not an eat message at all
    Ate,          // food_full_1..6: the food was consumed
    AlreadyFull,  // food_canteatf: the stomach is full, nothing was eaten
    CannotEat,    // food_cantmove / food_canteat / food_rcanteat
};

inline EatOutcome ClassifyEatMessage(const char* text) {
    if (!text || !*text) return EatOutcome::None;
    // The six food_full_N lines, in Sphere's own wording (defmessages, quoted
    // in runtime/scripts/core/messages.scp:169-175).
    if (ContainsCI(text, "still extremely hungry") ||     // food_full_1
        ContainsCI(text, "much less hungry") ||           // food_full_2
        ContainsCI(text, "more satiated") ||              // food_full_3
        ContainsCI(text, "nearly stuffed") ||             // food_full_4
        ContainsCI(text, "quite full") ||                 // food_full_5
        ContainsCI(text, "you are stuffed"))              // food_full_6
        return EatOutcome::Ate;
    if (ContainsCI(text, "too full to eat"))              // food_canteatf
        return EatOutcome::AlreadyFull;
    if (ContainsCI(text, "not capable of eating") ||      // food_canteat
        ContainsCI(text, "can't really eat this") ||      // food_rcanteat
        ContainsCI(text, "cannot really eat this"))
        return EatOutcome::CannotEat;
    return EatOutcome::None;
}

// --- carving a corpse -------------------------------------------------------
// CChar::Use_CarveCorpse (Source-X src/game/chars/CCharUse.cpp:49-170) puts the
// output INTO the corpse and confirms in words only (carve_corpse_* in
// runtime/scripts/core/messages.scp). A carved corpse sends one line per part
// (a sheep: wool, then meat); an already-carved or empty one sends
// carve_corpse_nothing. Nothing else arrives unless the corpse is open, so
// use_item_on with a blade on a corpse otherwise sat pending for its whole
// deadline (2026-09-03 smoke: Halain, "the corpse gave up no wool in 15s" x4
// while the server had said "the wool is now on the corpse").
enum class CarveOutcome : u8 {
    None = 0,   // not a carve message
    Carved,     // one of the parts landed on the corpse
    Nothing,    // carve_corpse_nothing: already carved / no parts
};

inline CarveOutcome ClassifyCarveMessage(const char* text) {
    if (!text || !*text) return CarveOutcome::None;
    if (ContainsCI(text, "wool is now on the corpse") ||       // carve_corpse_wool
        ContainsCI(text, "remains on the corpse") ||           // carve_corpse_meat
        ContainsCI(text, "hides are now in the corpse") ||     // carve_corpse_hides
        ContainsCI(text, "feathers are now on the corpse"))    // carve_corpse_feathers
        return CarveOutcome::Carved;
    if (ContainsCI(text, "nothing useful to carve"))           // carve_corpse_nothing
        return CarveOutcome::Nothing;
    return CarveOutcome::None;
}

// --- hunger, as the server states it ----------------------------------------
// Sphere states a character's hunger in two places and BOTH are statements of
// the same underlying STAT_FOOD:
//
//   * the status line, "You are <level>" (msg_food_lvl_self), whose level is
//     sm_szFoodLevel[food * 8 / foodMax] -- eight bands, 0 starving .. 7
//     stuffed (CCharStatus.cpp:837,870-885);
//   * the eat outcome, food_full_N, chosen by food * 5 / foodMax -- six bands
//     (CCharUse.cpp:967-991).
//
// `level` below is the eight-band status index, so both vocabularies can be
// compared on one scale. The eat lines cover a range of status bands, so each
// is mapped to the HUNGRIEST band it can mean, which never claims a character
// is fuller than it is:
//
//   food_full_1  [0,20%)   -> very hungry (1)     food_full_4  [60,80%)  -> 4
//   food_full_2  [20,40%)  -> hungry (2)          food_full_5  [80,100%) -> 6
//   food_full_3  [40,60%)  -> fairly content (3)  food_full_6  100%      -> 7
//
// This table exists because hunger is a state that CHANGES, and the only
// honest reading is the NEWEST statement -- which the caller can only pick by
// looking each phrase up by time. Most specific phrase first: "you are very
// hungry" must win over "you are hungry" on the same line.
struct HungerStatement {
    const char* text;
    u8          level;   // 0 starving .. 7 stuffed
};

inline const HungerStatement* HungerStatements(usize* count) {
    static const HungerStatement kRows[] = {
        // status line (msg_food_lvl_1..8, messages.scp:470-477)
        {"you are starving",        0},
        {"you are very hungry",     1},
        {"you are hungry",          2},
        {"you are fairly content",  3},
        {"you are content",         4},
        {"you are well fed",        6},
        {"you are fed",             5},
        {"you are stuffed",         7},
        // eat outcome (food_full_1..6, messages.scp:169-175)
        {"still extremely hungry",  1},
        {"much less hungry",        2},
        {"more satiated",           3},
        {"nearly stuffed",          4},
        {"quite full",              6},
        // refusal: full is full
        {"too full to eat",         7},
    };
    if (count) *count = sizeof(kRows) / sizeof(kRows[0]);
    return kRows;
}

// The status bands the shard itself calls hungry: 0 starving, 1 very hungry,
// 2 hungry. 3 is "fairly content" and above it nothing is hungry.
constexpr u8 kHungerLevelHungry   = 2;
constexpr u8 kHungerLevelStarving = 0;

}  // namespace uo::act
