// S5 -- traders go to the market.
//
// Four separate claims, each of which was false before this slice and each of
// which has to stay true:
//
//   1. THE MARKET IS A PLACE THE ATLAS KNOWS. market::kMarketBankPlaceId is an
//      id, never a coordinate pair, and the atlas is the only source for where
//      it is. atlasgen slugs place ids, so a regenerated atlas could rename or
//      move britain_bank_2 -- and if it does, this test is the tripwire rather
//      than a fleet walking to nowhere.
//   2. A SHORTFALL IS A REASON TO GO. Only Surplus() ever produced NeedTrade,
//      so a smith short of logs never scored TRADE_WITH_PLAYER at all and the
//      buyer half of every trade was unreachable.
//   3. A RAW RESOURCE IS NOT A PLAYER GOOD. Iron ore is short in exactly the
//      same way a log is, and it is a mining trip, not a rendezvous.
//   4. A GOAL THAT ACHIEVED NOTHING MUST REST, AND A MARKET TRIP MUST FIT ON
//      THE CLOCK. fleet7 re-picked TRADE_WITH_PLAYER 244 ms after standing it
//      down; and one measured leg is 250 s against a 300 s goal limit.
//
// Needs the generated atlas (argv[1] = the data directory) and nothing else --
// no server, no MULs, no network.

#include "uo/life.h"
#include "uo/market.h"
#include "uo/professions.h"
#include "uo/world_model.h"

#include "world/Atlas.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what);
    }
}

void Section(const char* name) { std::printf("[%s]\n", name); }

using namespace uo;

const life::Need* Find(const std::vector<life::Need>& needs, life::NeedKind k) {
    for (const life::Need& n : needs) {
        if (n.kind == k) return &n;
    }
    return nullptr;
}

int CountOf(const std::vector<life::Need>& needs, life::NeedKind k) {
    int n = 0;
    for (const life::Need& x : needs) {
        if (x.kind == k) ++n;
    }
    return n;
}

// A life standing in the world with nothing wrong with it, so the only needs
// that appear are the ones the case is about.
life::Observation Standing(i32 gold) {
    life::Observation obs;
    obs.nowMs = 5000000;
    obs.inWorld = true;
    obs.x = 1425; obs.y = 1690;
    obs.hp = 60; obs.hpMax = 60;
    obs.str = 60; obs.dex = 30; obs.intel = 20;
    obs.gold = gold;
    obs.goldOnHand = gold < 800 ? gold : 800;
    obs.weight = 100; obs.maxWeight = 400;
    obs.bandages = 40;
    obs.healPotions = 10;
    obs.food = 3;
    return obs;
}

// --------------------------------------------------------------------------
// A. The place. Coordinates come from the atlas, never from this file.
// --------------------------------------------------------------------------
void TestMarketPlace(const std::string& dataDir) {
    Section("A: the market is a place the atlas knows");

    world_atlas::Atlas atlas;
    std::string err;
    const std::string path = dataDir + "/revolution_atlas.txt";
    if (!atlas.Load(path.c_str(), &err)) {
        Check(false, "the generated atlas loads");
        std::printf("  (%s: %s)\n", path.c_str(), err.c_str());
        return;
    }

    const wm::Place* p = atlas.FindPlace(market::kMarketBankPlaceId);
    Check(p != nullptr, "the atlas still has the market place id");
    if (!p) return;

    // 1425,1690 is what atlasgen read out of the shard's own save
    // (runtime/save/sphereb01w.scp:498579, c_banker at P=1425,1690). If the
    // atlas ever says something else, the bot must follow the atlas -- but
    // somebody has to be told, and this is the telling.
    Check(p->position.x == 1425 && p->position.y == 1690,
          "the market is still at 1425,1690");
    Check(p->regionId == "a_townBritain", "and still inside a_townBritain");
    Check(p->Offers(wm::Service::Banker),
          "it offers a banker -- the runtime refuses it otherwise");
    Check(atlas.PlaceIsGuarded(*p),
          "it is guarded, so ending a session there is safe");
    Check(p->radius > 0, "it has an arrival radius to aim at");

    // The OTHER Britain bank exists and is a different place. This is why the
    // constant is an explicit id and not TravelToService(Banker, "Britain"):
    // that call picks the nearer of the two from the caller's position, so a
    // smith arriving from Minoc and a Britain resident would stand 250 tiles
    // apart and each report having reached "the Britain bank".
    const wm::Place* other = atlas.PlaceById("britain_bank");
    Check(other != nullptr, "Britain has a second bank");
    if (other) {
        const i32 dx = other->position.x - p->position.x;
        const i32 dy = other->position.y - p->position.y;
        Check(dx != 0 || dy != 0,
              "and it is somewhere else, which is the whole reason the market "
              "is named by id rather than by service");
    }
}

