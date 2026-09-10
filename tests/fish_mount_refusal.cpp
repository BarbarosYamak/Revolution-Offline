// A mounted fisher must not have the water tile it cast at blacklisted just
// because the server refused the CHARACTER for being on a horse.
//
// fleet122d30_20260907: Cyreth, Ithion, Rhaladan and Selael (all mounted
// fishers) turned their whole 30-minute runs into ~1,000 shore hops, 0
// casts, 0 catches. Cyreth.console.txt:142-176 -- "fish: refused at
// 641,2228 (\"you can't fish while riding\") -- marking it dead", then a
// hop, then the identical refusal on the freshly-picked tile. The refusal
// is skill18_fishing.scp's @PreStart judging the RIDER, not the water
// Runner::DoFish just answered a target cursor for -- kRefusedHere folded
// the two together and blacklisted a tile that fishes fine on foot.
//
// The companion behaviour (Gather.cpp, this fix): Runner::DoFish now calls
// the same DismountToWork mining/logging already use
// (Runner::DismountToWork / RemountAfterWork, proven by
// TestDismountFinishesOnMountItemRemoval in trade_verify.cpp and by live
// mining/logging runs) before every cast, so the refusal below should be
// rare in practice; this test exercises the fallback path directly.
#include "Client.h"
#include "life/Runner.h"
#include "life/runner/RunnerInternal.h"
#include "uo/endian.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

using namespace uo;

// Friended in life/Runner.h ("friend struct RunnerHarnessAccess;"); this
// definition is local to this translation unit only, same as every other
// *_verify.cpp / *_harness.cpp test's own copy.
namespace uo::life {
struct RunnerHarnessAccess {
    static void SetGoal(Runner& r, GoalKind kind) {
        r.planner_.Mutable().kind = kind;
        r.nextActionMs_ = 0;
    }
    static void StartOnFoot(Runner& r) { r.gatherOnFoot_ = true; }
    static void SeedCraftRepeat(Runner& r) {
        r.makeLastIssued_ = true;
        r.makeLastRemaining_ = 33;
        r.craftItem_ = "i_fish_cut_cooked";
    }
    static void Leave(Runner& r, Client& c, bool sameKind) {
        r.LeaveGoal(c, GoalKind::Craft,
                    sameKind ? GoalKind::Craft : GoalKind::Fish,
                    sameKind, "test production handoff");
    }
    static bool CraftCleared(const Runner& r) {
        return !r.makeLastIssued_ && r.makeLastRemaining_ == 0 && r.craftItem_.empty();
    }
    static bool Craft(Runner& r, Client& c, const Observation& o) {
        return r.DoCraft(c, o);
    }
    static void SeedFoodResult(Runner& r, i64 journalMs) {
        r.state_.productionBatch.item = "i_fish_cut_cooked";
        r.state_.productionBatch.phase = ProductionPhase::Stock;
        r.craftItem_ = "i_fish_cut_cooked";
        r.craftHadBefore_ = 0;
        r.craftJournalMs_ = journalMs;
    }
    static void Run(Runner& r, Client& c, const Observation& o) {
        r.RunGoal(c, o);
    }
    static Observation Make(Runner& r, Client& c, i64 nowMs) {
        return r.Observe(c, nowMs);
    }
    static bool DoFishForTest(Runner& r, Client& c, const Observation& o) {
        return r.DoFish(c, o);
    }
    static void SetProfessionForTest(Runner& r, const prof::Profession* p) {
        r.needCfg_.profession = p;
    }
    // Drops DoFish straight into the "did the last cast resolve" branch
    // (Gather.cpp, the block gated on fishCastMs_ != 0) without needing a
    // dock trip to already be in flight. fishAtDock_ = true routes the
    // fallthrough into the "AT A DOCK" water search, which is null-safe
    // with no world loaded (Client::NearestWater/NearestFishingSpot both
    // bail out on !world_ before touching anything).
    static void SeedResolvedCast(Runner& r, i64 castMs, i64 journalMs,
                                 i32 waterX, i32 waterY) {
        r.fishCastMs_ = castMs;
        r.fishCursorPending_ = false;
        r.fishCastJournalMs_ = journalMs;
        r.fishX_ = waterX;
        r.fishY_ = waterY;
        r.fishAtDock_ = true;
    }
    static bool DeadTargetHas(const Runner& r, i32 x, i32 y) {
        for (const auto& d : r.deadTargets_)
            if (d.first == x && d.second == y) return true;
        return false;
    }
    static void CheckInterruptedFish(Runner& r, Client& c) {
        r.fishTrips_ = 3;
        r.fishAtDock_ = true;
        r.LeaveGoal(c, GoalKind::Fish, GoalKind::Bank, false, "bank preemption");
    }
    static bool FreshFishTrip(const Runner& r) {
        return r.fishTrips_ == 0 && !r.fishAtDock_;
    }
};
}  // namespace uo::life

