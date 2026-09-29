#include "Client.h"
#include "life/Runner.h"
#include "uo/endian.h"
#include <cstdio>
#include <cstring>
#include <memory>

using namespace uo;
using namespace uo::life;
namespace uo::life {
struct RunnerHarnessAccess {
    static void Setup(Runner& r, const prof::Profession* p, const char* name) {
        r.needCfg_.profession = p;
        r.state_.identity.characterName = name;
    }
    static PersistentState& State(Runner& r) { return r.state_; }
    static void Tick(Runner& r, Client& c, const Observation& o) { r.TickCraftOrders(c, o); }
    static void Needs(Runner& r, const Observation& o, std::vector<Need>& n) { r.AddCraftOrderNeeds(o, n); }
    static bool Drive(Runner& r, Client& c, const Observation& o) { return r.DriveCraftOrder(c, o); }
    static u32 Partner(const Runner& r) { return r.tradePartner_; }
    static bool OpenOrder(Runner& r, Client& c, const Observation& o) {
        r.DriveOpenTrade(c, o);
        return r.tradeWantQty_ == 3 && r.tradeOfferPrice_ == 200 && r.tradeItem_ == "i_dagger";
    }
    static void Settle(Runner& r, const Observation& o, u32 partner, bool selling, int before, int gold) {
        r.tradePartner_ = partner; r.tradeItem_ = "i_dagger";
        for (const auto& order : r.state_.craftOrders)
            if (order.partnerSerial == partner && order.buying != selling) r.tradeOrderId_ = order.id;
        r.tradeSellingQty_ = selling ? 3 : 0;
        r.tradePackBefore_ = before; r.tradeGoldBefore_ = gold;
        r.SettleCraftOrder(o);
    }
};
}
int failures = 0, checks = 0;
void Check(bool ok, const char* reason) {
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL: %s\n", reason); }
}
std::unique_ptr<Client> ClientForTest() {
    Client::Config cfg{};
    auto c = std::make_unique<Client>(cfg);
    c->SetOfflineForTest(true); c->SetInWorldForTest(); c->SetClockForTest(1000000);
    u8 login[37]{}; login[0] = 0x1B;
    StoreBE32(login + 1, 1); StoreBE16(login + 9, 0x190);
    StoreBE16(login + 11, 100); StoreBE16(login + 13, 100);
    c->DispatchPacketForTest(login, sizeof(login));
    return c;
}
void Hear(Client& c, u32 who, const char* name, const std::string& text) {
    u8 mobile[23]{}; mobile[0] = 0x78;
    StoreBE16(mobile + 1, sizeof(mobile)); StoreBE32(mobile + 3, who);
    StoreBE16(mobile + 7, 0x190); StoreBE16(mobile + 9, 101); StoreBE16(mobile + 11, 100);
    mobile[18] = 1; c.DispatchPacketForTest(mobile, sizeof(mobile));
    std::vector<u8> speech(45 + text.size(), 0); speech[0] = 0x1C;
    StoreBE16(speech.data() + 1, static_cast<u16>(speech.size()));
    StoreBE32(speech.data() + 3, who);
    std::memcpy(speech.data() + 14, name, std::min<usize>(29, std::strlen(name)));
    std::memcpy(speech.data() + 44, text.c_str(), text.size() + 1);
    c.DispatchPacketForTest(speech.data(), speech.size());
}
CraftOrder Order() {
    CraftOrder o; o.id = "2_1000000"; o.terms = {"i_dagger", 3, 200};
    o.createdMs = 1000000; o.expiresMs = 1900000; o.x = 101; o.y = 100;
    return o;
}
int main() {
    auto order = Order(); CraftOrder parsed; std::string verb;
    Check(ParseCraftOrder(FormatCraftOrder(order, "REQUEST"), verb, parsed), "order round trip");
    Check(parsed.terms.qty == 3 && parsed.terms.pricePerUnit == 200 && parsed.id == order.id, "terms preserved");
    Check(ParseCraftOrder(FormatCraftOrder(order, "ACCEPT", "Alice"), verb, parsed) && verb == "ACCEPT", "addressed acceptance parses");
    for (const auto* bad : {"ORDER REQUEST x -1 i_dagger 1gp", "ORDER REQUEST x 201 i_dagger 1gp",
         "ORDER REQUEST x 1 i_dagger 0gp", "ORDER REQUEST x 1 i_dagger 9999999999999999999999gp",
         "ORDER REQUEST x 1 i_dagger 1gp junk", "ORDER REQUEST x 1 unknown! 1gp",
         "ORDER REQUEST x 200 i_dagger 1000gp"})
        Check(!ParseCraftOrder(bad, verb, parsed), "malformed orders refused");
    Check(!order.Active(999999) && order.Active(1000000) && !order.Active(1900000), "expiry and clock-reset bounds");

    prof::Profession smith; smith.id = "smith_test"; smith.produces = {"i_dagger"};
    Observation obs; obs.inWorld = true; obs.nowMs = 1001000;
    obs.x = obs.y = 100; obs.food = 10; obs.gold = obs.goldOnHand = 5000;
    obs.hp = obs.hpMax = 100; obs.maxWeight = 400;
    obs.skills = {{rules::kBlacksmithing, 1000}};
    obs.pack = {{"i_ingot_iron", 100}};
    auto client = ClientForTest(); Runner seller;
    RunnerHarnessAccess::Setup(seller, &smith, "Alice");
    Hear(*client, 2, "Bob", FormatCraftOrder(order, "REQUEST"));
    RunnerHarnessAccess::Tick(seller, *client, obs);
    auto& state = RunnerHarnessAccess::State(seller);
    Check(state.craftOrders.size() == 1 && state.craftOrders[0].partnerSerial == 2, "capable crafter accepts heard request");
    if (state.craftOrders.empty()) return 1;
    Check(state.productionBatch.item == "i_dagger" && state.productionBatch.attempts == 3, "order pins its own production quantity");
    UpdateProductionBatch(state.productionBatch, smith, obs);
    std::vector<Need> needs;
    RunnerHarnessAccess::Needs(seller, obs, needs);
    Check(!needs.empty() && needs[0].kind == NeedKind::NeedCraft && !needs[0].blocked, "funded order raises actionable crafting need");
    client->SetClockForTest(1002000); obs.nowMs = 1002000;
    Hear(*client, 2, "Bob", FormatCraftOrder(order, "REQUEST"));
    RunnerHarnessAccess::Tick(seller, *client, obs);
    Check(state.craftOrders.size() == 1, "repeat request does not duplicate production");
    obs.pack.push_back({"i_dagger", 3});
    RunnerHarnessAccess::Tick(seller, *client, obs);
    Check(state.craftOrders[0].phase == OrderPhase::Ready, "actual finished stock makes order ready");
    Check(state.productionBatch.item.empty(), "ready stock stops replacement material shopping");
    state.craftOrders[0].x = 1000;
    state.craftOrders[0].y = 1000;
    Check(RunnerHarnessAccess::Drive(seller, *client, obs) &&
          RunnerHarnessAccess::Partner(seller) == 2 && !client->TravelBusy(),
          "visible customer can receive goods without revisiting the old meeting tile");
    needs.clear(); RunnerHarnessAccess::Needs(seller, obs, needs);
    Check(needs[0].kind == NeedKind::NeedTrade, "ready order raises delivery need");
    obs.pack.back().qty = 0; obs.goldOnHand = 5000;
    RunnerHarnessAccess::Settle(seller, obs, 2, true, 3, 5000);
    Check(state.craftOrders[0].delivered == 0, "missing goods without payment do not complete order");
    obs.goldOnHand = 5200; obs.pack.back().qty = 2;
    RunnerHarnessAccess::Settle(seller, obs, 2, true, 3, 5000);
    Check(state.craftOrders[0].delivered == 1 && state.craftOrders[0].phase == OrderPhase::Ready, "paid partial delivery recorded without completing");
    obs.goldOnHand = 5600; obs.pack.back().qty = 0;
    RunnerHarnessAccess::Settle(seller, obs, 2, true, 2, 5200);
    Check(state.craftOrders[0].phase == OrderPhase::Completed, "verified final goods and payment complete order");

    {
        Runner stocked;
        RunnerHarnessAccess::Setup(stocked, &smith, "Alice");
        auto stockedClient = ClientForTest();
        Observation stockedObs = obs; stockedObs.pack = {{"i_dagger", 3}};
        stockedObs.skills.clear();
        Hear(*stockedClient, 2, "Bob", FormatCraftOrder(order, "REQUEST"));
        RunnerHarnessAccess::Tick(stocked, *stockedClient, stockedObs);
        const auto& jobs = RunnerHarnessAccess::State(stocked).craftOrders;
        Check(jobs.size() == 1 && jobs[0].phase == OrderPhase::Ready,
              "producer can fill an order from existing output without buying replacement materials");
    }

    PersistentState loaded; std::string error;
    Check(FromJson(ToJson(state), &loaded, &error), "order state round trip loads");
    Check(loaded.craftOrders.size() == 1 && loaded.craftOrders[0].delivered == 3 &&
          loaded.craftOrders[0].phase == OrderPhase::Completed, "delivery evidence persists");

    auto buyerClient = ClientForTest(); Runner buyer;
    prof::Profession customer; customer.id = "customer"; customer.goldReserve = 100;
    RunnerHarnessAccess::Setup(buyer, &customer, "Alice");
    auto request = Order(); request.buying = true; request.targetStock = 3;
    RunnerHarnessAccess::State(buyer).craftOrders.push_back(request);
    obs.pack.clear(); obs.gold = obs.goldOnHand = 5000;
    Hear(*buyerClient, 2, "Bob", FormatCraftOrder(request, "ACCEPT", "Alice"));
    RunnerHarnessAccess::Tick(buyer, *buyerClient, obs);
    auto& accepted = RunnerHarnessAccess::State(buyer).craftOrders[0];
    Check(accepted.partnerSerial == 2 && accepted.phase == OrderPhase::Accepted,
          "buyer remembers the first crafter at the quoted terms");
    {
        Runner earlyBuyer;
        RunnerHarnessAccess::Setup(earlyBuyer, &customer, "Alice");
        RunnerHarnessAccess::State(earlyBuyer).craftOrders.push_back(accepted);
        auto earlyClient = ClientForTest();
        u8 open[47]{}; open[0] = 0x6F; StoreBE16(open + 1, sizeof(open));
        StoreBE32(open + 4, 2); StoreBE32(open + 8, 0x40000001);
        StoreBE32(open + 12, 0x40000002); open[16] = 1;
        std::memcpy(open + 17, "Bob", 3);
        earlyClient->DispatchPacketForTest(open, sizeof(open));
        Check(RunnerHarnessAccess::OpenOrder(earlyBuyer, *earlyClient, obs),
              "window before READY recovers agreed terms from accepted partner");
    }
    buyerClient->SetClockForTest(1003000); obs.nowMs = 1003000;
    Hear(*buyerClient, 3, "Carol", FormatCraftOrder(request, "ACCEPT", "Alice"));
    RunnerHarnessAccess::Tick(buyer, *buyerClient, obs);
    Check(accepted.partnerSerial == 2, "a second acceptance cannot replace the chosen crafter");
    buyerClient->SetClockForTest(1004000); obs.nowMs = 1004000;
    Hear(*buyerClient, 3, "Carol", FormatCraftOrder(request, "READY", "Alice"));
    RunnerHarnessAccess::Tick(buyer, *buyerClient, obs);
    Check(accepted.phase == OrderPhase::Accepted, "another speaker cannot mark an order ready");
    buyerClient->SetClockForTest(1005000); obs.nowMs = 1005000;
    Hear(*buyerClient, 2, "Bob", FormatCraftOrder(request, "READY", "Alice"));
    RunnerHarnessAccess::Tick(buyer, *buyerClient, obs);
    Check(accepted.phase == OrderPhase::Ready, "chosen crafter's ready notice is remembered");
    buyerClient->SetClockForTest(1005500); obs.nowMs = 1005500;
    Hear(*buyerClient, 2, "Bob", FormatCraftOrder(request, "ACCEPT", "Alice"));
    RunnerHarnessAccess::Tick(buyer, *buyerClient, obs);
    Check(accepted.phase == OrderPhase::Ready, "repeat acceptance cannot downgrade a ready order");
    obs.pack = {{"i_dagger", 3}}; obs.nowMs = 1006000;
    RunnerHarnessAccess::Tick(buyer, *buyerClient, obs);
    Check(accepted.phase == OrderPhase::Cancelled, "buying the goods elsewhere cancels the outstanding need");
    accepted.phase = OrderPhase::Accepted; obs.nowMs = accepted.expiresMs;
    RunnerHarnessAccess::Tick(buyer, *buyerClient, obs);
    Check(accepted.phase == OrderPhase::Expired, "absent partner cannot hold an order open forever");

    accepted.phase = OrderPhase::Ready;
    obs.pack = {{"i_dagger", 3}}; obs.goldOnHand = 4400;
    RunnerHarnessAccess::Settle(buyer, obs, 2, false, 0, 5000);
    Check(accepted.phase == OrderPhase::Completed && accepted.delivered == 3,
          "agreed transfer settling at the deadline still records its goods and payment");

    // Bank stock counts with pack stock, and a future Poisoning plan alone
    // must not create another green-bottle order.
    prof::Profession mage; mage.id = "mage_test"; mage.consumes = {"i_potion_poison"};
    Observation mageObs = obs; mageObs.pack.clear(); mageObs.bank.clear();
    Check(!CraftOrderWant(mage, mageObs, {}, {}).Valid(), "inactive poisoning creates no order");
    mageObs.food = 0; mageObs.bank = {{"i_fish_cut_cooked", 10}};
    Check(!CraftOrderWant(mage, mageObs, {}, {}).Valid(), "banked cooked food prevents duplicate food order");
    mageObs.food = 10; mageObs.bank.clear();
    mageObs.wantPracticeSkill = rules::kPoisoning;
    mageObs.pack = {{"i_potion_poison", 10}}; mageObs.bank = {{"i_potion_poison", 10}};
    Check(!CraftOrderWant(mage, mageObs, {}, {}).Valid(), "pack plus bank avoids duplicate supply orders");
    const auto* realMage = prof::Find("mage");
    mageObs.pack = {{"i_dagger", 1}}; mageObs.bank.clear(); mageObs.wantPracticeSkill = -1;
    if (realMage) {
        const auto need = CraftOrderWant(*realMage, mageObs, {}, {});
        Check(need.Valid() && need.item == "i_potion_heal", "mage's human-readable potion need becomes a craftable order");
    } else Check(false, "mage profession exists");
    {
        auto fishClient = ClientForTest();
        prof::Profession fisher; fisher.id = "fisher";
        fisher.produces = {"i_fish_cut_cooked"};
        Runner fishRunner; RunnerHarnessAccess::Setup(fishRunner, &fisher, "Fisher");
        auto request = Order(); request.terms = {"i_fish_cut_cooked", 5, 12};
        Observation stockedFish; stockedFish.inWorld = true; stockedFish.nowMs = 1000000;
        stockedFish.pack = {{"i_fish_cut_cooked", 100}};
        Hear(*fishClient, 2, "Buyer", FormatCraftOrder(request, "REQUEST"));
        RunnerHarnessAccess::Tick(fishRunner, *fishClient, stockedFish);
        Check(RunnerHarnessAccess::State(fishRunner).craftOrders.empty(),
              "a fisher with cooked stock does not accept player commissions");
    }
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
