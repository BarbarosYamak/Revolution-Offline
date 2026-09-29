// PvP decisions (uo/pvp.h): the PK's victim choice, the anti-PK's lawful
// targets, the per-fight break-off judgement and the alarm.
#include "uo/pvp.h"
#include "uo/chatter.h"

#include <cstdio>

using namespace uo;
using namespace uo::pvp;

static int g_checks = 0, g_failures = 0;
static void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_failures; std::printf("  FAIL: %s\n", what); }
}

int main() {
    Self pk; pk.hpFrac = 1.0; pk.bandages = 30; pk.nerve = 0.85;
    Target miner; miner.noto = 1; miner.dist = 5; miner.hand1 = 0x0E86;
    std::printf("[the PK]\n");
    Check(ShouldAmbush(miner, pk), "a lone miner at the rock is an opening for a bold PK");
    Target t = miner; t.playersNearTarget = 2;
    Check(!ShouldAmbush(t, pk), "witnesses beside the victim: no");
    t = miner; t.guardedThere = true;
    Check(VictimScore(t, pk) == 0.0, "never inside the guards (GuardsInstantKill=1)");
    Self inTown = pk; inTown.guardedHere = true;
    Check(VictimScore(miner, inTown) == 0.0, "and never from inside them");
    t = miner; t.noto = 6;
    Check(VictimScore(t, pk) == 0.0, "a PK does not ambush reds");
    t = miner; t.noto = 2;
    Check(VictimScore(t, pk) == 0.0, "nor guild allies");
    Self hurt = pk; hurt.hpFrac = 0.6;
    Check(VictimScore(miner, hurt) == 0.0, "nobody starts a fight hurt");
    t = miner; t.hand1 = 0x13B9;
    Check(VictimScore(t, pk) < VictimScore(miner, pk), "an armed fighter is a worse victim than a worker");
    Self timid = pk; timid.nerve = 0.3;
    Check(AmbushThreshold(timid.nerve) > AmbushThreshold(pk.nerve), "a timid PK needs a better opening");
    t = miner; t.beatUsBefore = true;
    Check(!ShouldAmbush(t, pk), "one who beat us before is left alone");

    std::printf("[the anti-PK]\n");
    Self hero; hero.hpFrac = 1.0; hero.bandages = 30; hero.nerve = 0.7;
    Target red; red.noto = 6; red.dist = 6; red.attackingInnocent = true;
    Check(ShouldHunt(red, hero), "a red attacking an innocent is engaged");
    Target blue = red; blue.noto = 1;
    Check(HuntScore(blue, hero) == 0.0, "innocents are never a lawful target");
    Target gang = red; gang.hostilePlayersNear = 2; gang.attackingInnocent = false;
    Check(!ShouldHunt(gang, hero), "not alone into a gang of reds");
    Self coward = hero; coward.nerve = 0.3;
    Check(HuntScore(red, coward) == 0.0, "not everyone is a hero");
    Target war = red; war.noto = 5; war.attackingInnocent = false; war.myAlliesNear = 2;
    Check(ShouldHunt(war, hero), "a guild-war enemy with our friends around is fair game");
    Check(LawfulTarget(4) && LawfulTarget(5) && LawfulTarget(6) && !LawfulTarget(1) && !LawfulTarget(2) &&
          !LawfulTarget(7), "lawful: criminal, war enemy, murderer");

    std::printf("[break off: a judgement, not a constant]\n");
    Check(!ShouldBreakOff(0.30, 20, 0.85, 0, 1, 20), "a bold fighter winning the race stays at 30%%");
    Check(ShouldBreakOff(0.30, 90, 0.40, 0, 1, 20), "a cautious one losing it leaves");
    Check(ShouldBreakOff(0.40, 80, 0.85, 0, 3, 20), "outnumbered three to one: leave even at 40%%");
    Check(ShouldBreakOff(0.10, 5, 0.95, 3, 1, 20), "nobody fights to the last hit point");

    std::printf("[alarm]\n");
    Check(IsAlarm(kAlarmLine) && IsAlarm("PK VAR") && IsAlarm("pk!") && IsAlarm("yardim edin"), "the shout is heard");
    Check(!IsAlarm("selam") && !IsAlarm("WTS 10 i_bandage 5gp") && !IsAlarm("pkg"), "ordinary talk is not an alarm");
    bool smallTalkAlarms = false;
    for (int topic = 0; topic < static_cast<int>(chatter::Topic::Count); ++topic) {
        const chatter::Lines l = chatter::LinesFor(static_cast<chatter::Topic>(topic));
        for (int i = 0; i < l.count; ++i) smallTalkAlarms = smallTalkAlarms || IsAlarm(l.items[i]);
    }
    Check(!smallTalkAlarms, "no small-talk line raises a false PK alarm");
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
