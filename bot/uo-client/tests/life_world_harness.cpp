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
#include "uo/sparring.h"
#include "uo/json.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace uo;

namespace uo::life {
struct RunnerHarnessAccess {
    static void SocialObserve(Runner& r, Client& c, const Observation& o) { r.ObserveSocial(c, o); }
    static void SeedSparring(Runner& r, i64 now) {
        r.state_.identity.characterName = "Student";
        r.socialPeer_ = 0x1002; r.socialPeerName_ = "Teacher";
        r.socialActivity_ = social::Activity::Spar;
        r.socialConsented_ = r.sparActive_ = true;
        r.socialStartedMs_ = now; r.sessionStartMs_ = now;
        r.sparMeetingStartMs_ = now;
    }
    static bool SparTick(Runner& r, Client& c, const Observation& o) { return r.TickSparring(c,o); }
    static void SeedPoison(Runner& r, Client& c, i64 now) {
        SeedSparring(r, now); r.sparActive_ = false;
        r.socialActivity_ = social::Activity::Poison; r.poisonStudent_ = true;
        r.socialGroupUntilMs_ = now + 120000;
        r.socialHeardMs_ = c.JournalNowMs();
        r.needCfg_.profession = prof::Find("mage");
        r.state_.plan.skills = {{rules::kPoisoning, 1000}};
    }
    static bool PoisonTick(Runner& r, Client& c, const Observation& o) { return r.TickPoisonPractice(c,o); }

