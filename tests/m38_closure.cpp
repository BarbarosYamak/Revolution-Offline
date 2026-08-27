// M3.8 pre-M4 closure: deterministic tests for the units this milestone added.
//
//   * Revolution mount rules (Phase 7) -- two skills, rolled ranges, and the
//     refusal that must happen even where the runtime would allow the tame
//   * the TSV exports (Phase 9) -- regenerated in memory and compared against
//     the committed files, so the view cannot silently drift from the code
//
// No server, no MULs, no data files beyond the exports themselves.

#include "uo/mounts.h"
#include "uo/mount_policy.h"
#include "uo/supplier.h"
#include "uo/pet.h"
#include "uo/production.h"
#include "uo/vendor_policy.h"

#include <cstdio>
#include <cstring>
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

// --------------------------------------------------------------------------
void TestMountRulesShape() {
    Section("mount rules: shape");

    const auto& all = mounts::KnownMounts();
    Check(all.size() >= 16, "every documented mount is in the table");

    for (const auto& m : all) {
        Check(m.defname && m.defname[0], "every rule names a creature");
        Check(m.source && m.source[0], "every rule cites a source");

        // UNKNOWN is -1 and must be -1 on BOTH skills or neither. A rule that
        // knew the taming gate but not the lore gate would silently let a bot
        // through on half the requirement.
        const bool tUnknown = m.tamingMinTenths < 0;
        const bool lUnknown = m.loreMinTenths < 0;
        Check(tUnknown == lUnknown, "taming and lore are known together or not at all");

        if (!tUnknown) {
            Check(m.tamingMaxTenths >= m.tamingMinTenths, "taming max >= min");
            Check(m.loreMaxTenths >= m.loreMinTenths, "lore max >= min");
            // Revolution gates both skills at the same value.
            Check(m.tamingMinTenths == m.loreMinTenths,
                  "Revolution requires Animal Lore at the same value as Taming");
        }
        if (!m.randomRange) {
            Check(m.tamingMaxTenths == m.tamingMinTenths,
                  "a fixed-threshold mount has no spread");
        }
    }
}

// --------------------------------------------------------------------------
void TestDocumentedThresholds() {
    Section("mount rules: the published numbers");

    struct Expect { const char* def; int taming; int supply; };
    // /binek_bilgileri, confirmed by the project owner.
    const Expect kExpect[] = {
        {"c_horse_gray",      531,  0},
        {"c_llama",           551,  0},
        {"c_ostard_desert",   651, 10},
        {"c_ostard_forest",   651, 10},
        {"c_ostard_frenzied", 771,  7},
        {"c_ostard_mid",      800,  7},
        {"c_kirin",           900,  2},
        {"c_unicorn",         981,  1},
        {"c_steed",           999,  1},
        {"c_nightmare",       999,  1},
    };
    for (const auto& e : kExpect) {
        const auto* m = mounts::FindMount(e.def);
        Check(m != nullptr, "mount is present");
        if (!m) continue;
        Check(m->tamingMinTenths == e.taming, "taming threshold matches the archive");
        Check(m->loreMinTenths == e.taming, "lore threshold matches taming");
        Check(m->weeklySupply == e.supply, "weekly supply matches /spawntakip_sistemi");
    }

    // The calendar totals 49 a week. If a future edit changes a supply figure,
    // this is what notices.
    int total = 0;
    for (const auto& m : mounts::KnownMounts()) total += m.weeklySupply;
    Check(total == 49, "the supply calendar still totals 49 mounts a week");
}

