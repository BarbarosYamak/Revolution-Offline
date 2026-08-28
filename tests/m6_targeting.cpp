// M6.1 -- legal target classification and threat, with no server.
//
// Every rule asserted here is traceable to this shard's own configuration or
// to Source-X source, and the citation is in the check name. The reason this
// file exists at all is that M3.9 lost a character to exactly one of these
// rules -- a swing at a blue animal inside a guard zone with
// GuardsInstantKill=1 -- and no test could have caught it, because there was
// no model of legality to test.

#include "uo/combat.h"

#include <cstdio>
#include <vector>

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what);
    }
}

void Section(const char* name) { std::printf("[%s]\n", name); }

using namespace uo;
using namespace uo::combat;

Candidate Mob(Noto n, i32 dist = 3) {
    Candidate c;
    c.serial = 0x1000 + static_cast<u32>(dist);
    c.name = NotoName(n);
    c.noto = n;
    c.dist = dist;
    c.hpCur = 50;
    c.hpMax = 100;
    return c;
}

EngagePolicy Fighter() {
    EngagePolicy p;
    p.riskTolerance = 0.90;   // deliberately permissive, so LEGALITY is what
    return p;                 // the legality tests are actually measuring
}

// --------------------------------------------------------------------------
void TestShardRulesAreTheReadValues() {
    Section("rules: the crime settings are this shard's, not conventions");

    const CrimeRules& r = RevolutionCrimeRules();
    // runtime/sphere.ini:763, :772, :745, :757, :754
    Check(r.attackingIsACrime, "AttackingIsaCrime=1");
    Check(r.guardsInstantKill, "GuardsInstantKill=1");
    Check(r.criminalMinutes == 3, "CriminalTimer=3");
    Check(r.murderMinCount == 1,
          "MurderMinCount=1 -- the FIRST murder turns the character red");
    Check(r.murderDecaySeconds == 8 * 60 * 60,
          "MurderDecayTime=8*60*60 -- and it stays red for eight hours");
}

// --------------------------------------------------------------------------
void TestNotoValuesMatchTheWire() {
    Section("noto: the enum is Source-X NOTO_TYPE verbatim");

    // sphereproto.h:540-550. If these ever drift, every legality decision
    // below is being made about the wrong colour.
    Check(static_cast<int>(Noto::Unknown) == 0,      "0 = NOTO_INVALID");
    Check(static_cast<int>(Noto::Innocent) == 1,     "1 = NOTO_GOOD");
    Check(static_cast<int>(Noto::GuildSame) == 2,    "2 = NOTO_GUILD_SAME");
    Check(static_cast<int>(Noto::Neutral) == 3,      "3 = NOTO_NEUTRAL");
    Check(static_cast<int>(Noto::Criminal) == 4,     "4 = NOTO_CRIMINAL");
    Check(static_cast<int>(Noto::GuildWar) == 5,     "5 = NOTO_GUILD_WAR");
    Check(static_cast<int>(Noto::Murderer) == 6,     "6 = NOTO_EVIL");
    Check(static_cast<int>(Noto::Invulnerable) == 7, "7 = NOTO_INVUL");
}

// --------------------------------------------------------------------------
void TestTheM39Death() {
    Section("legality: the M3.9 death is now a refusal");

    const CrimeRules& r = RevolutionCrimeRules();
    Stance guarded;  guarded.inGuardedRegion = true;
    Stance wilds;    wilds.inGuardedRegion = false;

    // The exact board state that killed a character: a blue farm animal, in
    // town, with the character at full health and willing to fight anything.
    const Candidate cow = Mob(Noto::Innocent, 2);

    const Classification inTown = Classify(cow, guarded, r, Fighter(), 1.0);
    Check(inTown.legality == Legality::GuardKill,
          "a blue inside a guarded region classifies as GUARD_KILL");
    Check(!inTown.engage, "and is never engaged");

    const Classification outside = Classify(cow, wilds, r, Fighter(), 1.0);
    Check(outside.legality == Legality::FlagsCriminal,
          "the same blue outside a guard zone only FLAGS us");
    Check(!outside.engage,
          "and is still refused, because acceptCriminalFlag defaults to false");

    // A character that has explicitly decided to accept the flag may swing --
    // this is the PK case, and it must be reachable or M6's PvP primitives
    // are unreachable too.
    EngagePolicy pk = Fighter();
    pk.acceptCriminalFlag = true;
    Check(Classify(cow, wilds, r, pk, 1.0).engage,
          "a character that accepts the flag CAN attack it outside town");
    Check(!Classify(cow, guarded, r, pk, 1.0).engage,
          "but not even a PK swings at a blue under GuardsInstantKill");
}