using namespace uo::life;

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FAIL  %s\n", what);
    }
}
void Section(const char* name) { std::printf("[%s]\n", name); }

// 0x1B LOGIN_CONFIRM (Client.cpp OnLoginConfirm, >=18 bytes):
// serial(4) .. body@9(2) x@11(2) y@13(2) z@15(2) dir@17(1).
std::vector<u8> MakeLoginConfirm(u32 serial, u16 x, u16 y) {
    std::vector<u8> p(37, 0);
    p[0] = 0x1B;
    StoreBE32(&p[1], serial);
    StoreBE16(&p[9], 0x0190);
    StoreBE16(&p[11], x);
    StoreBE16(&p[13], y);
    return p;
}

// 0x2E EQUIP (Client.cpp OnEquip): item(4) graphic(2) [1]=0 layer(1) mobile(4).
std::vector<u8> MakeEquip(u32 item, u16 graphic, u8 layer, u32 mobile) {
    std::vector<u8> p(15, 0);
    p[0] = 0x2E;
    StoreBE32(&p[1], item);
    StoreBE16(&p[5], graphic);
    p[7] = 0;
    p[8] = layer;
    StoreBE32(&p[9], mobile);
    StoreBE16(&p[13], 0);
    return p;
}

// 0x1C ASCII_MESSAGE (Client.cpp OnAsciiMessage): serial(4) body(2) type(1)
// hue(2) font(2) name[30] text (NUL-terminated ASCII). Header is 44 bytes.
std::vector<u8> MakeAsciiMessage(u32 serial, const char* name, const char* text) {
    const usize textLen = std::strlen(text) + 1;
    std::vector<u8> p(44 + textLen, 0);
    p[0] = 0x1C;
    StoreBE32(&p[3], serial);
    p[9] = 0;
    if (name) {
        const usize n = std::strlen(name);
        std::memcpy(&p[14], name, n < 30 ? n : 30);
    }
    std::memcpy(&p[44], text, textLen);
    return p;
}

std::unique_ptr<Client> MakeConnectedClient(const char* tag) {
    Client::Config config{};
    config.loginHost = "127.0.0.1";
    config.username = config.password = tag;
    config.version = "2.0.7";
    config.sessionTag = tag;
    auto c = std::make_unique<Client>(config);
    c->SetOfflineForTest(true);
    c->SetInWorldForTest();
    c->SetClockForTest(1000000);
    return c;
}

// i_fishing_pole graphics (VendorPolicy.cpp kGraphics: 0x0DBF/0x0DC0), the
// same pair DoFish's own "no fishing pole" diagnostic names.
constexpr u16 kFishingPoleGfx = 0x0DBF;
constexpr u8  kLayerHand1 = 0x01;
constexpr u8  kLayerMount = 0x19;  // sphere::kLayerMount (25)

void TestRidingRefusalDoesNotBlacklistTheWater() {
    Section("fish: 'you can't fish while riding' leaves the water tile alive");

    auto client = MakeConnectedClient("fish_mount_riding");
    const u32 me   = 0x00019301;
    const u32 pole = 0x40041001;
    const u32 horse = 0x40041002;

    auto login = MakeLoginConfirm(me, 1465, 1751);
    client->DispatchPacketForTest(login.data(), login.size());
    auto poleEquip = MakeEquip(pole, kFishingPoleGfx, kLayerHand1, me);
    client->DispatchPacketForTest(poleEquip.data(), poleEquip.size());
    auto ride = MakeEquip(horse, 0x3E9F, kLayerMount, me);
    client->DispatchPacketForTest(ride.data(), ride.size());
    Check(client->PlayerIsMounted(), "the harness character is riding");

    prof::Profession fisherman;
    fisherman.id = "fisherman_test";
    Runner runner;
    RunnerHarnessAccess::SetProfessionForTest(runner, &fisherman);

    const i32 waterX = 1466, waterY = 1752;
    const i64 journalMs = client->JournalNowMs();
    auto refusal = MakeAsciiMessage(0xFFFFFFFF, "",
                                    "You can't fish while riding.");
    client->DispatchPacketForTest(refusal.data(), refusal.size());

    RunnerHarnessAccess::SeedResolvedCast(runner, 1000000, journalMs,
                                          waterX, waterY);
    Observation obs = RunnerHarnessAccess::Make(runner, *client, 1000500);
    Check(obs.mounted, "the observation agrees the character is mounted");

    RunnerHarnessAccess::DoFishForTest(runner, *client, obs);

    Check(!RunnerHarnessAccess::DeadTargetHas(runner, waterX, waterY),
          "the mount refusal must not blacklist the water it cast at");
}

