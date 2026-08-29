#include "life/Runner.h"

#include "Client.h"
#include "uo/log.h"
#include "uo/builders.h"
#include "uo/faucets.h"
#include "uo/market.h"
#include "uo/trade.h"
#include "uo/combat.h"
#include "uo/professions.h"
#include "uo/vendor_policy.h"
#include "uo/world_model.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace uo::life {

namespace {

// Item graphics, all read off the runtime's own itemdefs -- never guessed from
// generic UO tables. i_hatchet is [ITEMDEF 0f43] with DUPELIST 0f44 (the
// flipped graphic); i_log is 0x1BDD; i_bandage is [ITEMDEF 0e21].
constexpr u16 kHatchet[]  = {0x0F43, 0x0F44};
constexpr u16 kAxe[]      = {0x0F49, 0x0F4A};
constexpr u16 kLog        = 0x1BDD;
constexpr u16 kBandage    = 0x0E21;
constexpr u16 kKatana[]   = {0x13FE, 0x13FF};
constexpr u16 kFood[]     = {0x103B, 0x09EB, 0x09F2};
constexpr u16 kGoldCoin   = 0x0EED;             // i_gold
// i_spellbook, ITEMDEF 0efa. A spell scroll's graphic is 0x1F2D + the spell
// number: Create Food is spell 2 at 0x1F2F, Heal is 4 at 0x1F31, Magic Arrow
// is 5 at 0x1F32 and Recall is 31 at 0x1F4C -- four independent points, all
// read from this shard's own itemdefs. Circles 1-8 are therefore spells 1-64
// at 0x1F2E..0x1F6D.
// A working book rather than a complete one: circles 7-8 are sold by nobody
// on this shard, so a target of 64 would nag forever. 24 is the first three
// circles, which ARE obtainable by shopping. Mirrors kSpellbookComfortable in
// Needs.cpp -- the need and the goal must agree or the goal finishes a book
// the need still wants filled, and the pair loops.
constexpr int kSpellbookComfortableRuntime = 24;
// Enough to be worth a trip. Prices come from the shop window, never from
// here; these are only "is it worth walking".
constexpr i32 kSpellbookMoney = 120;
constexpr i32 kScrollMoney    = 120;
constexpr i64 kNoSpellbookCooldownMs = 240000;   // four minutes
constexpr i32 kMaxSpellbookTrips = 3;
constexpr u16 kSpellbookGraphic = 0x0EFA;
constexpr u16 kFirstScrollGraphic = 0x1F2E;   // spell 1
constexpr u16 kLastScrollGraphic  = 0x1F6D;   // spell 64
// [SPELL 2] s_create_food -- targetless, 4 mana, MAGERY 10.0 to try.
// The practice spell: nothing to target wrongly, nobody to anger.
constexpr int kSpellCreateFood = 2;
constexpr i32 kCreateFoodMana  = 4;

// The word the NPC expects after "train". Sphere matches on the skill KEY from
// skills/skill<N>_<name>.scp, not on our own label.
const char* SkillKey(int id) {
    switch (id) {
        case rules::kLumberjacking:   return "Lumberjacking";
        case rules::kFishing:         return "Fishing";
        case rules::kCooking:         return "Cooking";
        case rules::kCarpentry:       return "Carpentry";
        case rules::kSwordsmanship:   return "Swordsmanship";
        case rules::kTactics:         return "Tactics";
        case rules::kAnatomy:         return "Anatomy";
        case rules::kHealing:         return "Healing";
        case rules::kMining:          return "Mining";
        case rules::kBlacksmithing:   return "Blacksmithing";
        case rules::kMagery:          return "Magery";
        case rules::kMeditation:      return "Meditation";
        case rules::kAlchemy:         return "Alchemy";
        case rules::kTinkering:       return "Tinkering";
        case rules::kArmsLore:        return "ArmsLore";
        case rules::kEvaluatingIntel: return "EvaluatingIntel";
        case rules::kInscription:     return "Inscription";
        case rules::kTaming:          return "Taming";
        case rules::kAnimalLore:      return "AnimalLore";
        case rules::kVeterinary:      return "Veterinary";
        default:                      return "";
    }
}

// Which trade teaches which skill. A guildmaster teaches to 50.0 where a plain
// tradesman stops at 30.0 (c_human_guildmasters.scp:23), so the guild is
// preferred wherever one exists. Pairs are (paperdoll-title substring, the
// world model's service) so both the mobile scan and the travel layer agree.
struct TrainerFor {
    int         skillId;
    const char* trade;
    wm::Service service;
};
const TrainerFor kTrainers[] = {
    // THREE SKILLS PROFESSIONS ASK FOR AND NOBODY COULD TEACH.
    //
    // Parrying (fencer, macer, archer), Tailoring (full_crafter) and
    // Cartography (treasure_hunter) are all marked viaTrainer in the
    // catalogue and had no row here. TrainerForSkill returned null,
    // trainerTrade_ stayed EMPTY, and the goal asked
    // NearestMobileWithTrade("") -- which matches nothing, ever. Live:
    // goal_failed=TRAIN_AT_NPC reason="no '' reachable after 3 trips",
    // eight times in one six-bot run (run_m7/f6_*). The comment below this
    // table has warned about exactly this failure since the fisher hit it.
    //
    // Trades chosen from who actually HAS the skill on this shard, not from
    // generic UO: c_guild_warrior carries PARRYING={75.0 98.0}, the tailor
    // guildmaster carries TAILORING, and the mapmaker carries
    // CARTOGRAPHY={50.0 75.0} (c_human_guildmasters.scp, c_vendor_human.scp).
    {rules::kParrying,        "armorer",     wm::Service::Blacksmith},
    {rules::kTailoring,       "tailor",      wm::Service::Tailor},
    {rules::kCartography,     "mapmaker",    wm::Service::Mapmaker},
    // "swordsman" WAS A WORD NOBODY ON THIS SHARD WEARS.
    //
    // These two rows were dead for exactly the same reason the three missing
    // rows above were dead -- the goal asked NearestMobileWithTrade() for a
    // title that matches nothing -- and they were harder to spot, because a
    // wrong name looks like a right one in the table. Zero NPC chardefs across
    // runtime/scripts/npcs/*.scp carry "the swordsman"; the count is 0, next to
    // 12 for "the weaponsmith" and 6 for "the armorer".
    //
    // The warrior guildmaster is the right teacher by the owner's rule -- every
    // skill-related guildmaster teaches to 30.0 -- and it is the wrong ANSWER
    // here, because it cannot be reached. There are 7 in the world and no PLACE
    // in the atlas carries a warrior service, so TravelToService has nowhere to
    // send anyone; the nearest routable PLACE to Britain's is 47 tiles away,
    // well past scan range. The craft guildmasters work only because they stand
    // inside the shop their service already points at -- that is how Tarath
    // found Jarman and bought Carpentry. The warrior guild has no shop.
    //
    // Weaponsmiths and armorers do stand in one. Both carry the whole combat
    // set -- SWORDSMANSHIP/FENCING/MACEFIGHTING {15.0 38.0}, PARRYING/TACTICS
    // {45.0 68.0} (c_vendor_human.scp) -- both map to Service::Blacksmith,
    // which has 33 PLACEs, and 68 of them are spawned in the world save.
    // Training caps at the NPC's own roll, so a low-rolled weaponsmith teaches
    // less than a guildmaster would. A trainer that is sometimes weaker beats
    // one that is always unreachable.
    //
    // Routing guildmasters properly is real work and belongs with the atlas
    // generator, which today reads only vendor SPAWNERS and never the world
    // save's WORLDCHARs -- noted in docs/M4_OPEN_LOOSE_ENDS.md rather than
    // bodged here.
    {rules::kSwordsmanship,   "weaponsmith", wm::Service::Blacksmith},
    {rules::kTactics,         "weaponsmith", wm::Service::Blacksmith},
    {rules::kAnatomy,         "healer",      wm::Service::Healer},
    {rules::kHealing,         "healer",      wm::Service::Healer},
    {rules::kBlacksmithing,   "blacksmith",  wm::Service::Blacksmith},
    {rules::kMining,          "blacksmith",  wm::Service::Blacksmith},
    {rules::kTinkering,       "tinker",      wm::Service::Tinker},
    {rules::kArmsLore,        "blacksmith",  wm::Service::Blacksmith},
    {rules::kMagery,          "mage",        wm::Service::Mage},
    {rules::kMeditation,      "mage",        wm::Service::Mage},
    {rules::kEvaluatingIntel, "mage",        wm::Service::Mage},
    {rules::kInscription,     "scribe",      wm::Service::Scribe},
    {rules::kAlchemy,         "alchemist",   wm::Service::Alchemist},
    {rules::kTaming,          "animal",      wm::Service::Stablemaster},
    {rules::kAnimalLore,      "animal",      wm::Service::Stablemaster},
    {rules::kVeterinary,      "animal",      wm::Service::Stablemaster},
    {rules::kLumberjacking,   "carpenter",   wm::Service::Carpenter},
    // The fisher's own two. Without these NextSkillToBuy picks Fishing,
    // TrainerForSkill returns null, trainerTrade_ stays empty, and the goal
    // asks NearestMobileWithTrade("") -- burning every trip and failing.
    {rules::kFishing,         "fisher",      wm::Service::Fisherman},
    {rules::kCooking,         "cook",        wm::Service::Cook},
    {rules::kCarpentry,       "carpenter",   wm::Service::Carpenter},
};

const TrainerFor* TrainerForSkill(int id) {
    for (const TrainerFor& t : kTrainers) {
        if (t.skillId == id) return &t;
    }
    return nullptr;
}

// A buyer trade, as the world model names its destination. The item->trade
// half of this lives in uo::market, because it is shard vendor data that the
// need layer also has to ask about; only the trade->place mapping is here,
// where the world model is in scope.
wm::Service ServiceForTrade(const char* trade) {
    struct Row { const char* trade; wm::Service service; };
    static const Row kRows[] = {
        {"carpenter",   wm::Service::Carpenter},
        {"provisioner", wm::Service::Provisioner},
        {"tinker",      wm::Service::Tinker},
        {"bowyer",      wm::Service::Bowyer},
        {"blacksmith",  wm::Service::Blacksmith},
        {"jeweler",     wm::Service::Jeweler},
        {"tailor",      wm::Service::Tailor},
        {"scribe",      wm::Service::Scribe},
        {"alchemist",   wm::Service::Alchemist},
        {"mage",        wm::Service::Mage},
        {"fisher",      wm::Service::Fisherman},
        {"cook",        wm::Service::Cook},
        {"weaponsmith", wm::Service::Blacksmith},
    };
    for (const Row& r : kRows) {
        if (std::strcmp(r.trade, trade) == 0) return r.service;
    }
    return wm::Service::GeneralVendor;
}

// What a life gathers, as the ATLAS names it. One table, so "where is my
// work" is answered from the world model for every profession instead of
// TreeCount standing in for all of them.
//
// The atlas already files every one of these: atlasgen reads the mine and dock
// AREADEF names and measures foliage density off the client's own statics, so
// twenty docks come through tagged resources=fishing and the mines likewise.
// Nothing needed adding to the data -- only to the code that had been asking
// it one question on behalf of every character.
struct GatherKind {
    const char*      gathers;   // Profession::gathers
    wm::ResourceKind kind;
};
const GatherKind kGatherKinds[] = {
    {"logs", wm::ResourceKind::Lumber},
    {"ore",  wm::ResourceKind::Mining},
    {"fish", wm::ResourceKind::Fishing},
};

wm::ResourceKind ResourceKindFor(const std::string& gathers) {
    for (const GatherKind& g : kGatherKinds) {
        if (gathers == g.gathers) return g.kind;
    }
    return wm::ResourceKind::None;
}

// The two hand layers. Which one an item lands on is decided by THIS SHARD'S
// tiledata, not by generic UO: the newbie katana wears on layer 1 and the
// hatchet wears on layer 2, so a bot that hardcodes "the weapon layer" gets
// `worn on a different layer` from the server and never arms its own axe.
// Equips therefore ask for layer 0 -- "wherever this belongs" -- and the hands
// are READ as a pair.
constexpr u8 kLayerHand1 = 0x01;
constexpr u8 kLayerHand2 = 0x02;
constexpr u8 kLayerServerChooses = 0x00;

i32 CountAny(Client& c, const u16* list, usize n) {
    i32 total = 0;
    for (usize i = 0; i < n; ++i) total += static_cast<i32>(c.BackpackItemCount(list[i]));
    return total;
}

u32 FindAny(Client& c, const u16* list, usize n) {
    for (usize i = 0; i < n; ++i) {
        const u32 s = c.FindBackpackItemByGraphic(list[i]);
        if (s) return s;
    }
    return 0;
}

bool GraphicIsAny(u16 graphic, const u16* list, usize n) {
    for (usize i = 0; i < n; ++i) {
        if (graphic == list[i]) return true;
    }
    return false;
}

// Serial of the axe currently in a hand, or 0. The newbie kit arms a katana
// (from the Swordsmanship request), so "a hand is full" is NOT "the axe is in
// hand" -- and swinging a katana at a tree earns nothing but "The tool is out
// of charges", forever, silently.
u32 AxeSerialInHand(Client& c) {
    for (u8 layer : {kLayerHand1, kLayerHand2}) {
        const u16 g = c.EquippedGraphicAt(layer);
        if (GraphicIsAny(g, kHatchet, 2) || GraphicIsAny(g, kAxe, 2)) {
            return c.EquippedAtLayer(layer);
        }
    }
    return 0;
}

bool AxeInHand(Client& c) { return AxeSerialInHand(c) != 0; }

bool HandsBusy(Client& c) {
    return c.EquippedAtLayer(kLayerHand1) != 0 || c.EquippedAtLayer(kLayerHand2) != 0;
}

i32 TileDist(i32 ax, i32 ay, i32 bx, i32 by) {
    return std::max(ax > bx ? ax - bx : bx - ax, ay > by ? ay - by : by - ay);
}

}  // namespace

Runner::Runner() = default;
Runner::~Runner() = default;

void Runner::LogLine(const char* fmt, ...) const {
    if (!cfg_.verbose) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    // One prefix for the whole autonomous layer, so a run can be read with
    // `findstr [life]` and nothing else. Deliberately NOT a packet trace: the
    // M4 brief asks for choices to be explainable without one.
    LogInfo("[life] %s\n", buf);
}

bool Runner::Configure(const RunnerConfig& cfg, std::string* err) {
    cfg_ = cfg;
    store_ = Store(cfg.dataRoot);

    const std::string id = MakeIdentityId(cfg.accountName, cfg.characterName);

    std::string loadErr;
    if (store_.Load(id, &state_, &loadErr)) {
        LogLine("loaded persistent state for %s (%zu places, %zu suppliers, "
                "%zu events, %zu prior sessions)",
                state_.identity.characterName.c_str(),
                state_.memory.Places().size(), state_.memory.Suppliers().size(),
                state_.memory.Events().size(), state_.sessions.size());
    } else if (!loadErr.empty()) {
        // A corrupt file is a hard stop. Silently starting a fresh life on top
        // of an unreadable one would destroy exactly the history M4 exists to
        // keep, and would look like a successful run.
        if (err) *err = loadErr;
        return false;
    } else {
        state_ = PersistentState{};
        state_.identity.identityId    = id;
        state_.identity.accountName   = cfg.accountName;
        state_.identity.characterName = cfg.characterName;
        const prof::Profession* chosen =
            cfg.professionId.empty() ? nullptr : prof::Find(cfg.professionId.c_str());
        if (!chosen) {
            if (err) {
                *err = "unknown profession '" + cfg.professionId +
                       "' -- see uo::prof::All()";
            }
            return false;
        }
        const prof::ProfCheck pc = prof::Validate(rules::Revolution(), *chosen);
        if (!pc.ok) {
            if (err) {
                *err = "profession '" + chosen->id + "' is not a legal life: " +
                       prof::ProfViolationName(pc.violation);
            }
            return false;
        }
        state_.plan = PlanFromProfession(*chosen);
        LogLine("no prior state for %s: a new %s", id.c_str(),
                chosen->label.c_str());
        LogLine("creation request: %s 50.0 + %s 50.0, stats %d/%d/%d = %d",
                rules::SkillName(chosen->startSkillA), rules::SkillName(chosen->startSkillB),
                chosen->startStr, chosen->startDex, chosen->startInt,
                chosen->startStr + chosen->startDex + chosen->startInt);
    }

    // Which life is asking. Resolved from the plan's family so it works the
    // same for a fresh character and for one reloaded from disk. A plan family
    // with no catalogue entry -- the M4 lumberjack, saved before the catalogue
    // existed -- leaves this null and keeps the old lumberjack needs.
    needCfg_.profession = prof::Find(state_.plan.family.c_str());
    if (!needCfg_.profession) {
        // THE M4 CHARACTER IS NOT A DIFFERENT LIFE, ONLY AN OLDER SPELLING.
        //
        // FrontierLumberjackSwordsman() (Identity.cpp:121) predates the M5
        // catalogue and writes family "frontier_lumberjack_swordsman"; the
        // catalogue registers "lumberjack_swordsman" (Professions.cpp:149).
        // Tarath was created under the old plan, so every reload since has
        // fallen into the branch below and run with a NULL profession -- and
        // a null profession silently disables far more than the comment
        // implies. DoTrainCombat short-circuits on it before WantsToHunt is
        // ever evaluated (Runner.cpp, `!needCfg_.profession`), so the shard's
        // most-run character could never go hunting, and the plan was never
        // rebuilt so it had no viaTrainer flags either. It logged one line
        // about "the original lumberjack needs" and looked fine.
        //
        // Alias the old spelling rather than renaming it: the M4 plan is what
        // that character was actually created with, and rewriting history in
        // Identity.cpp would change what the saved file means.
        if (state_.plan.family == "frontier_lumberjack_swordsman") {
            needCfg_.profession = prof::Find("lumberjack_swordsman");
            if (needCfg_.profession) {
                LogLine("needs: plan family '%s' is the M4 spelling of '%s' "
                        "-- reading the catalogue entry",
                        state_.plan.family.c_str(), needCfg_.profession->id.c_str());
            }
        }
    }
    if (needCfg_.profession) {
        // THE CATALOGUE IS THE INTENTION; the save file only records which
        // life this is. Re-deriving here is not tidiness -- the saved plan
        // carries skills and targets but not the per-target `viaTrainer` and
        // `priority` fields, so a reloaded character silently had nothing it
        // was willing to buy from a trainer, and simply never trained. It
        // logged no error: NextSkillToBuy just returned -1 forever.
        //
        // Anything the character has actually EARNED lives on the server or
        // in Memory, never in the plan, so nothing is lost by rebuilding it.
        state_.plan = PlanFromProfession(*needCfg_.profession);
        LogLine("needs: reading '%s' from the profession catalogue",
                needCfg_.profession->id.c_str());
    } else {
        LogLine("needs: plan family '%s' is not in the catalogue -- using the "
                "original lumberjack needs", state_.plan.family.c_str());
    }

    // Pick a home, once. Deterministic from the identity id rather than random,
    // so the same character always gets the same home even if the state file is
    // lost -- and so a fleet spreads across the map instead of every member
    // rolling the same first entry.
    if (state_.homeCity.empty() && needCfg_.profession &&
        !needCfg_.profession->homeCities.empty()) {
        usize h = 0;
        for (char c : state_.identity.identityId) {
            h = h * 131 + static_cast<unsigned char>(c);
        }
        const std::vector<std::string>& homes = needCfg_.profession->homeCities;
        state_.homeCity = homes[h % homes.size()];
        LogLine("home: %s lives in %s", state_.identity.characterName.c_str(),
                state_.homeCity.c_str());
    }

    // Whatever the source -- a fresh plan or one reloaded from disk -- it has
    // to be a legal Revolution build before the character acts on it.
    const PlanCheck check = ValidatePlan(rules::Revolution(), state_.plan);
    if (!check.ok) {
        if (err) {
            char buf[192];
            std::snprintf(buf, sizeof(buf),
                          "build plan is not a legal Revolution build: %s (skill %d)",
                          PlanViolationName(check.violation), check.skillId);
            *err = buf;
        }
        return false;
    }
    LogLine("build plan '%s': %.1f resolved + %.1f unresolved = %.1f/%.1f, "
            "stats %d/%d/%d = %d",
            state_.plan.family.c_str(), check.resolvedTenths / 10.0,
            state_.plan.unresolvedTenths / 10.0, check.plannedTotalTenths / 10.0,
            rules::Revolution().totalSkillCapTenths / 10.0,
            state_.plan.targetStr, state_.plan.targetDex, state_.plan.targetInt,
            check.statTotal);

    // The goal that came back from disk is an INTENTION. Its clock and
    // counters are transient and are rebuilt at reconciliation.
    planner_.Mutable() = state_.goal;
    configured_ = true;
    return true;
}

// ---------------------------------------------------------------------------
// Observation -- source of truth C, rebuilt from scratch every tick.
// ---------------------------------------------------------------------------

Observation Runner::Observe(Client& client, i64 nowMs) const {
    Observation obs;
    obs.nowMs = nowMs;
    obs.inWorld = client.IsInWorld();
    if (!obs.inWorld) return obs;

    obs.dead    = client.IsDead();
    obs.mounted = client.PlayerIsMounted();
    obs.warMode = client.WarModeOn();

    obs.x = client.PlayerX();
    obs.y = client.PlayerY();
    obs.z = client.PlayerZ();

    obs.hp    = client.PlayerHp();
    obs.hpMax = client.PlayerHpMax();
    obs.mana  = client.PlayerMana();

    obs.str   = client.PlayerStr();
    obs.dex   = client.PlayerDex();
    obs.intel = client.PlayerInt();

    for (const SkillTarget& t : state_.plan.skills) {
        const i32 base = client.PlayerSkillBase(static_cast<u16>(t.skillId));
        if (base >= 0) obs.skills.push_back({t.skillId, base});
    }

    obs.gold      = client.PlayerGold();
    obs.weight    = client.PlayerWeight();
    obs.maxWeight = client.PlayerMaxWeight();
    if (obs.maxWeight <= 0) {
        // The server only sends maxWeight when its status packet is flagged >= 5
        // (Client.cpp OnStats), and this shard sends less -- so it is ALWAYS 0
        // here and WeightFraction() was permanently 0. The character therefore
        // never noticed it was full, never banked, and simply overflowed:
        // "You put the logs at your feet. It is too heavy.."
        //
        // Derive it from the engine's own formula instead of guessing.
        // CResourceCalc.cpp:24 -- 40 + STR * 3.5, plus a flat +60 for humans
        // with the Strong Back racial. Tenths of stones there; PlayerWeight()
        // is in whole stones, so this is the stone figure.
        obs.maxWeight = 40 + (obs.str * 35) / 10;
    }
    // The server's own words are the definitive signal, and they arrive whether
    // or not it ever told us a capacity.
    obs.overloaded = client.JournalSaidSince("it is too heavy", overloadWatchMs_);

    obs.bandages = static_cast<i32>(client.BackpackItemCount(kBandage));
    obs.logs     = static_cast<i32>(client.BackpackItemCount(kLog));
    obs.food     = CountAny(client, kFood, sizeof(kFood) / sizeof(kFood[0]));
    // HUNGER AS THE SERVER SAYS IT. "You are <level>" over the eight levels
    // in core/messages.scp:470-477. A player reads this line; so do we.
    // Watermarked from session start rather than a rolling mark: hunger is
    // a STATE, and the last thing it said is still true until it says
    // otherwise.
    obs.starving = client.JournalSaidSince("you are starving", sessionStartJournalMs_);
    obs.hungry   = obs.starving ||
                   client.JournalSaidSince("you are very hungry", sessionStartJournalMs_) ||
                   client.JournalSaidSince("you are hungry", sessionStartJournalMs_);

    obs.axeInPack = FindAny(client, kHatchet, 2) != 0 || FindAny(client, kAxe, 2) != 0;
    obs.weaponEquipped = HandsBusy(client);
    // Read the worn graphic rather than inferring from a full hand. The first
    // live run swung the newbie katana at a tree for two minutes because a
    // filled weapon hand was taken to mean "the axe is out".
    obs.axeEquipped = AxeInHand(client);

    std::vector<Client::HostileHit> hostiles;
    client.ScanHostiles(12, hostiles);
    obs.marketQuiet = obs.nowMs < marketQuietUntilMs_;
    obs.hostilesNear = static_cast<i32>(hostiles.size());
    const u32 warTarget = client.WarWatchdog().TargetSerial();
    i32 adjacent = 0;
    for (const Client::HostileHit& h : hostiles) {
        if (TileDist(h.x, h.y, obs.x, obs.y) <= 1) ++adjacent;
    }
    // A FIGHT, not a crowd. Adjacency alone is not being attacked: Session B
    // opened combat with a COW that happened to be standing next to it, and
    // then spent twenty seconds failing to dent it. The war watchdog's target
    // is the client's own record that a fight is actually happening, so that
    // is the gate; adjacent hostiles only scale the pressure once it is.
    obs.underAttack = warTarget != 0;
    obs.attackersOnMe = obs.underAttack ? std::max(1, adjacent) : 0;

    const travel::DeathRecord& death = client.Knowledge().LastDeath();
    obs.corpseKnown = death.valid && death.corpseSerial != 0;
    obs.corpseX = death.x;
    obs.corpseY = death.y;
    obs.corpseRecoveryAttempts = death.recoveryAttempts;

    // Arrival is a claim about the TILE. `TreeCount` asks the shard's own
    // statics whether there is anything here to chop, which travel success
    // does not answer (see docs/UOOFFLINE_BEHAVIOR_AUDIT.md section 3.6).
    // AM I WHERE MY WORK IS -- for THIS life. TreeCount answered on behalf of
    // every profession, so a miner at its vein, a mage in its tower and a
    // fisher on a dock were all told they were "not at work" and sent hiking
    // toward the nearest forest looking for logs they had no use for.
    const std::string gathers =
        needCfg_.profession ? needCfg_.profession->gathers : std::string("logs");
    if (gathers == "fish") {
        Client::WaterHit w;
        obs.atWorkSite = client.NearestWater(obs.x, obs.y, 4, &w);
    } else if (gathers.empty()) {
        // A life that gathers nothing is never away from its work: a mage or
        // an alchemist works wherever it happens to be standing.
        obs.atWorkSite = true;
    } else {
        obs.atWorkSite = client.TreeCount(obs.x, obs.y, cfg_.searchRadius) > 0;
    }
    // NO SKILL ADVANCES INSIDE A REGION_FLAG_SAFE AREA (Source-X
    // Skill_Experience; docs/REVOLUTION_GAMEPLAY_TRUTH.md 3.2 point 1).
    // Twenty-five of them on map 0, and the ones that matter are the
    // SHRINES -- quiet, safe-looking, and exactly where a bot would
    // otherwise choose to stand and meditate for an hour to no effect.
    {
        const wm::Region* here = client.CurrentRegion();
        obs.inNoGainRegion = here && here->flags.safe;
    }
    obs.treeAdjacent = client.TreeCount(obs.x, obs.y, 2) > 0;
    obs.atBank = client.BankContainer() != 0 &&
                 client.ContainerKnown(client.BankContainer());

    // READ THE BOX while it is open, and KEEP what it said.
    //
    // A character that does not remember its own bank cannot sell what it
    // banked -- it has no reason to walk to a box it does not know holds
    // anything, so everything it ever gathered leaves the economy for good.
    // This is not omniscience: it is remembering your own container, which is
    // the most ordinary thing a player does.
    obs.bankOpen = obs.atBank;
    if (obs.atBank) {
        const u32 box = client.BankContainer();
        std::vector<market::Stock> fresh;
        const usize n = client.ContainerItemCount(box);
        for (usize i = 0; i < n; ++i) {
            u32 serial = 0; u16 gfx = 0, amount = 0;
            if (!client.ContainerItemAt(box, i, &serial, &gfx, &amount)) continue;
            const char* name = econ::ItemNameForGraphic(gfx);
            if (!name) continue;          // nothing we have a name for
            if (amount == 0) amount = 1;
            bool merged = false;
            for (market::Stock& k : fresh) {
                if (k.item == name) { k.qty += amount; merged = true; break; }
            }
            if (!merged) fresh.push_back({name, static_cast<i32>(amount)});
        }
        obs.bank = std::move(fresh);
    } else {
        // Away from the box: what the character REMEMBERS is what it has.
        obs.bank = state_.bank;
    }

    // The pack, as the M7 economy layer wants it: quantities keyed by itemdef
    // defname. Built from what THIS life produces and consumes, so the loop is
    // over a handful of names rather than every item on the shard. One name can
    // have several graphics -- iron ingots are 0x1BEF/0x1BF0/0x1BF1 by stack
    // size -- so a caller that checked only the first would miss most of a pack.
    if (needCfg_.profession) {
        auto countInto = [&](const std::string& item) {
            for (const market::Stock& have : obs.pack) {
                if (have.item == item) return;   // already counted
            }
            i32 n = 0;
            for (u16 g : econ::GraphicsForItem(item.c_str())) {
                n += static_cast<i32>(client.BackpackItemCount(g));
            }
            obs.pack.push_back({item, n});
        };
        for (const std::string& it : needCfg_.profession->produces) countInto(it);
        for (const std::string& it : needCfg_.profession->consumes) countInto(it);
    }

    // Which of this plan's trainable skills have already been refused. Read
    // from memory, so one wasted walk teaches the character for good -- and
    // so the answer survives a logout.
    for (usize i = 0; i < state_.plan.skills.size(); ++i) {
        if (i >= state_.plan.viaTrainer.size() || !state_.plan.viaTrainer[i]) continue;
        const int id = state_.plan.skills[i].skillId;
        const TrainerFor* tf = TrainerForSkill(id);
        if (tf && state_.memory.TrainerRefused(id, tf->trade)) {
            obs.trainerRefusedSkills.push_back(id);
        }
    }

    // Which of this life's own tools it is actually holding. Checked in the
    // pack AND in both hands: several Sphere gathering skills read SRC.WEAPON,
    // so "carried" and "wielded" are different questions.
    if (needCfg_.profession) {
        for (const prof::ToolNeed& t : needCfg_.profession->tools) {
            for (u16 g : t.graphics) {
                if (client.FindBackpackItemByGraphic(g) ||
                    client.EquippedGraphicAt(kLayerHand1) == g ||
                    client.EquippedGraphicAt(kLayerHand2) == g) {
                    obs.toolsHeld.push_back(t.name);
                    break;
                }
            }
        }
    }

    // What this life wants to BUY next. A generic tradesman teaches to 30.0
    // (sphere.ini NPCTrainPercent=30 of a GM's 100.0); a guildmaster overrides
    // to 50.0. The ceiling passed here is the lower one, so the character
    // never pays for a skill it has already grown past a plain trainer.
    // Which skill this life should PRACTISE -- do the thing that raises it.
    // Distinct from wantTrainSkill, which is a skill to BUY from a guildmaster
    // and stops at 30.0. Practice is how a skill reaches 100.
    obs.wantPracticeSkill = -1;
    for (const SkillTarget& t : state_.plan.skills) {
        if (obs.SkillTenths(t.skillId) >= t.tenths) continue;
        if (t.skillId != rules::kMeditation &&
            t.skillId != rules::kMagery) continue;   // see DoPracticeSkill
        obs.wantPracticeSkill = t.skillId;
        break;
    }

    // THE BOOK, AND WHAT IS IN IT.
    //
    // i_spellbook is ITEMDEF 0efa on this shard. The count is what the client
    // has been told is inside it, which is only populated after the book has
    // been opened once -- so 0 here means "no book, or a book we have not
    // looked in yet", and the goal opens it rather than assuming it is empty.
    obs.spellbookSerial = client.FindBackpackItemByGraphic(kSpellbookGraphic);
    obs.spellsKnown =
        obs.spellbookSerial
            ? static_cast<int>(client.ContainerItemCount(obs.spellbookSerial))
            : 0;

    obs.wantTrainSkill = NextSkillToBuy(state_.plan, obs, 300);
    if (obs.wantTrainSkill >= 0) {
        for (const SkillTarget& t : state_.plan.skills) {
            if (t.skillId == obs.wantTrainSkill) { obs.wantTrainTarget = t.tenths; break; }
        }
    }

    return obs;
}