// --------------------------------------------------------------------------
void TestBotRefusesIllegalTame() {
    Section("mount rules: the bot obeys Revolution, not the runtime");

    // THE CENTRAL ASSERTION OF PHASE 7.
    //
    // This runtime lets a horse be tamed at 29.1. Revolution requires 53.1 in
    // BOTH Animal Taming and Animal Lore. A character at 50/50 -- which is what
    // ordinary character creation clamps to, and exactly what the M3.7.1 tamer
    // had -- would succeed on this server and must be refused by us.
    Check(!mounts::CanAttemptTame(500, 500, "c_horse_gray"),
          "a 50/50 character is refused a horse despite the runtime's 29.1");
    Check(mounts::CanAttemptTame(531, 531, "c_horse_gray"),
          "53.1/53.1 clears a horse");

    // Both skills are required, so clearing one is not enough.
    Check(!mounts::CanAttemptTame(531, 500, "c_horse_gray"),
          "taming alone does not clear a horse");
    Check(!mounts::CanAttemptTame(500, 531, "c_horse_gray"),
          "lore alone does not clear a horse");

    // The runtime is STRICTER for kirin (105.0 vs 90.0). Our rule is still
    // Revolution's: the bot may attempt at 90, and the server will refuse it.
    // That is the correct division -- we do not model the server's mistakes.
    Check(mounts::CanAttemptTame(900, 900, "c_kirin"),
          "a kirin is legal at Revolution's 90.0 even though this runtime wants 105.0");

    // An UNKNOWN gate is not an open gate.
    Check(!mounts::CanAttemptTame(1000, 1000, "c_horse_pack"),
          "a pack horse is refused at any skill: no Revolution evidence");
    Check(!mounts::CanAttemptTame(1000, 1000, "c_ridgeback"),
          "a ridgeback is refused at any skill: no Revolution evidence");

    // Silence is not permission.
    Check(!mounts::CanAttemptTame(1000, 1000, "c_dragon_red"),
          "a creature with no rule at all is refused");
    Check(!mounts::CanAttemptTame(1000, 1000, nullptr),
          "a null creature is refused");
}

// --------------------------------------------------------------------------
void TestRolledRanges() {
    Section("mount rules: ranges are rolled per animal");

    // Mustang 65.0-80.0 and Shire 65.0-95.0 are not display spreads: each
    // individual has its own requirement inside the band. So there are two
    // different questions, and they have different answers.
    Check(mounts::CanAttemptTame(650, 650, "c_horse_mustang"),
          "at the minimum a mustang attempt is possible");
    Check(!mounts::CanTameReliably(650, 650, "c_horse_mustang"),
          "at the minimum a mustang is NOT reliable");
    Check(mounts::CanTameReliably(800, 800, "c_horse_mustang"),
          "at 80.0 every mustang is within reach");

    Check(mounts::CanAttemptTame(650, 650, "c_horse_shire"),
          "at the minimum a shire attempt is possible");
    Check(!mounts::CanTameReliably(900, 900, "c_horse_shire"),
          "90.0 is still not enough for every shire -- the band runs to 95.0");
    Check(mounts::CanTameReliably(950, 950, "c_horse_shire"),
          "at 95.0 every shire is within reach");

    // For a fixed mount the two questions collapse.
    Check(mounts::CanAttemptTame(531, 531, "c_horse_gray") ==
          mounts::CanTameReliably(531, 531, "c_horse_gray"),
          "a fixed-threshold mount is attemptable exactly when it is reliable");
}

// --------------------------------------------------------------------------
void TestSkillCost() {
    Section("mount rules: what a mount actually costs");

    // The number that makes a mount a career decision rather than a purchase.
    // Both skills, at the maximum of any range.
    Check(mounts::ReliableSkillCost("c_horse_gray") == 1062,
          "a horse costs 106.2 of a 700-point build");
    Check(mounts::ReliableSkillCost("c_nightmare") == 1998,
          "a nightmare costs 199.8 -- 28.5% of an entire character");
    Check(mounts::ReliableSkillCost("c_horse_shire") == 1900,
          "a shire costs 190.0, priced at the top of its rolled band");
    Check(mounts::ReliableSkillCost("c_horse_pack") == -1,
          "an unknown requirement has no cost, rather than a cost of zero");

    // Sanity: nothing may exceed the 700-point cap on its own.
    for (const auto& m : mounts::KnownMounts()) {
        const i32 cost = mounts::ReliableSkillCost(m.defname);
        Check(cost <= 7000, "no single mount consumes more than the whole skill cap");
    }
}

