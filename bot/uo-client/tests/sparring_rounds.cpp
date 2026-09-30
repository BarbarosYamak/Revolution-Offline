// Sparring v2: the pure decisions in uo/sparring.h -- kit, compatibility and
// the round plan. No client, no server. The client and runner only gather the
// facts these functions decide on (ClientSocial.cpp, runner/Social.cpp).

#include "uo/sparring.h"

#include <cstdio>
#include <vector>

using namespace uo;
using namespace uo::sparring;

static int g_checks = 0, g_failures = 0;
static void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_failures; std::printf("  FAIL: %s\n", what); }
}

static std::vector<Worn> Iron() {
    return {{4, 0x1415, 0}, {6, 0x1410, 0}, {7, 0x1411, 0}, {10, 0x1414, 0}, {13, 0x1413, 0}, {19, 0x1412, 0}};
}

static RoundSight Healthy(i64 now) {
    RoundSight s;
    s.nowMs = now; s.meetingStartMs = now - 1000;
    s.consented = s.inParty = s.peerReady = s.kitsCompatible = s.healthFresh = true;
    s.readyAskedMs = now - 500;
    s.selfPct = s.peerPct = 100; s.bandages = 20;
    s.lastRoundEndMs = 0;
    return s;
}

int main() {
    std::printf("[kit]\n");
    {
        std::vector<Worn> bare;
        Check(KitFor(bare.data(), bare.size()) == Kit::Fists, "bare hands, no armour: a fist spar (owner: every fighter may spar)");
        std::vector<Worn> book = {{1, 0x0EFA, 0}};
        Check(KitFor(book.data(), book.size()) == Kit::Fists, "a spellbook in hand fights as fists: casters can spar");
        auto dagger = Iron(); dagger.push_back({1, 0x0F51, 0});
        Check(KitFor(dagger.data(), dagger.size()) == Kit::Weapon, "dagger in full plain iron: a weapon spar");
        std::vector<Worn> daggerNoArmour = {{1, 0x0F51, 0}};
        Check(KitFor(daggerNoArmour.data(), daggerNoArmour.size()) == Kit::None, "a dagger without the iron set may not spar");
        auto sword = Iron(); sword.push_back({1, 0x13B9, 0});
        Check(KitFor(sword.data(), sword.size()) == Kit::None, "a real sword is not practice gear");
        auto magic = Iron(); magic.push_back({1, 0x0F51, 0x0488});
        Check(KitFor(magic.data(), magic.size()) == Kit::None, "a dyed/magic dagger is not practice gear");
        std::vector<Worn> shield = {{2, 0x1B72, 0}};
        Check(KitFor(shield.data(), shield.size()) == Kit::None, "a shield in the off hand: no");
        std::vector<Worn> mounted = {{25, 0x3E9F, 0}};
        Check(KitFor(mounted.data(), mounted.size()) == Kit::None, "mounted: dismount first");
        auto dyedIron = Iron(); dyedIron[0].hue = 0x0455; dyedIron.push_back({1, 0x13B3, 0});
        Check(KitFor(dyedIron.data(), dyedIron.size()) == Kit::None, "the iron set must be plain iron");
    }
    std::printf("[compatibility]\n");
    Check(Compatible(Kit::Fists, Kit::Fists) && Compatible(Kit::Weapon, Kit::Weapon), "same kit spars");
    Check(!Compatible(Kit::Weapon, Kit::Fists) && !Compatible(Kit::Fists, Kit::Weapon),
          "a dagger never spars an unarmoured wrestler");
    Check(!Compatible(Kit::None, Kit::None), "no kit, no spar");

    std::printf("[round plan]\n");
    const i64 t = 1000000;
    {
        RoundSight s = Healthy(t); s.peerReady = false; s.readyAskedMs = 0;
        Check(DecideRound(s).step == Step::SayReady, "first: announce readiness once");
        s.readyAskedMs = t - 1000;
        Check(DecideRound(s).step == Step::Wait, "then wait for the partner");
        s.readyAskedMs = t - kReadyWaitMs;
        Check(DecideRound(s).step == Step::End, "a partner who never says ready ends the meeting");
    }
    {
        RoundSight s = Healthy(t);
        Check(DecideRound(s).step == Step::StartRound, "both ready and healthy: start");
        s.roundActive = true;
        Check(DecideRound(s).step == Step::Continue, "a running round is left to the safety tick");
        s.roundActive = false; s.lastRoundEndMs = t - 1000;
        Check(DecideRound(s).step == Step::Wait, "breathe between rounds");
        s.lastRoundEndMs = t - kRoundGapMs;
        s.selfPct = 60;
        Check(DecideRound(s).step == Step::Bandage, "hurt: bandage before the next round");
        s.selfPct = 100; s.peerPct = 70;
        Check(DecideRound(s).step == Step::Wait, "partner still healing: wait");
        s.peerPct = 100; s.healthFresh = false;
        Check(DecideRound(s).step == Step::Wait, "stale health bars: wait, never swing blind");
    }
    {
        RoundSight s = Healthy(t);
        s.meetingStartMs = t - kMeetingMs;
        Check(DecideRound(s).step == Step::End, "the meeting clock ends it");
        s = Healthy(t); s.meetingStartMs = t - 170000;
        Check(DecideRound(s).step == Step::StartRound,
              "the clock counts from the PARTY: 170 s in is still a live meeting (v1 had already expired at 120 s from the invitation)");
        s = Healthy(t); s.roundsDone = kMaxRounds;
        Check(DecideRound(s).step == Step::End, "round cap ends it");
        s = Healthy(t); s.externalThreat = true;
        Check(DecideRound(s).step == Step::End, "a real threat ends it");
        s = Healthy(t); s.bandages = 0;
        Check(DecideRound(s).step == Step::End, "out of bandages ends it");
        s = Healthy(t); s.inParty = false;
        Check(DecideRound(s).step == Step::End, "leaving the party ends it");
        s = Healthy(t); s.kitsCompatible = false;
        Check(DecideRound(s).step == Step::End, "changing gear mid-meeting ends it");
        s = Healthy(t); s.consented = false;
        Check(DecideRound(s).step == Step::End, "no consent, no spar");
    }
    std::printf("[safety stop]\n");
    Check(!StopRound(59, 59, true, false, true, false), "59%%: above the owner's 40%% line, keep going");
    Check(StopRound(39, 100, true, false, true, false) && StopRound(100, 39, true, false, true, false),
          "either side below 40%% stops the round");
    Check(StopRound(100, 100, false, false, true, false), "stale health stops the round");
    Check(StopRound(100, 100, true, true, true, false), "a threat stops the round");
    Check(StopRound(100, 100, true, false, false, false), "out of reach stops the round");
    Check(StopRound(100, 100, true, false, true, true), "an expired lease stops the round");
    Check(kStartPercent > kStopPercent && kRoundMs > kRoundGapMs && kMeetingMs > kRoundMs,
          "tunables stay in a sane order");

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