// --------------------------------------------------------------------------
void TestSelfDefenceIsFree() {
    Section("legality: self-defence against a blue is not a crime");

    // Source-X CCharFight.cpp:1474 excludes a target that already holds
    // MEMORY_AGGREIVED|MEMORY_HARMEDBY toward us. So a blue that attacked
    // first may be fought back, IN TOWN, with no flag and no guard.
    const CrimeRules& r = RevolutionCrimeRules();
    Stance guarded; guarded.inGuardedRegion = true; guarded.attackersOnMe = 1;

    Candidate aggressor = Mob(Noto::Innocent, 1);
    aggressor.aggressedMe = true;
    aggressor.attackingMe = true;

    const Classification v = Classify(aggressor, guarded, r, Fighter(), 1.0);
    Check(v.legality == Legality::Lawful,
          "a blue that hit us first is LAWFUL to hit back, even in town");
    Check(v.engage, "and we do hit back");

    // The same blue that has NOT hit us is still off limits in the same spot.
    Check(Classify(Mob(Noto::Innocent, 1), guarded, r, Fighter(), 1.0)
              .legality == Legality::GuardKill,
          "an unprovoked blue in the same spot is still a guard kill");
}

// --------------------------------------------------------------------------
void TestNeverTargets() {
    Section("legality: things that are never targets");

    const CrimeRules& r = RevolutionCrimeRules();
    Stance me;

    Check(Classify(Mob(Noto::Invulnerable), me, r, Fighter(), 1.0).legality ==
              Legality::Forbidden,
          "yellow (a guard, an invulnerable NPC) is forbidden");
    Check(Classify(Mob(Noto::GuildSame), me, r, Fighter(), 1.0).legality ==
              Legality::Forbidden,
          "a guildmate is forbidden");

    // The one that matters most: an unknown hue is NOT fair game. The colour
    // arrives a packet later, and guessing costs a character.
    Check(Classify(Mob(Noto::Unknown), me, r, Fighter(), 1.0).legality ==
              Legality::Forbidden,
          "a mobile whose notoriety has not arrived is NOT a target");

    Candidate pet = Mob(Noto::Neutral);
    pet.isMyPet = true;
    Check(Classify(pet, me, r, Fighter(), 1.0).legality == Legality::Forbidden,
          "our own follower is forbidden");
}

// --------------------------------------------------------------------------
void TestLawfulColours() {
    Section("legality: gray, orange and red are lawful");

    const CrimeRules& r = RevolutionCrimeRules();
    Stance guarded; guarded.inGuardedRegion = true;

    for (Noto n : {Noto::Neutral, Noto::Criminal, Noto::GuildWar,
                   Noto::Murderer}) {
        const Classification v = Classify(Mob(n, 2), guarded, r, Fighter(), 1.0);
        if (v.legality != Legality::Lawful) {
            std::printf("  FAIL: %s should be lawful, got %s\n", NotoName(n),
                        LegalityName(v.legality));
            ++g_failures;
        }
        ++g_checks;
    }
}

// --------------------------------------------------------------------------
void TestRiskToleranceSeparatesLives() {
    Section("policy: a mage and a swordsman read the same board differently");

    const CrimeRules& r = RevolutionCrimeRules();
    Stance me;

    // One red player in war mode at melee range, bar full. Genuinely nasty.
    Candidate pk = Mob(Noto::Murderer, 1);
    pk.isPlayer = true;
    pk.warMode = true;
    pk.hpCur = 100; pk.hpMax = 100;

    EngagePolicy mage;  mage.riskTolerance = 0.30;   // catalogue value
    EngagePolicy sword; sword.riskTolerance = 0.55;  // catalogue value

    const Classification asMage  = Classify(pk, me, r, mage, 1.0);
    const Classification asSword = Classify(pk, me, r, sword, 1.0);

    Check(asMage.legality == Legality::Lawful &&
          asSword.legality == Legality::Lawful,
          "legality does not depend on nerve -- both may attack a red");
    Check(!asMage.engage, "the mage declines this fight");
    Check(asMage.threat == asSword.threat,
          "they see the SAME threat; only the tolerance differs");
    Check(asMage.threat > 0.5, "a red player in melee reads as high threat");
}