// --------------------------------------------------------------------------
// B-D, F. The buyer predicate.
// --------------------------------------------------------------------------
void TestBuyerWants() {
    Section("B: a smith short of logs wants to buy");

    const prof::Profession* smith = prof::Find("miner_smith");
    Check(smith != nullptr, "the catalogue still has miner_smith");
    if (!smith) return;

    const market::TradePolicy policy;

    // The pack carries no logs at all. i_log is produced by
    // lumberjack_swordsman, so it is a player good.
    {
        std::vector<market::Stock> pack;
        const char* why = nullptr;
        const std::vector<market::Want> want =
            market::PlayerMarketWants(*smith, pack, 1000, policy, &why);
        Check(!want.empty(), "a smith with 1000 gold and no logs wants to buy");
        bool sawLog = false, sawOre = false;
        for (const market::Want& w : want) {
            if (w.item == "i_log") sawLog = true;
            if (w.item == "i_ore_iron") sawOre = true;
        }
        Check(sawLog, "and the thing it wants is a log");
        Check(!sawOre,
              "F: iron ore is NOT on the list -- WhoProduces(i_ore_iron) is "
              "empty, so that is a mining trip, not a rendezvous");
        Check(why == nullptr, "a non-empty answer carries no refusal");
    }

    Section("C: a smith that cannot pay does not go");
    {
        // goldReserve 500 + blindPriceCeiling 12 = 512 to buy one at the worst
        // price it would ever accept. 505 is short of that, and the same test
        // ConsiderOffer makes on arrival must be made before the journey.
        std::vector<market::Stock> pack;
        const char* why = nullptr;
        const std::vector<market::Want> want =
            market::PlayerMarketWants(*smith, pack, 505, policy, &why);
        Check(want.empty(), "505 gold against a 500 reserve buys nothing");
        Check(why != nullptr && std::string(why).find("reserve") != std::string::npos,
              "and the refusal names the reserve");

        const std::vector<market::Want> justEnough =
            market::PlayerMarketWants(*smith, pack, 512, policy, nullptr);
        Check(!justEnough.empty(), "512 gold is exactly enough");
    }

    Section("D: a stocked smith does not go either");
    {
        // restockConsumablesTo is 20, so twenty logs is stocked.
        std::vector<market::Stock> pack;
        pack.push_back({"i_log", 20});
        const char* why = nullptr;
        const std::vector<market::Want> want =
            market::PlayerMarketWants(*smith, pack, 1000, policy, &why);
        Check(want.empty(), "twenty logs is a full restock; nothing to buy");
        Check(why != nullptr, "and it says why it is staying put");
        Check(why != nullptr &&
                  std::string(why).find("the world makes") != std::string::npos,
              "the remaining shortfall is ore, which the world makes");
    }
}

