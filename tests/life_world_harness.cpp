// The real life handlers and Client against a small atlas and navigation grid.
// Position changes arrive through the normal server packet dispatcher. This
// covers region decisions and journey ownership, not MUL tile movement.
#include "Client.h"
#include "life/Runner.h"
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
    }
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