// ---------------------------------------------------------------------------
// Learning. Only from things actually observed.
// ---------------------------------------------------------------------------

// Take the axe in hand, unequipping whatever is there first.
//
// A hand already holding something cannot take a second weapon: Sphere answers
// the lift-and-wear with "You put the hatchet in your pack" and the character
// keeps swinging the newbie katana at trees. So this is two actions, and the
// first live run is the reason it is not one.
//
// Returns true while the arming is still in progress (the caller should wait),
// false when the axe is in hand or there is no axe to arm.
bool Runner::ArmAxe(Client& client, const Observation& obs) {
    if (AxeInHand(client)) return false;
    if (client.ActionBusy()) return true;

    const u32 hatchet = FindAny(client, kHatchet, 2);
    const u32 axe = hatchet ? hatchet : FindAny(client, kAxe, 2);
    if (!axe) return false;   // nothing to arm; the caller decides what that means

    // Clear BOTH hands. Which one the axe wants is the shard's decision, so
    // the only reliable way to make room is to empty the pair.
    for (u8 layer : {kLayerHand1, kLayerHand2}) {
        const u32 worn = client.EquippedAtLayer(layer);
        if (!worn) continue;
        LogLine("arming the axe: taking off 0x%04X from layer %u first",
                client.EquippedGraphicAt(layer), static_cast<unsigned>(layer));
        client.ActionUnequip(worn);
        nextActionMs_ = obs.nowMs + 1400;
        return true;
    }

    // Layer 0 = "wherever this belongs". Naming a layer here is how the first
    // live run earned `worn on a different layer` and never armed its axe.
    LogLine("arming the axe");
    client.ActionEquip(axe, kLayerServerChooses);
    nextActionMs_ = obs.nowMs + 1600;
    return true;
}

// ---------------------------------------------------------------------------
// Holding the build to its caps.
//
// A build plan that is never enforced is a wish. Revolution's caps are 700.0
// skill and 225 stat, and both are reached by ACCUMULATION -- so a character
// that never locks anything eventually spends its budget on whatever it
// happened to use most, not on what it planned.
//
// The policy is deliberately narrow, because the plan is:
//   * a PLANNED skill at or past its target  -> LOCK  (stop spending budget)
//   * a PLANNED skill below its target        -> UP    (keep training it)
//   * an UNPLANNED skill                      -> left alone. The 200 unresolved
//     points are unspent ON PURPOSE, and setting them DOWN would quietly
//     decide the rest of the build.
//   * a stat at or past its target            -> LOCK
//   * a stat below its target                 -> UP
//
// Nothing here raises a skill or a stat. It moves the arrow beside it, which
// is what a player does, and the server remains free to refuse.
void Runner::MaintainBuildLocks(Client& client, const Observation& obs) {
    if (obs.nowMs < nextLockCheckMs_) return;
    nextLockCheckMs_ = obs.nowMs + 30000;

    const rules::Profile& p = rules::Revolution();

    // --- DO NOT LOCK EARLY. -----------------------------------------------
    //
    // A lock is an END-OF-BUILD instrument. Its only job is to stop a finished
    // part of the build from eating budget the unfinished parts still need,
    // and that budget is only scarce near the cap. A young character has
    // hundreds of points of headroom, so locking anything then just freezes
    // growth for no gain -- and a locked stat cannot fall either, which is
    // exactly the redistribution a build near the cap depends on.
    //
    // So: nothing is locked until the character is actually approaching the
    // cap. Below the gate, everything trains up and no lock packet is sent at
    // all. The gates are the last 100 skill points and the last 25 stat
    // points -- the region where the caps start to bind.
    const i32 skillSum = obs.SkillSumTenths();
    const i32 statSum  = obs.str + obs.dex + obs.intel;
    const bool skillsNearCap = skillSum >= p.totalSkillCapTenths - 1000;   // 600.0 of 700.0
    const bool statsNearCap  = statSum  >= p.totalStatCap - 25;            // 200 of 225

    if (!lockGateLogged_ && (skillsNearCap || statsNearCap)) {
        lockGateLogged_ = true;
        LogLine("build: approaching the caps (skills %.1f/%.1f, stats %d/%d) -- "
                "lock management starts now",
                skillSum / 10.0, p.totalSkillCapTenths / 10.0, statSum,
                p.totalStatCap);
    }

    if (skillsNearCap) {
        for (const SkillTarget& t : state_.plan.skills) {
            const i32 have = client.PlayerSkillBase(static_cast<u16>(t.skillId));
            if (have < 0) continue;   // the server has not told us yet
            const u8 want = (have >= t.tenths) ? build::kLockLocked : build::kLockUp;
            const i32 now = client.PlayerSkillLock(static_cast<u16>(t.skillId));
            if (now == static_cast<i32>(want)) continue;
            LogLine("build: %s at %.1f/%.1f -> %s", rules::SkillName(t.skillId),
                    have / 10.0, t.tenths / 10.0,
                    want == build::kLockLocked ? "LOCK" : "train up");
            client.ActionSetSkillLock(static_cast<u16>(t.skillId), want);
        }
    }

    if (statsNearCap) {
        // The client is never told a stat's lock state, so each transition is
        // sent once rather than reconciled against the server.
        const struct { u8 code; i32 have; i32 target; const char* name; } kStats[3] = {
            {0, obs.str,   state_.plan.targetStr, "STR"},
            {1, obs.dex,   state_.plan.targetDex, "DEX"},
            {2, obs.intel, state_.plan.targetInt, "INT"},
        };
        for (const auto& s : kStats) {
            const u8 want = (s.have >= s.target) ? build::kLockLocked : build::kLockUp;
            if (statLockSent_[s.code] == want + 1) continue;
            statLockSent_[s.code] = static_cast<u8>(want + 1);
            LogLine("build: %s at %d/%d -> %s", s.name, s.have, s.target,
                    want == build::kLockLocked ? "LOCK" : "train up");
            client.ActionSetStatLock(s.code, want);
        }
    }
}

// ---------------------------------------------------------------------------
// A SMALL HINT, and no more.
//
// The M4 brief's Phase 15 rule: "Seed only what the character would reasonably
// know at creation ... Everything else should be learned."
//
// A player who rolls a lumberjack knows that Yew has woods. They do NOT know
// which tree still holds wood, nor where the good stands are -- that is earned
// by swinging an axe. So this seeds the few NAMED forests nearest home, marked
// as hints, and nothing else. No global map, no yields, no confidence.
//
// Runs once per life: after the first session the character has its own
// experience and the hints only matter as fallback leads.
void Runner::SeedCommonKnowledge(Client& client, i64 nowMs) {
    if (state_.memory.HasEvent("common_knowledge_seeded")) return;
    if (!client.WorldKnowledgeReady()) return;

    // SEED WHAT THIS LIFE GATHERS. Every character was seeded with forest
    // hints regardless of profession -- a miner learned three woods it would
    // never chop and no mine at all, and a fisher no dock.
    const std::string gathers =
        needCfg_.profession ? needCfg_.profession->gathers : std::string("logs");
    if (gathers.empty()) return;      // a mage gathers nothing; seed nothing
    const wm::ResourceKind kind = ResourceKindFor(gathers);
    if (kind == wm::ResourceKind::None) return;

    std::vector<const wm::Place*> forests;
    client.ResourcePlacesNear(kind, client.PlayerX(),
                              client.PlayerY(), forests);
    if (forests.empty()) return;

    int seeded = 0;
    for (const wm::Place* p : forests) {
        if (seeded >= kSeedHints) break;
        state_.memory.HintResource(gathers.c_str(), p->name.c_str(),
                                   p->position.x,
                                   p->position.y, p->position.z, nowMs);
        LogLine("common knowledge: %s at %d,%d (%d tiles away)", p->name.c_str(),
                p->position.x, p->position.y,
                TileDist(p->position.x, p->position.y, client.PlayerX(),
                         client.PlayerY()));
        ++seeded;
    }
    state_.memory.NoteEvent("common_knowledge_seeded", gathers.c_str(),
                            "", client.PlayerX(), client.PlayerY(), nowMs);
    LogLine("seeded %d %s hint(s) of %zu the atlas knows -- everything else "
            "is earned", seeded, gathers.c_str(), forests.size());
}

void Runner::LearnFromObservation(Client& client, const Observation& obs) {
    // Keep what the open box said. Observe() is const by design -- it is the
    // ephemeral half of the truth split -- so the remembering happens here,
    // which is the function whose whole job is "what did I learn".
    if (obs.bankOpen) {
        state_.bank = obs.bank;
        state_.bankSeenMs = obs.nowMs;
    }
    // NOTHING is written here. Standing where trees are visible is not
    // knowledge worth keeping: doing so gave the character 64 imaginary
    // "stands" after one session, which it then preferred over asking the
    // atlas, and it spent four sessions working scrub 210 tiles short of the
    // real Yew woods. A stand is recorded only where a chop YIELDED (see
    // DoGatherLogs), and leads come from HintResource.
    if (obs.atBank) {
        bankTrips_ = 0;
        // Pack emptied: stop reacting to the overflow message that got us here.
        // (The `+ 1` that used to be here is now inside JournalNowMs, which
        // returns an exclusive mark for every caller rather than only this one.)
        overloadWatchMs_ = client.JournalNowMs();
        // (The place itself was recorded when the box was opened, from the
        // banker's own position -- see above.)
        if (!state_.memory.HasEvent("bank_learned")) {
            state_.memory.NoteEvent("bank_learned", "opened a bank box", "bank",
                                    obs.x, obs.y, obs.nowMs);
            LogLine("memory_learned=PLACE kind=bank at %d,%d", obs.x, obs.y);
        }
    }

    // A healer we have actually seen. The world model records the sighting;
    // this copies it into the character's own memory so it survives logout.
    const travel::ServiceSighting* healer =
        client.Knowledge().RecentService(wm::Service::Healer, obs.nowMs,
                                         60 * 60 * 1000);
    if (healer) {
        state_.memory.NotePlace("healer", healer->title.c_str(), healer->x,
                                healer->y, healer->z, obs.nowMs);
    }

    state_.memory.ExpireDanger(obs.nowMs);
}

// ---------------------------------------------------------------------------
// Unreachable-foe memory (audit section 3.7). Lives on the RUNNER, not on the
// goal, so abandoning a fight and coming back does not forget it.
// ---------------------------------------------------------------------------

// A destination we already travelled to this session and found treeless. The
// memory failure count reorders things eventually, but only a hard skip stops
// the same no-op trip repeating within one session.
bool Runner::IsDeadTarget(i32 x, i32 y) const {
    for (const auto& d : deadTargets_) {
        if (TileDist(d.first, d.second, x, y) <= 8) return true;
    }
    return false;
}

bool Runner::IsUnreachable(u32 serial, i64 nowMs) const {
    for (const auto& e : unreachable_) {
        if (e.first == serial && nowMs < e.second) return true;
    }
    return false;
}

void Runner::MarkUnreachable(u32 serial, i64 nowMs) {
    const i64 until = nowMs + 30 * 1000;
    for (auto& e : unreachable_) {
        if (e.first == serial) { e.second = until; return; }
    }
    unreachable_.emplace_back(serial, until);
    if (unreachable_.size() > 32) unreachable_.erase(unreachable_.begin());
}

// ---------------------------------------------------------------------------
// Checkpoint
// ---------------------------------------------------------------------------

bool Runner::Checkpoint(Client& client, i64 nowMs, const char* why) {
    if (!configured_) return false;
    state_.checkpointMs = nowMs;
    state_.goal = planner_.Current();
    if (client.IsInWorld()) {
        state_.lastKnownGold = client.PlayerGold();
        state_.lastKnownStr  = client.PlayerStr();
        state_.lastKnownDex  = client.PlayerDex();
        state_.lastKnownInt  = client.PlayerInt();
        state_.lastKnownX    = client.PlayerX();
        state_.lastKnownY    = client.PlayerY();
        state_.lastKnownDead = client.IsDead();
        state_.lastKnownSkills.clear();
        for (const SkillTarget& t : state_.plan.skills) {
            const i32 base = client.PlayerSkillBase(static_cast<u16>(t.skillId));
            if (base >= 0) state_.lastKnownSkills.push_back({t.skillId, base});
        }
    }
    std::string err;
    if (!store_.Save(state_, &err)) {
        LogError("[life] checkpoint failed (%s): %s\n", why, err.c_str());
        return false;
    }
    lastCheckpointMs_ = nowMs;
    LogLine("checkpoint (%s) -> %s", why,
            store_.PathFor(state_.identity.identityId).c_str());
    return true;
}

void Runner::EndSession(const char* why) {
    if (phase_ == Phase::WindDown || phase_ == Phase::LoggingOut ||
        phase_ == Phase::Done) {
        return;
    }
    LogLine("session_end_requested reason=\"%s\"", why ? why : "");
    phase_ = Phase::WindDown;
    windDownStartedMs_ = lastTickMs_;
}

// ---------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------

void Runner::Tick(Client& client, i64 nowMs) {
    if (!configured_ || finished_) return;
    lastTickMs_ = nowMs;

    switch (phase_) {
        case Phase::AwaitWorld: {
            if (!client.IsInWorld()) return;
            if (sessionStartMs_ == 0) {
                sessionStartMs_ = nowMs;
                sessionStartJournalMs_ = client.JournalNowMs();
                // The server has to tell us the skills before anything can be
                // reconciled against them.
                client.ActionRequestSkills();
                LogLine("in world; asked the server for the skill list");
                nextActionMs_ = nowMs + 2500;
                return;
            }
            if (nowMs < nextActionMs_) return;
            phase_ = Phase::Reconcile;
            return;
        }

        case Phase::Reconcile: {
            const Observation obs = Observe(client, nowMs);
            SeedCommonKnowledge(client, nowMs);
            const ReconcileReport rep = Reconcile(&state_, obs);

            LogLine("reconciliation: %s, %d field(s) differed",
                    rep.firstEverLogin ? "first ever login" : "resuming a life",
                    rep.driftFields);
            for (const ReconcileLine& l : rep.lines) {
                LogLine("  %-12s persisted=%-10s server=%-10s -> %s",
                        l.field.c_str(), l.persisted.c_str(), l.server.c_str(),
                        l.result.c_str());
            }
            if (rep.goalDropped) {
                LogLine("  restored objective DROPPED: %s", rep.goalDropReason.c_str());
            } else if (state_.goal.active) {
                LogLine("  restored objective KEPT: %s (progress %d)",
                        GoalKindName(state_.goal.kind), state_.goal.progress);
            }
            planner_.Mutable() = state_.goal;

            // Transient observations from the previous life are gone by
            // construction -- they were never persisted -- but the runtime
            // caches that DO exist in this process are cleared explicitly so
            // nothing carries over between sessions in one host.
            unreachable_.clear();
            chopTargetValid_ = false;
            chopCursorPending_ = false;
            travelInFlight_ = false;

            session_ = SessionSummary{};
            session_.startedMs = nowMs;
            session_.goldStart = obs.gold;
            session_.skillTenthsStart = obs.SkillSumTenths();
            logsAtSessionStart_ = obs.logs;

            state_.identity.sessions++;

            // Survival on, and left on. M3.9.1 proved this path live: a
            // character disengaged at ~32%, bandaged, and survived.
            client.SetSurvivalEnabled(true);
            client.EnsurePeaceMode();

            Checkpoint(client, nowMs, "login reconciled");
            phase_ = Phase::Live;
            return;
        }

        case Phase::Live: {
            if (!client.IsInWorld()) return;
            const Observation obs = Observe(client, nowMs);
            LearnFromObservation(client, obs);
            MaintainBuildLocks(client, obs);

            // --- session limits -------------------------------------------
            const i64 elapsed = nowMs - sessionStartMs_;
            if (cfg_.sessionLimitMs > 0 && elapsed >= cfg_.sessionLimitMs) {
                // Deferral rules from the audit (section 3.13): never end a
                // session on top of a corpse run, and never while dead.
                if (!obs.dead && planner_.Current().kind != GoalKind::RecoverCorpse) {
                    EndSession("session time limit reached");
                    return;
                }
            }
            if (cfg_.goalLimit > 0 && session_.goalsCompleted >= cfg_.goalLimit &&
                !obs.dead) {
                EndSession("goal limit reached");
                return;
            }

            // --- decide ----------------------------------------------------
            const std::vector<Need> needs =
                AssessNeeds(state_.plan, state_.memory, obs, needCfg_);
            std::string why;
            const GoalKind previous = planner_.Current().kind;
            const bool wasActive = planner_.Current().active;
            if (planner_.Select(needs, obs, state_.memory, nowMs, &why)) {
                session_.goalsAttempted++;
                {
                    const int gi = static_cast<int>(planner_.Current().kind);
                    if (gi >= 0 && gi < static_cast<int>(GoalKind::Count))
                        session_.goalPicks[gi]++;
                }
                if (wasActive) {
                    LogLine("goal_changed=%s from=%s reason=\"%s\"",
                            GoalKindName(planner_.Current().kind),
                            GoalKindName(previous), why.c_str());
                } else {
                    LogLine("goal=%s reason=\"%s\"",
                            GoalKindName(planner_.Current().kind), why.c_str());
                }
                LogGoalChange(obs, why);
                // A new goal starts from a clean transient slate.
                chopTargetValid_ = false;
                chopCursorPending_ = false;
                travelInFlight_ = false;
                travelAttempts_ = 0;
                logsAtGoalStart_ = obs.logs;
            }

            // --- act -------------------------------------------------------
            // Point the trainer machinery at whatever the plan wants next.
            // Doing it here, from data, is what keeps `if (miner) ...` out of
            // the goal bodies.
            if (obs.wantTrainSkill >= 0) {
                if (const TrainerFor* tf = TrainerForSkill(obs.wantTrainSkill)) {
                    if (trainerTrade_ != tf->trade) {
                        trainerTrade_ = tf->trade;
                        trainerService_ = tf->service;
                        trainTrips_ = 0;
                        trainAsked_ = false;
                    }
                }
            }

            RunGoal(client, obs);

            // --- checkpoint ------------------------------------------------
            if (cfg_.checkpointIntervalMs > 0 &&
                nowMs - lastCheckpointMs_ >= cfg_.checkpointIntervalMs) {
                Checkpoint(client, nowMs, "periodic");
            }
            return;
        }

        case Phase::WindDown: {
            if (!client.IsInWorld()) { phase_ = Phase::Done; finished_ = true; return; }
            // Logging out somewhere hostile is how this project lost three
            // characters -- Source-X does not drop a combat-flagged connection
            // immediately, and one died in the gap AFTER logout_ack. So the
            // wind-down walks to a known bank first and only then logs out.
            client.EnsurePeaceMode();

            // The deadline is checked BEFORE the travel guard, and it ABORTS
            // the trip. Session B put this the other way round and a trip that
            // never arrived held the wind-down open for fourteen minutes --
            // the timeout was unreachable while travel was busy, which is not
            // a bound at all.
            const bool outOfTime = nowMs - windDownStartedMs_ > 2 * 60 * 1000;
            if (outOfTime && client.TravelBusy()) {
                LogLine("wind-down: the trip has run past its deadline; abandoning "
                        "it and logging out where I stand");
                client.TravelAbort("wind-down deadline");
                travelInFlight_ = false;
                return;
            }
            if (client.TravelBusy()) return;

            const KnownPlace* bank = state_.memory.BestPlace("bank");
            const bool safeHere = client.BankContainer() != 0 ||
                                  windDownArrived_ ||
                                  (bank && TileDist(bank->x, bank->y, client.PlayerX(),
                                                    client.PlayerY()) <= 6);

            if (travelInFlight_) {
                travelInFlight_ = false;
                if (client.TravelSucceeded()) {
                    // Arriving IS the safe state. Requiring an open bank box
                    // here made the first live run re-plan the same
                    // already-arrived trip every tick for four minutes.
                    windDownArrived_ = true;
                    state_.memory.NotePlace("safe", "logout point", client.PlayerX(),
                                            client.PlayerY(), client.PlayerZ(), nowMs);
                    LogLine("wind-down: arrived somewhere safe at %d,%d",
                            client.PlayerX(), client.PlayerY());
                } else {
                    LogLine("wind-down: the trip did not arrive (%s)",
                            client.TravelFailureText());
                }
                return;
            }

            // Bounded, and it has to be: the alternative to "log out here" is
            // never logging out, and a character that never ends its session
            // never proves anything about resuming one.
            if (!safeHere && !outOfTime && windDownTrips_ < 2) {
                windDownTrips_++;
                if (bank) {
                    LogLine("wind-down: travelling to a known bank at %d,%d before "
                            "logout (attempt %d)", bank->x, bank->y, windDownTrips_);
                    travelInFlight_ =
                        client.TravelToPoint(bank->x, bank->y, 3, "logout_safe");
                } else {
                    LogLine("wind-down: no bank learned yet; asking the world for one "
                            "(attempt %d)", windDownTrips_);
                    travelInFlight_ = client.TravelToService(wm::Service::Banker, state_.homeCity.c_str());
                }
                if (!travelInFlight_) {
                    LogLine("wind-down: could not start the trip (%s); logging out here",
                            client.TravelFailureText());
                }
                return;
            }
            if (!safeHere) {
                LogLine("wind-down: giving up on reaching a safe spot after %d "
                        "attempt(s); logging out at %d,%d and recording it",
                        windDownTrips_, client.PlayerX(), client.PlayerY());
                state_.memory.NoteEvent("logout_unsafe",
                                        "could not reach a known-safe spot", "",
                                        client.PlayerX(), client.PlayerY(), nowMs);
            }

            session_.endedMs = nowMs;
            session_.goldEnd = client.PlayerGold();
            {
                i32 sum = 0;
                for (const SkillTarget& t : state_.plan.skills) {
                    const i32 b = client.PlayerSkillBase(static_cast<u16>(t.skillId));
                    if (b >= 0) sum += b;
                }
                session_.skillTenthsEnd = sum;
            }
            session_.logsGathered =
                logsAtSessionStart_ >= 0
                    ? std::max(0, static_cast<i32>(client.BackpackItemCount(kLog)) -
                                      logsAtSessionStart_)
                    : 0;
            session_.placesLearned = static_cast<i32>(state_.memory.Places().size());
            session_.suppliersLearned = static_cast<i32>(state_.memory.Suppliers().size());
            session_.cleanLogout = true;

            state_.identity.totalPlayTimeMs += (nowMs - sessionStartMs_);
            state_.sessions.push_back(session_);
            if (state_.sessions.size() > kMaxSessions) {
                state_.sessions.erase(state_.sessions.begin());
            }

            LogLine("session_summary duration=%llds goals=%d/%d gold=%d->%d "
                    "skills=%.1f->%.1f logs=+%d deaths=%d places=%d suppliers=%d",
                    static_cast<long long>((nowMs - sessionStartMs_) / 1000),
                    session_.goalsCompleted, session_.goalsAttempted,
                    session_.goldStart, session_.goldEnd,
                    session_.skillTenthsStart / 10.0, session_.skillTenthsEnd / 10.0,
                    session_.logsGathered, session_.deaths,
                    session_.placesLearned, session_.suppliersLearned);

            // HOW THE DAY WAS SPENT, as one greppable line.
            //
            // R1's exit proof is "at least four goal families, none above half
            // the picks", and that has to be checkable without reading fifty
            // thousand lines by eye. Printing the shape of the day is also the
            // only way the monotony ever became visible: p0gate10 looked like
            // a healthy session until its goals were counted and turned out to
            // be CRAFT / BUY_SUPPLIES / EARN_GOLD in a ring and nothing else.
            {
                // Counted by FAMILY, not by goal kind. A crafter alternating
                // BUY_SUPPLIES / CRAFT / EARN_GOLD scores three "kinds" and
                // is still doing one thing all day; the bar has to measure
                // what R1 actually asks for.
                i32 total = 0, top = 0;
                i32 famCount[static_cast<int>(GoalFamily::Count)] = {};
                for (int i = 0; i < static_cast<int>(GoalKind::Count); ++i) {
                    const i32 n = session_.goalPicks[i];
                    if (n <= 0) continue;
                    total += n;
                    famCount[static_cast<int>(FamilyOf(static_cast<GoalKind>(i)))] += n;
                }
                i32 families = 0;
                for (int f = 0; f < static_cast<int>(GoalFamily::Count); ++f) {
                    if (famCount[f] <= 0) continue;
                    ++families;
                    if (famCount[f] > top) top = famCount[f];
                }
                std::string hist;
                for (int f = 0; f < static_cast<int>(GoalFamily::Count); ++f) {
                    if (famCount[f] <= 0) continue;
                    if (!hist.empty()) hist += " ";
                    char fc[64];
                    std::snprintf(fc, sizeof(fc), "%s=%d(%.0f%%)",
                                  GoalFamilyName(static_cast<GoalFamily>(f)),
                                  famCount[f],
                                  total ? (100.0 * famCount[f] / total) : 0.0);
                    hist += fc;
                }
                hist += " |";
                for (int i = 0; i < static_cast<int>(GoalKind::Count); ++i) {
                    const i32 n = session_.goalPicks[i];
                    if (n <= 0) continue;
                    hist += " ";
                    char cell[64];
                    std::snprintf(cell, sizeof(cell), "%s=%d(%.0f%%)",
                                  GoalKindName(static_cast<GoalKind>(i)), n,
                                  total ? (100.0 * n / total) : 0.0);
                    hist += cell;
                }
                const double topFrac = total ? (static_cast<double>(top) / total) : 1.0;
                LogLine("session_goals families=%d picks=%d top=%.0f%% varied=%d | %s",
                        families, total, topFrac * 100.0,
                        (families >= 4 && topFrac <= 0.50) ? 1 : 0,
                        hist.empty() ? "(none)" : hist.c_str());
            }

            Checkpoint(client, nowMs, "clean logout");
            LogLine("logging out");
            client.ActionLogout();
            phase_ = Phase::LoggingOut;
            return;
        }

        case Phase::LoggingOut:
            if (!client.IsInWorld()) { phase_ = Phase::Done; finished_ = true; }
            return;

        case Phase::Done:
            finished_ = true;
            return;
    }
}

namespace {
std::string Fmt2(const char* fmt, ...) {
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return std::string(buf);
}
}  // namespace

void Runner::LogGoalChange(const Observation& obs, const std::string& why) {
    // Every need considered, not just the winner and the blocked ones. "Why
    // did it not do X" is only answerable if X appears somewhere.
    {
        const std::vector<Need> all =
            AssessNeeds(state_.plan, state_.memory, obs, needCfg_);
        std::string line;
        for (const Need& n : all) {
            line += Fmt2("%s(%s%s %.2f) ", NeedKindName(n.kind), n.what.c_str(),
                         n.blocked ? " BLOCKED" : "", n.urgency);
        }
        std::string held;
        for (const std::string& t : obs.toolsHeld) { held += t; held += ","; }
        // SAY WHAT THE CHARACTER WANTS TO BUY, even when the answer is
        // "nothing". NeedSkillTraining is absent from this line whenever
        // NextSkillToBuy returns -1, and an ABSENT need is indistinguishable
        // from a need that was never asked about: run_m5/pair2 and p0gate1
        // both show a scribe with 317 gold and Meditation at 21.9 -- squarely
        // inside the 30.0 trainer ceiling -- producing no training need at
        // all, and the log gave no way to tell whether the planner had
        // decided against it or never considered it. "Why didn't it do X" has
        // to be answerable from the log.
        LogLine("needs considered: %s | tools defined=%zu held=[%s] "
                "want_train=%s (target %.1f)",
                line.empty() ? "(none)" : line.c_str(),
                needCfg_.profession ? needCfg_.profession->tools.size() : 0,
                held.c_str(),
                obs.wantTrainSkill >= 0 ? rules::SkillName(obs.wantTrainSkill)
                                        : "nothing",
                obs.wantTrainTarget / 10.0);
    }

    const std::vector<Need> needs =
        AssessNeeds(state_.plan, state_.memory, obs, needCfg_);
    const std::vector<ScoredGoal> scored = planner_.Score(needs, obs, state_.memory);
    for (const ScoredGoal& g : scored) {
        if (g.kind != planner_.Current().kind) continue;
        LogLine("  score=%.1f", g.score);
        for (const std::string& r : g.reasons) LogLine("  reason: %s", r.c_str());
        break;
    }
    // Blocked goals are reported at every change, so "why didn't it do X"
    // never needs a packet trace to answer.
    for (const ScoredGoal& g : scored) {
        if (g.feasible) continue;
        LogLine("  BLOCKED_NEED %s: %s", GoalKindName(g.kind), g.blockedWhy.c_str());
    }
    (void)why;
}

// ---------------------------------------------------------------------------
// Goal execution
// ---------------------------------------------------------------------------

