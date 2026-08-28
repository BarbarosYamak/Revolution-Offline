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

{"fish_raw_to_fisher", SourceType::VendorSale, "fisher",
 "i_fish_big_1", "fisher",
 HistoryEvidence::Confirmed, RuntimeEvidence::ScriptSupported, Policy::Allow,
 "Revolution documented fish as sellable to NPC vendors, cooked or not; "
 "VENDOR_B_FISHER carries the BUY rows (tm_vend.scp:1022-1027, {4 24})"},

{"fish_steak_to_fisher", SourceType::VendorSale, "fisher",
 "i_fish_cut_raw", "fisher",
 HistoryEvidence::Confirmed, RuntimeEvidence::ScriptSupported, Policy::Allow,
 "same guide statement; the fisher buys steaks and whole fish both (owner, "
 "2026-08-28)"},

{"fish_cooked_to_cook", SourceType::VendorSale, "fisher",
 "i_fish_cut_cooked", "cook",
 HistoryEvidence::Confirmed, RuntimeEvidence::ScriptSupported, Policy::Allow,
 "cooked fish pays about 1gp more than raw; VENDOR_B_COOK BUY rows at "
 "tm_vend.scp:730-735. TNS's own saturating buyer pays 5gp until daily volume "
 "passes 500,000 (System_SellerBuro.scp:426-440), which corroborates the number"},

{"scroll_to_mage_shop", SourceType::VendorSale, "mage",
 "i_scroll_poison", "mage",
 HistoryEvidence::NotFullyConfirmed, RuntimeEvidence::LiveProven, Policy::Allow,
 "LIVE PROVEN on this shard -- a bot sold scribed scrolls to a mage shop and "
 "the purse moved. Revolution describes Inscription as an income profession "
 "but the archival statement about the NPC channel is weaker than the one for "
 "fishing, so the history stays NOT_FULLY_CONFIRMED. Live reality is not "
 "downgraded because the archive is vague"},

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
 HistoryEvidence::Unknown, RuntimeEvidence::ScriptSupported,
 Policy::RefusePlayerMarket,
 "potions and kegs are economically important player goods. The alchemist's "
 "role is to supply PvPers, mages and PvM players -- not to be an NPC money "
 "printer"},

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