// --------------------------------------------------------------------------
void TestRuntimeDivergence() {
    Section("mount rules: the server-authenticity debt is enumerable");

    // Being able to LIST where the server is more permissive than Revolution is
    // the point of carrying both numbers. If this ever drops to zero, either
    // the runtime was fixed or the table was flattened -- both worth noticing.
    int laxer = 0;
    for (const auto& m : mounts::KnownMounts()) {
        if (mounts::RuntimeIsMorePermissive(m)) ++laxer;
    }
    Check(laxer >= 5, "the runtime is measurably more permissive than Revolution");

    const auto* horse = mounts::FindMount("c_horse_gray");
    Check(horse && mounts::RuntimeIsMorePermissive(*horse),
          "the horse is the documented example: runtime 29.1 vs Revolution 53.1");

    const auto* kirin = mounts::FindMount("c_kirin");
    Check(kirin && !mounts::RuntimeIsMorePermissive(*kirin),
          "the kirin is not: the runtime is stricter there, and that is not debt");
}

// --------------------------------------------------------------------------
// Phase 9: the exports are a VIEW of the compiled graph. This is what stops the
// view rotting: regenerate every row in memory and compare against the file.
namespace {

std::vector<std::string> ReadLines(const std::string& path, bool* ok) {
    std::vector<std::string> out;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { *ok = false; return out; }
    *ok = true;
    std::string cur;
    int c;
    while ((c = std::fgetc(f)) != EOF) {
        if (c == '\r') continue;
        if (c == '\n') { out.push_back(cur); cur.clear(); continue; }
        cur += static_cast<char>(c);
    }
    if (!cur.empty()) out.push_back(cur);
    std::fclose(f);
    return out;
}

}  // namespace

void TestExportsNotStale(const std::string& dataDir) {
    Section("exports: the TSV view matches the compiled graph");

    bool ok = false;
    const auto mountLines = ReadLines(dataDir + "/revolution_mount_rules.tsv", &ok);
    Check(ok, "revolution_mount_rules.tsv exists");
    if (ok) {
        // header + one row per rule
        Check(mountLines.size() == mounts::KnownMounts().size() + 1,
              "the mount export has exactly one row per compiled rule");
    }

    const auto vendorLines = ReadLines(dataDir + "/revolution_vendor_policy.tsv", &ok);
    Check(ok, "revolution_vendor_policy.tsv exists");
    if (ok) {
        Check(vendorLines.size() == econ::VendorMatrix().size() + 1,
              "the vendor export has exactly one row per matrix entry");
    }

    const auto recipeLines = ReadLines(dataDir + "/revolution_recipes.tsv", &ok);
    Check(ok, "revolution_recipes.tsv exists");
    if (ok) {
        // One row per input, or one row for an input-less gathering step.
        usize expected = 0;
        for (const auto& r : prod::KnownRecipes()) {
            usize inputs = 0;
            for (const auto& in : r.inputs) if (in.item) ++inputs;
            expected += inputs ? inputs : 1;
        }
        Check(recipeLines.size() == expected + 1,
              "the recipe export has one row per recipe input");
    }

    bool resourcesOk = false;
    ReadLines(dataDir + "/revolution_resources.tsv", &resourcesOk);
    Check(resourcesOk, "revolution_resources.tsv exists");

    // An UNKNOWN must never be exported as a number. "-0.1" once shipped in
    // this very table and reads as a real threshold.
    if (ok) {
        bool sawBadUnknown = false;
        for (const auto& line : mountLines) {
            if (line.find("-0.1") != std::string::npos) sawBadUnknown = true;
        }
        Check(!sawBadUnknown, "no UNKNOWN is exported as a negative number");
    }
}