void Runner::RunGoal(Client& client, const Observation& obs) {
    if (obs.nowMs < nextActionMs_) return;

    // Bounded failure, checked before the body runs so a wedged goal cannot
    // keep acting after it has been abandoned.
    std::string exhaustedWhy;
    if (planner_.Exhausted(obs.nowMs, &exhaustedWhy)) {
        LogLine("goal_failed=%s reason=\"%s\"", GoalKindName(planner_.Current().kind),
                exhaustedWhy.c_str());
        session_.goalsFailed++;
        planner_.Finish(false, exhaustedWhy.c_str(), obs.nowMs);
        return;
    }

    bool done = false;
    switch (planner_.Current().kind) {
        case GoalKind::Survive:               done = DoSurvive(client, obs); break;
        case GoalKind::Heal:                  done = DoHeal(client, obs); break;
        case GoalKind::RecoverCorpse:         done = DoRecoverCorpse(client, obs); break;
        case GoalKind::GetTool:               done = DoGetTool(client, obs); break;
        case GoalKind::ReplaceEquipment:      done = DoReplaceEquipment(client, obs); break;
        case GoalKind::Bank:                  done = DoBank(client, obs); break;
        case GoalKind::GatherLogs:            done = DoGatherLogs(client, obs); break;
        case GoalKind::TrainCombat:           done = DoTrainCombat(client, obs); break;
        case GoalKind::EarnGold:              done = DoEarnGold(client, obs); break;
        case GoalKind::TravelToRequiredPlace: done = DoTravel(client, obs); break;
        case GoalKind::TrainAtNpc:            done = DoTrainAtNpc(client, obs); break;
        case GoalKind::TradeWithPlayer:       done = DoTradeWithPlayer(client, obs); break;
        case GoalKind::Fish:                  done = DoFish(client, obs); break;
        case GoalKind::BuySupplies:           done = DoBuySupplies(client, obs); break;
        case GoalKind::Craft:                 done = DoCraft(client, obs); break;
        case GoalKind::GetFood:               done = DoGetFood(client, obs); break;
        case GoalKind::PracticeSkill:         done = DoPracticeSkill(client, obs); break;
        case GoalKind::FillSpellbook:         done = DoFillSpellbook(client, obs); break;
        case GoalKind::IdleBriefly:           done = DoIdle(client, obs); break;
        case GoalKind::Count:                 break;
    }

    if (done) {
        LogLine("goal_completed=%s progress=%d",
                GoalKindName(planner_.Current().kind), planner_.Current().progress);
        session_.goalsCompleted++;
        // Count it toward satiation so the same errand does not own the whole
        // session. A life is train, earn, sell, hunt and company in turn, not
        // one loop repeated -- and goods that found no buyer simply going into
        // the bank is a fine end to an errand, not a failure to retry.
        planner_.NoteRan(planner_.Current().kind, obs.nowMs);
        planner_.Finish(true, nullptr, obs.nowMs);
        Checkpoint(client, obs.nowMs, "goal completed");
    }
}

// --- survival --------------------------------------------------------------
//
// SurvivalTick already owns potion / bandage / disengage, proven live in
// M3.9.1. This goal adds the two things a tick-level policy cannot decide:
// whether to fight back at all, and where to go when the answer is no.

bool Runner::DoSurvive(Client& client, const Observation& obs) {
    if (obs.dead) {
        // ASK ONCE AND WAIT. Resurrection is driven by the world -- a healer
        // walking over, a shrine -- so its deadline is fifteen minutes
        // (kResurrectTimeoutMs). Re-announcing the ghost every three seconds
        // superseded the outstanding request every single time:
        //
        //   resurrect invalid_state took=3056ms superseded
        //   [ACTION] resurrect start
        //
        // for as long as the character stayed dead (run_m5/r1warrior.console
        // .txt, the first death this project has ever recorded). Same
        // retry-inside-its-own-deadline fault as the bank, the vendor and the
        // trainer -- and it survived undetected precisely because nothing had
        // ever died to exercise it. "Never fired" and "broken" look identical.
        // AND GO AND FIND A HEALER, because waiting does not work.
        //
        // ActionResurrectAccept only ANSWERS an offer the server has already
        // made. A ghost standing in a field is never offered anything, so the
        // previous version waited out a fifteen-minute deadline, failed the
        // goal, was re-picked and waited again -- for the whole session.
        // Kaelen died in a graveyard and LOGGED IN STILL DEAD on the next run
        // (run_m5/r2a.console.txt: "needs considered: StayAlive(resurrection
        // 1.00)" and nothing else, forever). That is the ghost trap the
        // project already knew about from the other end: a character that dies
        // somewhere hostile silently fails every later session.
        //
        // WHAT KILLED US, recorded once. DeathRecord carries where and when
        // but not who (PersonalKnowledge.h:44-52), so the only witness is the
        // foe we were last fighting. Death is the strongest evidence a
        // creature type can give about itself, and without this the warrior
        // would walk back to the same lich next session having learned only
        // that the graveyard is dangerous -- which it already knew.
        if (!deathBlamed_) {
            deathBlamed_ = true;
            if (!currentFoeName_.empty()) {
                LogLine("dead: blaming '%s' -- it is what we were fighting",
                        currentFoeName_.c_str());
                state_.memory.NoteCreatureOutcome(currentFoeName_.c_str(),
                                                  kCreatureEvidenceDeath,
                                                  obs.nowMs);
            }
        }

        // A player walks to a healer. So does this.
        if (client.ActionBusy()) return false;
        client.ActionResurrectAccept();

        const u32 healer = client.NearestMobileWithTrade("healer");
        if (healer) {
            i32 hx = 0, hy = 0; i8 hz = 0;
            if (client.MobilePosition(healer, &hx, &hy, &hz) &&
                TileDist(obs.x, obs.y, hx, hy) > 1 && !client.TravelBusy()) {
                LogLine("dead: a healer is here -- getting close enough to be "
                        "raised");
                travelInFlight_ = client.TravelToEntity(healer, 1);
            }
            nextActionMs_ = obs.nowMs + 4000;
            return false;
        }
        if (!client.TravelBusy() && !travelInFlight_) {
            if (++ghostTrips_ > kMaxGhostTrips) {
                LogLine("dead: %d trips and no healer found; still a ghost",
                        ghostTrips_ - 1);
                ghostTrips_ = 0;
                nextActionMs_ = obs.nowMs + 30000;
                return false;
            }
            LogLine("dead: walking to a healer (trip %d)", ghostTrips_);
            travelInFlight_ =
                client.TravelToService(wm::Service::Healer, state_.homeCity.c_str());
        }
        nextActionMs_ = obs.nowMs + 5000;
        return false;
    }

    // Alive again: the next death is a new death, and a new verdict.
    deathBlamed_ = false;

    std::vector<Client::HostileHit> hostiles;
    client.ScanHostiles(12, hostiles);
    if (hostiles.empty()) {
        currentFoe_ = 0;
        client.EnsurePeaceMode();
        return true;   // the danger passed
    }

    // Remember where this went badly -- ONCE PER FIGHT, not once per tick.
    // Per-tick notes are how one twenty-minute stalemate compounded a single
    // wolf into heat 499.89 and made the whole forest look lethal.
    if (obs.nowMs - lastDangerNoteMs_ > 60000) {
        lastDangerNoteMs_ = obs.nowMs;
        state_.memory.NoteDanger(obs.x, obs.y, 14, hostiles.front().name.c_str(),
                                 0.5, obs.nowMs);
    }

    double bailAt = needCfg_.fleeHpFraction;
    const i32 extra = obs.attackersOnMe - 1;
    if (extra > 0) bailAt = std::min(0.90, bailAt + 0.08 * std::min(3, extra));

    if (obs.HpFraction() < bailAt) {
        LogLine("interrupt=FLEE reason=\"HP %.0f%%; %d attacker(s); bail at %.0f%%\"",
                obs.HpFraction() * 100.0, obs.attackersOnMe, bailAt * 100.0);
        client.EnsurePeaceMode();
        state_.memory.NoteDanger(obs.x, obs.y, 18, hostiles.front().name.c_str(), 1.5,
                                 obs.nowMs);
        // AND WHAT IT WAS, not just where it happened. A place cannot un-scare
        // you, but a creature type can prove itself safe or dangerous, and
        // "learn which graveyard mobs are safe and which are dangerous" is the
        // owner's warrior loop. Fleeing at low health from THIS thing is the
        // strongest evidence short of dying to it.
        state_.memory.NoteCreatureOutcome(hostiles.front().name.c_str(),
                                          kCreatureEvidenceNearDeathFlee,
                                          obs.nowMs);
        if (!state_.memory.HasEvent("first_near_death")) {
            state_.memory.NoteEvent("first_near_death", hostiles.front().name.c_str(),
                                    "", obs.x, obs.y, obs.nowMs);
        }
        // Retreat toward somewhere known-safe rather than a random direction.
        const KnownPlace* bank = state_.memory.BestPlace("bank");
        if (bank && !client.TravelBusy()) {
            client.TravelToPoint(bank->x, bank->y, 3, "flee_to_bank");
        }
        nextActionMs_ = obs.nowMs + 2000;
        planner_.NoteAttempt(obs.nowMs);
        return false;
    }

    // Fight back at whatever is actually on us. Never pick a NEW fight here:
    // this goal exists because something already started one.
    const Client::HostileHit* target = nullptr;
    for (const Client::HostileHit& h : hostiles) {
        if (IsUnreachable(h.serial, obs.nowMs)) continue;
        if (!target || TileDist(h.x, h.y, obs.x, obs.y) <
                           TileDist(target->x, target->y, obs.x, obs.y)) {
            target = &h;
        }
    }
    if (!target) {
        client.EnsurePeaceMode();
        return true;
    }

    const i32 dist = TileDist(target->x, target->y, obs.x, obs.y);

    if (currentFoe_ != target->serial) {
        currentFoe_ = target->serial;
        // The NAME as well as the serial. A serial dies with the corpse; the
        // name is what a per-creature verdict is keyed on, and it is the only
        // thing left to blame once we are a ghost.
        currentFoeName_ = target->name;
        chaseBestDist_ = dist;
        chaseProgressMs_ = obs.nowMs;
        fightStartedMs_ = obs.nowMs;
        foeHpAtStart_ = target->hpCur >= 0 && target->hpMax > 0
                            ? static_cast<double>(target->hpCur) / target->hpMax
                            : -1.0;
        LogLine("engaging %s (noto %d) at %d,%d",
                target->name.empty() ? "a hostile" : target->name.c_str(),
                target->noto, target->x, target->y);
    }

    // CANNOT DENT IT. A fight neither side can win is the worst outcome
    // available: Session A spent twenty of its thirty-one minutes in one, and
    // the goal-level timeout only restarted it every five minutes because
    // something was still attacking. So the fight itself is bounded on the one
    // signal a client actually has -- the foe's health bar.
    if (obs.nowMs - fightStartedMs_ > kFightAssessMs) {
        const double foeHp = target->hpCur >= 0 && target->hpMax > 0
                                 ? static_cast<double>(target->hpCur) / target->hpMax
                                 : -1.0;
        const bool noDent = foeHp < 0.0 || foeHpAtStart_ < 0.0 ||
                            (foeHpAtStart_ - foeHp) < 0.05;
        if (noDent) {
            LogLine("interrupt=DISENGAGE reason=\"%llds of fighting and %s is "
                    "still at %s health; this is a stalemate\"",
                    static_cast<long long>((obs.nowMs - fightStartedMs_) / 1000),
                    target->name.empty() ? "it" : target->name.c_str(),
                    foeHp >= 0.0 ? "the same" : "unknown");
            MarkUnreachable(target->serial, obs.nowMs);
            state_.memory.NoteDanger(obs.x, obs.y, 16,
                                     target->name.empty() ? "a stalemate foe"
                                                          : target->name.c_str(),
                                     1.0, obs.nowMs);
            currentFoe_ = 0;
            client.EnsurePeaceMode();
            // Walk away, or the same foe is simply re-engaged next tick.
            const KnownResourceSource* stand =
                state_.memory.BestResource("logs", obs.x, obs.y, obs.nowMs);
            if (stand && !client.TravelBusy()) {
                client.TravelToPoint(stand->x, stand->y, 4, "leave_stalemate");
            }
            return true;
        }
        // It IS taking damage -- reset the window and keep fighting.
        fightStartedMs_ = obs.nowMs;
        foeHpAtStart_ = foeHp;
    }

    // BOUNDED CHASE. A wounded animal runs, and a lumberjack that follows it
    // across the countryside has stopped being a lumberjack -- the first live
    // run spent four of its five minutes chasing one fleeing mobile and never
    // returned to the trees. Progress means getting CLOSER; when there is none
    // for a while, the foe is written off and work resumes.
    if (dist <= 1) {
        chaseBestDist_ = dist;
        chaseProgressMs_ = obs.nowMs;
    } else if (dist < chaseBestDist_) {
        chaseBestDist_ = dist;
        chaseProgressMs_ = obs.nowMs;
    } else if (obs.nowMs - chaseProgressMs_ > kChaseGiveUpMs) {
        LogLine("interrupt=DISENGAGE reason=\"cannot close on %s in %llds "
                "(best %d tiles); it is not worth the chase\"",
                target->name.empty() ? "it" : target->name.c_str(),
                static_cast<long long>(kChaseGiveUpMs / 1000), chaseBestDist_);
        MarkUnreachable(target->serial, obs.nowMs);
        currentFoe_ = 0;
        client.EnsurePeaceMode();
        return true;
    }

    if (!client.WarModeOn()) client.EnterWarMode();
    client.ActionAttack(target->serial);
    if (dist > 1 && !client.GotoBusy()) client.ActionGotoMobile(target->serial, 1);
    nextActionMs_ = obs.nowMs + 1200;
    return false;
}

bool Runner::DoHeal(Client& client, const Observation& obs) {
    if (obs.HpFraction() >= 0.95) return true;
    if (obs.bandages <= 0) {
        LogLine("goal_failed=HEAL reason=\"no bandages carried\"");
        planner_.NoteAttempt(obs.nowMs);
        nextActionMs_ = obs.nowMs + 3000;
        return false;
    }
    // SurvivalTick owns the actual bandage timing (it knows the ~3s skill
    // delay and will not restart a running heal, which is the bug that made
    // uo-offline's first bandage loop heal nothing at all). Here we only make
    // sure nothing else is competing for the body.
    if (client.WarModeOn() && obs.hostilesNear == 0) client.EnsurePeaceMode();
    nextActionMs_ = obs.nowMs + 2000;
    planner_.NoteProgress();
    return false;
}

// --- corpse ----------------------------------------------------------------

bool Runner::DoRecoverCorpse(Client& client, const Observation& obs) {
    if (obs.dead) {
        // Same guard as DoSurvive: one outstanding resurrection request, not
        // one every three seconds against a fifteen-minute deadline. Both
        // goals can be the one running while the character is a ghost, so
        // both had the fault.
        if (client.ActionBusy()) return false;
        client.ActionResurrectAccept();
        nextActionMs_ = obs.nowMs + 10000;
        return false;
    }
    if (!obs.corpseKnown) return true;   // nothing to recover

    const travel::DeathRecord& death = client.Knowledge().LastDeath();
    if (TileDist(death.x, death.y, obs.x, obs.y) > 2) {
        if (client.TravelBusy()) return false;
        if (!travelInFlight_) {
            LogLine("corpse run: heading to %d,%d (attempt %d)", death.x, death.y,
                    death.recoveryAttempts + 1);
            travelInFlight_ = client.TravelToLastCorpse();
            if (!travelInFlight_) {
                LogLine("corpse run: no route (%s)", client.TravelFailureText());
                planner_.NoteAttempt(obs.nowMs);
                nextActionMs_ = obs.nowMs + 5000;
            }
            return false;
        }
        travelInFlight_ = false;
        if (!client.TravelSucceeded()) {
            client.Knowledge().NoteCorpseRecoveryAttempt();
            planner_.NoteAttempt(obs.nowMs);
        }
        return false;
    }

    // Standing on it. Open, then take everything the container reports.
    if (!client.ContainerKnown(death.corpseSerial)) {
        if (client.ActionBusy()) return false;
        client.ActionOpenContainer(death.corpseSerial);
        nextActionMs_ = obs.nowMs + 1500;
        return false;
    }
    const usize count = client.ContainerItemCount(death.corpseSerial);
    if (count == 0) {
        LogLine("corpse recovered or empty at %d,%d", death.x, death.y);
        state_.memory.NoteEvent("corpse_recovered", "", "", death.x, death.y,
                                obs.nowMs);
        client.Knowledge().ClearDeath();
        return true;
    }
    if (client.ActionBusy()) return false;
    u32 serial = 0;
    u16 graphic = 0, amount = 0;
    if (client.ContainerItemAt(death.corpseSerial, 0, &serial, &graphic, &amount)) {
        client.TakeFromContainer(serial, amount ? amount : 1);
        planner_.NoteProgress();
        nextActionMs_ = obs.nowMs + 900;
    }
    return false;
}

// --- tools and equipment ---------------------------------------------------

// Which trade sells a given tool, and where the world model files them.
// Read off this shard's own vendor templates rather than guessed, the same
// discipline the trainer and buyer tables follow.
struct ToolVendor {
    const char* tool;      // the profession's own name for it
    const char* trade;     // paperdoll-title substring
    wm::Service service;
};
const ToolVendor kToolVendors[] = {
    {"hatchet",      "blacksmith",  wm::Service::Blacksmith},
    {"pickaxe",      "blacksmith",  wm::Service::Blacksmith},
    {"fishing pole", "fisherman",   wm::Service::Fisherman},
    {"mortar",       "alchemist",   wm::Service::Alchemist},
    {"spellbook",    "mage",        wm::Service::Mage},
};

const ToolVendor* VendorForTool(const std::string& tool) {
    for (const ToolVendor& t : kToolVendors) {
        if (tool == t.tool) return &t;
    }
    return nullptr;
}

bool Runner::DoGetTool(Client& client, const Observation& obs) {
    // WHICH TOOL IS MISSING, from the catalogue rather than from a hardcoded
    // hatchet. This body was written for the lumberjack and never generalised
    // -- the fourth place in this file where that was true, after the needs,
    // the sell path and the bank. A fisher standing next to a lake was being
    // sent to a blacksmith to buy an axe it had no use for.
    std::string toolName;
    std::vector<u16> toolGfx;
    if (needCfg_.profession) {
        for (const prof::ToolNeed& t : needCfg_.profession->tools) {
            bool have = false;
            for (u16 g : t.graphics) {
                if (client.FindBackpackItemByGraphic(g) ||
                    client.EquippedGraphicAt(kLayerHand1) == g ||
                    client.EquippedGraphicAt(kLayerHand2) == g) {
                    have = true;
                    break;
                }
            }
            if (have) continue;
            toolName = t.name;
            toolGfx = t.graphics;
            break;
        }
        if (toolName.empty()) return true;   // every tool this life needs
    } else {
        // A life predating the catalogue keeps the original behaviour exactly.
        if (obs.axeInPack || obs.axeEquipped) return true;
        toolName = "hatchet";
        toolGfx.assign(kHatchet, kHatchet + 2);
        toolGfx.push_back(kAxe[0]);
        toolGfx.push_back(kAxe[1]);
    }

    const ToolVendor* tv = VendorForTool(toolName);
    if (!tv) {
        LogLine("goal_failed=GET_TOOL reason=\"%s\" tool=%s",
                faucet::RefusalName(faucet::Refusal::NoKnownBuyer),
                toolName.c_str());
        // STAND DOWN. No cooldown here meant the goal failed and was
        // re-picked on the very next tick: Bruin logged 2,058 GET_TOOL goals
        // in ten minutes at sixty-millisecond intervals (run_m7/f6_Bruin).
        // GET_TOOL sits in the Emergency family, which is exempt from
        // satiation by design -- nobody should get bored of needing an axe --
        // so a cooldown is the ONLY brake it has, and it had none.
        planner_.Cooldown(GoalKind::GetTool, obs.nowMs + kNoToolCooldownMs);
        planner_.Finish(false, "no trade known to sell it", obs.nowMs);
        nextActionMs_ = obs.nowMs + 5000;
        return false;
    }

    const KnownSupplier* known = state_.memory.BestSupplier(toolName.c_str());

    // A tool purchase is legal under the vendor policy -- a tool is not a
    // resource, and buying one shortcuts no production chain. Verify that
    // here rather than assuming it, because the policy is the thing that
    // keeps the shard's player economy alive.
    const econ::VendorRuling ruling =
        econ::CanUseNPCVendorForGraphic(toolGfx.empty() ? 0 : toolGfx[0]);
    if (!ruling.allowed) {
        LogLine("goal_failed=GET_TOOL reason=\"%s\" tool=%s class=%s",
                faucet::RefusalName(faucet::Refusal::RevolutionAuthenticityUnknown),
                toolName.c_str(), econ::VendorClassName(ruling.klass));
        state_.memory.NoteEvent("policy_refused", toolName.c_str(),
                                econ::VendorClassName(ruling.klass),
                                obs.x, obs.y, obs.nowMs);
        // Same stand-down. A policy refusal is a settled answer, not a
        // temporary one -- re-asking it sixty times a second changes nothing.
        planner_.Cooldown(GoalKind::GetTool, obs.nowMs + kNoToolCooldownMs);
        planner_.Finish(false, "the vendor policy refuses this tool", obs.nowMs);
        nextActionMs_ = obs.nowMs + 5000;
        return false;
    }

    if (client.TravelBusy()) return false;

    const u32 vendor = client.VendorOfferFrom();
    if (vendor == 0) {
        if (!travelInFlight_) {
            if (known) {
                LogLine("get_tool: returning to a remembered supplier '%s' at %d,%d",
                        known->name.c_str(), known->x, known->y);
                travelInFlight_ = client.TravelToPoint(known->x, known->y, 2, "supplier");
            } else {
                LogLine("get_tool: no remembered supplier; looking for a %s to "
                        "sell a %s", tv->trade, toolName.c_str());
                travelInFlight_ =
                    client.TravelToService(tv->service, state_.homeCity.c_str());
            }
            if (!travelInFlight_) {
                LogLine("goal_blocked=GET_TOOL reason=\"%s\" (%s)",
                        faucet::RefusalName(faucet::Refusal::VendorUnreachable),
                        client.TravelFailureText());
                planner_.NoteAttempt(obs.nowMs);
                nextActionMs_ = obs.nowMs + 15000;
            }
            return false;
        }
        travelInFlight_ = false;
        // Arrived (or gave up). Ask whoever is here to show their wares.
        const u32 keeper = client.NearestMobileWithTrade(tv->trade);
        if (!keeper) {
            LogLine("get_tool: arrived but no %s is here", tv->trade);
            state_.memory.NoteEvent("vendor_not_observed", toolName.c_str(),
                                    tv->trade, obs.x, obs.y, obs.nowMs);
            planner_.NoteAttempt(obs.nowMs);
            nextActionMs_ = obs.nowMs + 5000;
            return false;
        }
        // WALK UP FIRST. Sphere routes a vendor keyword to whoever is nearest
        // in earshot, not to the name spoken -- the lesson a carpenter taught
        // when Joshua the architect answered instead.
        i32 vx = 0, vy = 0; i8 vz = 0;
        if (client.MobilePosition(keeper, &vx, &vy, &vz)) {
            const i32 d = TileDist(obs.x, obs.y, vx, vy);
            const i32 dz = (obs.z > vz) ? (obs.z - vz) : (vz - obs.z);
            if (d > 1 || dz > 3) {
                travelInFlight_ = client.TravelToEntity(keeper, 1);
                nextActionMs_ = obs.nowMs + 2000;
                return false;
            }
        }
        client.ActionVendorOpen(keeper);
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }

    // A shop window is open. A supplier exists only once we have READ its
    // stock and seen the item -- never because a place was tagged with a
    // profession (supplier.h, and three journeys that ended at a guildmaster).
    for (const Client::VendorItem& v : client.VendorOffer()) {
        bool match = false;
        for (u16 g : toolGfx) { if (v.graphic == g) { match = true; break; } }
        if (!match) continue;

        KnownSupplier s;
        s.need = toolName;
        s.name = v.name;
        s.sourceType = "npc_vendor";
        s.serial = vendor;
        s.x = obs.x; s.y = obs.y; s.z = obs.z;
        s.observedQuantity = v.amount;
        s.observedPricePerUnit = static_cast<i32>(v.price);
        s.lastVerifiedMs = obs.nowMs;
        s.policyAllows = true;
        state_.memory.NoteSupplier(s);
        // What a thing COSTS is a price observation like any other, and the
        // character has just read it off an open window with its own eyes.
        market::PriceObservation po;
        po.item = toolName;
        po.pricePerUnit = static_cast<i32>(v.price);
        po.source = market::PriceSource::NpcVendorSells;
        po.who = v.name;
        po.x = obs.x; po.y = obs.y; po.whenMs = obs.nowMs;
        state_.prices.Note(po);
        if (!state_.memory.HasEvent("supplier_learned")) {
            state_.memory.NoteEvent("supplier_learned", v.name.c_str(), "", obs.x,
                                    obs.y, obs.nowMs);
        }
        LogLine("memory_learned=SUPPLIER need=%s name=\"%s\" price=%u qty=%u",
                toolName.c_str(), v.name.c_str(), v.price, v.amount);

        if (obs.gold < static_cast<i32>(v.price)) {
            LogLine("goal_blocked=GET_TOOL reason=\"%s\" %s costs %u, carrying %d",
                    faucet::RefusalName(faucet::Refusal::EconomicRouteBlocked),
                    toolName.c_str(), v.price, obs.gold);
            planner_.NoteAttempt(obs.nowMs);
            nextActionMs_ = obs.nowMs + 10000;
            return false;
        }
        toolGoldBefore_ = obs.gold;
        client.ActionVendorBuy(vendor, v.serial, 1);
        state_.ledger.Note(market::GoldFlow::DestroyedVendorPurchase,
                           static_cast<i32>(v.price), toolName.c_str(),
                           obs.nowMs);
        planner_.NoteProgress();
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }

    LogLine("goal_blocked=GET_TOOL reason=\"%s\" this %zu-item list has no %s",
            faucet::RefusalName(faucet::Refusal::VendorNotObserved),
            client.VendorOffer().size(), toolName.c_str());
    planner_.NoteAttempt(obs.nowMs);
    nextActionMs_ = obs.nowMs + 8000;
    return false;
}

bool Runner::DoReplaceEquipment(Client& client, const Observation& obs) {
    // The cheapest fix first: something usable is already in the pack. The axe
    // is preferred -- it is this build's weapon AND its tool, so arming it
    // solves both needs at once.
    if (!obs.weaponEquipped) {
        if (ArmAxe(client, obs)) { planner_.NoteProgress(); return false; }
        const u32 sword = FindAny(client, kKatana, 2);
        if (!AxeInHand(client) && sword) {
            if (client.ActionBusy()) return false;
            LogLine("arming: no axe carried, equipping the sword instead");
            client.ActionEquip(sword, kLayerServerChooses);
            planner_.NoteProgress();
            nextActionMs_ = obs.nowMs + 1500;
            return false;
        }
    }
    if (obs.weaponEquipped && obs.bandages >= needCfg_.bandageLow) return true;

    if (obs.bandages < needCfg_.bandageLow) {
        const econ::VendorRuling ruling = econ::CanUseNPCVendorForGraphic(kBandage);
        if (!ruling.allowed) {
            // FINISH the goal rather than retrying every 30 seconds forever.
            // The policy verdict will not change within a session, so a retry
            // is not a retry -- it is a character standing still. The need
            // itself is now reported blocked in AssessNeeds, so this is the
            // belt to that braces.
            LogLine("goal_failed=REPLACE_EQUIPMENT reason=\"the vendor policy "
                    "grades a bandage %s, and no player supplier is known\"",
                    econ::VendorClassName(ruling.klass));
            state_.memory.NoteEvent("policy_refused", "i_bandage",
                                    econ::VendorClassName(ruling.klass),
                                    obs.x, obs.y, obs.nowMs);
            planner_.Finish(false, "no legitimate source of bandages",
                            obs.nowMs);
            return false;
        }
        if (client.TravelBusy()) return false;
        const u32 vendor = client.VendorOfferFrom();
        if (vendor == 0) {
            if (!travelInFlight_) {
                travelInFlight_ = client.TravelToService(wm::Service::Healer, state_.homeCity.c_str());
                if (!travelInFlight_) {
                    LogLine("BLOCKED_NEED bandages: %s", client.TravelFailureText());
                    planner_.NoteAttempt(obs.nowMs);
                    nextActionMs_ = obs.nowMs + 15000;
                }
                return false;
            }
            travelInFlight_ = false;
            const u32 keeper = client.NearestMobileWithTrade("healer");
            if (!keeper) {
                planner_.NoteAttempt(obs.nowMs);
                nextActionMs_ = obs.nowMs + 5000;
                return false;
            }
            client.ActionVendorOpen(keeper);
            nextActionMs_ = obs.nowMs + 2500;
            return false;
        }
        for (const Client::VendorItem& v : client.VendorOffer()) {
            if (v.graphic != kBandage) continue;
            const i32 want = std::min<i32>(needCfg_.bandageFull - obs.bandages, 20);
            if (want <= 0) return true;
            if (obs.gold < static_cast<i32>(v.price) * want) {
                LogLine("BLOCKED_NEED bandages: %d cost %u each, carrying %d gold",
                        want, v.price, obs.gold);
                planner_.NoteAttempt(obs.nowMs);
                nextActionMs_ = obs.nowMs + 10000;
                return false;
            }
            KnownSupplier s;
            s.need = "bandage";
            s.name = v.name;
            s.sourceType = "npc_vendor";
            s.serial = vendor;
            s.x = obs.x; s.y = obs.y; s.z = obs.z;
            s.observedQuantity = v.amount;
            s.observedPricePerUnit = static_cast<i32>(v.price);
            s.lastVerifiedMs = obs.nowMs;
            s.policyAllows = true;
            state_.memory.NoteSupplier(s);
            LogLine("memory_learned=SUPPLIER need=bandage name=\"%s\"", v.name.c_str());
            client.ActionVendorBuy(vendor, v.serial, static_cast<u16>(want));
            planner_.NoteProgress();
            nextActionMs_ = obs.nowMs + 2500;
            return false;
        }
        LogLine("BLOCKED_NEED bandages: this vendor's list has none");
        planner_.NoteAttempt(obs.nowMs);
        nextActionMs_ = obs.nowMs + 8000;
        return false;
    }
    return true;
}

// --- banking ---------------------------------------------------------------

