// Two owner rulings of 2026-09-07, each with a live death or a wasted
// session behind it.
//
// A. EVERY MINE TRIP GOES TO MINOC. "Britain-area mines are off the list for
//    now" -- so a miner who lives in Britain must still plan the journey to
//    Minoc, the way Kharain already does from his own home
//    (run_gates/g_Kharain.console.txt 00:44-00:55, moongate loop, 34 ingots).
//    The picker this guards is runner_detail::PickAllowedMine, which reads
//    the atlas allow-list kMinocMinePlaceIds and nothing about where the
//    character is standing -- the old selection took the hinted ore lead
//    whose LABEL carried the home city's name, which is exactly how a
//    Britain resident was sent to Brit Mine1.
//
// B. WIND-DOWN TRAVEL MUST NEVER ROUTE THROUGH HOSTILES. Odessa asked to end
//    her session at 00:54:08 on 2026-09-07, had learned no bank, took the
//    "asking the world for one" leg, walked north through the orc camp and
//    was killed at (1448,1375) at 00:55:57. No goal runs in Phase::WindDown,
//    DoSurvive included, so the destination itself has to be the safe one:
//    the nearest ground a guard will answer on, before any bank.
//
// Section A is pure atlas geometry (a local Atlas, no Client). Section B
// drives the REAL Runner through Phase::WindDown against the REAL Client on
// a small fixture atlas and navigation grid, with the Observation scripted --
// the same three injections tests/life_world_harness.cpp uses.

#include "Client.h"
#include "life/Runner.h"
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
// The friend hook Runner.h declares. Wind-down is a PHASE, not a goal, so
// there is no handler to call directly: the test sets the phase the same way
// EndSession does and lets Tick() run it.
struct RunnerHarnessAccess {
    static void EnterWindDown(Runner& runner, i64 startedMs) {
        runner.phase_ = Runner::Phase::WindDown;
        runner.windDownStartedMs_ = startedMs;
        runner.windDownTrips_ = 0;
        runner.windDownArrived_ = false;
        runner.windDownBlockedLogged_ = false;
        runner.travelInFlight_ = false;
    }
    // The bank-trip counter. Only the bank/known-safe arm advances it, so
    // "a journey is in flight and this is still zero" is exactly "a leg was
    // started, and it was not the bank leg".
    static i32 WindDownTrips(const Runner& runner) { return runner.windDownTrips_; }
};
}  // namespace uo::life

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

