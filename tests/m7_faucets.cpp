// M7 -- the Gold Faucet Registry.
//
// The rule the registry exists to enforce:
//
//     Just because an NPC technically accepts an item does not mean the bot
//     may use that NPC as its economic strategy.
//
// Stock Scripts-X buys hundreds of player-crafted goods. That is a fact about
// SPHERE. This suite checks that the registry keeps the two apart, that every
// row carries its evidence, and that the refusals name which kind of no they
// are.
//
// No server, no MULs, no world data.

#include "uo/faucets.h"
#include "uo/vendor_policy.h"

#include <cstdio>
#include <cstring>
#include <set>
#include <string>

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
using namespace uo::faucet;

const GoldFaucet* Row(const char* id) {
    for (const GoldFaucet& f : All()) {
        if (std::strcmp(f.id, id) == 0) return &f;
    }
    return nullptr;
}

// --------------------------------------------------------------------------
void TestEveryRowIsAuditable() {
    Section("registry: every row carries its own evidence");

    Check(All().size() >= 15, "the registry is populated");

    std::set<std::string> ids;
    for (const GoldFaucet& f : All()) {
        Check(f.id != nullptr && f.id[0] != 0, "every faucet has an id");
        if (!f.id) continue;
        Check(ids.insert(f.id).second, "ids are unique");

        // A verdict a human cannot read is a bug. This is the single most
        // important property in the file: the reason IS the character's
        // reasoning evidence, and it is what a later audit reads.
        if (!f.reason || f.reason[0] == 0) {
            std::printf("  FAIL: '%s' has no reason\n", f.id);
            ++g_failures;
        }
        ++g_checks;

        // History and runtime are recorded SEPARATELY, because they disagree
        // and the disagreement is the interesting part.
        Check(static_cast<int>(f.history) < static_cast<int>(HistoryEvidence::Count),
              "history evidence is a real value");
        Check(static_cast<int>(f.runtime) < static_cast<int>(RuntimeEvidence::Count),
              "runtime evidence is a real value");
        Check(static_cast<int>(f.policy) < static_cast<int>(Policy::Count),
              "policy is a real value");
    }
}

// --------------------------------------------------------------------------
void TestAllowedNeedsEvidence() {
    Section("registry: nothing is ALLOWED on no evidence at all");

    for (const GoldFaucet& f : All()) {
        if (!Allowed(f.policy)) continue;
        // An allowed route must rest on SOMETHING: either Revolution said so,
        // or we watched it work here. Allowing a route that has neither would
        // be exactly the stock-Scripts-X mistake in a new costume.
        const bool grounded =
            f.history == HistoryEvidence::Confirmed ||
            f.history == HistoryEvidence::NotFullyConfirmed ||
            f.runtime == RuntimeEvidence::LiveProven;
        if (!grounded) {
            std::printf("  FAIL: '%s' is ALLOWED with no evidence either way\n",
                        f.id);
            ++g_failures;
        }
        ++g_checks;
    }
}

// --------------------------------------------------------------------------
void TestTheAllowedFaucets() {
    Section("registry: the routes Revolution actually supports");

    // FISHING. The strongest case: the guide states it, and the runtime
    // carries the BUY rows.
    const GoldFaucet* fish = Row("fish_raw_to_fisher");
    Check(fish != nullptr, "fishing is in the registry");
    if (fish) {
        Check(Allowed(fish->policy), "and allowed");
        Check(fish->history == HistoryEvidence::Confirmed,
              "on a confirmed Revolution statement");
        Check(fish->sourceType == SourceType::VendorSale, "as a vendor sale");
    }

    // BOWCRAFT. Explicitly "players OR NPC vendors" -- the explicit NPC
    // channel is what separates it from the other crafting trades.
    const GoldFaucet* bow = Row("bow_to_bowyer");
    Check(bow && Allowed(bow->policy), "bowcraft is allowed");
    if (bow) {
        Check(bow->history == HistoryEvidence::Confirmed,
              "on Revolution's own Bowcraft guidance");
    }

    // INSCRIPTION. The nuance case, and the registry must hold both halves at
    // once rather than collapsing them.
    const GoldFaucet* scroll = Row("scroll_to_mage_shop");
    Check(scroll != nullptr, "inscription is in the registry");
    if (scroll) {
        Check(Allowed(scroll->policy), "and allowed");
        Check(scroll->runtime == RuntimeEvidence::LiveProven,
              "because it is LIVE PROVEN on this shard");
        Check(scroll->history == HistoryEvidence::NotFullyConfirmed,
              "while the ARCHIVAL claim stays not-fully-confirmed -- live "
              "reality is not downgraded because the archive is vague, and "
              "the archive is not overstated because the runtime works");
    }

    // MONSTER GOLD. Creation, not transfer: it did not come out of another
    // player's purse.
    const GoldFaucet* mob = Row("monster_gold");
    Check(mob && Allowed(mob->policy), "monster gold is allowed");
    if (mob) {
        Check(mob->sourceType == SourceType::MonsterLoot, "as monster loot");
        Check(mob->profession == nullptr,
              "and open to anybody who can survive the fight");
    }
}

