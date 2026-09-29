#pragma once

// ---------------------------------------------------------------------------
// "What every new player already knows" (docs/LIFE_GATE_WAVE1.md theme 1,
// BOT_ARCHITECTURE.md #38-39 -- "bots are not omniscient: a bot knows what it
// has seen, been told, learned as a new player would, or remembered").
//
// Wave-1 evidence: a freshly created character has an EMPTY places/resources
// memory, so its very first errands ask the atlas cold --
//
//   Vorar (lumberjack): GATHER_LOGS spun on "no known source of that
//   resource" -- TravelToResource(Lumber) had nothing proven AND nothing
//   hinted to fall back to.
//   Draver / Lyra (smith / scribe): BANK failed "no banker in sight" at
//   00:32/00:33 -- DoBank never travels anywhere, it only scans mobiles
//   already in view, and a fresh spawn is not standing at a counter.
//
// SeedNewbieKnowledge is the one-time fix: it hands a new life the same
// starting knowledge a real player has on day one -- where the home town's
// bank, healer and provisioner are, and a lead (never a proven stand) on
// where its own trade is worked nearby. Nothing here is omniscient: every
// seed comes from the ATLAS (map knowledge any client has) and is scoped to
// within kNewbieKnowledgeRadius tiles of the character's OWN home region --
// a Minoc smith does not learn Yew.
//
// PURE. No Client, no network, no randomness beyond what `profession` and
// `atlas` already fix -- so it is unit-testable directly against the real,
// generated data/revolution_atlas.txt (see tests/newbie_knowledge.cpp).
// ---------------------------------------------------------------------------

#include "uo/life.h"
#include "uo/professions.h"
#include "world/Atlas.h"

namespace uo::life {

// How far from home "common knowledge" reaches, in Chebyshev tiles. Generous
// enough to cover a town and its immediate outskirts (the Minoc mine sits
// ~100 tiles from Minoc's own town centre) without leaking into the next
// city over.
inline constexpr i32 kNewbieKnowledgeRadius = 200;

// How many resource-area leads a fresh character starts with. Same count
// Runner::SeedCommonKnowledge used before this replaced it.
inline constexpr int kNewbieResourceHints = 3;

// Seed a freshly-created (or freshly-loaded-with-empty-memory) character's
// Memory with what a new player would already know:
//
//   (a) the home town's bank (kind "common_knowledge_bank"; Service::Banker,
//       guarded preferred -- the same rule every other bank lookup in this
//       codebase uses)
//   (b) up to kNewbieResourceHints resource areas near home that yield what
//       `profession` gathers, as HINTS (Memory::HintResource) -- never as
//       proven stands. A hint never outranks an actually-proven spot: see
//       Memory::BestProvenResource / BestHint (and, for the older
//       Memory::BestResource callers, its own successes/failures scoring --
//       a hint starts at 0/0 and a proven stand does not).
//   (c) the home town's healer and provisioner (kind "common_knowledge_healer"
//       / "common_knowledge_provisioner")
//
// All of it confined to within kNewbieKnowledgeRadius tiles of homeCity's own
// region centre, as the atlas names it (world_atlas::Atlas::FindRegion).
//
// `profession` may be null (an older character predating the catalogue) --
// (b) is skipped but (a) and (c) still seed. `homeCity` empty, or not found
// in the atlas, seeds nothing at all: there is no "home" to know things near.
//
// Idempotent by construction (NotePlace/HintResource both dedupe on
// kind+location), but callers should still gate repeat calls behind a
// once-per-life marker (Runner uses Memory::HasEvent
// "newbie_knowledge_seeded") -- seeding is meant to run ONCE, the moment a
// life's memory is empty, not every session.
void SeedNewbieKnowledge(PersistentState& state, const prof::Profession* profession,
                         const std::string& homeCity,
                         const world_atlas::Atlas& atlas, i64 nowMs);

}  // namespace uo::life