bool Runner::DoBank(Client& client, const Observation& obs) {
    const u32 box = client.BankContainer();
    // ONLY the serial is needed to deposit. Requiring ContainerKnown -- that
    // the box's CONTENTS have arrived -- was wrong twice over: an EMPTY bank
    // box sends no 0x3C at all, so the flag never flipped, and the character
    // re-opened the bank every 2.5 seconds forever without ever putting
    // anything in it. You do not need to know what is in a container to put
    // something into it.
    if (box) {
        // The box is open, so whoever we asked did answer. Forgive every
        // banker we had written off: the next visit starts clean.
        bankerAsked_ = 0;
        bankerCounted_ = 0;
        bankOpenTries_ = 0;
        bankerSilent_.clear();
        if (client.ActionBusy()) return false;
        // Deposit whatever THIS LIFE produces, not just logs.
        //
        // This used to be hardcoded to kLog, which is the same lumberjack
        // assumption that made a mage want a hatchet. A smith carrying fifty
        // iron ingots reached the bank and deposited nothing, stayed at
        // 165/162 stones, and crawled through hundreds of fatigue rejects for
        // the rest of its life. Five of twenty bots were immobilised by it.
        //
        // One item per tick: each move is a separate action the server may
        // refuse, and batching them hides which one failed.
        // DO NOT BANK WHAT THIS LIFE IS ABOUT TO SELL.
        //
        // The need model already refuses to schedule a deposit for sellable
        // output (Needs.cpp, SellableInstead), but a BANK objective restored
        // from a previous session bypasses that reasoning entirely: Bryn
        // reached the bank carrying 15 fish, deposited all 15, and EARN_GOLD
        // pulled the same 15 straight back out two seconds later. A player
        // does not put its stock in the box on the way to the shop.
        //
        // Weight is the exception the need model already makes, and it is the
        // real reason to bank: a load too heavy to carry to a buyer has to go
        // somewhere.
        const bool loadDemandsIt =
            obs.WeightFraction() >= needCfg_.bankWeightFrac;
        if (needCfg_.profession && (loadDemandsIt ||
                                    needCfg_.profession->produces.empty())) {
            for (const std::string& made : needCfg_.profession->produces) {
                const std::vector<u16> gfx = econ::GraphicsForItem(made.c_str());
                u32 serial = 0;
                i32 amount = 0;
                for (u16 g : gfx) {
                    const u32 found = client.FindBackpackItemByGraphic(g);
                    if (!found) continue;
                    serial = found;
                    amount = static_cast<i32>(client.BackpackItemCount(g));
                    break;
                }
                if (!serial || amount <= 0) continue;
                // A DEPOSIT THAT NEVER LANDS MUST NOT BE RETRIED FOREVER.
                //
                // The bank serial outlives the visit: Bryn stood on the
                // Britain dock, seventy tiles from any banker, and pushed the
                // same fifteen fish at a box it could not reach once a second
                // for the rest of the session -- every attempt answered
                // "item landed in a different container", none of them
                // counted, and nothing else could run.
                if (bankDepositItem_ == made) {
                    if (++bankDepositTries_ > kMaxBankDepositTries) {
                        LogLine("bank: %d attempts to deposit %s all landed "
                                "elsewhere -- this box is not really open",
                                bankDepositTries_, made.c_str());
                        client.ForgetBankContainer();
                        bankDepositTries_ = 0;
                        bankDepositItem_.clear();
                        planner_.NoteAttempt(obs.nowMs);
                        nextActionMs_ = obs.nowMs + 3000;
                        return false;
                    }
                } else {
                    bankDepositItem_ = made;
                    bankDepositTries_ = 1;
                }
                LogLine("banking %d %s", amount, made.c_str());
                client.ActionMoveItem(serial, static_cast<u16>(amount), box);
                planner_.NoteProgress();
                nextActionMs_ = obs.nowMs + 1500;
                return false;
            }
        }
        // AND THE INPUTS, when the load demands it. DoBank could only ever
        // deposit what a life PRODUCES, so a scribe carrying two hundred and
        // thirty blank scrolls at 97% of its carry limit reached the bank,
        // found nothing it was allowed to put down, completed with progress 0
        // and was re-picked -- five thousand one hundred and sixty-nine times
        // in twenty minutes. Stock is still weight.
        //
        // Keep a working batch and box the rest, so the next errand can
        // actually be walked to.
        if (loadDemandsIt && needCfg_.profession) {
            const i32 keep = needCfg_.craftBatch * 2;
            // WHAT THIS LIFE CONSUMES, from the RECIPES rather than from the
            // hand-written list. The scribe has no `consumes` at all -- its
            // inputs were only ever implied by what it makes -- so a list-only
            // version of this deposited nothing and the bank goal still span.
            // The recipe graph already knows, and it cannot fall out of step
            // with itself.
            std::vector<std::string> inputs = needCfg_.profession->consumes;
            for (const std::string& made : needCfg_.profession->produces) {
                const prod::Recipe* r = prod::FindRecipe(made.c_str());
                if (!r) continue;
                for (const prod::Ingredient& in : r->inputs) {
                    if (!in.item) continue;
                    bool seen = false;
                    for (const std::string& have : inputs) {
                        if (have == in.item) { seen = true; break; }
                    }
                    if (!seen) inputs.emplace_back(in.item);
                }
            }
            for (const std::string& input : inputs) {
                const std::vector<u16> gfx = econ::GraphicsForItem(input.c_str());
                u32 serial = 0;
                i32 amount = 0;
                for (u16 g : gfx) {
                    const u32 found = client.FindBackpackItemByGraphic(g);
                    if (!found) continue;
                    serial = found;
                    amount = static_cast<i32>(client.BackpackItemCount(g));
                    break;
                }
                if (!serial || amount <= keep) continue;
                const i32 put = amount - keep;
                LogLine("banking %d spare %s (keeping %d to work with)", put,
                        input.c_str(), keep);
                client.ActionMoveItem(serial, static_cast<u16>(put), box);
                planner_.NoteProgress();
                nextActionMs_ = obs.nowMs + 1500;
                return false;
            }
        }

        const u32 logs = loadDemandsIt || !needCfg_.profession
                             ? client.FindBackpackItemByGraphic(kLog)
                             : 0;
        if (logs) {
            const u16 amount = static_cast<u16>(client.BackpackItemCount(kLog));
            LogLine("banking %u logs", amount);
            client.ActionMoveItem(logs, amount, box);
            planner_.NoteProgress();
            nextActionMs_ = obs.nowMs + 1500;
            return false;
        }
        // AND THE DEAD WEIGHT -- what this life has no name for at all.
        //
        // The need and the action have to agree or the goal cannot terminate.
        // NeedBank is scored from CARRIED WEIGHT; every branch above deposits
        // only what the profession produces, consumes or makes from. When the
        // load is none of those, the need stays at 0.72 and the goal completes
        // having done nothing, forever.
        //
        // Ysolde is the case that proves it. A scribe with STR 10 has a carry
        // limit of 75 stones, and her STARTING KIT alone is 73 of them: two
        // chainmail coifs she cannot wear usefully, two books, a candle, three
        // cast scrolls (runtime/save/spherechars.scp, serial 04001425d). Not
        // one of those is a scroll she wrote or a reagent she buys, so she was
        // full of things she could neither use nor put down.
        //
        // A player empties that into the box. So: anything in the pack that is
        // not gold, not a tool this life declares, not a consumable it stocks,
        // not what it makes and not what it makes it FROM is dead weight, and
        // it goes in -- one per tick, each named in the log so the decision is
        // auditable. Bounded by loadDemandsIt, the same guard the inputs
        // branch uses: below the weight line a character keeps its oddments.
        if (loadDemandsIt && client.BackpackContentsKnown()) {
            std::vector<u16> keepGfx{kGoldCoin, kLog};
            auto keepAll = [&keepGfx](const std::vector<u16>& g) {
                keepGfx.insert(keepGfx.end(), g.begin(), g.end());
            };
            auto keepNamed = [&keepGfx](const std::string& item) {
                const std::vector<u16> g = econ::GraphicsForItem(item.c_str());
                keepGfx.insert(keepGfx.end(), g.begin(), g.end());
            };
            if (needCfg_.profession) {
                for (const prof::ToolNeed& t : needCfg_.profession->tools)
                    keepAll(t.graphics);
                for (const prof::ConsumableNeed& c : needCfg_.profession->consumables)
                    keepAll(c.graphics);
                for (const std::string& s : needCfg_.profession->consumes)
                    keepNamed(s);
                for (const std::string& made : needCfg_.profession->produces) {
                    keepNamed(made);
                    const prod::Recipe* r = prod::FindRecipe(made.c_str());
                    if (!r) continue;
                    for (const prod::Ingredient& in : r->inputs)
                        if (in.item) keepNamed(in.item);
                }
            }
            const u32 pack = client.BackpackSerial();
            const usize n = client.ContainerItemCount(pack);
            for (usize i = 0; i < n; ++i) {
                u32 serial = 0; u16 gfx = 0, amount = 0;
                if (!client.ContainerItemAt(pack, i, &serial, &gfx, &amount)) continue;
                if (!serial) continue;
                bool named = false;
                for (u16 k : keepGfx) { if (k == gfx) { named = true; break; } }
                if (named) continue;
                LogLine("banking dead weight: 0x%04X x%u -- this life has no "
                        "use for it and the pack is at %.0f%%",
                        gfx, amount ? amount : 1,
                        obs.WeightFraction() * 100.0);
                client.ActionMoveItem(serial, amount ? amount : 1, box);
                planner_.NoteProgress();
                nextActionMs_ = obs.nowMs + 1500;
                return false;
            }
        }

        // (Recorded at box-open from the banker's own position, not here:
        // where the character stands after the last deposit is not the bank.)
        if (!state_.memory.HasEvent("first_bank_deposit") && planner_.Current().progress > 0) {
            state_.memory.NoteEvent("first_bank_deposit", "logs", "bank", obs.x,
                                    obs.y, obs.nowMs);
        }

        // A VISIT THAT DEPOSITED NOTHING IS NOT A COMPLETED BANK GOAL.
        //
        // Reporting success here is what produced the churn: Finish(true)
        // clears `active`, the next Select() sees NeedBank still at 0.72 --
        // because nothing left the pack -- and starts BANK again 60 ms later.
        // pair2 did that for five straight minutes, fsyncing state.json on
        // every lap. Say what actually happened and stand down.
        if (planner_.Current().progress == 0) {
            LogLine("bank: the box is open and there is nothing in the pack "
                    "this life may put down (weight %d/%d) -- standing down "
                    "for %llds rather than re-deciding",
                    obs.weight, obs.maxWeight,
                    static_cast<long long>(kBankCooldownMs / 1000));
            planner_.Cooldown(GoalKind::Bank, obs.nowMs + kBankCooldownMs);
            planner_.Finish(false, "nothing to deposit", obs.nowMs);
            nextActionMs_ = obs.nowMs + 5000;
            return false;
        }
        return true;
    }

    if (client.TravelBusy()) return false;

    // WAIT FOR THE ASK BEFORE ASKING AGAIN.
    //
    // open_bank's deadline is 6 s (kBankTimeoutMs) and this used to re-issue
    // every 2.5 s, so every attempt was killed by the NEXT one before it could
    // either succeed or time out. The whole of run_m5/pair3 is sixty-three
    // repetitions of
    //
    //   [action] open_bank superseded by open_bank
    //   [ACTION_RESULT] open_bank invalid_state (2520ms) superseded
    //
    // and not one resolved result in twenty minutes. An action already in
    // flight is not a reason to start another one.
    if (client.ActionBusy()) return false;

    // AN ASK THAT PRODUCED NO BOX IS A FAILED ASK, and it has to be counted
    // here: the action layer can report "superseded" or "timeout", but only
    // this goal knows that the box it wanted still is not open.
    if (bankerAsked_) {
        // COUNT PER BANKER, NOT PER GOAL. Britain's bankers wander, so the
        // nearest one changes between asks: p0gate1 asked Hyman, Hyman, then
        // Lyndon, and wrote LYNDON off "3 times" when he had been asked once.
        // A tally that blames whoever stood closest at the third failure is
        // not evidence about anybody.
        if (bankerCounted_ != bankerAsked_) {
            bankerCounted_ = bankerAsked_;
            bankOpenTries_ = 0;
        }
        if (++bankOpenTries_ >= kMaxBankOpenTries) {
            LogLine("bank: asked 0x%08X for the box %d times and got nothing "
                    "back -- finding a different banker",
                    bankerAsked_, bankOpenTries_);
            bankerSilent_.push_back(bankerAsked_);
            bankOpenTries_ = 0;
            bankerCounted_ = 0;
        }
        bankerAsked_ = 0;
    }

    const u32 banker = client.NearestMobileWithTrade("banker", bankerSilent_);
    if (banker) {
        client.ActionOpenBank(banker);
        // REMEMBER WHERE THE BANKER STANDS, not where we happened to be when
        // the last item went into the box. Recording the player's position at
        // deposit time put Bryn's "bank" on the Britain dock, seventy tiles
        // from Hyman, and every later trip walked confidently to a spot with
        // no banker in it -- three round trips in one minute before the trip
        // counter gave up. A remembered place is only useful if it is the
        // thing, not a place the thing was once near.
        i32 bx = 0, by = 0; i8 bz = 0;
        if (client.MobilePosition(banker, &bx, &by, &bz)) {
            state_.memory.NotePlace("bank", "bank", bx, by, bz, obs.nowMs);
        }
        bankerAsked_ = banker;
        bankOpenedMs_ = obs.nowMs;
        nextActionMs_ = obs.nowMs + kBankAskGapMs;
        // ASKING IS NOT PROGRESS. NoteProgress() here reset the attempt
        // counter on every retry, so Exhausted() never fired and the planner
        // believed a goal that had done nothing for twenty minutes was
        // working. An ask is an attempt; the box opening is the progress.
        planner_.NoteAttempt(obs.nowMs);
        return false;
    }
    if (!bankerSilent_.empty()) {
        // Every banker within sight has now been asked and none opened a box.
        // Walking to another bank is the honest next move, but not on this
        // goal and not this second.
        LogLine("bank: %d banker(s) in reach and not one opened a box -- "
                "standing down for %llds",
                static_cast<int>(bankerSilent_.size()),
                static_cast<long long>(kBankCooldownMs / 1000));
        state_.memory.NoteEvent("bank_no_answer", "no banker opened a box", "",
                                obs.x, obs.y, obs.nowMs);
        bankerSilent_.clear();
        bankOpenTries_ = 0;
        bankTrips_ = 0;
        planner_.Cooldown(GoalKind::Bank, obs.nowMs + kBankCooldownMs);
        planner_.Finish(false, "no banker answered", obs.nowMs);
        nextActionMs_ = obs.nowMs + 5000;
        return false;
    }
    if (!travelInFlight_) {
        // BOUNDED. A trip that "arrives" without putting a banker in reach
        // completes instantly, and without a counter this alternates
        // start/clear forever -- the same no-op travel loop that pinned
        // GATHER_LOGS, logged eight times a second.
        if (++bankTrips_ > kMaxBankTrips) {
            LogLine("goal_failed=BANK reason=\"%d trips and still no banker in "
                    "reach; the pack stays full\"", bankTrips_ - 1);
            state_.memory.NoteEvent("bank_unreachable",
                                    "could not reach a banker", "", obs.x, obs.y,
                                    obs.nowMs);
            planner_.Finish(false, "no banker reachable", obs.nowMs);
            bankTrips_ = 0;
            nextActionMs_ = obs.nowMs + 30000;
            return false;
        }
        const KnownPlace* known = state_.memory.BestPlace("bank");
        // A REMEMBERED PLACE THAT KEEPS BEING WRONG IS NOT A MEMORY.
        //
        // Bryn walked to a "bank" on the Britain dock, found nobody, walked
        // back, and did it again -- three round trips a minute, and the trip
        // counter reset every time the goal was re-picked, so it never ran
        // out. Two failed arrivals at the same spot is enough: unlearn it and
        // ask the world model instead. The place that replaces it is recorded
        // from the BANKER's own position when a box actually opens.
        if (known && bankTrips_ > 2) {
            LogLine("bank: two trips to %d,%d found no banker -- forgetting "
                    "that place", known->x, known->y);
            state_.memory.ForgetPlace("bank", known->x, known->y);
            known = state_.memory.BestPlace("bank");
        }
        if (known) {
            LogLine("bank: returning to a remembered bank at %d,%d (trip %d)",
                    known->x, known->y, bankTrips_);
            travelInFlight_ = client.TravelToPoint(known->x, known->y, 2, "bank");
        } else {
            LogLine("bank: no bank learned yet; asking the world model for one "
                    "(trip %d)", bankTrips_);
            travelInFlight_ = client.TravelToService(wm::Service::Banker, state_.homeCity.c_str());
        }
        if (!travelInFlight_) {
            LogLine("goal_blocked=BANK reason=\"%s\"", client.TravelFailureText());
            planner_.NoteAttempt(obs.nowMs);
            nextActionMs_ = obs.nowMs + 10000;
        }
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }
    travelInFlight_ = false;
    if (!client.TravelSucceeded()) {
        LogLine("bank: the trip did not arrive (%s)", client.TravelFailureText());
        planner_.NoteAttempt(obs.nowMs);
        nextActionMs_ = obs.nowMs + 1500;
        return false;
    }
    // Arrived. ASK WHO IS HERE before concluding there is no banker:
    // NearestMobileWithTrade matches on the paperdoll title, and a title only
    // arrives after a 0x98 name request. Without this the character stands
    // next to a banker and reports none in reach.
    LogLine("bank: arrived at %d,%d -- asking who is here", client.PlayerX(),
            client.PlayerY());
    client.ActionScanMobiles();
    nextActionMs_ = obs.nowMs + 2500;
    return false;
}

// --- the work --------------------------------------------------------------

bool Runner::DoGatherLogs(Client& client, const Observation& obs) {
    if (!obs.axeInPack && !obs.axeEquipped) {
        LogLine("goal_failed=GATHER_LOGS reason=\"no axe\"");
        planner_.Finish(false, "no axe", obs.nowMs);
        return false;
    }
    if (obs.WeightFraction() >= 0.95) {
        LogLine("gather: pack full at %.0f%%", obs.WeightFraction() * 100.0);
        return true;
    }

    // Arm the axe. `skill44_lumberjacking.scp` requires SRC.WEAPON, so the axe
    // has to be IN HAND, not merely carried -- and it must be the AXE, not
    // whatever the newbie kit armed. This is also the weapon the character
    // fights with, which is the whole point of the era Lumberjack template.
    if (!obs.axeEquipped) {
        // Arming is progress, not a failed attempt -- counting it against the
        // attempt budget would exhaust the goal before the first swing.
        if (ArmAxe(client, obs)) return false;
        if (!AxeInHand(client)) {
            LogLine("goal_failed=GATHER_LOGS reason=\"no axe to arm\"");
            planner_.Finish(false, "no axe to arm", obs.nowMs);
            return false;
        }
    }

    // --- am I actually where the work is? ---------------------------------
    //
    // `areaExhausted_` overrides the census: TreeCount can still see trunks
    // here while NearestTree has none left to offer, because every one of them
    // has already been worked this visit. Believing the census in that state
    // is what kept the character standing in a clearing it had finished.
    if (!obs.atWorkSite || areaExhausted_) {
        if (client.TravelBusy()) return false;
        if (!travelInFlight_) {
            // Earned knowledge first, then common knowledge, then go looking.
            // The order matters: preferring a merely-remembered spot over a
            // named forest is what kept this character in the scrub.
            const KnownResourceSource* proven =
                state_.memory.BestProvenResource("logs", obs.x, obs.y, obs.nowMs);
            if (proven && IsDeadTarget(proven->x, proven->y)) proven = nullptr;
            const KnownResourceSource* hint =
                proven ? nullptr
                       : state_.memory.BestHint("logs", obs.x, obs.y, obs.nowMs);
            if (hint && IsDeadTarget(hint->x, hint->y)) hint = nullptr;
            if (proven) {
                LogLine("gather: back to a stand that has paid out before at "
                        "%d,%d (%d successes, %d failures)",
                        proven->x, proven->y, proven->successes, proven->failures);
                lastHintX_ = proven->x;
                lastHintY_ = proven->y;
                travelInFlight_ =
                    client.TravelToPoint(proven->x, proven->y, 4, "proven_stand");
            } else if (hint) {
                LogLine("gather: nothing proven yet -- trying %s at %d,%d "
                        "(a lead, %d disappointment(s) so far)",
                        hint->label.empty() ? "a known forest" : hint->label.c_str(),
                        hint->x, hint->y, hint->failures);
                lastHintX_ = hint->x;
                lastHintY_ = hint->y;
                travelInFlight_ =
                    client.TravelToPoint(hint->x, hint->y, 6, "forest_hint");
            } else {
                LogLine("gather: no stand and no lead left; asking the world for lumber");
                travelInFlight_ = client.TravelToResource(wm::ResourceKind::Lumber);
            }
            if (!travelInFlight_) {
                LogLine("goal_blocked=GATHER_LOGS reason=\"%s\"",
                        client.TravelFailureText());
                planner_.NoteAttempt(obs.nowMs);
                nextActionMs_ = obs.nowMs + 10000;
            }
            return false;
        }
        travelInFlight_ = false;
        // A completed journey is a new place, so the exhaustion verdict about
        // the OLD one no longer applies.
        areaExhausted_ = false;
        // ARRIVAL IS A CLAIM ABOUT THE TILE. A journey that reports success
        // and leaves us six tiles short of the trees is a failure here, and
        // saying so is what keeps it out of the "worked fine" column.
        if (client.TreeCount(client.PlayerX(), client.PlayerY(), cfg_.searchRadius) == 0) {
            LogLine("gather: trip reported %s but there are no trees within %d tiles",
                    client.TravelSucceeded() ? "success" : "failure", cfg_.searchRadius);
            // Charge the DESTINATION WE AIMED AT, and refuse to aim there
            // again this session. Charging wherever we happen to stand let a
            // no-op trip -- one that "arrived" without moving -- pile failures
            // onto a stand with three real successes, several times a second.
            if (lastHintX_ != 0 || lastHintY_ != 0) {
                state_.memory.NoteResource("logs", lastHintX_, lastHintY_,
                                           client.PlayerZ(), false, obs.nowMs);
                deadTargets_.emplace_back(lastHintX_, lastHintY_);
                if (deadTargets_.size() > 32) deadTargets_.erase(deadTargets_.begin());
                lastHintX_ = lastHintY_ = 0;
            }
            planner_.NoteAttempt(obs.nowMs);
            nextActionMs_ = obs.nowMs + 3000;
        }
        return false;
    }

    // --- pick a tree and stand next to it ---------------------------------
    if (!chopTargetValid_) {
        Client::TreeHit tree;
        // Ask for the nearest tree we have NOT already worked. The exclusion
        // is the whole point: without it every widening radius hands back the
        // same tree, the area reads as exhausted after ONE tree, and the
        // character loops on it -- 231 swings at a single trunk in one live
        // session.
        const bool found =
            client.NearestTree(obs.x, obs.y, cfg_.searchRadius, &tree, &visitedTrees_);
        if (!found) {
            LogLine("gather: every tree within %d tiles is worked out -> "
                    "this area is done for now", cfg_.searchRadius);
            // Charge the FAILURE TO THE LEAD that sent us here, so the next
            // trip picks a different named forest instead of walking back to
            // the same dry one. Without this the hint list never reorders and
            // the character loops on its nearest disappointment.
            if (lastHintX_ != 0 || lastHintY_ != 0) {
                state_.memory.NoteResource("logs", lastHintX_, lastHintY_, obs.z,
                                           false, obs.nowMs);
                lastHintX_ = lastHintY_ = 0;
            }
            visitedTrees_.clear();
            // DO NOT complete the goal here. "This area is done" is a reason
            // to GO SOMEWHERE ELSE, not a reason to hand control back -- and
            // handing it back put the character in a 2.5-second loop: the
            // planner re-picked GATHER_LOGS (still the top need), the
            // character was still standing in the same worked-out clearing,
            // and it said the same sentence again. Forty completions with
            // progress=0 in under two minutes, live.
            //
            // This is also the M4 Session L churn -- 22 goals attempted, 1
            // completed -- finally visible.
            deadTargets_.emplace_back(obs.x, obs.y);
            if (deadTargets_.size() > 32) deadTargets_.erase(deadTargets_.begin());
            areaExhausted_ = true;
            planner_.NoteAttempt(obs.nowMs);
            nextActionMs_ = obs.nowMs + 500;
            return false;
        }
        chopX_ = tree.x; chopY_ = tree.y; chopZ_ = tree.z;
        chopGraphic_ = tree.graphic;
        chopTargetValid_ = true;
        chopCursorPending_ = false;
        swingsOnTree_ = 0;
    }

    // Adjacency is read from the LIVE position, not from the tick's snapshot:
    // the walk finishes between ticks, and swinging from the stale reading is
    // what earned "You can't reach this." eight times per tree in the first
    // live run. A tree is surrounded by eight cells and some of them are other
    // trees, so try them nearest-first rather than always stepping east.
    // RANGE=2 in skill44_lumberjacking.scp -- the shard's own number. Standing
    // adjacent is not required, and demanding it wastes walks.
    if (TileDist(chopX_, chopY_, client.PlayerX(), client.PlayerY()) > 2) {
        if (client.GotoBusy()) return false;
        if (approachCell_ >= 8) {
            LogLine("cannot stand next to the tree at %d,%d -> moving on",
                    chopX_, chopY_);
            visitedTrees_.emplace_back(chopX_, chopY_);
            chopTargetValid_ = false;
            approachCell_ = 0;
            planner_.NoteAttempt(obs.nowMs);
            return false;
        }
        static const int kdx[8] = {1, -1, 0,  0, 1,  1, -1, -1};
        static const int kdy[8] = {0,  0, 1, -1, 1, -1,  1, -1};
        // Order the eight cells by how close they are to where we stand now,
        // then walk to the next untried one.
        int order[8] = {0, 1, 2, 3, 4, 5, 6, 7};
        const i32 px = client.PlayerX(), py = client.PlayerY();
        std::stable_sort(order, order + 8, [&](int a, int b) {
            return TileDist(chopX_ + kdx[a], chopY_ + kdy[a], px, py) <
                   TileDist(chopX_ + kdx[b], chopY_ + kdy[b], px, py);
        });
        const int pick = order[approachCell_++];
        client.ActionGoto(chopX_ + kdx[pick], chopY_ + kdy[pick]);
        nextActionMs_ = obs.nowMs + 1200;
        return false;
    }
    approachCell_ = 0;

    // --- swing --------------------------------------------------------------
    if (client.ActionBusy()) return false;

    if (chopCursorPending_) {
        if (client.TargetActive()) {
            client.ActionTargetStatic(chopX_, chopY_, chopZ_, chopGraphic_);
            chopCursorPending_ = false;
            lastChopMs_ = obs.nowMs;
            chopSwungJournalMs_ = client.JournalNowMs();
            // LET THE CHOP FINISH. skill44_lumberjacking.scp is DELAY=1.6 and
            // Source-X rolls rand(5)+2 strokes per attempt (CCharSkill.cpp),
            // so one chop runs 3.2 to 9.6 seconds. Re-using the axe before it
            // resolves fires @Abort -- "You decide not to chop wood for now."
            // A live session threw away 191 of 240 swings that way, an 80%
            // loss that looked like bad luck rather than a bug.
            //
            // The wait is cut short the moment the pack gains a log, so a
            // fast success is not paid for twice (see the yield check below)
            // -- and equally by any of Sphere's DEFINITIVE answers, checked
            // on a short poll below.
            nextActionMs_ = obs.nowMs + kChopPollMs;
            return false;
        }
        if (obs.nowMs - lastChopMs_ > 6000) {
            chopCursorPending_ = false;
            planner_.NoteAttempt(obs.nowMs);
        }
        return false;
    }

    // Did the last swing produce anything? The honest signal a client has is
    // the pack: logs went up, or they did not.
    //
    // BOUNDED PER TREE. The first live run swung at one tree for two minutes
    // straight because the "nothing came out" branch was gated on a timer that
    // the next swing kept resetting. Count swings instead of watching a clock:
    // a counter cannot be reset by the thing it is counting.
    // --- has the swing RESOLVED? -------------------------------------------
    //
    // A flat ten-second sleep after every swing was most of the loop's wall
    // clock, and most of it was spent waiting for an answer the server had
    // already given. "There is nothing here to chop" arrives on the FIRST
    // stroke, and with 60% of trees barren by the shard's own resource table
    // (regionresources.scp: 60.0 mr_nothing against 40.0 mr_tree) that is the
    // common case, not the exception. A live run managed seven swings in three
    // minutes and gathered nothing.
    //
    // So the ten seconds is now a CEILING, not a delay: poll briefly and stop
    // the moment Sphere says something conclusive.
    if (!chopCursorPending_ && lastChopMs_ != 0 &&
        obs.nowMs - lastChopMs_ < kChopResolveMs && obs.logs <= logsSeen_) {
        static const char* kResolved[] = {
            "there is nothing here to chop",       // barren: move on NOW
            "but fail to produce any useable wood",// attempt resolved, no yield
            "you decide not to chop wood for now", // @Abort
            "that is too far away",
            "you can't reach this",
        };
        bool done = false;
        for (const char* line : kResolved) {
            if (client.JournalSaidSince(line, chopSwungJournalMs_)) {
                done = true;
                break;
            }
        }
        if (!done) {
            nextActionMs_ = obs.nowMs + kChopPollMs;
            return false;
        }
        // Resolved with no wood. Fall through: the swing counter below decides
        // whether to try this tree once more or move to the next one.
        lastChopMs_ = 0;
    }

    if (obs.logs > logsSeen_) {
        // The chop resolved early and paid out -- stop waiting out the stroke
        // window and swing again.
        nextActionMs_ = obs.nowMs;
        logsSeen_ = obs.logs;
        swingsOnTree_ = 0;
        planner_.NoteProgress();
        state_.memory.NoteResource("logs", chopX_, chopY_, chopZ_, true, obs.nowMs);
        if (!state_.memory.HasEvent("first_logs")) {
            state_.memory.NoteEvent("first_logs", "first logs gathered", "forest",
                                    obs.x, obs.y, obs.nowMs);
            LogLine("first logs gathered at %d,%d (pack now holds %d)", obs.x, obs.y,
                    obs.logs);
        }
    } else if (swingsOnTree_ >= kMaxSwingsPerTree) {
        // A barren tree is NOT a failure. `regionresources.scp` gives
        // `r_default_tree` 60.0 mr_nothing against 40.0 mr_tree, so three trees
        // in five hold no wood at all and Source-X says so on the first swing
        // ("There is nothing here to chop", CCharSkill.cpp:1682). Counting
        // that against the goal's attempt budget ended GATHER_LOGS every few
        // trees in the live run -- the character was working correctly and
        // being told it had failed.
        LogLine("tree at %d,%d holds no wood -> next tree (%d tried this stand)",
                chopX_, chopY_, static_cast<int>(visitedTrees_.size()) + 1);
        // DO NOT MarkStump here. It rewrites the tree's graphic in OUR OWN
        // statics overlay, so the next TreeCount() sees nothing -- the bot
        // blinds itself, concludes it is not at a work site, "travels" zero
        // tiles to where it already stands, and loops, charging a failure onto
        // a genuinely productive stand every time round. `visitedTrees_`
        // already stops us re-picking this tree, and it does it without
        // lying to the census.
        state_.memory.NoteResource("logs", chopX_, chopY_, chopZ_, false, obs.nowMs);
        visitedTrees_.emplace_back(chopX_, chopY_);
        if (visitedTrees_.size() > 64) visitedTrees_.erase(visitedTrees_.begin());
        chopTargetValid_ = false;
        swingsOnTree_ = 0;
        return false;
    }

    const u32 axe = AxeSerialInHand(client);
    if (!axe) {
        planner_.NoteAttempt(obs.nowMs);
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }
    client.ActionUseObject(axe);
    chopCursorPending_ = true;
    swingsOnTree_++;
    lastChopMs_ = obs.nowMs;
    nextActionMs_ = obs.nowMs + 800;
    return false;
}