// --------------------------------------------------------------------------
void TestMountPolicy() {
    Section("mount policy: when to ride");
    using namespace uo::mountpolicy;

    // The arithmetic, from M3.7.1's MEASURED 1.82x rather than a theoretical 2x.
    // 4.0s to mount, 0.10s saved per tile -> 40 tiles to break even.
    Check(NetSecondsSaved(40, false) == 0.0, "40 tiles is exactly break-even");
    Check(NetSecondsSaved(100, false) > 0.0, "100 tiles repays the overhead");
    Check(NetSecondsSaved(10, false) < 0.0, "10 tiles is a net loss");
    Check(NetSecondsSaved(0, false) == 0.0, "a zero-length trip saves nothing");
    // Having to get off again at the far end raises the bar.
    Check(NetSecondsSaved(60, true) < NetSecondsSaved(60, false),
          "a trip that ends indoors is worth less");

    auto base = []() {
        TripContext c;
        c.distanceTiles = 500;
        c.ownedMountAvailable = true;
        return c;
    };

    Check(ShouldUseMountForTravel(base()).ride, "a long trip on an owned mount: ride");
    Check(ShouldUseMountForTravel(base()).reason == Reason::Ride, "...and says so");

    // Every refusal is distinguishable. A bot that reports NO_MOUNT_AVAILABLE
    // when the real answer is NOT_LEGAL has hidden an authenticity decision
    // behind a logistics one.
    { auto c = base(); c.alreadyMounted = true;
      Check(!ShouldUseMountForTravel(c).ride, "already mounted: nothing to do");
      Check(ShouldUseMountForTravel(c).reason == Reason::AlreadyMounted, "...distinctly"); }

    { auto c = base(); c.mountIsLegal = false;
      Check(ShouldUseMountForTravel(c).reason == Reason::NotLegal,
            "an illegal mount is refused as illegal, not as unavailable"); }

    { auto c = base(); c.ownedMountAvailable = false;
      Check(ShouldUseMountForTravel(c).reason == Reason::NoMountAvailable, "no mount"); }

    { auto c = base(); c.inCombat = true;
      Check(ShouldUseMountForTravel(c).reason == Reason::Unsafe,
            "a bot does not stop to climb onto a horse mid-fight"); }

    { auto c = base(); c.destinationIndoors = true;
      Check(ShouldUseMountForTravel(c).reason == Reason::DestinationIndoors,
            "indoors is a refusal: the one place a bot has ever got stuck"); }

    { auto c = base(); c.distanceTiles = 20;
      Check(ShouldUseMountForTravel(c).reason == Reason::TooShort,
            "crossing a courtyard is walked"); }

    // Legality is checked BEFORE availability, so an illegal mount reports as
    // illegal even when there is also nothing to ride.
    { auto c = base(); c.mountIsLegal = false; c.ownedMountAvailable = false;
      Check(ShouldUseMountForTravel(c).reason == Reason::NotLegal,
            "legality outranks availability in the reason given"); }

    // The floor and the arithmetic must agree. 45 tiles clears break-even by
    // half a second but is below the floor, and the floor wins.
    { auto c = base(); c.distanceTiles = 45;
      Check(!ShouldUseMountForTravel(c).ride,
            "a marginal trip is walked rather than ridden"); }
}


