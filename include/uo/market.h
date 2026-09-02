#pragma once

// M7 -- what one character has to sell, what it has to buy, and the only
// prices it is allowed to know.
//
// The rule this file exists to enforce: A BOT MAY NOT KNOW THE MARKET. There
// is no global price index, no shard-wide inventory, no "cheapest vendor"
// query. A character knows a price because it stood in front of a vendor and
// read it, or because another character quoted it. That is the whole source.
//
// Everything here is a pure function of a profession record and what the
// character is carrying, so it runs under ctest with no server.

#include "uo/faucets.h"
#include "uo/professions.h"
#include "uo/types.h"

#include <string>
#include <vector>

namespace uo::market {

// One line of a pack, by itemdef defname (the same keys the production graph
// and the profession catalogue use).
struct Stock {
    std::string item;
    i32         qty = 0;
};

i32 QtyOf(const std::vector<Stock>& pack, const std::string& item);

// Something this character will hand over. Quantity only -- the price comes
// from PriceBook, which only knows what was observed.
struct Offer {
    std::string item;
    i32         qty = 0;
    // Why it is spare, printable, so a trade can be explained afterwards.
    std::string reason;
};

// Something this character needs and cannot make.
struct Want {
    std::string item;
    i32         qty = 0;
    // True when NO profession in the catalogue produces it -- the world does,
    // so a player supplier is impossible and a vendor is the only option.
    bool        rawResource = false;
    std::string reason;
};

// How much of its own output a life keeps back before selling any. A smith
// that sells every ingot cannot smith tomorrow.
struct TradePolicy {
    i32 keepOfOwnOutput = 20;
    i32 restockConsumablesTo = 20;
    // Never pay more than this multiple of what the character believes the
    // thing is worth. Without a ceiling a bot with 1000gp will accept any
    // number, and one greedy seller drains the fleet.
    double maxOverBelief = 2.0;
    // With no belief at all, this is the most a character will pay per unit
    // for something it genuinely needs. Small on purpose: a first purchase is
    // how you LEARN a price, not where you spend a fortune.
    i32 blindPriceCeiling = 12;
    // What a seller asks when it has NEVER seen a price for the thing.
    //
    // This is NOT a claim to know a value, and the distinction matters because
    // the rest of this file is built on refusing to invent one.
    // BelievedSalePrice still returns -1 with no observation, because a belief
    // without evidence is a lie. An OPENING ASK is different: it is an offer,
    // and the market either takes it or does not.
    //
    // Without it nothing can bootstrap. Every price in a PriceBook arrives by
    // a trade completing, no trade completes without somebody naming a number
    // first, and a fleet where nobody will ever name one has no market at all
    // -- which is precisely what the first implementation produced.
    //
    // Deliberately LOW. A first sale is how you learn a price, not where you
    // make your money, and asking under the odds is the fast way to find the
    // real number.
    i32 openingAsk = 2;
    // A LOT WORTH WALKING WITH -- normally. Five keeps a character from
    // trekking across town to sell two of something.
    //
    // But it must not become a reason to stand still while broke. Voris held
    // 2 poison potions, 2 empty bottles and exactly 100 gold: he could not buy
    // bottles (BUY_SUPPLIES keeps a hard floor of 100), could brew at most two
    // more, and would still have been under this threshold -- so he meditated
    // instead of selling the only thing he owned. Use PolicyForPurse() below
    // rather than the bare default wherever a life might be broke.
    i32 minimumSurplusToOffer = 5;
};

// THE SHARD'S MARKET -- one deterministic point for the whole fleet.
//
// data/revolution_atlas.txt:2108
//   PLACE britain_bank_2 bank a_townBritain 1425 1690 0 5 banker
// which atlasgen derived from the shard's own save,
// runtime/save/sphereb01w.scp:498579 and :498592 -- two t_custom_spawner_char
// worldgems at P=1425,1690, SPAWNID=c_banker and SPAWNID=c_minter. The region
// a_townBritain carries flag 0x1 (kFlagGuarded, src/world/Atlas.cpp:73,85) and
// RECT a_townBritain 1410 1517 1690 1777 (atlas:903) contains the tile, so
// ending a session here satisfies the never-log-out-in-the-open rule.
//
// WHY THIS BANK AND NOT britain_bank (atlas:2106, 1650,1608):
//   * REVOLUTION_ECONOMY_FORUM_EVIDENCE.md:372-374 -- "Britain was
//     overwhelmingly the trade hub" across ~530 market threads.
//   * :385 (class S, staff) -- "Britain | Provisioner east of the bank".
//     britain_provisioner_2 is at 1469,1668 (atlas:2102): 44 tiles EAST of
//     1425,1690 and 181 tiles WEST of 1650,1608. Only the west bank fits.
//   * :387 (S) -- coop officers "near the banks" in Britain.
//   * It is the cheaper arrival from the Britain moongate.
//
// AN ID, NEVER COORDINATES. TravelToService(Banker, "Britain") would pick the
// NEARER of the two Britain banks from the caller's position
// (Atlas::NearestPlaceWithServiceInRegion), so a smith arriving from Minoc
// lands at 1650,1608 while a Britain resident stands at 1425,1690 and the
// rendezvous fails 250 tiles apart. A rendezvous has to be ONE place.
constexpr const char* kMarketBankPlaceId = "britain_bank_2";

// THE THRESHOLD BENDS WHEN THE PURSE IS EMPTY. A small lot is not worth a trip
// when there is money to work with; when there is not, the small lot IS the
// way back to work. "sell those poisons buy more bottle" (project owner,
// 2026-08-30).
inline TradePolicy PolicyForPurse(i32 goldOnHand) {
    TradePolicy p;
    if (goldOnHand < 200) p.minimumSurplusToOffer = 1;
    return p;
}

// What this life can spare. Reads `produces` from the catalogue: a life only
// ever offers what its own profession makes.
std::vector<Offer> Surplus(const prof::Profession& p,
                           const std::vector<Stock>& pack,
                           const TradePolicy& policy);

// What this life is short of. Reads `consumes` and `consumables`.
std::vector<Want> Shortfall(const prof::Profession& p,
                            const std::vector<Stock>& pack,
                            const TradePolicy& policy);

// WHAT THIS LIFE WOULD WALK TO A PLAYER MARKET TO BUY.
//
// A MARKET HAS TWO SIDES. Only the seller's was ever modelled: a life with a
// surplus had a reason to go, a life 20 logs short of the spear it wants to
// forge had none, so half of every trade was unreachable and no smith ever
// scored TRADE_WITH_PLAYER at all.
//
// Shortfall() already marks which wants a player could possibly supply.
// `rawResource` means WhoProduces() is empty -- the WORLD makes the thing, so
// the errand is gathering or a vendor, not a rendezvous. Iron ore is exactly
// that (no profession lists it in `produces`); a log is not
// (lumberjack_swordsman produces it), which is the one live producer-consumer
// edge in the catalogue.
//
// Affordability is asked HERE, before the journey, with the same refusal
// ConsiderOffer makes on arrival (Market.cpp): can it pay for ONE at the worst
// price it would accept -- `blindPriceCeiling` -- without eating the reserve
// its profession keeps for tools? A life that cannot stays home, which is the
// requested "nothing to sell and nothing it can afford does not go".
//
// `whyNotOut`, when given, receives the refusal whenever the list comes back
// empty, so the caller can say why rather than merely being silent.
std::vector<Want> PlayerMarketWants(const prof::Profession& p,
                                    const std::vector<Stock>& holdings,
                                    i32 gold,
                                    const TradePolicy& policy,
                                    const char** whyNotOut = nullptr);

// CAN THIS LIFE AFFORD TO ASK AT ALL? The capital gate PlayerMarketWants
// applies before it will name a single want: one unit at the blind ceiling
// without eating the reserve the profession keeps for tools.
//
// Exposed because "the market said no" and "this life cannot afford to walk
// to the market" are different facts, and a need that gates on the first was
// waiting forever for the second. A tailor with gold=0 returns an EMPTY want
// list here -- not because nobody sells yarn, but because it could never buy
// any -- so nothing ever announced a WTB and nothing ever wrote
// `no_player_seller` (run_gates/g_Aelia.console.txt:83-710, MAKE_CLOTH
// BLOCKED on "the player market has not been asked for it yet", 20x in a
// 5-minute gate with gold=0 throughout).
bool CanAffordToShop(const prof::Profession& p, i32 gold,
                     const TradePolicy& policy);

// WHERE A MISSING CRAFT INPUT SHOULD ACTUALLY COME FROM.
//
// The buy errand used to know exactly one answer -- an NPC shopkeeper -- and
// treated "no trade sells this" as the end of the road. In the 2026-09-01
// 30-bot wave that verdict fired on three characters holding the wrong end of
// their own production chain: a FISHER short of fish (g_Dorvar:740), a
// MINER_SMITH short of iron ingots (g_Zarthal:488) and an ARCHER short of logs
// (g_Titus:636). Fish and ingots are things those two lives MAKE; a log is
// something lumberjack_swordsman makes and an archer buys from a player. None
// of the three was a lookup that lost a buyer, and none of them should have
// been an NPC errand at all -- materials never go to or come from NPCs, so
// refusing the vendor was right and stopping there was wrong.
//
// This is catalogue reasoning only -- "a smith is the sort of person who makes
// ingots" -- exactly like WhoProduces below. It says nothing about who is
// online or where they are.
enum class SupplyRoute : u8 {
    // A shopkeeper trade is known to sell it. Unchanged from before: the
    // vendor matrix still gets the final say afterwards.
    NpcVendor,
    // This life's own profession produces or gathers it. Go and make it.
    SelfProduce,
    // Another profession in the catalogue produces it. A player supplies it.
    PlayerMarket,
    // Nobody in the catalogue makes it and no NPC trade sells it.
    NoKnownSource,
};

const char* SupplyRouteName(SupplyRoute r);

// `npcTradeKnown` is the caller's own vendor-table lookup (Runner's
// SupplierTradeFor). It is passed in rather than repeated here so the vendor
// table stays in one place.
SupplyRoute RouteForInput(const prof::Profession& me, const char* item,
                          bool npcTradeKnown);

// Which professions in the catalogue produce `item`. This is CATALOGUE
// knowledge -- "a smith is the sort of person who makes ingots" -- which a
// player plainly has. It says nothing about who is online, where they are, or
// what they charge, and it must never be used as if it did.
std::vector<const prof::Profession*> WhoProduces(const char* item);

// The mirror: which professions must OBTAIN `item` from somebody else. Read off
// `consumes`, which means exactly that (a smith's own ingots are correctly
// absent from its own list even though every weapon it makes eats six).
//
// Catalogue knowledge, the same kind WhoProduces is: "a carpenter is the sort
// of person who buys logs". It says nothing about who is online or what they
// hold, and it must never be used as if it did.
std::vector<const prof::Profession*> WhoConsumes(const char* item);

// True when the two lives can trade in one direction: a produces something b
// consumes. The M7 interdependence test.
bool CanSupply(const prof::Profession& producer,
               const prof::Profession& consumer, std::string* itemOut = nullptr);

// ---------------------------------------------------------------------------
// Selling to an NPC. Permitted, but not unconditionally.
// ---------------------------------------------------------------------------

struct Ledger;   // defined below

struct SellRuling {
    bool        allowed = false;
    const char* reason  = nullptr;   // always populated, allowed or not
    // WHICH KIND of no. "cannot earn gold" is never an acceptable answer when
    // the truth is "found a buyer, but this is a player-market good".
    faucet::Refusal refusal = faucet::Refusal::None;
    // The registry row that decided it, allowed or refused, so the reasoning
    // is traceable to its evidence rather than to a branch in the code.
    const faucet::GoldFaucet* via = nullptr;
};

// WHERE NEW GOLD ENTERS THE SHARD.
//
// The buying question and the selling question are NOT the same question, and
// answering the second with the first's table was wrong. A spell scroll is
// PlayerCrafted -- a bot must never BUY one from an NPC, because that
// shortcuts the whole scribing profession -- and yet Revolution scribes made
// their living SELLING scrolls to vendors. Both are true at once.
//
// Project owner, 2026-08-28, describing Revolution directly:
//
//   "you cant sell logs to npc only to players"
//   "whole economy based on players mostly except fishing, killing mobs,
//    scribe etc"
//   "people were scribing scroll and selling scrolls to vendor make money or
//    caught fish cook fish then sell"
//
// So the shard has a few narrow taps where gold is created, and everything
// else is players trading with players. This enumerates the taps. Anything
// not listed is UNKNOWN and refused, and the accumulated refusals are the
// research backlog -- the same discipline the buying side has followed since
// M3.7.
// THE LINE, as the owner drew it (2026-08-28): MATERIALS go to players,
// FINISHED GOODS may go to an NPC.
//
// That is a coherent rule rather than a list of exceptions. A crafted item
// carries player labour -- gathering, a skill, a failure rate -- so the gold an
// NPC pays for it is bought with real time. A raw material carries none, and an
// NPC price for it just sets a floor that kills the player market underneath.
//
// Owner's research table, same date, with the ALLOW column as decided:
//
//   raw fish, cooked fish        ALLOW  Revolution guide, explicit
//   cooked animal food products  ALLOW  14 Apr 2008 update
//   crafted bows                 ALLOW  guide: "players or NPC vendors"
//   scrolls                      ALLOW  proven live on this shard
//   carpentry, smithing,
//   tailoring, tinkering,
//   alchemy                      ALLOW  owner decision; the historical NPC
//                                       channel is NOT confirmed, and that is
//                                       recorded rather than dressed up
//   raw logs, iron ingots        REFUSE Revolution listed both as player-market
enum class NpcSellClass : u8 {
    Unknown = 0,      // no evidence and no decision. Refused.
    // --- taps: an NPC pays, and new gold enters the shard ---------------
    Fish,             // raw or cooked; the fisher takes both
    CookedFood,       // animal food products (14 Apr 2008)
    MobLoot,          // what a corpse yielded
    ScribedScroll,    // a scribe's stock in trade
    CraftedGood,      // carpentry, smithing, tailoring, tinkering, alchemy,
                      // bowcraft -- a finished item, not a material
    // --- refused, and the refusal says which kind ------------------------
    RawResource,      // logs, boards, ore, hides -- materials, not goods
    PlayerMarketGood, // ingots and anything Revolution named as player-traded
    Count,
};

const char* NpcSellClassName(NpcSellClass c);

// How Revolution treated SELLING this to an NPC.
NpcSellClass ClassifyForNpcSale(const char* item);

// May this life sell `item` to an NPC vendor?
//
// Selling to an NPC CREATES gold -- the vendor pays from nowhere -- so it is
// the one flow that can print money. What makes it legitimate is a real
// player-side loss on the other side: the log came out of a tree the character
// had to find and swing at, and that time is the cost.
//
// What makes it ILLEGITIMATE is buying the inputs from an NPC too. Then the
// cycle is vendor -> craft -> vendor with no world contact at all, and
// tools/economy_arbitrage.py finds 66 such loops on this shard's own itemdefs
// (a cake returns +16.6 gp at 2.44x, a crossbow +20.0 at 3.48x). The audit's
// invariant states it directly:
//
//   No deterministic NPC/vendor/crafting/recycling cycle may generate net gold
//   or resources without a player-side loss.
//
// So the ledger is the evidence: if this character bought a raw input of
// `item` from an NPC, selling the result back to one is refused.
//
// `playersDeclined` opens the NPC PRICE FLOOR for MATERIALS (owner ruling,
// 2026-09-02; see econ::MaterialFloorOpen). It defaults to false so every
// existing caller keeps the strict answer: the floor is a fallback for after
// the WTS window has closed unanswered, never a first choice. Even with it
// true the floor only opens where a live BUY row exists -- otherwise the item
// banks -- and the anti-arbitrage ledger test below still applies unchanged.
SellRuling MaySellToNpc(const prof::Profession& p, const char* item,
                        const Ledger& ledger, bool playersDeclined = false);

// ---------------------------------------------------------------------------
// THE MATERIAL SURPLUS CAP (owner ruling, 2026-09-02)
//
//   "materials exist to be CRAFTED, not sold. NPC sale of materials ONLY when
//    bank+pack exceeds a plan-derived surplus cap, dynamic per character. Last
//    resort, never default."
//
// MaySellToNpc answers "is this class of good sellable at all". It is a fact
// about the item and the shard. This answers a different question -- "does THIS
// character, with THIS build plan and THIS purse, actually have more of the
// stuff than its own future needs" -- and the two are ANDed on the sell side.
//
// NO GLOBAL CONSTANT. "keep/bank/surplus counts derive from plans, wealth and
// prices per character, not global constants" (project owner). Every term below
// is read off something that differs between two characters:
//
//   cap = ownPlanNeed + trainingStock + marketReserve
//
//   ownPlanNeed   = craftBatch x (the largest quantity of `item` any ONE of
//                   this profession's own recipes eats). What the next sitting
//                   at the bench consumes. Zero when its recipes never eat it,
//                   which is the ordinary gatherer case.
//
//   trainingStock = the owner's stocking rule, scaled to what is LEFT to climb:
//                   "you cant train with only 15-20 iron first you need to
//                   stock some maybe 500-600 then you start train blacksmith"
//                   (2026-08-30). 550 units for a full 0->100.0 climb is
//                   kUnitsPerSkillPoint = 5.5 units per skill POINT, so a smith
//                   at 70.0 aiming for 100.0 banks 165, not 550. Applied only
//                   to a skill the build plan still has to raise AND whose
//                   recipes actually eat this material.
//
//   marketReserve = restockConsumablesTo x (how many professions in the
//                   catalogue must buy `item` from somebody). This is the term
//                   that keeps a pure gatherer from dumping: a lumberjack whose
//                   own bench needs nothing still holds one restock lot per
//                   consumer profession, so there is something in the pack when
//                   a carpenter finally shouts WTB. Halved when the purse is
//                   below this life's own goldReserve -- a broke character
//                   releases its market stock sooner, which is the same "the
//                   threshold bends when the purse is empty" rule PolicyForPurse
//                   already applies.
//
// The `gaps` argument is the ONLY thing this function cannot derive itself: how
// far each planned skill still has to climb is life-layer state (the build plan
// against the observed skill sheet). The caller supplies it rather than this
// file reaching into the life layer.
// ---------------------------------------------------------------------------
struct SkillGap {
    int skillId = -1;
    i32 tenthsRemaining = 0;   // 0 when the skill is at or past its target
};

struct MaterialCap {
    // False for anything that is not a raw or processed material: finished
    // goods are not what the ruling covers and pass the gate untouched.
    bool isMaterial = false;
    i32  units    = 0;   // the sum below
    i32  ownPlan  = 0;
    i32  training = 0;
    i32  market   = 0;
};

MaterialCap MaterialSurplusCap(const prof::Profession& p, const char* item,
                               i32 craftBatch, i32 gold,
                               const std::vector<SkillGap>& gaps,
                               const TradePolicy& policy);

// The sell-side gate itself: BOTH halves of the ruling in one answer.
//
//   1. the player-first window has closed for this item (`playersDeclined`), and
//   2. what the character holds -- PACK PLUS BANK, because banked stock is
//      still its stock -- exceeds the cap above.
//
// A refusal is not a failure; it is "bank it and get on with something else".
struct MaterialSaleGate {
    bool        allowed = false;
    const char* reason  = nullptr;   // always populated
    i32         held    = 0;
    i32         cap     = 0;
    MaterialCap detail;
};

MaterialSaleGate MaterialNpcSaleGate(const prof::Profession& p, const char* item,
                                     i32 heldPackAndBank, bool playersDeclined,
                                     i32 craftBatch, i32 gold,
                                     const std::vector<SkillGap>& gaps,
                                     const TradePolicy& policy);


// --- M7: the disposal order for what a life will NOT wear --------------------
//
// "mage wears only mage equipment, sell the rest -- studded is ok -- to
// players first, NPC only if nobody buys" (project owner).
//
// The rule splits across two milestones and this is the second half. WHAT a
// life may wear is a catalogue fact and lives on Profession::wears (M5). What
// happens to everything else is a gold-flow question and lives here, because
// the last step of it -- handing an item to an NPC -- is the one flow on this
// shard that CREATES gold, and the Gold Faucet Registry is what decides
// whether a given item may take that route.
//
// The order is not a preference, it is the order:
//
//   1. WEAR IT. Free, immediate, and the reason the character picked it up.
//   2. OFFER IT TO PLAYERS. Even when an NPC would take it -- this is exactly
//      where the ordinary surplus rule inverts. Surplus() skips anything an
//      NPC buys because that is a shorter errand for goods the life MAKES;
//      for gear it did not make, the owner's rule puts players first, and a
//      player sale moves gold sideways instead of printing it.
//   3. SELL IT TO AN NPC, and only if the registry says that route is
//      established for this item. For looted armour it currently does NOT:
//      `monster_loot_resale` is Policy::Unknown -- "whether Revolution
//      intended corpse-loot resale as an income strategy is unestablished,
//      and it is the classic route by which a hunting bot becomes a
//      vendor-dumping bot". So today step 3 refuses, by evidence and not by
//      omission, and the item is banked instead.
enum class Disposal : u8 {
    Wear = 0,        // it fits the life's class and beats what is worn
    OfferToPlayers,  // step 2 -- announce it, whatever the NPCs would pay
    SellToNpc,       // step 3 -- only where the registry allows the route
    Bank,            // nobody may have it: it goes in the box, not the bin
};

const char* DisposalName(Disposal d);

struct DisposalRuling {
    Disposal        what = Disposal::Bank;
    const char*     reason = nullptr;      // always populated
    const faucet::GoldFaucet* via = nullptr;  // set when what == SellToNpc
};

// `wearable` is the caller's answer to "does this fit the life's class and
// improve on what is worn" -- Runner::MayWear owns that, because it needs the
// live paperdoll. `playersDeclined` is set once the item has been offered and
// nobody wanted it; until then step 2 has not finished and step 3 is not
// reached.
DisposalRuling DisposeOfGear(const prof::Profession& p, const char* item,
                             bool wearable, bool playersDeclined,
                             const Ledger& ledger);

// --- who on this shard will buy a thing -------------------------------------
//
// Read off the shard's own vendor buy-templates
// (runtime/scripts/templates/tm_vend.scp), not inferred from the trade name --
// and the difference matters. The obvious guess for logs is the LUMBERJACK
// vendor, and it is wrong: c_lumberjack SELLS logs and buys only axes
// (c_vendor_human.scp:2853-2922). The carpenter is the one that buys them.
//
// Trades are named as the paperdoll-title substring to look for. Mapping a
// trade to a travel destination is the caller's job, because that is world-
// model knowledge and this layer stays protocol-free.
struct NpcBuyer {
    const char* item;
    const char* trade;
};

// Every trade that buys `item`, best price first. Empty is a real answer: it
// means no NPC on this shard takes it, and the character should bank the goods
// rather than walk the world looking for a buyer that does not exist.
//
// `playersDeclined` opens the NPC price floor for materials -- see
// MaySellToNpc above. It defaults to false, so the strict answer is what a
// caller that has not asked the player market first still gets.
std::vector<const NpcBuyer*> NpcBuyersFor(const char* item,
                                          bool playersDeclined = false);

// Cheap form of the same question, for the need layer.
bool HasNpcBuyer(const char* item, bool playersDeclined = false);

// ---------------------------------------------------------------------------
// Prices. Observed only.
// ---------------------------------------------------------------------------

enum class PriceSource : u8 {
    NpcVendorSells = 0,   // read off an open vendor window: what it charges us
    NpcVendorBuys,        // what it offered US for the same thing
    PlayerQuoted,         // another character said a number
    PlayerTraded,         // a trade that actually completed at this number
};

const char* PriceSourceName(PriceSource s);

struct PriceObservation {
    std::string item;
    i32         pricePerUnit = 0;
    PriceSource source = PriceSource::NpcVendorSells;
    std::string who;             // vendor or character name, as seen
    i32         x = 0, y = 0;
    i64         whenMs = 0;
};

// Everything one character has ever seen a price be. Per-character by
// construction: it is stored in that character's own memory file and is never
// merged with anybody else's.
class PriceBook {
public:
    void Note(const PriceObservation& o);