bool Runner::DoTrainCombat(Client& client, const Observation& obs) {
    // Something is already here: finish that fight. This is how every
    // character trains, hunter or not.
    // A FIGHT IN PROGRESS IS DEFENCE. A fight to pick is not.
    //
    // This used to hand EVERY sighting to DoSurvive, which answers "what is
    // most likely to kill me" and attacks the nearest reachable thing. That is
    // right when something is already swinging and wrong when choosing whom to
    // start on -- and it meant the whole M6 layer (Classify, ChooseTarget,
    // ChoosePrey and 54 unit tests) was called by nothing at all.
    if (obs.attackersOnMe > 0) return DoSurvive(client, obs);

    if (obs.hostilesNear > 0 && !client.ActionBusy()) {
        std::vector<Client::HostileHit> seen;
        client.ScanHostiles(12, seen);
        if (!seen.empty()) {
            std::vector<combat::Candidate> cands;
            cands.reserve(seen.size());
            for (const Client::HostileHit& h : seen) {
                combat::Candidate c;
                c.serial = h.serial;
                c.name   = h.name;
                // The notoriety BYTE and the Noto enum agree value for value --
                // 1 blue, 2 green, 3/4 gray, 5 orange, 6 red, 7 yellow
                // (Client.h:644, combat.h:28-37) -- so this is a mapping,
                // not a guess.
                c.noto   = static_cast<combat::Noto>(h.noto);
                c.dist   = TileDist(h.x, h.y, obs.x, obs.y);
                c.hpCur  = h.hpCur;
                c.hpMax  = h.hpMax;
                c.warMode = h.warMode;
                cands.push_back(std::move(c));
            }
            combat::Stance me;
            // REGION_FLAG_GUARDED, straight from the atlas -- there is no
            // Observation field for it and inventing one would just cache a
            // fact the world model already answers.
            {
                const wm::Region* here = client.CurrentRegion();
                me.inGuardedRegion = here && here->flags.guarded;
            }
            me.attackersOnMe   = obs.attackersOnMe;

            // What this life has LEARNED about these creatures, so a lich it
            // died to last session is not "weak and alone" today.
            const Memory& mem = state_.memory;
            const i64 now = obs.nowMs;
            const combat::CreatureDangerLookup danger =
                [&mem, now](const std::string& n) {
                    return mem.CreatureDanger(n.c_str(), now);
                };

            const combat::EngagePolicy policy;
            const int prey = combat::ChoosePrey(cands, me, combat::RevolutionCrimeRules(),
                                                policy, obs.HpFraction(), danger);
            if (prey >= 0) {
                const combat::Candidate& c = cands[static_cast<usize>(prey)];
                const combat::Classification v =
                    combat::Classify(c, me, combat::RevolutionCrimeRules(), policy,
                                     obs.HpFraction());
                // PRINT THE VERDICT. R2's whole point is that a kill is
                // exercised THROUGH the legality layer, not around it, and a
                // verdict nobody logs is a verdict nobody can check.
                LogLine("hunt: picked '%s' at %d tiles -- verdict=%s threat=%.2f "
                        "learned_danger=%.2f (%s)",
                        c.name.c_str(), c.dist, combat::LegalityName(v.legality),
                        v.threat, mem.CreatureDanger(c.name.c_str(), obs.nowMs),
                        v.reason.c_str());
                client.ActionAttack(c.serial);
                currentFoe_ = c.serial;
                currentFoeName_ = c.name;
                planner_.NoteProgress();
                nextActionMs_ = obs.nowMs + 2500;
                return false;
            }
            LogLine("hunt: %zu hostile(s) in sight and none worth starting on",
                    seen.size());
        }
        return DoSurvive(client, obs);
    }
    if (obs.hostilesNear > 0) return DoSurvive(client, obs);

    // NOTHING HERE. Until now that was the end of it -- "return true" -- and
    // it is why M6 has never once been exercised live: the layer that decides
    // what may legally be attacked was never given anything to decide about.
    // A fighter with no fight in reach should go and find one.
    if (!needCfg_.profession || !WantsToHunt(*needCfg_.profession)) return true;

    // Not while hurt, and not while loaded: the goal scorer already docks
    // both, but arriving at a graveyard at half health is a death rather than
    // a lesson, and that is a decision this goal should make for itself.
    if (obs.hp * 100 < obs.hpMax * 80) {
        LogLine("hunt: %d/%d health -- not going looking for a fight",
                obs.hp, obs.hpMax);
        return true;
    }
    if (obs.WeightFraction() >= 0.7) {
        LogLine("hunt: carrying too much to fight (%.0f%%)",
                obs.WeightFraction() * 100.0);
        return true;
    }

    if (client.TravelBusy()) return false;
    if (!travelInFlight_) {
        if (++huntTrips_ > kMaxHuntTrips) {
            LogLine("goal_failed=TRAIN_COMBAT reason=\"no hunting ground "
                    "reachable after %d trips\"", huntTrips_);
            planner_.Finish(false, "no hunting ground reachable", obs.nowMs);
            huntTrips_ = 0;
            nextActionMs_ = obs.nowMs + 60000;
            return false;
        }
        LogLine("hunt: no fight in reach -- going to the nearest graveyard "
                "(trip %d)", huntTrips_);
        travelInFlight_ = client.TravelToPlaceCategory(wm::PlaceCategory::Graveyard);
        if (!travelInFlight_) {
            LogLine("goal_blocked=TRAIN_COMBAT reason=\"%s\"",
                    client.TravelFailureText());
            planner_.NoteAttempt(obs.nowMs);
        }
        nextActionMs_ = obs.nowMs + 3000;
        return false;
    }
    travelInFlight_ = false;
    huntTrips_ = 0;
    // Arrived. Ask what is here; the targeting layer judges legality, and a
    // graveyard's dead are the one thing on this shard that is always lawful
    // to swing at.
    LogLine("hunt: arrived at %d,%d -- looking for something to fight",
            client.PlayerX(), client.PlayerY());
    client.ActionScanMobiles();
    if (!state_.memory.HasEvent("first_hunt")) {
        state_.memory.NoteEvent("first_hunt", "went hunting", "graveyard",
                                obs.x, obs.y, obs.nowMs);
    }
    nextActionMs_ = obs.nowMs + 3000;
    return false;
}

bool Runner::DoEarnGold(Client& client, const Observation& obs) {
    const prof::Profession* me = needCfg_.profession;
    if (!me) {
        // A life that predates the catalogue (the M4 lumberjack) has no
        // `produces` list, so there is nothing this goal can honestly sell.
        LogLine("earn_gold: '%s' is not in the catalogue -- nothing to sell",
                state_.plan.family.c_str());
        return true;
    }

    // --- did the last sale actually pay? ----------------------------------
    //
    // The purse is the proof, not the fact that a packet was sent. Sphere
    // answers a refused sale with silence, and a sale that "worked" without
    // gold arriving is the same silent failure that made a working trainer
    // purchase read as a failure earlier in M5.
    if (sellSent_) {
        if (sellGoldBefore_ >= 0 && obs.gold > sellGoldBefore_) {
            const i32 paid = obs.gold - sellGoldBefore_;
            const i32 each = sellWanted_ > 0 ? paid / sellWanted_ : paid;
            LogLine("earn_gold: sold %d %s for %d gold (%d each) to a '%s'",
                    sellWanted_, sellItem_.c_str(), paid, each,
                    sellTrade_.c_str());

            // What it was worth, as OBSERVED. This is the only kind of price
            // this project lets a character know.
            market::PriceObservation po;
            po.item = sellItem_;
            po.pricePerUnit = each;
            po.source = market::PriceSource::NpcVendorBuys;
            po.who = sellTrade_;
            po.x = obs.x; po.y = obs.y;
            po.whenMs = obs.nowMs;
            state_.prices.Note(po);

            // Selling to an NPC CREATES gold. Recording it as a source is what
            // makes the anti-arbitrage invariant checkable afterwards.
            state_.ledger.Note(market::GoldFlow::CreatedVendor, paid,
                               sellItem_.c_str(), obs.nowMs);

            KnownSupplier sup;
            sup.need = std::string("buyer:") + sellItem_;
            sup.name = sellTrade_;
            sup.sourceType = "npc_vendor";
            sup.x = obs.x; sup.y = obs.y; sup.z = obs.z;
            sup.observedPricePerUnit = each;
            sup.lastVerifiedMs = obs.nowMs;
            sup.policyAllows = true;
            state_.memory.NoteSupplier(sup);

            state_.memory.NoteEvent("sold_to_vendor", sellItem_.c_str(),
                                    sellTrade_.c_str(), obs.x, obs.y, obs.nowMs);
            planner_.NoteProgress();
            sellSent_ = false;
            sellAsked_ = false;
            sellTrips_ = 0;
            sellLotCap_ = 0;   // this buyer could pay; stop rationing
            Checkpoint(client, obs.nowMs, "sold to a vendor");
            return true;
        }
        if (obs.nowMs - sellAskedMs_ > 12000) {
            LogLine("earn_gold: offered %d %s and the purse did not move "
                    "(still %d) -- this buyer did not take them",
                    sellWanted_, sellItem_.c_str(), obs.gold);
            state_.memory.NoteEvent("sale_refused", sellItem_.c_str(),
                                    sellTrade_.c_str(), obs.x, obs.y, obs.nowMs);
            sellSent_ = false;
            sellAsked_ = false;
            // A VENDOR'S PURSE IS FINITE. OFFER FEWER BEFORE GIVING UP.
            //
            // Alenne bought 5 poison scrolls for 125 gold and then refused 11
            // of the same scroll at the same quoted 25 each -- 275 gold she no
            // longer had. The offer is still LISTED, so nothing about the shop
            // says no; only the silent purse does. The old code went straight
            // to the next trade, found poison scrolls have exactly one buyer
            // trade, failed the goal, and was re-picked ten seconds later to
            // offer the identical 11 again. Gold sat at 135 for the rest of
            // the run while eleven saleable scrolls sat in the pack
            // (run_m5/p0gate3).
            //
            // A player offers half. Halve until the lot is empty, and only
            // then decide this buyer is no use -- the same "a refusal is
            // information, act on it" rule as the banker and trainer paths.
            if (sellLotCap_ <= 0) sellLotCap_ = sellWanted_;
            sellLotCap_ /= 2;
            if (sellLotCap_ > 0) {
                LogLine("earn_gold: trying a smaller lot -- %d %s this time "
                        "(a vendor's own purse runs out)",
                        sellLotCap_, sellItem_.c_str());
                nextActionMs_ = obs.nowMs + 1500;
                return false;
            }
            sellLotCap_ = 0;
            ++sellBuyerIndex_;      // try the next trade that buys this
            sellTrade_.clear();
        }
        nextActionMs_ = obs.nowMs + 1500;
        return false;
    }

    // --- what is there to sell? -------------------------------------------
    const market::TradePolicy tp;
    const std::vector<market::Offer> offers =
        market::Surplus(*me, obs.pack, tp);
    if (offers.empty()) {
        // THE STOCK MAY BE IN THE BOX. The need layer scores this errand from
        // pack AND bank on purpose -- goods in the bank are still this
        // character's stock, "it just has to go and fetch them, which is a
        // step in the errand" (Needs.cpp) -- but this goal counted the pack
        // alone. A fisher with fish in the bank therefore scored NeedGold at
        // 0.45, won the scoring, entered here, found the pack empty, completed
        // with progress 0, and was re-picked sixty milliseconds later. It did
        // that for the whole session and never fished once, because the errand
        // that outranked fishing could never finish.
        //
        // Fetching the stock IS the errand, so do that rather than refuse.
        std::vector<market::Stock> holdings = obs.pack;
        for (const market::Stock& b : obs.bank) {
            bool merged = false;
            for (market::Stock& h : holdings) {
                if (h.item == b.item) { h.qty += b.qty; merged = true; break; }
            }
            if (!merged) holdings.push_back(b);
        }
        const std::vector<market::Offer> banked =
            market::Surplus(*me, holdings, tp);
        if (banked.empty()) {
            LogLine("earn_gold: nothing spare to sell (neither the pack nor "
                    "the bank holds a surplus of what this life makes)");
            return true;
        }

        // Only chase stock a buyer would actually take; a bank full of
        // player-market goods is not a reason to walk to the bank.
        const market::Offer* fetch = nullptr;
        for (const market::Offer& o : banked) {
            if (market::QtyOf(obs.bank, o.item) <= 0) continue;
            if (market::MaySellToNpc(*me, o.item.c_str(), state_.ledger).allowed) {
                fetch = &o;
                break;
            }
        }
        if (!fetch) {
            LogLine("earn_gold: the bank holds a surplus but no NPC route for "
                    "it -- that is the player market's job, not this goal's");
            return true;
        }

        if (client.BankContainer() == 0) {
            // ARRIVING IS NOT ENOUGH -- the box has to be OPENED, by asking a
            // banker for it. Travelling and then re-testing "am I at the bank"
            // loops forever the moment the trip completes instantly because
            // the character is already standing there, which is exactly what
            // it did: eight identical "going to fetch it" lines in twelve
            // seconds, never once opening the box. Same shape as the no-op
            // travel loop that pinned GATHER_LOGS.
            if (client.TravelBusy()) return false;
            const u32 banker = client.NearestMobileWithTrade("banker");
            if (banker) {
                LogLine("earn_gold: the stock is in the bank (%d %s) -- opening "
                        "the box", market::QtyOf(obs.bank, fetch->item),
                        fetch->item.c_str());
                client.ActionOpenBank(banker);
                i32 bx = 0, by = 0; i8 bz = 0;
                if (client.MobilePosition(banker, &bx, &by, &bz)) {
                    state_.memory.NotePlace("bank", "bank", bx, by, bz,
                                            obs.nowMs);
                }
                bankOpenedMs_ = obs.nowMs;
                nextActionMs_ = obs.nowMs + 2500;
                return false;
            }
            if (!travelInFlight_) {
                if (++bankTrips_ > kMaxBankTrips) {
                    LogLine("goal_failed=EARN_GOLD reason=\"%d trips and still "
                            "no banker in reach\"", bankTrips_);
                    planner_.Finish(false, "no banker reachable", obs.nowMs);
                    bankTrips_ = 0;
                    nextActionMs_ = obs.nowMs + 30000;
                    return false;
                }
                const KnownPlace* known = state_.memory.BestPlace("bank");
                LogLine("earn_gold: the stock is in the bank (%d %s) -- going "
                        "to fetch it (trip %d)",
                        market::QtyOf(obs.bank, fetch->item),
                        fetch->item.c_str(), bankTrips_);
                travelInFlight_ =
                    known ? client.TravelToPoint(known->x, known->y, 2, "bank")
                          : client.TravelToService(wm::Service::Banker,
                                                   state_.homeCity.c_str());
                if (!travelInFlight_) {
                    LogLine("goal_blocked=EARN_GOLD reason=\"%s\" (%s)",
                            faucet::RefusalName(faucet::Refusal::VendorUnreachable),
                            client.TravelFailureText());
                    planner_.NoteAttempt(obs.nowMs);
                    nextActionMs_ = obs.nowMs + 15000;
                }
                nextActionMs_ = obs.nowMs + 2000;
                return false;
            }
            travelInFlight_ = false;
            // Arrived. A title only exists after a name request, so ask who is
            // here before concluding no banker is.
            client.ActionScanMobiles();
            nextActionMs_ = obs.nowMs + 2500;
            return false;
        }
        bankTrips_ = 0;

        const std::vector<u16> gfx = econ::GraphicsForItem(fetch->item.c_str());
        const u32 serial = client.FindContainerItemByGraphic(
            client.BankContainer(), gfx.data(), gfx.size());
        if (!serial) {
            LogLine("earn_gold: the bank ledger says %s but the open box does "
                    "not show it -- the ledger is stale", fetch->item.c_str());
            return true;
        }
        const i32 take = market::QtyOf(obs.bank, fetch->item);
        LogLine("earn_gold: withdrawing %d %s to sell", take,
                fetch->item.c_str());
        client.TakeFromContainer(serial, static_cast<u16>(take));
        planner_.NoteProgress();
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }

    // Take the first offer this life may legitimately sell.
    const market::Offer* chosen = nullptr;
    for (const market::Offer& o : offers) {
        const market::SellRuling r =
            market::MaySellToNpc(*me, o.item.c_str(), state_.ledger);
        if (!r.allowed) {
            LogLine("earn_gold: will NOT sell %d %s -- %s", o.qty,
                    o.item.c_str(), r.reason);
            state_.memory.NoteEvent("sale_refused_policy", o.item.c_str(),
                                    r.reason, obs.x, obs.y, obs.nowMs);
            continue;
        }
        chosen = &o;
        break;
    }
    if (!chosen) {
        LogLine("earn_gold: everything spare is barred from an NPC sale; "
                "banking instead");
        return true;
    }

    if (sellItem_ != chosen->item) {
        sellItem_ = chosen->item;
        sellBuyerIndex_ = 0;
        sellTrade_.clear();
        sellTrips_ = 0;
        sellLotCap_ = 0;
    }
    sellWanted_ = chosen->qty;

    // --- who buys it? ------------------------------------------------------
    const std::vector<const market::NpcBuyer*> buyers =
        market::NpcBuyersFor(sellItem_.c_str());
    if (buyers.empty()) {
        // A real answer, not a failure. The character stays resource-rich and
        // wealth-poor, which is a legitimate state on this shard.
        LogLine("earn_gold: no NPC trade on this shard buys %s; banking it "
                "instead", sellItem_.c_str());
        state_.memory.NoteEvent("no_buyer", sellItem_.c_str(), "",
                                obs.x, obs.y, obs.nowMs);
        return true;
    }
    if (sellBuyerIndex_ >= buyers.size()) {
        LogLine("goal_failed=EARN_GOLD reason=\"tried all %zu trades that buy "
                "%s\" -- standing down for %llds", buyers.size(),
                sellItem_.c_str(),
                static_cast<long long>(kNoBuyerCooldownMs / 1000));
        // STAND DOWN, do not re-decide. Failing here without a cooldown put
        // EARN_GOLD straight back at the top of the list and the whole walk
        // began again 2.6 seconds later: run_m5/p0gate3 logged this same line
        // every few seconds with gold pinned at 135 and eleven saleable
        // scrolls in the pack. The buyers have not changed in that time; the
        // vendor's purse needs a restock cycle, and something else can be
        // done meanwhile.
        planner_.Cooldown(GoalKind::EarnGold, obs.nowMs + kNoBuyerCooldownMs);
        planner_.Finish(false, "no buyer took the goods", obs.nowMs);
        sellBuyerIndex_ = 0;
        sellLotCap_ = 0;
        nextActionMs_ = obs.nowMs + 5000;
        return false;
    }
    const market::NpcBuyer* buyer = buyers[sellBuyerIndex_];
    if (sellTrade_ != buyer->trade) {
        sellTrade_ = buyer->trade;
        sellService_ = ServiceForTrade(buyer->trade);
        sellAsked_ = false;
    }

    if (client.TravelBusy()) return false;

    // --- get to one ---------------------------------------------------------
    const u32 vendor = client.NearestMobileWithTrade(sellTrade_.c_str());
    if (!vendor) {
        if (sellTrips_ >= kMaxSellTrips) {
            LogLine("earn_gold: no '%s' reachable after %d trips; trying the "
                    "next trade that buys %s", sellTrade_.c_str(), sellTrips_,
                    sellItem_.c_str());
            ++sellBuyerIndex_;
            sellTrade_.clear();
            sellTrips_ = 0;
            return false;
        }
        if (!travelInFlight_) {
            ++sellTrips_;
            const KnownSupplier* known = state_.memory.BestSupplier(
                (std::string("buyer:") + sellItem_).c_str());
            if (known && known->name == sellTrade_) {
                LogLine("earn_gold: back to a buyer we have used before, "
                        "'%s' at %d,%d", known->name.c_str(), known->x, known->y);
                travelInFlight_ =
                    client.TravelToPoint(known->x, known->y, 2, "buyer");
            } else {
                LogLine("earn_gold: looking for a '%s' to buy %d %s (trip %d)",
                        sellTrade_.c_str(), sellWanted_, sellItem_.c_str(),
                        sellTrips_);
                travelInFlight_ = client.TravelToService(sellService_, state_.homeCity.c_str());
            }
            if (!travelInFlight_) {
                LogLine("goal_blocked=EARN_GOLD reason=\"%s\"",
                        client.TravelFailureText());
                planner_.NoteAttempt(obs.nowMs);
            }
            nextActionMs_ = obs.nowMs + 2000;
            return false;
        }
        travelInFlight_ = false;
        LogLine("earn_gold: arrived at %d,%d -- asking who is here",
                client.PlayerX(), client.PlayerY());
        client.ActionScanMobiles();
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }

    // --- stand next to the one we mean to deal with -------------------------
    //
    // Sphere routes a vendor keyword to whoever is NEAREST in earshot, not to
    // the name spoken. The first live sale said "Weston sell" three tiles from
    // Weston the carpenter, and JOSHUA THE ARCHITECT answered -- with "You
    // have nothing I'm interested in", because architects do not buy logs.
    // Walking up first is what makes the intended vendor the nearest listener.
    if (sellVendorSerial_ != vendor) {
        sellVendorSerial_ = vendor;
        sellApproached_ = false;
    }
    if (!sellApproached_) {
        i32 vx = 0, vy = 0; i8 vz = 0;
        if (client.MobilePosition(vendor, &vx, &vy, &vz)) {
            const i32 d = TileDist(obs.x, obs.y, vx, vy);
            const i32 dz = (obs.z > vz) ? (obs.z - vz) : (vz - obs.z);
            if (d > 1 || dz > 3) {
                LogLine("earn_gold: the '%s' is %d tiles and %d z away -- "
                        "walking up before speaking, or the nearest other "
                        "vendor answers instead", sellTrade_.c_str(), d, dz);
                // WALK TO THE MOBILE, NOT TO ITS FOOTPRINT.
                //
                // TravelToPoint zeroes travelEntitySerial_ and passes no Z at
                // all (ClientTravel.cpp:183-186), so A* is free to finish on
                // whichever floor of that column it reaches first. In a
                // multi-storey Britain mage shop that is the wrong storey:
                // "the 'mage' is 1 tiles and 40 z away", arrived by every 2D
                // measure and out of speech range by the server's, so the buy
                // list never came and the character could not sell a thing for
                // a whole session (run_m5/p0gate4). A UO storey is about 20 z
                // and the same-floor tolerance is 12, so this is never a near
                // miss -- it is a different room.
                //
                // TravelToEntity keeps the serial, re-aims at the live
                // position as it closes, and pins the goal Z on the final leg
                // (ClientTravel.cpp:644-651) -- which is exactly what chasing
                // a wandering NPC needs.
                travelInFlight_ = client.TravelToEntity(vendor, 1);
                sellApproached_ = true;   // one approach, then talk regardless
                nextActionMs_ = obs.nowMs + 2000;
                return false;
            }
        }
        sellApproached_ = true;
    }

    // --- ask what it will take ---------------------------------------------
    if (!sellAsked_) {
        LogLine("earn_gold: asking the '%s' what it buys", sellTrade_.c_str());
        client.ActionVendorSellOpen(vendor);
        sellAsked_ = true;
        sellAskedMs_ = obs.nowMs;
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }

    if (client.VendorSellFrom() != vendor) {
        if (obs.nowMs - sellAskedMs_ > 10000) {
            LogLine("earn_gold: the '%s' never showed a buy list",
                    sellTrade_.c_str());
            ++sellBuyerIndex_;
            sellTrade_.clear();
            sellAsked_ = false;
            sellVendorSerial_ = 0;
            sellApproached_ = false;
        }
        nextActionMs_ = obs.nowMs + 1500;
        return false;
    }

    // --- sell, matching by GRAPHIC -----------------------------------------
    //
    // The 0x9E list carries the serials of OUR OWN items, so this is a join
    // against the pack rather than against the vendor's stock.
    const std::vector<u16> mine = econ::GraphicsForItem(sellItem_.c_str());
    for (const Client::VendorItem& v : client.VendorSellOffer()) {
        bool match = false;
        for (u16 g : mine) { if (v.graphic == g) { match = true; break; } }
        if (!match) continue;

        i32 qty = std::min<i32>(sellWanted_, static_cast<i32>(v.amount));
        // A cap set by an earlier refusal from THIS buyer -- see the
        // "purse did not move" branch above.
        if (sellLotCap_ > 0) qty = std::min<i32>(qty, sellLotCap_);
        if (qty <= 0) continue;

        LogLine("earn_gold: '%s' offers %u gold each for %s; selling %d",
                sellTrade_.c_str(), v.price, sellItem_.c_str(), qty);
        sellWanted_ = qty;
        sellGoldBefore_ = obs.gold;
        sellAskedMs_ = obs.nowMs;
        client.ActionVendorSell(vendor, v.serial, static_cast<u16>(qty));
        sellSent_ = true;
        nextActionMs_ = obs.nowMs + 3000;
        return false;
    }

    // NOTHING IT MAKES -- BUT MAYBE SOMETHING IT FOUND.
    //
    // Loot is income for a warrior and a mage, and the sell path could not see
    // it: market::Surplus only ever considers `produces`, which means WHAT I
    // MAKE. Loot is WHAT I FOUND, and the model had no word for it, so a mage
    // with a pack of graveyard drops had nothing the economy recognised.
    //
    // The fix needs no item table and no guessing. The 0x9E list IS the
    // server's own answer: it enumerates OUR items this vendor will buy, with
    // its prices. So offer whatever is in that list that this life has no use
    // for. A graphic table would have been the wrong foundation anyway --
    // ItemNameForGraphic maps 63 graphics, nearly all crafting materials and
    // not one weapon or piece of armour.
    for (const Client::VendorItem& v : client.VendorSellOffer()) {
        if (v.amount <= 0 || v.price == 0) continue;
        if (LifeNeedsGraphic(v.graphic)) continue;   // tool, stock, or input

        i32 qty = static_cast<i32>(v.amount);
        if (sellLotCap_ > 0) qty = std::min<i32>(qty, sellLotCap_);
        if (qty <= 0) continue;

        LogLine("earn_gold: selling %d looted 0x%04X at %u each to a '%s' "
                "(this life has no use for it)",
                qty, v.graphic, v.price, sellTrade_.c_str());
        sellWanted_ = qty;
        sellGoldBefore_ = obs.gold;
        sellAskedMs_ = obs.nowMs;
        client.ActionVendorSell(vendor, v.serial, static_cast<u16>(qty));
        sellSent_ = true;
        nextActionMs_ = obs.nowMs + 3000;
        return false;
    }

    LogLine("earn_gold: this '%s' does not take %s after all, nor anything "
            "spare we are carrying; trying the next trade",
            sellTrade_.c_str(), sellItem_.c_str());
    state_.memory.NoteEvent("buyer_list_lacks_item", sellItem_.c_str(),
                            sellTrade_.c_str(), obs.x, obs.y, obs.nowMs);
    ++sellBuyerIndex_;
    sellTrade_.clear();
    sellAsked_ = false;
    return false;
}