void TestGenuineDeadWaterStillBlacklisted() {
    Section("fish: 'try fishing elsewhere' still marks the tile dead");

    auto client = MakeConnectedClient("fish_mount_elsewhere");
    const u32 me   = 0x00019302;
    const u32 pole = 0x40041003;

    auto login = MakeLoginConfirm(me, 1465, 1751);
    client->DispatchPacketForTest(login.data(), login.size());
    auto poleEquip = MakeEquip(pole, kFishingPoleGfx, kLayerHand1, me);
    client->DispatchPacketForTest(poleEquip.data(), poleEquip.size());
    Check(!client->PlayerIsMounted(), "the harness character is on foot");

    prof::Profession fisherman;
    fisherman.id = "fisherman_test";
    Runner runner;
    RunnerHarnessAccess::SetProfessionForTest(runner, &fisherman);

    const i32 waterX = 1466, waterY = 1752;
    const i64 journalMs = client->JournalNowMs();
    auto refusal = MakeAsciiMessage(0xFFFFFFFF, "", "Try fishing elsewhere.");
    client->DispatchPacketForTest(refusal.data(), refusal.size());
    auto failedRoll = MakeAsciiMessage(0xFFFFFFFF, "",
        "You fish a while, but fail to catch anything.");
    client->DispatchPacketForTest(failedRoll.data(), failedRoll.size());

    RunnerHarnessAccess::SeedResolvedCast(runner, 1000000, journalMs,
                                          waterX, waterY);
    Observation obs = RunnerHarnessAccess::Make(runner, *client, 1000500);

    RunnerHarnessAccess::DoFishForTest(runner, *client, obs);

    Check(RunnerHarnessAccess::DeadTargetHas(runner, waterX, waterY),
          "a depleted tile is blacklisted even with a generic failed-roll reply");
    RunnerHarnessAccess::CheckInterruptedFish(runner, *client);
    Check(RunnerHarnessAccess::FreshFishTrip(runner),
          "bank preemption does not consume the next fishing trip allowance");
}

void TestFishingDispatcherKeepsRiderOnFoot() {
    Section("fishing stays on foot until the goal ends");
    auto client = MakeConnectedClient("fish_dispatch_mount");
    const u32 me = 0x19303, horse = 0x19304;
    auto login = MakeLoginConfirm(me, 1465, 1751);
    client->DispatchPacketForTest(login.data(), login.size());
    auto pole = MakeEquip(0x40041005, kFishingPoleGfx, kLayerHand1, me);
    client->DispatchPacketForTest(pole.data(), pole.size());
    u8 mobile[23]{};
    mobile[0] = 0x78;
    StoreBE16(mobile + 1, sizeof(mobile));
    StoreBE32(mobile + 3, horse);
    StoreBE16(mobile + 7, 0x00C8);
    StoreBE16(mobile + 9, 1466);
    StoreBE16(mobile + 11, 1751);
    mobile[18] = 1;
    client->DispatchPacketForTest(mobile, sizeof(mobile));
    Check(client->NearestMobileWithBody(0x00C8, 3) == horse,
          "the dismounted horse is available to remount");
    prof::Profession fisherman;
    fisherman.id = "fisherman_test";
    Runner runner;
    RunnerHarnessAccess::SetProfessionForTest(runner, &fisherman);
    RunnerHarnessAccess::StartOnFoot(runner);
    RunnerHarnessAccess::SetGoal(runner, GoalKind::Fish);
    RunnerHarnessAccess::SeedResolvedCast(runner, 1000000,
                                        client->JournalNowMs(), 1466, 1752);
    client->ClearSentForTest();
    for (i64 now : {1001000LL, 1003000LL, 1005000LL}) {
        client->SetClockForTest(now);
        auto obs = RunnerHarnessAccess::Make(runner, *client, now);
        RunnerHarnessAccess::Run(runner, *client, obs);
    }
    auto clickedHorse = [&]() {
        for (const auto& packet : client->SentForTest())
            if (packet.opcode == 0x06 && packet.bytes.size() >= 5 &&
                LoadBE32(packet.bytes.data() + 1) == horse) return true;
        return false;
    };
    Check(!clickedHorse(), "waiting for a fish must not remount the horse");
    RunnerHarnessAccess::SetGoal(runner, GoalKind::IdleBriefly);
    auto obs = RunnerHarnessAccess::Make(runner, *client, 1007000);
    RunnerHarnessAccess::Run(runner, *client, obs);
    Check(clickedHorse(), "leaving fishing still remounts the nearby horse");
}

