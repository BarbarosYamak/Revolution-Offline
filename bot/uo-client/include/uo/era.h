#pragma once
#include "uo/types.h"

#include <cstdio>
#include <string>

// THE SHARD'S CALENDAR. Which Revolution a character is living in.
//
// The owner's goal is RevolutionUO as it lived from about 2008 to 2016. The
// shard changed over those years, and every change below is dated by the
// shard's own update archive (/guncellemeler, quoted in docs/
// REVOLUTION_RUNBOOK_SPEC.md, REVOLUTION_RULESET_PROFILE.md,
// REVOLUTION_ANTIMACRO_SPEC.md, REVOLUTION_PRODUCTION_CHAINS.md,
// M3_7_RESOURCE_ECONOMY.md, M3_9_WORLD_READINESS.md). Nothing here is
// invented: a change without a dated source is not in the table.
//
// WHAT THE DATE CONTROLS. Sphere is the authority on rules; the date cannot
// change what the server does. It changes what a CHARACTER KNOWS TO DO: a bot
// living in March 2009 does not charge its runebook with Recall scrolls,
// because nobody on Revolution could until 13.05.2009. The server's own
// scripts are the revolution_2009_2010 profile (REVOLUTION_RULESET_PROFILE.md),
// so a date outside that window is allowed but logged as a mismatch: a 2012
// character expects guild runebooks the server does not have.
//
// UNKNOWN: anything dated 2013-2015 (no archive entries were found), when the
// shard opened, and whether it ever wiped. The 2016 rows belong to the
// Revolution16 revival, which the profile excludes.
namespace uo::era {

// yyyymmdd, so dates compare as integers.
inline constexpr i32 Key(i32 y, i32 m, i32 d) { return y * 10000 + m * 100 + d; }

inline constexpr i32 kProfileStart = Key(2009, 1, 1);    // revolution_2009_2010
inline constexpr i32 kProfileEnd   = Key(2010, 12, 31);
inline constexpr i32 kDefaultDate  = Key(2010, 6, 1);    // inside the profile, after the 2009 updates
inline constexpr i32 kFirstYear = 2008, kLastYear = 2016;

enum class Feature : u8 {
    AntiMacroFixed = 0,     // 15.04.2008 module present (bug-fixed)
    CampfireCooking,        // 15.04.2008 Camping + kindling cooking
    ReagentCrystal,         // 07.11.2008
    OreWeightOne,           // 13.12.2008 ore weight 3 -> 1
    Golems,                 // 19.12.2008
    DespiseChampion,        // 19.02.2009 Barracoon in Despise
    CookingBatch8,          // 12.04.2009 batch (skill/100)*8
    RunebookEightPages,     // 12.05.2009
    RunebookCharges,        // 13.05.2009 Recall-scroll charges, no Magery needed
    RunebookCopying,        // 13.05.2009 Inscription menu
    RecallOneReagent,       // 14.05.2009 3 -> 1 reagents
    SosFromGathering,       // 18.05.2009 S.O.S bottles from mining and lumberjacking
    CraftsmenCity,          // 04.03.2010
    PackAnimals,            // 03.11.2010
    StoreCrystal,           // 20.11.2010 Reagent Crystal renamed
    AntiMacroCodeScreen,    // 22.02.2011 code screen every 2-3 h of production
    GateOneReagent,         // 24.03.2011
    CookingBatch10,         // 01.06.2011 batch (skill/100)*10
    GuildRunebooks,         // 07.01.2012
    FifteenFixedChests,     // 22.03.2012 guardians removed
    AntiMacroDisconnect,    // 21.01.2016 one-minute code or disconnect
    Count
};

struct Change {
    Feature     feature;
    i32         date;
    const char* id;
    const char* what;
    const char* source;     // where in docs/ the dated archive line is quoted
};

inline const Change* Changes(int* count) {
    static const Change kChanges[] = {
        {Feature::AntiMacroFixed, Key(2008, 4, 15), "antimacro_fixed", "anti-macro module bug-fixed", "REVOLUTION_ANTIMACRO_SPEC.md:27"},
        {Feature::CampfireCooking, Key(2008, 4, 15), "campfire_cooking", "campfire cooking with kindling", "REVOLUTION_PRODUCTION_CHAINS.md:693"},
        {Feature::ReagentCrystal, Key(2008, 11, 7), "reagent_crystal", "Reagent Crystal", "M3_7_RESOURCE_ECONOMY.md:388"},
        {Feature::OreWeightOne, Key(2008, 12, 13), "ore_weight_1", "ore weight cut 3 -> 1", "REVOLUTION_PRODUCTION_CHAINS.md:243"},
        {Feature::Golems, Key(2008, 12, 19), "golems", "golems added", "REVOLUTION_PRODUCTION_CHAINS.md:456"},
        {Feature::DespiseChampion, Key(2009, 2, 19), "despise_champion", "champion Barracoon in Despise", "M3_9_WORLD_READINESS.md:170"},
        {Feature::CookingBatch8, Key(2009, 4, 12), "cooking_batch_8", "cooking batch (skill/100)*8", "REVOLUTION_PRODUCTION_CHAINS.md:690"},
        {Feature::RunebookEightPages, Key(2009, 5, 12), "runebook_8_pages", "runebook: 8 named pages", "REVOLUTION_RUNBOOK_SPEC.md:37"},
        {Feature::RunebookCharges, Key(2009, 5, 13), "runebook_charges", "runebook charges from Recall scrolls, no Magery", "REVOLUTION_RUNBOOK_SPEC.md:39"},
        {Feature::RunebookCopying, Key(2009, 5, 13), "runebook_copying", "runebook copying via Inscription", "REVOLUTION_RUNBOOK_SPEC.md:40"},
        {Feature::RecallOneReagent, Key(2009, 5, 14), "recall_1_reagent", "Recall reagents 3 -> 1", "REVOLUTION_RULESET_PROFILE.md:93"},
        {Feature::SosFromGathering, Key(2009, 5, 18), "sos_gathering", "S.O.S bottles from mining and lumberjacking", "REVOLUTION_WORLD_POPULATION.md:77"},
        {Feature::CraftsmenCity, Key(2010, 3, 4), "craftsmen_city", "Craftsmen city", "REVOLUTION_WORLD_POPULATION.md:33"},
        {Feature::PackAnimals, Key(2010, 11, 3), "pack_animals", "pack horses and llamas", "M3_7_RESOURCE_ECONOMY.md:225"},
        {Feature::StoreCrystal, Key(2010, 11, 20), "store_crystal", "Reagent Crystal renamed Store Crystal", "M3_7_RESOURCE_ECONOMY.md:389"},
        {Feature::AntiMacroCodeScreen, Key(2011, 2, 22), "antimacro_code", "anti-macro code screen during production", "REVOLUTION_ANTIMACRO_SPEC.md:28"},
        {Feature::GateOneReagent, Key(2011, 3, 24), "gate_1_reagent", "Gate Travel reagents 6 -> 1", "REVOLUTION_RULESET_PROFILE.md:117"},
        {Feature::CookingBatch10, Key(2011, 6, 1), "cooking_batch_10", "cooking batch (skill/100)*10", "REVOLUTION_PRODUCTION_CHAINS.md:691"},
        {Feature::GuildRunebooks, Key(2012, 1, 7), "guild_runebooks", "guild runebooks from guild stones", "REVOLUTION_RUNBOOK_SPEC.md:42"},
        {Feature::FifteenFixedChests, Key(2012, 3, 22), "fixed_chests_15", "15 fixed chests, guardians removed", "REVOLUTION_WORLD_POPULATION.md:103"},
        {Feature::AntiMacroDisconnect, Key(2016, 1, 21), "antimacro_disconnect", "anti-macro: one minute or disconnect", "REVOLUTION_ANTIMACRO_SPEC.md:29"},
    };
    if (count) *count = static_cast<int>(sizeof(kChanges) / sizeof(kChanges[0]));
    return kChanges;
}

inline i32 DateOf(Feature f) {
    int n = 0;
    const Change* c = Changes(&n);
    for (int i = 0; i < n; ++i) if (c[i].feature == f) return c[i].date;
    return 0;
}

// Has this change happened by `date`?
inline bool Active(Feature f, i32 date) {
    const i32 d = DateOf(f);
    return d != 0 && date >= d;
}

inline bool InProfile(i32 date) { return date >= kProfileStart && date <= kProfileEnd; }

inline bool Valid(i32 date) {
    const i32 y = date / 10000, m = date / 100 % 100, d = date % 100;
    return y >= 1997 && y <= 2100 && m >= 1 && m <= 12 && d >= 1 && d <= 31;
}

// "2009-05-14" or "20090514" -> 20090514; 0 when unreadable.
inline i32 Parse(const std::string& text) {
    int y = 0, m = 0, d = 0;
    if (std::sscanf(text.c_str(), "%d-%d-%d", &y, &m, &d) == 3 ||
        (text.size() == 8 && std::sscanf(text.c_str(), "%4d%2d%2d", &y, &m, &d) == 3)) {
        const i32 k = Key(y, m, d);
        return Valid(k) ? k : 0;
    }
    return 0;
}

inline std::string Format(i32 date) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", date / 10000, date / 100 % 100, date % 100);
    return buf;
}

// Dated mechanics a bot plans with.
inline i32 RecallReagentsEach(i32 date) { return Active(Feature::RecallOneReagent, date) ? 1 : 3; }
// The campfire batch at GM was 40 (14.04.2008), 80 (12.04.2009), 100
// (01.06.2011), linear in skill (REVOLUTION_PRODUCTION_CHAINS.md:689-691).
inline i32 CookingBatch(i32 date, i32 skillTenths) {
    const i32 gm = Active(Feature::CookingBatch10, date) ? 100 : Active(Feature::CookingBatch8, date) ? 80 : 40;
    return skillTenths * gm / 1000;
}
inline i32 RunebookPages(i32 date) { return Active(Feature::RunebookEightPages, date) ? 8 : 0; }  // 0 = UNKNOWN before

}  // namespace uo::era
