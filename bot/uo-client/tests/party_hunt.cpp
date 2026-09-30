// Party hunting decisions (uo/party_hunt.h), pure.
#include "uo/party_hunt.h"
#include "uo/social.h"
#include "uo/chatter.h"

#include <cstdio>

using namespace uo;
using namespace uo::party;

static int g_checks = 0, g_failures = 0;
static void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_failures; std::printf("  FAIL: %s\n", what); }
}

int main() {
    std::printf("[group size]\n");
    Check(DesiredGroupSize(15) == 2 && DesiredGroupSize(45) == 3 && DesiredGroupSize(95) == 5 &&
          DesiredGroupSize(200) == kMaxGroup, "a sociable character wants a bigger group, 2..5");

    std::printf("[where]\n");
    Check(ChooseGround(500, 2) == Ground::Graveyard, "novices hunt graveyards");
    Check(ChooseGround(700, 2) == Ground::Despise, "a capable pair goes to Despise");
    Check(ChooseGround(600, 4) == Ground::Despise, "four weaker hunters together can too");
    Check(ChooseGround(900, 2) == Ground::Despise && ChooseGround(900, 3) == Ground::Covetous,
          "Covetous needs skill AND a group of three");
    Check(GroundSuits(Ground::Despise, 650) && !GroundSuits(Ground::Despise, 500),
          "a joiner may be a little weaker than the bar, not far below it");
    Check(Invitation(Ground::Graveyard) == "Anyone for a graveyard hunt? Meet here.",
          "the graveyard call is the old phrase, byte for byte");
    Ground g = Ground::Graveyard;
    Check(ParseInvitation("Anyone for a Despise hunt? Meet here.", &g) && g == Ground::Despise, "a dungeon call parses");
    social::Activity a = social::Activity::None;
    Check(social::IsInvitation(Invitation(Ground::Covetous), &a) && a == social::Activity::Hunt,
          "and older listeners hear every hunt call as a hunt");
    Check(std::string(GroundRegion(Ground::Despise)) == "a_despise_level_1_1", "a trip goes to a dungeon's FIRST level");

    std::printf("[focus fire]\n");
    std::string m;
    Check(ParseFocusCall(FocusCall("a skeleton"), &m) && m == "a skeleton", "the leader's call round-trips");
    Check(!ParseFocusCall("hedef:", &m) && !ParseFocusCall("sa", &m), "not every line is a call");
    std::vector<Seen> hs = {{"a zombie", 10, 10}, {"a skeleton", 30, 30}, {"a skeleton", 12, 11}};
    Check(PickFocus(hs, "a skeleton", 11, 11, true) == 2, "the called monster nearest the leader");
    Check(PickFocus(hs, "", 11, 10, true) == 0, "no call: whatever stands in the fighting leader's reach");
    Check(PickFocus(hs, "", 11, 10, false) == -1, "a leader not fighting sets no focus");
    Check(PickFocus(hs, "a lich", 11, 11, true) == -1, "a called monster nobody sees sets none");

    std::printf("[healing]\n");
    std::vector<Member> ms = {{1, 90, 1}, {2, 50, 1}, {3, 30, 8}};
    Check(HealTarget(ms, Role::Tank, true, false) == 2, "a melee member bandages the lowest friend in reach");
    Check(HealTarget(ms, Role::Healer, true, true) == 3, "a healer reaches the lowest friend by spell");
    std::vector<Member> fine = {{1, 95, 1}, {2, 80, 1}};
    Check(HealTarget(fine, Role::Healer, true, true) == 0, "nobody hurt: keep fighting");
    Check(HealTarget(ms, Role::Tank, false, false) == 0, "no bandages, no spell: nothing to give");

    std::printf("[loot and roles]\n");
    Check(MyLootTurn(0, 0, 3) && !MyLootTurn(1, 0, 3) && MyLootTurn(1, 1, 3) && MyLootTurn(3, 0, 3),
          "kills are looted in turn round the roster");
    Check(MyLootTurn(7, 0, 1), "alone: every kill is ours");
    Check(RoleFor(2) == Role::Healer && RoleFor(1) == Role::Striker && RoleFor(0) == Role::Tank, "mage heals, archer strikes, melee tanks");
    Check(FollowDistance(Role::Tank) < FollowDistance(Role::Healer), "the caster keeps further back");
    Check(MayOpen(Role::Tank, false) && !MayOpen(Role::Healer, false) && MayOpen(Role::Striker, true),
          "only the tank opens; the rest wait for the leader to engage");

    std::printf("[who invites]\n");
    int hunts = 0;
    for (u32 s = 0; s < 300; ++s) hunts += PrefersHunt(true, s, 4);
    Check(hunts == 200, "a hunter asks for a hunt two sessions in three");
    Check(!PrefersHunt(false, 1, 1), "a non-hunter never does");
    bool collision = false;
    for (int topic = 0; topic < static_cast<int>(chatter::Topic::Count); ++topic) {
        const chatter::Lines l = chatter::LinesFor(static_cast<chatter::Topic>(topic));
        for (int i = 0; i < l.count; ++i) collision = collision || ParseFocusCall(l.items[i], &m);
    }
    Check(!collision, "no small-talk line reads as a target call");
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