    static bool SocialRun(Runner& r, Client& c, const Observation& o) { return r.DoSocialize(c, o); }
    static u32 SocialPeer(const Runner& r) { return r.socialPeer_; }
    static void SocialIdentity(Runner& r) {
        r.state_.identity.characterName = "Student";
        r.state_.plan.skills.push_back({rules::kAnatomy, 1000});
    }
    static void SocialChatty(Runner& r, i32 sociability) {
        r.state_.persona.set = true;
        r.state_.persona.sociability = sociability;
    }
    static void SocialEnd(Runner& r, Client& c) { r.EndSocialGroup(c, "test finished"); }
    static void SocialPendingAcceptance(Runner& r) { r.socialOwnParty_ = true; }
    static void SocialJustSpoke(Runner& r, i64 nowMs) { r.socialChatMs_ = nowMs; }
    static void SocialNeeds(Runner& r, Client& c, const Observation& o, std::vector<Need>& needs) {
        r.AddSocialNeeds(c, o, needs);
    }
    static i32 GroupSupport(Runner& runner, Client& client, const Observation& obs,
                            u32 target, bool announce) {
        return runner.HuntSupport(client, obs, target, announce);
    }
    static void RememberPlayerAttack(Runner& runner, const char* name) {
        runner.state_.memory.NoteEvent("attacked_by_player", name, "test", 0, 0, 1);
    }
    static bool KnowsCompanion(const Runner& runner) {
        return runner.state_.memory.HasEvent("hunt_companion");
    }
    static void SeedPendingBankDeposit(Runner& runner) {
        runner.bankItemMovePending_ = true;
        runner.bankItemMoveJournalMs_ = 0;
        runner.marketQuietUntilMs_ = 100000;
        runner.planner_.Cooldown(GoalKind::TradeWithPlayer, 100000);
    }
    static bool SettleBankDeposit(Runner& runner, Client& client,
                                  const Observation& obs) {
        return runner.SettleBankItemMove(client, obs);
    }
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
    static bool RefillMana(Runner& runner, Client& client, const Observation& obs) {
        return runner.RefillManaWhenSafe(client, obs);
    }
    static bool MaintainCasterBuffs(Runner& runner, Client& client,
                                    const Observation& obs) {
        return runner.MaintainCasterBuffs(client, obs);
    }
    static void CombatGap(Runner& runner, i64 until) { runner.nextActionMs_ = until; }
    static void BankLot(Runner& runner, u16 amount) {
        runner.bankItemMoveAmount_ = amount;
        runner.bankItemMoveSerial_ = 0x4000BBBB;
        runner.bankItemMoveDestination_ = 0x4000AAAA;
    }
    static u16 BankLot(const Runner& runner) { return runner.bankItemMoveAmount_; }
    static bool Survive(Runner& runner, Client& client, const Observation& obs) {
        return runner.DoSurvive(client, obs);
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
    static const std::string& HomeCity(const Runner& runner) {
        return runner.state_.homeCity;
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
    // --- S7: sequential hubs (Economy.cpp, 2026-09-07) ----------------------
    static bool MarketPlaceUsableForTest(Runner& runner, Client& client) {
        return runner.MarketPlaceUsable(client);
    }
    static std::string ActiveMarketPlaceId(const Runner& runner) {
        return runner.marketPlaceId_;
    }
    static std::string OtherMarketPlaceId(const Runner& runner) {
        return runner.marketOtherPlaceId_;
    }
    static std::string PrimaryMarketPlaceId(const Runner& runner) {
        return runner.marketPrimaryPlaceId_;
    }
    static bool MarketHubTried(const Runner& runner) {
        return runner.marketHubTried_;
    }
    static bool TryOtherMarketHubForTest(Runner& runner, Client& client,
                                         const Observation& obs) {
        return runner.TryOtherMarketHub(client, obs);
    }
    static void ResetTradeStateForTest(Runner& runner) {
        runner.ResetTradeState();
    }
    // Session clock start, so TryOtherMarketHub's own trip-budget veto has a
    // real "how long is left" to measure against instead of the default 0
    // (which reads as a session already 100% spent against obs.nowMs).
    static void SetSessionStart(Runner& runner, i64 nowMs) {
        runner.sessionStartMs_ = nowMs;
    }

    // --- "a buyer pays for what is in the window" (Economy.cpp, 2026-09-07) --
    // What Baelos shouted out loud before the window ever opened: the WTB
    // ceiling, not an agreement. DriveOpenTrade's own listen loop is what
    // normally writes this; seeded directly here so the test can drive
    // straight to the window race the defect lived in.
    static void SeedTradeWant(Runner& runner, const market::TradeIntent& want,
                              i64 nowMs) {
        runner.tradeWant_ = want;
        runner.tradeWantAskedMs_ = nowMs;
    }
    static bool DriveOpenTradeForTest(Runner& runner, Client& client,
                                      const Observation& obs) {
        return runner.DriveOpenTrade(client, obs);
    }
    static bool DoTradeWithPlayerForTest(Runner& runner, Client& client,
                                         const Observation& obs) {
        return runner.DoTradeWithPlayer(client, obs);
    }
    static i32 TradeOfferedQty(const Runner& runner) {
        return runner.tradeOfferedQty_;
    }
    static i32 TradeOfferPrice(const Runner& runner) {
        return runner.tradeOfferPrice_;
    }
    // gold this life actually put in the window -- qty x price, the two
    // numbers the defect kept apart from each other and from the truth.
    static i32 GoldOwed(const Runner& runner) {
        return runner.tradeOfferedQty_ * runner.tradeOfferPrice_;
    }
    static i32 BelievedSalePrice(const Runner& runner, const char* item) {
        return runner.state_.prices.BelievedSalePrice(item);
    }
    // Seeds a "we already agreed to buy" state directly, bypassing the window
    // dance -- DoTradeWithPlayer's Completed-phase price observation reads
    // only these fields plus the CLIENT's own trade phase, so this isolates
    // the poison guard from the funding machinery already covered by
    // DriveOpenTradeForTest above.
    static void SeedCompletedBuy(Runner& runner, const std::string& item,
                                 const market::TradeIntent& ceilingWant,
                                 i32 packBefore, i32 goldBefore,
                                 i32 agreedUnitPrice) {
        runner.tradeItem_ = item;
        runner.tradeSellingQty_ = 0;
        runner.tradeWant_ = ceilingWant;
        runner.tradePackBefore_ = packBefore;
        runner.tradeGoldBefore_ = goldBefore;
        runner.tradeOfferPrice_ = agreedUnitPrice;
    }

    // --- GET_TOOL frees an occupied hand before a wielded-tool wear
    // (Gear.cpp, 2026-09-07) -- stubs the profession catalogue directly so
    // the test drives Runner::DoGetTool's real per-tool loop without needing
    // a shard-loaded profession record.
    static void SetToolProfessionForTest(Runner& runner,
                                         const prof::Profession* p) {
        runner.needCfg_.profession = p;
    }
    static bool DoGetToolForTest(Runner& runner, Client& client,
                                 const Observation& obs) {
        return runner.DoGetTool(client, obs);
    }

    // --- a refused equip is never retried (Gear.cpp/Core.cpp, 2026-09-07) --
    static bool DoUpgradeGearForTest(Runner& runner, Client& client,
                                     const Observation& obs) {
        return runner.DoUpgradeGear(client, obs);
    }
    // The per-tick "what did I learn" pass Tick() runs before any goal, which
    // is where the server's answer to a wear request is read.
    static void LearnForTest(Runner& runner, Client& client,
                             const Observation& obs) {
        runner.LearnFromObservation(client, obs);
    }
    static bool MayWearForTest(const Runner& runner, const ArmorPiece& a,
                               const Observation& obs) {
        return runner.MayWear(a, obs);
    }
    static bool RemembersUnwearable(const Runner& runner, u16 graphic) {
        return runner.state_.memory.IsUnwearable(graphic);
    }
    // What DoUpgradeGear's wear pass records at the moment it asks. Set here
    // directly because the pass itself needs Client::ItemEquipLayer, and that
    // reads tiledata this harness deliberately does not load -- see the test.
    static void NotePendingWearForTest(Runner& runner, u32 serial, u16 graphic,
                                       u16 overGraphic = 0) {
        runner.pendingWearSerial_ = serial;
        runner.pendingWearGraphic_ = graphic;
        runner.pendingWearOverGraphic_ = overGraphic;
    }

    // --- a target that never retaliates is dropped, not re-picked forever
    // (Train.cpp, fleet122c30_20260907) --------------------------------------
    static bool DoTrainCombatForTest(Runner& runner, Client& client,
                                     const Observation& obs) {
        return runner.DoTrainCombat(client, obs);
    }
    // Skips DoUpgradeGear's own hand-off (Train.cpp: "no gear yet -- shopping
    // before the graveyard"), which needs real equipped-item packets this
    // harness has no reason to build for a test about the engage loop, not
    // about armour.
    static void CoolGearErrandForTest(Runner& runner, i64 nowMs) {
        runner.planner_.Cooldown(GoalKind::UpgradeGear, nowMs + 999999);
    }
    static void SetProfessionForTest(Runner& runner, const prof::Profession* p) {
        runner.needCfg_.profession = p;
    }
    static i32 HuntEngageTriesForTest(const Runner& runner, u32 serial) {
        return runner.HuntEngageTries(serial);
    }
    static bool IsHuntExcludedForTest(const Runner& runner, u32 serial) {
        return runner.IsHuntExcluded(serial);
    }
    static bool IsUnreachableForTest(const Runner& runner, u32 serial, i64 nowMs) {
        return runner.IsUnreachable(serial, nowMs);
    }
    static u32 CurrentFoeForTest(const Runner& runner) {
        return runner.currentFoe_;
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

// How many "Guards" shouts the client actually put on the wire (0x03 ascii
// speech). Asserting on the sent packet, not on a log line, because the
// packet is the only thing Sphere's guardcall keyword ever sees.
int GuardShouts(const Client& client) {
    int n = 0;
    for (const auto& p : client.SentForTest()) {
        if (p.opcode != 0x03) continue;
        const std::string body(reinterpret_cast<const char*>(p.bytes.data()),
                               p.bytes.size());
        if (body.find("Guards") != std::string::npos) ++n;
    }
    return n;
}

// A 0x78 Mobile Incoming with an empty equipment list -- enough to register a
// hostile in Client::ScanHostiles (mobileCache_), which the wind-down handler
// reads directly and independently of Runner::Observe's override seam.
void SpawnHostile(Client& client, u32 serial, u16 x, u16 y, u8 noto, bool war = false) {
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
    packet[17] = war ? 0x40 : 0;    // status flags
    packet[18] = noto;              // notoriety: 3 = gray, hostile-eligible
    // bytes 19..22 are the zero-serial equipment-list terminator
    client.DispatchPacketForTest(packet, sizeof(packet));
}

std::vector<u8> MakePaperdoll(u32 serial, const char* title) {
    std::vector<u8> p(66, 0);
    p[0] = 0x88;
    StoreBE32(&p[1], serial);
    const usize n = std::strlen(title);
    std::memcpy(&p[5], title, n < 60 ? n : 60);
    return p;
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

// --- packet builders for the trade-window fix (Economy.cpp, 2026-09-07) ----
// Layouts mirror tests/trade_verify.cpp's own builders byte-for-byte (cited
// there against the Client.cpp/ClientTrade.cpp handlers); duplicated locally
// rather than shared, matching this project's existing per-test-file
// convention for hand-built packets.

// 0x1B LOGIN_CONFIRM: serial(4) .. x@11(2) y@13(2).
std::vector<u8> MakeLoginConfirm(u32 serial, u16 x, u16 y) {
    std::vector<u8> p(37, 0);
    p[0] = 0x1B;
    StoreBE32(&p[1], serial);
    StoreBE16(&p[9], 0x0190);
    StoreBE16(&p[11], x);
    StoreBE16(&p[13], y);
    return p;
}

// 0x2E EQUIP_ITEM: item(4) graphic(2) pad(1) layer(1) mobile(4) hue(2).
std::vector<u8> MakeEquip(u32 item, u16 graphic, u8 layer, u32 mobile) {
    std::vector<u8> p(15, 0);
    p[0] = 0x2E;
    StoreBE32(&p[1], item);
    StoreBE16(&p[5], graphic);
    p[8] = layer;
    StoreBE32(&p[9], mobile);
    return p;
}

// 0x25 ADD_ITEM_TO_CONTAINER: serial(4) graphic(2) gfxOffset(1) amount(2)
// x(2) y(2) container(4) hue(2).
// 0xB0 GENERIC_GUMP: serial(4) context(4) x(4) y(4) layoutLen(2) layout,
// then count(2) and UTF-16BE texts -- the shape Client::OnGenericGump reads.
std::vector<u8> MakeGump(u32 serial, u32 context, const std::string& layout,
                         const std::vector<std::string>& texts) {
    std::vector<u8> p(21, 0);
    p[0] = 0xB0;
    StoreBE32(&p[3], serial);
    StoreBE32(&p[7], context);
    StoreBE16(&p[19], static_cast<u16>(layout.size() + 1));
    for (char c : layout) p.push_back(static_cast<u8>(c));
    p.push_back(0);
    p.push_back(static_cast<u8>(texts.size() >> 8)); p.push_back(static_cast<u8>(texts.size()));
    for (const std::string& t : texts) {
        p.push_back(static_cast<u8>(t.size() >> 8)); p.push_back(static_cast<u8>(t.size()));
        for (char c : t) { p.push_back(0); p.push_back(static_cast<u8>(c)); }
    }
    StoreBE16(&p[1], static_cast<u16>(p.size()));
    return p;
}

std::vector<u8> MakeAddItem(u32 serial, u16 graphic, u16 amount, u32 container) {
    std::vector<u8> p(20, 0);
    p[0] = 0x25;
    StoreBE32(&p[1], serial);
    StoreBE16(&p[5], graphic);
    StoreBE16(&p[8], amount);
    StoreBE32(&p[14], container);
    return p;
}

// 0x1D DELETE_OBJECT: serial(4 BE).
std::vector<u8> MakeDeleteObject(u32 serial) {
    std::vector<u8> p(5, 0);
    p[0] = 0x1D;
    StoreBE32(&p[1], serial);
    return p;
}

// 0x98 AllNames / MobName reply: cmd(1) len(2) serial(4) name[30] (Client.cpp
// OnMobName). ScanHostiles only fills HostileHit::name from this cache -- a
// spawned mobile with no name reply is skipped by DoTrainCombat's candidate
// loop entirely (h.name.empty() -> RequestMobileStatus, continue), so a
// combat-picking test needs this every bit as much as SpawnHostile itself.
std::vector<u8> MakeMobName(u32 serial, const char* name) {
    std::vector<u8> p(37, 0);
    p[0] = 0x98;
    StoreBE16(&p[1], static_cast<u16>(p.size()));
    StoreBE32(&p[3], serial);
    const usize n = std::strlen(name);
    std::memcpy(&p[7], name, n < 30 ? n : 30);
    return p;
}

// 0xA1 Update Mobile Hits (9B fixed): cmd(1) serial(4 BE) maxHp(2 BE)
// curHp(2 BE) (Client.cpp OnMobileHp). For a foreign serial this is the
// only client-visible evidence a hit landed on it -- Source-X sends no
// per-swing packet.
std::vector<u8> MakeMobileHp(u32 serial, u16 maxHp, u16 curHp) {
    std::vector<u8> p(9, 0);
    p[0] = 0xA1;
    StoreBE32(&p[1], serial);
    StoreBE16(&p[5], maxHp);
    StoreBE16(&p[7], curHp);
    return p;
}

// 0x6F SECURE_TRADE_OPEN (action 0): partner(4) myContainer(4)
// theirContainer(4) flag(1) name[30].
std::vector<u8> MakeTradeOpen(u32 partner, u32 myContainer, u32 theirContainer,
                              const char* name) {
    std::vector<u8> p(47, 0);
    p[0] = 0x6F;
    p[3] = 0;
    StoreBE32(&p[4], partner);
    StoreBE32(&p[8], myContainer);
    StoreBE32(&p[12], theirContainer);
    p[16] = 1;
    if (name) {
        const usize n = std::strlen(name);
        std::memcpy(&p[17], name, n < 30 ? n : 30);
    }
    return p;
}

// 0x6F SECURE_TRADE_CHANGE (action 2): window(4) mine(4) theirs(4), each
// check a bare 0/nonzero flag (ClientTrade.cpp OnSecureTrade case 2).
std::vector<u8> MakeTradeChange(u32 window, bool mine, bool theirs) {
    std::vector<u8> p(17, 0);
    p[0] = 0x6F;
    p[3] = 2;
    StoreBE32(&p[4], window);
    StoreBE32(&p[8], mine ? 1u : 0u);
    StoreBE32(&p[12], theirs ? 1u : 0u);
    return p;
}

// 0x6F SECURE_TRADE_CLOSE (action 1): container(4) 0 0 0. The container is
// the window being deleted -- Sphere sends each side its own
// (CItemContainer::Trade_Delete -> PacketTradeAction::prepareClose), and
// ClientTrade.cpp case 1 now requires it to name the live trade. Whether the
// close reads as completed or cancelled still depends only on
// TradeState::BothAccepted() at the time it arrives, not on anything else in
// this packet.
std::vector<u8> MakeTradeClose(u32 myContainer) {
    std::vector<u8> p(17, 0);
    p[0] = 0x6F;
    p[3] = 1;
    StoreBE32(&p[4], myContainer);
    return p;
}

// 0x1C ASCII_MESSAGE: serial(4)@3 body(2)@7 type(1)@9 name[30]@14 text@44.
std::vector<u8> MakeAsciiMessage(u32 serial, const char* name,
                                 const char* text) {
    const usize textLen = std::strlen(text) + 1;
    std::vector<u8> p(44 + textLen, 0);
    p[0] = 0x1C;
    StoreBE32(&p[3], serial);
    if (name) {
        const usize n = std::strlen(name);
        std::memcpy(&p[14], name, n < 30 ? n : 30);
    }
    std::memcpy(&p[44], text, textLen);
    return p;
}
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string root = std::string(argv[1]) + "/life_world";
    {
        Client::Config config{};
        auto client = std::make_unique<Client>(config);
        client->SetOfflineForTest(true);
        client->SetClockForTest(200000);
        auto login = MakeLoginConfirm(0x2002, 100, 100);
        client->DispatchPacketForTest(login.data(), login.size());
        SpawnHostile(*client, 0x1002, 102, 100, 1);
        client->ClearSentForTest();
        client->ActionIdentifyNearbyPerson();
        Check(client->SentForTest().size() == 2 && client->SentForTest()[0].opcode == 0x98 &&
              client->SentForTest()[1].opcode == 0x06,
              "social discovery identifies one nearby human with ordinary client packets");
        u8 doll[66]{}; doll[0] = 0x88;
        StoreBE32(doll + 1, 0x1002);
        std::memcpy(doll + 5, "Teacher", 7);
        client->DispatchPacketForTest(doll, sizeof(doll));
        auto name = MakeMobName(0x1002, "Teacher");
        client->DispatchPacketForTest(name.data(), name.size());
        life::Runner runner;
        life::RunnerHarnessAccess::SocialIdentity(runner);
        life::RunnerHarnessAccess::SocialChatty(runner, 100);
        life::Observation obs; obs.nowMs = 200000; obs.x = obs.y = 100;
        obs.hp = obs.hpMax = 50;
        client->ClearSentForTest();
        u8 invite[] = {0xBF, 0, 10, 0, 6, 7, 0, 0, 0x10, 2};
        client->DispatchPacketForTest(invite, sizeof(invite));
        life::RunnerHarnessAccess::SocialObserve(runner, *client, obs);
        bool unsolicitedAccept = false, greeted = false;
        for (const auto& packet : client->SentForTest()) {
            if (packet.opcode == 0xBF) unsolicitedAccept = true;
            if (packet.opcode == 0x03) greeted = true;
        }
        Check(life::RunnerHarnessAccess::SocialPeer(runner) == 0 && !unsolicitedAccept,
              "unsolicited party invitation is never auto-accepted");
        Check(greeted, "first friendly encounter can produce a greeting without a social goal");
        auto speech = MakeAsciiMessage(0x1002, "Teacher", "Anyone for training and healing practice? Meet here.");
        life::RunnerHarnessAccess::SocialJustSpoke(runner, obs.nowMs);
        client->DispatchPacketForTest(speech.data(), speech.size());
        life::RunnerHarnessAccess::SocialObserve(runner, *client, obs);
        Check(life::RunnerHarnessAccess::SocialPeer(runner) == 0x1002,
              "nearby invitation is answered even immediately after our own chat");
        life::RunnerHarnessAccess::SocialRun(runner, *client, obs);
        Check(!client->PartyContains(0x1002), "sending acceptance does not fabricate membership");
        u8 members[] = {0xBF, 0, 15, 0, 6, 1, 2, 0, 0, 0x10, 2, 0, 0, 0x20, 2};
        client->DispatchPacketForTest(members, sizeof(members) - 1);
        Check(client->PartySize() == 0, "truncated party roster grants no membership");
        client->DispatchPacketForTest(members, sizeof(members));
        Check(client->PartyContains(0x1002) && client->PartyLeader() == 0x1002,
              "server roster establishes party membership and leader");
        client->ClearSentForTest();
        obs.nowMs += 1000;
        life::RunnerHarnessAccess::SocialRun(runner, *client, obs);
        Check(client->CurrentAction().kind == act::Kind::UseSkill &&
              client->CurrentAction().id == rules::kAnatomy && client->CurrentAction().destination == 0x1002,
              "confirmed training party practices Anatomy on its consenting companion");
        bool attacked = false;
        for (const auto& packet : client->SentForTest()) if (packet.opcode == 0x05) attacked = true;
        Check(!attacked, "training consent never authorizes an attack packet");
        std::vector<life::Need> needs;
        obs.underAttack = true;
        life::RunnerHarnessAccess::SocialNeeds(runner, *client, obs, needs);
        Check(needs.empty(), "combat suppresses social and market opportunities");
        life::RunnerHarnessAccess::SocialEnd(runner, *client);
        u8 removed[] = {0xBF, 0, 11, 0, 6, 2, 0, 0, 0, 0x20, 2};
        client->DispatchPacketForTest(removed, sizeof(removed));
        Check(client->PartySize() == 0, "server removal clears party membership");
        client->ClearSentForTest();
        life::RunnerHarnessAccess::SocialPendingAcceptance(runner);
        life::RunnerHarnessAccess::SocialEnd(runner, *client);
        client->DispatchPacketForTest(members, sizeof(members));
        life::RunnerHarnessAccess::SocialObserve(runner, *client, obs);
        bool leftLateParty = false;
        for (const auto& packet : client->SentForTest())
            if (packet.opcode == 0xBF && packet.bytes.size() == 10 && packet.bytes[5] == 2)
                leftLateParty = true;
        Check(leftLateParty, "late membership after cancellation sends an explicit party leave");
        client->CompleteActionForTest(act::Result::Success, "fixture ready");
        Check(!client->BeginSparringRound(0x1002), "unknown health blocks even a bare-handed spar");
        const u8 layers[] = {4, 6, 7, 10, 13, 19};
        const u16 graphics[] = {0x1411, 0x1412, 0x1414, 0x1413, 0x1415, 0x1410};
        for (u32 who : {0x2002u, 0x1002u}) for (int i=0; i<6; ++i) {
            auto equip = MakeEquip(0x40000000 + who*16+i, graphics[i], layers[i], who);
            client->DispatchPacketForTest(equip.data(), equip.size());
        }
        auto health = [&](u32 who, u16 hp) {
            u8 packet[9] = {0xA1}; StoreBE32(packet+1, who);
            StoreBE16(packet+5, 100); StoreBE16(packet+7, hp);
            client->DispatchPacketForTest(packet, sizeof(packet));
        };
        Check(!client->BeginSparringRound(0x1002), "unknown health blocks sparring despite iron armour");
        health(0x2002,100); health(0x1002,100);
        Check(client->SparringReady(0x1002), "fresh healthy iron-armoured party pair can prepare");
        Check(sparring::PoisonReceiver(601, 601, 10), "Poison receiver has both skills above sixty and bandages");
        Check(!sparring::PoisonReceiver(600, 1000, 10) && !sparring::PoisonReceiver(1000, 600, 10),
              "both Healing and Anatomy must exceed sixty, not merely one of them");
        Check(!sparring::PoisonReceiver(1000, 1000, 0), "Poison practice requires curing supplies");
        Check(client->PoisonPracticeReady(0x1002), "fresh healthy consenting party can prepare Poison practice");
        u8 poisoned[17] = {0x77}; StoreBE32(poisoned+1, 0x1002); StoreBE16(poisoned+5, 0x190);
        StoreBE16(poisoned+7, 102); StoreBE16(poisoned+9, 100); poisoned[15] = 0x04;
        client->DispatchPacketForTest(poisoned, sizeof(poisoned));
        Check(client->MobilePoisoned(0x1002) && !client->PoisonPracticeReady(0x1002),
              "server poison flag prevents another training cast before cure");
        poisoned[15] = 0;
        client->DispatchPacketForTest(poisoned, sizeof(poisoned));
        Check(!client->MobilePoisoned(0x1002) && client->PoisonPracticeReady(0x1002),
              "server cure update restores practice eligibility");
        client->ClearSentForTest();
        Check(client->BeginSparringRound(0x1002), "prepared pair starts a real attack round");
        Check(!client->SentForTest().empty() && client->SentForTest().back().opcode == 0x05,
              "sparring uses real server attack packet");
        health(0x1002,59);
        Check(client->SparringPeer() == 0x1002, "59%% is above the owner's 40%% stop line: the round goes on");
        health(0x1002,39);
        Check(client->SparringPeer() == 0 && client->SentForTest().back().opcode == 0x72 &&
              client->SentForTest().back().bytes[1] == 0,
              "peer low HP immediately sends war off even before war acknowledgement");
        Check(!client->BeginSparringRound(0x1002), "injured peer cannot immediately restart");
        health(0x1002,100);
        Check(client->BeginSparringRound(0x1002), "recovered peer can start a fresh round");
        client->SetClockForTest(206000); client->SparringSafetyTick();
        Check(client->SparringPeer() == 0, "watchdog ends stalled round without a planner tick");
        Check(!client->BeginSparringRound(0x1002), "stale health prevents a new round");
        Check(!client->PoisonPracticeReady(0x1002), "stale health also prevents Poison practice");
        health(0x2002,100); health(0x1002,100);
        Check(client->BeginSparringRound(0x1002), "fresh status restores eligibility");
        auto weapon = MakeEquip(0x40009999, 0x13B9, 1, 0x1002);
        client->DispatchPacketForTest(weapon.data(), weapon.size());
        Check(client->SparringPeer() == 0 && !client->SparringReady(0x1002),
              "partner switching to a sword immediately stops sparring");
        weapon = MakeEquip(0x40009999, 0x0F51, 1, 0x1002);
        client->DispatchPacketForTest(weapon.data(), weapon.size());
        Check(!client->BeginSparringRound(0x1002), "a dagger never spars an empty-handed partner (kit mismatch)");
        auto myDagger = MakeEquip(0x40009998, 0x0F52, 1, 0x2002);
        client->DispatchPacketForTest(myDagger.data(), myDagger.size());
        Check(client->BeginSparringRound(0x1002), "two iron-armoured players with daggers may spar");
        client->DispatchPacketForTest(removed, sizeof(removed));
        Check(client->SparringPeer() == 0 && !client->BeginSparringRound(0x1002),
              "party removal immediately stops and blocks further attacks");
        client->DispatchPacketForTest(members, sizeof(members));
        life::Runner sparRunner;
        obs.nowMs = 206000; obs.bandages = 20; obs.underAttack = false;
        life::RunnerHarnessAccess::SeedSparring(sparRunner, obs.nowMs);
        client->ClearSentForTest();
        life::RunnerHarnessAccess::SparTick(sparRunner, *client, obs);
        attacked = false;
        for (const auto& p : client->SentForTest()) if (p.opcode == 0x05) attacked = true;
        Check(!attacked, "consenting party still requires partner readiness before attacking");
        bool saidReady = false;
        for (const auto& p : client->SentForTest()) if (p.opcode == 0x03 || p.opcode == 0xAD) saidReady = true;
        Check(saidReady, "the runner announces readiness once per meeting");
        auto ready = MakeAsciiMessage(0x1002, "Teacher", "Student: Ready to spar.");
        client->DispatchPacketForTest(ready.data(), ready.size());
        life::RunnerHarnessAccess::SparTick(sparRunner, *client, obs);
        attacked = false;
        for (const auto& p : client->SentForTest()) if (p.opcode == 0x05) attacked = true;
        Check(attacked, "one addressed 'Ready to spar.' authorizes the rounds");
        client->SetClockForTest(206001); obs.nowMs = 206001;
        auto stop = MakeAsciiMessage(0x1002, "Teacher", "Student: Let's stop and regroup.");
        client->DispatchPacketForTest(stop.data(), stop.size());
        life::RunnerHarnessAccess::SparTick(sparRunner, *client, obs);
        Check(client->SparringPeer() == 0 && life::RunnerHarnessAccess::SocialPeer(sparRunner) == 0,
              "withdrawn consent immediately stops the attack and ends the group");

        const auto savedSpells = spell::SpellTable();
        spell::LoadSpellTableFromText("spell\tdefname\tname\tcircle\tminskill\tmana\tflags\treagents\n"
            "20\ts_poison\tPoison\t3\t300\t9\tspellflag_targ_obj|spellflag_harm|spellflag_tick\ti_reag_nightshade\n");
        life::Runner poisonRunner;
        obs.nowMs = 207000; client->SetClockForTest(obs.nowMs);
        life::RunnerHarnessAccess::SeedPoison(poisonRunner, *client, obs.nowMs);
        obs.skills = {{rules::kMagery,500}, {rules::kPoisoning,0}};
        obs.mana = 50; obs.spellbookSerial = 0x40005555;
        obs.pack = {{"i_reag_nightshade", 10}};
        auto page = MakeAddItem(0x40005556, 0x1F2E, 20, obs.spellbookSerial);
        client->DispatchPacketForTest(page.data(), page.size());
        health(0x2002,100); health(0x1002,100);
        client->CompleteActionForTest(act::Result::Success, "previous round ended");
        client->ClearSentForTest();
        auto casts = [&]() { int n=0; for (const auto& p : client->SentForTest())
            if (p.opcode == 0x12) ++n; return n; };
        life::RunnerHarnessAccess::PoisonTick(poisonRunner, *client, obs);
        Check(casts() == 0, "Poison consent still requires a fresh numbered healer-ready message");
        obs.nowMs++; client->SetClockForTest(obs.nowMs);
        ready = MakeAsciiMessage(0x1002, "Teacher", "Student: Ready to receive Poison round 1.");
        client->DispatchPacketForTest(ready.data(), ready.size());
        life::RunnerHarnessAccess::PoisonTick(poisonRunner, *client, obs);
        Check(casts() == 1, "Poison practice casts a real spell on the ready consenting healer");
        client->CompleteActionForTest(act::Result::Success, "cast completed");
        obs.nowMs++; client->SetClockForTest(obs.nowMs);
        client->DispatchPacketForTest(ready.data(), ready.size());
        life::RunnerHarnessAccess::PoisonTick(poisonRunner, *client, obs);
        Check(casts() == 1, "replayed readiness cannot authorize a second Poison cast");
        obs.nowMs++; client->SetClockForTest(obs.nowMs);
        client->DispatchPacketForTest(stop.data(), stop.size());
        life::RunnerHarnessAccess::PoisonTick(poisonRunner, *client, obs);
        Check(life::RunnerHarnessAccess::SocialPeer(poisonRunner) == 0,
              "withdrawing Poison consent ends the meeting");
        spell::SpellTable() = savedSpells;


    }
    {
        Client::Config config{};
        auto client = std::make_unique<Client>(config);
        client->SetOfflineForTest(true);
        client->SetClockForTest(200000);
        auto login = MakeLoginConfirm(0xED04, 100, 100);
        client->DispatchPacketForTest(login.data(), login.size());
        SpawnHostile(*client, 0x1001, 104, 100, 6);
        SpawnHostile(*client, 0x1002, 103, 100, 1, true);
        std::vector<Client::HostileHit> support;
        Check(client->CombatSupportNear(0x1001, support) == 0,
              "an unknown human-shaped mobile is not assumed to be an ally");
        u8 doll[66]{}; doll[0] = 0x88;
        StoreBE32(doll + 1, 0x1002);
        std::memcpy(doll + 5, "Companion", 9);
        client->DispatchPacketForTest(doll, sizeof(doll));
        auto name = MakeMobName(0x1002, "Companion");
        client->DispatchPacketForTest(name.data(), name.size());
        Check(client->CombatSupportNear(0x1001, support) == 1,
              "a known friendly fighter near prey provides support");
        life::Runner runner;
        life::Observation obs; obs.nowMs = 200000;
        client->ClearSentForTest();
        Check(life::RunnerHarnessAccess::GroupSupport(runner, *client, obs, 0x1001, true) == 1,
              "group handler recognizes a visible companion");
        Check(life::RunnerHarnessAccess::KnowsCompanion(runner),
              "cooperative encounter enters persistent life memory");
        const auto sent = client->SentForTest().size();
        life::RunnerHarnessAccess::GroupSupport(runner, *client, obs, 0x1001, true);
        Check(sent > 0 && client->SentForTest().size() == sent,
              "help chat is sent once and throttled on repeated ticks");
        life::RunnerHarnessAccess::RememberPlayerAttack(runner, "Companion");
        Check(life::RunnerHarnessAccess::GroupSupport(runner, *client, obs, 0x1001, false) == 0,
              "a remembered player attacker is not trusted as hunting support");
        SpawnHostile(*client, 0x1002, 103, 100, 6, true);
        Check(client->CombatSupportNear(0x1001, support) == 0,
              "a hostile player never counts as friendly support");
    }
    {
        Client::Config config{};
        auto client = std::make_unique<Client>(config);
        client->SetOfflineForTest(true);
        client->SetClockForTest(1000);
        life::Runner runner;
        life::Observation obs;
        obs.nowMs = 1000;
        life::RunnerHarnessAccess::SeedPendingBankDeposit(runner);
        client->ActionOpenContainer(0x4000AAAA);
        client->CompleteActionForTest(act::Result::Success, "item removed during drag");
        auto refusal = MakeAsciiMessage(0xFFFFFFFF, "System", "Your bankbox can't hold more weight.");
        client->DispatchPacketForTest(refusal.data(), refusal.size());
        Check(life::RunnerHarnessAccess::SettleBankDeposit(runner, *client, obs),
              "full-bank refusal overrides an early move success");
        Check(runner.GetPlanner().Cooling(life::GoalKind::Bank, obs.nowMs),
              "full-bank refusal prevents an endless deposit retry");
        Check(!runner.GetPlanner().Cooling(life::GoalKind::TradeWithPlayer, obs.nowMs),
              "full-bank refusal reopens the player-sale recovery path");
        life::Runner smaller;
        OpenBank(*client, 0x4000AAAA);
        life::RunnerHarnessAccess::SeedPendingBankDeposit(smaller);
        life::RunnerHarnessAccess::BankLot(smaller, 2);
        Check(life::RunnerHarnessAccess::SettleBankDeposit(smaller, *client, obs) &&
              life::RunnerHarnessAccess::BankLot(smaller) == 1,
              "a refused two-unit deposit retries one unit before giving up");
        Check(!smaller.GetPlanner().Cooling(life::GoalKind::Bank, obs.nowMs),
              "a smaller deposit keeps banking available");
    }
    {
        Client::Config config{};
        auto client = std::make_unique<Client>(config);
        client->SetOfflineForTest(true); client->SetInWorldForTest();
        client->SetClockForTest(1000);
        life::Runner runner;
        life::RunnerHarnessAccess::SetProfessionForTest(runner, prof::Find("mage"));
        life::Observation obs; obs.inWorld = true; obs.nowMs = 1000;
        obs.hp = obs.hpMax = 50; obs.mana = 49; obs.manaMax = 50;
        Check(life::RunnerHarnessAccess::RefillMana(runner, *client, obs),
              "a safe mage refills even one missing mana point");
        const auto sent = client->SentForTest().size();
        Check(sent > 0, "safe refill issues meditation");
        client->CompleteActionForTest(act::Result::Success, "meditating");
        obs.nowMs += 1000;
        Check(life::RunnerHarnessAccess::RefillMana(runner, *client, obs) &&
              client->SentForTest().size() == sent,
              "ordinary work waits without repeatedly restarting meditation");
        obs.mana = 50;
        Check(!life::RunnerHarnessAccess::RefillMana(runner, *client, obs),
              "full mana releases ordinary work");
        obs.mana = 20; obs.underAttack = true;
        Check(!life::RunnerHarnessAccess::RefillMana(runner, *client, obs),
              "danger takes priority over mana refill");
        obs.underAttack = false; obs.hostilesNear = 1;
        Check(!life::RunnerHarnessAccess::RefillMana(runner, *client, obs),
              "hostiles on unguarded ground prevent stationary refill");
        obs.underAttack = true;
        life::RunnerHarnessAccess::CombatGap(runner, obs.nowMs + 10000);
        Check(life::RunnerHarnessAccess::RefillMana(runner, *client, obs),
              "a mage attempts meditation in a distant combat opening");
        client->CompleteActionForTest(act::Result::Success, "meditation interrupted");
        const auto combatSent = client->SentForTest().size();
        obs.nowMs += 2100;
        Check(life::RunnerHarnessAccess::RefillMana(runner, *client, obs) &&
              client->SentForTest().size() > combatSent,
              "combat meditation is retried rather than waiting for combat to end");
        Check(!life::RunnerHarnessAccess::RefillMana(runner, *client, obs),
              "an in-flight action is not replaced with another meditation attempt");
    }
    {
        // Buffs use the same observed spellbook, skill, mana and reagent gates
        // as an ordinary cast.  A known full book rotates the five safe buffs;
        // a non-caster or an active threat never spends a preparation cast.
        const auto savedSpells = spell::SpellTable();
        spell::LoadSpellTableFromText(
            "spell\tdefname\tname\tcircle\tminskill\tmana\tflags\treagents\n"
            "6\ts_night_sight\tNight Sight\t1\t100\t4\tspellflag_good\ti_reag_spider_silk,i_reag_sulfur_ash\n"
            "7\ts_reactive_armor\tReactive Armor\t1\t100\t4\tspellflag_good\ti_reag_garlic,i_reag_spider_silk,i_reag_sulfur_ash\n"
            "15\ts_protection\tProtection\t2\t200\t6\tspellflag_good\ti_reag_garlic,i_reag_ginseng,i_reag_sulfur_ash\n"
            "17\ts_bless\tBless\t3\t300\t9\tspellflag_good\ti_reag_garlic,i_reag_mandrake_root\n"
            "36\ts_magic_reflection\tMagic Reflection\t5\t500\t14\tspellflag_good\ti_reag_garlic,i_reag_mandrake_root,i_reag_spider_silk\n");
        Client::Config config{};
        auto client = std::make_unique<Client>(config);
        client->SetOfflineForTest(true);
        auto login = MakeLoginConfirm(0x2003, 100, 100);
        client->DispatchPacketForTest(login.data(), login.size());
        life::Runner runner;
        life::RunnerHarnessAccess::SetProfessionForTest(runner, prof::Find("mage"));
        constexpr u32 book = 0x40005590u;
        for (int spell : {6, 7, 15, 17, 36}) {
            auto page = MakeAddItem(0x40005600u + static_cast<u32>(spell),
                                    0x1F2E, static_cast<u16>(spell), book);
            client->DispatchPacketForTest(page.data(), page.size());
        }
        life::Observation obs;
        obs.inWorld = true; obs.nowMs = 1000;
        obs.hp = obs.hpMax = 60; obs.mana = obs.manaMax = 60;
        obs.spellbookSerial = book;
        obs.skills = {{rules::kMagery, 1000}};
        obs.pack = {{"i_reag_garlic", 10}, {"i_reag_ginseng", 10},
                    {"i_reag_mandrake_root", 10}, {"i_reag_spider_silk", 10},
                    {"i_reag_sulfur_ash", 10}};
        for (int expected : {6, 7, 15, 17, 36}) {
            client->ClearSentForTest();
            Check(life::RunnerHarnessAccess::MaintainCasterBuffs(runner, *client, obs),
                  "a safe mage casts the next available long-lived buff");
            Check(client->CurrentAction().kind == act::Kind::CastSpell &&
                  client->CurrentAction().id == expected &&
                  client->CurrentAction().destination == client->PlayerSerial(),
                  "the buff is cast on the caster through the normal spell action");
            client->CompleteActionForTest(act::Result::Success, "buff applied");
            obs.nowMs += 75001;
        }
        obs.underAttack = true;
        Check(!life::RunnerHarnessAccess::MaintainCasterBuffs(runner, *client, obs),
              "an active fight takes priority over a caster buff refresh");
        obs.underAttack = false;
        life::RunnerHarnessAccess::SetProfessionForTest(runner, prof::Find("fencer"));
        Check(!life::RunnerHarnessAccess::MaintainCasterBuffs(runner, *client, obs),
              "caster buffs are reserved for mage and warlock builds");
        spell::SpellTable() = savedSpells;
    }
    {
        Client::Config config{};
        config.loginHost = "127.0.0.1";
        auto client = std::make_unique<Client>(config);
        client->SetOfflineForTest(true);
        auto login = MakeLoginConfirm(0xED04, 1462, 1681);
        client->DispatchPacketForTest(login.data(), login.size());
        u8 wheel[14]{};
        wheel[0] = 0x1A;
        StoreBE16(wheel + 1, sizeof(wheel));
        StoreBE32(wheel + 3, 0x40000471);
        StoreBE16(wheel + 7, 0x101C);
        StoreBE16(wheel + 9, 1473);
        StoreBE16(wheel + 11, 1689);
        client->DispatchPacketForTest(wheel, sizeof(wheel));
        Check(life::runner_detail::FindSpinWheel(*client, 18) == 0x40000471,
              "workshop arrival sees the wheel diagonally across the visible square");
        Check(life::runner_detail::FindSpinWheel(*client, 18, {0x40000471}) == 0,
              "a rejected wheel is still excluded from station selection");
    }
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

        for (const auto work : {life::GoalKind::Mine, life::GoalKind::GatherLogs, life::GoalKind::Fish}) {
            Check(client->TravelToPoint(400, 400, 2, "old food errand"), "food journey starts");
            life::RunnerHarnessAccess::LeaveGoal(runner, *client, work, work);
            Check(client->TravelBusy(), "a same-kind work pick preserves its journey");
            life::RunnerHarnessAccess::LeaveGoal(runner, *client, life::GoalKind::GetFood, work);
            Check(!client->TravelBusy(), "gathering cancels the superseded food journey");
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
        life::RunnerHarnessAccess::LeaveGoal(
            runner, *client, life::GoalKind::Survive, life::GoalKind::Mine);
        Check(client->TravelBusy(), "mining also preserves an active survival retreat");
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

            // A hostile merely in sight is not an attack.  This was the live
            // fleet's spam path: every protected character repeated "Guards"
            // every fifteen seconds whenever another character entered scan.
            life::Observation watched = calm;
            watched.nowMs += 60000;
            watched.hostilesNear = 5;
            life::RunnerHarnessAccess::GuardKeeper(runner, *client, watched);
            Check(GuardShouts(*client) == 1,
                  "a visible but non-attacking hostile does not call guards");
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
    // --- S7 SEQUENTIAL HUBS (owner ruling 2026-09-07): "it can check minoc
    // first britain after, same for buyers as well". A character tries its
    // cheaper hub first, then the other one when the first hub's whole
    // announce/listen window closes with no trade -- and the trip-budget
    // veto still applies to that second hop, skipped rather than forced.
    // Reuses the same two-hub fixture atlas (bank=Britain, minoc_bank=Minoc)
    // set up above for the S6/S7 home-market tests.
    {
        Client::Config config{};
        config.loginHost = "127.0.0.1";
        config.username = config.password = "offline_world";
        config.version = "2.0.7";
        config.sessionTag = "hub_seq";
        config.atlasPath = atlasPath.c_str();
        config.navgridPath = gridPath.c_str();
        auto client = std::make_unique<Client>(config);
        client->SetOfflineForTest(true);
        client->SetInWorldForTest();
        client->SetClockForTest(1000000);
        Position(*client, 35, 35);   // near the Britain-town bank

        life::Runner runner;
        life::RunnerConfig rc;
        rc.dataRoot = root + "/hub_seq";
        rc.accountName = "offline_world";
        rc.characterName = "hub_seq";
        rc.professionId = "merchant_tinker";
        std::string error;
        Check(runner.Configure(rc, &error), error.c_str());

        // Resolving the home market picks the nearer hub (Britain's "bank")
        // and caches the other one (Minoc's "minoc_bank") for later.
        Check(life::RunnerHarnessAccess::MarketPlaceUsableForTest(runner, *client),
              "the fixture's Britain bank is a usable market place");
        Check(life::RunnerHarnessAccess::ActiveMarketPlaceId(runner) == "bank",
              "the cheaper hub (Britain) is tried first");
        Check(life::RunnerHarnessAccess::PrimaryMarketPlaceId(runner) == "bank",
              "and is cached as the permanent primary hub");
        Check(life::RunnerHarnessAccess::OtherMarketPlaceId(runner) == "minoc_bank",
              "with Minoc cached as the fallback hub");
        Check(!life::RunnerHarnessAccess::MarketHubTried(runner),
              "no hub switch has happened yet");

        life::Observation obs;
        obs.inWorld = true;
        obs.x = 35; obs.y = 35;
        obs.nowMs = 2000000;
        obs.hp = obs.hpMax = 50;
        life::RunnerHarnessAccess::SetSessionStart(runner, obs.nowMs);

        // No trade at Britain: with a full 30-minute session (the default)
        // the second hop is affordable, so the switch is made.
        Check(life::RunnerHarnessAccess::TryOtherMarketHubForTest(runner, *client, obs),
              "no trade at the first hub switches to the second");
        Check(life::RunnerHarnessAccess::ActiveMarketPlaceId(runner) == "minoc_bank",
              "the active hub is now Minoc");
        Check(life::RunnerHarnessAccess::MarketHubTried(runner),
              "the switch is recorded");

        // ONE SWITCH PER ERRAND: a second no-trade at Minoc must not bounce
        // back to Britain.
        Check(!life::RunnerHarnessAccess::TryOtherMarketHubForTest(runner, *client, obs),
              "a second attempt this errand does not switch again");
        Check(life::RunnerHarnessAccess::ActiveMarketPlaceId(runner) == "minoc_bank",
              "so the active hub does not bounce back to Britain");

        // The errand ends (success or failure both call ResetTradeState):
        // the NEXT independent errand starts at the cheaper hub again.
        life::RunnerHarnessAccess::ResetTradeStateForTest(runner);
        Check(life::RunnerHarnessAccess::ActiveMarketPlaceId(runner) == "bank",
              "the next errand tries the primary (Britain) hub again");
        Check(!life::RunnerHarnessAccess::MarketHubTried(runner),
              "and the switch flag is clear for it");

        // VETO BRANCH: with no session time left, the second hop is skipped
        // -- not forced -- and the errand's own "no seller/buyer came"
        // failure fires instead of a walk that cannot finish.
        life::RunnerHarnessAccess::MakeSessionEnding(runner, obs.nowMs);
        Check(!life::RunnerHarnessAccess::TryOtherMarketHubForTest(runner, *client, obs),
              "an unaffordable second hop is vetoed, not forced");
        Check(life::RunnerHarnessAccess::ActiveMarketPlaceId(runner) == "bank",
              "the active hub stays put when the trip is vetoed");
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
    // Fishing is geographically viable at every listed coastal city.  Unlike
    // ore and wood gatherers, deterministic homes must therefore distribute
    // fishers instead of pinning each one to the first list entry (Skara Brae).
    {
        const char* expectedHomes[] = {"Skara Brae", "Jhelom", "Vesper", "Britain"};
        for (int i = 0; i < 4; ++i) {
            life::Runner fisher;
            life::RunnerConfig fisherCfg;
            fisherCfg.dataRoot = root + "/fisher_spread_" + std::to_string(i);
            fisherCfg.accountName = "fisher_spread";
            fisherCfg.characterName = "dock" + std::to_string(i);
            fisherCfg.professionId = "fisher";
            std::string fisherError;
            Check(fisher.Configure(fisherCfg, &fisherError), fisherError.c_str());
            Check(life::RunnerHarnessAccess::HomeCity(fisher) == expectedHomes[i],
                  "fishers spread across their coastal home cities");
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

        // The observer's window says the character left, and who it is.
        std::string statusText;
        Check(json::ReadFile((root + "/winddown_guard/offline_world.winddown_guard/status.json").c_str(),
                             &statusText), "logout leaves a status.json for the observer");
        const json::Value status = json::Parse(statusText, nullptr);
        Check(!status["online"].AsBool(true) && status["phase"].AsString() == "offline" &&
              status["character"].AsString() == "winddown_guard" &&
              status["family"].AsString() == "fencer" && !status["rhythm"].AsString().empty(),
              "the status file marks the character offline with its family and play rhythm");
    }

    // --- travel: the runebook is read, then chosen by where the trip goes --
    {
        Client::Config config{};
        auto client = std::make_unique<Client>(config);
        client->SetOfflineForTest(true);
        client->SetClockForTest(400000);
        auto login = MakeLoginConfirm(0x2002, 100, 100);
        client->DispatchPacketForTest(login.data(), login.size());
        auto pack = MakeEquip(0x40007000, 0x0E75, 0x15, 0x2002);
        client->DispatchPacketForTest(pack.data(), pack.size());
        auto book = MakeAddItem(0x40007001, 0x22C5, 1, 0x40007000);
        client->DispatchPacketForTest(book.data(), book.size());
        Check(client->HasRunebook() && !client->RunebookRead(), "a carried book has not been read yet");
        Check(!client->HasRecallReagents(), "no reagents in the pack: an uncharged Recall is not affordable");
        for (u16 g : {0x0F7A, 0x0F7B, 0x0F86}) {
            auto reg = MakeAddItem(0x40007100 + g, g, 5, 0x40007000);
            client->DispatchPacketForTest(reg.data(), reg.size());
        }
        Check(client->HasRecallReagents(), "one of each Recall reagent counted from the pack");

        client->ClearSentForTest();
        Check(client->ActionReadRunebook(), "the runner can open its book to read it");
        std::vector<std::string> texts = {"Runebook", "Charges: 00", "Page", "Name", "Destination", "Travel", "Rune",
                                          "1", "Britain", "1490,1555,30", "2", "Minoc", "2500,480,0"};
        for (int n = 3; n <= 8; ++n) { texts.push_back(std::to_string(n)); texts.push_back("(empty)"); texts.push_back("-"); }
        std::string layout;
        for (int n = 1; n <= 8; ++n) layout += "{ button 10 10 2103 2104 1 0 " + std::to_string(10 + n) + " }";
        auto gump = MakeGump(0x40007001, 0x1234, layout, texts);
        client->DispatchPacketForTest(gump.data(), gump.size());
        bool closed = false;
        for (const auto& p : client->SentForTest())
            if (p.opcode == 0xB1 && p.bytes.size() >= 15 && LoadBE32(p.bytes.data() + 11) == 0) closed = true;
        Check(client->RunebookRead() && client->RunebookFilledPages() == 2, "both marked pages are read from the gump");
        Check(closed, "a book opened only to read it is closed again (button 0)");
        Check(client->RunebookPageForGoal(1480, 1600) == 1,
              "a trip to a Britain shop picks the Britain page by its point, whatever the trip's label");
        Check(client->RunebookPageForGoal(2520, 500) == 2, "and a Minoc trip the Minoc page");
        Check(client->RunebookPageForGoal(150, 120) == 0, "a short trip just walks");
    }

    // --- small talk: "sa" is answered "as", once, never a handshake --------
    {
        Client::Config config{};
        auto client = std::make_unique<Client>(config);
        client->SetOfflineForTest(true);
        client->SetClockForTest(300000);
        auto login = MakeLoginConfirm(0x2002, 100, 100);
        client->DispatchPacketForTest(login.data(), login.size());
        SpawnHostile(*client, 0x1003, 102, 100, 1);
        u8 doll[66]{}; doll[0] = 0x88;
        StoreBE32(doll + 1, 0x1003);
        std::memcpy(doll + 5, "Mert", 4);
        client->DispatchPacketForTest(doll, sizeof(doll));
        auto name = MakeMobName(0x1003, "Mert");
        client->DispatchPacketForTest(name.data(), name.size());
        life::Runner runner;
        life::RunnerHarnessAccess::SocialIdentity(runner);
        life::RunnerHarnessAccess::SocialChatty(runner, 100);
        life::Observation obs; obs.nowMs = 300000; obs.x = obs.y = 100;
        obs.hp = obs.hpMax = 50;
        auto said = [&]() {
            std::vector<std::string> lines;
            for (const auto& p : client->SentForTest())
                if (p.opcode == 0x03 && p.bytes.size() > 8)
                    lines.emplace_back(reinterpret_cast<const char*>(p.bytes.data() + 8));
            return lines;
        };
        auto sa = MakeAsciiMessage(0x1003, "Mert", "Sa!");
        client->DispatchPacketForTest(sa.data(), sa.size());
        client->ClearSentForTest();
        life::RunnerHarnessAccess::SocialObserve(runner, *client, obs);
        const auto first = said();
        bool answered = false;
        for (const auto& line : first)
            answered = answered || line == "as" || line == "as hosgeldin" || line == "aleykumselam";
        Check(answered, "a nearby player's \"sa\" is answered with \"as\"");
        bool handshake = false;
        for (const auto& line : first) {
            social::Activity a = social::Activity::None;
            handshake = handshake || social::IsInvitation(line, &a);
        }
        Check(!handshake, "small talk never reads as an invitation");

        client->SetClockForTest(340000); obs.nowMs = 340000;
        auto again = MakeAsciiMessage(0x1003, "Mert", "selam");
        client->DispatchPacketForTest(again.data(), again.size());
        client->ClearSentForTest();
        life::RunnerHarnessAccess::SocialObserve(runner, *client, obs);
        Check(said().empty(), "a second greeting from the same person within ten minutes gets no reply "
                              "(two bots cannot greet each other forever)");
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

    // --- "a buyer pays for what is in the window" (Economy.cpp, 2026-09-07) -
    // Baelos WTB'd "118 i_bandage 4gp"; Aelia's window opened before Baelos's
    // own listen loop ever parsed her "WTS 10 i_bandage 3gp" reply -- the
    // race DoTradeWithPlayer's own `if (tr.Active()) return DriveOpenTrade`
    // short-circuit produces every time a seller answers fast. The old code
    // funded 118 x 4 = 472 gold before either the true quantity or the true
    // price had been read anywhere.
    {
        const u32 me = 0x00019001;
        const u32 partner = 0x00019002;
        const u32 myPack = 0x40020001;
        const u32 myContainer = 0x40020002;
        const u32 theirContainer = 0x40020003;
        const u16 kBandageGfx = 0x0E21;

        Client::Config config{};
        config.loginHost = "127.0.0.1";
        config.username = config.password = "trade_fix";
        config.version = "2.0.7";
        config.sessionTag = "trade_fix";
        auto client = std::make_unique<Client>(config);
        client->SetOfflineForTest(true);
        client->SetInWorldForTest();
        client->SetClockForTest(5999000);

        auto login = MakeLoginConfirm(me, 100, 100);
        client->DispatchPacketForTest(login.data(), login.size());
        auto pack = MakeEquip(myPack, 0x0E75, 0x15, me);
        client->DispatchPacketForTest(pack.data(), pack.size());
        Check(client->BackpackSerial() == myPack, "the backpack serial is known");
        auto gold = MakeAddItem(0x40020010, 0x0EED, 1000, myPack);
        client->DispatchPacketForTest(gold.data(), gold.size());

        life::Runner runner;
        life::RunnerConfig rc;
        rc.dataRoot = root + "/trade_fix";
        rc.accountName = "trade_fix";
        rc.characterName = "trade_fix";
        rc.professionId = "fencer";
        std::string error;
        Check(runner.Configure(rc, &error), error.c_str());

        // Baelos's own broadcast, said and recorded a moment before the
        // window opens (JournalHeardSince is strictly-after, so the seeded
        // clock must lead the packets below or Aelia's own reply would be
        // filtered out as "not newer than when we asked").
        market::TradeIntent want;
        want.item = "i_bandage";
        want.qty = 118;
        want.pricePerUnit = 4;   // the ceiling shouted in our own WTB
        life::RunnerHarnessAccess::SeedTradeWant(runner, want, 5999000);

        life::Observation obs;
        obs.inWorld = true;
        obs.nowMs = 6000000;
        obs.goldOnHand = 1000;
        obs.gold = 1000;
        client->SetClockForTest(6000000);

        // The window opens -- Aelia dropping her own item, per the
        // seller-opens convention -- before Baelos has heard anything.
        auto open = MakeTradeOpen(partner, myContainer, theirContainer, "Aelia");
        client->DispatchPacketForTest(open.data(), open.size());
        Check(client->Trade().Active(), "trade window opened");

        // Aelia's real WTS reply, on the wire, at a price and quantity Baelos
        // never shouted himself.
        auto said = MakeAsciiMessage(partner, "Aelia", "WTS 10 i_bandage 3gp");
        client->DispatchPacketForTest(said.data(), said.size());

        // Her 10 bandages land in HER side of the window.
        auto goods = MakeAddItem(0x40020020, kBandageGfx, 10, theirContainer);
        client->DispatchPacketForTest(goods.data(), goods.size());

        // FIRST TICK ONLY OBSERVES. A real window opens on a single-item
        // drop and the seller's true quantity lands a moment later
        // (DriveOpenTrade's own kTradeSettleMs comment) -- so the buyer must
        // not price off the very first sighting, and this tick proves it
        // does not: no gold offered yet.
        life::RunnerHarnessAccess::DriveOpenTradeForTest(runner, *client, obs);
        Check(life::RunnerHarnessAccess::TradeOfferedQty(runner) == 0,
              "the first sighting of the seller's goods is watched, not "
              "paid for -- it could still be the opening token, not the "
              "real offer");

        // Past the settle window, with the count unchanged: NOW it prices.
        life::Observation settled = obs;
        settled.nowMs = obs.nowMs + 2600;
        life::RunnerHarnessAccess::DriveOpenTradeForTest(runner, *client, settled);
        Check(life::RunnerHarnessAccess::TradeOfferedQty(runner) == 10,
              "counted the 10 Aelia actually delivered, not the 118 asked for");
        Check(life::RunnerHarnessAccess::TradeOfferPrice(runner) == 3,
              "priced at Aelia's 3gp ask, never at our own 4gp ceiling");
        Check(life::RunnerHarnessAccess::GoldOwed(runner) == 30,
              "30 gold offered for 10 bandages at 3gp each, not 472 for 118 "
              "at a ceiling nobody agreed to");
        obs = settled;

        // Accept step: the window still holds exactly what was priced, so
        // this life accepts.
        life::RunnerHarnessAccess::DriveOpenTradeForTest(runner, *client, obs);

        // Both sides tick their accept and the server closes the window as
        // completed.
        auto change = MakeTradeChange(myContainer, true, true);
        client->DispatchPacketForTest(change.data(), change.size());
        auto close = MakeTradeClose(myContainer);
        client->DispatchPacketForTest(close.data(), close.size());
        // The client-side move request for the coin has received its normal
        // success before the completed handler is allowed to refresh the pack.
        client->CompleteActionForTest(act::Result::Success, "coin offered");

        // The completed handler refreshes the pack once before it reads the
        // Observation. A close packet can beat the normal inventory update on
        // the live shard, which previously made this completed trade look
        // empty even though both sides had accepted.
        life::Observation after = obs;
        after.pack.push_back({"i_bandage", 10});
        // The status-bar total includes banked coin while a player trade can
        // only move the 30 coins that left the backpack.  Keep the two
        // deliberately different so this regression catches a return to the
        // wrong purse baseline.
        after.gold = 8688;
        after.goldOnHand = 970;
        life::RunnerHarnessAccess::DoTradeWithPlayerForTest(runner, *client, after);
        Check(client->CurrentAction().kind == act::Kind::OpenContainer,
              "a completed trade refreshes the backpack before it verifies goods");
        client->CompleteActionForTest(act::Result::Success, "backpack refreshed");
        life::RunnerHarnessAccess::DoTradeWithPlayerForTest(runner, *client, after);
        Check(life::RunnerHarnessAccess::BelievedSalePrice(runner, "i_bandage") == 3,
              "got 10 for 30 gold -- the recorded observation is 3/unit, "
              "Aelia's real price");
    }

    // --- the poison guard discards an impossible observation ---------------
    // Isolated from the funding machinery above: seeds a "trade completed"
    // state directly and checks only the price-observation arithmetic in
    // DoTradeWithPlayer's Completed branch.
    {
        Client::Config config{};
        config.loginHost = "127.0.0.1";
        config.username = config.password = "trade_poison";
        config.version = "2.0.7";
        config.sessionTag = "trade_poison";
        auto client = std::make_unique<Client>(config);
        client->SetOfflineForTest(true);
        client->SetInWorldForTest();
        client->SetClockForTest(7000000);

        const u32 me = 0x0001A000;
        const u32 packSerial = 0x4003A010;
        auto login = MakeLoginConfirm(me, 100, 100);
        client->DispatchPacketForTest(login.data(), login.size());
        auto pack = MakeEquip(packSerial, 0x0E75, 0x15, me);
        client->DispatchPacketForTest(pack.data(), pack.size());

        const u32 partner = 0x0001A001;
        auto open = MakeTradeOpen(partner, 0x4003A001, 0x4003A002, "Wren");
        client->DispatchPacketForTest(open.data(), open.size());
        auto change = MakeTradeChange(0x4003A001, true, true);
        client->DispatchPacketForTest(change.data(), change.size());
        auto close = MakeTradeClose(0x4003A001);
        client->DispatchPacketForTest(close.data(), close.size());

        life::Runner runner;
        life::RunnerConfig rc;
        rc.dataRoot = root + "/trade_poison";
        rc.accountName = "trade_poison";
        rc.characterName = "trade_poison";
        rc.professionId = "fencer";
        std::string error;
        Check(runner.Configure(rc, &error), error.c_str());

        market::TradeIntent ceilingWant;
        ceilingWant.item = "i_bandage";
        ceilingWant.qty = 118;
        ceilingWant.pricePerUnit = 4;   // the ceiling this poisoned trade defied
        life::RunnerHarnessAccess::SeedCompletedBuy(
            runner, "i_bandage", ceilingWant, /*packBefore=*/0,
            /*goldBefore=*/470, /*agreedUnitPrice=*/4);

        // 10 delivered for 470 gold -- a real 47/unit, more than 3x the 4gp
        // ceiling this deal was supposed to have been bound by.
        life::Observation after;
        after.inWorld = true;
        after.nowMs = 7000000;
        after.gold = 0;
        after.pack.push_back({"i_bandage", 10});
        life::RunnerHarnessAccess::DoTradeWithPlayerForTest(runner, *client, after);
        Check(client->CurrentAction().kind == act::Kind::OpenContainer,
              "even a suspicious completed trade refreshes before pricing it");
        client->CompleteActionForTest(act::Result::Success, "backpack refreshed");
        life::RunnerHarnessAccess::DoTradeWithPlayerForTest(runner, *client, after);
        // NOT -1: i_bandage carries a shard-value seed (Market.cpp
        // kShardValueSeeds, 3gp) that BelievedSalePrice falls back to once
        // there is no PlayerTraded observation to prefer. Seeing that seed
        // -- rather than 47, PlayerTraded's own top rank -- is exactly the
        // proof the poisoned observation was never recorded: had it been,
        // this would read 47.
        Check(life::RunnerHarnessAccess::BelievedSalePrice(runner, "i_bandage") == 3,
              "a 47/unit observation against a 4gp ceiling is discarded, "
              "leaving only the shard seed -- never taught to the fleet");
    }

    // --- GET_TOOL keeps an owned work tool in the pack ----------------------
    // The gathering actions arm the one tool they need immediately before
    // use.  GET_TOOL owns the whole profession catalogue, so it must regard a
    // hatchet in the pack as complete rather than disarming a combat weapon
    // just to wield it: a crafter can own several hand tools but only wield
    // one at once.
    {
        const u32 me = 0x00019101;
        const u32 myPack = 0x40021001;
        const u32 sword = 0x40021002;
        const u32 hatchet = 0x40021003;
        const u16 kSwordGfx = 0x0F5E;    // a weapon graphic, not this tool's own
        const u16 kHatchetGfx = 0x0F43;  // i_hatchet's classic graphic

        Client::Config config{};
        config.loginHost = "127.0.0.1";
        config.username = config.password = "get_tool_swap";
        config.version = "2.0.7";
        config.sessionTag = "get_tool_swap";
        auto client = std::make_unique<Client>(config);
        client->SetOfflineForTest(true);
        client->SetInWorldForTest();
        client->SetClockForTest(8000000);

        auto login = MakeLoginConfirm(me, 100, 100);
        client->DispatchPacketForTest(login.data(), login.size());
        auto pack = MakeEquip(myPack, 0x0E75, 0x15, me);
        client->DispatchPacketForTest(pack.data(), pack.size());
        Check(client->BackpackSerial() == myPack, "the backpack serial is known");

        // The sword is worn on HAND1 before the goal ever runs.
        auto worn = MakeEquip(sword, kSwordGfx, life::runner_detail::kLayerHand1, me);
        client->DispatchPacketForTest(worn.data(), worn.size());
        Check(client->EquippedAtLayer(life::runner_detail::kLayerHand1) == sword,
              "the sword starts worn on HAND1");

        // The hatchet sits in the pack, never worn.
        auto item = MakeAddItem(hatchet, kHatchetGfx, 1, myPack);
        client->DispatchPacketForTest(item.data(), item.size());
        Check(client->FindBackpackItemByGraphic(kHatchetGfx) == hatchet,
              "the hatchet is in the pack");

        // Trainer payments are evaluated against the stack the NPC receives.
        // A five-coin change stack must not be selected when the 495-coin
        // stack beside it is the one that can pay a 295-gold course.
        const u32 smallGold = 0x40021004;
        const u32 lessonGold = 0x40021005;
        auto small = MakeAddItem(smallGold, 0x0EED, 5, myPack);
        auto large = MakeAddItem(lessonGold, 0x0EED, 495, myPack);
        client->DispatchPacketForTest(small.data(), small.size());
        client->DispatchPacketForTest(large.data(), large.size());
        Check(client->FindBackpackItemByGraphicAtLeast(0x0EED, 295) == lessonGold,
              "a trainer payment selects the stack that can pay the full quote");
        Check(client->FindBackpackItemByGraphicAtLeast(0x0EED, 496) == 0,
              "the lookup does not pretend split stacks are one payment");

        life::Runner runner;
        life::RunnerConfig rc;
        rc.dataRoot = root + "/get_tool_swap";
        rc.accountName = "get_tool_swap";
        rc.characterName = "get_tool_swap";
        rc.professionId = "fencer";
        std::string error;
        Check(runner.Configure(rc, &error), error.c_str());

        // A synthetic catalogue entry: a SRC.WEAPON tool, exactly the shape
        // ToolNeed::mustBeWielded exists for.
        prof::ToolNeed hatchetNeed;
        hatchetNeed.name = "hatchet";
        hatchetNeed.graphics = {kHatchetGfx};
        hatchetNeed.mustBeWielded = true;
        prof::Profession lumberjackSwordsman;
        lumberjackSwordsman.id = "lumberjack_swordsman_test";
        lumberjackSwordsman.tools = {hatchetNeed};
        life::RunnerHarnessAccess::SetToolProfessionForTest(runner,
                                                            &lumberjackSwordsman);

        life::Observation obs;
        obs.inWorld = true;
        obs.nowMs = 8000000;
        obs.x = 100; obs.y = 100; obs.z = 0;
        obs.gold = 0;

        Check(life::RunnerHarnessAccess::DoGetToolForTest(runner, *client, obs),
              "GET_TOOL is complete when its required tool is in the pack");
        bool changedHand = false;
        for (const auto& p : client->SentForTest()) {
            if ((p.opcode == 0x07 || p.opcode == 0x13) && p.bytes.size() >= 5)
                changedHand = true;
        }
        Check(!changedHand,
              "GET_TOOL does not disarm the current weapon for an owned tool");
    }

    // --- A REFUSED EQUIP IS AN ANSWER, AND FEMALE ARMOUR IS NEVER CHOSEN --
    //
    // In the 122x30 wave eleven characters re-sent the same equip every two
    // seconds for the whole session -- 779 requests, Wynven 169 of them, all
    // for one i_armor_female_studded (0x1C02) on a male body
    // (artifacts/fleet122c30_20260907/Wynven.console.txt, 21:16 onward).
    // The server refuses it in ei_equipitem's @EquipTest with a cliloc this
    // client cannot read, so the refusal looked like silence and the chooser
    // re-derived the same answer from the same unchanged pack.
    {
        const u32 me = 0x00019201;
        const u32 myPack = 0x40031001;
        const u32 bustier = 0x40031002;
        const u16 kFemaleStudded = 0x1C02;   // i_armor_female_studded
        const u8  kTorso = 13;               // the layer tiledata gives it

        Client::Config config{};
        config.loginHost = "127.0.0.1";
        config.username = config.password = "gear_refusal";
        config.version = "2.0.7";
        config.sessionTag = "gear_refusal";
        auto client = std::make_unique<Client>(config);
        client->SetOfflineForTest(true);
        client->SetInWorldForTest();
        client->SetClockForTest(9000000);

        // MakeLoginConfirm sends body 0x0190 -- the human MALE body.
        auto login = MakeLoginConfirm(me, 100, 100);
        client->DispatchPacketForTest(login.data(), login.size());
        Check(!client->PlayerIsFemale(), "the harness character is on a male body");
        auto pack = MakeEquip(myPack, 0x0E75, 0x15, me);
        client->DispatchPacketForTest(pack.data(), pack.size());

        auto piece = MakeAddItem(bustier, kFemaleStudded, 1, myPack);
        client->DispatchPacketForTest(piece.data(), piece.size());
        Check(client->FindBackpackItemByGraphic(kFemaleStudded) == bustier,
              "the female studded armour is in the pack");

        life::Runner runner;
        life::RunnerConfig rc;
        rc.dataRoot = root + "/gear_refusal";
        rc.accountName = "gear_refusal";
        rc.characterName = "gear_refusal";
        rc.professionId = "fencer";
        std::string error;
        Check(runner.Configure(rc, &error), error.c_str());

        // A life that does not fight, so the goal's own stand-down branch is
        // the one that runs after the wear pass produces nothing. Leather so
        // the class rule itself has no opinion about studded armour -- the
        // only thing that may refuse it here is the BODY.
        prof::Profession leatherCrafter;
        leatherCrafter.id = "leather_crafter_test";
        leatherCrafter.wears = prof::Profession::Wear::Leather;
        leatherCrafter.goldReserve = 0;
        life::RunnerHarnessAccess::SetToolProfessionForTest(runner,
                                                            &leatherCrafter);

        life::Observation obs;
        obs.inWorld = true;
        obs.nowMs = 9000000;
        obs.x = 100; obs.y = 100; obs.z = 0;
        obs.str = 50;              // well over the piece's ReqStr 35
        obs.gold = 50000;          // money is not what stops this
        obs.female = false;

        const life::ArmorPiece* femaleStudded =
            life::ArmorFor(kFemaleStudded);
        Check(femaleStudded != nullptr, "0x1C02 is in the armour table");
        Check(femaleStudded->sex == life::WearerSex::FemaleOnly,
              "0x1C02 is marked female-only from its ITEMDEF's CanUse mask");
        Check(!life::RunnerHarnessAccess::MayWearForTest(runner, *femaleStudded,
                                                         obs),
              "a male body may not wear female armour, whatever its strength");
        obs.female = true;
        Check(life::RunnerHarnessAccess::MayWearForTest(runner, *femaleStudded,
                                                        obs),
              "the same piece is fine on a female body -- the gate is the body, "
              "not the piece");
        obs.female = false;

        // 1. THE CHOOSER NEVER PICKS IT. One tick of the real goal, and no
        //    equip request for that item leaves the client at all.
        life::RunnerHarnessAccess::DoUpgradeGearForTest(runner, *client, obs);
        auto EquipsSent = [&client](u32 serial) {
            int n = 0;
            for (const auto& p : client->SentForTest()) {
                if (p.opcode != 0x13 || p.bytes.size() < 5) continue;
                if (LoadBE32(p.bytes.data() + 1) == serial) ++n;
            }
            return n;
        };
        Check(EquipsSent(bustier) == 0,
              "the gear chooser never asks to wear female armour on a male body");
        Check(runner.GetPlanner().Cooling(life::GoalKind::UpgradeGear, obs.nowMs),
              "and the goal stands down with a cooldown rather than being "
              "re-picked at tick rate");

        // 2. AND WHEN THE SERVER REFUSES SOMETHING WE DID PICK, ONE REFUSAL IS
        //    ENOUGH -- whatever the reason was.
        //
        // The wear pass cannot be driven the rest of the way here: it resolves
        // the layer with Client::ItemEquipLayer, which reads tiledata, and this
        // harness runs without the client MUL files (every armour row is
        // skipped for want of a layer, which is why part 1 above tests the
        // chooser's gate itself rather than counting on the loop). So the ask
        // is made the way the goal makes it -- the same ActionEquip, the same
        // recorded intent -- and the SERVER'S half is the real thing.
        obs.female = true;
        obs.nowMs += 60000;                     // past the cooldown above
        client->ActionEquip(bustier, kTorso);
        life::RunnerHarnessAccess::NotePendingWearForTest(runner, bustier,
                                                          kFemaleStudded);
        Check(EquipsSent(bustier) == 1, "the wear request went out once");

        // The server's answer: the item reappears in the pack instead of on
        // layer 13. Client::OnAddItemToContainer reports that as Rejected
        // (bb3c832) -- the same packet the live shard sent Wynven.
        auto bounced = MakeAddItem(bustier, kFemaleStudded, 1, myPack);
        client->DispatchPacketForTest(bounced.data(), bounced.size());
        Check(client->ActionResult() == act::Result::Rejected,
              "the bounce is read as a rejection, not a success");
        Check(client->EquippedGraphicAt(kTorso) == 0,
              "and nothing is worn on the torso layer");

        obs.nowMs += 2000;
        life::RunnerHarnessAccess::LearnForTest(runner, *client, obs);
        Check(life::RunnerHarnessAccess::RemembersUnwearable(runner,
                                                             kFemaleStudded),
              "the refusal is remembered against the graphic, not the serial");
        Check(!life::RunnerHarnessAccess::MayWearForTest(runner, *femaleStudded,
                                                         obs),
              "and the chooser will not pick it again EVEN ON A BODY THAT MAY "
              "wear it -- the remembered refusal is the general backstop, not "
              "a second reading of the sex table");

        // 3. THE SECOND TICK DOES NOT RE-ISSUE IT. This is the whole defect:
        //    two seconds later the pack was unchanged and the old code asked
        //    again, every two seconds, for the rest of the session.
        life::RunnerHarnessAccess::DoUpgradeGearForTest(runner, *client, obs);
        Check(EquipsSent(bustier) == 1,
              "the refused item is struck off -- no second equip, no 2-second "
              "retry loop");
        Check(runner.GetPlanner().Cooling(life::GoalKind::UpgradeGear, obs.nowMs),
              "with nothing left to wear the goal ends on a cooldown");

        // And the lesson is durable: it is the kind of thing that goes in the
        // state file, not a session-local flag (tests/m4_life.cpp round-trips
        // it).
        life::RunnerHarnessAccess::LearnForTest(runner, *client, obs);
        Check(life::RunnerHarnessAccess::RemembersUnwearable(runner,
                                                             kFemaleStudded),
              "a second look at the same finished action does not disturb it");

        // 4. BUT A REFUSAL WITH AN EXCUSE IS NOT A FACT ABOUT THE ITEM.
        //
        // Corus asked for leather leggings over the cloth long pants he was
        // already wearing and the server bounced them -- "You put the long
        // pants in your pack." (live smoke 2026-09-07 22:04:31). That stops
        // being true the moment the trousers come off, so the retry ends and
        // the ITEM TYPE is not written off. Only the piece asked for onto an
        // EMPTY layer earns a durable entry.
        const u32 leggings = 0x40031003;
        const u16 kLeatherLeggings = 0x13CB;
        const u16 kLongPants = 0x1539;      // ordinary cloth, armor 0
        auto legs = MakeAddItem(leggings, kLeatherLeggings, 1, myPack);
        client->DispatchPacketForTest(legs.data(), legs.size());
        client->ActionEquip(leggings, 23);
        life::RunnerHarnessAccess::NotePendingWearForTest(
            runner, leggings, kLeatherLeggings, kLongPants);
        auto legsBounced = MakeAddItem(leggings, kLeatherLeggings, 1, myPack);
        client->DispatchPacketForTest(legsBounced.data(), legsBounced.size());
        Check(client->ActionResult() == act::Result::Rejected,
              "the leggings bounce is a rejection too");
        obs.nowMs += 2000;
        life::RunnerHarnessAccess::LearnForTest(runner, *client, obs);
        Check(!life::RunnerHarnessAccess::RemembersUnwearable(runner,
                                                              kLeatherLeggings),
              "a refusal onto an OCCUPIED layer is not remembered as a fact "
              "about the piece -- the trousers are the reason, and they come off");
        const life::ArmorPiece* leatherLegs = life::ArmorFor(kLeatherLeggings);
        Check(leatherLegs != nullptr &&
              life::RunnerHarnessAccess::MayWearForTest(runner, *leatherLegs, obs),
              "so leather leggings are still something this character may wear");
    }

    // --- a hunt target that never retaliates is dropped, not re-picked
    // forever (Train.cpp, fleet122c30_20260907: "hunt: picked 'Spectre'" x28
    // with no swing on one character; target never retaliates, no exchange,
    // re-picked every ~2.5 s) -------------------------------------------
    {
        Client::Config config{};
        config.loginHost = "127.0.0.1";
        config.username = config.password = "offline_world";
        config.version = "2.0.7";
        config.sessionTag = "hunt_engage_giveup";
        config.atlasPath = atlasPath.c_str();
        config.navgridPath = gridPath.c_str();
        auto client = std::make_unique<Client>(config);
        client->SetOfflineForTest(true);
        client->SetInWorldForTest();
        client->SetClockForTest(1000000);

        life::Runner runner;
        life::RunnerConfig rc;
        rc.dataRoot = root + "/hunt_engage_giveup";
        rc.accountName = "offline_world";
        rc.characterName = "hunt_engage_giveup";
        rc.professionId = "fencer";
        std::string error;
        Check(runner.Configure(rc, &error), error.c_str());

        // Same unreachable-landmark spot the wind-down regression above
        // already proved is unguarded in this atlas. CurrentRegion() reads
        // world_knowledge_ directly and does not lazily load it itself --
        // WorldKnowledgeReady() (EnsureWorldKnowledge) has to run first.
        Check(client->WorldKnowledgeReady(), "real Client loads atlas and grid");
        Position(*client, 452, 450);
        Check(client->CurrentRegion() && !client->CurrentRegion()->flags.guarded,
              "hunting ground for this fixture is unguarded");
        // Sidesteps DoUpgradeGear's own "no gear yet" hand-off, which needs
        // real equipped-item packets this fixture has no reason to build --
        // the defect under test is the engage loop, not armour.
        life::RunnerHarnessAccess::CoolGearErrandForTest(runner, 1000000);

        const u32 foe = 0x40200001u;
        SpawnHostile(*client, foe, 457, 450, 3);
        auto nm = MakeMobName(foe, "Harness Ghoul");
        client->DispatchPacketForTest(nm.data(), nm.size());
        std::vector<Client::HostileHit> seen;
        Check(client->ScanHostiles(12, seen) == 1 && seen[0].name == "Harness Ghoul",
              "the hostile is scannable and named before the fixture starts");

        life::Observation obs;
        obs.inWorld = true;
        obs.x = 452; obs.y = 450;
        obs.hp = obs.hpMax = 100;
        obs.hostilesNear = 1;
        obs.underAttack = false;
        obs.attackersOnMe = 0;
        // Otherwise DoTrainCombat hands off to ReplaceEquipment ("restock
        // healing supplies before the next fight", Train.cpp) before ever
        // reaching the candidate-picking loop this fixture is testing.
        // Above kFighterBandageFloor (100, market.h) less one fight's worth,
        // so the bandage-floor gate (Train.cpp, above the engage loop this
        // fixture targets) does not hand off before ever reaching it.
        obs.bandages = 200;
        // Otherwise the FIRST gear gate (Train.cpp: "!obs.weaponEquipped")
        // hands off to ReplaceEquipment before the UpgradeGear cooldown
        // above (the SECOND gate, HasBasicArmor) is ever reached.
        obs.weaponEquipped = true;
        obs.nowMs = 1000000;

        // (a) Three opening attacks, none of them retaliated against -- the
        // exact evidence shape (obs.underAttack/attackersOnMe stay zero, so
        // DoTrainCombat keeps re-entering its own candidate-picking loop
        // instead of handing off to DoSurvive).
        for (int i = 0; i < 3; ++i) {
            obs.nowMs += 2500;
            client->SetClockForTest(obs.nowMs);
            life::RunnerHarnessAccess::DoTrainCombatForTest(runner, *client, obs);
            if (i == 0)
                Check(client->GotoTargetsForTest(456, 450),
                      "opening on distant non-retaliating prey requests an adjacent chase tile");
            client->CompleteActionForTest(act::Result::Timeout,
                                          "no swing landed -- test");
        }
        Check(life::RunnerHarnessAccess::HuntEngageTriesForTest(runner, foe) == 3,
              "three engage attempts were counted against the never-"
              "retaliating target");
        Check(!life::RunnerHarnessAccess::IsHuntExcludedForTest(runner, foe),
              "not excluded yet -- the budget is 3 tries, not fewer");

        obs.nowMs += 2500;
        client->SetClockForTest(obs.nowMs);
        life::RunnerHarnessAccess::DoTrainCombatForTest(runner, *client, obs);
        Check(life::RunnerHarnessAccess::IsHuntExcludedForTest(runner, foe),
              "the 4th tick gives up instead of re-attacking, and excludes "
              "the target for the rest of this trip");
        Check(life::RunnerHarnessAccess::CurrentFoeForTest(runner) == 0,
              "giving up clears currentFoe_ so nothing keeps chasing it");

        // (b) The only candidate in sight is now excluded: the goal must end
        // with a cooldown, not spin re-scanning the same excluded serial
        // every few seconds forever (the exclusion is trip-scoped, not the
        // 30 s unreachable_ window, so it would never clear on its own).
        obs.nowMs += 2500;
        client->SetClockForTest(obs.nowMs);
        Check(!runner.GetPlanner().Cooling(life::GoalKind::TrainCombat, obs.nowMs),
              "no cooldown yet, before the goal has had a chance to end");
        life::RunnerHarnessAccess::DoTrainCombatForTest(runner, *client, obs);
        Check(runner.GetPlanner().Cooling(life::GoalKind::TrainCombat, obs.nowMs),
              "with every visible hostile given up on, the goal ends on a "
              "cooldown instead of looping");
    }

    // --- a retaliating target is engaged normally and never excluded -------
    {
        Client::Config config{};
        config.loginHost = "127.0.0.1";
        config.username = config.password = "offline_world";
        config.version = "2.0.7";
        config.sessionTag = "hunt_engage_retaliates";
        config.atlasPath = atlasPath.c_str();
        config.navgridPath = gridPath.c_str();
        auto client = std::make_unique<Client>(config);
        client->SetOfflineForTest(true);
        client->SetInWorldForTest();
        client->SetClockForTest(1000000);

        life::Runner runner;
        life::RunnerConfig rc;
        rc.dataRoot = root + "/hunt_engage_retaliates";
        rc.accountName = "offline_world";
        rc.characterName = "hunt_engage_retaliates";
        rc.professionId = "fencer";
        std::string error;
        Check(runner.Configure(rc, &error), error.c_str());

        Check(client->WorldKnowledgeReady(), "real Client loads atlas and grid");
        Position(*client, 452, 450);
        life::RunnerHarnessAccess::CoolGearErrandForTest(runner, 1000000);

        const u32 foe = 0x40200002u;
        SpawnHostile(*client, foe, 453, 450, 3);
        auto nm = MakeMobName(foe, "Harness Skeleton");
        client->DispatchPacketForTest(nm.data(), nm.size());

        life::Observation obs;
        obs.inWorld = true;
        obs.x = 452; obs.y = 450;
        obs.hp = obs.hpMax = 100;
        obs.hostilesNear = 1;
        obs.underAttack = false;
        obs.attackersOnMe = 0;
        // Otherwise DoTrainCombat hands off to ReplaceEquipment ("restock
        // healing supplies before the next fight", Train.cpp) before ever
        // reaching the candidate-picking loop this fixture is testing.
        // Above kFighterBandageFloor (100, market.h) less one fight's worth,
        // so the bandage-floor gate (Train.cpp, above the engage loop this
        // fixture targets) does not hand off before ever reaching it.
        obs.bandages = 200;
        // Otherwise the FIRST gear gate (Train.cpp: "!obs.weaponEquipped")
        // hands off to ReplaceEquipment before the UpgradeGear cooldown
        // above (the SECOND gate, HasBasicArmor) is ever reached.
        obs.weaponEquipped = true;
        obs.nowMs = 1000000;

        // The opening attack: one engage attempt, exactly like the other
        // fixture's first tick.
        life::RunnerHarnessAccess::DoTrainCombatForTest(runner, *client, obs);
        Check(life::RunnerHarnessAccess::HuntEngageTriesForTest(runner, foe) == 1,
              "the opening attack counts as one engage attempt");

        // Now it retaliates. DoTrainCombat's own first line hands every
        // later tick to DoSurvive while underAttack/attackersOnMe hold, and
        // nothing on that path touches the engage-attempt budget -- a
        // fight that is actually happening must never be written off.
        obs.underAttack = true;
        obs.attackersOnMe = 1;
        for (int i = 0; i < 3; ++i) {
            obs.nowMs += 2500;
            client->SetClockForTest(obs.nowMs);
            life::RunnerHarnessAccess::DoTrainCombatForTest(runner, *client, obs);
        }
        Check(life::RunnerHarnessAccess::HuntEngageTriesForTest(runner, foe) == 1,
              "a retaliating target's try count never advances past the "
              "opening attack -- DoSurvive owns it now, not the picker");
        Check(!life::RunnerHarnessAccess::IsHuntExcludedForTest(runner, foe),
              "a retaliating target is never excluded");
    }

    // --- a target that is taking real damage but never retaliates is NOT
    // written off, even past the 3-try budget (owner brief 2026-09-07, live
    // wave fleet122d30_20260907: "giving up on 'Chickadee'"/"'Rat'" excluded
    // two animals mid-fight -- correct for Spectre, wrong for a fleeing or
    // dying target we are actually hitting). The only client-visible proof a
    // hit landed is the target's own health bar (0xA1), since Source-X sends
    // no per-swing packet. -----------------------------------------------
    {
        Client::Config config{};
        config.loginHost = "127.0.0.1";
        config.username = config.password = "offline_world";
        config.version = "2.0.7";
        config.sessionTag = "hunt_engage_damage_no_retaliate";
        config.atlasPath = atlasPath.c_str();
        config.navgridPath = gridPath.c_str();
        auto client = std::make_unique<Client>(config);
        client->SetOfflineForTest(true);
        client->SetInWorldForTest();
        client->SetClockForTest(1000000);

        life::Runner runner;
        life::RunnerConfig rc;
        rc.dataRoot = root + "/hunt_engage_damage_no_retaliate";
        rc.accountName = "offline_world";
        rc.characterName = "hunt_engage_damage_no_retaliate";
        rc.professionId = "fencer";
        std::string error;
        Check(runner.Configure(rc, &error), error.c_str());

        Check(client->WorldKnowledgeReady(), "real Client loads atlas and grid");
        Position(*client, 452, 450);
        Check(client->CurrentRegion() && !client->CurrentRegion()->flags.guarded,
              "hunting ground for this fixture is unguarded");
        life::RunnerHarnessAccess::CoolGearErrandForTest(runner, 1000000);

        const u32 foe = 0x40200003u;
        SpawnHostile(*client, foe, 453, 450, 3);
        auto nm = MakeMobName(foe, "Harness Rat");
        client->DispatchPacketForTest(nm.data(), nm.size());
        // Full health before the opening attack, so the FIRST engage attempt
        // anchors the exchange check at 100%.
        auto fullHp = MakeMobileHp(foe, 50, 50);
        client->DispatchPacketForTest(fullHp.data(), fullHp.size());

        life::Observation obs;
        obs.inWorld = true;
        obs.x = 452; obs.y = 450;
        obs.hp = obs.hpMax = 100;
        obs.hostilesNear = 1;
        obs.underAttack = false;
        obs.attackersOnMe = 0;
        obs.bandages = 200;
        obs.weaponEquipped = true;
        obs.nowMs = 1000000;

        // Opening attack: anchors HuntEngageStartHp at 100% (50/50). The
        // pending attack action must be resolved (as the Ghoul fixture does)
        // or client.ActionBusy() blocks every later tick from ever reaching
        // the candidate loop again.
        obs.nowMs += 2500;
        client->SetClockForTest(obs.nowMs);
        life::RunnerHarnessAccess::DoTrainCombatForTest(runner, *client, obs);
        client->CompleteActionForTest(act::Result::Timeout,
                                      "no swing landed -- test");
        Check(life::RunnerHarnessAccess::HuntEngageTriesForTest(runner, foe) == 1,
              "the opening attack counts as one engage attempt");

        // It never retaliates (obs.attackersOnMe stays 0), but it IS taking
        // damage -- the rat is fleeing at half health, not fighting back.
        auto dentedHp = MakeMobileHp(foe, 50, 25);
        client->DispatchPacketForTest(dentedHp.data(), dentedHp.size());

        // Two more attempts (tries 2 and 3): the give-up check only runs
        // once tries has already reached the budget, so these still fire
        // unconditionally, same as the never-damaged fixture above.
        for (int i = 0; i < 2; ++i) {
            obs.nowMs += 2500;
            client->SetClockForTest(obs.nowMs);
            life::RunnerHarnessAccess::DoTrainCombatForTest(runner, *client, obs);
            client->CompleteActionForTest(act::Result::Timeout,
                                          "no swing landed -- test");
        }
        Check(life::RunnerHarnessAccess::HuntEngageTriesForTest(runner, foe) == 3,
              "three engage attempts recorded, exactly like the never-"
              "damaged fixture");

        // The 4th tick is where the never-damaged fixture excludes the
        // target. Here the health bar has visibly dropped since the first
        // attack (100% -> 50%), so it must NOT be excluded -- the budget
        // resets and the fight continues instead.
        obs.nowMs += 2500;
        client->SetClockForTest(obs.nowMs);
        life::RunnerHarnessAccess::DoTrainCombatForTest(runner, *client, obs);
        Check(!life::RunnerHarnessAccess::IsHuntExcludedForTest(runner, foe),
              "a target taking damage without retaliating is never excluded");
        Check(life::RunnerHarnessAccess::CurrentFoeForTest(runner) == foe,
              "the fight keeps going -- the reset re-attacked the same foe "
              "this tick instead of walking away from it");
        Check(life::RunnerHarnessAccess::HuntEngageTriesForTest(runner, foe) == 1,
              "the try budget was reset to a fresh attempt (this tick's "
              "re-attack), not left pinned at the old ceiling");
        client->CompleteActionForTest(act::Result::Timeout,
                                      "no swing landed -- test");

        // Damage stops landing from here on (the rat has outrun the swing
        // range, say), so the SAME evidence rule must still give up once a
        // fresh 3-try budget also shows no further exchange.
        for (int i = 0; i < 2; ++i) {
            obs.nowMs += 2500;
            client->SetClockForTest(obs.nowMs);
            life::RunnerHarnessAccess::DoTrainCombatForTest(runner, *client, obs);
            client->CompleteActionForTest(act::Result::Timeout,
                                          "no swing landed -- test");
        }
        obs.nowMs += 2500;
        client->SetClockForTest(obs.nowMs);
        life::RunnerHarnessAccess::DoTrainCombatForTest(runner, *client, obs);
        Check(life::RunnerHarnessAccess::IsHuntExcludedForTest(runner, foe),
              "once the health bar stops moving too, the same target is "
              "still eventually written off");
    }

    // A ghost must not walk past a healer that wanders into view on the way
    // to the atlas healer. A nearby live healer stops the stale landmark
    // journey so Sphere can offer resurrection immediately.
    {
        Client::Config config{};
        config.loginHost = "127.0.0.1";
        config.username = config.password = "offline_wandering_healer";
        config.version = "2.0.7";
        config.sessionTag = "wandering_healer";
        config.atlasPath = atlasPath.c_str();
        config.navgridPath = gridPath.c_str();
        auto client = std::make_unique<Client>(config);
        client->SetOfflineForTest(true);
        client->SetInWorldForTest();
        constexpr i64 kNow = 3000000;
        client->SetClockForTest(kNow);
        Position(*client, 450, 450);
        Check(client->WorldKnowledgeReady(), "wandering-healer fixture loads its world");

        life::Runner runner;
        life::RunnerConfig rc;
        rc.dataRoot = root + "/wandering_healer";
        rc.accountName = "offline_wandering_healer";
        rc.characterName = "wandering_healer";
        rc.professionId = "fencer";
        std::string error;
        Check(runner.Configure(rc, &error), "wandering-healer runner configures");

        u8 death[2] = {0x2C, 0x00};
        client->DispatchPacketForTest(death, sizeof(death));
        Check(client->TravelToService(wm::Service::Healer),
              "a dead bot can begin the ordinary atlas healer route");

        constexpr u32 kHealer = 0x4020A001u;
        SpawnHostile(*client, kHealer, 451, 450, 1);
        const auto doll = MakePaperdoll(kHealer, "Mara, the healer");
        client->DispatchPacketForTest(doll.data(), doll.size());
        life::Observation ghost;
        ghost.inWorld = true;
        ghost.dead = true;
        ghost.nowMs = kNow;
        ghost.x = 450; ghost.y = 450;
        ghost.hp = ghost.hpMax = 1;
        life::RunnerHarnessAccess::Survive(runner, *client, ghost);
        Check(!client->TravelBusy(),
              "a visible wandering healer replaces the fixed healer route");
        int resurrectionRequests = 0;
        for (const auto& p : client->SentForTest())
            if (p.opcode == 0x03) ++resurrectionRequests;
        Check(resurrectionRequests == 1,
              "a ghost beside the named healer asks to be resurrected once");

        // Seeing the same healer across the room must continue its trip; it
        // is not close enough to understand a resurrection request. This is
        // the Arvdris fleet regression where the old else branch said to
        // Ayuna while the active entity journey was still 24 tiles away.
        Client::Config farConfig = config;
        farConfig.sessionTag = "wandering_healer_far";
        auto farClient = std::make_unique<Client>(farConfig);
        farClient->SetOfflineForTest(true);
        farClient->SetInWorldForTest();
        farClient->SetClockForTest(kNow);
        Position(*farClient, 450, 450);
        life::Runner farRunner;
        life::RunnerConfig farRc = rc;
        farRc.dataRoot = root + "/wandering_healer_far";
        farRc.characterName = "wandering_healer_far";
        Check(farRunner.Configure(farRc, &error), "far-healer runner configures");
        farClient->DispatchPacketForTest(death, sizeof(death));
        constexpr u32 kFarHealer = 0x4020A002u;
        SpawnHostile(*farClient, kFarHealer, 458, 450, 1);
        const auto farDoll = MakePaperdoll(kFarHealer, "Mara, the healer");
        farClient->DispatchPacketForTest(farDoll.data(), farDoll.size());
        life::RunnerHarnessAccess::Survive(farRunner, *farClient, ghost);
        farClient->ClearSentForTest();
        ghost.nowMs += 4000;
        farClient->SetClockForTest(ghost.nowMs);
        life::RunnerHarnessAccess::Survive(farRunner, *farClient, ghost);
        resurrectionRequests = 0;
        for (const auto& p : farClient->SentForTest())
            if (p.opcode == 0x03) ++resurrectionRequests;
        Check(resurrectionRequests == 0,
              "a distant healer is not asked to resurrect until the ghost arrives");
    }

    // --- a caster moves to establish line of sight before spending a spell --
    // (Train.cpp, fleet122combat_20260909: casters repeatedly received
    // "Target is not in line of sight" at 6-8 tiles and never chased.)
    {
        Client::Config config{};
        config.loginHost = "127.0.0.1";
        config.username = config.password = "offline_world";
        config.version = "2.0.7";
        config.sessionTag = "caster_line_of_sight";
        config.atlasPath = atlasPath.c_str();
        config.navgridPath = gridPath.c_str();
        auto client = std::make_unique<Client>(config);
        client->SetOfflineForTest(true);
        client->SetInWorldForTest();
        client->SetClockForTest(1100000);

        life::Runner runner;
        life::RunnerConfig rc;
        rc.dataRoot = root + "/caster_line_of_sight";
        rc.accountName = "offline_world";
        rc.characterName = "caster_line_of_sight";
        rc.professionId = "fencer";
        std::string error;
        Check(runner.Configure(rc, &error), error.c_str());
        life::RunnerHarnessAccess::SetProfessionForTest(runner, prof::Find("mage"));
        life::RunnerHarnessAccess::CoolGearErrandForTest(runner, 1100000);
        Check(client->WorldKnowledgeReady(), "caster fixture loads atlas and grid");
        Position(*client, 452, 450);

        const u32 foe = 0x40200011u;
        SpawnHostile(*client, foe, 457, 450, 3);
        auto name = MakeMobName(foe, "Blocked Skeleton");
        client->DispatchPacketForTest(name.data(), name.size());

        // A real spellbook page records its spell number in the amount field.
        // The synthetic harness has no MUL sight data, so this target is
        // deliberately not visible and exercises the approach branch.
        const auto savedSpells = spell::SpellTable();
        spell::LoadSpellTableFromText("spell\tdefname\tname\tcircle\tminskill\tmana\tflags\treagents\n"
            "10\ts_harm\tHarm\t2\t100\t4\tspellflag_targ_char|spellflag_harm|spellflag_damage\t\n");
        constexpr u32 book = 0x40005570u;
        auto page = MakeAddItem(0x40005571u, 0x1F2E, 10, book);
        client->DispatchPacketForTest(page.data(), page.size());

        life::Observation obs;
        obs.inWorld = true;
        obs.x = 452; obs.y = 450;
        obs.hp = obs.hpMax = 100;
        obs.mana = obs.manaMax = 50;
        obs.hostilesNear = 1;
        obs.bandages = 200;
        obs.spellbookSerial = book;
        obs.skills = {{rules::kMagery, 500}};
        obs.nowMs = 1100000;
        client->ClearSentForTest();
        life::RunnerHarnessAccess::DoTrainCombatForTest(runner, *client, obs);
        Check(client->GotoTargetsForTest(454, 450),
              "a caster without sight walks to a three-tile firing position");
        int casts = 0;
        for (const auto& p : client->SentForTest()) if (p.opcode == 0x12) ++casts;
        Check(casts == 0,
              "a caster does not waste a spell while the target is outside line of sight");
        Check(life::RunnerHarnessAccess::HuntEngageTriesForTest(runner, foe) == 0,
              "approaching for sight is not counted as a failed combat exchange");

        // The local map said the next cast was viable, but the server rejects
        // it after the target moves behind cover.  This is the live fleet
        // race: the authoritative refusal must exclude the prey before the
        // picker can send a second spell.
        client->ActionCastSpell(1, foe);
        auto refusal = MakeAsciiMessage(0xFFFFFFFF, "System",
                                        "Target is not in line of sight");
        client->DispatchPacketForTest(refusal.data(), refusal.size());
        // Reproduce the fleet race: another combat action begins before the
        // runner's next tick.  The refusal target must not be lost with the
        // completed cast action.
        client->ActionAttack(foe);
        client->ClearSentForTest();
        obs.nowMs += 2500;
        client->SetClockForTest(obs.nowMs);
        life::RunnerHarnessAccess::DoTrainCombatForTest(runner, *client, obs);
        Check(life::RunnerHarnessAccess::IsUnreachableForTest(runner, foe, obs.nowMs),
              "a server-rejected hunt spell temporarily excludes that target");
        casts = 0;
        for (const auto& p : client->SentForTest()) if (p.opcode == 0x12) ++casts;
        Check(casts == 0,
              "the rejected target receives no immediate second spell");

        // The same completed action reaches the defensive path after a
        // hostile retaliates.  It must get the same authoritative treatment.
        const u32 defensiveFoe = 0x40200012u;
        SpawnHostile(*client, defensiveFoe, 453, 451, 3);
        auto defensiveName = MakeMobName(defensiveFoe, "Harness Zombie");
        client->DispatchPacketForTest(defensiveName.data(), defensiveName.size());
        client->ActionCastSpell(1, defensiveFoe);
        client->CompleteActionForTest(act::Result::Rejected,
                                      "Target is not in line of sight");
        client->ClearSentForTest();
        obs.underAttack = true;
        obs.attackersOnMe = 1;
        obs.hostilesNear = 1;
        obs.nowMs += 2500;
        client->SetClockForTest(obs.nowMs);
        life::RunnerHarnessAccess::Survive(runner, *client, obs);
        Check(life::RunnerHarnessAccess::IsUnreachableForTest(
                  runner, defensiveFoe, obs.nowMs),
              "a server-rejected defensive spell also excludes that target");
        casts = 0;
        for (const auto& p : client->SentForTest()) if (p.opcode == 0x12) ++casts;
        Check(casts == 0,
              "defensive combat does not immediately re-cast through blocked sight");
        spell::SpellTable() = savedSpells;
    }

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
