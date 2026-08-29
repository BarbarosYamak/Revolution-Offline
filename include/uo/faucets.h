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
