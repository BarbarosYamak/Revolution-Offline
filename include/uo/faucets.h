#pragma once

// ---------------------------------------------------------------------------
// The Gold Faucet Registry -- where new gold may legitimately enter the shard,
// and why each route is or is not allowed.
//
// THE PROBLEM THIS EXISTS TO SOLVE
//
// `EarnGold` must not mean "find any Scripts-X vendor willing to buy
// something". Stock Scripts-X buys hundreds of player-crafted goods, and that
// is a fact about SPHERE, not about Revolution. The project already walked
// into this once from the selling side: the first sell path was built from
// `tm_vend.scp`, which really does carry `BUY=i_log,{5 15}` on the carpenter
// template, and on Revolution logs were a player-market good.
//
//     Just because an NPC technically accepts an item does not mean the bot
//     may use that NPC as its economic strategy.
//
// WHY IT IS A REGISTRY RATHER THAN A PREDICATE
//
// Every route carries its own evidence, separately for HISTORY and for this
// RUNTIME, because those two disagree and the disagreement is the interesting
// part. Inscription is the clearest case: our own bot has sold scrolls to a
// mage shop on this shard and watched the purse move, while the archival
// statement about Revolution's NPC scroll channel is weaker than the one for
// fishing. Live reality is not downgraded because the archive is vague, and
// the archive is not overstated because the runtime happens to work.
//
// THE ASYMMETRY IS THE POINT
//
// A fisher can reliably sell fish to an NPC. A smith mostly cannot sell its
// output to one and has to reach players, hunt, or find another route. That is
// not a gap to be filled in -- it is what makes the professions live different
// economic lives instead of all being the same money printer with different
// animations.
// ---------------------------------------------------------------------------

#include "uo/types.h"

#include <string>
#include <vector>

namespace uo::faucet {

// What KIND of activity puts the gold in a character's hand.
enum class SourceType : u8 {
    VendorSale = 0,   // an NPC pays for goods; the gold is created
    MonsterLoot,      // gold that was in a corpse
    Treasure,         // a map or a chest
    Bounty,           // Revolution's Head Hunter
    Begging,          // the skill
    Count,
};

const char* SourceTypeName(SourceType s);

// How well the HISTORICAL claim is supported -- that Revolution actually
// worked this way, whatever this runtime happens to permit.
enum class HistoryEvidence : u8 {
    Unknown = 0,          // no statement either way. Fails safe.
    Confirmed,            // a Revolution guide or dated changelog says so
    NotFullyConfirmed,    // described as income, but the NPC channel is vague
    // THE SHARD OWNER DECIDED IT. Not a historical claim and must never be
    // read as one: it says nobody has shown what Revolution did, and the
    // person whose shard this is has ruled on it anyway. That is a legitimate
    // ground for allowing a route -- it is their world -- and it is a
    // different KIND of ground from a guide or a changelog, so it gets its own
    // grade rather than being laundered into Confirmed.
    //
    // Every entry carrying this must quote the instruction and, where it
    // overrides evidence, say what that evidence was.
    OwnerDecision,
    Count,
};

// How well the RUNTIME claim is supported -- that the mechanic works here.
enum class RuntimeEvidence : u8 {
    Unverified = 0,       // nobody has tried it on this shard
    ScriptSupported,      // the scripts carry the BUY row / the recipe exists
    LiveProven,           // a bot did it and the server moved the gold
    Blocked,              // tried, and the runtime does not support it
    Count,
};

const char* HistoryEvidenceName(HistoryEvidence e);
const char* RuntimeEvidenceName(RuntimeEvidence e);

// The verdict. `Unknown` is a real answer and is NOT the same as `Refused`:
// one says "we have not established this", the other says "we have, and no".
enum class Policy : u8 {
    Unknown = 0,      // no evidence yet; refused for now, revisit on evidence
    Allow,
    RefuseAuthenticity,   // the runtime permits it; Revolution did not
    RefusePlayerMarket,   // this belongs to the player economy
    BlockedRuntime,       // allowed in principle, unavailable here
    Count,
};

const char* PolicyName(Policy p);
bool Allowed(Policy p);

struct GoldFaucet {
    const char* id = nullptr;
    SourceType  sourceType = SourceType::VendorSale;
    // The profession whose life this belongs to, or nullptr for anybody.
    const char* profession = nullptr;
    // What is handed over (an itemdef defname), or the activity's subject.
    const char* input = nullptr;
    // Who pays: a vendor trade title, or the mechanic that pays.
    const char* destination = nullptr;

