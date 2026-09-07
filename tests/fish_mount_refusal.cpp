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

    RunnerHarnessAccess::SeedResolvedCast(runner, 1000000, journalMs,
                                          waterX, waterY);
    Observation obs = RunnerHarnessAccess::Make(runner, *client, 1000500);

    RunnerHarnessAccess::DoFishForTest(runner, *client, obs);

    Check(RunnerHarnessAccess::DeadTargetHas(runner, waterX, waterY),
          "a genuine dead tile ('try fishing elsewhere') is still blacklisted");
}

}  // namespace

int main() {
    TestRidingRefusalDoesNotBlacklistTheWater();
    TestGenuineDeadWaterStillBlacklisted();
    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    if (g_failures == 0) std::printf("OK\n");
    return g_failures == 0 ? 0 : 1;
}