// ---------------------------------------------------------------------------
// TRAIN_AT_NPC -- buy a skill the way a player does.
//
//   travel to a trade NPC -> ask who is here -> say "train <skill>"
//   -> READ THE PRICE THE NPC QUOTES -> hand over exactly that in gold
//   -> verify against the server's own skill number
//
// The price is never computed. Source-X answers with
// "For %d gold I will train you in all I know of %s" (defmessages.tbl
// NPC_TRAINER_PRICE) and the bot reads that line. If the shard retunes
// NPCTrainCost the bot follows without being told, and a refusal
// ("I know nothing about that", "You already know as much as I can teach")
// is read as a refusal rather than timed out.
//
// Nothing here sets a skill. The gold leaves the pack by an ordinary
// lift-and-drop onto the trainer, and the proof of training is the server's
// skill value afterwards.
// ---------------------------------------------------------------------------
bool Runner::DoTrainAtNpc(Client& client, const Observation& obs) {
    const int skillId = obs.wantTrainSkill;
    if (skillId < 0) return true;

    const i32 have = client.PlayerSkillBase(static_cast<u16>(skillId));

    // --- did the gold we handed over actually buy anything? ---------------
    //
    // The proof is the SERVER'S skill number, not our own bookkeeping. Asking
    // for a fresh skill list is what a player's client does anyway.
    if (trainPaid_) {
        if (have > trainSkillBefore_) {
            LogLine("training: %s %.1f -> %.1f, bought from a trainer",
                    rules::SkillName(skillId), trainSkillBefore_ / 10.0, have / 10.0);
            state_.memory.NoteEvent("skill_trained", rules::SkillName(skillId),
                                    trainerTrade_.c_str(), obs.x, obs.y,
                                    obs.nowMs);
            // Remember WHERE, so the next skill this life buys does not start
            // the search from nothing. Recorded only because we dealt with it.
            KnownSupplier sup;
            sup.need = std::string("trainer:") + trainerTrade_;
            sup.name = trainerTrade_;
            sup.sourceType = "npc_trainer";
            sup.x = obs.x; sup.y = obs.y; sup.z = obs.z;
            sup.observedPricePerUnit = trainQuoted_;
            sup.lastVerifiedMs = obs.nowMs;
            sup.policyAllows = true;
            state_.memory.NoteSupplier(sup);
            // The other half of the verdict: this trade DOES teach this skill,
            // and here is what it charged. Worth as much as the refusal.
            TrainerVerdict v;
            v.skillId  = skillId;
            v.trade    = trainerTrade_;
            v.npcSerial = trainerSerial_;
            v.taught   = true;
            v.atTenths = have;
            v.quoted   = trainQuoted_;
            v.why      = "taught";
            v.whenMs   = obs.nowMs;
            state_.memory.NoteTrainerVerdict(v);
            trainPaid_ = false;
            trainAsked_ = false;
            trainSkillsAsked_ = false;
            trainPackRefreshed_ = false;
            trainPayAttempts_ = 0;
            trainTrips_ = 0;
            Checkpoint(client, obs.nowMs, "skill bought from a trainer");
            return true;
        }
        // The server does not push the new number, so ASK for it -- once,
        // promptly. The first version only asked after a ten-second timeout
        // had already declared failure, so a purchase that actually worked
        // (11.8 -> 21.1 for 93 gold, live) was recorded as "has not moved".
        if (!trainSkillsAsked_ && obs.nowMs - trainPaidMs_ > 1500) {
            client.ActionRequestSkills();
            trainSkillsAsked_ = true;
            nextActionMs_ = obs.nowMs + 1500;
            return false;
        }
        if (obs.nowMs - trainPaidMs_ > 15000) {
            LogLine("training: paid %d for %s but the server still reports "
                    "%.1f after 15s", trainQuoted_, rules::SkillName(skillId),
                    have / 10.0);
            state_.memory.NoteEvent("training_unverified",
                                    rules::SkillName(skillId),
                                    trainerTrade_.c_str(), obs.x, obs.y,
                                    obs.nowMs);
            planner_.NoteAttempt(obs.nowMs);
            trainPaid_ = false;
            trainAsked_ = false;
            trainSkillsAsked_ = false;
            // Ask for the pack again: the most likely reason a give did
            // nothing is that the serial it named no longer exists.
            trainPackRefreshed_ = false;
        }
        nextActionMs_ = obs.nowMs + 1500;
        return false;
    }
    if (have >= obs.wantTrainTarget) {
        LogLine("training: %s is already at %.1f -- nothing to buy",
                rules::SkillName(skillId), have / 10.0);
        return true;
    }

    if (client.TravelBusy()) return false;

    // --- get to a trainer --------------------------------------------------
    // Skip anyone of this trade who has already been asked three times and
    // never said a word. Alenne the mage stood one tile away and answered
    // nothing about Meditation across two sessions and seven asks, while Alek
    // -- also "the mage", in a different Britain shop -- quoted 184 gold for
    // the same skill on the first ask. Why one answers and the other does not
    // is UNKNOWN; what a player does about it is not, and is what this does.
    // AND SKIP ANYONE WHO ALREADY REFUSED. Silence and refusal are different
    // answers with the same consequence for THIS NPC -- and, because the
    // ceiling is that NPC's own skill and not the trade's, the next one may
    // still teach it. Seeded from memory rather than kept in the transient
    // list, so a refusal survives the logout that earned it.
    for (u32 refused :
         state_.memory.TrainersWhoRefused(skillId, trainerTrade_.c_str())) {
        bool known = false;
        for (u32 k : trainerSilent_) { if (k == refused) { known = true; break; } }
        if (!known) trainerSilent_.push_back(refused);
    }
    // THE GUILDMASTER FIRST. Both the guildmaster and the shopkeeper of a
    // trade answer NearestMobileWithTrade -- it truncates "mage guildmaster"
    // to "mage" -- so it returns whoever is nearer, and in Britain's mage shop
    // that is always the shopkeeper. Alenne, two tiles away, refused Ysolde
    // Meditation at 21.9 on every visit while a mage guildmaster stood five
    // tiles off at 1490,1549 able to teach it (run_m5/p0gate7). Ask the one
    // whose job it is; fall back to the trade only when no guildmaster is in
    // reach, because an ordinary tradesman can still teach a low skill.
    u32 trainer = client.NearestGuildmasterForTrade(trainerTrade_.c_str(),
                                                    trainerSilent_);
    if (trainer && trainer != trainerSerial_) {
        LogLine("training: a '%s guildmaster' is here -- asking them rather "
                "than the shopkeeper", trainerTrade_.c_str());
    }
    if (!trainer) {
        trainer = client.NearestMobileWithTrade(trainerTrade_.c_str(),
                                                trainerSilent_);
    }
    if (!trainer) {
        if (trainTrips_ >= kMaxTrainTrips) {
            LogLine("goal_failed=TRAIN_AT_NPC reason=\"no '%s' reachable after "
                    "%d trips\"", trainerTrade_.c_str(), trainTrips_);
            state_.memory.NoteEvent("trainer_unreachable", trainerTrade_.c_str(),
                                    "", obs.x, obs.y, obs.nowMs);
            // STAND DOWN. Without this the goal was re-picked 60 ms later and
            // the whole three-trip count began again -- and every one of those
            // "trips" was a no-op, because the remembered trainer place is the
            // tile the character is already standing on: run_m5/p0gate9 logs
            // travel_start and ARRIVED at the same coordinate with legs=0,
            // three times, then goal_failed, then immediately goal=TRAIN_AT_NPC
            // again. Nothing about the world changes in two seconds.
            planner_.Cooldown(GoalKind::TrainAtNpc,
                              obs.nowMs + kNoTrainerCooldownMs);
            planner_.Finish(false, "no trainer reachable", obs.nowMs);
            trainTrips_ = 0;
            nextActionMs_ = obs.nowMs + 5000;
            return false;
        }
        if (!travelInFlight_) {
            ++trainTrips_;
            const KnownSupplier* known = state_.memory.BestSupplier(
                (std::string("trainer:") + trainerTrade_).c_str());
            if (known) {
                // STANDING ON THE SPOT AND SEEING NOBODY IS THE DISPROOF.
                //
                // A remembered supplier is a POSITION, not a mobile, and the
                // NPC that earned it can be gone -- despawned, re-rolled by a
                // spawner, or just wandered off. The old code walked back
                // regardless, which meant that when the trainer was missing it
                // issued a travel to the tile it was ALREADY on. Live:
                //
                //   arrived at (2629,2099,10)
                //   training: back to a trainer we have used before, 'carpenter' at 2629,2099
                //   training: back to a trainer we have used before, 'carpenter' at 2629,2099
                //   goal_failed=TRAIN_AT_NPC reason="no 'carpenter' reachable after 3 trips"
                //
                // Two of the three trips were spent travelling nowhere, two
                // seconds apart, and the goal then blamed the world. This is
                // the same shape ForgetPlace was written for -- belief that
                // survives being disproved is not memory, it is a loop.
                const i32 dToKnown = TileDist(obs.x, obs.y, known->x, known->y);
                if (dToKnown <= 3) {
                    LogLine("training: no '%s' where we remembered one at %d,%d "
                            "-- forgetting it and looking properly",
                            trainerTrade_.c_str(), known->x, known->y);
                    state_.memory.ForgetSupplier(
                        (std::string("trainer:") + trainerTrade_).c_str(),
                        known->x, known->y);
                    // Do not spend a trip on a lesson. Rescan from here first:
                    // the trade may be a few tiles off rather than absent.
                    --trainTrips_;
                    client.ActionScanMobiles();
                    nextActionMs_ = obs.nowMs + 2000;
                    return false;
                }
                LogLine("training: back to a trainer we have used before, "
                        "'%s' at %d,%d", known->name.c_str(), known->x, known->y);
                travelInFlight_ =
                    client.TravelToPoint(known->x, known->y, 2, "trainer");
            } else {
                LogLine("training: looking for a '%s' to teach %s (trip %d)",
                        trainerTrade_.c_str(), rules::SkillName(skillId), trainTrips_);
                travelInFlight_ = client.TravelToServiceSkipping(
                    trainerService_, state_.homeCity.c_str(), trainerSilent_,
                    &trainerShopsTried_);
            }
            if (!travelInFlight_) {
                LogLine("goal_blocked=TRAIN_AT_NPC reason=\"%s\"",
                        client.TravelFailureText());
                planner_.NoteAttempt(obs.nowMs);
            }
            nextActionMs_ = obs.nowMs + 2000;
            return false;
        }
        travelInFlight_ = false;
        LogLine("training: arrived at %d,%d -- asking who is here",
                client.PlayerX(), client.PlayerY());
        client.ActionScanMobiles();
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }

    // --- stand where a player would stand ---------------------------------
    //
    // Speech is heard by position, not by intent. The first live attempt at a
    // castle scribe stood 3 tiles away in 2D but SEVEN z below her -- another
    // floor -- and got a greeting but no training reply, three times, silently.
    // Close the distance in three dimensions before talking.
    if (trainerSerial_ != trainer) {
        // A DIFFERENT NPC, so the conversation starts over. Leaving
        // trainAsked_ set meant the bot skipped re-asking and then judged the
        // silence of an NPC it had never spoken to, reporting "the trainer
        // never answered" about a conversation that never happened.
        trainerSerial_ = trainer;
        trainerApproached_ = false;
        trainApproaches_ = 0;
        trainSilentAsks_ = 0;
        trainAsked_ = false;
    }
    if (!trainerApproached_) {
        i32 tx = 0, ty = 0; i8 tz = 0;
        if (client.MobilePosition(trainer, &tx, &ty, &tz)) {
            const i32 d = TileDist(obs.x, obs.y, tx, ty);
            const i32 dz = (obs.z > tz) ? (obs.z - tz) : (tz - obs.z);
            if (d > 2 || dz > 3) {
                LogLine("training: '%s' is %d tiles and %d z away -- walking to "
                        "them before speaking (approach %d of %d)",
                        trainerTrade_.c_str(), d, dz, trainApproaches_ + 1,
                        kMaxTrainApproaches);
                travelInFlight_ = client.TravelToEntity(trainer, 1);
                // ONE APPROACH WAS NOT ENOUGH, and the cost of that was a
                // whole session. Ysolde stood 7 tiles and 2 z from Alenne,
                // gave up closing after a single attempt, and then asked about
                // Meditation seven times from outside the shop. Every ask went
                // unheard, every timeout read as "the trainer never answered",
                // and the character concluded a perfectly good trainer was
                // useless. The very next session it walked all the way in,
                // asked once, and was quoted 219 gold.
                //
                // Speech is heard by POSITION. Keep closing until we are
                // actually in earshot, and only then talk regardless.
                if (++trainApproaches_ >= kMaxTrainApproaches) {
                    trainerApproached_ = true;
                }
                nextActionMs_ = obs.nowMs + 2000;
                return false;
            }
            trainApproaches_ = 0;
        }
        trainerApproached_ = true;
    }

    // --- ask, then read what the NPC actually says -------------------------
    if (!trainAsked_) {
        // TWO marks, on two clocks, because they answer different questions:
        // the journal mark says "read replies after this point", the tick mark
        // says "how long have I waited". Using the journal clock for both made
        // a 12-second window expire in 2.5 seconds.
        trainAskedMs_ = client.JournalNowMs();
        trainAskedTickMs_ = obs.nowMs;
        trainAsked_ = true;
        LogLine("training: asking the trainer about %s", rules::SkillName(skillId));
        client.ActionNpcTrain(trainer, SkillKey(skillId));
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }

    // Refusals first -- each is a real answer, not a timeout.
    struct Refusal { const char* text; const char* why; };
    static const Refusal kRefusals[] = {
        {"i know nothing about",        "this NPC does not teach it"},
        {"you know more about",         "the character already exceeds the trainer"},
        {"you already know as much",    "the trainer has nothing left to give"},
        {"i would never train",         "the trainer refuses this character"},
        {"there is nothing that i can", "the trainer has nothing to teach"},
    };
    for (const Refusal& r : kRefusals) {
        if (!client.JournalSaidSince(r.text, trainAskedMs_)) continue;
        LogLine("training: %s refused to teach %s at %.1f -- %s",
                trainerTrade_.c_str(), rules::SkillName(skillId), have / 10.0,
                r.why);
        // A durable verdict, not a log line. The previous version wrote an
        // event nothing ever read and then reset the trip counter, so the
        // character re-selected the goal and asked the same NPC again roughly
        // every two seconds for the rest of the session -- 30+ times in the
        // first live run, with the NPC patiently refusing each time.
        TrainerVerdict v;
        v.skillId  = skillId;
        v.trade    = trainerTrade_;
        v.npcSerial = trainerSerial_;
        v.taught   = false;
        v.atTenths = have;
        v.why      = r.why;
        v.whenMs   = obs.nowMs;
        state_.memory.NoteTrainerVerdict(v);
        state_.memory.NoteEvent("trainer_refused", r.why, trainerTrade_.c_str(),
                                obs.x, obs.y, obs.nowMs);
        planner_.Finish(false, r.why, obs.nowMs);
        trainAsked_ = false;
        trainTrips_ = 0;
        Checkpoint(client, obs.nowMs, "trainer refusal");
        return false;
    }

    const i32 quoted = client.JournalNumberSince("i will train you", trainAskedMs_);
    if (quoted <= 0) {
        if (obs.nowMs - trainAskedTickMs_ > 12000) {
            ++trainSilentAsks_;
            LogLine("training: no quote and no refusal after 12s (ask %d of %d)",
                    trainSilentAsks_, kMaxSilentAsks);
            planner_.NoteAttempt(obs.nowMs);
            trainAsked_ = false;
            // Silence is most often distance. Try to close it again before
            // repeating the same words from the same spot.
            trainerApproached_ = false;
            if (trainSilentAsks_ >= kMaxSilentAsks) {
                // Give up on this NPC for now. Deliberately NOT written as a
                // trainer verdict: a verdict is what an NPC SAID, and this one
                // said nothing. Recording silence as a refusal would teach the
                // character something the world never told it.
                LogLine("goal_failed=TRAIN_AT_NPC reason=\"'%s' never answered "
                        "about %s\"", trainerTrade_.c_str(),
                        rules::SkillName(skillId));
                state_.memory.NoteEvent("trainer_silent",
                                        rules::SkillName(skillId),
                                        trainerTrade_.c_str(), obs.x, obs.y,
                                        obs.nowMs);
                planner_.Cooldown(GoalKind::TrainAtNpc,
                                  obs.nowMs + kNoTrainerCooldownMs);
                planner_.Finish(false, "the trainer never answered", obs.nowMs);
                // Do not walk back to this same silent NPC next time. Held for
                // the session only, and not written to memory: silence is not
                // something the world told the character, so it is not a
                // belief -- it is just somewhere already tried today.
                bool listed = false;
                for (u32 sk : trainerSilent_) {
                    if (sk == trainerSerial_) { listed = true; break; }
                }
                if (!listed && trainerSerial_) {
                    trainerSilent_.push_back(trainerSerial_);
                }
                trainSilentAsks_ = 0;
                trainerSerial_ = 0;
                trainerApproached_ = false;
            }
        }
        nextActionMs_ = obs.nowMs + 1500;
        return false;
    }

    if (obs.gold < quoted) {
        LogLine("BLOCKED_NEED %s: the trainer wants %d gold and the purse holds "
                "%d -- going back to work", rules::SkillName(skillId), quoted, obs.gold);
        state_.memory.NoteEvent("trainer_quote", rules::SkillName(skillId),
                                trainerTrade_.c_str(), obs.x, obs.y, obs.nowMs);
        planner_.Finish(false, "cannot afford the quoted fee", obs.nowMs);
        trainAsked_ = false;
        return false;
    }

    // --- pay exactly what was quoted ---------------------------------------
    //
    // Ask for the pack's contents FIRST. Sphere splits a gold stack to make
    // change, which retires the old serial, and a give addressed to a retired
    // serial is a silent no-op: no gold moves, the NPC says nothing, and
    // nothing anywhere reports an error. That is exactly what happened on the
    // second purchase of the first successful live run -- the first 108gp
    // give did nothing and only the retry landed.
    if (!trainPackRefreshed_) {
        client.ActionOpenBackpack();
        trainPackRefreshed_ = true;
        nextActionMs_ = obs.nowMs + 1200;
        return false;
    }
    if (trainPayAttempts_ >= kMaxPayAttempts) {
        LogLine("goal_failed=TRAIN_AT_NPC reason=\"%d attempts to hand over %d "
                "gold all failed\"", trainPayAttempts_, quoted);
        planner_.Finish(false, "could not hand over the fee", obs.nowMs);
        trainAsked_ = false;
        trainPackRefreshed_ = false;
        trainPayAttempts_ = 0;
        return false;
    }
    const u32 gold = client.FindBackpackItemByGraphic(kGoldCoin);
    if (!gold) {
        LogLine("training: quoted %d but no gold stack found in the pack", quoted);
        planner_.NoteAttempt(obs.nowMs);
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }
    LogLine("training: paying the quoted %d gold for %s (purse %d)",
            quoted, rules::SkillName(skillId), obs.gold);
    trainSkillBefore_ = have;
    trainQuoted_ = quoted;
    // The TICK clock, not the journal clock. These are different clocks and
    // mixing them made the ten-second verification window expire in 8.7s.
    trainPaidMs_ = obs.nowMs;
    trainSkillsAsked_ = false;
    ++trainPayAttempts_;
    client.ActionNpcGive(trainer, gold, static_cast<u16>(quoted));
    trainPaid_ = true;
    nextActionMs_ = obs.nowMs + 4000;
    return false;
}

// ---------------------------------------------------------------------------
// TRADE_WITH_PLAYER -- the only goal that needs somebody else to exist.
//
// Both halves run in the same body, because a character is whichever one the
// situation makes it: it announces what it has spare, and it answers what it
// hears. A fleet of twenty is mostly listeners at any moment.
//
//   stand where players gather (the home bank)
//   -> announce "WTS 20 i_log 2gp", occasionally, not every tick
//   -> hear "WTB i_log" -> walk to the speaker -> open the trade window
//   -> put the goods in -> accept -> verify against the PACK, not the packet
//
// The listener half is symmetrical: hear a WTS, decide with ConsiderOffer,
// answer with WTB, and wait to be traded with.
// ---------------------------------------------------------------------------
bool Runner::DoTradeWithPlayer(Client& client, const Observation& obs) {
    const prof::Profession* me = needCfg_.profession;
    if (!me) return true;

    // --- a trade is already open: drive it to a conclusion ------------------
    const trade::TradeState& tr = client.Trade();
    if (tr.Active()) {
        return DriveOpenTrade(client, obs);
    }
    if (tr.CurrentPhase() == trade::Phase::Completed) {
        LogLine("trade: window closed complete with %s", tradePartnerName_.c_str());
        // The PACK is the proof. A completed window means the server moved
        // the goods; believing the packet without checking is how a "sale"
        // that moved nothing gets recorded as income.
        const i32 now = market::QtyOf(obs.pack, tradeItem_);
        if (tradeSellingQty_ > 0 && now < tradePackBefore_) {
            const i32 moved = tradePackBefore_ - now;
            const i32 paid = obs.gold - tradeGoldBefore_;
            LogLine("trade: gave %d %s to %s for %d gold", moved,
                    tradeItem_.c_str(), tradePartnerName_.c_str(), paid);
            if (paid > 0) {
                market::PriceObservation po;
                po.item = tradeItem_;
                po.pricePerUnit = paid / moved;
                po.source = market::PriceSource::PlayerTraded;
                po.who = tradePartnerName_;
                po.x = obs.x; po.y = obs.y; po.whenMs = obs.nowMs;
                state_.prices.Note(po);
                state_.ledger.Note(market::GoldFlow::TransferPlayerTrade, paid,
                                   tradeItem_.c_str(), obs.nowMs);
            }
            state_.memory.NoteEvent("traded_with_player", tradeItem_.c_str(),
                                    tradePartnerName_.c_str(), obs.x, obs.y,
                                    obs.nowMs);
            planner_.NoteProgress();
        } else if (tradeSellingQty_ == 0 && now > tradePackBefore_) {
            const i32 got = now - tradePackBefore_;
            const i32 spent = tradeGoldBefore_ - obs.gold;
            LogLine("trade: got %d %s from %s for %d gold", got,
                    tradeItem_.c_str(), tradePartnerName_.c_str(), spent);
            if (spent > 0) {
                market::PriceObservation po;
                po.item = tradeItem_;
                po.pricePerUnit = spent / got;
                po.source = market::PriceSource::PlayerTraded;
                po.who = tradePartnerName_;
                po.x = obs.x; po.y = obs.y; po.whenMs = obs.nowMs;
                state_.prices.Note(po);
                state_.ledger.Note(market::GoldFlow::TransferPlayerTradeOut, spent,
                                   tradeItem_.c_str(), obs.nowMs);
            }
            planner_.NoteProgress();
        } else {
            LogLine("trade: window completed but nothing moved");
        }
        ResetTradeState();
        Checkpoint(client, obs.nowMs, "traded with a player");
        return true;
    }
    if (tr.CurrentPhase() == trade::Phase::Cancelled) {
        LogLine("trade: %s cancelled (%s)", tradePartnerName_.c_str(),
                trade::CloseReasonName(tr.Reason()));
        state_.memory.NoteEvent("trade_cancelled", tradeItem_.c_str(),
                                tradePartnerName_.c_str(), obs.x, obs.y,
                                obs.nowMs);
        ResetTradeState();
        return false;
    }

    // --- listen ------------------------------------------------------------
    //
    // Done BEFORE announcing, so a character that can answer somebody else's
    // offer does that rather than adding its own to the noise.
    std::vector<Client::Heard> heard;
    client.JournalHeardSince(tradeHeardMs_, heard);
    if (!heard.empty()) tradeHeardMs_ = heard.back().timeMs;

    for (const Client::Heard& h : heard) {
        // Somebody answered OUR offer.
        std::string wanted;
        if (!tradeOffer_.item.empty() &&
            market::ParseBuyReply(h.text, &wanted) && wanted == tradeOffer_.item) {
            LogLine("trade: %s wants our %s", h.name.c_str(), wanted.c_str());
            tradePartner_ = h.speaker;
            tradePartnerName_ = h.name;
            tradeItem_ = tradeOffer_.item;
            tradeSellingQty_ = tradeOffer_.qty;
            return false;   // next tick walks over and opens the window
        }
        // Somebody is selling something we need.
        market::TradeIntent offer;
        if (!market::ParseSellOffer(h.text, &offer)) continue;
        const market::BuyDecision d =
            market::ConsiderOffer(*me, obs.pack, obs.gold, tradePolicy_, offer);
        LogLine("trade: heard '%s' from %s -> %s (%s)", h.text.c_str(),
                h.name.c_str(), d.accept ? "want it" : "no", d.reason);
        if (!d.accept) continue;
        // Say so out loud. The seller is listening for exactly this, and
        // saying it is also what makes the deal visible to a human watching.
        client.ActionSay(market::FormatBuyReply(offer.item).c_str());
        tradePartner_ = h.speaker;
        tradePartnerName_ = h.name;
        tradeItem_ = offer.item;
        tradeSellingQty_ = 0;            // we are the BUYER
        tradeWantQty_ = d.qty;
        tradeOfferPrice_ = offer.pricePerUnit;
        return false;
    }

    // --- a partner is named: go and open the window -------------------------
    if (tradePartner_ != 0) {
        if (client.TravelBusy()) return false;
        i32 px = 0, py = 0; i8 pz = 0;
        if (!client.MobilePosition(tradePartner_, &px, &py, &pz)) {
            LogLine("trade: lost sight of %s", tradePartnerName_.c_str());
            ResetTradeState();
            return false;
        }
        if (TileDist(obs.x, obs.y, px, py) > 2) {
            if (!travelInFlight_) {
                travelInFlight_ = client.TravelToEntity(tradePartner_, 1);
                nextActionMs_ = obs.nowMs + 1500;
            } else {
                travelInFlight_ = false;
            }
            return false;
        }
        // Only the SELLER opens the window, so both sides do not race to open
        // one and cancel each other. Sphere opens a trade by dropping an item
        // on the partner, which the buyer has nothing to do with yet.
        if (tradeSellingQty_ > 0) {
            const std::vector<u16> gfx = econ::GraphicsForItem(tradeItem_.c_str());
            u32 serial = 0;
            for (u16 g : gfx) {
                serial = client.FindBackpackItemByGraphic(g);
                if (serial) break;
            }
            if (!serial) {
                LogLine("trade: no %s in the pack after all", tradeItem_.c_str());
                ResetTradeState();
                return false;
            }
            tradePackBefore_ = market::QtyOf(obs.pack, tradeItem_);
            tradeGoldBefore_ = obs.gold;
            LogLine("trade: opening a window with %s for %d %s",
                    tradePartnerName_.c_str(), tradeSellingQty_,
                    tradeItem_.c_str());
            client.ActionTradeStart(tradePartner_, serial);
            nextActionMs_ = obs.nowMs + 2500;
            return false;
        }
        // Buyer: stand there and wait for the seller to open it.
        tradePackBefore_ = market::QtyOf(obs.pack, tradeItem_);
        tradeGoldBefore_ = obs.gold;
        if (obs.nowMs - tradeAnnouncedMs_ > 20000) {
            LogLine("trade: %s never opened a window", tradePartnerName_.c_str());
            ResetTradeState();
        }
        nextActionMs_ = obs.nowMs + 1500;
        return false;
    }

    // --- collect the stock before selling it --------------------------------
    //
    // Everything this character ever gathered is in the bank, because banking
    // is what it does when a pack fills. Announcing goods that are in a box on
    // the other side of town is an offer it cannot honour, so the withdrawal
    // is part of the errand.
    if (obs.atBank) {
        market::TradeIntent want;
        std::vector<market::Stock> holdings = obs.pack;
        for (const market::Stock& b : obs.bank) {
            bool merged = false;
            for (market::Stock& h : holdings) {
                if (h.item == b.item) { h.qty += b.qty; merged = true; break; }
            }
            if (!merged) holdings.push_back(b);
        }
        if (market::ChooseSellOffer(*me, holdings, state_.prices, tradePolicy_,
                                    &want)) {
            const i32 inPack = market::QtyOf(obs.pack, want.item);
            const i32 inBank = market::QtyOf(obs.bank, want.item);
            if (inPack < want.qty && inBank > 0) {
                const std::vector<u16> gfx = econ::GraphicsForItem(want.item.c_str());
                const u32 serial = client.FindContainerItemByGraphic(
                    client.BankContainer(), gfx.data(), gfx.size());
                if (serial) {
                    const i32 take = std::min(inBank, want.qty - inPack);
                    LogLine("trade: withdrawing %d %s from the bank to sell",
                            take, want.item.c_str());
                    client.TakeFromContainer(serial, static_cast<u16>(take));
                    nextActionMs_ = obs.nowMs + 2000;
                    return false;
                }
            }
        }
    }

    // --- nothing heard: stand where players are and announce ----------------
    market::TradeIntent offer;
    if (!market::ChooseSellOffer(*me, obs.pack, state_.prices, tradePolicy_,
                                 &offer)) {
        // Nothing worth announcing -- most often because this character has
        // never seen a price for what it carries and refuses to invent one.
        // SAME DEAD END, SAME COOLDOWN. Returning plain success here let the
        // need score identically on the very next tick and the goal was
        // re-picked sixteen times a second -- a lumberjack logged
        // goal=TRADE_WITH_PLAYER eight times in half a second and did nothing
        // else all session. An errand that cannot even be started is the
        // market being unavailable, not a goal that succeeded.
        LogLine("trade: nothing to announce (no observed price for what is spare)");
        marketQuietUntilMs_ = obs.nowMs + kMarketQuietMs;
        return true;
    }
    tradeOffer_ = offer;

    // A market needs a marketplace. The bank is where players actually stand,
    // and going there is also what makes the fleet visibly congregate instead
    // of shouting into an empty forest.
    if (!obs.atBank && client.BankContainer() == 0) {
        if (client.TravelBusy()) return false;
        if (!travelInFlight_) {
            LogLine("trade: taking %d %s to the %s market",
                    offer.qty, offer.item.c_str(),
                    state_.homeCity.empty() ? "nearest" : state_.homeCity.c_str());
            travelInFlight_ =
                client.TravelToService(wm::Service::Banker, state_.homeCity.c_str());
            if (!travelInFlight_) {
                LogLine("goal_blocked=TRADE_WITH_PLAYER reason=\"%s\"",
                        client.TravelFailureText());
                planner_.NoteAttempt(obs.nowMs);
            }
            nextActionMs_ = obs.nowMs + 2000;
            return false;
        }
        travelInFlight_ = false;
        return false;
    }

    if (obs.nowMs - tradeAnnouncedMs_ >= kAnnounceIntervalMs) {
        const std::string line = market::FormatSellOffer(offer);
        LogLine("trade: announcing '%s'", line.c_str());
        client.ActionSay(line.c_str());
        tradeAnnouncedMs_ = obs.nowMs;
        ++tradeAnnounceCount_;
    }
    if (tradeAnnounceCount_ >= kMaxAnnounces) {
        LogLine("trade: nobody answered %d offers of %s -- back to work",
                tradeAnnounceCount_, offer.item.c_str());
        state_.memory.NoteEvent("no_player_buyer", offer.item.c_str(), "",
                                obs.x, obs.y, obs.nowMs);
        tradeAnnounceCount_ = 0;
        // AND STOP SCHEDULING IT for a while. Finishing the goal was not
        // enough: the need scored the same on the very next tick, the errand
        // was re-picked, and a lumberjack spent whole sessions announcing logs
        // to an empty Yew while its own training and hunting needs -- which it
        // could actually have finished -- sat underneath it.
        marketQuietUntilMs_ = obs.nowMs + kMarketQuietMs;
        planner_.Finish(false, "nobody wanted it", obs.nowMs);
        return false;
    }
    nextActionMs_ = obs.nowMs + 2000;
    return false;
}

// Put the goods (or the gold) in the window, then accept. Kept separate
// because it is the half that runs on BOTH sides of the same deal.
bool Runner::DriveOpenTrade(Client& client, const Observation& obs) {
    const trade::TradeState& tr = client.Trade();

    if (!tradeOffered_) {
        if (tradeSellingQty_ > 0) {
            const std::vector<u16> gfx = econ::GraphicsForItem(tradeItem_.c_str());
            for (u16 g : gfx) {
                const u32 serial = client.FindBackpackItemByGraphic(g);
                if (!serial) continue;
                client.ActionTradeOffer(serial,
                                        static_cast<u16>(tradeSellingQty_));
                break;
            }
        } else {
            const i32 owed = tradeWantQty_ * tradeOfferPrice_;
            const u32 gold = client.FindBackpackItemByGraphic(kGoldCoin);
            if (gold && owed > 0) {
                client.ActionTradeOffer(gold, static_cast<u16>(owed));
                LogLine("trade: offering %d gold for %d %s", owed,
                        tradeWantQty_, tradeItem_.c_str());
            }
        }
        tradeOffered_ = true;
        tradeOpenedMs_ = obs.nowMs;
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }

    // Accept once the partner has put something in. Accepting an EMPTY window
    // is how a character gives its goods away for nothing.
    if (tr.CurrentPhase() == trade::Phase::Open && !tr.TheirOffer().empty() &&
        !tr.CheckSent()) {
        LogLine("trade: partner offered %zu line(s); accepting",
                tr.TheirOffer().size());
        client.ActionTradeAccept(true);
        nextActionMs_ = obs.nowMs + 1500;
        return false;
    }

    if (obs.nowMs - tradeOpenedMs_ > kTradeGiveUpMs) {
        LogLine("trade: %s put nothing in after %llds -- cancelling",
                tradePartnerName_.c_str(),
                static_cast<long long>(kTradeGiveUpMs / 1000));
        client.ActionTradeCancel();
        state_.memory.NoteEvent("trade_timeout", tradeItem_.c_str(),
                                tradePartnerName_.c_str(), obs.x, obs.y,
                                obs.nowMs);
        ResetTradeState();
        return false;
    }
    nextActionMs_ = obs.nowMs + 1000;
    return false;
}

void Runner::ResetTradeState() {
    tradePartner_ = 0;
    tradePartnerName_.clear();
    tradeItem_.clear();
    tradeOffer_ = market::TradeIntent{};
    tradeSellingQty_ = 0;
    tradeWantQty_ = 0;
    tradeOfferPrice_ = 0;
    tradeOffered_ = false;
    tradePackBefore_ = 0;
    tradeGoldBefore_ = 0;
    tradeAnnounceCount_ = 0;
    travelInFlight_ = false;
}

// ---------------------------------------------------------------------------
// FISH -- the one gold faucet a character can reach on day one.
//
// Every number here is the shard's own, read out of
// runtime/scripts/skills/skill18_fishing.scp rather than assumed:
//
//   DELAY=8.0     one cast takes eight seconds
//   RANGE=4       water four tiles away is reachable
//   FLAGS=skf_gather
//   @PreStart     refuses outright while mounted
//
// The answers Sphere gives are equally its own, and they are the only honest
// signal that a cast resolved:
//
//   "You fish a while, but fail to catch anything."
//   "You pull your line back in and stop fishing."
//
// The same lesson the axe taught applies: a flat sleep after every cast spends
// most of the loop waiting for an answer the server already gave, so the eight
// seconds is a CEILING and the goal polls for a verdict.
// ---------------------------------------------------------------------------
// Every kind of fish the sea yields, counted as one catch. The graphic table
// listed only i_fish_big_1 for a long while, so a character could pull a fish
// out of the water and its own pack counter would report nothing -- the catch
// was real, the blindness was ours (VendorPolicy.cpp kGraphics).
static i32 FishInPack(const std::vector<market::Stock>& pack) {
    static const char* kKinds[] = {"i_fish_big_1", "i_fish_big_2",
                                   "i_fish_big_3", "i_fish_big_4",
                                   "i_fish_small"};
    i32 n = 0;
    for (const char* k : kKinds) n += market::QtyOf(pack, k);
    return n;
}

