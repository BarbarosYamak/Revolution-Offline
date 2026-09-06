// The need/handler contract. See include/uo/needgate.h and
// docs/NEED_HANDLER_CONTRACT.md.
//
// Every rule below was ALREADY being applied, by a handler, on the same
// observation. This file is where they were moved to so the need model applies
// the identical rule at scoring time instead of scoring high and being refused
// a tick later. Nothing here is new knowledge and nothing here may read state
// the character cannot observe.
#include "uo/needgate.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "uo/activities/recovery.h"
#include "uo/market.h"

namespace uo::life {

namespace {

// The wording of the block record. Parsed back by NeedBlockActive, so the two
// live next to each other rather than in two files.
constexpr const char* kBlockEventKind = "need_blocked";

// Everything after "reason=" in the detail. Returns nullptr when the row is
// not a block record for this need/session.
const char* BlockReason(const LifeEvent& e, NeedKind kind,
                        const NeedConfig& cfg, const Observation& obs) {
    if (e.kind != kBlockEventKind) return nullptr;
    const std::string& d = e.detail;
    // need=<Name>
    const std::string want = std::string("need=") + NeedKindName(kind) + " ";
    if (d.compare(0, want.size(), want) != 0) return nullptr;
    // session=<N>
    const std::string::size_type sPos = d.find("session=");
    if (sPos == std::string::npos) return nullptr;
    int session = -1;
    if (std::sscanf(d.c_str() + sPos, "session=%d", &session) != 1) return nullptr;
    if (session != cfg.sessionIndex) return nullptr;
    // scope=<session|window>
    const std::string::size_type scPos = d.find("scope=");
    if (scPos == std::string::npos) return nullptr;
    const bool windowed = d.compare(scPos, 12, "scope=window") == 0;
    if (windowed) {
        // A per-process clock, which is all a window has. A record from a
        // previous process cannot be aged, so it is simply not honoured --
        // the same rule the drained-counter readers in Needs.cpp already use.
        if (e.atMs > obs.nowMs) return nullptr;
        if (obs.nowMs - e.atMs >= kBlockWindowMs) return nullptr;
    }
    const std::string::size_type rPos = d.find("reason=");
    if (rPos == std::string::npos) return nullptr;
    return d.c_str() + rPos + 7;
}

}  // namespace

void NoteNeedBlocked(Memory& mem, NeedKind kind, const char* reason,
                     BlockScope scope, const NeedConfig& cfg, i64 nowMs) {
    if (!reason || !reason[0]) return;
    char detail[512];
    std::snprintf(detail, sizeof(detail), "need=%s session=%d scope=%s reason=%s",
                  NeedKindName(kind), cfg.sessionIndex,
                  scope == BlockScope::Session ? "session" : "window", reason);
    // ONE ROW PER (need, scope, reason, session). Memory::NoteEvent is a ring
    // and does NOT collapse repeats, and a handler that refuses on every tick
    // would otherwise push the whole of this character's history off the front
    // of it within a minute.
    // A window that has since EXPIRED must be writable again, or a shelf that
    // refilled and drained twice would be recorded once and read forever.
    for (const LifeEvent& e : mem.Events()) {
        if (e.kind != kBlockEventKind || e.detail != detail) continue;
        if (scope == BlockScope::Session) return;
        if (nowMs >= e.atMs && nowMs - e.atMs < kBlockWindowMs) return;
    }
    mem.NoteEvent(kBlockEventKind, detail, "", 0, 0, nowMs);
}

bool NeedBlockActive(const Memory& mem, NeedKind kind, const NeedConfig& cfg,
                     const Observation& obs, std::string* why) {
    const char* found = nullptr;
    for (const LifeEvent& e : mem.Events()) {
        if (const char* r = BlockReason(e, kind, cfg, obs)) found = r;
    }
    if (!found) return false;
    // A BLOCK IS ONLY GOOD WHILE THE FACT BEHIND IT IS, and this belongs here
    // rather than in CanAct because AssessNeeds reads blocks through THIS
    // function -- a staleness rule kept one level up is a rule the need site
    // never runs. HEAL's block says "nothing to heal with and nothing on the
    // way"; a bandage or a potion in the pack ends that sentence, and a
    // character must never sit out a stand-down with the medicine in hand.
    // The other blocked kinds have no such cheap tell -- a pasture does not
    // appear in the pack -- so they run their scope out, which is the point
    // of them.
    if (kind == NeedKind::Heal && (obs.bandages > 0 || obs.healPotions > 0))
        return false;
    if (why) *why = found;
    return true;
}

GateVerdict CanAct(NeedKind kind, const GateSubject& subject, const Memory& mem,
                   const Observation& obs, const NeedConfig& cfg) {
    GateVerdict v;

    // ARM B FIRST. An observed refusal is a fact; the rules below are
    // inference from the same observation the handler had.
    // (NeedBlockActive drops a block whose fact has since been undone, so a
    // handler asking here gets the same answer AssessNeeds got.)
    if (NeedBlockActive(mem, kind, cfg, obs, &v.why)) {
        v.ok = false;
        return v;
    }

    switch (kind) {
        // --- BUY_SUPPLIES (src/life/runner/Economy.cpp:2225-2250) ----------
        //
        // The handler asks SupplierTradeFor, then market::RouteForInput, and
        // hands the errand away or fails on three of the four answers. Odessa
        // scored this need 133 and was handed off five times in one session
        // for i_ingot_iron, ending in goal_spinning=BUY_SUPPLIES (D7).
        case NeedKind::NeedSupplies: {
            if (!cfg.profession || !subject.item || !subject.item[0]) break;
            const market::SupplyRoute route = market::RouteForInput(
                *cfg.profession, subject.item,
                SupplierTradeFor(subject.item) != nullptr);
            if (route == market::SupplyRoute::PlayerMarket) {
                v.ok = false;
                v.why = "short of an input no shopkeeper sells -- another "
                        "profession makes it, so this is a rendezvous with a "
                        "player, not a shopping trip";
            } else if (route == market::SupplyRoute::SelfProduce) {
                v.ok = false;
                v.why = "short of an input this life produces itself -- the "
                        "answer is to go and make it, not to shop for it";
            } else if (route == market::SupplyRoute::NoKnownSource) {
                v.ok = false;
                v.why = "no trade known to sell it";
            }
            break;
        }

        // --- TRAIN_COMBAT readiness (src/life/runner/Train.cpp:82-168) -----
        //
        // The gates the handler runs before it will open a fight, in its own
        // order. Each one hands off to a goal that scores on its own need
        // (Heal, Bank, ReplaceEquipment), so refusing here loses nothing and
        // saves the pick, the walk and the walk back (D1/D5).
        case NeedKind::NeedTraining: {
            if (!subject.hunting) break;
            if (obs.huntReturnPending) {
                v.ok = false;
                v.why = "secure the last hunt's loot before starting another";
                break;
            }
            // AMMUNITION: the handler's line is twenty, not one. A need that
            // passed at 19 arrows scored the hunt and was handed straight to
            // Bank or Craft.
            if (cfg.profession &&
                cfg.profession->combatStrategy == CombatStrategyId::Ranged &&
                market::QtyOf(obs.pack, "i_arrow") < 20) {
                v.ok = false;
                v.why = "fewer than twenty arrows -- restock ammunition before "
                        "hunting";
                break;
            }
            // HEALTH, with the one escape the need model already carries and
            // the handler did not: hungry, broke and out of bandages, resting
            // fixes nothing and hunting is the only door out (the Kaelen
            // deadlock, Needs.cpp). Both sides now read the same rule, so the
            // escape actually opens.
            const bool outOfOptions =
                obs.bandages <= 0 && obs.gold < cfg.goldFloor && obs.hungry;
            if (!outOfOptions && obs.HpFraction() < cfg.healHpFraction) {
                v.ok = false;
                v.why = "recover before opening another fight";
                break;
            }
            if (outOfOptions && obs.hp * 100 < obs.hpMax * 50) {
                v.ok = false;
                v.why = "recover before opening another fight";
                break;
            }
            if (obs.WeightFraction() >= BankWeightLine(cfg)) {
                v.ok = false;
                v.why = "make room for loot before fighting";
                break;
            }
            break;
        }

        // --- RECOVER_CORPSE (src/life/runner/Survive.cpp:1005-1012) --------
        //
        // DecideRecovery's Recover step: too hurt to walk back, so heal
        // first. The need scored 0.75 x 950 = 712 and the handler handed the
        // turn away three seconds later, every time, because the need could
        // not see the return line. Only while ALIVE -- a ghost's business
        // with this goal is resurrection, which the handler still owns.
        case NeedKind::RecoverCorpse: {
            if (obs.dead) break;
            // DecideRecovery's ORDER, not a re-derivation of its rules.
            // `threatened` is its second test and outranks everything below.
            if (obs.underAttack || obs.attackersOnMe > 0) {
                v.ok = false;
                v.why = "under attack -- the corpse waits until this fight is "
                        "over";
                break;
            }
            // ABANDONMENT COMES BEFORE THE HEALTH LINE, in DecideRecovery and
            // therefore here. A character raised at a tenth of its health that
            // has already spent its trips does NOT get told to heal first: its
            // next step is Abandon, and Abandon is what clears the death
            // record. Block the need at that point and the record is never
            // cleared, so the corpse is carried forever and the handler's own
            // exit is unreachable (bot-brain: an-errands-exit-cannot-live-in-
            // the-errand). The distance clause is DecideRecovery's too --
            // standing on the corpse, the errand finishes rather than quits.
            const RecoveryTuning tune;
            const i32 corpseDist =
                std::max(std::abs(obs.corpseX - obs.x), std::abs(obs.corpseY - obs.y));
            if (obs.corpseRecoveryAttempts >= tune.maxAttempts && corpseDist > 2)
                break;
            if (obs.HpFraction() < cfg.healHpFraction) {
                v.ok = false;
                v.why = "too hurt to walk back to the corpse -- heal first";
                break;
            }
            break;
        }

        default:
            break;
    }
    return v;
}

}  // namespace uo::life
