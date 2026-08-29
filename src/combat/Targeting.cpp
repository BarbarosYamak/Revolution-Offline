#include "uo/combat.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>

namespace uo::combat {

const char* NotoName(Noto n) {
    switch (n) {
        case Noto::Unknown:      return "unknown";
        case Noto::Innocent:     return "innocent";
        case Noto::GuildSame:    return "guild";
        case Noto::Neutral:      return "neutral";
        case Noto::Criminal:     return "criminal";
        case Noto::GuildWar:     return "guild-war";
        case Noto::Murderer:     return "murderer";
        case Noto::Invulnerable: return "invulnerable";
    }
    return "?";
}

const char* LegalityName(Legality l) {
    switch (l) {
        case Legality::Lawful:        return "lawful";
        case Legality::FlagsCriminal: return "flags_criminal";
        case Legality::GuardKill:     return "guard_kill";
        case Legality::Forbidden:     return "forbidden";
    }
    return "?";
}

const CrimeRules& RevolutionCrimeRules() {
    static const CrimeRules r{};   // defaults ARE the read values; see the header
    return r;
}

namespace {

std::string Fmt(const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return std::string(buf);
}

// The health BAR as a fraction, or -1 when the bar has not arrived. A player
// reading a bar sees "about half"; they do not see 43/97.
double BarFraction(const Candidate& c) {
    if (c.hpMax <= 0 || c.hpCur < 0) return -1.0;
    return std::min(1.0, static_cast<double>(c.hpCur) / c.hpMax);
}

}  // namespace

// ---------------------------------------------------------------------------
// LEGALITY
//
// Source-X, CCharFight.cpp:1474:
//   if (g_Cfg.m_fAttackingIsACrime
//       && (pCharTarg->Noto_GetFlag(this) == NOTO_GOOD)
//       && !pCharTarg->Memory_FindObjTypes(this, MEMORY_AGGREIVED|MEMORY_HARMEDBY))
//       CheckCrimeSeen(...)
//
// Three conditions, all three modelled below. The third is why self-defence
// is free: once something has hit us, hitting it back is not a crime.
// ---------------------------------------------------------------------------
Classification Classify(const Candidate& c, const Stance& me,
                        const CrimeRules& rules, const EngagePolicy& policy,
                        double myHpFraction) {
    Classification out;

    // --- things that are never targets ------------------------------------
    if (c.noto == Noto::Invulnerable) {
        out.legality = Legality::Forbidden;
        out.reason = "invulnerable (a guard or a protected NPC); the swing "
                     "would simply be refused";
        return out;
    }
    if (c.isMyPet) {
        out.legality = Legality::Forbidden;
        out.reason = "our own follower";
        return out;
    }
    if (c.noto == Noto::GuildSame) {
        out.legality = Legality::Forbidden;
        out.reason = "same guild";
        return out;
    }
    if (c.noto == Noto::Unknown) {
        // An unknown is not a target. The hue arrives in the next packet; a
        // character that treats "I have not been told yet" as "fair game" is
        // one blue farm animal away from a guard.
        out.legality = Legality::Forbidden;
        out.reason = "notoriety has not arrived yet -- an unknown is not a target";
        return out;
    }

    // --- would this flag us? ----------------------------------------------
    const bool wouldFlag = rules.attackingIsACrime &&
                           c.noto == Noto::Innocent &&
                           !c.aggressedMe;

    if (wouldFlag) {
        if (me.inGuardedRegion && rules.guardsInstantKill) {
            out.legality = Legality::GuardKill;
            out.reason = Fmt("attacking an innocent inside a guarded region: "
                             "GuardsInstantKill=1, so this is a death, not a risk");
        } else {
            out.legality = Legality::FlagsCriminal;
            out.reason = Fmt("attacking an unprovoked innocent flags us criminal "
                             "for %d minute(s)", rules.criminalMinutes);
        }
    } else {
        out.legality = Legality::Lawful;
        out.reason = c.aggressedMe
            ? "it attacked us first, so hitting back is not a crime"
            : Fmt("%s: lawful to attack", NotoName(c.noto));
    }

    // --- threat, from what is on screen -----------------------------------
    //
    // Deliberately coarse. There is no monster table here and there must not
    // be one: a bot that knows an ogre's real hit points knows something no
    // client is sent. What a player has is: is it hitting me, how close is
    // it, is its bar full, and how many other things are on me.
    double threat = 0.0;
    if (c.attackingMe)  threat += 0.45;
    else if (c.warMode) threat += 0.15;

    if (c.dist <= 1)      threat += 0.25;
    else if (c.dist <= 3) threat += 0.15;
    else if (c.dist <= 6) threat += 0.05;

    const double bar = BarFraction(c);
    if (bar >= 0.0) threat += 0.20 * bar;   // a full bar is a long fight
    else            threat += 0.10;         // no bar yet: assume middling

    // A real player is the dangerous case: potions, spells, and friends.
    if (c.isPlayer) threat += 0.20;
    if (c.noto == Noto::Murderer) threat += 0.10;

    if (me.attackersOnMe > 1) {
        threat += 0.05 * std::min(3, me.attackersOnMe - 1);
    }
    out.threat = std::min(1.0, threat);

    // --- may we, and should we, swing? ------------------------------------
    if (out.legality == Legality::Forbidden ||
        out.legality == Legality::GuardKill) {
        out.engage = false;
        return out;
    }

    if (out.legality == Legality::FlagsCriminal && !policy.acceptCriminalFlag) {
        // Self-defence is the exception, and it does not even need the
        // exception: if it aggressed us, wouldFlag was false above. This
        // branch is therefore always an UNPROVOKED attack on a blue.
        out.engage = false;
        out.reason += " -- refused: this character does not open fights that "
                      "flag it";
        return out;
    }

    if (c.attackingMe && policy.defendSelf) {
        out.engage = true;
        out.reason += "; already under attack, so defending";
        return out;
    }

    if (c.dist > policy.maxEngageDistance) {
        out.engage = false;
        out.reason += Fmt("; too far to open (%d > %d)", c.dist,
                          policy.maxEngageDistance);
        return out;
    }

    if (myHpFraction < policy.minHpFractionToOpen) {
        out.engage = false;
        out.reason += Fmt("; too hurt to open a fight (%.0f%% < %.0f%%)",
                          myHpFraction * 100.0,
                          policy.minHpFractionToOpen * 100.0);
        return out;
    }

    // Risk tolerance is the last gate: a mage (0.30) refuses fights a
    // swordsman (0.55) takes, from the same board state.
    if (out.threat > policy.riskTolerance) {
        out.engage = false;
        out.reason += Fmt("; threat %.2f above this character's tolerance %.2f",
                          out.threat, policy.riskTolerance);
        return out;
    }

    out.engage = true;
    return out;
}

int ChooseTarget(const std::vector<Candidate>& candidates, const Stance& me,
                 const CrimeRules& rules, const EngagePolicy& policy,
                 double myHpFraction,
                 std::vector<Classification>* verdictsOut) {
    if (verdictsOut) verdictsOut->clear();

    int best = -1;
    double bestScore = -1.0;
    for (usize i = 0; i < candidates.size(); ++i) {
        const Classification v =
            Classify(candidates[i], me, rules, policy, myHpFraction);
        if (verdictsOut) verdictsOut->push_back(v);
        if (!v.engage) continue;

        // The thing already hitting us outranks anything else, however weak
        // the other thing looks: walking past an active attacker to open a
        // second fight is how one fight becomes two.
        double score = v.threat;
        if (candidates[i].attackingMe) score += 1.0;
        // Break ties toward the closer one -- fewer steps, less time exposed.
        score -= 0.01 * candidates[i].dist;

        if (score > bestScore) { bestScore = score; best = static_cast<int>(i); }
    }
    return best;
}

int ChoosePrey(const std::vector<Candidate>& candidates, const Stance& me,
               const CrimeRules& rules, const EngagePolicy& policy,
               double myHpFraction) {
    int best = -1;
    double bestScore = -1.0;
    for (usize i = 0; i < candidates.size(); ++i) {
        const Candidate& c = candidates[i];

        // A fight already in progress is not a fight to pick. ChooseTarget
        // owns that case, and it must, because walking off to start a second
        // fight while something swings at you is how one fight becomes two.
        if (c.attackingMe) continue;

        const Classification v = Classify(c, me, rules, policy, myHpFraction);
        if (!v.engage) continue;

        // WEAKEST FIRST -- the inverse of ChooseTarget. A warrior levels on
        // the weakest undead at the edge of the graveyard, not the strongest
        // thing in the middle of it.
        double score = 1.0 - v.threat;

        // AND ALONE. Company is what actually kills a new fencer: it is not
        // the skeleton, it is the second skeleton. Every other engageable
        // mobile within kCrowdRadius of this one is a reason to pick a
        // different fight, and the penalty is steep enough to outweigh a
        // sizeable threat difference.
        int company = 0;
        for (usize j = 0; j < candidates.size(); ++j) {
            if (j == i) continue;
            const Candidate& o = candidates[j];
            if (o.isMyPet || o.isPlayer) continue;
            const i32 dx = o.dist - c.dist;
            if (dx > -kCrowdRadius && dx < kCrowdRadius) ++company;
        }
        score -= 0.35 * company;

        // Then prefer the near one, as ChooseTarget does: fewer steps across
        // open ground is less time for something else to notice us.
        score -= 0.01 * c.dist;

        if (score > bestScore) { bestScore = score; best = static_cast<int>(i); }
    }
    return best;
}

}  // namespace uo::combat
