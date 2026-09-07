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
    // --- the bandage WTB hand-off (runner/Gear.cpp) ------------------------
    // The stand-down needs no Client: it reads the Observation, the plan and
    // the planner, and hands off. That is exactly the seam the defect lived
    // in, so the regression drives it directly.
    static bool StandDownBandages(Runner& runner, const Observation& obs) {
        return runner.StandDownBandageShopping(obs, "no bandages bought",
                                               30000);
    }
    static GoalKind NextPick(Runner& runner, const std::vector<Need>& needs,
                             const Observation& obs) {
        std::string why;
        runner.planner_.Select(needs, obs, runner.state_.memory, obs.nowMs,
                               &why);
        return runner.planner_.Current().kind;
    }
    static bool Cooling(const Runner& runner, GoalKind kind, i64 nowMs) {
        return runner.planner_.Cooling(kind, nowMs);
    }
    static i64 BandageWtbAskedMs(const Runner& runner) {
        return runner.bandageWtbAskedMs_;
    }
    static const prof::Profession* ProfessionOf(const Runner& runner) {
        return runner.needCfg_.profession;
    }
    static void MakeSessionEnding(Runner& runner, i64 nowMs) {
        runner.cfg_.sessionLimitMs = 1000;
        runner.sessionStartMs_ = nowMs - 1000;
    }
    // Drops straight into Phase::WindDown the way EndSession() would, without
    // needing a live Phase::Live tick (goal picking, needs, planner) first --
    // the defect and its fix are entirely inside the WindDown case, so the
    // regression for it has no business depending on any of that machinery.
    static void EnterWindDown(Runner& runner, i64 nowMs) {
        runner.phase_ = Runner::Phase::WindDown;
        runner.windDownStartedMs_ = nowMs;
        runner.windDownTrips_ = 0;
        runner.windDownArrived_ = false;
        runner.windDownBlockedLogged_ = false;
        runner.windDownStuckCycles_ = 0;
        runner.windDownUnsafeLogout_ = false;
        runner.travelInFlight_ = false;
        runner.windDownLastX_ = -1;
        runner.windDownLastY_ = -1;
        runner.windDownMovedMs_ = 0;
        runner.lastTickMs_ = nowMs;
        if (runner.sessionStartMs_ == 0) runner.sessionStartMs_ = nowMs;
    }
    static void SetWindDownArrivedForTest(Runner& runner, bool v) {
        runner.windDownArrived_ = v;
    }
    static bool IsLoggingOut(const Runner& runner) {
        return runner.phase_ == Runner::Phase::LoggingOut;
    }
    // Forces the wind-down deadline to already be behind us (past the grace
    // period too, so the "still moving" sentinel from a fresh EnterWindDown
    // cannot buy the longer budget by accident) without waiting the real
    // 2-5 minutes out in simulated clock. Kharazar-shaped regression: a spot
    // with no known safe ground reachable, deadline already blown.
    static void ForceWindDownOutOfTime(Runner& runner, i64 nowMs) {
        runner.windDownStartedMs_ =
            nowMs - (Runner::kWindDownGraceMs + 60000);
    }
    static i32 WindDownStuckCycles(const Runner& runner) {
        return runner.windDownStuckCycles_;
    }
    static bool WindDownForcedUnsafe(const Runner& runner) {
        return runner.windDownUnsafeLogout_;
    }
    static bool SessionCleanLogout(const Runner& runner) {
        return runner.session_.cleanLogout;
    }
    // --- S6: the market is the home-town bank (Economy.cpp) ----------------
    static void SetHomeCity(Runner& runner, const std::string& city) {
        runner.state_.homeCity = city;
    }
    static std::string ResolveMarketPlaceId(Runner& runner, Client& client) {
        return runner.ResolveHomeMarketPlaceId(client);
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

// A 0x78 Mobile Incoming with an empty equipment list -- enough to register a
// hostile in Client::ScanHostiles (mobileCache_), which the wind-down handler
// reads directly and independently of Runner::Observe's override seam.
void SpawnHostile(Client& client, u32 serial, u16 x, u16 y, u8 noto) {
    u8 packet[23]{};
    packet[0] = 0x78;
    StoreBE16(packet + 1, sizeof(packet));
    StoreBE32(packet + 3, serial);
    StoreBE16(packet + 7, 0x0190);  // body: any nonzero graphic
    StoreBE16(packet + 9, x);
    StoreBE16(packet + 11, y);
    packet[13] = 0;                 // z
    packet[14] = 0;                 // dir
    StoreBE16(packet + 15, 0);      // hue
    packet[17] = 0;                 // status flags
    packet[18] = noto;              // notoriety: 3 = gray, hostile-eligible
    // bytes 19..22 are the zero-serial equipment-list terminator
    client.DispatchPacketForTest(packet, sizeof(packet));
}

// 0xD1 is the only thing Sphere's CClient::CharDisconnect ever sees as "this
// session asked to end" (Client::ActionLogout's own comment). Asserting on
// the wire, not on phase_, because phase_ alone cannot tell "logged out" from
// "about to log out and then loop back".
bool LogoutIssued(const Client& client) {
    for (const auto& p : client.SentForTest())
        if (p.opcode == 0xD1) return true;
    return false;
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
                 "PLACE\thealer\thealer\ttown\t50\t50\t0\t3\thealer\t\tTown Healer\n"
                 // A guarded PLACE with no matching RECT: PlaceIsGuarded says
                 // yes (it is filed under a guarded region by id), but
                 // CurrentRegion() at a tile a few steps away resolves to the
                 // unguarded "world" catch-all -- exactly the Minoc Mine 1
                 // mismatch that looped Morven, Rhaler and Kharain
                 // (fleet122_20260907): the atlas calls the landmark guarded,
                 // the ground under a bot standing next to it is not.
                 "REGION\tminocgate\tdungeon\t1\t450\t450\t0\tMinocGate\tMinocGate\n"
                 "PLACE\tminocmine\tlandmark\tminocgate\t450\t450\t0\t5\t\t\tMinoc Mine 1\n"
                 // A SECOND HOME TOWN, for the home-town-bank resolver (S6):
                 // Runner::ResolveHomeMarketPlaceId must land a Minoc-homed
                 // character on THIS bank and a Britain-homed one on "bank"
                 // above, never on either by coincidence of distance.
                 "REGION\ttownminoc\ttown\t1\t490\t490\t0\tMinoc\tMinoc\n"
                 "RECT\ttownminoc\t480\t480\t500\t500\n"
                 "PLACE\tminoc_bank\tbank\ttownminoc\t490\t490\t0\t5\t"
                     "banker\t\tMinoc Bank\n";
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
        // === a dry-counter fighter really asks the market for bandages =====
        //
        // gate_bandage20_20260907: 16 fighters chose ASK_PLAYERS, 0 ever said
        // `WTB ... i_bandage`. HandOff's `to` is advice (runner/Core.cpp), and
        // MAKE_BANDAGES (0.25 + 0.45 x shortfall) out-scores NeedTrade's buy
        // arm (0.15 + 0.40 x frac) at the same weight 145 past ~2/3 shortfall,
        // so the very next pick was always the scissors -- Baelos 13:05:15,
        // 83.2 against 79.8. This pins the fix at the seam it broke at: after
        // the hand-off tick, the planner's own choice is the trade.
        if (std::string(family) == "fencer") {
            life::Runner wtb;
            life::RunnerConfig wc = rc;
            wc.dataRoot = root + "/" + family + "_wtb";
            wc.characterName = "wtb_fencer";
            std::string werr;
            if (!wtb.Configure(wc, &werr)) {
                Check(false, werr.c_str());
            } else {
                Check(life::RunnerHarnessAccess::ProfessionOf(wtb) != nullptr,
                      "the fixture fighter has a profession to buy with");
                life::Observation atBank;
                atBank.inWorld = true;
                atBank.nowMs = 2000000;
                atBank.x = atBank.y = 40;          // the fixture's guarded town
                atBank.hp = atBank.hpMax = 50;
                atBank.gold = 7845;                // Baelos's own purse
                atBank.bandages = 28;              // 28/100: dry counters
                atBank.atBank = true;

                // The errand that fails first is the shop run, exactly as in
                // the gate: REPLACE_EQUIPMENT owns the tick when the town's
                // counters turn out to be empty.
                life::Need gear;
                gear.kind = life::NeedKind::NeedEquipment;
                gear.what = "bandages";
                gear.urgency = 0.80;
                life::Need make;
                make.kind = life::NeedKind::NeedMakeBandages;
                make.what = "bandages";
                make.urgency = 0.57;               // 145 x 0.57 = 83.2
                life::Need buy;
                buy.kind = life::NeedKind::NeedTrade;
                buy.what = "buy from a player";
                buy.urgency = 0.55;                // 145 x 0.55 = 79.8
                Check(life::RunnerHarnessAccess::NextPick(
                          wtb, {gear, make, buy}, atBank) ==
                          life::GoalKind::ReplaceEquipment,
                      "the shop run owns the tick before the counters run dry");

                Check(!life::RunnerHarnessAccess::StandDownBandages(wtb, atBank),
                      "the dry-counter stand-down ends the shop run");
                Check(life::RunnerHarnessAccess::BandageWtbAskedMs(wtb) == 0,
                      "deciding to ask is not asking: no WTB clock starts at "
                      "the hand-off");
                atBank.nowMs += 2000;              // HandOff's own nextActionMs_
                Check(life::RunnerHarnessAccess::Cooling(
                          wtb, life::GoalKind::MakeBandages, atBank.nowMs),
                      "the WTB window rests MAKE_BANDAGES so the ask can win");
                Check(life::RunnerHarnessAccess::NextPick(
                          wtb, {make, buy}, atBank) ==
                          life::GoalKind::TradeWithPlayer,
                      "the pick after the hand-off is TRADE_WITH_PLAYER, not "
                      "MAKE_BANDAGES");

                // ... and it is not vetoed on the way out. 246 s was what
                // Baelos had left when an 800 s flat charge refused him a walk
                // he was not making (Baelos.console.txt:1128).
                const i64 announceCycle = 6 * 8000;          // kMaxAnnounces x
                const i64 windDown      = 2 * 60 * 1000;     // kWindDownBudgetMs
                Check(life::MarketTripNeedMs(0, announceCycle, windDown) <=
                          246000,
                      "asking at the bank you are standing in fits 246 s of "
                      "session");
                Check(life::MarketTripNeedMs(1136, announceCycle, windDown) >
                          246000,
                      "and the 1,136 tiles to the rendezvous still do not");

                // === S7: the "0 tiles" above is what the RESOLVER actually
                // produces for this character, not a number asserted on
                // faith. Before S6 this was always minoc_bank, 1,500+ tiles
                // from every Britain-homed character's own counter; S6's own
                // home-town rule is superseded by S7's two hubs (owner
                // ruling 2026-09-07). Standing AT a home bank makes it the
                // trivially cheapest of the two hubs to reach -- the same
                // "measured from wherever the character actually stands"
                // rule that decides every other trip in this file.
                life::RunnerHarnessAccess::SetHomeCity(wtb, "Britain");
                Position(*client, 40, 40);
                const std::string britainMarket =
                    life::RunnerHarnessAccess::ResolveMarketPlaceId(wtb, *client);
                Check(britainMarket == "bank",
                      "standing at the Britain bank, the two-hub resolver "
                      "still picks Britain -- it is zero tiles away");
                if (const wm::Place* p = client->KnownPlace(britainMarket.c_str())) {
                    Check(p->position.x == atBank.x && p->position.y == atBank.y,
                          "standing at the Britain bank IS standing at this "
                          "character's own resolved market -- zero tiles");
                }

                life::RunnerHarnessAccess::SetHomeCity(wtb, "Minoc");
                Position(*client, 490, 490);
                Check(life::RunnerHarnessAccess::ResolveMarketPlaceId(wtb, *client) ==
                          "minoc_bank",
                      "and standing at the Minoc bank, the same resolver "
                      "picks Minoc -- miners at their own counter are not "
                      "sent 450 tiles to Britain");

                life::RunnerHarnessAccess::SetHomeCity(wtb, "Nowhereville");
                Position(*client, 40, 40);
                Check(life::RunnerHarnessAccess::ResolveMarketPlaceId(wtb, *client) ==
                          "bank",
                      "an unrecognised home city is irrelevant to a resolver "
                      "that never reads it any more -- from the Britain "
                      "bank, Britain is still the nearer of the two hubs");

                // The window can only close on evidence about sellers. A
                // character that never spoke has learned nothing.
                Check(std::string(life::PlanBandageSupply(
                          life::RunnerHarnessAccess::ProfessionOf(wtb), 7845,
                          false, false, /*waitedOut=*/false,
                          /*couldNotAsk=*/true).why)
                          .find("no seller came") == std::string::npos,
                      "'no seller came' cannot be reported before an announce");
                Check(std::string(life::PlanBandageSupply(
                          life::RunnerHarnessAccess::ProfessionOf(wtb), 7845,
                          false, false, /*waitedOut=*/true).why)
                          .find("no seller came") != std::string::npos,
                      "a WTB that was spoken and ran out still reports it");
            }
        }

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
    // AND THE OTHER HALF OF IT (owner ruling the same day, option a): Alder
    // lives in Trinsic, took an ordinary moongate errand to Skara Brae for
    // UPGRADE_GEAR, and RETURN_HOME fired on "1207 tiles from Bank of
    // Britannia - Trinsic Branch banker" and dragged him home mid-errand
    // (artifacts/alder_home_20260907/Alder.console.txt 12:22). Straight-line
    // distance is not the way home; ordinary players travel between towns.
    //
    // The atlas rows below are COPIED from data/revolution_atlas.txt, tabs,
    // coordinates and all -- the three town AREADEFs with their real flags
    // and the rectangles that hold the banks, the Trinsic branch and Papua
    // bank ROOMDEFs (both REGION_FLAG_GUARDED, flags 5), a_papua_4's flags
    // (none), the four bank places, and every moongate row that joins the
    // Moonglow, Britain, Skara Brae and Trinsic pads. Nothing here is
    // invented.
    {
        uo::world_atlas::Atlas atlas;
        std::string err;
        const char* rows =
            "MAP\t0\t7168\t4096\n"
            "REGION\ta_world\tworld\t0\t1323\t1624\t55\tALLMAP\tFelucca\n"
            "RECT\ta_world\t0\t0\t7167\t4095\n"
            "REGION\ta_townBritain\ttown\t1\t1495\t1629\t10\tBritain\tBritain\n"
            "RECT\ta_townBritain\t1410\t1517\t1690\t1777\n"
            "REGION\ta_townTrinsic\ttown\t1\t1867\t2780\t0\tTrinsic\tTrinsic\n"
            "RECT\ta_townTrinsic\t1795\t2792\t2069\t2874\n"
            "REGION\ta_bankritannia_trinsic_branch_1\tbuilding\t5\t1816\t2821\t0\t"
                "Trinsic\tBank of Britannia - Trinsic Branch\n"
            "RECT\ta_bankritannia_trinsic_branch_1\t1808\t2818\t1818\t2838\n"
            "REGION\ta_townSkaraBrae\ttown\t1\t632\t2233\t0\tSkara Brae\tSkara Brae\n"
            "RECT\ta_townSkaraBrae\t541\t2108\t644\t2226\n"
            "REGION\ta_papua_4\twilderness\t0\t5729\t3209\t-1\tPapua\tPapua\n"
            "RECT\ta_papua_4\t5633\t3088\t5742\t3328\n"
            "REGION\ta_olde_loan_savings_1\tbuilding\t5\t5675\t3136\t14\tPapua\t"
                "Ye Olde Loan & Savings\n"
            "RECT\ta_olde_loan_savings_1\t5658\t3121\t5681\t3140\n"
            "PLACE\tbritain_bank\tbank\ta_townBritain\t1650\t1608\t20\t5\t"
                "banker\t\tBritain banker\n"
            "PLACE\tskara_brae_bank\tbank\ta_townSkaraBrae\t587\t2146\t0\t5\t"
                "banker\t\tSkara Brae banker\n"
            "PLACE\tbank_of_britannia_trinsic_branch_bank\tbank\t"
                "a_bankritannia_trinsic_branch_1\t1813\t2825\t0\t5\t"
                "banker\t\tBank of Britannia - Trinsic Branch banker\n"
            "PLACE\tpapua_bank\tbank\ta_papua_4\t5669\t3131\t14\t5\t"
                "banker\t\tPapua minter\n"
            "TRANSIT\tmg_moonglow__britain\tmoongate\t4467\t1283\t5\t1336\t1997\t5\t0\tBritain\n"
            "TRANSIT\tmg_moonglow__trinsic\tmoongate\t4467\t1283\t5\t1828\t2948\t-20\t0\tTrinsic\n"
            "TRANSIT\tmg_moonglow__skara_brae\tmoongate\t4467\t1283\t5\t643\t2067\t5\t0\tSkara Brae\n"
            "TRANSIT\tmg_britain__trinsic\tmoongate\t1336\t1997\t5\t1828\t2948\t-20\t0\tTrinsic\n"
            "TRANSIT\tmg_britain__skara_brae\tmoongate\t1336\t1997\t5\t643\t2067\t5\t0\tSkara Brae\n"
            "TRANSIT\tmg_skara_brae__britain\tmoongate\t643\t2067\t5\t1336\t1997\t5\t0\tBritain\n"
            "TRANSIT\tmg_skara_brae__trinsic\tmoongate\t643\t2067\t5\t1828\t2948\t-20\t0\tTrinsic\n"
            // S7 (two-hub market): Minoc and Vesper, COPIED the same way --
            // a_townMinoc/minoc_bank and a_townVesper/vesper_bank are
            // data/revolution_atlas.txt:953/2184 and :768/2317 verbatim
            // (RECTs omitted: NearestPlaceWithServiceInRegion matches a place
            // filed under a region by id, and PlaceIsGuarded reads the
            // region's own flags -- neither needs the rectangles here). The
            // Trinsic/Minoc and Britain/Minoc moongate pairs are
            // :3913/:3917/:3926/:3880 -- this shard's public network is a
            // complete graph over its pads (RunnerShared.cpp
            // TravelTilesWithGates), so every city the resolver compares
            // against Minoc needs its own real edge to it, not just to
            // Britain.
            "REGION\ta_townMinoc\ttown\t1\t2466\t544\t0\tMinoc\tMinoc\n"
            "PLACE\tminoc_bank\tbank\ta_townMinoc\t2503\t552\t0\t5\t"
                "banker\t\tMinoc banker\n"
            // Britain's SECOND bank (:2109), closer to the AREADEF centre
            // (1495,1629) than britain_bank is -- 70 tiles against 155 --
            // which is why RegionBank("Britain") (the same anchor S6 used)
            // resolves the Britain hub to THIS one, not britain_bank. Left
            // out, the Trinsic and Vesper numbers below would be measured
            // against the wrong Britain bank.
            "PLACE\tbritain_bank_2\tbank\ta_townBritain\t1425\t1690\t0\t5\t"
                "banker\t\tBritain banker\n"
            "REGION\ta_townVesper\ttown\t1\t2899\t676\t0\tVesper\tVesper\n"
            "PLACE\tvesper_bank\tbank\ta_townVesper\t2881\t684\t0\t5\t"
                "banker\t\tVesper banker\n"
            "TRANSIT\tmg_minoc__britain\tmoongate\t2701\t692\t5\t1336\t1997\t5\t0\tBritain\n"
            "TRANSIT\tmg_britain__minoc\tmoongate\t1336\t1997\t5\t2701\t692\t5\t0\tMinoc\n"
            "TRANSIT\tmg_trinsic__minoc\tmoongate\t1828\t2948\t-20\t2701\t692\t5\t0\tMinoc\n"
            "TRANSIT\tmg_minoc__trinsic\tmoongate\t2701\t692\t5\t1828\t2948\t-20\t0\tTrinsic\n"
            // The direct Trinsic pad -> Britain pad edge (:3922), the
            // reverse of mg_britain__trinsic already above. Without it the
            // only walk-to-a-gate route TravelTilesWithGates can find from
            // Trinsic bank toward Britain bank is the accidental one through
            // the MINOC pad (Trinsic pad -> Minoc's arrival tile -> overland
            // to Britain), which is a worse number than the real direct
            // gate and would understate what Britain actually costs.
            "TRANSIT\tmg_trinsic__britain\tmoongate\t1828\t2948\t-20\t1336\t1997\t5\t0\tBritain\n";
        Check(atlas.LoadFromText(rows, &err), "stranded fixture atlas loads");

        // (1) THE PAPUA BANK -- the tile Alder and Kharazar actually log in
        // on, and STILL stranded after the errand fix.
        const life::runner_detail::HomeReturn lost =
            life::runner_detail::ResolveHomeReturn(&atlas, "Britain", 5674, 3134);
        Check(lost.resolved && !lost.inHome,
              "a character at the Papua bank is not in its home region");
        Check(lost.x == 1650 && lost.y == 1608,
              "and the way home ends at the Britain BANK, not at the AREADEF "
              "centre -- the bank is where a player's life is kept");
        Check(lost.directTiles == 4024,
              "4,024 tiles from home in a straight line");
        Check(lost.tiles == 2240,
              "and 2,240 even taking the nearest moongate (1,851 tiles to the "
              "Moonglow pad, 389 on from the Britain pad) -- the Lost Lands "
              "have no public gate of their own");
        Check(lost.tiles > uo::life::kStrandedFromHomeTiles,
              "which is what makes it stranded rather than merely away");
        Check(!lost.onErrandGround,
              "and the guarded ROOMDEF it is standing in (a_olde_loan_savings_1, "
              "flags 5) does not make the Lost Lands an errand: the test is a "
              "guarded TOWN region, and Papua has none");

        // (2) THE PAPUA STREET -- unguarded wilderness, same verdict. The
        // per-tile guard flag flaps between (1) and (2); the town test does
        // not, which is the whole reason it is town-wide.
        const life::runner_detail::HomeReturn street =
            life::runner_detail::ResolveHomeReturn(&atlas, "Britain", 5729, 3209);
        Check(street.resolved && !street.onErrandGround &&
                  street.directTiles == 4079 && street.tiles == 2315 &&
                  street.tiles > uo::life::kStrandedFromHomeTiles,
              "unguarded wilderness four thousand tiles out is stranded too");

        // (3) SKARA BRAE, HOME TRINSIC -- Alder's errand. Guarded town ground
        // the moongate network reaches, and the way home is one gate.
        const life::runner_detail::HomeReturn errand =
            life::runner_detail::ResolveHomeReturn(&atlas, "Trinsic", 587, 2146);
        Check(errand.resolved && !errand.inHome,
              "standing at the Skara Brae bank is not standing in Trinsic");
        Check(errand.x == 1813 && errand.y == 2825,
              "and home is the Trinsic branch bank");
        Check(errand.directTiles == 1226,
              "the straight line is 1,226 tiles -- the figure that called an "
              "errand a stranding");
        Check(errand.tiles == 202,
              "but the way a player travels it is 79 tiles to the Skara pad "
              "and 123 on from the Trinsic pad: 202");
        Check(errand.tiles < uo::life::kStrandedFromHomeTiles,
              "which is an errand, not a stranding");
        Check(errand.onErrandGround,
              "and Skara Brae is a guarded town the gate network reaches, so "
              "the need is suppressed however far home turns out to be");

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

        // --- S7: the market has two hubs -- Britain bank and Minoc bank,
        // nearer one wins (owner ruling 2026-09-07). ResolveMarketHub is the
        // pure atlas function Runner::ResolveHomeMarketPlaceId calls; tested
        // directly here the same way ResolveHomeReturn is above, against real
        // coordinates -- INCLUDING britain_bank_2 (:2109, 1425,1690), 70
        // tiles from the AREADEF centre against britain_bank's 155, which is
        // why RegionBank("Britain") (S6's own anchor) picks IT as the
        // Britain hub. Every number below was checked against a standalone
        // run of ResolveMarketHub over the actual data/revolution_atlas.txt,
        // not derived by hand.
        {
            // Standing at each hub's own bank, that hub is zero tiles away
            // and trivially wins.
            const life::runner_detail::MarketHubPick atBritain =
                life::runner_detail::ResolveMarketHub(&atlas, 1425, 1690);
            Check(atBritain.resolved && atBritain.placeId == "britain_bank_2" &&
                      atBritain.tiles == 0,
                  "standing at the Britain hub, Britain wins at zero tiles");

            const life::runner_detail::MarketHubPick atMinoc =
                life::runner_detail::ResolveMarketHub(&atlas, 2503, 552);
            Check(atMinoc.resolved && atMinoc.placeId == "minoc_bank" &&
                      atMinoc.tiles == 0,
                  "standing at the Minoc bank, Minoc wins at zero tiles");

            // Trinsic-homed, standing at its own bank (1813, 2825 --
            // bank_of_britannia_trinsic_branch_bank, the same tile
            // ResolveHomeReturn's own Trinsic case above uses).
            //
            // THIS CONTRADICTS THE BRIEF'S OWN ASSUMPTION ("Trinsic-homed at
            // Trinsic bank -> Britain"). By the real atlas, Minoc is
            // cheaper: both hubs are one moongate hop from the Trinsic pad
            // (123 tiles to walk to it either way -- mg_trinsic__minoc and
            // mg_trinsic__britain share the same "from"), so the difference
            // is entirely the OTHER end of the hop. Minoc's own bank sits
            // 198 tiles from Minoc's pad; britain_bank_2 sits 307 from
            // Britain's. 123+198=321 beats 123+307=430. This shard's
            // moongate network is a complete graph (RunnerShared.cpp
            // TravelTilesWithGates), so Trinsic can gate to Minoc directly
            // without detouring through Britain -- confirmed both here and
            // by an ad hoc ResolveMarketHub run against the live
            // data/revolution_atlas.txt (321 / 430, exact match). The live
            // smoke (Alder, Castor) logged britain_bank_2 instead only
            // because both characters' saved positions were already inside
            // Britain when the resolver ran, not because they were standing
            // at their own Trinsic bank -- see this brief's own report for
            // the console evidence.
            const life::runner_detail::MarketHubPick trinsic =
                life::runner_detail::ResolveMarketHub(&atlas, 1813, 2825);
            Check(trinsic.resolved && trinsic.placeId == "minoc_bank" &&
                      trinsic.tiles == 321 && trinsic.otherTiles == 430,
                  "a Trinsic-homed character AT ITS OWN BANK resolves to "
                  "Minoc (321 tiles) over Britain (430) -- the real atlas' "
                  "answer, not the brief's assumed one");
            Check(life::MarketTripNeedMs(trinsic.tiles, 6 * 8000,
                                        2 * 60 * 1000) <= 600000,
                  "and that trip is not vetoed with 600 s of session left");

            // Vesper-homed, standing at its own bank (2881, 684). Vesper has
            // no public moongate of its own in this atlas or in
            // data/revolution_atlas.txt; the nearest gate pad at all is
            // Minoc's, 180 tiles from the Vesper bank. Vesper->Minoc is the
            // raw walk (378, cheaper than detouring through the gate to
            // reach the very town it sits next to); Vesper->Britain is
            // 180+307=487 via that same pad. Minoc wins again, confirmed the
            // same way against the live atlas (378 / 487, exact match).
            const life::runner_detail::MarketHubPick vesper =
                life::runner_detail::ResolveMarketHub(&atlas, 2881, 684);
            Check(vesper.resolved && vesper.placeId == "minoc_bank" &&
                      vesper.tiles == 378 && vesper.otherTiles == 487,
                  "a Vesper-homed character's cheaper hub is Minoc, 378 "
                  "tiles against Britain's 487 -- whichever the atlas says, "
                  "not assumed either way");

            // No home city at all: the resolver never reads one (unlike S6),
            // so an unrecognised or absent home city is simply irrelevant --
            // it always ranks the two hubs from wherever (x, y) is.
            const life::runner_detail::MarketHubPick fromNowhere =
                life::runner_detail::ResolveMarketHub(&atlas, 1425, 1690);
            Check(fromNowhere.resolved && fromNowhere.placeId == "britain_bank_2",
                  "an unknown home city is moot -- the nearest of the two "
                  "hubs from wherever the character stands still resolves");
        }
    }
    // --- wind-down regression: guarded ground, hostile merely in scan ------
    // fleet122_20260907: Morven, Rhaler and Kharain looped ~9,300 times each
    // between "running for guarded ground at Minoc Mine 1" and "arrived
    // somewhere safe" without ever logging out, because a hostile still in
    // scan (never attacking, full HP) kept safeHere false and CallGuards-
    // IfProtected kept failing (the guard polygon does not reach two tiles
    // out), so the same zero-distance travel restarted every tick.
    {
        Client::Config config{};
        config.loginHost = "127.0.0.1";
        config.username = config.password = "offline_world";
        config.version = "2.0.7";
        config.sessionTag = "winddown_guard";
        config.atlasPath = atlasPath.c_str();
        config.navgridPath = gridPath.c_str();
        auto client = std::make_unique<Client>(config);
        client->SetOfflineForTest(true);
        client->SetInWorldForTest();
        client->SetClockForTest(1000000);

        life::Runner runner;
        life::RunnerConfig rc;
        rc.dataRoot = root + "/winddown_guard";
        rc.accountName = "offline_world";
        rc.characterName = "winddown_guard";
        rc.professionId = "fencer";
        std::string error;
        Check(runner.Configure(rc, &error), error.c_str());

        // Two tiles from the "Minoc Mine 1" landmark -- inside the travel
        // radius used to reach it, but not inside its own guard polygon.
        Check(client->WorldKnowledgeReady(), "real Client loads atlas and grid");
        Position(*client, 452, 450);
        Check(client->CurrentRegion() && !client->CurrentRegion()->flags.guarded,
              "standing near the guarded landmark, not inside its own polygon");

        SpawnHostile(*client, 0x40100001u, 453, 450, 3);
        std::vector<Client::HostileHit> seen;
        Check(client->ScanHostiles(12, seen) == 1 && !seen[0].warMode,
              "the hostile is merely in scan, not flagged for war");

        life::RunnerHarnessAccess::EnterWindDown(runner, 1000000);
        int ticks = 0;
        for (; ticks < 10 && !life::RunnerHarnessAccess::IsLoggingOut(runner);
             ++ticks) {
            runner.Tick(*client, 1000000 + ticks * 100);
        }
        Check(life::RunnerHarnessAccess::IsLoggingOut(runner),
              "wind-down logs out from guarded ground with a hostile merely "
              "in scan, rather than looping forever");
        Check(ticks <= 1,
              "already inside the guarded landmark's radius resolves in one "
              "tick -- no re-run of the same zero-distance travel");
        Check(LogoutIssued(*client), "an 0xD1 logout request reached the wire");
    }

    // --- wind-down regression: "arrived somewhere safe" must not repeat ----
    // fleet122_20260907: Dorvar's wind-down reached its chosen destination
    // (a banker at Buccaneer's Den, unguarded and never personally learned
    // as a bank) and printed "arrived somewhere safe" every ~30s for the
    // rest of the session because safeHere's own region/bank-memory/bank-box
    // checks all failed there on re-evaluation. Arriving must be trusted.
    {
        Client::Config config{};
        config.loginHost = "127.0.0.1";
        config.username = config.password = "offline_world";
        config.version = "2.0.7";
        config.sessionTag = "winddown_arrived";
        config.atlasPath = atlasPath.c_str();
        config.navgridPath = gridPath.c_str();
        auto client = std::make_unique<Client>(config);
        client->SetOfflineForTest(true);
        client->SetInWorldForTest();
        client->SetClockForTest(1000000);

        life::Runner runner;
        life::RunnerConfig rc;
        rc.dataRoot = root + "/winddown_arrived";
        rc.accountName = "offline_world";
        rc.characterName = "winddown_arrived";
        rc.professionId = "fencer";
        std::string error;
        Check(runner.Configure(rc, &error), error.c_str());

        // Unguarded wilderness, far from the fixture's only (guarded) bank
        // and from the Minoc Mine 1 landmark, with nothing hostile in sight --
        // exactly what safeHere's region/bank-memory/bank-box checks see at
        // Dorvar's Buccaneer's Den banker: none of them true.
        Check(client->WorldKnowledgeReady(), "real Client loads atlas and grid");
        Position(*client, 200, 450);
        Check(client->CurrentRegion() && !client->CurrentRegion()->flags.guarded,
              "the Dorvar-shaped tile is not itself guarded ground");

        // The wind-down's own arrival bookkeeping already ran once (its
        // travelInFlight_ resolution is unchanged by this fix and is not
        // under test here) and set windDownArrived_ -- the fact this
        // regression is about is what the NEXT tick does with that fact.
        life::RunnerHarnessAccess::EnterWindDown(runner, 1000000);
        life::RunnerHarnessAccess::SetWindDownArrivedForTest(runner, true);
        Check(!LogoutIssued(*client),
              "logout has not been requested before the wind-down even ticks");

        runner.Tick(*client, 1000000);
        Check(life::RunnerHarnessAccess::IsLoggingOut(runner),
              "the tick after arrival trusts it and logs out -- no repeat "
              "'arrived somewhere safe' loop");
        Check(LogoutIssued(*client), "an 0xD1 logout request reached the wire");
    }

    // --- wind-down regression: a session past its limit always ends -------
    // Kharazar (2026-09-07, 11:03): wind-down ran return_home for 300s, hit
    // "no safe logout from 5925,3621 ... wind-down deadline", and then just
    // sat in survival ticks with no further logout attempt until the process
    // was killed by hand. No known bank, no route the atlas can offer, no
    // hostile in the way -- safeHere never turns true and the old code
    // retried the same silent cycle forever. One more retreat lap is worth
    // it (never logging out is worse than trying again once); a second lap
    // that is just as unsafe means the pocket is sealed and the session must
    // end anyway, on the wire, right where it stands.
    {
        Client::Config config{};
        config.loginHost = "127.0.0.1";
        config.username = config.password = "offline_world";
        config.version = "2.0.7";
        config.sessionTag = "winddown_unsafe";
        config.atlasPath = atlasPath.c_str();
        config.navgridPath = gridPath.c_str();
        auto client = std::make_unique<Client>(config);
        client->SetOfflineForTest(true);
        client->SetInWorldForTest();
        client->SetClockForTest(2000000);

        life::Runner runner;
        life::RunnerConfig rc;
        rc.dataRoot = root + "/winddown_unsafe";
        rc.accountName = "offline_world";
        rc.characterName = "winddown_unsafe";
        rc.professionId = "fencer";
        std::string error;
        Check(runner.Configure(rc, &error), error.c_str());

        // Unguarded wilderness, no bank this fresh life has learned, nothing
        // hostile in scan -- safeHere resolves false on every one of its
        // checks, exactly the dead end Kharazar's session hit.
        Check(client->WorldKnowledgeReady(), "real Client loads atlas and grid");
        Position(*client, 200, 200);
        Check(client->CurrentRegion() && !client->CurrentRegion()->flags.guarded,
              "standing in unguarded wilderness with nothing hostile nearby");
        std::vector<Client::HostileHit> seen;
        Check(client->ScanHostiles(12, seen) == 0,
              "nothing to fight and nothing to hide from -- the dead end is "
              "purely about there being no safe ground to reach");

        life::RunnerHarnessAccess::EnterWindDown(runner, 2000000);
        life::RunnerHarnessAccess::ForceWindDownOutOfTime(runner, 2000000);
        runner.Tick(*client, 2000000);
        Check(!life::RunnerHarnessAccess::IsLoggingOut(runner),
              "the first dead-end tick spends its one extra retreat lap "
              "rather than giving up immediately");
        Check(!LogoutIssued(*client),
              "no logout on the wire yet -- one more lap was owed first");
        Check(life::RunnerHarnessAccess::WindDownStuckCycles(runner) == 1,
              "the retreat lap is counted so a second dead end cannot loop "
              "forever the way Kharazar's session did");

        // Same dead end again, well past the reset grace window: the second
        // lap is exactly as unsafe as the first, so this must be the one
        // that ends the session instead of resetting for a third try.
        life::RunnerHarnessAccess::ForceWindDownOutOfTime(runner, 2100000);
        runner.Tick(*client, 2100000);
        Check(life::RunnerHarnessAccess::IsLoggingOut(runner),
              "a session past its limit always ends -- the second dead-end "
              "lap forces the logout instead of retrying a third time");
        Check(LogoutIssued(*client), "an 0xD1 logout request reached the wire");
        Check(life::RunnerHarnessAccess::WindDownForcedUnsafe(runner),
              "the forced logout is recorded as unsafe, not as an ordinary "
              "clean wind-down");
        Check(!life::RunnerHarnessAccess::SessionCleanLogout(runner),
              "the session summary must not read this dead end as a clean "
              "logout");
    }

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