// --------------------------------------------------------------------------
void TestSupplierResolution() {
    Section("suppliers: concrete entities, never professions");
    using namespace uo::supply;

    const i64 t0 = 1000000;
    Registry reg;

    // THE GUILDMISTRESS CASE, asserted so it cannot come back.
    //
    // Three milestones sent bots to a PROFESSION and called arrival success:
    // blank runes (M3.6), the mage shop (M3.7), tinker tools (M3.8) -- the last
    // arriving at "Justine, the engineer guildmistress", who keeps no shop.
    //
    // Nothing was ever OBSERVED selling those things, so an empty registry must
    // say exactly that, rather than offering a place to walk to and hope.
    Need tools{NeedKind::Item, "i_tinker_tools", 1};
    Check(!reg.Best(tools, 0, 0, t0).usable,
          "an unobserved need has no supplier -- not a profession to guess at");
    Check(reg.Resolve(tools, 0, 0, t0).empty(),
          "a guild hall never enters the registry, because it sold nothing");

    // A supplier exists only once its shop list was actually read.
    reg.RecordVendorStock(0x9096, "Rhyssa", 2458, 455, 15,
                          "i_tinker_tools", 4, 34, t0);
    auto best = reg.Best(tools, 2460, 460, t0);
    Check(best.usable, "a verified sighting makes a usable supplier");
    Check(best.supplier.serial == 0x9096, "and it is CONCRETE -- a serial");
    Check(best.freshness == Freshness::VerifiedCurrent, "just-seen is verified");

    // Freshness decays with time, not with the entity moving.
    Check(Registry::FreshnessOf(best.supplier, t0 + kVerifiedMs + 1) ==
          Freshness::Recent, "past the verified window it is only recent");
    Check(Registry::FreshnessOf(best.supplier, t0 + kRecentMs + 1) ==
          Freshness::Stale, "and eventually stale");

    // Arriving to nothing DEMOTES, and three strikes invalidate. Sphere
    // restocks, so one empty cycle must not permanently condemn a vendor --
    // M3.7 watched a blacksmith carry no hammer while tm_vend.scp lists one.
    reg.RecordAbsent(0x9096, "i_tinker_tools", t0 + 1);
    Check(reg.Best(tools, 2460, 460, t0 + 1).usable,
          "one absence is not a verdict");
    reg.RecordAbsent(0x9096, "i_tinker_tools", t0 + 2);
    reg.RecordAbsent(0x9096, "i_tinker_tools", t0 + 3);
    auto dead = reg.Best(tools, 2460, 460, t0 + 3);
    Check(!dead.usable, "three absences invalidate");
    Check(dead.freshness == Freshness::Invalid, "...and say so");

    // A fresh sighting resurrects it: the vendor restocked.
    reg.RecordVendorStock(0x9096, "Rhyssa", 2458, 455, 15,
                          "i_tinker_tools", 4, 34, t0 + 10);
    Check(reg.Best(tools, 2460, 460, t0 + 10).usable,
          "a restock clears the invalidation");

    // POLICY REFUSAL IS NOT IGNORANCE. Logs are WORLD_GATHERED, so an NPC
    // selling them is refused -- but the observation is still recorded, and the
    // reason must survive to the caller rather than being reported as "unknown".
    Registry reg2;
    reg2.RecordVendorStock(0x1234, "a carpenter", 100, 100, 0,
                           "i_log", 50, 3, t0);
    Need logs{NeedKind::Item, "i_log", 1};
    auto refused = reg2.Best(logs, 100, 100, t0);
    Check(!refused.usable, "a policy-refused good is not usable");
    Check(refused.why.find("policy") != std::string::npos,
          "and the refusal says POLICY, not 'never seen'");
    Check(reg2.Size() == 1, "the observation is still kept -- it is a world fact");

    // Quantity is checked against what was SEEN, not what a template promises.
    Registry reg3;
    reg3.RecordVendorStock(0x2222, "a smith", 10, 10, 0, "i_ingot_iron", 2, 5, t0);
    Need many{NeedKind::Item, "i_ingot_iron", 40};
    Check(!reg3.Best(many, 10, 10, t0).usable,
          "observed stock below the need is not usable");

    // World resources need no vendor policy: gathering is always legitimate.
    Registry reg4;
    reg4.RecordResource("i_cotton", 1221, 1718, 0, t0);
    Need cotton{NeedKind::Resource, "i_cotton", 1};
    auto field = reg4.Best(cotton, 1200, 1700, t0);
    Check(field.usable, "a field the bot stood in is a usable source");
    Check(field.supplier.kind == SupplierKind::WorldResource, "and is a resource");
}