    HistoryEvidence history = HistoryEvidence::Unknown;
    RuntimeEvidence runtime = RuntimeEvidence::Unverified;
    Policy          policy = Policy::Unknown;
    // Always populated, allowed or refused. A verdict a human cannot read is
    // a bug, and the reason IS the character's reasoning evidence.
    const char* reason = nullptr;
};

// Everything known, allowed or not. Refused and unknown routes are kept
// deliberately: a registry that lists only what works cannot be audited, and
// "we looked at this and said no" is the most useful row in it.
const std::vector<GoldFaucet>& All();

// Every faucet that would pay for `item`, best evidence first. Empty means no
// route is even a candidate.
std::vector<const GoldFaucet*> ForItem(const char* item);

// The single ALLOWED faucet for `item`, or nullptr. This is the one a caller
// should use to decide whether a sale may proceed.
const GoldFaucet* AllowedForItem(const char* item);

// THE NPC PRICE FLOOR (owner ruling, 2026-09-02), asked of the registry.
//
// AllowedForItem above stays STRICT and unchanged: it answers "is there an
// unconditional, evidence-backed NPC faucet for this". The floor is the second,
// CONDITIONAL question, and it is deliberately a different function so that no
// existing caller silently changes meaning.
//
// True means: the switch is on, the player-first window has closed for this
// item (a complete WTS cycle nobody answered), and the item is a material.
// It does NOT mean a buyer exists -- market::NpcBuyersFor still has to find a
// live BUY row, and where none exists the item banks. The policy itself lives
// in econ::MaterialFloorOpen (progression/VendorPolicy.cpp); this is the
// registry's acknowledgement of it, so a reader looking for "may this sale
// proceed" finds both answers in one place.
bool NpcFloorOpenFor(const char* item, bool playersDeclined);

// WHO WOULD BUY THIS AT ALL? A DIFFERENT QUESTION FROM "MAY AN NPC PAY".
//
// AllowedForItem answers the second one, and using it as the first is what
// left three whole lives unable to work: a tailor, a merchant/tinker and a
// lumberjack/carpenter make NOTHING an NPC may buy, by design, so ChooseCraft
// skipped every entry of their `produces` lists and reported "this life makes
// nothing sellable" for an entire session (Aelia x44, Odessa x8, wave 2
// 2026-09-01).
//
// RefusePlayerMarket does not mean unsellable -- it means "this belongs to
// the player economy", which is a DESTINATION, not a refusal to produce.
// RefuseAuthenticity carries the same note in its own reasons ("Player-market
// usage stays valid", "a smith without an NPC faucet has to reach players").
// Both are therefore PlayerMarket here and nullptr from AllowedForItem, which
// is the whole point of keeping the two questions apart.
enum class SaleRoute : u8 {
    None = 0,       // a row names it and refuses it (Unknown, BlockedRuntime)
    Npc,            // an allowed NPC faucet exists
    PlayerMarket,   // refused at the counter, valid between players
    Unrecorded,     // no row names it at all -- evidence absent, not negative
};
SaleRoute RouteForItem(const char* item);

// Is this LIFE's whole output class a player-market good? Several rows are
// deliberately written as a class -- "tailor_output_to_vendor" is keyed on
// i_robe but its reason speaks for cloth, bolts and robes together -- so an
// Unrecorded item that is genuinely this trade's own work rides with the row.
//
// Deliberately NOT folded into RouteForItem: on its own this would say yes to
// anything at all (a tailor and a lump of gold), so the caller must ALSO have
// established that the item is this trade's product -- in practice, that it is
// a recipe on the profession's `produces` list. See life::ChooseCraft.
//
// `professionId` is the catalogue id (prof::Profession::id).
bool OutputClassIsPlayerMarket(const char* professionId);

// Every faucet this profession may legitimately use.
std::vector<const GoldFaucet*> ForProfession(const char* professionId);

// ---------------------------------------------------------------------------
// Why a plan to earn gold was refused.
//
// "cannot earn gold" is never an acceptable log line when the truth is
// "found a buyer, but this item is historically a player-market good". The
// refusal reason is part of the character's reasoning, and the accumulated
// refusals are the research backlog.
// ---------------------------------------------------------------------------
enum class Refusal : u8 {
    None = 0,
    NotSellable,
    NoKnownBuyer,
    // NOT the same thing as NoKnownBuyer, and conflating them cost a whole
    // wave. NoKnownBuyer is the SELL side: this character holds a thing and
    // no NPC trade will take it. NoKnownSupplier is the BUY side: this
    // character is short of an input and no NPC trade sells it -- which for a
    // material is the CORRECT answer under the never-sell-materials-to-NPCs
    // rule, not an error. The buy path used to log the seller's word for it,
    // so "REFUSE_NO_KNOWN_BUYER item=i_ingot_iron" read as a broken lookup
    // when it was a smith who should have gone mining
    // (run_gates/g_Zarthal.console.txt:488, g_Dorvar:740, g_Titus:636).
    NoKnownSupplier,
    UnknownPrice,
    RevolutionAuthenticityUnknown,
    PlayerMarketGood,
    InsufficientSurplus,
    RequiredForBuild,
    RequiredForProduction,
    VendorNotObserved,
    VendorUnreachable,
    VendorWrongFloor,
    EconomicRouteBlocked,
    InsufficientSkill,
    MissingTool,
    MissingRecipe,
    CarryWeight,
    Count,
};

const char* RefusalName(Refusal r);

}  // namespace uo::faucet