// --------------------------------------------------------------------------
void TestThreatUsesOnlyWhatIsVisible() {
    Section("threat: built from the health bar, never from hidden stats");

    const CrimeRules& r = RevolutionCrimeRules();
    Stance me;
    EngagePolicy p = Fighter();

    Candidate full = Mob(Noto::Neutral, 5);
    full.hpCur = 100; full.hpMax = 100;
    Candidate hurt = Mob(Noto::Neutral, 5);
    hurt.hpCur = 10; hurt.hpMax = 100;

    Check(Classify(full, me, r, p, 1.0).threat >
          Classify(hurt, me, r, p, 1.0).threat,
          "a full bar is more threatening than a nearly empty one");

    Candidate near = Mob(Noto::Neutral, 1);
    Candidate far  = Mob(Noto::Neutral, 9);
    Check(Classify(near, me, r, p, 1.0).threat >
          Classify(far, me, r, p, 1.0).threat,
          "closer is more threatening");

    Candidate swinging = Mob(Noto::Neutral, 1);
    swinging.attackingMe = true;
    Check(Classify(swinging, me, r, p, 1.0).threat >
          Classify(near, me, r, p, 1.0).threat,
          "something actually hitting us outranks something merely nearby");

    // No bar at all must not read as harmless -- that is the ambush case.
    Candidate noBar = Mob(Noto::Neutral, 1);
    noBar.hpCur = -1; noBar.hpMax = -1;
    Check(Classify(noBar, me, r, p, 1.0).threat > 0.0,
          "a mobile with no health bar yet is not assumed harmless");
}

// --------------------------------------------------------------------------
void TestGangPressureRaisesThreat() {
    Section("threat: more attackers on us makes each of them worse");

    const CrimeRules& r = RevolutionCrimeRules();
    EngagePolicy p = Fighter();
    Candidate wolf = Mob(Noto::Neutral, 1);
    wolf.attackingMe = true;

    Stance alone;   alone.attackersOnMe = 1;
    Stance swarmed; swarmed.attackersOnMe = 4;

    Check(Classify(wolf, swarmed, r, p, 1.0).threat >
          Classify(wolf, alone, r, p, 1.0).threat,
          "the same wolf is more dangerous when three others are also on us");
}

// --------------------------------------------------------------------------
void TestChooseTarget() {
    Section("choice: the thing already hitting us wins");

    const CrimeRules& r = RevolutionCrimeRules();
    Stance me; me.attackersOnMe = 1;
    EngagePolicy p = Fighter();

    std::vector<Candidate> board;
    board.push_back(Mob(Noto::Invulnerable, 1));    // 0: a guard, never
    Candidate blue = Mob(Noto::Innocent, 1);
    board.push_back(blue);                          // 1: a blue, never
    board.push_back(Mob(Noto::Neutral, 2));         // 2: lawful, idle
    Candidate onMe = Mob(Noto::Neutral, 3);
    onMe.attackingMe = true;
    board.push_back(onMe);                          // 3: lawful, ON US

    std::vector<Classification> verdicts;
    const int pick = ChooseTarget(board, me, r, p, 1.0, &verdicts);
    Check(pick == 3, "the attacker is chosen over the closer idle mobile");
    Check(verdicts.size() == board.size(),
          "every candidate gets a printable verdict, chosen or not");
    Check(!verdicts[0].engage && !verdicts[1].engage,
          "the guard and the blue are both refused");

    // Nothing lawful on the board -> no target at all, rather than a default.
    std::vector<Candidate> onlyForbidden;
    onlyForbidden.push_back(Mob(Noto::Invulnerable, 1));
    onlyForbidden.push_back(Mob(Noto::Innocent, 1));
    Check(ChooseTarget(onlyForbidden, me, r, p, 1.0) == -1,
          "a board with nothing lawful on it yields NO target");
    Check(ChooseTarget({}, me, r, p, 1.0) == -1, "an empty board yields none");
}

// --------------------------------------------------------------------------
void TestHurtCharacterDoesNotOpenFights() {
    Section("policy: a hurt character does not start something new");

    const CrimeRules& r = RevolutionCrimeRules();
    Stance me;
    EngagePolicy p = Fighter();
    p.minHpFractionToOpen = 0.60;

    const Candidate idle = Mob(Noto::Neutral, 2);
    Check(Classify(idle, me, r, p, 1.0).engage,
          "at full health it opens the fight");
    Check(!Classify(idle, me, r, p, 0.35).engage,
          "at 35% it does not");

    // But it still defends itself at 35%, because declining to fight back is
    // not an option when something is already swinging.
    Candidate onMe = Mob(Noto::Neutral, 1);
    onMe.attackingMe = true;
    Check(Classify(onMe, me, r, p, 0.35).engage,
          "it still fights back against something already on it");
}

}  // namespace

int main() {
    std::printf("m6_targeting\n");
    TestShardRulesAreTheReadValues();
    TestNotoValuesMatchTheWire();
    TestTheM39Death();
    TestSelfDefenceIsFree();
    TestNeverTargets();
    TestLawfulColours();
    TestRiskToleranceSeparatesLives();
    TestThreatUsesOnlyWhatIsVisible();
    TestGangPressureRaisesThreat();
    TestChooseTarget();
    TestHurtCharacterDoesNotOpenFights();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