// ---------------------------------------------------------------------------
// CRAFTING -- the half of the economy a gatherer never needed.
//
// A crafter's day is two errands. BUY_SUPPLIES fetches what it cannot make;
// CRAFT makes what it can sell. They are separate goals because they fail
// differently, and "I cannot craft" when the truth is "nobody has sold me
// nightshade yet" is a bot lying about its own state.
// ---------------------------------------------------------------------------

// Who sells a craft input, as the paperdoll names them. Read off this shard's
// own vendor templates, never guessed: the mage shop carries both halves of
// the Inscription chain -- SELL=i_scroll_blank,{10 15} and every Magery
// reagent (templates/tm_vend.scp:633-656) -- which is why a scribe's whole
// shopping trip is one stop.
const char* SupplierTradeFor(const std::string& item) {
    if (item.rfind("i_reag_", 0) == 0) return "mage";
    if (item == "i_scroll_blank")      return "mage";
    if (item == "i_bottle_empty")      return "alchemist";
    if (item == "i_feather")           return "provisioner";
    return nullptr;
}

// How to reach one output through the shard's legacy craft menus. Two levels
// at most, and both strings are matched as case-insensitive substrings.
//
// Inscription is nested -- the blank scroll opens "Spell Circles" and the
// spell lives one level down (sm_legacy_inscription.scp:12-31, 93-118).
// Bowcraft is flat, its options named "<name> (<resmake>)"
// (sm_legacy_bowcraft.scp:13-33). Nothing here is inferred from generic UO.
struct CraftMenuPath {
    const char* item;
    const char* step1;
    const char* step2;   // nullptr for a flat menu
};
const CraftMenuPath kCraftMenus[] = {
    {"i_scroll_poison",      "Spell Circle 3", "poison"},
    {"i_scroll_recall",      "Spell Circle 4", "recall"},
    {"i_bow",                "bow",            nullptr},
    {"i_crossbow",           "crossbow",       nullptr},
    {"i_arrow_shaft",        "arrow_shaft",    nullptr},
};

const CraftMenuPath* CraftMenuFor(const std::string& item) {
    for (const CraftMenuPath& m : kCraftMenus) {
        if (item == m.item) return &m;
    }
    return nullptr;
}

bool Runner::DoBuySupplies(Client& client, const Observation& obs) {
    const prof::Profession* me = needCfg_.profession;
    if (!me) return true;

    // SETTLE THE PREVIOUS ASK FIRST, from the gold the server actually took.
    // The ledger is the economy's own books; it must record purchases that
    // HAPPENED. Noting the flow at request time counted one on every
    // superseded retry against a vendor that had walked out of reach.
    if (!pendingBuyItem_.empty() && !client.ActionBusy()) {
        const i32 spent = pendingBuyGoldBefore_ - obs.gold;
        if (spent > 0) {
            state_.ledger.Note(market::GoldFlow::DestroyedVendorPurchase, spent,
                               pendingBuyItem_.c_str(), obs.nowMs);
            LogLine("supplies: the server took %d gold for %s (purse %d -> %d)",
                    spent, pendingBuyItem_.c_str(), pendingBuyGoldBefore_,
                    obs.gold);
            planner_.NoteProgress();   // THIS is progress: goods changed hands
        } else {
            LogLine("supplies: asked to buy %s and the purse did not move -- "
                    "nothing was bought", pendingBuyItem_.c_str());
        }
        pendingBuyItem_.clear();
        pendingBuyGoldBefore_ = 0;
    }

    const CraftIntent intent = ChooseCraft(*me, obs, needCfg_.craftBatch);
    if (!intent.item || intent.missing.empty()) {
        LogLine("supplies: nothing short after all");
        supplyItem_.clear();
        return true;
    }

    const prod::Ingredient want = intent.missing.front();
    if (supplyItem_ != want.item) {
        supplyItem_ = want.item;
        supplyTrips_ = 0;
        const char* trade = SupplierTradeFor(supplyItem_);
        supplyTrade_ = trade ? trade : "";
    }

    if (supplyTrade_.empty()) {
        LogLine("goal_failed=BUY_SUPPLIES reason=\"%s\" item=%s",
                faucet::RefusalName(faucet::Refusal::NoKnownBuyer),
                supplyItem_.c_str());
        planner_.Finish(false, "no trade known to sell it", obs.nowMs);
        return false;
    }

    // THE POLICY DECIDES, not the shop. An NPC that technically stocks a thing
    // is not thereby a legitimate source for it -- that is the whole point of
    // the vendor matrix, and buying a player-market good from a vendor would
    // cut a real player out of the economy this project exists to simulate.
    const econ::VendorRuling ruling = econ::CanUseNPCVendorFor(supplyItem_.c_str());
    if (!ruling.allowed) {
        LogLine("goal_failed=BUY_SUPPLIES reason=\"%s\" item=%s class=%s (%s)",
                faucet::RefusalName(faucet::Refusal::RevolutionAuthenticityUnknown),
                supplyItem_.c_str(), econ::VendorClassName(ruling.klass),
                ruling.reason ? ruling.reason : "");
        state_.memory.NoteEvent("policy_refused", supplyItem_.c_str(),
                                econ::VendorClassName(ruling.klass), obs.x,
                                obs.y, obs.nowMs);
        planner_.Finish(false, "the vendor policy refuses this input", obs.nowMs);
        return false;
    }

    if (client.TravelBusy()) return false;

    const u32 vendor = client.VendorOfferFrom();
    if (vendor == 0) {
        const u32 keeper = client.NearestMobileWithTrade(supplyTrade_.c_str());
        if (keeper) {
            i32 vx = 0, vy = 0; i8 vz = 0;
            if (client.MobilePosition(keeper, &vx, &vy, &vz)) {
                const i32 d = TileDist(obs.x, obs.y, vx, vy);
                const i32 dz = (obs.z > vz) ? (obs.z - vz) : (vz - obs.z);
                if (d > 1 || dz > 3) {
                    travelInFlight_ = client.TravelToEntity(keeper, 1);
                    nextActionMs_ = obs.nowMs + 2000;
                    return false;
                }
            }
            client.ActionVendorOpen(keeper);
            nextActionMs_ = obs.nowMs + 2500;
            return false;
        }
        if (!travelInFlight_) {
            if (++supplyTrips_ > kMaxSupplyTrips) {
                LogLine("goal_failed=BUY_SUPPLIES reason=\"%s\" no '%s' found "
                        "after %d trips",
                        faucet::RefusalName(faucet::Refusal::VendorUnreachable),
                        supplyTrade_.c_str(), supplyTrips_);
                planner_.Finish(false, "no supplier reachable", obs.nowMs);
                supplyTrips_ = 0;
                nextActionMs_ = obs.nowMs + 30000;
                return false;
            }
            LogLine("supplies: looking for a '%s' to sell %d %s (trip %d)",
                    supplyTrade_.c_str(), want.qty, supplyItem_.c_str(),
                    supplyTrips_);
            travelInFlight_ = client.TravelToService(
                ServiceForTrade(supplyTrade_.c_str()), state_.homeCity.c_str());
            if (!travelInFlight_) {
                LogLine("goal_blocked=BUY_SUPPLIES reason=\"%s\" (%s)",
                        faucet::RefusalName(faucet::Refusal::VendorUnreachable),
                        client.TravelFailureText());
                planner_.NoteAttempt(obs.nowMs);
            }
            nextActionMs_ = obs.nowMs + 2000;
            return false;
        }
        travelInFlight_ = false;
        LogLine("supplies: arrived at %d,%d -- asking who is here",
                client.PlayerX(), client.PlayerY());
        client.ActionScanMobiles();
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }

    // AN OPEN SHOP WINDOW IS NOT A VENDOR STILL IN REACH.
    //
    // The approach check above only runs while no offer is open, so once the
    // window came up the character stopped watching where the shopkeeper went
    // -- and Britain's shopkeepers walk. In run_m5/p0gate1 Ysolde bought once,
    // Nightshade wandered off, and every buy after that was answered "You
    // can't reach the Vendor" while the goal re-issued it every 2.5 seconds
    // for the rest of the session. Re-measure before each purchase and walk
    // back if the shop has moved.
    {
        i32 vx = 0, vy = 0; i8 vz = 0;
        if (client.MobilePosition(vendor, &vx, &vy, &vz)) {
            const i32 d = TileDist(obs.x, obs.y, vx, vy);
            const i32 dz = (obs.z > vz) ? (obs.z - vz) : (vz - obs.z);
            if (d > 1 || dz > 3) {
                LogLine("supplies: the '%s' has moved to %d,%d (%d tiles) -- "
                        "walking back before buying",
                        supplyTrade_.c_str(), vx, vy, d);
                travelInFlight_ = client.TravelToEntity(vendor, 1);
                planner_.NoteAttempt(obs.nowMs);
                nextActionMs_ = obs.nowMs + 2000;
                return false;
            }
        }
    }

    // ONE BUY IN FLIGHT AT A TIME. kVendorTimeoutMs is 8 s and this used to
    // re-issue every 2.5 s, so each attempt was superseded before it could
    // resolve -- the identical defect the bank ask had.
    if (client.ActionBusy()) return false;

    // A shop window is open: find the input in it and buy the shortfall.
    const std::vector<u16> gfx = econ::GraphicsForItem(supplyItem_.c_str());
    for (const Client::VendorItem& v : client.VendorOffer()) {
        bool match = false;
        for (u16 g : gfx) { if (v.graphic == g) { match = true; break; } }
        if (!match) continue;

        const i32 unit = static_cast<i32>(v.price);
        i32 take = want.qty;
        // WORKING CAPITAL IS NOT THE DEATH RESERVE.
        //
        // goldReserve is what a life keeps back to replace a tool after it
        // dies. Charging the working stock against it deadlocked a scribe
        // outright: blank scrolls cost 3 gold, the purse held 781, the
        // reserve was 900, and the character stood in the mage shop
        // refusing to buy three scrolls -- every fifteen seconds, for the
        // whole session. A reserve that forbids the only activity which
        // refills it is not caution, it is a trap.
        //
        // So inputs are bought out of working capital: everything above a
        // small hard floor, and never more than a quarter of the purse in one
        // trip, so a bad price cannot empty a character either.
        constexpr i32 kHardFloor = 100;      // never end a trip broke
        const i32 above = obs.gold - kHardFloor;
        const i32 spendable = (above < obs.gold / 4) ? above : obs.gold / 4;
        if (unit > 0 && take * unit > spendable) take = spendable / unit;
        if (take <= 0) {
            LogLine("goal_blocked=BUY_SUPPLIES reason=\"%s\" %s costs %d each, "
                    "purse %d, spendable %d",
                    faucet::RefusalName(faucet::Refusal::EconomicRouteBlocked),
                    supplyItem_.c_str(), unit, obs.gold, spendable);
            planner_.NoteAttempt(obs.nowMs);
            nextActionMs_ = obs.nowMs + 15000;
            return false;
        }

        market::PriceObservation po;
        po.item = supplyItem_;
        po.pricePerUnit = unit;
        po.source = market::PriceSource::NpcVendorSells;
        po.who = v.name;
        po.x = obs.x; po.y = obs.y; po.whenMs = obs.nowMs;
        state_.prices.Note(po);

        LogLine("supplies: buying %d %s at %d each from '%s'", take,
                supplyItem_.c_str(), unit, v.name.c_str());
        client.ActionVendorBuy(vendor, v.serial, static_cast<u16>(take));
        // THE LEDGER RECORDS WHAT THE SERVER DID, not what we asked for.
        // Noting the flow here counted a purchase on every one of those
        // superseded retries, so the economy's own books recorded gold
        // destroyed that never left the purse. What is remembered instead is
        // the ASK; the flow is noted on the next tick, from the gold the
        // server actually took (see `pendingBuy_` below).
        pendingBuyItem_ = supplyItem_;
        pendingBuyGoldBefore_ = obs.gold;
        // BUYING IS AN ATTEMPT, NOT PROGRESS. NoteProgress() cleared the
        // failure ladder on every retry, so a goal that bought nothing for
        // twenty minutes never ran out of attempts.
        planner_.NoteAttempt(obs.nowMs);
        supplyItem_.clear();
        // Longer than kVendorTimeoutMs (8 s, Client.cpp): an ask re-issued
        // inside its own deadline supersedes itself and never resolves.
        nextActionMs_ = obs.nowMs + 9000;
        return false;
    }

    LogLine("goal_failed=BUY_SUPPLIES reason=\"%s\" this '%s' does not stock %s",
            faucet::RefusalName(faucet::Refusal::VendorNotObserved),
            supplyTrade_.c_str(), supplyItem_.c_str());
    state_.memory.NoteEvent("vendor_lacks", supplyItem_.c_str(),
                            supplyTrade_.c_str(), obs.x, obs.y, obs.nowMs);
    planner_.Finish(false, "this vendor does not stock it", obs.nowMs);
    return false;
}

bool Runner::DoCraft(Client& client, const Observation& obs) {
    const prof::Profession* me = needCfg_.profession;
    if (!me) return true;

    const CraftIntent intent = ChooseCraft(*me, obs, 1);
    if (!intent.item) {
        LogLine("craft: nothing this life can make and sell (%s)", intent.why);
        return true;
    }
    if (!intent.missing.empty()) {
        LogLine("goal_blocked=CRAFT reason=\"%s\" %s short of %d x %s",
                faucet::RefusalName(faucet::Refusal::RequiredForProduction),
                intent.item, intent.missing.front().qty,
                intent.missing.front().item);
        planner_.Finish(false, "inputs are short", obs.nowMs);
        return false;
    }

    if (craftItem_ != intent.item) {
        craftItem_ = intent.item;
        craftHadBefore_ = market::QtyOf(obs.pack, craftItem_);
        craftMade_ = 0;
        craftMenuStep_ = 0;
    }

    // Did the last attempt land? The pack is the only honest witness -- Sphere
    // answers a failed inscription with "the scroll is ruined", which consumes
    // the input and produces nothing, so counting attempts would count wrong.
    const i32 now = market::QtyOf(obs.pack, craftItem_);
    if (now > craftHadBefore_) {
        craftMade_ += now - craftHadBefore_;
        craftHadBefore_ = now;
        craftMenuStep_ = 0;
        planner_.NoteProgress();
        LogLine("craft: made one %s (%d this sitting)", craftItem_.c_str(),
                craftMade_);
        if (!state_.memory.HasEvent("first_craft")) {
            state_.memory.NoteEvent("first_craft", craftItem_.c_str(), "",
                                    obs.x, obs.y, obs.nowMs);
        }
        if (craftMade_ >= needCfg_.craftBatch) {
            LogLine("craft: %d %s made -- enough for a trip to a buyer",
                    craftMade_, craftItem_.c_str());
            craftItem_.clear();
            return true;
        }
    }

    const CraftMenuPath* path = CraftMenuFor(craftItem_);
    if (!path) {
        LogLine("goal_failed=CRAFT reason=\"%s\" no menu path known for %s",
                faucet::RefusalName(faucet::Refusal::MissingRecipe),
                craftItem_.c_str());
        planner_.Finish(false, "no craft menu path known", obs.nowMs);
        return false;
    }

    if (client.ActionBusy()) return false;

    // --- walk the menu -----------------------------------------------------
    if (client.CraftMenuOpen()) {
        // READ THE MENU, DO NOT COUNT STEPS.
        //
        // The first version tracked which level it thought it was on. One
        // failed answer reset that counter to zero while the SUBMENU was
        // still open, so it then hunted for "Spell Circle 3" inside the list
        // that was already offering "poison" -- and said "the craft menu does
        // not offer it" sixteen times a second while printing the very option
        // it wanted. The menu itself says which level it is; ask it.
        const char* want = nullptr;
        if (path->step2 && client.DialogHasOption(path->step2)) {
            want = path->step2;          // already in the submenu
        } else if (client.DialogHasOption(path->step1)) {
            want = path->step1;          // the top menu, or a flat one
        }
        if (!want) {
            LogLine("goal_failed=CRAFT reason=\"%s\" this menu offers neither "
                    "'%s' nor '%s'",
                    faucet::RefusalName(faucet::Refusal::MissingRecipe),
                    path->step1, path->step2 ? path->step2 : "(flat menu)");
            for (const std::string& o : client.CraftableNow()) {
                LogLine("craft:   offered: %s", o.c_str());
            }
            planner_.Finish(false, "the craft menu does not offer it", obs.nowMs);
            nextActionMs_ = obs.nowMs + 5000;
            return false;
        }
        if (!client.ChooseDialogByName(want)) {
            // DialogHasOption just said it was there, so this is a send
            // failure rather than a missing option. Let it settle and re-read.
            nextActionMs_ = obs.nowMs + 1500;
            return false;
        }
        LogLine("craft: chose '%s'", want);
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }

    // --- open it -----------------------------------------------------------
    //
    // The menu opens by USING the thing the craft is made from -- a blank
    // scroll for Inscription, a log for Bowcraft. The recipe's own first
    // input is that thing, so nothing here has to be hardcoded per trade.
    const prod::Recipe* r = prod::FindRecipe(craftItem_.c_str());
    if (!r || !r->inputs[0].item) {
        LogLine("goal_failed=CRAFT reason=\"%s\" %s has no recipe",
                faucet::RefusalName(faucet::Refusal::MissingRecipe),
                craftItem_.c_str());
        planner_.Finish(false, "no recipe", obs.nowMs);
        return false;
    }
    const std::vector<u16> openGfx = econ::GraphicsForItem(r->inputs[0].item);
    u32 opener = 0;
    for (u16 g : openGfx) {
        opener = client.FindBackpackItemByGraphic(g);
        if (opener) break;
    }
    if (!opener) {
        LogLine("goal_blocked=CRAFT reason=\"%s\" nothing in the pack to open "
                "the %s menu with (%s)",
                faucet::RefusalName(faucet::Refusal::MissingTool),
                craftItem_.c_str(), r->inputs[0].item);
        planner_.Finish(false, "no material to start from", obs.nowMs);
        return false;
    }

    LogLine("craft: making %s -- using a %s to open the menu",
            craftItem_.c_str(), r->inputs[0].item);
    client.ActionUseObject(opener);
    craftStartedMs_ = obs.nowMs;
    craftMenuStep_ = 0;
    nextActionMs_ = obs.nowMs + 2000;
    return false;
}

bool Runner::DoFish(Client& client, const Observation& obs) {
    const prof::Profession* me = needCfg_.profession;
    if (!me) return true;

    if (obs.WeightFraction() >= 0.95) {
        LogLine("fish: pack full at %.0f%%", obs.WeightFraction() * 100.0);
        return true;
    }

    // The pole has to be IN HAND, the same way the axe does: skf_gather reads
    // the character's weapon, and a pole in the pack is not a pole in hand.
    // TAKE THE SERIAL FROM THE LAYER IT WAS FOUND ON.
    //
    // The first version noticed the pole was equipped and then asked the PACK
    // for its serial -- which is 0, because it is not in the pack -- and fell
    // back to hand1 regardless of which hand held it. i_fishing_pole is
    // TWOHANDS=Y so the kit arms it automatically, and the result was a fisher
    // standing beside water reporting REFUSE_MISSING_TOOL while holding one.
    //
    // That also produced a wrong conclusion on the way: the pack dump showed
    // no pole, and [NEWBIE FISHING] was very nearly recorded as BLOCKED_RUNTIME
    // when the kit had worked correctly all along.
    const std::vector<u16> poleGfx = econ::GraphicsForItem("i_fishing_pole");
    u32 pole = 0;
    for (u16 g : poleGfx) {
        if (client.EquippedGraphicAt(kLayerHand1) == g) {
            pole = client.EquippedAtLayer(kLayerHand1);
            break;
        }
        if (client.EquippedGraphicAt(kLayerHand2) == g) {
            pole = client.EquippedAtLayer(kLayerHand2);
            break;
        }
    }
    if (!pole) {
        for (u16 g : poleGfx) {
            const u32 inPack = client.FindBackpackItemByGraphic(g);
            if (!inPack) continue;
            LogLine("fish: arming the pole");
            client.ActionEquip(inPack, kLayerServerChooses);
            nextActionMs_ = obs.nowMs + 1500;
            return false;
        }
        // Say WHAT WAS THERE. "no fishing pole" is not a diagnosis when the
        // question is whether the kit delivered one, whether it is on a layer
        // this code does not read, or whether its graphic is not the one the
        // item table names.
        char seen[192];
        int n = 0;
        seen[0] = 0;
        const u32 pack = client.BackpackSerial();
        const usize count = client.ContainerItemCount(pack);
        for (usize i = 0; i < count && n < 160; ++i) {
            u32 sr = 0; u16 gfx = 0, amt = 0;
            if (!client.ContainerItemAt(pack, i, &sr, &gfx, &amt)) continue;
            n += std::snprintf(seen + n, sizeof(seen) - n, "%s0x%04X",
                               n ? "," : "", gfx);
        }
        LogLine("goal_failed=FISH reason=\"%s\" wanted=0x0DBF/0x0DC0 pack=[%s]",
                faucet::RefusalName(faucet::Refusal::MissingTool), seen);
        planner_.Finish(false, "no fishing pole", obs.nowMs);
        return false;
    }

    // --- has the last cast resolved? --------------------------------------
    // Gated on the target reply having been SENT: fishCastMs_ is also set
    // when the pole is double-clicked, and ungated this branch swallowed the
    // whole 9-second ceiling between cursor and reply doing nothing -- the
    // cast1 live run armed the cursor at :24.6 and answered it at :33.7.
    // The ceiling is NOT one stroke. DELAY=8.0 is per @Stroke, and a round
    // can run more than one before Sphere speaks: the cast3 live run held a
    // line at (1465,1751) for a full 9 seconds with no verdict at all, and
    // @Success (skill18_fishing.scp:37) packs the fish SILENTLY -- so the
    // only signs of a good round are the fish appearing in the pack or,
    // eventually, @Fail's sentence. Twenty-five seconds is three strokes of
    // headroom; on expiry the recast below aborts the stale round cleanly.
    const i64 kFishRoundCeilingMs = 25000;
    if (!fishCursorPending_ && fishCastMs_ != 0 &&
        obs.nowMs - fishCastMs_ < kFishRoundCeilingMs) {
        // Two kinds of answer. A cast that HELD and came up empty means the
        // water is good and the roll said mr_nothing (regionresources.scp:
        // RESOURCES=60.0 mr_nothing); cast there again. A REFUSAL is Sphere
        // saying this tile can never pay -- "There are no fish here." is
        // DEFMSG FISHING_2 (core/messages.scp:158) and came back instantly
        // when the cast1 run targeted near-shore water at (1466,1752) -- so
        // the tile goes on the dead list and the sweep moves on.
        // "You pull your line back in and stop fishing." is DELIBERATELY not
        // a resolution. It is @Abort (skill18_fishing.scp:43-44) -- the echo
        // of this character's own recast cancelling the previous round -- and
        // it arrives moments AFTER the new round's journal mark. Treating it
        // as a verdict resolved every new cast instantly, which recast again,
        // which aborted again: the cast3 run cancelled its own line twice a
        // second for a minute straight.
        static const char* kNoCatch[] = {
            "but fail to catch anything",       // @Fail: resolved, no fish
        };
        static const char* kRefusedHere[] = {
            "there are no fish here",           // no fish resource at the tile
            "try fishing elsewhere",            // FISHING_1, same verdict
            "try fishing in water",             // not water at all
            "can't fish from where you are standing",
            "you can't fish while riding",      // @PreStart refusal
            "cannot fish so close to yourself", // adjacent water is refused
            "target cannot be seen",
            "that is too far away",
        };
        bool done = false;
        for (const char* line : kNoCatch) {
            if (client.JournalSaidSince(line, fishCastJournalMs_)) {
                done = true;
                break;
            }
        }
        if (!done) {
            for (const char* line : kRefusedHere) {
                if (!client.JournalSaidSince(line, fishCastJournalMs_))
                    continue;
                LogLine("fish: refused at %d,%d (\"%s\") -- marking it dead",
                        fishX_, fishY_, line);
                deadTargets_.emplace_back(fishX_, fishY_);
                if (deadTargets_.size() > 32)
                    deadTargets_.erase(deadTargets_.begin());
                done = true;
                break;
            }
        }
        // ALL four fish, not just the first: the region rolls mr_fish1-4
        // (core/regionresources.scp:64-90) and each REAPs its own item, so a
        // counter watching only i_fish_big_1 misses three catches in four --
        // and @Success is silent, so the pack count is the only proof.
        const i32 caught = FishInPack(obs.pack);
        // ...and Sphere announces a catch OUT LOUD as well: "You pull out a
        // fish!" (hardcoded, not in skill18_fishing.scp). In the cast4 live
        // run that sentence arrived at :07:12 and the pack counter never
        // moved -- the round resolved only when the 25 s ceiling expired --
        // so the server's own receipt is trusted directly and the pack count
        // stays as a second witness.
        const bool saidCaught =
            client.JournalSaidSince("you pull out a fish", fishCastJournalMs_);
        if (caught > fishSeen_ || saidCaught) {
            LogLine("fish: caught one at %d,%d (%s)", fishX_, fishY_,
                    saidCaught ? "journal" : "pack count");
            fishSeen_ = caught;
            planner_.NoteProgress();
            state_.memory.NoteResource("fish", fishX_, fishY_, obs.z, true,
                                       obs.nowMs);
            if (!state_.memory.HasEvent("first_fish")) {
                state_.memory.NoteEvent("first_fish", "first fish caught",
                                        "water", obs.x, obs.y, obs.nowMs);
            }
            done = true;
        }
        if (!done) {
            nextActionMs_ = obs.nowMs + kFishPollMs;
            return false;
        }
        fishCastMs_ = 0;
    }

    // --- find water -------------------------------------------------------
    //
    // RANGE=4 is the shard's own number, so a spot further than that is not a
    // spot at all. Searching a wider radius and then walking is the difference
    // between fishing and standing hopefully near a lake.
    // GO TO A DOCK FIRST, and only then look for water.
    //
    // The order used to be the other way round, and it was the whole problem:
    // NearestFishingSpot searches 24 tiles and there is water within 24 tiles
    // of almost anywhere near Britain, so the dock trip only ran when that
    // search FAILED -- which it never did. The character chased the nearest
    // pond from wherever it stood, the route stopped short, it picked again
    // from the new position, and it drifted across the map without once
    // reaching a dock. There is no "Docks ARRIVED" line in any of those runs.
    if (!fishAtDock_) {
        if (client.TravelBusy()) return false;
        if (!travelInFlight_) {
            ++fishTrips_;
            if (fishTrips_ > kMaxFishTrips) {
                LogLine("goal_failed=FISH reason=\"%s\" no dock reachable "
                        "after %d trips",
                        faucet::RefusalName(faucet::Refusal::EconomicRouteBlocked),
                        fishTrips_);
                planner_.Finish(false, "no dock reachable", obs.nowMs);
                fishTrips_ = 0;
                return false;
            }
            const KnownResourceSource* known =
                state_.memory.BestProvenResource("fish", obs.x, obs.y, obs.nowMs);
            if (known) {
                LogLine("fish: back to a shore that has paid out at %d,%d",
                        known->x, known->y);
                travelInFlight_ =
                    client.TravelToPoint(known->x, known->y, 4, "fishing spot");
            } else {
                LogLine("fish: going to a dock (trip %d)", fishTrips_);
                travelInFlight_ =
                    client.TravelToResource(wm::ResourceKind::Fishing);
            }
            if (!travelInFlight_) {
                LogLine("goal_blocked=FISH reason=\"%s\"",
                        client.TravelFailureText());
                planner_.NoteAttempt(obs.nowMs);
            }
            nextActionMs_ = obs.nowMs + 2000;
            return false;
        }
        travelInFlight_ = false;
        // ARRIVAL IS A CLAIM ABOUT THE TILE, and the proof is water nearby.
        Client::FishingSpot probe;
        if (!client.NearestFishingSpot(client.PlayerX(), client.PlayerY(), 12,
                                       &probe)) {
            LogLine("fish: trip reported %s but there is no shore within 12 "
                    "tiles of %d,%d",
                    client.TravelSucceeded() ? "success" : "failure",
                    client.PlayerX(), client.PlayerY());
            deadTargets_.emplace_back(client.PlayerX(), client.PlayerY());
            if (deadTargets_.size() > 32) deadTargets_.erase(deadTargets_.begin());
            planner_.NoteAttempt(obs.nowMs);
            nextActionMs_ = obs.nowMs + 2000;
            return false;
        }
        LogLine("fish: at the water's edge (%d,%d), shore %d,%d",
                client.PlayerX(), client.PlayerY(), probe.standX, probe.standY);
        fishAtDock_ = true;
        fishTrips_ = 0;
        state_.memory.NoteResource("fish", client.PlayerX(), client.PlayerY(),
                                   client.PlayerZ(), true, obs.nowMs);
        return false;
    }

    // AT A DOCK: cast at whatever water is already in range.
    //
    // Every previous version picked a "stand tile" and then tried to walk to
    // it, and every one of them failed differently -- and the post-mortem
    // found ONE cause under all of them: the client could not see the water
    // it was standing next to. Around Britain's docks the sea is wet STATICS
    // over impassable dry-by-tiledata land (see WaterHit in Client.h), so the
    // land-only water search reported "no water 2-4 tiles" while castable
    // water sat 2 tiles away, and every walk was aimed at either a wet tile
    // (an unwalkable A* goal by definition) or a "dry" tile under a water
    // static (walkable:false -- "goal not walkable"). With both water forms
    // visible, standing at the edge is usually already enough.
    //
    // 2 to 4 tiles: RANGE=4 is the maximum from skill18_fishing.scp, and the
    // minimum is the server's own "You cannot fish so close to yourself."

    // A shore hop is in flight. Its deadline is checked BEFORE the
    // GotoBusy early-return -- checked after, a hung walk means the ceiling
    // can never fire.
    if (fishTargetSet_) {
        if (obs.nowMs > fishWalkMs_) {
            LogLine("fish: shore hop to %d,%d timed out at %d,%d",
                    fishTargetX_, fishTargetY_, obs.x, obs.y);
            fishTargetSet_ = false;
            deadTargets_.emplace_back(fishTargetX_, fishTargetY_);
            if (deadTargets_.size() > 32) deadTargets_.erase(deadTargets_.begin());
            fishAtDock_ = false;   // re-approach through the travel path
            planner_.NoteAttempt(obs.nowMs);
            return false;
        }
        if (client.GotoBusy()) return false;
        // The walk resolved -- arrived, or A* stopped as close as it could.
        // Either way the next tick asks the only question arrival ever
        // poses, "is there castable water from HERE", against a FRESH
        // observation; scanning this tick's snapshot after a walk is the
        // stale-position bug the chop goal already paid for. The tile is
        // marked tried first so a hop that resolved somewhere useless is
        // never picked again.
        LogLine("fish: shore hop done at %d,%d (wanted %d,%d)",
                client.PlayerX(), client.PlayerY(), fishTargetX_, fishTargetY_);
        deadTargets_.emplace_back(fishTargetX_, fishTargetY_);
        if (deadTargets_.size() > 32) deadTargets_.erase(deadTargets_.begin());
        fishTargetSet_ = false;
        nextActionMs_ = obs.nowMs + 300;
        return false;
    }
    // When Sphere has already refused SEVERAL waters around this stand, the
    // whole near band is fishless and probing the rest of the ring one cast
    // at a time is just slower agreement. Refusals are structural, not luck:
    // an empty roll answers "you fish a while, but fail to catch anything"
    // (RESOURCES=60.0 mr_nothing, core/regionresources.scp:93), while "There
    // are no fish here." is about the TILE. Three of those within casting
    // range is enough evidence to move along the shore instead. Only refused
    // WATER counts -- the dead list also holds failed stand tiles, and those
    // say nothing about fish.
    // EXACT-tile matching for cast bookkeeping. IsDeadTarget deliberately
    // matches within EIGHT tiles (Runner.cpp:766) because it judges travel
    // destinations -- "this clearing is treeless" -- and that radius is
    // right for those. Applied to per-tile casts it is catastrophic: one
    // "There are no fish here." at (1466,1752) blackened the whole dock,
    // and the very next scan reported "no water 2-4 tiles ... and no shore
    // to hop to" from a stand with live water three tiles away (cast2 run).
    auto deadExact = [&](i32 x, i32 y) -> bool {
        for (const auto& d : deadTargets_)
            if (d.first == x && d.second == y) return true;
        return false;
    };

    int refusedNear = 0;
    for (const auto& d : deadTargets_) {
        if (std::max(std::abs(d.first - obs.x), std::abs(d.second - obs.y)) > 4)
            continue;
        Client::WaterHit dw;
        if (client.NearestWater(d.first, d.second, 0, &dw)) ++refusedNear;
    }

    Client::WaterHit water;
    bool haveTarget = false;
    for (int r = 2; r <= 4 && !haveTarget && refusedNear < 3; ++r) {
        for (i32 dy = -r; dy <= r && !haveTarget; ++dy) {
            for (i32 dx = -r; dx <= r && !haveTarget; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                Client::WaterHit w;
                if (!client.NearestWater(obs.x + dx, obs.y + dy, 0, &w)) continue;
                if (deadExact(w.x, w.y)) continue;
                water = w;
                haveTarget = true;
            }
        }
    }
    if (!haveTarget) {
        // No castable water from where we stand. That makes this a SHORT
        // WALK problem, not a search problem: NearestFishingSpot now vets
        // its stand tile with the pathfinder's own walkability query, so a
        // spot it returns is a goal ActionGoto's A* will accept -- the
        // missing property that killed every earlier walk. Commit to the
        // tile ONCE: re-picking from the current position every tick is
        // what made attempt 2's target drift from 9 tiles out to 20.
        // The dead list rides along so refused water is not re-nominated:
        // without it the sweep down the pier proposes the same "no fish
        // here" tiles from every new stand.
        Client::FishingSpot spot;
        if (!client.GotoBusy() &&
            client.NearestFishingSpot(client.PlayerX(), client.PlayerY(), 12,
                                      &spot, &deadTargets_) &&
            !deadExact(spot.standX, spot.standY) &&
            !(spot.standX == client.PlayerX() &&
              spot.standY == client.PlayerY())) {
            LogLine("fish: shore hop %d,%d -> %d,%d (water %d,%d)",
                    client.PlayerX(), client.PlayerY(),
                    spot.standX, spot.standY, spot.waterX, spot.waterY);
            client.ActionGoto(spot.standX, spot.standY);
            fishTargetX_ = spot.standX;
            fishTargetY_ = spot.standY;
            fishTargetSet_ = true;
            // A ceiling, not a wait: the hop is at most 12 tiles of route,
            // and 15 s is roomy even at a walk. Timed-out hops are marked
            // dead above, so this cannot retry the same tile forever, and
            // the planner's own attempt cap bounds the whole goal.
            fishWalkMs_ = obs.nowMs + 15000;
            nextActionMs_ = obs.nowMs + 500;
            return false;
        }
        // No walkable shore to hop to either: this place is a dud.
        LogLine("fish: no water 2-4 tiles from %d,%d and no shore to hop to "
                "-- finding another dock", obs.x, obs.y);
        fishAtDock_ = false;
        deadTargets_.emplace_back(obs.x, obs.y);
        if (deadTargets_.size() > 32) deadTargets_.erase(deadTargets_.begin());
        planner_.NoteAttempt(obs.nowMs);
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }

    fishTrips_ = 0;

    // --- cast --------------------------------------------------------------
    if (client.ActionBusy()) return false;

    if (fishCursorPending_) {
        if (client.TargetActive()) {
            // Answer the cursor the way a classic client answers a click on
            // the water the player SEES. Coastline water is a static and a
            // static reply carries its graphic; open sea is wet land and
            // gets a ground reply. Sphere types both as fishable t_water
            // (items/i_ground_tiles.scp:733 [ITEMDEF 01796] TYPE=T_WATER,
            // DUPELIST through 017b2), so the difference is fidelity, not
            // permission -- and water.z is the surface actually targeted:
            // the static's own z (-5 at the Britain dock), not the land
            // buried under it (-15).
            if (water.graphic != 0) {
                client.ActionTargetStatic(water.x, water.y, water.z,
                                          water.graphic);
            } else {
                client.ActionTargetGround(water.x, water.y, water.z);
            }
            fishCursorPending_ = false;
            fishCastMs_ = obs.nowMs;
            fishCastJournalMs_ = client.JournalNowMs();
            fishX_ = water.x; fishY_ = water.y;
            fishSeen_ = FishInPack(obs.pack);
            nextActionMs_ = obs.nowMs + kFishPollMs;
            return false;
        }
        if (obs.nowMs - fishCastMs_ > 6000) {
            fishCursorPending_ = false;
            planner_.NoteAttempt(obs.nowMs);
        }
        return false;
    }

    client.ActionUseObject(pole);
    fishCursorPending_ = true;
    fishCastMs_ = obs.nowMs;
    nextActionMs_ = obs.nowMs + 800;
    return false;
}

