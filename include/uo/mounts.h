#pragma once

#include "uo/types.h"

#include <vector>

namespace uo::mounts {

// ---------------------------------------------------------------------------
// M3.8 Phase 7 -- Revolution mount rules, as a machine-readable table.
//
// WHY THIS EXISTS AS ITS OWN LAYER
//
// M3.7.1 rode a horse and proved the mount MECHANIC. It proved nothing about
// Revolution's taming difficulty, because the runtime and the shard's own
// published rules disagree:
//
//   Revolution /binek_bilgileri   horse 53.1
//   runtime c_horse_gray          TAMING=29.1
//
// 29.1 is stock Sphere's number. A character created the ordinary way clamps at
// 50.0, which clears 29.1 comfortably and would FAIL Revolution's 53.1. So the
// tamer that rode a horse in M3.7.1 could not have tamed one on the real shard.
//
// The brief's instruction is the right one: do not necessarily rewrite every
// CHARDEF, but make the BOT obey the historical requirement even where the
// server is more permissive. Authenticity is enforced on our side of the wire;
// the server divergence is recorded as debt rather than papered over.
//
// SUPPLY IS THE OTHER HALF, and it is why mounts matter economically at all.
// /spawntakip_sistemi describes a calendar regenerated every Monday: 49 mounts
// a week, 7 a day -- 1 Steed, 1 Nightmare, 1 Unicorn, 2 Kirin, 5 Mustang,
// 5 Shire, 7 Frenzied, 7 Mid, 10 Forest, 10 Desert Ostard, with the rare ones
// late in the week. A hard-capped weekly supply contested by every tamer is the
// purest player-market good in the archive. Nothing in M3.8 simulates that
// calendar; the per-creature weekly figure is carried here so M4 can.
// ---------------------------------------------------------------------------

enum class Era : u8 {
    // Present and legal in the 2009-2010 Revolution window.
    TargetEra,
    // Real on Revolution but outside the window we model, or its era is not
    // established. A bot must not tame it; we do not claim it never existed.
    OutsideWindow,
    // Exists in the runtime's stock Sphere data with no Revolution evidence at
    // all. Not the same as "post-era": we simply have nothing.
    StockSphereOnly,
    Count,
};

const char* EraName(Era e);

struct MountRule {
    const char* defname = nullptr;
    u16         body    = 0;

    // Revolution's requirement, in tenths. -1 means UNKNOWN and is never
    // guessed.
    //
    // TWO SKILLS, NOT ONE. Revolution gates every mount on Animal Taming AND
    // Animal Lore at the SAME value. An earlier revision of this table modelled
    // taming only, which understated the real cost by half: a horse is not 53.1
    // points of a 700-point budget, it is 53.1 + 53.1 = 106.2, and a Nightmare
    // is 199.8. That is the difference between a mount being an incidental
    // purchase and being a career decision.
    i32 tamingMinTenths = -1;
    i32 loreMinTenths   = -1;

    // RANGES ARE ROLLED PER CREATURE, not a spread in the display.
    // Mustang is 65.0-80.0 and Shire 65.0-95.0, and each individual animal has
    // its own requirement somewhere in that band. So there is no single gate:
    // at the minimum an attempt MAY work, and only at the maximum is it certain.
    // When randomRange is false, max == min and the distinction collapses.
    bool randomRange     = false;
    i32  tamingMaxTenths = -1;
    i32  loreMaxTenths   = -1;

    i32 runtimeTamingTenths = -1;
    Era era                 = Era::StockSphereOnly;
    // May an autonomous bot attempt to tame this at all? False for anything
    // outside the target era, whatever the runtime would permit.
    bool botLegal = false;
    // Weekly supply from /spawntakip_sistemi, 0 = not on the calendar.
    i32 weeklySupply = 0;
    const char* source = nullptr;
};

const std::vector<MountRule>& KnownMounts();

// nullptr when we have no rule for it.
const MountRule* FindMount(const char* defname);

// THE ONE FUNCTION THAT MATTERS AT RUNTIME.
//
// Returns true only when the bot may legitimately attempt this creature under
// REVOLUTION's rule, never the runtime's -- so a bot standing in front of a
// horse it could tame at 29.1 still refuses below 53.1.
//
// BOTH skills are required, at the same threshold. Refuses for anything not
// botLegal, and refuses when either requirement is UNKNOWN: an unknown gate is
// not an open gate.
//
// For a random-range mount this uses the MINIMUM -- the point at which an
// attempt becomes possible. Use CanTameReliably for the point at which it
// becomes certain.
bool CanAttemptTame(i32 tamingTenths, i32 loreTenths, const char* defname);

// True when the character clears the MAXIMUM of a rolled range, so no
// individual of that species can be out of reach. Identical to CanAttemptTame
// for the fixed-threshold mounts.
bool CanTameReliably(i32 tamingTenths, i32 loreTenths, const char* defname);

// Total skill points a character must commit to make this mount reliably
// tameable -- taming + lore, at the maximum. Returns -1 when UNKNOWN.
//
// This is the number that makes mounts an economic decision rather than a
// purchase: a Nightmare costs 199.8 of a 700-point build, so a warrior who
// wants one is choosing it over a third combat skill.
i32 ReliableSkillCost(const char* defname);

// True when the runtime is more permissive than Revolution for this creature --
// i.e. the server would allow a tame the bot must refuse. This is the
// SERVER_AUTHENTICITY_DEBT set, and it is worth being able to enumerate.
bool RuntimeIsMorePermissive(const MountRule& m);

}  // namespace uo::mounts
