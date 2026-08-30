// tests/interaction_rotation.cpp -- docs/BOT_ARCHITECTURE.md sections 16, 47.
//
// "One silent NPC is not the whole trade" -- a rule this project has broken
// in BOTH directions, and each break cost a live session.

#include "uo/interaction/npc_rotation.h"

#include <cstdio>

namespace {

int g_checks = 0;
int g_failures = 0;
using namespace uo::life;

void Expect(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { std::printf("  FAIL: %s\n", what); ++g_failures; }
}

// TOO FORGIVING: the bank ask went to the same banker every tick forever,
// because nothing recorded that this one had already ignored us.
void TestASilentNpcIsEventuallySkipped() {
    std::printf("[rotation: an npc that never answers gets skipped]\n");
    NpcRotation r;
    r.Configure(3);

    r.Aim(0xAAA);
    Expect(!r.NoteSilence(), "one silence is not a verdict");
    Expect(!r.NoteSilence(), "nor two");
    Expect(r.NoteSilence(), "three is");
    Expect(r.Skip().size() == 1, "and the banker is on the skip list");
    Expect(r.Skip()[0] == 0xAAA, "the right one");
}

// TOO HARSH: Ysolde asked ONE mage trainer for Meditation, was refused, and
// the skill was written off entirely -- she never bought a tenth of it from
// anyone. Sphere's teaching ceiling is per-NPC, so the next trainer is a
// different question.
void TestTheNextNpcGetsAFullChance() {
    std::printf("[rotation: the next one is a fresh question, not a verdict]\n");
    NpcRotation r;
    r.Configure(3);

    r.Aim(0xAAA);
    r.NoteSilence(); r.NoteSilence(); r.NoteSilence();

    Expect(!r.Aim(0xBBB), "a different face is not the same face");
    Expect(r.Tries() == 0, "and starts on a clean tally");
    Expect(!r.NoteSilence(), "so its first silence is only its first");
    Expect(!r.NoteSilence(), "and its second only its second");
    Expect(r.NoteSilence(), "three of its own before it is written off");
    Expect(r.Exhausted() == 2, "two npcs have now gone silent");
}

void TestOutOfDoors() {
    std::printf("[rotation: knowing when there is nobody left to ask]\n");
    NpcRotation r;
    r.Configure(1);
    Expect(!r.OutOfDoors(3), "nobody tried yet");
    r.Aim(1); r.NoteSilence();
    r.Aim(2); r.NoteSilence();
    Expect(!r.OutOfDoors(3), "two of three");
    r.Aim(3); r.NoteSilence();
    Expect(r.OutOfDoors(3), "three of three: the errand fails honestly");
}

// A banker that ignored us while we walked past is not a banker that will
// ignore us at the counter.
void TestAnAnswerForgivesEveryone() {
    std::printf("[rotation: one answer clears the grudges]\n");
    NpcRotation r;
    r.Configure(1);
    r.Aim(1); r.NoteSilence();
    r.Aim(2); r.NoteSilence();
    Expect(r.Exhausted() == 2, "two written off");

    r.Aim(3);
    r.NoteAnswered();
    Expect(r.Exhausted() == 0, "a successful ask starts the town clean again");
}

void TestRepeatedSilenceDoesNotDuplicate() {
    std::printf("[rotation: an npc is written off once, not once per tick]\n");
    NpcRotation r;
    r.Configure(1);
    r.Aim(0xCCC);
    Expect(r.NoteSilence(), "written off");

    // Once written off there is no current npc to blame, so a further report
    // of silence belongs to nobody. This assertion originally expected true
    // and was simply wrong about the semantics -- the code was right, and the
    // test is the thing that changed.
    Expect(!r.NoteSilence(), "with nobody aimed at, silence blames nobody");

    // And re-aiming at the same face does not list it twice: the skip list is
    // a set of doors already tried, not a tally of ticks.
    r.Aim(0xCCC);
    r.NoteSilence();
    Expect(r.Skip().size() == 1, "listed exactly once, however often asked");
}

}  // namespace

int main() {
    std::printf("interaction_rotation\n");
    TestASilentNpcIsEventuallySkipped();
    TestTheNextNpcGetsAFullChance();
    TestOutOfDoors();
    TestAnAnswerForgivesEveryone();
    TestRepeatedSilenceDoesNotDuplicate();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
