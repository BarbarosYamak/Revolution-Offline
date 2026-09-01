#include "uo/activities/recovery.h"

// The arithmetic of getting your things back, in its own translation unit so
// ctest reaches it without Client.

namespace uo::life {

const char* RecoveryStepName(RecoveryStep s) {
    switch (s) {
        case RecoveryStep::SeekResurrection: return "seek resurrection";
        case RecoveryStep::Recover:          return "recover first";
        case RecoveryStep::TravelToCorpse:   return "travel to the corpse";
        case RecoveryStep::Loot:             return "loot";
        case RecoveryStep::ReEquip:          return "re-equip";
        case RecoveryStep::Abandon:          return "abandon";
        case RecoveryStep::Done:             return "done";
    }
    return "?";
}

RecoveryPlan DecideRecovery(const RecoverySight& see,
                            const RecoveryTuning& tune) {
    RecoveryPlan out;

    // A GHOST CAN DO NOTHING ELSE. Not travel, not loot, not decide.
    if (see.dead) {
        out.step = RecoveryStep::SeekResurrection;
        out.reason = "a ghost, and nothing else is possible until that ends";
        return out;
    }

    // GEAR IN THE PACK BELONGS ON THE PAPERDOLL, and this comes before going
    // back for anything else: a character that loots its armour and walks off
    // still wearing nothing has recovered its things and not its safety.
    if (see.gearInPack) {
        out.step = RecoveryStep::ReEquip;
        out.reason = "the gear is back in the pack but not yet worn";
        return out;
    }

    if (!see.corpseKnown) {
        out.step = RecoveryStep::Done;
        out.reason = "nowhere to go back to";
        return out;
    }
    if (see.corpseEmpty) {
        out.step = RecoveryStep::Done;
        out.reason = "the corpse has nothing left in it";
        return out;
    }

    // ENOUGH TRIPS. A corpse decays; an errand that cannot count does not.
    if (see.attemptsSoFar >= tune.maxAttempts) {
        out.step = RecoveryStep::Abandon;
        out.reason = "walked at this corpse as often as it is worth";
        return out;
    }

    // THE DEATH LOOP, refused before it can start.
    //
    // UO raises a ghost at roughly a tenth of its health. Walking back into
    // the fight that killed you at a tenth is not courage, it is the loop:
    // die, resurrect, walk back, die. Heal first -- the corpse will keep for
    // a few minutes, and this is the single cheapest place to break the cycle
    // section 47 asks fleets to detect.
    if (see.hpFraction < tune.minHpToReturn) {
        out.step = RecoveryStep::Recover;
        out.reason = "too hurt to walk back into whatever did this";
        return out;
    }

    // AND THE PLACE ITSELF. Danger heat is this character's OWN memory --
    // somewhere it died three times is dangerous to it specifically -- and a
    // cautious life is allowed to write the gear off rather than feed it.
    // A bold one goes back into more than a timid one will.
    // The curve REACHES ITS ENDPOINTS, which the first version did not:
    // 0.35 + 0.55 x tolerance tops out at 0.90, so a character documented as
    // "go back regardless" still abandoned a corpse at heat 0.95. The same
    // mistake as the combat crowding rule an hour earlier -- a tolerance that
    // never quite reaches the value its own header promises. Both were caught
    // by a test rather than by a session, which is the argument for the
    // tests.
    const double tolerated = 0.35 + 0.70 * tune.riskTolerance;
    // Always make one prepared recovery attempt.  Heat commonly contains the
    // death that created this corpse, so applying it before attempt zero made
    // the nominal three-trip budget unreachable and wrote off every valuable
    // full-loot corpse immediately.  After one failed approach, learned danger
    // may legitimately veto further trips.
    if (see.attemptsSoFar > 0 && see.dangerHeatAtCorpse > tolerated) {
        out.step = RecoveryStep::Abandon;
        out.reason = "that place has killed this character too often to be "
                     "worth the gear";
        return out;
    }

    if (see.corpseDistance > 2) {
        out.step = RecoveryStep::TravelToCorpse;
        out.reason = "healthy enough, and the place is survivable";
        return out;
    }

    out.step = RecoveryStep::Loot;
    out.reason = "standing over it";
    return out;
}

}  // namespace uo::life
