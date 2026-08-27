#pragma once

// ---------------------------------------------------------------------------
// The Revolution production graph (M3.7).
//
// WHAT THIS IS FOR
//
// M3.7's whole thesis is that a bot must be able to answer "where does this
// come from?" and act on the answer. Not "I am a Tailor, therefore I have
// cloth" -- but "I need cloth; can I make it; what is missing; where is the
// station". So this file holds edges, not professions.
//
// THREE RULES IT EXISTS TO ENFORCE
//
//   1. EVERY EDGE CARRIES ITS EVIDENCE. A recipe whose numbers came from
//      Source-X C++ is not the same kind of fact as one from a forum post, and
//      the difference has already burned this project twice (M3's reagent
//      figure, M3.5's poison rule -- both inferred from a declaration rather
//      than observed). `evidence` is not a comment; it is the field that says
//      whether a rule may be trusted.
//
//   2. CAPABILITY FOLLOWS FROM SKILLS, NOT FROM A CLASS. `CanSelfProduce`
//      takes the character's actual skills, tools and inventory. There is no
//      profession enum anywhere in this file, and the tests use a
//      Mining+Blacksmithy+Alchemy+Magery hybrid on purpose.
//
//   3. A STATION IS A PLACE, NOT A FLAG. On this runtime a spinning wheel or
//      loom must be a DYNAMIC item -- Source-X resolves a use-target with
//      uid.ObjFind(), so a map static yields pItemTarg == nullptr and the
//      IT_WOOL / IT_YARN / IT_THREAD cases fall straight through
//      (CClientTarg.cpp:2053, :2186). A forge is kinder: FindItemTypeNearby
//      does scan statics. `Station` is therefore a travel goal, and
//      `StationNeedsDynamicItem` records which ones cannot be satisfied by
//      map art.
//
// Deterministic data. No inference, no pricing, no I/O.
// ---------------------------------------------------------------------------

#include "uo/types.h"

#include <vector>

namespace uo::prod {

// Where a good legitimately comes from. Ordered so that a lower value is a
// weaker claim: Unknown is the default and must fail safe.
enum class Provenance : u8 {
    Unknown = 0,
    WorldGathered,      // a gathering skill takes it from terrain
    AnimalHarvested,    // taken from a living creature (shearing)
    PvmDrop,            // carved or looted from a corpse
    TreasureDrop,       // chest, S.O.S. or map
    WorldProcessed,     // a station transforms it; no craft menu, no skill
    PlayerCrafted,      // a skill menu with a SKILLMAKE gate
    PlayerMarket,       // a RevolutionUO cooperative category
    NpcVerified,        // a dated Revolution entry says an NPC sold it
    Count,
};

const char* ProvenanceName(Provenance p);

// A physical place the character must stand next to.
enum class Station : u8 {
    None = 0,
    Forge,          // t_forge  -- smelting, and the blacksmith menu
    Anvil,          // t_anvil  -- repairs
    SpinningWheel,  // t_spinwheel
    Loom,           // t_loom
    Count,
};

const char* StationName(Station s);

// True when a MAP STATIC of this station is inert and only a dynamic item
// works. See the header comment; this is the single most load-bearing runtime
// fact M3.7 found.
bool StationNeedsDynamicItem(Station s);

// A held item the step requires. Not an inventory list -- a capability.
enum class Tool : u8 {
    None = 0,
    Blade,          // any IT_WEAPON_AXE/_SWORD/_FENCE/_MACE_SHARP/IT_CARPENTRY_CHOP
    Pickaxe,        // IT_WEAPON_MACE_PICK
    Scissors,       // IT_SCISSORS
    SewingKit,      // IT_SEWING_KIT
    SmithHammer,    // IT_WEAPON_MACE_SMITH, and it must be EQUIPPED in HAND1
    TinkerTools,
    CarpentryTool,
    MortarPestle,
    PenAndInk,
    FishingPole,
    BlankScroll,    // Inscription's menu opens by double-clicking one
    Count,
};

const char* ToolName(Tool t);

// True when the tool must be worn rather than merely carried. Blacksmithy is
// the only one, and it is a real live failure mode: CClientUse.cpp:1273 reads
// LayerFind(LAYER_HAND1), so a hammer in the pack is not a hammer.
bool ToolMustBeEquipped(Tool t);

struct Ingredient {
    const char* item = nullptr;   // itemdef defname, lowercase
    i32         qty  = 0;
};

// One edge of the graph: how `outputQty` of `output` is produced.
struct Recipe {
    const char* output     = nullptr;
    i32         outputQty  = 1;
    Provenance  provenance = Provenance::Unknown;
    Station     station    = Station::None;
    Tool        tool       = Tool::None;
    // Skill gates, in tenths. -1 means the step imposes none -- and several
    // genuinely do not: spinning wool is a plain item-use with no check at all.
    i32 skillId       = -1;
    i32 skillTenths   = 0;
    i32 skillId2      = -1;
    i32 skillTenths2  = 0;
    // Four is enough for every edge in the Revolution graph; the widest real
    // recipe (the runebook) uses exactly four.
    Ingredient  inputs[4] = {};
    const char* evidence  = nullptr;   // ENGINE file:line, SCRIPT file, or a dated archive entry
};

// The graph. Compiled in rather than loaded, so it is versioned, reviewable in
// a diff, and available to a test that runs with no server and no data files.
const std::vector<Recipe>& KnownRecipes();

// The first recipe producing `item`, or nullptr. Case-sensitive, lowercase.
const Recipe* FindRecipe(const char* item);

// What class of good this is, whether or not anything produces it.
Provenance ProvenanceOf(const char* item);

// True when no recipe produces it: a leaf of the graph. Ore, logs, wool and
// hides are leaves -- the world produces them and nothing else does.
bool IsRawResource(const char* item);

// What a character actually is, at this instant. Deliberately not a build, a
// profession or a plan: only what it can do right now.
struct Capability {
    // skillTenths[skillId]; -1 for "not reported yet" so an unknown skill can
    // never be mistaken for a zero one.
    std::vector<i32> skillTenths;
    // Tools the character holds. `equipped` matters for SmithHammer.
    std::vector<Tool> toolsCarried;
    std::vector<Tool> toolsEquipped;
    // Stations the character can currently reach, as found by the world model.
    std::vector<Station> stationsReachable;

