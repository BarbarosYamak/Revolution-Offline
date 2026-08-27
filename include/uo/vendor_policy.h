#pragma once

// ---------------------------------------------------------------------------
// NPC vendor authenticity policy (M3.7 Phase 19) and acquisition choice
// (Phase 21).
//
// THE PROBLEM THIS SOLVES
//
// The M3.7 vendor audit enumerated every item a stock Sphere NPC will sell on
// this shard: 608 of them across 32 working human professions. Among them are
// iron ingots, logs, boards, wool, yarn, thread, cloth, bolts of cloth, cotton,
// flax, hides, blank scrolls, bottles, feathers, nails, gears and all
// twenty-six reagents. 284 of the 608 -- 47% -- are goods a player can make.
//
// So a bot with gold can skip mining, smelting, shearing, spinning, weaving and
// chopping entirely, and RevolutionUO's player economy evaporates. Preventing
// that is the whole of M3.7.
//
// WHERE THE ENFORCEMENT LIVES, AND WHY
//
// HERE, in the bot -- not in the shard. Three reasons, in order of weight:
//
//   1. It keeps the server a NEUTRAL FACT. If we edited vendor stock, every
//      later authenticity claim would be measuring our own edits. With the
//      shard untouched, "the bot refused to buy wool" is falsifiable against a
//      server that would happily have sold it.
//   2. M3.7 forbids Source-X and Scripts-X modifications, and this is the
//      reason that rule is right rather than merely a constraint.
//   3. A human player on this shard keeps the stock economy. Only the
//      autonomous Revolution bots are held to the reconstruction.
//
// UNKNOWN FAILS SAFE, AND FAILS LOUDLY
//
// The audit's largest class after PLAYER_CRAFTED is UNKNOWN: 177 items with no
// Revolution evidence either way, including -- consequentially -- the eight
// Magery reagents, blank scrolls and bottles. Refusing them is the safe
// reading, but a silent refusal would turn a research gap into a mystery. So
// every refusal reports the item and its class, and the accumulated list of
// UNKNOWN refusals IS the research backlog.
// ---------------------------------------------------------------------------

#include "uo/types.h"
#include "uo/production.h"

#include <string>
#include <vector>