    // The most recent observation of `item` from `source`, or nullptr. Returns
    // NOTHING rather than a guess when the character has never seen one --
    // "I do not know what this is worth" is a legitimate and common state.
    const PriceObservation* Latest(const char* item, PriceSource source) const;

    // What this character believes it can get for one unit, or -1 if it has
    // no basis. A completed player trade outranks a quote, which outranks an
    // NPC's buy price, because that is the order of how much each one proves.
    i32 BelievedSalePrice(const char* item) const;

    // Anything older than `maxAgeMs` is stale: prices move, and acting on a
    // week-old number is not knowledge.
    void Expire(i64 nowMs, i64 maxAgeMs);

    const std::vector<PriceObservation>& All() const { return obs_; }
    std::vector<PriceObservation>&       Mutable()   { return obs_; }
    usize Size() const { return obs_.size(); }

private:
    std::vector<PriceObservation> obs_;
};

// ---------------------------------------------------------------------------
// Player-to-player trade.
//
// This is what the milestone is actually for. Once materials stopped being
// NPC-sellable, a lumberjack's logs and a smith's ingots have exactly one
// market: other characters. A twenty-bot fleet spent a whole session with
// EARN_GOLD blocked 3,297 times for want of a buyer, which is the correct
// behaviour and also a dead end until this exists.
//
// THE NON-OMNISCIENCE RULE APPLIES HERE HARDEST. There is no "who needs
// boards" query and no shard-wide want list. A character learns that someone
// wants something the way a player does: it HEARS them say so. So the wire
// format below is a spoken line, parsed out of the journal, and a bot that was
// not in earshot simply does not know.
// ---------------------------------------------------------------------------

// One side of a proposed deal, as announced out loud.
struct TradeIntent {
    std::string item;
    i32         qty = 0;
    i32         pricePerUnit = 0;   // the seller's own claim, not a fact