// --------------------------------------------------------------------------
void TestTheRefusedFaucets() {
    Section("registry: technical acceptance is not authenticity");

    // Each of these is bought by a stock template. That is precisely why the
    // registry has to say no explicitly rather than by omission.
    struct Case { const char* id; Policy expect; const char* why; };
    const Case kCases[] = {
        {"smith_output_to_vendor",     Policy::RefuseAuthenticity,
         "smith output: mine -> smith -> dump would print gold"},
        {"carpentry_output_to_vendor", Policy::RefuseAuthenticity,
         "carpentry output"},
        {"tailor_output_to_vendor",    Policy::RefusePlayerMarket,
         "robes and cloth are player goods"},
        {"tinker_output_to_vendor",    Policy::RefusePlayerMarket,
         "tinker output"},
        {"alchemy_output_to_vendor",   Policy::RefusePlayerMarket,
         "potions and kegs"},
        {"ingot_to_vendor",            Policy::RefusePlayerMarket,
         "iron ingots"},
        {"log_to_vendor",              Policy::RefusePlayerMarket,
         "logs -- the owner said so directly"},
    };
    for (const Case& c : kCases) {
        const GoldFaucet* f = Row(c.id);
        if (!f) {
            std::printf("  FAIL: '%s' is missing from the registry\n", c.id);
            ++g_failures;
            continue;
        }
        ++g_checks;
        if (f->policy != c.expect) {
            std::printf("  FAIL: '%s' is %s, expected %s (%s)\n", c.id,
                        PolicyName(f->policy), PolicyName(c.expect), c.why);
            ++g_failures;
        }
        ++g_checks;
        // Every one of them IS script-supported. If that ever reads otherwise
        // the row has stopped describing the thing it was written about.
        Check(f->runtime == RuntimeEvidence::ScriptSupported,
              "the stock scripts really do buy it, which is the whole point");
    }
}

// --------------------------------------------------------------------------
void TestBlockedAndUnknownAreDifferent() {
    Section("registry: blocked, unknown and refused are three answers");

    // BLOCKED: Revolution did it, we cannot yet.
    const GoldFaucet* treasure = Row("treasure_gold");
    Check(treasure != nullptr, "treasure is in the registry");
    if (treasure) {
        Check(treasure->history == HistoryEvidence::Confirmed,
              "Revolution adjusted treasure gold, so it is authentic");
        Check(treasure->policy == Policy::BlockedRuntime,
              "but BLOCKED until live-proven here rather than assumed");
        Check(!Allowed(treasure->policy), "so not usable yet");
    }

    const GoldFaucet* bounty = Row("head_hunter_bounty");
    Check(bounty != nullptr, "the Head Hunter bounty is recorded");
    if (bounty) {
        Check(bounty->history == HistoryEvidence::Confirmed,
              "Revolution ran a Head Hunter system");
        Check(bounty->runtime == RuntimeEvidence::Unverified,
              "the runtime mechanic is unverified here");
    }

    // UNKNOWN: we have not established it either way. NOT the same as refused.
    const GoldFaucet* beg = Row("begging");
    Check(beg != nullptr, "begging is recorded");
    if (beg) {
        Check(beg->policy == Policy::Unknown,
              "begging is UNKNOWN pending a runtime audit");
        Check(!Allowed(beg->policy), "and therefore not used meanwhile");
    }

    // Unknown must never be silently treated as allowed.
    for (const GoldFaucet& f : All()) {
        if (f.policy == Policy::Unknown) {
            Check(!Allowed(f.policy), "an UNKNOWN route is never allowed");
        }
    }
}

// --------------------------------------------------------------------------
void TestLookups() {
    Section("registry: the lookups a caller actually uses");

    Check(AllowedForItem("i_fish_big_1") != nullptr, "fish has an allowed route");
    Check(AllowedForItem("i_scroll_poison") != nullptr, "so does a scroll");

    // The three that matter most, because a stock template buys every one.
    Check(AllowedForItem("i_log") == nullptr, "a log has NO allowed route");
    Check(AllowedForItem("i_ingot_iron") == nullptr, "nor an ingot");
    Check(AllowedForItem("i_club") == nullptr, "nor a club");

    // ...but they ARE in the registry, with a verdict. Absence and refusal
    // are different states and the caller must be able to tell them apart.
    Check(!ForItem("i_log").empty(),
          "a log is KNOWN and refused, not merely absent");
    Check(ForItem("i_something_nobody_ever_made").empty(),
          "something nobody has considered is genuinely absent");

    Check(AllowedForItem(nullptr) == nullptr, "a null query is not a crash");
    Check(ForProfession(nullptr).empty(), "nor a null profession");

    // A profession's allowed routes include the ones open to anybody.
    const std::vector<const GoldFaucet*> smith = ForProfession("miner_smith");
    bool sawMob = false, sawSmithOutput = false;
    for (const GoldFaucet* f : smith) {
        if (std::strcmp(f->id, "monster_gold") == 0) sawMob = true;
        if (std::strcmp(f->id, "smith_output_to_vendor") == 0) sawSmithOutput = true;
    }
    Check(sawMob, "a smith may still hunt: monster gold is open to anybody");
    Check(!sawSmithOutput, "but may not dump its own output on a vendor");
}

