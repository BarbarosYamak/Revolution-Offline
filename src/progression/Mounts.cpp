#include "uo/mounts.h"

#include <cstring>

namespace uo::mounts {

const char* EraName(Era e) {
    switch (e) {
        case Era::TargetEra:       return "TARGET_ERA";
        case Era::OutsideWindow:   return "OUTSIDE_WINDOW";
        case Era::StockSphereOnly: return "STOCK_SPHERE_ONLY";
        case Era::Count:           break;
    }
    return "?";
}

namespace {

// Revolution's mount requirements, confirmed by the project owner against
// /binek_bilgileri. Runtime figures read from
// runtime/scripts/npcs/c_monster_classic.scp.
//
// TWO SKILLS AT THE SAME VALUE. Every mount gates on Animal Taming AND Animal
// Lore. An earlier revision of this file modelled taming alone and so
// understated every cost by half.
//
// RANGES ARE ROLLED PER ANIMAL. Mustang 65.0-80.0 and Shire 65.0-95.0 are not
// display spreads: each individual has its own requirement inside the band. A
// character at the minimum may succeed on some and never on others, which is
// why min and max are both carried and why there is a CanTameReliably.
//
// A CORRECTION I MADE AND HAD TO UNMAKE: an intermediate revision took
// frenzied to 75.0 and nightmare to 98.1, and turned both ranges into fixed
// gates at their upper bound. That was wrong -- the archived page was right all
// along. The numbers below are the confirmed ones, and the episode is recorded
// here because "the source we already had was correct" is exactly the kind of
// finding this project keeps having to re-learn.
//
// SUPPLY (/spawntakip_sistemi): a calendar regenerated every Monday, 49 mounts
// a week, 7 a day. Horse and Llama are NOT on it -- they are common spawns.
// The 49 are the contested tier, and a hard weekly cap fought over by every
// tamer on the shard is the purest player-market good in the archive.
const MountRule kMounts[] = {
    // defname, body, tamingMin, loreMin, randomRange, tamingMax, loreMax,
    // runtimeTaming, era, botLegal, weeklySupply, source

    // ---- common: not rationed ---------------------------------------------
    {"c_horse_tan",      0x00C8, 531, 531, false, 531, 531, 291, Era::TargetEra, true, 0,
     "REV 53.1 taming + 53.1 lore; runtime TAMING=29.1; not on supply calendar"},
    {"c_horse_brown_dk", 0x00CC, 531, 531, false, 531, 531, 291, Era::TargetEra, true, 0,
     "REV 53.1 + 53.1; runtime TAMING=29.1; not on supply calendar"},
    {"c_horse_gray",     0x00E2, 531, 531, false, 531, 531, 291, Era::TargetEra, true, 0,
     "REV 53.1 + 53.1; runtime c_monster_classic.scp:5063 TAMING=29.1"},
    {"c_horse_brown_lt", 0x00E4, 531, 531, false, 531, 531, 291, Era::TargetEra, true, 0,
     "REV 53.1 + 53.1; runtime TAMING=29.1"},
    {"c_llama",          0x00DC, 551, 551, false, 551, 551, 351, Era::TargetEra, true, 0,
     "REV 55.1 + 55.1; runtime TAMING=35.1; not on supply calendar"},

    // ---- the 49-a-week contested tier -------------------------------------
    {"c_ostard_desert",   0x00D2, 651, 651, false, 651, 651, 291, Era::TargetEra, true, 10,
     "REV 65.1 + 65.1; runtime TAMING=29.1 (36-point gap, widest here); supply 10/wk"},
    {"c_ostard_forest",   0x00DB, 651, 651, false, 651, 651,  -1, Era::TargetEra, true, 10,
     "REV 65.1 + 65.1, same as desert; runtime has NO TAMING line; supply 10/wk"},
    {"c_ostard_frenzied", 0x00D3, 771, 771, false, 771, 771,  -1, Era::TargetEra, true, 7,
     "REV 77.1 + 77.1; runtime has NO TAMING line; supply 7/wk"},
    {"c_ostard_mid",      0x00D4, 800, 800, false, 800, 800,  -1, Era::TargetEra, true, 7,
     "REV 80.0 + 80.0; runtime has NO TAMING line; supply 7/wk"},

    // Rolled ranges: min is where an attempt becomes possible, max where it
    // becomes certain.
    {"c_horse_mustang",   0x0000, 650, 650, true,  800, 800,  -1, Era::TargetEra, true, 5,
     "REV 65.0-80.0 RANDOM per animal, taming and lore alike; not in runtime; supply 5/wk"},
    {"c_horse_shire",     0x0000, 650, 650, true,  950, 950,  -1, Era::TargetEra, true, 5,
     "REV 65.0-95.0 RANDOM per animal, taming and lore alike; not in runtime; supply 5/wk"},

    {"c_kirin",           0x0084, 900, 900, false, 900, 900, 1050, Era::TargetEra, true, 2,
     "REV 90.0 + 90.0; runtime TAMING=105.0 -- runtime is STRICTER; supply 2/wk"},
    {"c_unicorn",         0x0007, 981, 981, false, 981, 981,  800, Era::TargetEra, true, 1,
     "REV 98.1 + 98.1; runtime TAMING=80.0; supply 1/wk"},
    {"c_steed",           0x0000, 999, 999, false, 999, 999,   -1, Era::TargetEra, true, 1,
     "REV 99.9 + 99.9; not found in runtime; supply 1/wk -- rarest tier"},
    {"c_nightmare",       0x0074, 999, 999, false, 999, 999,   -1, Era::TargetEra, true, 1,
     "REV 99.9 + 99.9; runtime has NO TAMING line; supply 1/wk -- rarest tier"},

    // ---- no Revolution evidence -------------------------------------------
    // Rideable in the runtime; absent from /binek_bilgileri. A bot may not tame
    // what we cannot date.
    {"c_horse_pack",      0x0123,  -1,  -1, false,  -1,  -1,  291, Era::StockSphereOnly, false, 0,
     "no /binek_bilgileri entry; runtime TAMING=29.1 -- historical gate UNKNOWN"},
    {"c_llama_pack",      0x0124,  -1,  -1, false,  -1,  -1,  291, Era::StockSphereOnly, false, 0,
     "no /binek_bilgileri entry; runtime TAMING=29.1 -- historical gate UNKNOWN"},
    {"c_ridgeback",       0x00BB,  -1,  -1, false,  -1,  -1,  800, Era::StockSphereOnly, false, 0,
     "not on /binek_bilgileri; runtime TAMING=80.0 -- no Revolution evidence"},
};

constexpr usize kMountCount = sizeof(kMounts) / sizeof(kMounts[0]);

}  // namespace

const std::vector<MountRule>& KnownMounts() {
    static const std::vector<MountRule> v(kMounts, kMounts + kMountCount);
    return v;
}

const MountRule* FindMount(const char* defname) {
    if (!defname) return nullptr;
    for (const auto& m : kMounts) {
        if (std::strcmp(m.defname, defname) == 0) return &m;
    }
    return nullptr;
}

namespace {

bool Gate(i32 tamingTenths, i32 loreTenths, const MountRule& m,
          bool useMax) {
    if (!m.botLegal) return false;
    const i32 t = useMax ? m.tamingMaxTenths : m.tamingMinTenths;
    const i32 l = useMax ? m.loreMaxTenths   : m.loreMinTenths;
    // An UNKNOWN requirement is not an open one. This is the whole reason the
    // fields are -1 rather than 0.
    if (t < 0 || l < 0) return false;
    return tamingTenths >= t && loreTenths >= l;
}

}  // namespace

bool CanAttemptTame(i32 tamingTenths, i32 loreTenths, const char* defname) {
    const MountRule* m = FindMount(defname);
    // Silence is not permission.
    if (!m) return false;
    return Gate(tamingTenths, loreTenths, *m, false);
}

bool CanTameReliably(i32 tamingTenths, i32 loreTenths, const char* defname) {
    const MountRule* m = FindMount(defname);
    if (!m) return false;
    return Gate(tamingTenths, loreTenths, *m, true);
}

i32 ReliableSkillCost(const char* defname) {
    const MountRule* m = FindMount(defname);
    if (!m) return -1;
    if (m->tamingMaxTenths < 0 || m->loreMaxTenths < 0) return -1;
    return m->tamingMaxTenths + m->loreMaxTenths;
}

bool RuntimeIsMorePermissive(const MountRule& m) {
    if (m.tamingMinTenths < 0 || m.runtimeTamingTenths < 0) return false;
    return m.runtimeTamingTenths < m.tamingMinTenths;
}

}  // namespace uo::mounts