    bool Valid() const { return !item.empty() && qty > 0 && pricePerUnit >= 0; }
    i32  Total() const { return qty * pricePerUnit; }
};

// The spoken forms. Deliberately terse and machine-parseable in both
// directions, because both ends are bots -- but shaped like the WTS/WTB
// shorthand players actually used, so a human watching the shard reads
// something familiar rather than a protocol.
//
//   "WTS 20 i_board 4gp"      <- a seller announcing
//   "WTB i_board"             <- a buyer answering
std::string FormatSellOffer(const TradeIntent& t);
// `toWhom` NAMES THE SELLER, and that is not decoration.
//
// A bare "WTB i_ingot_iron" is addressed to nobody, so every seller holding
// that item read it as the answer to its OWN standing WTS. On 2026-09-02 two
// of them (Kharain, Elvar) both walked to Odessa and both opened a trade
// window inside two seconds; Odessa could pay one, both handshakes timed out
// at 25s and both sellers then stood at the bank. Naming the one seller being
// answered is how a player does it and is what makes the reply exclusive.
std::string FormatBuyReply(const std::string& item,
                           const std::string& toWhom = std::string());

// The leading "Name, ..." of a spoken line, or empty when it was said to the
// room. One token only -- a sentence that happens to contain a comma is not an
// address.
std::string SpeechAddressee(const std::string& said);
// True when `me` may act on `said`: either it named nobody, or it named us.
bool AddressedTo(const std::string& said, const std::string& me);

// "Name, sorry -- sorted": the buyer telling a seller it did not choose to
// stand down, so the loser ends its errand instead of waiting out a timeout.
std::string FormatDecline(const std::string& toWhom);
bool ParseDecline(const std::string& said, std::string* whoOut);

// A DEMAND-SIDE ANNOUNCEMENT, not a reply.
//
//   "WTB 20 i_log 4gp"
//
// The buyer half had no voice at all: a crafter short of materials walked to
// the market and stood there SILENTLY for the whole listening window, so the
// only way a trade could ever start was a gatherer happening to announce the
// exact thing somebody happened to need. Supply could call; demand could not.
//
// The price is the MOST this life will pay per unit, out of its own purse and
// its own observed prices (ChooseBuyWant below). It is not a market rate and
// this fleet has no such thing.
std::string FormatBuyWant(const TradeIntent& t);

// Parse a heard line. Returns false when it is not an offer at all, which is
// the common case: most of what a character hears is not addressed to it.
bool ParseSellOffer(const std::string& said, TradeIntent* out);
// The bare reply form, "WTB i_log". TOLERANT OF THE FULL FORM: a WTB that
// carries a quantity ("WTB 20 i_log 4gp") names the same item and must resolve
// to the same answer, or a seller would stop recognising answers to its own
// offer the moment the buyer learned to speak first.
bool ParseBuyReply(const std::string& said, std::string* itemOut);
// The full form, with quantity and ceiling. A bare "WTB i_log" parses here too
// and comes back with qty 0 and pricePerUnit 0 -- "some, at whatever you ask"
// -- which is exactly what the reply form means.
bool ParseBuyWant(const std::string& said, TradeIntent* out);

// WHICH KIND OF "WTB" IS THIS? Two lines share the prefix and mean opposite
// things:
//
//   "Elvar, WTB i_ingot_iron"   a REPLY    -- "yes, I will take YOUR offer"
//   "WTB 8 i_ingot_iron 52gp"   an ANNOUNCE -- "will somebody sell me these"
//
// A seller holding a live WTS read the second as the first. Evidence:
// run_gates/g_Elvar.console.txt:338-340, 2026-09-02 12:53:57 -- Odessa
// broadcasts "WTB 8 i_ingot_iron 52gp", Elvar logs `trade:  wants our
// i_ingot_iron` (the answer-to-our-offer branch), says NOTHING back, and
// opens a trade window 5.7s later. The buyer never heard a WTS, so it had no
// price, no quantity and no partner of its own; it put nothing in the window
// and both sides timed out at 25s (g_Odessa.console.txt:257-284). Every trade
// that started from the demand side died exactly this way.
//
// The difference is structural, so the test is too: an ANNOUNCE names a
// quantity AND a price and is said to the room. Anything else -- the bare
// form, or any WTB addressed to one player by name -- is a reply.
// ParseBuyReply stays deliberately tolerant of both forms (see above); this
// classifier is what a listen loop uses to pick a branch.
enum class BuyLineKind : u8 { NotABuyLine = 0, Reply, Announce };
BuyLineKind ClassifyBuyLine(const std::string& said, TradeIntent* out = nullptr);

// What this life should announce, or nothing. Reads the same Surplus() the NPC
// path reads, then keeps only what NO NPC will buy -- because if an NPC takes
// it, that is a shorter errand and the player market does not need to carry it.
//
// `book` supplies the asking price. With no observation the character has no
// basis for a number and announces nothing rather than inventing one; that is
// the same rule BelievedSalePrice follows.
bool ChooseSellOffer(const prof::Profession& p,
                     const std::vector<Stock>& pack,
                     const PriceBook& book,
                     const TradePolicy& policy,
                     TradeIntent* out);

// WHAT THIS LIFE SHOULD SHOUT THAT IT WANTS. The mirror of ChooseSellOffer.
//
// Reads the same PlayerMarketWants the buy errand is already scored from, and
// prices it at the MOST ConsiderOffer would actually accept -- so the number
// said out loud and the number the buyer will honour at the window are the same
// number. A bot that advertises a ceiling it then refuses is a bot no gatherer
// can plan around.
//
// `gold` is PACK COIN. DriveOpenTrade only ever offers coin found in the
// backpack, so a want priced against the bank balance is a promise the window
// cannot keep.
bool ChooseBuyWant(const prof::Profession& p,
                   const std::vector<Stock>& holdings,
                   const PriceBook& book,
                   const TradePolicy& policy,
                   i32 gold,
                   TradeIntent* out);

// SHOULD THIS LIFE ANSWER A WTB IT JUST HEARD? The mirror of ConsiderOffer.
//
// Only from the PACK: an offer is a promise, and a promise has to be honourable
// without a second errand (the same rule the announce path follows -- see
// "ANNOUNCE ONLY WHAT IS IN THE HAND" in Runner::DoTradeWithPlayer).
//
// A DIRECT REQUEST OUTRANKS `minimumSurplusToOffer`. That threshold exists so a
// character does not trek across town to sell two of something; somebody
// standing here asking out loud has already paid that cost.
//
// Refuses when the buyer's ceiling is below what this life believes the goods
// are worth -- a seller that undercuts its own observed price teaches the fleet
// a wrong number.
bool AnswerBuyWant(const prof::Profession& p,
                   const std::vector<Stock>& pack,
                   const PriceBook& book,
                   const TradePolicy& policy,
                   const TradeIntent& want,
                   TradeIntent* out);

// --- FUNDING A WINDOW THE BUYER DID NOT OPEN --------------------------------
//
// A seller answers a WTB by walking over and opening a trade window. The buyer
// is then looking at a window it did not itself commit to: the "heard a WTS"
// branch that normally records how many and at what price never ran for this
// deal, so the coin it owes computes as zero and it stands there offering
// nothing.
//
// What it DOES have is the want it said out loud a few seconds earlier -- item,
// quantity and the most it will pay, all three already bounded by its own purse
// (ChooseBuyWant). That announcement IS the plan, and this is the decision to
// honour it. Deliberately a separate, pure function rather than arithmetic
// inline in the goal handler: it is the one place that says how much of
// somebody else's window this life is willing to pay for.
//
// `goldOnHand` is PACK COIN, for the same reason ChooseBuyWant takes pack coin:
// only backpack gold can be dropped into a trade window.
struct FundingDecision {
    bool        accept = false;
    i32         qty = 0;           // units the coin actually covers
    i32         gold = 0;          // coin to put in the window
    const char* reason = nullptr;  // always populated
};

FundingDecision FundOpenWindow(const TradeIntent& planned, i32 goldOnHand,
                               i32 goldReserve);

// Should this life answer an offer it just heard?
struct BuyDecision {
    bool        accept = false;
    i32         qty = 0;           // how many we actually want
    const char* reason = nullptr;  // always populated
};

// --- S4: GEAR IS A REASON TO BUY, and Shortfall never said so ---------------
//
// Shortfall() reads `consumes` and `consumables`: materials and supplies. A
// SWORD is neither, so `ConsiderOffer` answered "this life has no use for it"
// to every weapon ever offered, and a swordsman could not want a sword. The
// half of the trade predicate that speaks for equipment lives below.
//
// What a life has ON, by layer. Only the layers the question needs -- the
// hands -- so a caller does not have to reconstruct a paperdoll. Empty is a
// legitimate value and means "nothing observed", which DecideAcquire reads as
// an empty slot; the caller supplies the truth it has.
struct WornItem {
    u8  layer = 0;
    u16 graphic = 0;    // 0 = the slot is empty
};

// A PIECE OF EQUIPMENT THIS FLEET CAN TRADE, keyed by the defname that travels
// in a WTS line.
//
// Rows are present only where a script line backs every field, the same rule
// the production graph follows. `weaponSkill` is the ITEMDEF's own SKILL= line
// -- which is the ONLY authority on what a weapon trains on this shard, and it
// disagrees with the crafting menu's categories: i_dagger sits under "Bladed"
// and is SKILL=Fencing.
struct GearItem {
    const char* item;        // itemdef DEFNAME
    u16         graphic;     // the ITEMDEF id
    u16         flip;        // DUPELIST id -- the flipped art, seen on the wire
    u8          layer;       // 1 = one hand, 2 = the other (Client.cpp:51-52)
    int         weaponSkill; // rules:: skill id; -1 when it is not a weapon
    u16         reqStr;      // ReqStr from the itemdef
    const char* evidence;
};

const GearItem* FindGear(const char* item);
// By either art id: a weapon on the wire is often the flipped one.
const GearItem* FindGearByGraphic(u16 graphic);

// WOULD THIS LIFE BUY THE OFFERED PIECE AND PUT IT ON?
//
// The held/worn/wearable/reserve arithmetic is NOT re-derived here: it is
// life::DecideAcquire (include/uo/activities/acquire.h), which exists because
// six heater shields were bought by a wear-loop and a buy-loop that could not
// see each other. What this function owns is the two questions DecideAcquire
// deliberately does not answer -- "is this my class of thing" and "is this
// price one I will pay sight unseen".
BuyDecision WantsGear(const prof::Profession& p,
                      const std::vector<Stock>& pack,
                      i32 gold,
                      const TradePolicy& policy,
                      const TradeIntent& offer,
                      const std::vector<WornItem>& worn);

// `worn` defaults to empty so a caller with no paperdoll view still gets the
// material half of the answer unchanged. A caller that HAS one should pass it:
// without it a life that is already armed will accept a second sword.
BuyDecision ConsiderOffer(const prof::Profession& p,
                          const std::vector<Stock>& pack,
                          i32 gold,
                          const TradePolicy& policy,
                          const TradeIntent& offer,
                          const std::vector<WornItem>& worn = {});

// ---------------------------------------------------------------------------
// Where the gold went. Telemetry, not a rule.
// ---------------------------------------------------------------------------

// SERVER-WIDE ACCOUNTING, and the three cases must never blur:
//
//   creation    raises the shard's total player gold
//   destruction lowers it
//   transfer    leaves it exactly where it was
//
// This is what makes inflation telemetry possible later, and it is why a sale
// to a PLAYER is not a source however much gold arrives in the purse.
enum class GoldFlow : u8 {
    // --- created: gold that did not exist before -------------------------
    CreatedVendor = 0,    // an NPC paid for goods
    CreatedPvmLoot,       // gold that was in a corpse
    CreatedTreasure,      // a map or chest
    CreatedBounty,        // Revolution's Head Hunter
    CreatedBegging,       // begging an NPC (begging a PLAYER is a transfer)
    StartingKit,          // the shard handed it over at creation