bool Runner::DoTravel(Client& client, const Observation& obs) {
    if (obs.atWorkSite || obs.atBank) return true;
    // WHERE THIS LIFE'S WORK IS. This body walked every character toward a
    // forest -- a miner, a mage and a fisher included -- because the need it
    // answers had no profession gate and the destination was Lumber.
    const std::string gathers =
        needCfg_.profession ? needCfg_.profession->gathers : std::string("logs");
    if (gathers.empty()) return true;   // nothing to travel to; work is here
    if (client.TravelBusy()) return false;
    if (travelAttempts_ >= 3) {
        LogLine("goal_failed=TRAVEL_TO_REQUIRED_PLACE reason=\"three trips did not arrive\"");
        planner_.Finish(false, "three trips did not arrive", obs.nowMs);
        return false;
    }
    if (!travelInFlight_) {
        travelAttempts_++;
        const KnownResourceSource* stand =
            state_.memory.BestResource(gathers.c_str(), obs.x, obs.y,
                                       obs.nowMs);
        if (stand) {
            travelInFlight_ =
                client.TravelToPoint(stand->x, stand->y, 4, gathers.c_str());
        } else {
            travelInFlight_ = client.TravelToResource(ResourceKindFor(gathers));
        }
        if (!travelInFlight_) {
            LogLine("travel: could not start (%s)", client.TravelFailureText());
            planner_.NoteAttempt(obs.nowMs);
            nextActionMs_ = obs.nowMs + 8000;
        }
        return false;
    }
    travelInFlight_ = false;
    if (client.TravelSucceeded()) {
        planner_.NoteProgress();
        return true;
    }
    LogLine("travel: did not arrive (%s)", client.TravelFailureText());
    planner_.NoteAttempt(obs.nowMs);
    return false;
}

// PRACTISE THE SKILL BY DOING IT.
//
// The half of progression that is not buying tenths from a guildmaster. A
// guildmaster sells up to 30.0 and stops; everything above that is the hours a
// character puts in. Meditation is the honest first case: it is raised purely
// by using it, needs no target, no reagents and no foe, and this life already
// wants it.
//
// DELIBERATELY NARROW. Magery and Evaluating Intelligence are raised by
// casting, which needs a spell choice, reagents and a legal target, and
// getting that wrong would have a mage burning its scribe stock on practice
// casts. Fighting skills already have TRAIN_COMBAT. Everything else --
// Inscription, Blacksmithing, Lumberjacking -- is raised by the work the life
// already does, and must NOT get an errand of its own: a scribe writes scrolls
// to sell, not to practise.
// EAT SOMETHING.
//
// The simplest need in the model, and it had NO GOAL AT ALL until now:
// NeedFood was assessed and printed every tick -- 27 times in one twenty
// minute session -- and appeared in no entry of the goal table, so it fell
// into a void every time. That is why M4's hunger row reads BUILT / NEVER
// FIRED. It was never reachable.
//
// Two halves, in the order a person would do them: eat what you are carrying,
// and if you are carrying none, go and buy some.
// DOES THIS LIFE HAVE A USE FOR THIS GRAPHIC?
//
// The one predicate behind both halves of "everything else is spare": what the
// dead-weight bank pass puts down, and what the loot pass sells. Gold, the
// tools this profession declares, the consumables it stocks, what it makes,
// and what it makes those from -- everything else is spare.
bool Runner::LifeNeedsGraphic(u16 gfx) const {
    if (gfx == kGoldCoin) return true;
    const prof::Profession* me = needCfg_.profession;
    if (!me) return false;
    for (const prof::ToolNeed& t : me->tools)
        for (u16 g : t.graphics) if (g == gfx) return true;
    for (const prof::ConsumableNeed& c : me->consumables)
        for (u16 g : c.graphics) if (g == gfx) return true;
    auto named = [&](const std::string& item) {
        for (u16 g : econ::GraphicsForItem(item.c_str())) if (g == gfx) return true;
        return false;
    };
    for (const std::string& it : me->consumes) if (named(it)) return true;
    for (const std::string& made : me->produces) {
        if (named(made)) return true;
        const prod::Recipe* r = prod::FindRecipe(made.c_str());
        if (!r) continue;
        for (const prod::Ingredient& in : r->inputs)
            if (in.item && named(in.item)) return true;
    }
    return false;
}

bool Runner::DoGetFood(Client& client, const Observation& obs) {
    if (client.ActionBusy()) return false;

    const u32 food = FindAny(client, kFood, sizeof(kFood) / sizeof(kFood[0]));
    if (food) {
        LogLine("food: eating (hungry=%d starving=%d, carrying %d)",
                obs.hungry ? 1 : 0, obs.starving ? 1 : 0, obs.food);
        // A double-click is how a player eats. The server decides whether it
        // helped; the next tick's journal says so.
        client.ActionUseObject(food);
        planner_.NoteProgress();
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }

    // A MAGE MAKES ITS OWN SUPPER.
    //
    // "food was not a problem at all normally in Revolution UO -- mages make
    // their own food with the spell, you can collect from farms throughout the
    // world, fishers can sell fish steaks to people" (project owner,
    // 2026-08-29). Treating hunger as a shopping errand was the mistake under
    // all of this: it made a solved problem into a goal that could eat a whole
    // session.
    //
    // [SPELL 2] s_create_food is spellflag_playeronly with no target flag at
    // all (spells_magery.scp:36) -- four mana, MAGERY 10.0 to attempt, cast at
    // nobody. Anyone who can cast it should, before walking anywhere: it costs
    // no gold, needs no shop, and works while broke, which is exactly the
    // predicament the stand-down below was written for. It also raises Magery,
    // so the errand pays for itself.
    //
    // Mana is the renewable resource. Spending 4 of it on dinner is free in a
    // way that 30 gold is not.
    // HAVING THE SKILL IS NOT HAVING THE SPELL.
    //
    // First live outing of the line above: Voris, an alchemist carrying Magery
    // 50.0 and no spellbook worth the name, asked for Create Food every six
    // seconds for the whole session and was told every time --
    //
    //   [ACTION] cast_spell id=2 target=0x00000000 mana=32
    //   System: The spell is not in your spellbook.
    //
    // -- with mana sitting at 32 the entire time, which is the tell: a cast
    // that costs nothing never happened. The skill check passed and the
    // capability check did not exist. Ask once, believe the answer, and go
    // shopping like anyone else.
    if (noCreateFoodSpell_) {
        // fall through to buying
    } else if (obs.SkillTenths(rules::kMagery) >= 100) {
        if (createFoodMark_ != 0 &&
            client.JournalSaidSince("not in your spellbook", createFoodMark_)) {
            noCreateFoodSpell_ = true;
            LogLine("food: Create Food is not in this character's spellbook "
                    "(magery %.1f) -- buying food instead for the rest of the "
                    "session", obs.SkillTenths(rules::kMagery) / 10.0);
        } else if (obs.mana >= kCreateFoodMana) {
            LogLine("food: casting Create Food rather than shopping "
                    "(magery %.1f, mana %d)",
                    obs.SkillTenths(rules::kMagery) / 10.0, obs.mana);
            createFoodMark_ = client.JournalNowMs();
            client.ActionCastSpell(kSpellCreateFood);
            planner_.NoteProgress();
            nextActionMs_ = obs.nowMs + 6000;
            return false;
        }
        // Out of mana but able to cast: waiting for mana beats walking to a
        // shop, and beats standing the goal down while broke.
        else if (obs.gold < kFoodMoney) {
            LogLine("food: %d mana is short of the %d Create Food needs -- "
                    "resting for it rather than shopping with %d gold",
                    obs.mana, kCreateFoodMana, obs.gold);
            nextActionMs_ = obs.nowMs + 15000;
            return false;
        }
    }

    // STILL TO DO, and deliberately not faked here: crops on the world's farms
    // are a second free source, and fish steaks bought from a FISHER are the
    // third -- which is the same errand as R4's first player-to-player trade,
    // since the fisher selling them will be another bot. Both belong with the
    // market layer rather than bolted into this goal.

    // NOTHING TO EAT AND NOTHING TO BUY WITH.
    //
    // A goal that cannot possibly succeed must stand down, or it eats the
    // session. This one did exactly that on its first live outing: Kaelen died,
    // lost everything to full loot, and woke with no food and no gold -- so
    // GET_FOOD failed, was re-picked, and took the WHOLE 25 minutes:
    //
    //   session_goals families=1 picks=5 top=100% varied=0 | upkeep=5(100%)
    //   session_summary goals=0/5 gold=0->0
    //
    // Being hungry with an empty purse is a real predicament and the honest
    // response is to go and earn something, not to keep walking to a shop.
    if (obs.gold < kFoodMoney) {
        LogLine("food: hungry with %d gold -- nothing to eat and nothing to buy "
                "with; standing down to go and earn", obs.gold);
        planner_.Cooldown(GoalKind::GetFood, obs.nowMs + kNoFoodCooldownMs);
        planner_.Finish(false, "no food and no money", obs.nowMs);
        nextActionMs_ = obs.nowMs + 5000;
        return false;
    }

    // Buying food is an ordinary provisioner errand, and it is NOT a craft
    // input -- so it does not belong in DoBuySupplies, which is about the
    // things a profession makes other things from.
    if (client.TravelBusy()) return false;

    // A PROVISIONER ON THIS SHARD CANNOT SELL FOOD.
    //
    // This goal asked one anyway, all session, and never ate. The shop window
    // opened every time -- 24 items, all of them backpacks, lockpicks, bottles
    // and board games -- because TNS's VENDOR_S_PROVISIONER has its four food
    // lines COMMENTED OUT (tm_vend.scp:1276-1279):
    //
    //   //SELL=i_bread_loaf,{5 38}
    //   //SELL=i_lamb_leg,{5 38}
    //   //SELL=i_chicken_leg,{5 38}
    //   //SELL=i_bird_cooked,{5 38}
    //
    // So this was never a protocol failure or a pathing failure. The errand was
    // addressed to a shop that structurally does not stock the goods. Voris and
    // Ysolde spent a whole 25-minute session on it and picked no other goal:
    //   session_goals families=1 picks=4 top=100% | GET_FOOD=4(100%)
    //
    // The BAKER carries it -- SELL=i_bread_loaf,{55 140}, plus pies, muffins
    // and cakes -- and i_bread_loaf is ITEMDEF 0103b, which is already the
    // first entry in kFood above, so the eating side needed no change at all.
    // Ask the baker first and keep the provisioner only as a fallback: it costs
    // nothing when a baker is near, and a town without one still gets a try.
    //
    // The runtime vendor lists are TNS's, kept deliberately. Uncommenting those
    // four lines would have been the smaller diff and the wrong one -- it edits
    // the shard's economy to suit the bot instead of teaching the bot where
    // food is sold.
    u32 keeper = client.NearestMobileWithTrade("baker");
    const char* keeperTrade = "baker";
    if (!keeper) {
        keeper = client.NearestMobileWithTrade("provisioner");
        keeperTrade = "provisioner";
    }
    if (keeper) {
        // WALK TO THEM FIRST. Every other vendor path learned this today and
        // this one, written later the same day, did not: it shouted the
        // keeper's name from wherever it happened to be standing.
        //
        // Seen directly rather than deduced -- the owner sent a screenshot of
        // Kaelen saying "Taite buy" twice from INSIDE A DIFFERENT ROOM of the
        // Britain provisioner's shop, a wall between him and Taite, who
        // answered with the generic "I'm here to sell thee supplies" because
        // she had heard her name and nothing she could act on. No log line
        // says any of that: to the bot it looked like an ask that got no shop
        // window. Headless runs prove mechanics; they cannot prove where the
        // character is standing.
        i32 vx = 0, vy = 0; i8 vz = 0;
        // AND IF WE CANNOT SEE WHERE THEY ARE, DO NOT SPEAK.
        //
        // The old code fell through to the ask when MobilePosition failed,
        // which is exactly how the shout-through-a-wall came back after being
        // fixed: the walk-up logged three times at an unchanging 4 tiles, the
        // mobile went stale, the position lookup failed, and the bot spoke
        // from wherever it stood. Not knowing where the keeper is, is a reason
        // to look again -- never a reason to assume we have arrived.
        if (!client.MobilePosition(keeper, &vx, &vy, &vz)) {
            LogLine("food: lost sight of the %s -- looking again before speaking",
                    keeperTrade);
            client.ActionScanMobiles();
            nextActionMs_ = obs.nowMs + 2000;
            return false;
        }
        const i32 d = TileDist(obs.x, obs.y, vx, vy);
        const i32 dz = (obs.z > vz) ? (obs.z - vz) : (vz - obs.z);
        if (d > 1 || dz > 3) {
            LogLine("food: the %s is %d tiles and %d z away -- "
                    "walking up before speaking", keeperTrade, d, dz);
            travelInFlight_ = client.TravelToEntity(keeper, 1);
            nextActionMs_ = obs.nowMs + 2000;
            return false;
        }
        LogLine("food: nothing to eat -- asking the %s", keeperTrade);
        client.ActionVendorOpen(keeper);
        nextActionMs_ = obs.nowMs + 9000;
        return false;
    }
    if (!travelInFlight_) {
        if (++foodTrips_ > kMaxFoodTrips) {
            LogLine("goal_failed=GET_FOOD reason=\"%d trips and no baker or "
                    "provisioner in reach\"", foodTrips_ - 1);
            planner_.Cooldown(GoalKind::GetFood, obs.nowMs + kNoFoodCooldownMs);
            planner_.Finish(false, "no food seller reachable", obs.nowMs);
            foodTrips_ = 0;
            nextActionMs_ = obs.nowMs + 5000;
            return false;
        }
        // Head for the baker, since that is who sells the goods. Alternate to
        // the provisioner on later trips so a town with no baker in the atlas
        // still gets tried rather than walking the same empty errand thrice.
        const bool tryBaker = (foodTrips_ % 2) == 1;
        LogLine("food: looking for a %s (trip %d)",
                tryBaker ? "baker" : "provisioner", foodTrips_);
        travelInFlight_ = client.TravelToService(
            tryBaker ? wm::Service::Baker : wm::Service::Provisioner,
            state_.homeCity.c_str());
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }
    travelInFlight_ = false;
    client.ActionScanMobiles();
    nextActionMs_ = obs.nowMs + 2500;
    return false;
}

// Walk to a mage shop, open it, and buy one thing. `graphic` of 0 means "any
// spell scroll", which is what buying from this shard's mage shop actually is:
// the stock is random_first_circle .. random_fourth_circle, so the spell that
// arrives is not chosen. Returns false while still working.
bool Runner::BuyFromMageShop(Client& client, const Observation& obs,
                             u16 graphic, u16 qty, const char* what) {
    if (client.TravelBusy()) return false;

    const u32 keeper = client.NearestMobileWithTrade("mage");
    if (!keeper) {
        if (++spellbookTrips_ > kMaxSpellbookTrips) {
            LogLine("goal_failed=FILL_SPELLBOOK reason=\"no mage shop reachable "
                    "after %d trips\"", spellbookTrips_ - 1);
            planner_.Cooldown(GoalKind::FillSpellbook,
                              obs.nowMs + kNoSpellbookCooldownMs);
            planner_.Finish(false, "no mage reachable", obs.nowMs);
            spellbookTrips_ = 0;
            return false;
        }
        LogLine("spellbook: looking for a mage shop to sell %s (trip %d)",
                what, spellbookTrips_);
        travelInFlight_ =
            client.TravelToService(wm::Service::Mage, state_.homeCity.c_str());
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }

    // Walk up before speaking -- the lesson the food goal had to learn twice.
    i32 vx = 0, vy = 0; i8 vz = 0;
    if (!client.MobilePosition(keeper, &vx, &vy, &vz)) {
        client.ActionScanMobiles();
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }
    const i32 d = TileDist(obs.x, obs.y, vx, vy);
    if (d > 1) {
        LogLine("spellbook: the mage is %d tiles away -- walking up", d);
        travelInFlight_ = client.TravelToEntity(keeper, 1);
        nextActionMs_ = obs.nowMs + 2000;
        return false;
    }

    if (client.VendorOffer().empty()) {
        LogLine("spellbook: asking the mage to show %s", what);
        client.ActionVendorOpen(keeper);
        nextActionMs_ = obs.nowMs + 9000;
        return false;
    }

    for (const Client::VendorItem& v : client.VendorOffer()) {
        const bool match =
            graphic ? (v.graphic == graphic)
                    : (v.graphic >= kFirstScrollGraphic &&
                       v.graphic <= kLastScrollGraphic);
        if (!match) continue;
        LogLine("spellbook: buying %s ('%s', 0x%04X) at %d gold",
                what, v.name.c_str(), v.graphic, static_cast<i32>(v.price));
        client.ActionVendorBuy(keeper, v.serial, qty);
        // An ask, not progress -- same reason as BUY_SUPPLIES: counting a
        // purchase before the server takes the gold clears the failure ladder
        // on every retry.
        planner_.NoteAttempt(obs.nowMs);
        // A bought scroll must be re-read into the book, so force a re-open.
        spellbookOpened_ = false;
        nextActionMs_ = obs.nowMs + 9000;
        return false;
    }

    LogLine("goal_failed=FILL_SPELLBOOK reason=\"this mage has no %s\"", what);
    planner_.Cooldown(GoalKind::FillSpellbook,
                      obs.nowMs + kNoSpellbookCooldownMs);
    planner_.Finish(false, "mage has no scrolls", obs.nowMs);
    return false;
}

// ---------------------------------------------------------------------------
// FILLING THE BOOK.
//
// "we need to add that mages tries to fill their book, make it full spell
// book" (project owner, 2026-08-29). A mage's spellbook is equipment, and this
// shard demonstrated why it is not optional: Voris carried Magery 50.0 and
// asked for Create Food twenty-six times in one session, being told every time
// "The spell is not in your spellbook".
//
// WHERE SCROLLS COME FROM ON THIS SHARD -- read from its own vendor and loot
// tables, written up in docs/REVOLUTION_GAMEPLAY_TRUTH.md 3.5:
//
//   circles 1-4   any mage shop, but as random_first_circle .. fourth, so the
//                 spell that arrives is NOT chosen
//   circle 5, part of 6, and Resurrection   a scribe, by name
//   circles 7-8   nobody sells them: dungeon chests and monster loot only
//
// Two consequences this goal is built around. A mage cannot buy its way to a
// full book, so the goal aims at a working kit rather than completeness. And
// because purchases are random, the honest loop is buy-then-look, not
// pick-then-buy -- which is also how a player fills a book on this shard.
//
// The book is READ, never assumed: opening it and counting what the server
// sends back is the only truthful source for what this character can cast.
bool Runner::DoFillSpellbook(Client& client, const Observation& obs) {
    if (client.ActionBusy()) return false;

    // NO BOOK IS A DIFFERENT PROBLEM FROM AN EMPTY ONE.
    if (obs.spellbookSerial == 0) {
        if (obs.gold < kSpellbookMoney) {
            LogLine("spellbook: no book and %d gold -- a book costs about %d, "
                    "so go and earn first", obs.gold, kSpellbookMoney);
            planner_.Cooldown(GoalKind::FillSpellbook,
                              obs.nowMs + kNoSpellbookCooldownMs);
            planner_.Finish(false, "no book and no money", obs.nowMs);
            return false;
        }
        BuyFromMageShop(client, obs, kSpellbookGraphic, 1, "a spellbook");
        return false;
    }

    // LOOK IN IT BEFORE BUYING ANYTHING.
    //
    // ContainerItemCount is only meaningful once the server has sent the
    // contents, which happens when the book is opened. Treating an unopened
    // book as an empty one would send the character shopping for spells it
    // already owns.
    if (!spellbookOpened_) {
        LogLine("spellbook: opening the book to see what is already in it");
        client.ActionUseObject(obs.spellbookSerial);
        spellbookOpened_ = true;
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }

    if (obs.spellsKnown >= kSpellbookComfortableRuntime) {
        LogLine("spellbook: %d spells is a working book -- the rest are scribe "
                "and dungeon work, not shopping", obs.spellsKnown);
        planner_.Finish(true, "book is serviceable", obs.nowMs);
        return true;
    }

    // A SCROLL IN THE PACK BELONGS IN THE BOOK.
    //
    // Looted scrolls arrive here too, which is the only route to circles 7-8,
    // so this runs before any purchase.
    for (u16 g = kFirstScrollGraphic; g <= kLastScrollGraphic; ++g) {
        const u32 scroll = client.FindBackpackItemByGraphic(g);
        if (!scroll) continue;
        LogLine("spellbook: adding scroll 0x%04X to the book (%d spells so far)",
                g, obs.spellsKnown);
        client.ActionMoveItem(scroll, 1, obs.spellbookSerial);
        planner_.NoteProgress();
        // Re-read the book after the drop rather than assuming it took: a
        // duplicate spell is refused and the scroll stays in the pack.
        spellbookOpened_ = false;
        nextActionMs_ = obs.nowMs + 2500;
        return false;
    }

    // NOTHING TO ADD, SO BUY. Circles 1-4 only, and random at that.
    if (obs.gold < kScrollMoney) {
        LogLine("spellbook: %d spells and %d gold -- too poor to buy scrolls, "
                "standing down to earn", obs.spellsKnown, obs.gold);
        planner_.Cooldown(GoalKind::FillSpellbook,
                          obs.nowMs + kNoSpellbookCooldownMs);
        planner_.Finish(false, "no money for scrolls", obs.nowMs);
        return false;
    }
    BuyFromMageShop(client, obs, 0, 1, "a scroll");
    return false;
}

bool Runner::DoPracticeSkill(Client& client, const Observation& obs) {
    const int skillId = obs.wantPracticeSkill;
    if (skillId < 0) return true;
    if (client.ActionBusy()) return false;

    const i32 have = obs.SkillTenths(skillId);

    // MAGERY IS RAISED BY CASTING, WITH OR WITHOUT A FOE.
    //
    // Owner's rule, and the spell table agrees: [SPELL 2] s_create_food is
    // FLAGS=spellflag_playeronly with no targ flag at all
    // (runtime/scripts/spells/spells_magery.scp:36), so it is cast at nobody.
    // Four mana, MAGERY 10.0 to attempt. It is the honest practice spell: no
    // target to pick wrong, no harm flag to make a criminal of the caster, and
    // what it produces is FOOD -- which this character also needs and has no
    // other way to get.
    //
    // Deliberately not a combat spell. Practising Magery must not be a way to
    // start fights the life did not choose.
    if (skillId == rules::kMagery) {
        // And the same capability check the food goal needed. Practising by
        // casting a spell the character does not own burns the session at one
        // refusal every six seconds, with mana never moving.
        if (noCreateFoodSpell_ ||
            (createFoodMark_ != 0 &&
             client.JournalSaidSince("not in your spellbook", createFoodMark_))) {
            noCreateFoodSpell_ = true;
            LogLine("practice: Create Food is not in this character's spellbook "
                    "-- Magery cannot be practised this way");
            planner_.Finish(false, "no create food spell", obs.nowMs);
            nextActionMs_ = obs.nowMs + 5000;
            return false;
        }
        if (obs.mana < kCreateFoodMana) {
            LogLine("practice: %d mana is not enough to cast (need %d) -- "
                    "resting instead", obs.mana, kCreateFoodMana);
            nextActionMs_ = obs.nowMs + 15000;
            return true;   // meditation or time will bring it back
        }
        LogLine("practice: casting Create Food to raise Magery (%.1f, mana %d)",
                have / 10.0, obs.mana);
        createFoodMark_ = client.JournalNowMs();
        client.ActionCastSpell(kSpellCreateFood);
        planner_.NoteProgress();
        nextActionMs_ = obs.nowMs + 6000;
        return false;
    }

    LogLine("practice: using %s to raise it (%.1f)", rules::SkillName(skillId),
            have / 10.0);
    client.ActionUseSkill(skillId);
    planner_.NoteProgress();
    // Meditation runs for a while and the server decides when it ends. Long
    // enough that this is not a spin, short enough to notice an interruption.
    nextActionMs_ = obs.nowMs + 12000;
    return false;
}

bool Runner::DoIdle(Client& client, const Observation& obs) {
    (void)client;
    // A bounded no-op. It exists so a tick with nothing to do SAYS so rather
    // than spinning, and so the planner is never in a "no goal" state.
    nextActionMs_ = obs.nowMs + 5000;
    if (obs.nowMs - planner_.Current().startedAtMs > 15000) return true;
    return false;
}

}  // namespace uo::life
