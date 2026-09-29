// A build may only plan Meditation if it plans to SPEND mana.
//
// Sphere's Skill_Meditation refuses outright at full mana -- CCharSkill.cpp
// gives "You are at peace" and no attempt -- so a life whose plan never
// drains its mana pool can never gain a tenth of Meditation. Planning it
// anyway is not a harmless extra: NeedSkillTraining picks the plan's highest
// unfinished skill, so the dead entry becomes the life's PERMANENT training
// answer and every practice sitting is spent on a skill that cannot move.
//
// Observed: Selene (alchemist) reported `want_train=Meditation (target 50.0)`
// on every tick of a whole session with Meditation flat at 0.0 --
// artifacts/fleet_ramp_20260906/Selene.console.txt. Owner ruling 2026-09-06:
// "if it is craft no need, if it's attack sure".
//
// The rule asserted here, over the catalogue only -- no server, no MULs:
//
//   Meditation planned  =>  Magery planned in a mana-SPENDING role
//                           (Primary or Secondary, never a Utility dabble).
//
// Magery is the only mana consumer this catalogue models. The role, not the
// mere presence of Magery, is what separates a caster from a crafter who
// happens to keep its creation 50.0: a Utility Magery target sits at the
// value creation already gave it, is never trained, and is never cast in a
// Craft income loop -- so the mana pool it implies is always full.

#include "uo/professions.h"
#include "uo/rules.h"

#include <cstdio>

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

using namespace uo;

const prof::SkillTargetSpec* Find(const prof::Profession& p, int skillId) {
    for (const prof::SkillTargetSpec& t : p.targets) {
        if (t.skillId == skillId) return &t;
    }
    return nullptr;
}

bool SpendsMana(const prof::SkillTargetSpec& magery) {
    return magery.role == prof::SkillRole::Primary ||
           magery.role == prof::SkillRole::Secondary;
}

void TestMeditationNeedsAManaSpender() {
    std::printf("[catalogue: Meditation only where the build spends mana]\n");

    int planners = 0;
    for (const prof::Profession& p : prof::All()) {
        const prof::SkillTargetSpec* med = Find(p, rules::kMeditation);
        if (med == nullptr) {
            // A build that does not plan Meditation must not claim the third
            // creation slot for it either -- that slot is for a skill the
            // life means to earn (m5_professions, TestTheZeroSkillSlot).
            if (p.startZeroSkill == rules::kMeditation) {
                std::printf("  FAIL: '%s' starts Meditation at 0.0 but never "
                            "plans it\n", p.id.c_str());
                ++g_failures;
            }
            ++g_checks;
            continue;
        }
        ++planners;

        const prof::SkillTargetSpec* mag = Find(p, rules::kMagery);
        if (mag == nullptr) {
            std::printf("  FAIL: '%s' plans Meditation %.1f with no mana-"
                        "spending skill at all\n",
                        p.id.c_str(), med->tenths / 10.0);
            ++g_failures;
            ++g_checks;
            continue;
        }
        if (!SpendsMana(*mag)) {
            std::printf("  FAIL: '%s' plans Meditation %.1f but Magery %.1f is "
                        "only %s -- a dabble spends no mana, so Meditation "
                        "can never gain\n",
                        p.id.c_str(), med->tenths / 10.0, mag->tenths / 10.0,
                        prof::SkillRoleName(mag->role));
            ++g_failures;
        }
        ++g_checks;
    }

    // Guards the rule against being satisfied by an empty catalogue or by a
    // future pass that deletes Meditation everywhere: the attack builds keep
    // it, and that is the half of the ruling this test also has to hold.
    Check(planners >= 3,
          "the caster archetypes still plan Meditation (it was not deleted "
          "wholesale)");
}

}  // namespace

int main() {
    std::printf("=== build plan: Meditation requires a mana spender ===\n");
    TestMeditationNeedsAManaSpender();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