void TestCookingUsesDroppedKindlingAndStopsAfterFood() {
    auto client = MakeConnectedClient("fish_cooking_handoff");
    auto login = MakeLoginConfirm(0x19305, 1465, 1751);
    client->DispatchPacketForTest(login.data(), login.size());
    u8 ground[14]{};
    ground[0] = 0x1A;
    StoreBE16(ground + 1, sizeof(ground));
    StoreBE32(ground + 3, 0x40041006);
    StoreBE16(ground + 7, 0x0DE1);
    StoreBE16(ground + 9, 1465);
    StoreBE16(ground + 11, 1751);
    client->DispatchPacketForTest(ground, sizeof(ground));
    prof::Profession fisherman;
    fisherman.id = "fisherman_test";
    fisherman.produces = {"i_fish_cut_cooked"};
    Runner runner;
    RunnerHarnessAccess::SetProfessionForTest(runner, &fisherman);
    auto obs = RunnerHarnessAccess::Make(runner, *client, 1001000);
    obs.pack = {{"i_fish_cut_raw", 40}};
    Check(obs.cookingFuelNearby, "observation sees the dropped cooking fuel");
    auto choice = ChooseCraft(fisherman, obs, 5);
    Check(choice.item && choice.missing.empty(),
          "the planner keeps cooking after kindling leaves the backpack");
    ProductionBatch batch;
    batch.item = "i_fish_cut_cooked";
    batch.attempts = 100;
    const auto shopping = ProductionShopping(batch, obs);
    bool wantsKindling = false;
    for (const auto& missing : shopping.missing)
        if (std::string(missing.item) == "i_kindling") wantsKindling = true;
    Check(!wantsKindling, "production shopping does not replace available ground fuel");
    auto away = obs;
    away.cookingFuelNearby = false;
    choice = ChooseCraft(fisherman, away, 5);
    Check(choice.missing.size() == 1 && choice.missing[0].qty == 1,
          "away from the fire, a cooking batch needs one kindling, not one per steak");
    client->ClearSentForTest();
    RunnerHarnessAccess::Craft(runner, *client, obs);
    bool lit = false;
    for (const auto& p : client->SentForTest())
        if (p.opcode == 0x06 && LoadBE32(p.bytes.data() + 1) == 0x40041006) lit = true;
    Check(lit, "cooking lights dropped kindling instead of shopping for another piece");

    // A confirmed fire and food result during stocking must not start makelast.
    StoreBE16(ground + 7, 0x0DE3);
    client->DispatchPacketForTest(ground, sizeof(ground));
    RunnerHarnessAccess::SeedFoodResult(runner, client->JournalNowMs());
    obs.pack.push_back({"i_fish_cut_cooked", 1});
    client->ClearSentForTest();
    Check(RunnerHarnessAccess::Craft(runner, *client, obs),
          "one confirmed meal finishes the cooking interruption during stocking");
    bool spoke = false;
    for (const auto& p : client->SentForTest())
        if (p.opcode == 0xAD || p.opcode == 0x03) spoke = true;
    Check(!spoke, "preparing food during stocking does not launch a bulk command");
}

}  // namespace

int main() {
    TestCookingUsesDroppedKindlingAndStopsAfterFood();
    for (bool war : {false, true}) {
        auto client = MakeConnectedClient("fish_craft_repeat");
        if (war) client->ActionWarMode(true);
        client->ClearSentForTest();
        Runner runner;
        RunnerHarnessAccess::SeedCraftRepeat(runner);
        RunnerHarnessAccess::Leave(runner, *client, true);
        Check(client->SentForTest().empty(), "same craft goal preserves the server repeat");
        RunnerHarnessAccess::Leave(runner, *client, false);
        const auto& packets = client->SentForTest();
        Check(packets.size() == 2 && packets[0].opcode == 0x72 &&
              packets[1].opcode == 0x72 && packets[0].bytes[1] == !war &&
              packets[1].bytes[1] == war,
              "leaving craft cancels the shard repeat and restores stance");
        Check(RunnerHarnessAccess::CraftCleared(runner), "the next sitting cannot inherit the old repeat");
        Check(client->WarModeOn() == war, "the handoff preserves the original war mode");
        client->ClearSentForTest();
        RunnerHarnessAccess::Leave(runner, *client, false);
        Check(client->SentForTest().empty(), "repeat cancellation is issued only once");
    }
    TestRidingRefusalDoesNotBlacklistTheWater();
    TestGenuineDeadWaterStillBlacklisted();
    TestFishingDispatcherKeepsRiderOnFoot();
    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    if (g_failures == 0) std::printf("OK\n");
    return g_failures == 0 ? 0 : 1;
}
