#include "uo/travel_mode.h"

namespace uo::travelmode {

const char* ModeName(Mode m) {
    switch (m) {
        case Mode::Walk:            return "walk";
        case Mode::Moongate:        return "moongate";
        case Mode::LooseRuneRecall: return "loose_rune_recall";
        case Mode::RunebookRecall:  return "runebook_recall";
        default:                    return "?";
    }
}

i32 EstimateWalkSeconds(i32 tiles) {
    if (tiles <= 0) return 0;
    // ~2 tiles/second running, plus a third again for corners, doors and the
    // fact that a real route is never the straight line.
    return (tiles * 2) / 3 + 1;
}

namespace {

// Shared gate on anything that involves casting.
bool CanCastAtAll(const Capability& c, std::string* why) {
    if (c.dead)     { if (why) *why = "dead characters cannot cast"; return false; }
    if (c.inCombat) { if (why) *why = "in combat"; return false; }
    return true;
}

} // namespace

Option Evaluate(Mode m, const Capability& c, i32 walkTiles) {
    Option o;
    o.mode = m;

    switch (m) {
        case Mode::Walk:
            // The one that always works. A bot with nothing else still gets
            // where it is going, which is what M2.5 spent its whole milestone
            // making true.
            o.usable = true;
            o.estimatedSeconds = EstimateWalkSeconds(walkTiles);
            return o;

        case Mode::Moongate:
            if (!c.moongateRouteKnown) { o.why = "no useful gate pair known"; return o; }
            if (c.dead) { o.why = "dead characters cannot use gates"; return o; }
            o.usable = true;
            // The walk to a gate and away from the far side still dominates,
            // but it beats crossing a continent on foot.
            o.estimatedSeconds = EstimateWalkSeconds(walkTiles / 6) + 30;
            return o;

        case Mode::LooseRuneRecall:
            if (!c.haveMarkedRune) { o.why = "no rune marked at that destination"; return o; }
            if (!CanCastAtAll(c, &o.why)) return o;
            if (c.mageryTenths < c.recallSkillTenths) {
                o.why = "Magery below Recall's requirement";
                return o;
            }
            if (c.manaNow < c.recallMana) { o.why = "not enough mana"; return o; }
            if (!c.haveReagents) { o.why = "out of reagents"; return o; }
            o.usable = true;
            o.manaCost = c.recallMana;
            o.estimatedSeconds = 8;
            // Runes wear out and are eventually destroyed. Measured live: the
            // server warns "The recall rune is starting to fade" and then the
            // rune is gone from the world save.
            o.consumesRune = true;
            return o;

        case Mode::RunebookRecall: {
            if (!c.haveRunebookPage) { o.why = "no runebook page for that destination"; return o; }
            if (!CanCastAtAll(c, &o.why)) return o;

            const bool charged = c.runebookCharges > 0;
            if (!charged) {
                // Uncharged: the character casts it themselves, so Recall's own
                // requirements apply exactly as usual.
                if (c.mageryTenths < c.recallSkillTenths) {
                    o.why = "Magery below Recall's requirement, and no charges";
                    return o;
                }
                if (c.manaNow < c.recallMana) { o.why = "not enough mana, and no charges"; return o; }
                if (!c.haveReagents) { o.why = "out of reagents, and no charges"; return o; }
                o.manaCost = c.recallMana;
            } else {
                // Charged: the book casts from a stored Recall scroll, so the
                // caster's Magery and mana are not required. This is the
                // 13.05.2009 rule, and it works because a scroll supplies the
                // skill.
                o.chargeCost = 1;
            }
            o.usable = true;
            o.estimatedSeconds = 8;
            // A page is permanent: when its rune wears out the book re-cuts one
            // from the point it stored. That permanence is most of why players
            // carried books.
            o.consumesRune = false;
            return o;
        }

        default:
            o.why = "unknown mode";
            return o;
    }
}

namespace {

// Lower sorts first.
int Score(const Option& o) {
    if (!o.usable) return 1000000;
    int s = o.estimatedSeconds;
    // Prefer a runebook page to a loose rune at equal speed: the page survives,
    // the rune does not.
    if (o.consumesRune) s += 5;
    // A charge is a consumed scroll and scrolls cost money, so a free cast is
    // slightly preferred when the character can afford the mana.
    s += o.chargeCost * 3;
    return s;
}

} // namespace

std::vector<Option> Rank(const Capability& c, i32 walkTiles) {
    std::vector<Option> all;
    for (int i = 0; i < static_cast<int>(Mode::Count); ++i)
        all.push_back(Evaluate(static_cast<Mode>(i), c, walkTiles));

    // Insertion sort: four items, and a stable order keeps the output
    // reproducible for tests and logs.
    for (usize i = 1; i < all.size(); ++i) {
        Option key = all[i];
        usize j = i;
        while (j > 0 && Score(all[j - 1]) > Score(key)) {
            all[j] = all[j - 1];
            --j;
        }
        all[j] = key;
    }
    return all;
}

Mode Choose(const Capability& c, i32 walkTiles) {
    const std::vector<Option> ranked = Rank(c, walkTiles);
    for (const Option& o : ranked)
        if (o.usable) return o.mode;
    return Mode::Walk;   // unreachable: Walk is always usable
}

} // namespace uo::travelmode