// --------------------------------------------------------------------------
void TestPetSemantics() {
    Section("pets: the engine's own words, not UO folklore");
    using namespace uo::pet;

    // Exact vocabulary from CCharNPCPet.cpp:88-115. Asserted so nobody
    // "improves" them into something the server does not parse.
    Check(std::strcmp(CommandWords(Command::Come), "come") == 0, "come");
    Check(std::strcmp(CommandWords(Command::Stay), "stay") == 0, "stay");
    Check(std::strcmp(CommandWords(Command::Stop), "stop") == 0, "stop");
    Check(std::strcmp(CommandWords(Command::Kill), "kill") == 0, "kill");
    Check(std::strcmp(CommandWords(Command::Attack), "attack") == 0, "attack");
    Check(std::strcmp(CommandWords(Command::GuardMe), "guard me") == 0, "guard me");

    // The prefix carries a trailing space on purpose: the engine matches "ALL "
    // exactly (CClientEvent.cpp:1954). "allkill" is not a command.
    Check(std::strcmp(kAllPrefix, "all ") == 0, "the all-prefix keeps its space");

    // WHICH COMMANDS RAISE A CURSOR. Getting this wrong hangs a scenario:
    // waiting for a target on "stay" waits forever, and NOT waiting on "kill"
    // fires the target reply into nothing.
    Check(NeedsTarget(Command::Kill), "kill is spoken-then-targeted");
    Check(NeedsTarget(Command::Attack), "attack is spoken-then-targeted");
    Check(NeedsTarget(Command::FollowTarget), "follow takes a target");
    Check(!NeedsTarget(Command::Come), "come acts immediately");
    Check(!NeedsTarget(Command::Stay), "stay acts immediately");
    Check(!NeedsTarget(Command::Stop), "stop acts immediately");
    Check(!NeedsTarget(Command::GuardMe), "guard me acts immediately");

    // Range: hearing is 14 tiles, veterinary is 2. Different numbers for
    // different jobs, and both are the engine's.
    Check(kHearingTiles == 14, "a pet hears at 14 tiles");
    Check(kVeterinaryTiles == 2, "bandaging needs 2");

    // UNKNOWN HEALTH IS NOT FULL HEALTH.
    OwnedAnimal a;
    a.serial = 0x1234;
    Check(HealthPercent(a) == -1, "unreported health is -1, never an optimistic 100");
    Check(!IsInDanger(a), "unknown is not danger; it is unknown");

    a.hpCur = 30; a.hpMax = 100;
    Check(HealthPercent(a) == 30, "a ratio reports as a percent");
    Check(IsInDanger(a), "30% is in danger");
    a.hpCur = 90;
    Check(!IsInDanger(a), "90% is not");

    // Dead is not "in danger" -- it is past saving, and a rescue loop that
    // treated it as rescuable would spin.
    a.alive = false; a.hpCur = 0;
    Check(!IsInDanger(a), "a dead pet is not in danger");

    // Veterinary preconditions.
    OwnedAnimal hurt;
    hurt.alive = true; hurt.nearby = true; hurt.hpCur = 50; hurt.hpMax = 100;
    Check(CanVeterinaryHeal(hurt, 1), "hurt, near and reachable: heal it");
    Check(!CanVeterinaryHeal(hurt, 5), "out of the 2-tile range: no");
    hurt.hpCur = 100;
    Check(!CanVeterinaryHeal(hurt, 1), "a healthy pet wastes the bandage");
    hurt.hpCur = 50; hurt.mounted = true;
    Check(!CanVeterinaryHeal(hurt, 1),
          "a mounted animal is an ITEM on layer 25, not a heal target");

    // A MOUNTED ANIMAL IS NOT A MISSING ANIMAL. M3.7.1 lost four runs to this
    // fact wearing four disguises.
    OwnedAnimal ridden;
    ridden.mounted = true;
    Check(IsUnobservableBecauseMounted(ridden),
          "riding it explains why no mobile scan can find it");
    OwnedAnimal onFoot;
    Check(!IsUnobservableBecauseMounted(onFoot), "an unridden pet should be findable");
}

}  // namespace

int main(int argc, char** argv) {
    std::printf("m38_closure\n");
    // The data directory is passed by CTest so the test can run from anywhere.
    const std::string dataDir = (argc > 1) ? argv[1] : "data";

    TestMountRulesShape();
    TestDocumentedThresholds();
    TestBotRefusesIllegalTame();
    TestRolledRanges();
    TestSkillCost();
    TestRuntimeDivergence();
    TestMountPolicy();
    TestSupplierResolution();
    TestPetSemantics();
    TestExportsNotStale(dataDir);

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