namespace uo::econ {

// How the M3.7 audit graded an item's presence on a stock NPC vendor. Values
// are per-item facts from docs/REVOLUTION_VENDOR_MATRIX.md.
enum class VendorClass : u8 {
    Unknown = 0,          // 177 items. No evidence either way. Fails safe
    RevolutionNpcVerified,// a DATED Revolution entry says an NPC sold it
    PlayerMarketGood,     // a RevolutionUO cooperative search category
    // A BASIC CRAFT TOOL. Permitted, and the reasoning is deliberate rather
    // than convenient: a tool is not a resource. Buying a shovel shortcuts no
    // chain -- the miner must still find the mountain and swing at it -- and
    // Revolution's guide, which restricts plenty (poisoned weapons, robe
    // skills, net skill), never restricts tool purchase.
    //
    // The live case that forced this class into existence: `i_pickaxe` carries
    // REQSTR=50 while a Fishing/Mining hybrid starts at STR 30, so the shard's
    // own NEWBIE MINING kit hands out a tool its owner cannot lift. `i_shovel`
    // has no REQSTR and is the same t_weapon_mace_pick. Without a purchasable
    // tool, a legitimately-built miner simply cannot mine.
    BasicCraftTool,
    // BASIC FOOD. Permitted, and forced into existence by M3.8 enabling hunger.
    //
    // With HitsHungerLoss=1 a character that cannot buy food eventually dies,
    // and death on this shard is full loot loss. The first hunger run proved the
    // problem exactly: a bot stood in front of a baker holding 39 sacks of
    // flour and 32 french breads and was refused, because a loaf is not in the
    // audited matrix and Unknown fails safe.
    //
    // The permission is EVIDENCED, not convenient. M3 measured the cooking
    // economy and found "Cooking is a food and skill-gain activity, not an
    // income multiplier" -- cooked fish is worth LESS than raw and loses every
    // buyer. So a baker selling bread undercuts no real player market, which is
    // the whole test this class exists to apply. Compare BasicCraftTool: a tool
    // is not a resource, and food is not a trade good.
    //
    // It stays narrow deliberately: staples a character eats, not prepared
    // goods a cook would sell for profit.
    BasicFood,
    WorldGathered,        // a gathering skill produces it
    WorldProcessed,       // a station transforms it
    PlayerCrafted,        // a live skill menu makes it
    PvmTreasure,
    StockSphereOnly,      // present only because Scripts-X ships it
    EraConflict,          // later than revolution_2009_2010
    Count,
};

const char* VendorClassName(VendorClass c);

struct VendorRuling {
    bool        allowed = false;
    VendorClass klass   = VendorClass::Unknown;
    // Always populated, allowed or not. A refusal a human cannot read is a bug.
    const char* reason  = nullptr;
    // True when this refusal is an authenticity GAP rather than a decision --
    // i.e. we do not know what Revolution did. Callers should log these.
    bool authenticityGap = false;
};

// The classification the audit assigned, or Unknown.
VendorClass ClassifyForVendor(const char* item);

// May an autonomous Revolution bot buy `item` from an NPC?
VendorRuling CanUseNPCVendorFor(const char* item);

// Every item the audit graded, for tests and for reporting.
const std::vector<std::pair<const char*, VendorClass>>& VendorMatrix();

// The same two questions keyed by the item GRAPHIC, which is what a vendor
// offer actually carries on the wire. This is the form the live buy path uses:
// a bot never sees "i_wool", it sees 0x0DF8 in a 0x74 price list.
//
// An unmapped graphic returns Unknown, which refuses -- the same fail-safe
// default as an unlisted defname.
VendorClass  ClassifyForVendorGraphic(u16 graphic);
VendorRuling CanUseNPCVendorForGraphic(u16 graphic);

// The defname a graphic maps to, or nullptr. For logging a refusal in words.
const char*  ItemNameForGraphic(u16 graphic);

// --- acquisition -----------------------------------------------------------

// Every legitimate way to obtain something. Ordered roughly by how much of the
// world the character has to touch.
enum class Acquisition : u8 {
    None = 0,
    AlreadyHeld,
    Gather,          // a gathering skill, in the world
    Process,         // a station transforms what is already held
    Craft,           // a skill menu
    NpcPurchase,     // only where the policy above permits it
    PlayerPurchase,  // a player vendor
    DirectTrade,     // secure trade with another character
    Pvm,
    Treasure,
    Tame,
    Count,
};

const char* AcquisitionName(Acquisition a);

// What the caller knows right now. No I/O, no prices fetched here: the caller
// supplies what it has observed, which is how `econ::PriceBook`'s
// live-vs-historical separation stays intact.
struct AcquisitionContext {
    prod::Capability          capability;
    std::vector<prod::Ingredient> inventory;
    i32  gold = 0;
    // An NPC quote actually observed in a vendor window, or -1. Never an
    // itemdef VALUE and never a historical forum price.
    i32  observedNpcPrice = -1;
    // A price seen in a completed player trade or a player vendor, or -1.
    i32  observedPlayerPrice = -1;
    // Gold the character will not spend -- reserve for travel, reagents, a
    // rebuy after death.
    i32  goldReserve = 0;
};

struct AcquisitionPlan {
    Acquisition method = Acquisition::None;
    const char* reason = nullptr;
    // When Craft or Process: what still has to be obtained first.
    std::vector<prod::Requirement> blockers;
    // When the method is a purchase: what it will cost.
    i32 estimatedCost = 0;
    // True when no legal method exists. The bot should say so, not improvise.
    bool blocked = false;
};

// Choose how to obtain one unit of `item`. Only historically legal options are
// ever returned -- an NPC purchase appears only if CanUseNPCVendorFor allows it.
AcquisitionPlan ChooseAcquisitionMethod(const char* item,
                                        const AcquisitionContext& ctx);

// --- needs and offers (Phase 16 foundation) --------------------------------
//
// Deliberately NOT autonomous negotiation -- that is M4. These are the nouns a
// deterministic proof needs so that "Miner sells ingots to Smith" can be
// expressed and checked rather than hard-coded into a scenario.

struct ResourceNeed {
    std::string item;
    i32         qty = 0;
    i32         urgency = 0;    // higher is sooner; a plain sort key
    std::string forPurpose;     // "craft i_dagger", "train Blacksmithing"
};

struct SellOffer {
    std::string item;
    i32         qty = 0;
    i32         askPerUnit = 0;
    std::string seller;
};

struct BuyOffer {
    std::string item;
    i32         qty = 0;
    i32         bidPerUnit = 0;
    std::string buyer;
};

// A supplier a character has actually dealt with or seen. Not a global market
// view: a bot may not know a price it has never observed.
struct KnownSupplier {
    std::string who;
    std::string item;
    i32         lastPricePerUnit = 0;
    i64         lastSeenMs = 0;
    bool        isNpc = false;
};

// Match needs against offers. Deterministic and total: every need either gets
// a match or is reported unmatched, so a caller can never mistake silence for
// success.
struct Match {
    usize needIndex  = 0;
    usize offerIndex = 0;
    i32   qty = 0;
    i32   pricePerUnit = 0;
};

std::vector<Match> MatchNeeds(const std::vector<ResourceNeed>& needs,
                              const std::vector<SellOffer>& offers,
                              std::vector<usize>* unmatched);

} // namespace uo::econ