// --------------------------------------------------------------------------
void TestEnumNamesDoNotCollide() {
    Section("registry: no two enum values print the same string");

    // Policy::BlockedRuntime and RuntimeEvidence::Blocked both printed
    // "BLOCKED_RUNTIME". They are different enums meaning different things --
    // "authentic but not usable here" versus "we tried and it does not work"
    // -- and a log or a report showing one string for both cannot tell them
    // apart. It was latent because no row uses the runtime value yet, which
    // is exactly the kind of collision that surfaces only once it matters.
    std::set<std::string> seen;
    for (int i = 0; i < static_cast<int>(Policy::Count); ++i) {
        seen.insert(PolicyName(static_cast<Policy>(i)));
    }
    for (int i = 0; i < static_cast<int>(RuntimeEvidence::Count); ++i) {
        const char* n = RuntimeEvidenceName(static_cast<RuntimeEvidence>(i));
        if (!seen.insert(n).second) {
            std::printf("  FAIL: '%s' printed by two enums\n", n);
            ++g_failures;
        }
        ++g_checks;
    }
    for (int i = 0; i < static_cast<int>(HistoryEvidence::Count); ++i) {
        const char* n = HistoryEvidenceName(static_cast<HistoryEvidence>(i));
        // UNKNOWN legitimately appears in more than one enum -- it means the
        // same thing in each -- so it is the one allowed overlap.
        if (std::strcmp(n, "UNKNOWN") == 0) continue;
        if (!seen.insert(n).second) {
            std::printf("  FAIL: '%s' printed by two enums\n", n);
            ++g_failures;
        }
        ++g_checks;
    }
}

// --------------------------------------------------------------------------
void TestRefusalReasonsAreSpecific() {
    Section("refusals: every code is distinct and printable");

    std::set<std::string> names;
    for (int i = 0; i < static_cast<int>(Refusal::Count); ++i) {
        const char* n = RefusalName(static_cast<Refusal>(i));
        Check(n != nullptr && n[0] != 0, "every refusal has a name");
        Check(names.insert(n).second, "and the names are distinct");
    }
    // The specific one the brief calls out: "cannot earn gold" is never the
    // right answer when a buyer exists and policy blocks the sale.
    Check(std::strcmp(RefusalName(Refusal::PlayerMarketGood),
                      "REFUSE_PLAYER_MARKET_GOOD") == 0,
          "the player-market refusal says exactly that");
}

}  // namespace

// REGRESSION -- a live defect, not a hypothetical. The fisher pulled fish out
// of the sea at Britain dock (run_m5/cast5.console.txt) and its own economy
// layer reported an empty hold, because only 0x09CC of the four big-fish
// graphics was mapped. The pack counter reads these names, Surplus() reads the
// pack, and the sell path reads Surplus, so three catches in four were
// unsellable and invisible. Ids: items/i_profession_cook_barkeep_baker.scp.
void TestEveryFishIsVisible() {
    Section("fish: every kind the sea yields can be seen and sold");
    const struct { u16 gfx; const char* name; } kFish[] = {
        {0x09CC, "i_fish_big_1"}, {0x09CD, "i_fish_big_2"},
        {0x09CE, "i_fish_big_3"}, {0x09CF, "i_fish_big_4"},
        {0x0DD6, "i_fish_small"},
    };
    for (const auto& f : kFish) {
        const char* n = uo::econ::ItemNameForGraphic(f.gfx);
        Check(n && std::strcmp(n, f.name) == 0, f.name);
        Check(!uo::econ::GraphicsForItem(f.name).empty(), f.name);
        Check(uo::faucet::AllowedForItem(f.name) != nullptr, f.name);
    }
}

int main() {
    std::printf("m7_faucets\n");
    TestEveryRowIsAuditable();
    TestAllowedNeedsEvidence();
    TestTheAllowedFaucets();
    TestTheRefusedFaucets();
    TestBlockedAndUnknownAreDifferent();
    TestLookups();
    TestEnumNamesDoNotCollide();
    TestRefusalReasonsAreSpecific();
    TestEveryFishIsVisible();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