    i32  Skill(i32 skillId) const;
    bool HasTool(Tool t) const;
    bool CanReach(Station s) const;
};

// Why a step cannot be taken. Ordered by what a planner should fix first.
enum class Block : u8 {
    None = 0,
    NoRecipe,         // nothing in the graph produces it
    MissingInput,     // an ingredient is absent
    MissingSkill,     // a skill gate is not met
    MissingTool,
    ToolNotEquipped,  // held but not worn -- the smith-hammer case
    NoStation,        // the station is not reachable from here
    Count,
};

const char* BlockName(Block b);

struct Requirement {
    Block       block = Block::None;
    const char* item  = nullptr;   // the missing ingredient, when MissingInput
    i32         qty   = 0;
    i32         skillId = -1;      // the failing skill, when MissingSkill
    i32         skillTenths = 0;
    Tool        tool    = Tool::None;
    Station     station = Station::None;
};

// Everything standing between the character and ONE unit of `item`, given what
// it holds. `have` is (item, qty) pairs.
//
// Shallow by design: it reports the immediate blockers of the final step, not
// the whole subtree. A planner that wants depth calls this again on each
// missing input, which is also how it discovers a cycle.
std::vector<Requirement> MissingInputs(const char* item,
                                       const Capability& cap,
                                       const std::vector<Ingredient>& have);

// True when `MissingInputs` is empty.
bool CanSelfProduce(const char* item,
                    const Capability& cap,
                    const std::vector<Ingredient>& have);

// Walk the graph from `item` down to raw resources, deepest-first, and return
// the production order. Empty when a cycle is detected, with `cycle` set --
// a graph that eats its own tail would hang a planner, so it is an error, not
// a shrug.
std::vector<const Recipe*> ProductionOrder(const char* item, bool* cycle);

// Every raw resource `item` ultimately rests on, with the quantity needed for
// one unit. This is the "what would I have to go and get?" query.
std::vector<Ingredient> RawInputsFor(const char* item, i32 qty);

} // namespace uo::prod
