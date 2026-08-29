#include "uo/faucets.h"

#include <cstring>

namespace uo::faucet {

const char* SourceTypeName(SourceType s) {
    switch (s) {
        case SourceType::VendorSale:  return "VENDOR_SALE";
        case SourceType::MonsterLoot: return "MONSTER_LOOT";
        case SourceType::Treasure:    return "TREASURE";
        case SourceType::Bounty:      return "BOUNTY";
        case SourceType::Begging:     return "BEGGING";
        case SourceType::Count:       break;
    }
    return "?";
}

const char* HistoryEvidenceName(HistoryEvidence e) {
    switch (e) {
        case HistoryEvidence::Unknown:           return "UNKNOWN";
        case HistoryEvidence::Confirmed:         return "REVOLUTION_CONFIRMED";
        case HistoryEvidence::NotFullyConfirmed: return "NOT_FULLY_CONFIRMED";
        case HistoryEvidence::Count:             break;
    }
    return "?";
}

const char* RuntimeEvidenceName(RuntimeEvidence e) {
    switch (e) {
        case RuntimeEvidence::Unverified:      return "RUNTIME_UNVERIFIED";
        case RuntimeEvidence::ScriptSupported: return "SCRIPT_SUPPORTED";
        case RuntimeEvidence::LiveProven:      return "LIVE_PROVEN";
        // Distinct from Policy::BlockedRuntime, which prints
        // "BLOCKED_RUNTIME". These are different enums with different
        // meanings -- one is "we tried and the runtime does not support
        // it", the other is "authentic, but not usable here" -- and a
        // log that showed the same string for both could not tell them
        // apart. No row uses this value yet, so the collision was
        // latent rather than active.
        case RuntimeEvidence::Blocked:         return "RUNTIME_TRIED_AND_BLOCKED";
        case RuntimeEvidence::Count:           break;
    }
    return "?";
}

const char* PolicyName(Policy p) {
    switch (p) {
        case Policy::Unknown:            return "UNKNOWN";
        case Policy::Allow:              return "ALLOW";
        case Policy::RefuseAuthenticity: return "REFUSE_AUTHENTICITY_UNKNOWN";
        case Policy::RefusePlayerMarket: return "REFUSE_PLAYER_MARKET_GOOD";
        case Policy::BlockedRuntime:     return "BLOCKED_RUNTIME";
        case Policy::Count:              break;
    }
    return "?";
}

bool Allowed(Policy p) { return p == Policy::Allow; }

const char* RefusalName(Refusal r) {
    switch (r) {
        case Refusal::None:                          return "NONE";
        case Refusal::NotSellable:                   return "REFUSE_NOT_SELLABLE";
        case Refusal::NoKnownBuyer:                  return "REFUSE_NO_KNOWN_BUYER";
        case Refusal::UnknownPrice:                  return "REFUSE_UNKNOWN_PRICE";
        case Refusal::RevolutionAuthenticityUnknown: return "REFUSE_REVOLUTION_AUTHENTICITY_UNKNOWN";
        case Refusal::PlayerMarketGood:              return "REFUSE_PLAYER_MARKET_GOOD";
        case Refusal::InsufficientSurplus:           return "REFUSE_INSUFFICIENT_SURPLUS";
        case Refusal::RequiredForBuild:              return "REFUSE_REQUIRED_FOR_BUILD";
        case Refusal::RequiredForProduction:         return "REFUSE_REQUIRED_FOR_PRODUCTION";
        case Refusal::VendorNotObserved:             return "REFUSE_VENDOR_NOT_OBSERVED";
        case Refusal::VendorUnreachable:             return "REFUSE_VENDOR_UNREACHABLE";
        case Refusal::VendorWrongFloor:              return "REFUSE_VENDOR_WRONG_FLOOR";
        case Refusal::EconomicRouteBlocked:          return "REFUSE_ECONOMIC_ROUTE_BLOCKED";
        case Refusal::InsufficientSkill:             return "REFUSE_INSUFFICIENT_SKILL";
        case Refusal::MissingTool:                   return "REFUSE_MISSING_TOOL";
        case Refusal::MissingRecipe:                 return "REFUSE_MISSING_RECIPE";
        case Refusal::CarryWeight:                   return "REFUSE_CARRY_WEIGHT";
        case Refusal::Count:                         break;
    }
    return "?";
}

namespace {

// ===========================================================================
// THE REGISTRY
//
// Ordered allowed-first so ForItem() naturally returns the usable route ahead
// of the ones that merely exist. Refused and unknown rows are kept on purpose:
// a registry that lists only what works cannot be audited, and "we looked at
// this and said no" is the most useful row in it.
// ===========================================================================
const GoldFaucet kFaucets[] = {

// --- ALLOWED ---------------------------------------------------------------

{"dagger_to_weaponsmith", SourceType::VendorSale, "miner_smith",
 "i_dagger", "weaponsmith",
 HistoryEvidence::Unknown, RuntimeEvidence::ScriptSupported, Policy::Allow,
 "OWNER DECISION, 2026-08-29: \"brannoc he can make gold by selling daggers he "
 "crafted, go mine then forge then make dagger and sell them\". This is the "
 "call the smith_output_to_vendor entry below was explicitly waiting on -- it "
 "refuses i_spear_short saying no Revolution evidence establishes NPC buyback "
 "for general smith output, and the risk it names is real: mine -> smith -> "
 "dump to NPC prints gold from nothing. What makes a DAGGER the narrow "
 "exception rather than the thin end of it: SKILLMAKE=Blacksmithing 0.0, so "
 "it is the piece a smith with no skill can actually make on day one, and it "
 "is the whole point of the loop the owner described. VENDOR_B_WEAPONS_BLADED "
 "carries the BUY row, so the mechanism is the shard's own. The gold rate is "
 "bounded by the ore the character has to dig first, which is the shape of a "
 "faucet rather than a fountain. i_spear_short stays REFUSED"},

{"poison_potion_to_alchemist", SourceType::VendorSale, "alchemist",
 "i_potion_poison", "alchemist",
 HistoryEvidence::Unknown, RuntimeEvidence::ScriptSupported, Policy::Allow,
 "OWNER DECISION, 2026-08-29: \"Voris it can make poison bottle and it can "
 "sell to npc\". FLAGGED AT THE TIME and recorded here so the conflict is not "
 "lost: the alchemy_output_to_vendor entry below refuses potion sales on "
 "CONFIRMED Revolution evidence -- potions were a player-market good and on "
 "18.08.2009 Night Sight potions STOPPED being sold by NPCs, which is the "
 "shard deliberately pushing the other way. That evidence still stands and is "
 "not being deleted. What limits the damage: VENDOR_B_ALCHEMIST pays 3 gold "
 "for i_potion_Poison (tm_vend.scp), which against the reagent cost is close "
 "to break-even, so this is a training sink and a trickle of coin rather than "
 "an income loop. If it turns out to pay, it should be refused again"},

{"fish_raw_to_fisher", SourceType::VendorSale, "fisher",
 "i_fish_big_1", "fisher",
 HistoryEvidence::Confirmed, RuntimeEvidence::ScriptSupported, Policy::Allow,
 "Revolution documented fish as sellable to NPC vendors, cooked or not; "
 "VENDOR_B_FISHER carries the BUY rows (tm_vend.scp:1022-1027, {4 24})"},

{"fish_big_2_to_fisher", SourceType::VendorSale, "fisher",
 "i_fish_big_2", "fisher",
 HistoryEvidence::Confirmed, RuntimeEvidence::ScriptSupported, Policy::Allow,
 "the sea yields four kinds of big fish and VENDOR_B_FISHER buys every one "
 "of them (tm_vend.scp:1022-1027, {4 24}); listing only the first left three "
 "catches in four unsellable"},

{"fish_big_3_to_fisher", SourceType::VendorSale, "fisher",
 "i_fish_big_3", "fisher",
 HistoryEvidence::Confirmed, RuntimeEvidence::ScriptSupported, Policy::Allow,
 "the sea yields four kinds of big fish and VENDOR_B_FISHER buys every one "
 "of them (tm_vend.scp:1022-1027, {4 24}); listing only the first left three "
 "catches in four unsellable"},

{"fish_big_4_to_fisher", SourceType::VendorSale, "fisher",
 "i_fish_big_4", "fisher",
 HistoryEvidence::Confirmed, RuntimeEvidence::ScriptSupported, Policy::Allow,
 "the sea yields four kinds of big fish and VENDOR_B_FISHER buys every one "
 "of them (tm_vend.scp:1022-1027, {4 24}); listing only the first left three "
 "catches in four unsellable"},

{"fish_small_to_fisher", SourceType::VendorSale, "fisher",
 "i_fish_small", "fisher",
 HistoryEvidence::Confirmed, RuntimeEvidence::ScriptSupported, Policy::Allow,
 "as the big fish; tm_vend.scp:1022-1027"},

{"fish_steak_to_fisher", SourceType::VendorSale, "fisher",
 "i_fish_cut_raw", "fisher",
 HistoryEvidence::Confirmed, RuntimeEvidence::ScriptSupported, Policy::Allow,
 "THE BEST FISH TRADE THERE IS, and it goes to the cook, not the fisher. "
 "A steak is VALUE=3 against a whole fish's 2, so it pays 2 gold against 1 "
 "(engine: itemdef VALUE less VendorMarkup=15), and it weighs 0.1 stones "
 "against 5.0 -- a hundredfold better per stone carried. Only "
 "VENDOR_B_COOK buys it: VENDOR_B_FISHER's list ends at whole fish "
 "(tm_vend.scp, TNS's tables as installed 2026-08-28). Cutting needs "
 "Cooking 10.0 (i_profession_cook_barkeep_baker.scp:103)"},

{"fish_cooked_to_cook", SourceType::VendorSale, "fisher",
 "i_fish_cut_cooked", "cook",
 HistoryEvidence::Confirmed, RuntimeEvidence::ScriptSupported, Policy::Allow,
 "THE BEST-ATTESTED PRICE IN THE WHOLE FORUM RECORD: 5 gp, recorded twice "
 "two years apart (topics 49757 in 2008 and 76498 in 2010), and both the "
 "cook and the fisherman buy it (owner, 2026-08-28). The runtime now agrees "
 "-- i_fish_cut_cooked VALUE=6 pays exactly 5, and BUY rows were added to "
 "VENDOR_B_FISHER and VENDOR_B_COOK, neither of which bought it before. "
 "History and runtime USED to disagree here and the note is kept: "
 "recorded separately. On TNS cooked fish was the fisher's real money: "
 "references/tns/scripts/Systems/System_SellerBuro.scp:420-445 pays 5gp a "
 "cooked steak and decays that to 3, 2 then 1 as the shard-wide daily count "
 "passes 500k, 1M and 2M. That script is a DONOR reference and is NOT "
 "installed in runtime/scripts. On the shard as it actually boots there is "
 "no BUY row for i_fish_cut_cooked anywhere in the tree, so cooking is a "
 "gold dead end here until the system is installed. Cite the donor path, "
 "never a bare filename: the earlier wording read as if the running shard "
 "carried it"},

{"scroll_to_mage_shop", SourceType::VendorSale, "mage",
 "i_scroll_poison", "mage",
 HistoryEvidence::Confirmed, RuntimeEvidence::LiveProven, Policy::Allow,
 "CONFIRMED, and the wording matters because a heuristic was overruling this "
 "row. Revolution's own players describe the channel directly -- \"people "
 "were scribing scroll and selling scrolls to vendor make money\" -- and "
 "scribing is one of the three named taps where gold enters this shard, "
 "beside fishing and mob loot (docs/REVOLUTION_ECONOMY_FORUM_EVIDENCE.md). "
 "This row read NOT_FULLY_CONFIRMED on the grounds that the archive was "
 "vaguer than for fishing; it is not, and while it did, the closed-loop test "
 "in MaySellToNpc refused every scroll a scribe wrote because it had bought "
 "the blanks from the same mage. The margin pays for the skill and the time: "
 "a blank costs 6 (topic 88176) and a resurrection scroll fetched 167-170 "
 "(topic 49757)"},

{"scroll_recall_to_mage_shop", SourceType::VendorSale, "mage",
 "i_scroll_recall", "mage",
 HistoryEvidence::NotFullyConfirmed, RuntimeEvidence::LiveProven, Policy::Allow,
 "as scroll_to_mage_shop; tm_vend.scp:1881 VENDOR_B_MAGE_4TH {10 15}"},

{"bow_to_bowyer", SourceType::VendorSale, "bowyer",
 "i_bow", "bowyer",
 HistoryEvidence::Confirmed, RuntimeEvidence::ScriptSupported, Policy::Allow,
 "Revolution's own Bowcraft guidance states crafted bows could be sold to "
 "other players OR to NPC vendors -- an explicit NPC channel, which is what "
 "separates this from the other crafting trades"},

{"crossbow_to_bowyer", SourceType::VendorSale, "bowyer",
 "i_crossbow", "bowyer",
 HistoryEvidence::Confirmed, RuntimeEvidence::ScriptSupported, Policy::Allow,
 "same guidance; VENDOR_B_BOWYER buys it (tm_vend.scp:1444)"},

{"monster_gold", SourceType::MonsterLoot, nullptr,
 nullptr, "corpse",
 HistoryEvidence::Confirmed, RuntimeEvidence::ScriptSupported, Policy::Allow,
 "a Revolution changelog explicitly adjusted gold drops from strong "
 "creatures, so the gold was intended to be there. It is CREATED, not "
 "transferred: it did not come out of another player's purse"},

// --- REFUSED, and each says WHICH kind of no -------------------------------

{"smith_output_to_vendor", SourceType::VendorSale, "miner_smith",
 "i_spear_short", "weaponsmith",
 HistoryEvidence::Unknown, RuntimeEvidence::ScriptSupported,
 Policy::RefuseAuthenticity,
 "the stock weaponsmith template buys it, and that is a fact about Sphere. "
 "No Revolution evidence establishes NPC buyback for general smith output, "
 "and allowing it means mine -> smith endlessly -> dump to NPC -> print gold. "
 "A smith without an NPC faucet has to reach players, hunt, or find another "
 "route, and that is the intended shape rather than a gap"},

{"carpentry_output_to_vendor", SourceType::VendorSale, "lumberjack_swordsman",
 "i_club", "weaponsmith",
 HistoryEvidence::Unknown, RuntimeEvidence::ScriptSupported,
 Policy::RefuseAuthenticity,
 "no direct Revolution evidence that generic carpentry output was an NPC gold "
 "faucet. Player-market usage stays valid. NOTE: this row REVERSES an earlier "
 "allowance in this repository -- the club was permitted on a general "
 "finished-goods rule, and the later, more specific instruction refuses it"},

{"tailor_output_to_vendor", SourceType::VendorSale, "tailor",
 "i_robe", "tailor",
 HistoryEvidence::Unknown, RuntimeEvidence::ScriptSupported,
 Policy::RefusePlayerMarket,
 "cloth, bolts and robes -- especially mage and special robes -- carry strong "
 "player-market context on Revolution. Generic tailor buyback is an economic "
 "shortcut around that market"},

{"tinker_output_to_vendor", SourceType::VendorSale, "tinker",
 "i_gears", "tinker",
 HistoryEvidence::Unknown, RuntimeEvidence::ScriptSupported,
 Policy::RefusePlayerMarket,
 "trapped pouches, tools, gears and golems are player goods; some explicitly "
 "so. No NPC income loop without evidence for a specific exception"},

{"alchemy_output_to_vendor", SourceType::VendorSale, "alchemist",
 "i_potion_cure", "alchemist",
 HistoryEvidence::Confirmed, RuntimeEvidence::ScriptSupported,
 Policy::RefusePlayerMarket,
 "CONFIRMED as a player-market good, and the two shards genuinely differ "
 "here. TNS scripts an NPC buyback -- VENDOR_B_ALCHEMIST names Refresh, "
 "Agility, Night Sight, Heal, Strength, Poison, Cure, Explosion, the Greater "
 "variants, Deadly Poison and Total Refresh -- so the mechanism exists and "
 "is not in doubt; what it paid is not in the public pack. REVOLUTION pushed "
 "the other way, deliberately and on the record: on 18.08.2009 Night Sight "
 "potions STOPPED being sold by NPCs. Demand was between players and it was "
 "large -- a 2008 Revolution warrior describes carrying roughly 150 Deadly "
 "Poison, 80 Heal and 80 Cure in kegs for a large battle, which makes every "
 "PvPer, PvMer, tamer, mage and warlock a repeat customer. The February 2011 "
 "update let a Store Crystal of 250 identical potions be listed in the "
 "Tezgahtarlar Kooperatifi, using 250 Heal as its worked example. NOTE: the "
 "'.fiyat 50000' in that example demonstrates the LISTING SYSTEM and is not "
 "evidence of a price -- do not quote it as one (owner, 2026-08-28)"},

{"ingot_to_vendor", SourceType::VendorSale, "miner_smith",
 "i_ingot_iron", "blacksmith",
 HistoryEvidence::Unknown, RuntimeEvidence::ScriptSupported,
 Policy::RefusePlayerMarket,
 "Revolution named iron ingots as a player-market good. An NPC price sets a "
 "floor under it and nobody pays a player more than the floor"},

{"log_to_vendor", SourceType::VendorSale, "lumberjack_swordsman",
 "i_log", "carpenter",
 HistoryEvidence::Unknown, RuntimeEvidence::ScriptSupported,
 Policy::RefusePlayerMarket,
 "owner, 2026-08-28: 'you cant sell logs to npc only to players'. "
 "VENDOR_B_CARPENTER does carry BUY=i_log,{5 15} (tm_vend.scp:167), which is "
 "exactly why technical acceptance is not authenticity"},

{"board_to_vendor", SourceType::VendorSale, "lumberjack_swordsman",
 "i_board", "carpenter",
 HistoryEvidence::Unknown, RuntimeEvidence::ScriptSupported,
 Policy::RefusePlayerMarket,
 "a material, like the log it came from"},

{"blank_scroll_to_vendor", SourceType::VendorSale, "lumberjack_swordsman",
 "i_scroll_blank", "mage",
 HistoryEvidence::Unknown, RuntimeEvidence::ScriptSupported,
 Policy::RefusePlayerMarket,
 "the mage shop buys them at {10 15} (tm_vend.scp:671) against a log worth 1, "
 "which would be a 10x faucet out of one gathering skill. It is also exactly "
 "what a scribe needs from a carpenter, so it belongs to the player market"},

// --- CONFIRMED HISTORY, NOT YET USABLE HERE --------------------------------

{"treasure_gold", SourceType::Treasure, nullptr,
 nullptr, "treasure chest",
 HistoryEvidence::Confirmed, RuntimeEvidence::Unverified,
 Policy::BlockedRuntime,
 "Revolution updates explicitly adjusted treasure gold quantities, so the "
 "faucet is authentic. Nothing in this project has yet opened a treasure map "
 "or chest on this runtime, so it stays blocked until live-proven rather than "
 "assumed to work"},

{"head_hunter_bounty", SourceType::Bounty, nullptr,
 nullptr, "head hunter",
 HistoryEvidence::Confirmed, RuntimeEvidence::Unverified,
 Policy::BlockedRuntime,
 "Revolution ran a Head Hunter system: an NPC paid gold for eligible player "
 "heads by the victim's properties. Authentic and Revolution-specific, but "
 "the runtime mechanic has not been verified or restored here"},

// --- UNKNOWN: audit required, refused meanwhile ----------------------------

{"begging", SourceType::Begging, nullptr,
 nullptr, "npc or player",
 HistoryEvidence::Confirmed, RuntimeEvidence::Unverified, Policy::Unknown,
 "Revolution's guide describes Begging as generating small amounts of gold. "
 "The RUNTIME behaviour is unaudited, and it matters which it is: begging an "
 "NPC would CREATE gold, begging a player would TRANSFER it, and the two must "
 "never be recorded as the same thing"},

{"monster_loot_resale", SourceType::VendorSale, nullptr,
 nullptr, "various",
 HistoryEvidence::Unknown, RuntimeEvidence::ScriptSupported, Policy::Unknown,
 "weapons and armour off corpses are widely bought by stock templates. "
 "Whether Revolution intended corpse-loot resale as an income strategy is "
 "unestablished, and it is the classic route by which a hunting bot becomes a "
 "vendor-dumping bot"},

{"gem_to_jeweler", SourceType::VendorSale, nullptr,
 nullptr, "jeweler",
 HistoryEvidence::Unknown, RuntimeEvidence::ScriptSupported, Policy::Unknown,
 "the jeweler template buys gems. No Revolution statement either way yet"},
};

}  // namespace

const std::vector<GoldFaucet>& All() {
    static const std::vector<GoldFaucet> kAll = [] {
        std::vector<GoldFaucet> v;
        v.reserve(sizeof(kFaucets) / sizeof(kFaucets[0]));
        for (const GoldFaucet& f : kFaucets) v.push_back(f);
        return v;
    }();
    return kAll;
}

std::vector<const GoldFaucet*> ForItem(const char* item) {
    std::vector<const GoldFaucet*> out;
    if (!item) return out;
    for (const GoldFaucet& f : All()) {
        if (!f.input) continue;
        if (std::strcmp(f.input, item) == 0) out.push_back(&f);
    }
    return out;
}

const GoldFaucet* AllowedForItem(const char* item) {
    for (const GoldFaucet* f : ForItem(item)) {
        if (Allowed(f->policy)) return f;
    }
    return nullptr;
}

std::vector<const GoldFaucet*> ForProfession(const char* professionId) {
    std::vector<const GoldFaucet*> out;
    if (!professionId) return out;
    for (const GoldFaucet& f : All()) {
        if (!Allowed(f.policy)) continue;
        // A faucet with no profession (monster gold) is open to anybody who
        // can survive the fight.
        if (!f.profession || std::strcmp(f.profession, professionId) == 0) {
            out.push_back(&f);
        }
    }
    return out;
}

}  // namespace uo::faucet
