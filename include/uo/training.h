#pragma once

// ---------------------------------------------------------------------------
// Historical training policy (M3.6).
//
// WHY THIS EXISTS
//
// M3 trained Magery by casting Night Sight at skill 50 and measured
// ~0.53 skill/hour. The gain was real and legitimately earned -- but Night
// Sight is the WRONG SPELL for that band, so the rate is not an authentic
// Revolution number and the ~17-hour projection to 60 derived from it is not a
// valid authenticity estimate.
//
// RevolutionUO's own training guide (forum topic 59111) gives the bands:
//
//     0-30    Night Sight
//     30-40   Bless
//     40-60   Greater Heal
//     60-70   Magic Reflection
//     70-80   Reveal / Invisibility / Energy Bolt
//     80-90   Energy Field / Mass Dispel
//     90-100  Earth Elemental / Earthquake
//
// This turns that table into something the bot selects from, rather than a
// constant buried in a scenario.
//
// WHAT IT IS NOT
//
// Not a spell-spam list. Where a band offers alternatives, the choice is made
// on what the character can actually do right now -- does it know the spell,
// can it pay the mana, is there a legal target, is it safe -- and the first
// affordable option in the band's own preference order wins. A band with no
// affordable option returns nothing, which is a real answer: the honest move
// is to meditate, or to go and fix the shortage, not to cast something from
// the wrong band.
// ---------------------------------------------------------------------------

#include "uo/types.h"

#include <string>
#include <vector>

namespace uo::train {

enum class TargetKind : u8 {
    None = 0,
    Self,        // spellflag_targ_char + playeronly, cast on yourself
    Character,   // needs another character
    Item,
    Ground,
};

const char* TargetKindName(TargetKind t);

// One legitimate training action. All numbers are the shard's own, read from
// runtime/scripts/spells/spells_magery.scp.
struct Action {
    std::string name;
    int  spellId = -1;          // Sphere [SPELL n]; -1 = not a spell
    int  manaCost = 0;
    int  skillReqTenths = 0;    // the spell's own SKILLREQ
    TargetKind target = TargetKind::None;
    int  reagentKinds = 0;      // how many distinct reagents it lists
};

// A band of the skill, and the actions Revolution players used inside it, in
// preference order.
struct Band {
    int minTenths = 0;          // inclusive
    int maxTenths = 0;          // exclusive
    std::vector<Action> actions;
    std::string evidence;
};

// What the character can do at this instant.
struct Context {
    int  skillTenths = 0;
    int  manaNow = 0;
    int  manaMax = 0;
    bool haveReagents = true;   // this runtime has ReagentsRequired=0
    bool haveOtherCharacter = false;
    bool safe = true;           // not in combat / not fleeing
    // Spell ids the character's book actually holds. Empty means "unknown",
    // and is treated as permissive so a caller that has not read the book yet
    // still gets a sensible answer.
    std::vector<int> knownSpells;

    bool Knows(int spellId) const;
};

// The Magery table above, as data.
const std::vector<Band>& MageryBands();

// Which band covers this skill value, or nullptr past the end.
const Band* BandFor(const std::vector<Band>& bands, int skillTenths);

// The action to take now, or nullptr when nothing in the band is affordable.
// `whyNot` (optional) receives a short reason when nothing is returned, so a
// caller can log "no mana" rather than a silent no-op.
const Action* ChooseAction(const std::vector<Band>& bands, const Context& ctx,
                           std::string* whyNot = nullptr);

// Mana a character must have banked before an action is worth starting.
inline bool Affordable(const Action& a, const Context& ctx) {
    return ctx.manaNow >= a.manaCost;
}

} // namespace uo::train
