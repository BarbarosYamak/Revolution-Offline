// The real life handlers and Client against a small atlas and navigation grid.
// Position changes arrive through the normal server packet dispatcher. This
// covers region decisions and journey ownership, not MUL tile movement.
#include "Client.h"
#include "life/Runner.h"
// The RETURN_HOME resolver both halves of the pair share
// (runner_detail::ResolveHomeReturn); see the stranded block in main().
#include "life/runner/RunnerInternal.h"
#include "world/Atlas.h"
#include "world/NavGrid.h"
#include "uo/endian.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace uo;

namespace uo::life {
struct RunnerHarnessAccess {
    static void Retreat(Runner& runner, Client& client) {
        runner.RetreatToSafety(client);
    }
    static void Recover(Runner& runner, Client& client, const Observation& obs,
                        RecoveryStep previous) {
        runner.lastRecoveryPlan_ = previous;
        runner.travelInFlight_ = true;
        runner.DoRecoverCorpse(client, obs);
    }
    // The per-tick keeper Tick() runs before any goal (Core.cpp).
    static void GuardKeeper(Runner& runner, Client& client,
                            const Observation& obs) {
        runner.KeepCallingGuards(client, obs);
    }
    static bool Retreating(const Runner& runner) { return runner.survivalRetreat_; }
    static bool Heal(Runner& runner, Client& client, const Observation& obs) {
        return runner.DoHeal(client, obs);
    }
    static void LeaveGoal(Runner& runner, Client& client, GoalKind from,
                          GoalKind to) {
        runner.LeaveGoal(client, from, to, from == to, "harness");
    }
    static RestStep RestStepForTest(Runner& runner, Client& client,
                                    const Observation& obs) {
        runner.RestTick(client, obs, GoalKind::IdleBriefly);
        return runner.lastRestPlan_;
    }
    static void MakeSessionEnding(Runner& runner, i64 nowMs) {
        runner.cfg_.sessionLimitMs = 1000;
        runner.sessionStartMs_ = nowMs - 1000;
    }
};
}