// --------------------------------------------------------------------------
// B/E. The need the planner actually sees.
// --------------------------------------------------------------------------
void TestNeedTrade() {
    Section("B: the buyer's shortfall reaches the planner as NeedTrade");

    const prof::Profession* smith = prof::Find("miner_smith");
    if (!smith) { Check(false, "miner_smith"); return; }

    life::NeedConfig cfg;
    cfg.profession = smith;
    const life::BuildPlan plan = life::PlanFromProfession(*smith);
    life::Memory mem;

    {
        life::Observation obs = Standing(1000);
        // Nothing spare, so the SELLER branch cannot be what fires.
        const std::vector<life::Need> needs =
            life::AssessNeeds(plan, mem, obs, cfg);
        const life::Need* t = Find(needs, life::NeedKind::NeedTrade);
        Check(t != nullptr, "a smith with no logs and money raises NeedTrade");
        if (t) {
            Check(t->what == "buy from a player", "and it is the BUY side");
            Check(!t->blocked, "not blocked -- the market has not been tried");
            // 20 short of a 20 restock -> frac 1.0 -> 0.15 + 0.40 = 0.55,
            // which x145 is the 79.8 the seller side scores live.
            Check(t->urgency > 0.54 && t->urgency < 0.56,
                  "urgency is the seller's own 0.55, so weight 145 needs no "
                  "re-tuning");
            Check(!t->evidence.empty(), "and it cites what it is short of");
        }
        Check(CountOf(needs, life::NeedKind::NeedTrade) == 1,
              "exactly one NeedTrade: Planner::Score takes the first of a "
              "kind, so a second would be dead text");
    }

    // marketQuiet gates BOTH sides with the one switch.
    {
        life::Observation obs = Standing(1000);
        obs.marketQuiet = true;
        const std::vector<life::Need> needs =
            life::AssessNeeds(plan, mem, obs, cfg);
        const life::Need* t = Find(needs, life::NeedKind::NeedTrade);
        Check(t != nullptr && t->blocked,
              "a quiet market blocks the buyer exactly as it blocks the seller");
        Check(t != nullptr && t->urgency == 0.0, "and zeroes the urgency");
    }

    // C again, through AssessNeeds: no money, no errand.
    {
        life::Observation obs = Standing(505);
        const std::vector<life::Need> needs =
            life::AssessNeeds(plan, mem, obs, cfg);
        Check(Find(needs, life::NeedKind::NeedTrade) == nullptr,
              "a smith that cannot afford one log raises no trade need at all");
    }

    Section("E: the seller is unchanged");
    {
        const prof::Profession* jack = prof::Find("lumberjack_swordsman");
        Check(jack != nullptr, "the catalogue still has lumberjack_swordsman");
        if (!jack) return;
        life::NeedConfig jcfg;
        jcfg.profession = jack;
        const life::BuildPlan jplan = life::PlanFromProfession(*jack);
        life::Memory jmem;

        // 113 is what Tarath actually banks (bot_data state.json). A
        // lumberjack turns logs into boards, so IsOwnInput() is true for
        // i_log and keepOfOwnOutput (20) comes off the top before anything is
        // spare: 113 - 20 = 93 against a surplusWorthTrip of 20, frac 1.0,
        // urgency 0.15 + 0.40 = 0.55 -- the 79.8 fleet7 logged.
        //
        // (The S5 plan's own worked example used 30 logs and expected 0.55.
        // It does not: 30 - 20 = 10 spare, frac 0.5, urgency 0.35. The
        // working reserve is real and this records it.)
        life::Observation obs = Standing(20000);
        obs.pack.push_back({"i_log", 113});
        const std::vector<life::Need> needs =
            life::AssessNeeds(jplan, jmem, obs, jcfg);
        const life::Need* t = Find(needs, life::NeedKind::NeedTrade);
        Check(t != nullptr, "113 logs and no NPC buyer is still a trade need");
        if (t) {
            Check(t->what == "sell to a player", "and it is still the SELL side");
            Check(t->urgency > 0.54 && t->urgency < 0.56,
                  "at the same 0.55 it scored before this slice");
        }
        Check(CountOf(needs, life::NeedKind::NeedTrade) == 1,
              "the buyer branch does not add a second one behind the seller's");

        // The ramp is a ramp: a smaller load is a weaker reason to walk.
        life::Observation small = Standing(20000);
        small.pack.push_back({"i_log", 30});
        const std::vector<life::Need> smallNeeds =
            life::AssessNeeds(jplan, jmem, small, jcfg);
        const life::Need* ts = Find(smallNeeds, life::NeedKind::NeedTrade);
        Check(ts == nullptr || (ts->urgency > 0.34 && ts->urgency < 0.36),
              "30 logs is 10 spare after the working reserve -- 0.35, not 0.55");
    }
}