    // --- destroyed: gold that left the economy ---------------------------
    DestroyedVendorPurchase,
    DestroyedTrainer,
    DestroyedService,     // healers, stablemasters, dues

    // --- transferred: total unchanged ------------------------------------
    TransferPlayerTrade,      // we received from a player
    TransferPlayerTradeOut,   // we paid a player
    Count,
};

// Which of the three an entry is. Kept as an enum rather than three bools so
// a flow cannot accidentally be none of them or two of them.
enum class GoldEffect : u8 { Created = 0, Destroyed, Transferred, Count };

GoldEffect EffectOf(GoldFlow f);
const char* GoldEffectName(GoldEffect e);

const char* GoldFlowName(GoldFlow f);
// True for the entries that ADD gold to the shard's player economy.
bool IsGoldSource(GoldFlow f);

struct GoldEntry {
    GoldFlow    flow = GoldFlow::CreatedPvmLoot;
    i32         amount = 0;      // always positive; the flow says the direction
    std::string detail;          // what was bought/sold/trained
    i64         whenMs = 0;
};

// A character's own ledger. The point is the anti-arbitrage invariant: if
// sources consistently exceed sinks with no player-side loss, the economy is
// printing gold, and the audit's ratchet test should be failing.
struct Ledger {
    std::vector<GoldEntry> entries;

    void Note(GoldFlow f, i32 amount, const char* detail, i64 whenMs);
    i32  TotalIn() const;
    i32  TotalOut() const;
    i32  Net() const { return TotalIn() - TotalOut(); }
    i32  TotalFor(GoldFlow f) const;
};

}  // namespace uo::market