bool IsAllowedMineId(const char* id) {
    for (const char* allowed : life::runner_detail::kMinocMinePlaceIds)
        if (std::string(allowed) == id) return true;
    return false;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::printf("usage: mine_and_winddown <source-data-dir> <scratch-dir>\n");
        return 2;
    }
    const std::string dataDir = argv[1];
    const std::string scratch = std::string(argv[2]) + "/mine_and_winddown";
    std::filesystem::create_directories(scratch);

    // --- A. the mine allow-list --------------------------------------------
    {
        world_atlas::Atlas real;
        std::string err;
        Check(real.Load((dataDir + "/revolution_atlas.txt").c_str(), &err),
              "the generated atlas loads");

        // The premise of the ruling: this atlas really does offer Britain
        // mines, so a nearest-first picker standing in Britain would take one.
        Check(real.PlaceById("brit_mine1_resource_area") != nullptr,
              "the atlas offers Brit Mine1 -- the row the ruling rejects");

        const char* id = nullptr;
        const wm::Place* pick = life::runner_detail::PickAllowedMine(&real, &id);
        Check(pick != nullptr, "the allow-list resolves against the real atlas");
        Check(pick && IsAllowedMineId(id), "the resolved id is an allow-listed one");
        Check(id && std::string(id).find("minoc") != std::string::npos,
              "a Britain-home merchant_tinker is sent to a Minoc mine id");
        Check(pick && pick->Yields(wm::ResourceKind::Mining),
              "the chosen place actually yields ore");
        // Minoc Mine 1 is a_minoc_mine_1_1, RECTs 2556,474..2581,503.
        Check(pick && pick->position.x > 2400 && pick->position.y < 700,
              "the chosen place sits in Minoc, not in the Britain basin");

        // A synthetic atlas where the Britain mine is nearer to everything
        // proves the picker never consults distance at all.
        world_atlas::Atlas synth;
        const char* text =
            "MAP\t0\t4096\t4096\n"
            "REGION\ta_britain\ttown\t1\t1440\t1690\t0\tBritain\tBritain\n"
            "RECT\ta_britain\t1400\t1600\t1500\t1750\n"
            "REGION\ta_brit_mine1_1\tcave\t0\t1443\t1228\t0\tMines\tBrit Mine1\n"
            "RECT\ta_brit_mine1_1\t1436\t1215\t1464\t1251\n"
            "REGION\ta_minoc_mine_1_1\tcave\t4\t2558\t499\t0\tMines\tMinoc Mine 1\n"
            "RECT\ta_minoc_mine_1_1\t2556\t474\t2581\t503\n"
            "PLACE\tbrit_mine1_resource_area\tresource_area\ta_brit_mine1_1\t"
            "1443\t1228\t0\t20\t\tmining\tBrit Mine1\n"
            "PLACE\tminoc_mine_1_resource_area\tresource_area\ta_minoc_mine_1_1\t"
            "2558\t499\t0\t20\t\tmining\tMinoc Mine 1\n";
        Check(synth.LoadFromText(text, &err), "synthetic two-mine atlas loads");
        const char* synthId = nullptr;
        const wm::Place* synthPick =
            life::runner_detail::PickAllowedMine(&synth, &synthId);
        Check(synthPick && std::string(synthId) == "minoc_mine_1_resource_area",
              "with a nearer Britain mine on the map the pick is still Minoc");

        // No allow-listed row at all is a DATA fault, and the picker says so
        // by returning nothing rather than quietly handing back Brit Mine1 --
        // DoMine's caller logs "Minoc only -> unavailable" on this arm.
        world_atlas::Atlas britOnly;
        const char* britText =
            "MAP\t0\t4096\t4096\n"
            "REGION\ta_brit_mine1_1\tcave\t0\t1443\t1228\t0\tMines\tBrit Mine1\n"
            "RECT\ta_brit_mine1_1\t1436\t1215\t1464\t1251\n"
            "PLACE\tbrit_mine1_resource_area\tresource_area\ta_brit_mine1_1\t"
            "1443\t1228\t0\t20\t\tmining\tBrit Mine1\n";
        Check(britOnly.LoadFromText(britText, &err), "Britain-only atlas loads");
        const char* noneId = nullptr;
        Check(life::runner_detail::PickAllowedMine(&britOnly, &noneId) == nullptr,
              "an atlas without a Minoc mine resolves to nothing, not to Britain");
        Check(life::runner_detail::PickAllowedMine(nullptr, &noneId) == nullptr,
              "no atlas resolves to nothing");
    }

    // --- B. wind-down with a hostile in scan --------------------------------
    //
    // The fixture has TWO guarded regions: a nearby watch post with no
    // services, and a far city holding the only bank. The pre-ruling code
    // walked to the bank; the ruling says the guard line comes first.
    const std::string atlasPath = scratch + "/atlas.txt";
    const std::string gridPath = scratch + "/grid.bin";
    {
        std::ofstream atlas(atlasPath);
        atlas << "MAP\t0\t512\t512\n"
                 "REGION\tworld\tworld\t0\t256\t256\t0\tWorld\tWorld\n"
                 "RECT\tworld\t0\t0\t512\t512\n"
                 "REGION\tpost\ttown\t1\t240\t240\t0\tWatch\tWatch Post\n"
                 "RECT\tpost\t220\t220\t260\t260\n"
                 "REGION\tcity\ttown\t1\t40\t40\t0\tBritain\tBritain\n"
                 "RECT\tcity\t20\t20\t80\t80\n"
                 "PLACE\tpost_square\tlandmark\tpost\t240\t240\t0\t3\t\t\tWatch Post\n"
                 "PLACE\tcity_bank\tbank\tcity\t40\t40\t0\t5\tbanker\t\tCity Bank\n";
    }
    {
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
        Check(grid.Adopt(32, 32, cells.data()) && grid.Save(gridPath.c_str()),
              "fixture grid saved");
    }

    Client::Config config{};
    config.loginHost = "127.0.0.1";
    config.username = config.password = "winddown";
    config.version = "2.0.7";
    config.sessionTag = "winddown";
    config.atlasPath = atlasPath.c_str();
    config.navgridPath = gridPath.c_str();
    auto client = std::make_unique<Client>(config);
    client->SetOfflineForTest(true);
    client->SetInWorldForTest();
    i64 nowMs = 1'000'000;
    client->SetClockForTest(nowMs);
    Check(client->WorldKnowledgeReady(), "real Client loads the fixture world");

    // The nearest guarded ground from the wilderness is the watch post, not
    // the city that holds the bank. This is the choice the ruling turns on.
    {
        const wm::Place* guarded = life::runner_detail::NearestGuardedPlace(
            client->WorldAtlas(), 200, 200);
        Check(guarded && guarded->id == "post_square",
              "nearest guarded ground from the wilderness is the watch post");
        const wm::Place* bank =
            client->WorldAtlas()->NearestPlaceWithService(wm::Service::Banker,
                                                          200, 200);
        Check(bank && bank->id == "city_bank" &&
                  life::runner_detail::TileDist(bank->position.x,
                                                bank->position.y, 200, 200) >
                      life::runner_detail::TileDist(240, 240, 200, 200),
              "the only bank is further off than that guarded ground");
    }

    life::Runner runner;
    life::RunnerConfig rc;
    rc.dataRoot = scratch + "/state";
    rc.accountName = "winddown";
    rc.characterName = "winddown";
    rc.professionId = "merchant_tinker";
    rc.sessionLimitMs = 0;
    std::string error;
    if (!runner.Configure(rc, &error)) {
        Check(false, error.c_str());
        std::printf("%d checks, %d failures\n", checks, failures);
        return 1;
    }

    life::Observation obs;
    obs.inWorld = true;
    obs.nowMs = nowMs;
    obs.x = obs.y = 200;
    obs.hp = 40; obs.hpMax = 50;
    runner.SetObservationOverrideForTest(&obs);

    // (1) A hostile in scan out in the open: the leg must be the guarded one.
    Position(*client, 200, 200);
    Check(client->CurrentRegion() && !client->CurrentRegion()->flags.guarded,
          "the character starts on unguarded ground");
    obs.hostilesNear = 3;
    life::RunnerHarnessAccess::EnterWindDown(runner, nowMs - 1);
    nowMs += 500;
    client->SetClockForTest(nowMs);
    obs.nowMs = nowMs;
    runner.Tick(*client, nowMs);
    Check(client->TravelBusy(), "a wind-down with a hostile in scan does travel");
    Check(life::RunnerHarnessAccess::WindDownTrips(runner) == 0,
          "the leg started is the guarded one -- no bank trip was counted");
    client->TravelAbort("fixture");

    // (2) The same tick with nothing hostile in sight takes the ordinary
    // bank route -- the ruling narrows wind-down under threat, it does not
    // replace it.
    obs.hostilesNear = 0;
    life::RunnerHarnessAccess::EnterWindDown(runner, nowMs - 1);
    nowMs += 500;
    client->SetClockForTest(nowMs);
    obs.nowMs = nowMs;
    runner.Tick(*client, nowMs);
    Check(client->TravelBusy(), "an unthreatened wind-down still heads for safety");
    Check(life::RunnerHarnessAccess::WindDownTrips(runner) == 1,
          "with nothing in sight it is the ordinary bank trip again");
    client->TravelAbort("fixture");

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
