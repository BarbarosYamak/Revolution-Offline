#include "uo/training.h"

namespace uo::train {

const char* TargetKindName(TargetKind t) {
    switch (t) {
        case TargetKind::Self:      return "self";
        case TargetKind::Character: return "character";
        case TargetKind::Item:      return "item";
        case TargetKind::Ground:    return "ground";
        default:                    return "none";
    }
}

bool Context::Knows(int spellId) const {
    // An empty book means "we have not read it yet", not "it is empty". Being
    // permissive here keeps a caller that has not requested the spellbook from
    // silently training nothing.
    if (knownSpells.empty()) return true;
    for (int s : knownSpells)
        if (s == spellId) return true;
    return false;
}

namespace {

Action Spell(const char* name, int id, int mana, int reqTenths, TargetKind t,
             int reagents) {
    Action a;
    a.name = name;
    a.spellId = id;
    a.manaCost = mana;
    a.skillReqTenths = reqTenths;
    a.target = t;
    a.reagentKinds = reagents;
    return a;
}

} // namespace

const std::vector<Band>& MageryBands() {
    // Every number is from runtime/scripts/spells/spells_magery.scp; every
    // band boundary is from the RevolutionUO training guide.
    static const std::vector<Band> kBands = [] {
        std::vector<Band> v;
        const std::string src =
            "RevolutionUO forum training guide, topic 59111 "
            "(REVOLUTION_FORUM_GUIDE); spell numbers from spells_magery.scp";

        Band b0;
        b0.minTenths = 0; b0.maxTenths = 300;
        b0.actions = {Spell("Night Sight", 6, 4, 0, TargetKind::Self, 2)};
        b0.evidence = src;
        v.push_back(b0);

        Band b1;
        b1.minTenths = 300; b1.maxTenths = 400;
        b1.actions = {Spell("Bless", 17, 9, 200, TargetKind::Self, 2)};
        b1.evidence = src;
        v.push_back(b1);

        // The band M3 got wrong. Greater Heal, not Night Sight.
        Band b2;
        b2.minTenths = 400; b2.maxTenths = 600;
        b2.actions = {Spell("Greater Heal", 29, 11, 400, TargetKind::Self, 4)};
        b2.evidence = src;
        v.push_back(b2);

        Band b3;
        b3.minTenths = 600; b3.maxTenths = 700;
        b3.actions = {Spell("Magic Reflection", 36, 20, 600, TargetKind::Self, 3)};
        b3.evidence = src + "; the guide also lists Paralyze as an alternative here";
        v.push_back(b3);

        Band b4;
        b4.minTenths = 700; b4.maxTenths = 800;
        b4.actions = {
            Spell("Reveal", 39, 20, 700, TargetKind::Ground, 3),
            Spell("Invisibility", 44, 20, 700, TargetKind::Self, 3),
            Spell("Energy Bolt", 42, 20, 700, TargetKind::Character, 2),
        };
        b4.evidence = src;
        v.push_back(b4);

        Band b5;
        b5.minTenths = 800; b5.maxTenths = 900;
        b5.actions = {
            Spell("Energy Field", 50, 40, 800, TargetKind::Ground, 4),
            Spell("Mass Dispel", 53, 40, 800, TargetKind::Ground, 4),
        };
        b5.evidence = src;
        v.push_back(b5);

        Band b6;
        b6.minTenths = 900; b6.maxTenths = 1001;
        b6.actions = {
            Spell("Earthquake", 56, 50, 900, TargetKind::None, 4),
            Spell("Summon Earth Elemental", 58, 40, 900, TargetKind::None, 4),
        };
        b6.evidence = src;
        v.push_back(b6);

        return v;
    }();
    return kBands;
}

const Band* BandFor(const std::vector<Band>& bands, int skillTenths) {
    for (const Band& b : bands) {
        if (skillTenths >= b.minTenths && skillTenths < b.maxTenths) return &b;
    }
    return nullptr;
}

const Action* ChooseAction(const std::vector<Band>& bands, const Context& ctx,
                           std::string* whyNot) {
    const Band* band = BandFor(bands, ctx.skillTenths);
    if (!band || band->actions.empty()) {
        if (whyNot) *whyNot = "no band covers this skill value";
        return nullptr;
    }

    // A character in trouble should not be standing still casting practice
    // spells. Saying so is more useful than returning something unusable.
    if (!ctx.safe) {
        if (whyNot) *whyNot = "not safe to train here";
        return nullptr;
    }

    bool sawUnknown = false, sawUnaffordable = false, sawNoTarget = false;
    bool sawNoReagents = false, sawUnderSkill = false;

    for (const Action& a : band->actions) {
        // The shard's own requirement, not the band's. A band boundary is
        // player advice; SKILLREQ is the rule.
        if (ctx.skillTenths < a.skillReqTenths) { sawUnderSkill = true; continue; }
        if (!ctx.Knows(a.spellId))              { sawUnknown = true; continue; }
        if (a.reagentKinds > 0 && !ctx.haveReagents) { sawNoReagents = true; continue; }
        if (a.target == TargetKind::Character && !ctx.haveOtherCharacter) {
            sawNoTarget = true; continue;
        }
        if (!Affordable(a, ctx)) { sawUnaffordable = true; continue; }
        return &a;
    }

    // Report the most actionable reason: the one the caller can do something
    // about soonest.
    if (whyNot) {
        if (sawUnaffordable)      *whyNot = "not enough mana";
        else if (sawNoReagents)   *whyNot = "out of reagents";
        else if (sawNoTarget)     *whyNot = "no legal target";
        else if (sawUnknown)      *whyNot = "spell not in the book";
        else if (sawUnderSkill)   *whyNot = "below the spell's own SKILLREQ";
        else                      *whyNot = "nothing in this band is usable";
    }
    return nullptr;
}

} // namespace uo::train