// --------------------------------------------------------------------------
// G. Satiation -- the measured fleet7 regression.
// --------------------------------------------------------------------------
void TestCooldown() {
    Section("G: a market trip that achieved nothing must rest");

    const i64 t0 = 5000000;
    const i64 kMarketQuietMs = 10 * 60 * 1000;

    life::Planner planner;
    life::Memory mem;
    life::Observation obs = Standing(1000);
    obs.nowMs = t0;

    std::vector<life::Need> needs;
    {
        life::Need n;
        n.kind = life::NeedKind::NeedTrade;
        n.urgency = 0.55;
        n.what = "buy from a player";
        n.reason = "short of an input only another profession makes";
        needs.push_back(n);
    }

    std::string why;
    Check(planner.Select(needs, obs, mem, t0, &why),
          "NeedTrade at 0.55 x 145 = 79.8 wins over explore(15) and idle(10)");
    Check(planner.Current().kind == life::GoalKind::TradeWithPlayer,
          "and the goal picked is TRADE_WITH_PLAYER");

    // The stand-down: cool the GOAL, not only the need.
    planner.Cooldown(life::GoalKind::TradeWithPlayer, t0 + kMarketQuietMs);

    auto tradeAt = [&](i64 whenMs) {
        life::Observation o = obs;
        o.nowMs = whenMs;
        const std::vector<life::ScoredGoal> scored = planner.Score(needs, o, mem);
        for (const life::ScoredGoal& g : scored) {
            if (g.kind == life::GoalKind::TradeWithPlayer) return g;
        }
        return life::ScoredGoal{};
    };

    // 244 ms is the measured gap in run_m7/fleet7.console.txt: stand-down at
    // 16:24:10.031, re-selected at 16:24:10.275.
    {
        const life::ScoredGoal g = tradeAt(t0 + 244);
        Check(!g.feasible, "244 ms later it is NOT feasible (the fleet7 defect)");
        Check(!g.reasons.empty() &&
                  g.reasons.front().rfind("COOLING", 0) == 0,
              "and the console says COOLING rather than dropping it silently");
    }
    // 50.9 s is the measured end-to-end repeat of the whole cycle.
    Check(!tradeAt(t0 + 50900).feasible, "51 s later it is still resting");
    Check(!tradeAt(t0 + kMarketQuietMs - 1).feasible,
          "and one millisecond before the ten minutes are up");
    Check(tradeAt(t0 + kMarketQuietMs + 1).feasible,
          "ten minutes on, it may try again -- a rest, not a ban");

    // Cooldown is monotonic: a second, shorter one must not cut the rest.
    planner.Cooldown(life::GoalKind::TradeWithPlayer, t0 + 1000);
    Check(!tradeAt(t0 + 50900).feasible,
          "a shorter cooldown does not shorten a longer one already running");
}

// --------------------------------------------------------------------------
// H. The clock has to fit the trip.
// --------------------------------------------------------------------------
void TestGoalClock() {
    Section("H: the goal clock admits a market trip");

    life::Planner planner;

    // 250 s out + 60 s listening + 250 s back = 560 s, all three legs measured
    // (docs/S5_MARKET_TRIP_PLAN.md section 3).
    Check(planner.TimeLimitFor(life::GoalKind::TradeWithPlayer) >= 560000,
          "TRADE_WITH_PLAYER gets more than the 560 s the trip costs");
    Check(planner.TimeLimitFor(life::GoalKind::TradeWithPlayer) <= 15 * 60 * 1000,
          "and it is still bounded -- this is one exception, not an amnesty");
    Check(planner.TimeLimitFor(life::GoalKind::GatherLogs) == 5 * 60 * 1000,
          "every other goal keeps the 5 minute default");

    // The default would have killed the errand mid-journey, every time.
    planner.Mutable().kind = life::GoalKind::TradeWithPlayer;
    planner.Mutable().active = true;
    planner.Mutable().startedAtMs = 0;
    planner.Mutable().attempts = 0;
    Check(!planner.Exhausted(300000, nullptr),
          "at 300 s a market trip is still running");
    Check(planner.Exhausted(14 * 60 * 1000, nullptr),
          "at 14 minutes it is not");

    planner.Mutable().kind = life::GoalKind::GatherLogs;
    Check(planner.Exhausted(300000, nullptr),
          "at 300 s a chopping goal is exhausted, exactly as before");
}