namespace {
int checks = 0, failures = 0;
void Check(bool ok, const char* reason) {
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL: %s\n", reason); }
}

void Position(Client& client, u16 x, u16 y) {
    u8 packet[19]{};
    packet[0] = 0x20;
    StoreBE32(packet + 1, client.PlayerSerial());
    StoreBE16(packet + 5, 0x0190);
    StoreBE16(packet + 11, x);
    StoreBE16(packet + 13, y);
    client.DispatchPacketForTest(packet, sizeof(packet));
}

void OpenBank(Client& client, u32 serial) {
    u8 packet[7]{};
    packet[0] = 0x24;
    StoreBE32(packet + 1, serial);
    StoreBE16(packet + 5, 0x004A);
    client.ActionOpenBank(0, "bank");
    client.DispatchPacketForTest(packet, sizeof(packet));
}

// How many "Guards!" shouts the client actually put on the wire (0x03 ascii
// speech). Asserting on the sent packet, not on a log line, because the
// packet is the only thing Sphere's guardcall keyword ever sees.
int GuardShouts(const Client& client) {
    int n = 0;
    for (const auto& p : client.SentForTest()) {
        if (p.opcode != 0x03) continue;
        const std::string body(reinterpret_cast<const char*>(p.bytes.data()),
                               p.bytes.size());
        if (body.find("Guards!") != std::string::npos) ++n;
    }
    return n;
}
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string root = std::string(argv[1]) + "/life_world";
    std::filesystem::create_directories(root);
    const std::string atlasPath = root + "/atlas.txt";
    const std::string gridPath = root + "/grid.bin";
    {
        std::ofstream atlas(atlasPath);
        atlas << "MAP\t0\t512\t512\n"
                 "REGION\tworld\tworld\t0\t256\t256\t0\tWorld\tWorld\n"
                 "RECT\tworld\t0\t0\t512\t512\n"
                 "REGION\ttown\ttown\t1\t40\t40\t0\tBritain\tBritain\n"
                 "RECT\ttown\t20\t20\t80\t80\n"
                 "REGION\tpit\tdungeon\t8C\t400\t400\t0\tPit\tPit\n"
                 "RECT\tpit\t380\t380\t420\t420\n"
                 "PLACE\tbank\tbank\ttown\t40\t40\t0\t5\tbanker\t\tTown Bank\n"
                 "PLACE\thealer\thealer\ttown\t50\t50\t0\t3\thealer\t\tTown Healer\n";
    }
    std::vector<navgrid::Cell> cells(32 * 32);
    const int delta[8][2] = {{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}};
    for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
        auto& cell = cells[y * 32 + x];
        cell.anchorOffX = cell.anchorOffY = 8;
        cell.flags = navgrid::kCellPassable;
        for (int d = 0; d < 8; ++d) {
            int nx = x + delta[d][0], ny = y + delta[d][1];
            if (nx >= 0 && nx < 32 && ny >= 0 && ny < 32) cell.edges |= 1u << d;
        }
    }
    navgrid::NavGrid grid;
    Check(grid.Adopt(32, 32, cells.data()) && grid.Save(gridPath.c_str()), "fixture grid saved");

    for (const char* family : {"fencer", "mage", "warlock", "merchant_tinker"}) {
        Client::Config config{};
        config.loginHost = "127.0.0.1";
        config.username = config.password = "offline_world";
        config.version = "2.0.7";
        config.sessionTag = family;
        config.atlasPath = atlasPath.c_str();
        config.navgridPath = gridPath.c_str();
        auto client = std::make_unique<Client>(config);
        client->SetOfflineForTest(true);
        client->SetInWorldForTest();
        client->SetClockForTest(1000000);
        Check(client->WorldKnowledgeReady(), "real Client loads atlas and grid");
        Position(*client, 200, 200);
        Check(client->CurrentRegion() && !client->CurrentRegion()->flags.guarded,
              "server position selects unguarded wilderness");
        const auto* bank = client->NearestServicePlace(wm::Service::Banker);
        Check(bank && client->PlaceGuarded(*bank), "retreat destination is a guarded bank");

        client->ActionApplyPoison(0x40000010, 0x40000011);
        u8 cursor[19]{};
        cursor[0] = 0x6C;
        StoreBE32(cursor + 2, 1);
        client->DispatchPacketForTest(cursor, sizeof(cursor));
        Check(client->CurrentAction().awaitingTarget &&
              client->CurrentAction().destination == 0x40000011,
              "poisoning answers weapon cursor and waits for potion cursor");
        StoreBE32(cursor + 2, 2);
        client->DispatchPacketForTest(cursor, sizeof(cursor));
        Check(!client->CurrentAction().awaitingTarget,
              "poisoning answers second cursor without resending the weapon");
        client->CompleteActionForTest(act::Result::Success, "poison applied");

        life::Runner runner;
        life::RunnerConfig rc;
        rc.dataRoot = root + "/" + family;
        rc.accountName = "offline_world";
        rc.characterName = family;
        rc.professionId = family;
        std::string error;
        if (!runner.Configure(rc, &error)) { Check(false, error.c_str()); continue; }
        Check(client->TravelToPoint(400, 400, 2, "old errand"), "old errand starts");
        life::RunnerHarnessAccess::Retreat(runner, *client);
        Check(client->TravelBusy(), "retreat replaces the old errand with a live journey");

        // Hector and Aurelius: a FLEE began a banker trip, its attackers
        // cleared for one observation, and the planner picked TRAIN_COMBAT.
        // That pick must not cancel survival's live journey; HEAL also has to
        // leave it alone while the escape is in flight.
        life::Observation quiet;
        quiet.inWorld = true;
        quiet.nowMs = 1000000;
        quiet.hp = quiet.hpMax = 88;
        life::RunnerHarnessAccess::LeaveGoal(
            runner, *client, life::GoalKind::Survive, life::GoalKind::TrainCombat);
        Check(client->TravelBusy(),
              "TRAIN_COMBAT does not abort a banker retreat after attackers clear");
        Check(!life::RunnerHarnessAccess::Heal(runner, *client, quiet),
              "HEAL yields while a quiet survival retreat is still travelling");
        Check(client->TravelBusy() && life::RunnerHarnessAccess::Retreating(runner),
              "the quiet HEAL tick preserves the escape journey and its latch");
        Position(*client, 40, 40);
        Check(client->CurrentRegion() && client->CurrentRegion()->flags.guarded,
              "server arrival selects guarded town");
        client->TravelAbort("fixture arrival");

        // D14 remainder: a retreat that CROSSES the town line must shout.
        // Tordor (2026-09-06 03:12:17-03:13:09) fled from outside a guard
        // zone, reached one, and died inside a_townBritain without ever
        // saying the word -- because the only two callers of
        // CallGuardsIfProtected are decision arms that ran once, on the tile
        // he fled FROM. The keeper runs every tick instead.
        {
            life::Observation flee;
            flee.inWorld = true;
            flee.nowMs = 1000000;
            flee.hp = 40; flee.hpMax = 100;
            flee.hostilesNear = 3;
            flee.attackersOnMe = 1;
            flee.underAttack = true;

            client->ClearSentForTest();
            Position(*client, 200, 200);
            flee.x = flee.y = 200;
            Check(life::RunnerHarnessAccess::Retreating(runner),
                  "the survival retreat is still in flight");
            life::RunnerHarnessAccess::GuardKeeper(runner, *client, flee);
            Check(GuardShouts(*client) == 0,
                  "out in the wilderness there is nobody to shout to");

            Position(*client, 40, 40);
            flee.x = flee.y = 40;
            life::RunnerHarnessAccess::GuardKeeper(runner, *client, flee);
            Check(GuardShouts(*client) == 1,
                  "crossing into the guarded town with a hostile still on "
                  "him, the retreating character calls the guards");

            // Same second, same tick shape: the throttle -- not the decision
            // -- is what stops this becoming a packet per tick.
            life::RunnerHarnessAccess::GuardKeeper(runner, *client, flee);
            flee.nowMs += 5000;
            life::RunnerHarnessAccess::GuardKeeper(runner, *client, flee);
            Check(GuardShouts(*client) == 1,
                  "five seconds later it is still one shout, not three");

            // Nothing in sight and unhurt: a stale retreat flag is not a
            // reason to shout at an empty street.
            life::Observation calm = flee;
            calm.nowMs += 60000;
            calm.hp = calm.hpMax;
            calm.hostilesNear = 0;
            calm.attackersOnMe = 0;
            calm.underAttack = false;
            life::RunnerHarnessAccess::GuardKeeper(runner, *client, calm);
            Check(GuardShouts(*client) == 1,
                  "safe and whole again, the shouting stops");
            client->ClearSentForTest();
        }

        life::Observation obs;
        obs.inWorld = true;
        obs.nowMs = 1000000;
        obs.x = obs.y = 40;
        obs.hp = 12; obs.hpMax = 100;
        obs.corpseKnown = true;
        client->Knowledge().NoteDeath(200, 200, 0, "world", obs.nowMs - 1000);
        Check(client->TravelToLastCorpse(), "corpse return starts through real travel API");
        life::RunnerHarnessAccess::Recover(runner, *client, obs, life::RecoveryStep::TravelToCorpse);
        Check(!client->TravelBusy(), "wounded recovery cancels the corpse journey");
        Check(client->TravelToService(wm::Service::Healer), "medical journey uses atlas healer");
        life::RunnerHarnessAccess::Recover(runner, *client, obs, life::RecoveryStep::Recover);
        Check(client->TravelBusy(), "another recovery tick preserves the medical journey");
        client->TravelAbort("fixture complete");
        Position(*client, 400, 400);
        Check(client->CurrentRegion() && client->CurrentRegion()->flags.underground &&
              client->CurrentRegion()->flags.BlocksRecallOut(),
              "dungeon flags are read from the atlas after leaving town");

        // A remembered bank serial is not a safe place to log out: Sphere
        // accepts bank touches only from the tile where that gump opened.
        Position(*client, 200, 200);
        OpenBank(*client, 0x40014400u);
        Check(client->BankContainer() != 0 && client->BankOpenTileHeld(),
              "bank safety begins on the tile that opened the box");
        Position(*client, 201, 200);
        Check(!client->BankOpenTileHeld(),
              "one step leaves a stale bank serial without bank safety");
        life::Observation logout;
        logout.inWorld = true;
        logout.nowMs = 1000000;
        logout.hp = logout.hpMax = 100;
        life::RunnerHarnessAccess::MakeSessionEnding(runner, logout.nowMs);
        Check(life::RunnerHarnessAccess::RestStepForTest(runner, *client, logout) ==
                  life::RestStep::Settle,
              "rest does not treat an off-tile bank container as logout safety");
    }
    // --- STRANDED ON THE WRONG FACET (owner ruling 2026-09-07) -------------
    //
    // Alder and Kharazar logged in at the Papua bank and could not leave: the
    // Lost Lands are four thousand tiles from the Britain bank they call home
    // and every service lookup made from Papua answers with a Papua provider.
    // Observe and DoReturnHome must agree about where home is and how far it
    // is, so both go through ONE resolver and this is the test of it.
    //
    // The atlas rows below are COPIED from data/revolution_atlas.txt, tabs,
    // coordinates and all -- a_townBritain's flags (guarded) and first
    // rectangle, a_papua_4's flags (none) and first rectangle, and the two
    // bank places. Nothing here is invented.
    {
        uo::world_atlas::Atlas atlas;
        std::string err;
        const char* rows =
            "MAP\t0\t7168\t4096\n"
            "REGION\ta_world\tworld\t0\t1323\t1624\t55\tALLMAP\tFelucca\n"
            "RECT\ta_world\t0\t0\t7167\t4095\n"
            "REGION\ta_townBritain\ttown\t1\t1495\t1629\t10\tBritain\tBritain\n"
            "RECT\ta_townBritain\t1410\t1517\t1690\t1777\n"
            "REGION\ta_papua_4\twilderness\t0\t5729\t3209\t-1\tPapua\tPapua\n"
            "RECT\ta_papua_4\t5633\t3088\t5742\t3328\n"
            "PLACE\tbritain_bank\tbank\ta_townBritain\t1650\t1608\t20\t5\t"
                "banker\t\tBritain banker\n"
            "PLACE\tpapua_bank\tbank\ta_papua_4\t5669\t3131\t14\t5\t"
                "banker\t\tPapua minter\n";
        Check(atlas.LoadFromText(rows, &err), "stranded fixture atlas loads");

        // The tile Alder and Kharazar actually log in on.
        const life::runner_detail::HomeReturn lost =
            life::runner_detail::ResolveHomeReturn(&atlas, "Britain", 5674, 3134);
        Check(lost.resolved && !lost.inHome,
              "a character at the Papua bank is not in its home region");
        Check(lost.x == 1650 && lost.y == 1608,
              "and the way home ends at the Britain BANK, not at the AREADEF "
              "centre -- the bank is where a player's life is kept");
        Check(lost.tiles == 4024,
              "the distance the need scores on is the distance the errand "
              "walks: 4,024 tiles, well past the service trip budget");
        Check(lost.tiles > uo::life::kStrandedFromHomeTiles,
              "which is what makes it stranded rather than merely away");

        // Standing on the home bank tile: home, and zero to go.
        const life::runner_detail::HomeReturn athome =
            life::runner_detail::ResolveHomeReturn(&atlas, "Britain", 1650, 1608);
        Check(athome.resolved && athome.inHome && athome.tiles == 0,
              "at the Britain bank the character is home and the need dies");

        // Arm A of the need/handler contract: no home city, and a home city
        // the atlas has never heard of, both leave the errand with nowhere to
        // walk -- so `resolved` is false and Observe leaves homeKnown false.
        Check(!life::runner_detail::ResolveHomeReturn(&atlas, "", 5674, 3134)
                   .resolved,
              "no home city means no errand, so the need may not score");
        Check(!life::runner_detail::ResolveHomeReturn(&atlas, "Atlantis",
                                                      5674, 3134)
                   .resolved,
              "nor does a home city this atlas does not know");
        Check(!life::runner_detail::ResolveHomeReturn(nullptr, "Britain",
                                                      5674, 3134)
                   .resolved,
              "nor a character whose world knowledge has not loaded");
    }

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