// --------------------------------------------------------------------------
// I. Arrival commitment -- the measured run_r4 regression.
//
// run_r4/pair_Tarath.console.txt:1812, 20:39:34.756:
//   goal_changed=REPLACE_EQUIPMENT from=TRADE_WITH_PLAYER
//   reason="REPLACE_EQUIPMENT 130.0 superseded TRADE_WITH_PLAYER 79.8"
// Fourteen tiles short of britain_bank_2, at the end of a journey he had been
// walking since 20:37, for heal potions he had wanted the whole way. He
// reached the bank at 20:39:43.017 and walked straight back out to the healer,
// while Durnholde stood at that same bank with ingots to sell.
//
// A market trip is paid for on ARRIVAL, so arrival is where the goal becomes
// expensive to abandon. While the character is at the market on this errand,
// only an emergency may take it away.
// --------------------------------------------------------------------------
void TestArrivalCommitment() {
    Section("I: a character standing at the market keeps the errand");

    const i64 t0 = 5000000;
    // Past cfg.minCommitMs (20 s), so the commitment floor is NOT what is
    // being measured here -- the hold has to come from the market rule.
    const i64 t1 = t0 + 60000;

    life::Need trade;
    trade.kind = life::NeedKind::NeedTrade;
    trade.urgency = 0.55;                 // x 145 = 79.8, the measured score
    trade.what = "sell to a player";
    trade.reason = "spare stock only another character will take";

    life::Need potions;
    potions.kind = life::NeedKind::NeedEquipment;
    potions.urgency = 0.50;               // x 260 = 130.0, the measured score
    potions.what = "heal potions";
    potions.reason = "potions=0 low=2";

    life::Need dying;
    dying.kind = life::NeedKind::StayAlive;
    dying.urgency = 1.00;                 // x 1000 = 1000, past preemptScore
    dying.what = "stay alive";
    dying.reason = "under attack";

    auto start = [&](life::Planner& p, life::Memory& mem) {
        life::Observation obs = Standing(1000);
        obs.nowMs = t0;
        std::vector<life::Need> needs{trade};
        std::string why;
        Check(p.Select(needs, obs, mem, t0, &why),
              "the errand starts as TRADE_WITH_PLAYER");
        Check(p.Current().kind == life::GoalKind::TradeWithPlayer,
              "and that is what is running");
    };

    // --- control: away from the market, 130 still takes the goal ----------
    {
        life::Planner p;
        life::Memory mem;
        start(p, mem);

        life::Observation obs = Standing(1000);
        obs.nowMs = t1;
        obs.atMarket = false;             // still walking
        std::vector<life::Need> needs{trade, potions};
        std::string why;
        Check(p.Select(needs, obs, mem, t1, &why),
              "en route, REPLACE_EQUIPMENT 130 beats TRADE 79.8 by more than "
              "the 15% hysteresis and takes the goal -- unchanged behaviour");
        Check(p.Current().kind == life::GoalKind::ReplaceEquipment,
              "and the running goal really is REPLACE_EQUIPMENT");
    }

    // --- at the market: the same 130 may not ------------------------------
    {
        life::Planner p;
        life::Memory mem;
        start(p, mem);

        life::Observation obs = Standing(1000);
        obs.nowMs = t1;
        obs.atMarket = true;              // arrived; the trip is paid for
        std::vector<life::Need> needs{trade, potions};
        std::string why;
        Check(!p.Select(needs, obs, mem, t1, &why),
              "at the market, REPLACE_EQUIPMENT 130 does not take the goal");
        Check(p.Current().kind == life::GoalKind::TradeWithPlayer,
              "the character stays on TRADE_WITH_PLAYER");
        Check(why.find("market") != std::string::npos,
              "and the console says why -- the market, not the hysteresis");

        // --- but an emergency still does ----------------------------------
        std::vector<life::Need> emergency{trade, potions, dying};
        Check(p.Select(emergency, obs, mem, t1, &why),
              "a Survive-level score preempts even at the market");
        Check(p.Current().kind == life::GoalKind::Survive,
              "and the character defends itself");
    }

    // --- the hold is not a lock: a cooled trade goal releases it ----------
    {
        life::Planner p;
        life::Memory mem;
        start(p, mem);

        // This is what every stand-down in DoTradeWithPlayer does.
        p.Cooldown(life::GoalKind::TradeWithPlayer, t1 + 1000);

        life::Observation obs = Standing(1000);
        obs.nowMs = t1;
        obs.atMarket = true;
        std::vector<life::Need> needs{trade, potions};
        std::string why;
        Check(p.Select(needs, obs, mem, t1, &why),
              "once the trade goal is cooled it holds nothing -- the errand "
              "is over and the character may leave");
        Check(p.Current().kind == life::GoalKind::ReplaceEquipment,
              "and it moves on to the next thing it wanted");
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::printf("m8_market_trip\n");
    // The data directory is passed by CTest so the test can run from anywhere.
    const std::string dataDir = (argc > 1) ? argv[1] : "data";

    TestMarketPlace(dataDir);
    TestBuyerWants();
    TestNeedTrade();
    TestCooldown();
    TestGoalClock();
    TestArrivalCommitment();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
