#include "uo/life.h"
#include "uo/professions.h"
#include "uo/faucets.h"
#include "uo/vendor_policy.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace uo::life {

const char* PlanViolationName(PlanViolation v) {
    switch (v) {
        case PlanViolation::None:                  return "none";
        case PlanViolation::SkillBudgetExceeded:   return "skill_budget_exceeded";
        case PlanViolation::PerSkillCap:           return "per_skill_cap";
        case PlanViolation::InactiveSkill:         return "inactive_skill";
        case PlanViolation::NegativeTarget:        return "negative_target";
        case PlanViolation::StatTotalExceeded:     return "stat_total_exceeded";
        case PlanViolation::StatPerCapExceeded:    return "stat_per_cap_exceeded";
        case PlanViolation::CreationTooRich:       return "creation_too_rich";
        case PlanViolation::RejectedCreationSplit: return "rejected_creation_split";
        case PlanViolation::Count:                 break;
    }
    return "?";
}

PlanCheck ValidatePlan(const rules::Profile& p, const BuildPlan& plan) {
    PlanCheck out;

    // --- the 700-point budget ---------------------------------------------
    for (const SkillTarget& s : plan.skills) {
        if (s.tenths < 0) {
            out.violation = PlanViolation::NegativeTarget;
            out.skillId = s.skillId;
            return out;
        }
        if (s.tenths > p.perSkillCapTenths) {
            out.violation = PlanViolation::PerSkillCap;
            out.skillId = s.skillId;
            return out;
        }
        if (!rules::SkillActive(s.skillId)) {
            // The reason Resisting Spells must not creep back in: uo-offline's
            // T2A dexxer template carries it, and a generic T2A template is
            // not evidence about Revolution. M3.6 found it inactive here.
            out.violation = PlanViolation::InactiveSkill;
            out.skillId = s.skillId;
            return out;
        }
        out.resolvedTenths += s.tenths;
    }

    if (plan.unresolvedTenths < 0) {
        out.violation = PlanViolation::NegativeTarget;
        return out;
    }

    out.plannedTotalTenths = out.resolvedTenths + plan.unresolvedTenths;
    if (out.plannedTotalTenths > p.totalSkillCapTenths) {
        out.violation = PlanViolation::SkillBudgetExceeded;
        return out;
    }

    // --- the 225 / 100 stat rule ------------------------------------------
    out.statTotal = plan.targetStr + plan.targetDex + plan.targetInt;
    if (plan.targetStr > p.perStatCap || plan.targetDex > p.perStatCap ||
        plan.targetInt > p.perStatCap) {
        out.violation = PlanViolation::StatPerCapExceeded;
        return out;
    }
    if (out.statTotal > p.totalStatCap) {
        out.violation = PlanViolation::StatTotalExceeded;
        return out;
    }

    // --- creation is initialisation, not a request for final stats --------
    //
    // Source-X clamps to 60 per stat / 80 total and 50 per skill / 100 total
    // (CChar::InitPlayer). Asking for more is not an exploit -- the server
    // simply clamps it -- but it means the plan does not know what it will
    // actually get, and every character in this project that started life
    // with an unrealistic stat request died.
    const i32 createTotal = plan.createStr + plan.createDex + plan.createInt;
    if (createTotal > 80 || plan.createStr > 60 || plan.createDex > 60 ||
        plan.createInt > 60) {
        out.violation = PlanViolation::CreationTooRich;
        return out;
    }
    i32 createSkillTotal = 0;
    for (const SkillTarget& s : plan.createSkills) {
        if (s.tenths < 0 || s.tenths > 500) {   // 50.0 per skill, in tenths
            out.violation = PlanViolation::CreationTooRich;
            out.skillId = s.skillId;
            return out;
        }
        createSkillTotal += s.tenths;
    }
    if (createSkillTotal > 1000) {              // 100.0 total, in tenths
        out.violation = PlanViolation::CreationTooRich;
        return out;
    }

    // --- the split that killed six characters -----------------------------
    //
    // STR 55 / DEX 15 / INT 10 is explicitly rejected by the M4 plan: six
    // characters died solo on it against a single awake animal, one from full
    // health in about 34 seconds. This is a hard refusal rather than a
    // warning, because the failure was silent -- the character created fine
    // and died later, so nothing at creation time flagged it.
    if (plan.createStr == 55 && plan.createDex == 15 && plan.createInt == 10) {
        out.violation = PlanViolation::RejectedCreationSplit;
        return out;
    }

    out.ok = true;
    return out;
}

BuildPlan FrontierLumberjackSwordsman() {
    BuildPlan plan;
    plan.family = "frontier_lumberjack_swordsman";

    // The five skills docs/M4_LIFECYCLE_PLAN.md names, at 100.0 each.
    plan.skills = {
        {rules::kLumberjacking, 1000},
        {rules::kSwordsmanship, 1000},
        {rules::kTactics,       1000},
        {rules::kAnatomy,       1000},
        {rules::kHealing,       1000},
    };

    // 700.0 budget, 500.0 spent. The remaining 200.0 is UNRESOLVED ON PURPOSE.
    // The M4 plan names five skills and stops; filling the rest would be
    // inventing a canonical Revolution build, which the brief forbids. It is
    // carried as a number so the budget check is still exact.
    plan.unresolvedTenths = 2000;

    // 100 / 100 / 25 = 225. Straight off the M3.8 evidence list, and
    // independently corroborated by uo-offline's T2A dexxer stats.
    plan.targetStr = 100;
    plan.targetDex = 100;
    plan.targetInt = 25;

    // MEASURED, not reasoned. 40/35/5 survived the whole kill-carve-loot
    // chain where 55/15/10 died six times, which points at DEX -- swing speed
    // and evasion -- rather than raw strength.
    plan.createStr = 40;
    plan.createDex = 35;
    plan.createInt = 5;

    // Creation skills are chosen for the KIT as much as the skill, from the
    // shard's own newbie templates (templates_special/sp_tm_newbie.scp):
    //   [NEWBIE LUMBERJACKING] -> i_hatchet    (the axe the whole loop needs)
    //   [NEWBIE SWORDSMANSHIP] -> i_katana     (a real weapon, not Wrestling)
    //   [NEWBIE HEALING]       -> i_bandage,50 + i_scissors
    // 40 + 40 + 20 = 100.0, exactly the server's own creation ceiling.
    plan.createSkills = {
        {rules::kLumberjacking, 400},
        {rules::kSwordsmanship, 400},
        {rules::kHealing,       200},
    };

    return plan;
}

std::string MakeIdentityId(const std::string& account, const std::string& character) {
    std::string id;
    id.reserve(account.size() + character.size() + 1);
    auto append = [&id](const std::string& s) {
        for (char c : s) {
            const unsigned char u = static_cast<unsigned char>(c);
            if (std::isalnum(u)) id.push_back(static_cast<char>(std::tolower(u)));
            else if (c == '-' || c == '_') id.push_back(c);
            else id.push_back('_');
        }
    };
    append(account);
    id.push_back('.');
    append(character);
    return id;
}

i32 Observation::SkillTenths(int skillId) const {
    for (const SkillTarget& s : skills) {
        if (s.skillId == skillId) return s.tenths;
    }
    return 0;
}

i32 Observation::SkillSumTenths() const {
    i32 sum = 0;
    for (const SkillTarget& s : skills) sum += s.tenths;
    return sum;
}


// ---------------------------------------------------------------------------
// M5: a plan is a profession, restated as the thing the character asks for at
// creation plus the things it must earn.
// ---------------------------------------------------------------------------
BuildPlan PlanFromProfession(const prof::Profession& p) {
    BuildPlan plan;
    plan.family = p.id;

    for (const prof::SkillTargetSpec& t : p.targets) {
        plan.skills.push_back({t.skillId, t.tenths});
        plan.viaTrainer.push_back(t.viaTrainer);
        plan.priority.push_back(t.priority);
    }
    plan.unresolvedTenths = p.unresolvedTenths;

    plan.targetStr = p.targetStr;
    plan.targetDex = p.targetDex;
    plan.targetInt = p.targetInt;

    // THE REVOLUTION CREATION RULE: exactly two skills at 50.0, and about 50
    // stat points. Nothing else is requested and nothing else is granted.
    plan.createStr = p.startStr;
    plan.createDex = p.startDex;
    plan.createInt = p.startInt;
    plan.createSkills = {
        {p.startSkillA, prof::kRevolutionStartSkillEach},
        {p.startSkillB, prof::kRevolutionStartSkillEach},
    };
    return plan;
}

int NextSkillToBuy(const BuildPlan& plan, const Observation& obs,
                   i32 trainerCeilingTenths) {
    int best = -1;
    int bestPriority = -1;
    for (usize i = 0; i < plan.skills.size(); ++i) {
        if (i >= plan.viaTrainer.size() || !plan.viaTrainer[i]) continue;
        const SkillTarget& t = plan.skills[i];
        const i32 have = obs.SkillTenths(t.skillId);
        if (have >= t.tenths) continue;
        // Past what a trainer can give, paying one is simply waste -- the
        // character has to grind from here whether it pays or not.
        //
        // 300 is no longer a guess. It is this shard's own configured hard
        // ceiling: sphere.ini NPCTrainMax=300 (set by the owner 2026-08-28;
        // 420 had been Source-X's built-in default, CServerConfig.cpp:149, and
        // was never Revolution's) with NPCTrainPercent=30. Source-X computes
        // min(NPC's own skill x 30%, NPCTrainMax, the student's cap) in
        // CChar::NPC_GetTrainMax (CCharNPCStatus.cpp:514-541), so 30.0 is the
        // most any NPC here can teach, and an NPC below 100.0 in the skill
        // teaches less -- Alenne stopped at 21.9, which puts her Meditation at
        // about 73.0. The per-NPC part is still only knowable by asking, which
        // is why an actual refusal outranks this number below.
        if (have >= trainerCeilingTenths) continue;
        // Which is why the answers actually received outrank the guess. An
        // NPC of this trade has already said no to this skill; the ceiling is
        // below the character and only moves further below as it grows.
        bool refused = false;
        for (int r : obs.trainerRefusedSkills) {
            if (r == t.skillId) { refused = true; break; }
        }
        if (refused) continue;
        const int pri = (i < plan.priority.size()) ? plan.priority[i] : 0;
        if (pri > bestPriority) { bestPriority = pri; best = t.skillId; }
    }
    return best;
}
// ---------------------------------------------------------------------------
// What to make next.
//
// A crafter is not a gatherer with extra steps: it cannot begin at all until
// somebody sells it the inputs, and it must not begin at all unless what it
// makes has somewhere legitimate to go. Both of those are answered here, once,
// so the need model and the goal cannot disagree about them -- the way
// EARN_GOLD and its own need disagreed about the bank and deadlocked.
// ---------------------------------------------------------------------------
// Does this life fight on purpose? A lumberjack finishes fights that find it;
// a swordsman goes looking. Read off the build rather than off a list of
// archetype names: a profession that wants MORE than the 50.0 a weapon school
// is granted at creation intends to use it.
bool WantsToHunt(const prof::Profession& p) {
    for (const prof::SkillTargetSpec& t : p.targets) {
        const bool weapon = t.skillId == rules::kSwordsmanship ||
                            t.skillId == rules::kMaceFighting ||
                            t.skillId == rules::kFencing ||
                            t.skillId == rules::kArchery;
        if (weapon && t.tenths > 500) return true;
    }
    return false;
}

// --- the weapon-school basic, for a WantsToHunt fighter with empty hands ---
//
// "no gear yet -- shopping before the graveyard" (life/Runner.cpp, DoTrainCombat)
// hands off to REPLACE_EQUIPMENT when obs.weaponEquipped is false, but until
// this existed that errand could only ARM a weapon already sitting in the
// pack -- nothing could BUY one, so a WantsToHunt fighter created bare-handed,
// or stripped of its weapon by death, had no way back to a weapon at all.
//
// One row per weapon school, keyed by the SAME skill id WantsToHunt checks
// above, so the two can never disagree about which fighters this applies to.
// Each pick is BOTH the newbie kit's own choice for that skill AND the
// cheapest weapon of its school an NPC on this shard actually stocks.
const SchoolWeapon kSchoolWeapons[] = {
    // Swordsmanship -> katana. The newbie kit's own pick
    // (templates_special/sp_tm_newbie.scp:572-573) and
    // VENDOR_S_WEAPONS_BLADED's cheapest ACTUAL sword
    // (items/weapons/i_weapons.scp:966-988, VALUE=56;
    // templates/tm_vend.scp:1810/1842). i_cleaver_meat (VALUE=16) and
    // i_knife_butcher (VALUE=11) are technically cheaper and SKILL=
    // Swordsmanship too, but they are a cook's cleaver and a butcher's
    // knife -- kitchen tools with a mismatched TYPE, not this shard's idea
    // of a swordsman's first weapon -- and i_hatchet (VALUE=33) is the
    // lumberjack's own tool, already claimed by ArmAxe in Runner.cpp.
    { rules::kSwordsmanship, {0x13FE, 0x13FF}, "i_katana", "katana", false },
    // Fencing -> kryss. Newbie kit (sp_tm_newbie.scp:439-440); VALUE=51,
    // items/weapons/i_weapons.scp:994-1016. Sold at the SAME shop as the
    // katana, c_weaponsmith_blade (tm_vend.scp:1811/1843,
    // npcs/c_vendor_human.scp:6033 "the weaponsmith"). i_dagger is cheaper
    // (VALUE=21) and is every newbie's generic starting item
    // (sp_tm_newbie.scp:61), but [NEWBIE FENCING] overrides it with the
    // kryss specifically -- Revolution's own newbie kit treats the dagger
    // as a default, not a fencer's weapon.
    { rules::kFencing,       {0x1400, 0x1401}, "i_kryss",  "kryss",  false },
    // Macefighting -> club. Newbie kit (sp_tm_newbie.scp:499-500); VALUE=12,
    // items/weapons/i_weapons.scp:713-739. i_staff_gnarled is cheaper
    // (VALUE=8) but is the BEGGING template's staff
    // (sp_tm_newbie.scp:346-347), not a macefighter's. Sold by
    // c_weaponsmith_blunt -- a DIFFERENT NPC than the blade smith above but
    // the SAME paperdoll title "the weaponsmith"
    // (tm_vend.scp:1866-1876, npcs/c_vendor_human.scp:6199).
    { rules::kMaceFighting,  {0x13B3, 0x13B4}, "i_club",   "club",   false },
    // Archery -> bow. Newbie kit (sp_tm_newbie.scp:330-332); VALUE=20, the
    // cheapest of the school (items/weapons/i_weapons_archery.scp:11-38).
    // Sold both by c_bowyer (tm_vend.scp:1616/1628, unambiguous title "the
    // bowyer") and by c_weaponsmith_blade (tm_vend.scp:1802/1834) -- the
    // bowyer is offered as a second seller by the caller precisely because
    // its title, unlike "the weaponsmith", is never shared with a shop that
    // stocks something else.
    { rules::kArchery,       {0x13B1, 0x13B2}, "i_bow",    "bow",    true  },
};

const SchoolWeapon* SchoolWeaponFor(const prof::Profession& p) {
    for (const prof::SkillTargetSpec& t : p.targets) {
        if (t.tenths <= 500) continue;
        for (const SchoolWeapon& w : kSchoolWeapons)
            if (t.skillId == w.skill) return &w;
    }
    return nullptr;
}

// How to reach one output through the shard's legacy craft menus. Two levels
// at most, and both strings are matched as case-insensitive substrings.
//
// Inscription is nested -- the blank scroll opens "Spell Circles" and the
// spell lives one level down (sm_legacy_inscription.scp:12-31, 93-118).
// Bowcraft is flat, its options named "<name> (<resmake>)"
// (sm_legacy_bowcraft.scp:13-33). Nothing here is inferred from generic UO.
// CraftMenuPath is declared in uo/life.h so a test can assert a route without
// a server; the table itself stays here, next to the goal that walks it.
const CraftMenuPath kCraftMenus[] = {
    {"i_scroll_poison",      "Spell Circle 3", "poison",  nullptr},
    {"i_scroll_recall",      "Spell Circle 4", "recall",  nullptr},
    {"i_bow",                "bow",            nullptr,   nullptr},
    {"i_crossbow",           "crossbow",       nullptr,   nullptr},
    {"i_arrow_shaft",        "arrow_shaft",    nullptr,   nullptr},
    // BLACKSMITHING. Corwyn reached 58 ingots and then stopped dead on
    // "no menu path known for i_dagger" -- the table had no smith entry at
    // all, so the whole mine -> smelt -> smith -> sell chain ended one step
    // from the end.
    //
    // Three levels, from sm_legacy_blacksmithing.scp:
    //   ON=i_sword_viking Weapons          -> ON=i_sword_viking Swords & Blades
    //   -> ON=i_dagger <name> (<resmake>)
    // where <name> is the itemdef's NAME. If this shard serves the newer
    // def_blacksmithing gump instead, its categories are clilocs ("Bladed",
    // 1011081) -- the failure branch below prints what the menu ACTUALLY
    // offered, which is how to settle it without guessing twice.
    {"i_dagger",             "Weapons",        "Swords & Blades", "dagger"},
    // ALCHEMY IS A FLAT MENU -- sm_legacy_alchemy.scp has no categories, just
    // "ON=i_potion_Poison <name> (<resmake>)" straight off the mortar. But the
    // names are suffixes of one another and the menu lists them in this order:
    //     Lesser Poison / Poison / Greater Poison / Deadly Poison
    // so a plain substring search for "poison" finds LESSER poison first and
    // quietly brews the wrong thing. A leading '^' means match the START of
    // the option instead, which only the plain "Poison" satisfies.
    {"i_potion_poisonless",  "^Lesser Poison", nullptr,   nullptr},
    {"i_potion_poison",      "^Poison",        nullptr,   nullptr},
    {"i_potion_poisongreat", "^Greater Poison",nullptr,   nullptr},
    {"i_potion_poisondeadly","^Deadly Poison", nullptr,   nullptr},
    {"i_spear_short",        "Weapons",        "Spears and Forks", "short spear"},
    // COOKING. Two levels, from sm_legacy_cooking.scp (this shard runs the
    // legacy menu: crafting_settings.scp has scp.NewCrafting_Cooking=0):
    //   ON=i_ribs_cooked Barbecue -> ON=i_fish_cut_cooked <name> (<resmake>)
    // where <name> resolves off tiledata for 0x097B, "fish steak" -- the
    // itemdef carries no NAME= of its own. Matching is case-insensitive
    // substring, and no other Barbecue entry contains it.
    {"i_fish_cut_cooked",    "Barbecue",       "fish steak",      nullptr},
    // CARPENTRY. The board is the ONE entry that sits on the top level of
    // sm_carpentry -- every other option there opens a submenu:
    //   sm_legacy_carpentry.scp:15  ON=i_board boards
    //   sm_legacy_carpentry.scp:16  MAKEITEM=i_board
    // so the path is flat and the option text is literally "boards". No other
    // top-level option contains that substring ("bulletin board" is singular
    // and lives one level down, in sm_wood_misc). Without this row Cyras,
    // Halain and Vorar spent the whole 2026-09-02 wave on "no menu path known
    // for i_board" -- 45 refusals, no boards, and the carpenter's entire
    // log -> board -> furniture chain stopped at its first step.
    {"i_board",              "boards",         nullptr,   nullptr},
    // TINKERING. Two levels, from sm_legacy_tinkering.scp:
    //   :18  ON=i_clock_parts Parts   -> SKILLMENU=sm_parts
    //   :201 ON=i_gears <name> (<resmake>)  (inside sm_parts)
    // i_gears carries no NAME= of its own (i_profession.scp:1055-1064), so
    // <name> resolves off tiledata for 0x1053 -- "gears" -- the same way the
    // fish steak above does. "gears" appears on no other sm_parts option, and
    // "Parts" on no other top-level one. Serena: 30 refusals, no gears.
    {"i_gears",              "Parts",          "gears",   nullptr},
    // TAILORING. Legacy menu (crafting_settings.scp:32
    // NewCrafting_Tailoring=0). The ROOT is not chosen by name: the sewing
    // kit arms a target cursor (CClientUse.cpp:551) and the targeted item's
    // TYPE picks sm_tailor_cloth (IT_CLOTH) or sm_tailor_leather
    // (IT_LEATHER/IT_HIDE) (CClientTarg.cpp:2383-2399) -- DoCraft gives the
    // cursor the recipe's first input, so cloth recipes land in the cloth
    // root and leather recipes in the leather root. From
    // sm_legacy_tailoring.scp:
    //   sm_tailor_cloth   -> Shirts        -> sm_cloth_shirts   -> robe
    //   sm_tailor_cloth   -> Misc.         -> sm_cloth_misc     -> sash
    //   sm_tailor_leather -> Leather Armour-> sm_leather_armor  -> leather tunic
    // Leaf text resolves off tiledata (no NAME= on these itemdefs), same as
    // i_dagger. No sibling option in those submenus contains the leaf
    // substring (sphere-expert check, 2026-09-02). Aelia and Amara had no
    // tailoring row at all -- three strikes and "cannot be made".
    {"i_sash",               "Misc.",          "sash",           nullptr},
    {"i_robe",               "Shirts",         "robe",           nullptr},
    {"i_leather_tunic",      "Leather Armour", "leather tunic",  nullptr},
};

const CraftMenuPath* CraftMenuFor(const std::string& item) {
    for (const CraftMenuPath& m : kCraftMenus) {
        if (item == m.item) return &m;
    }
    return nullptr;
}

void CraftFocus::NoteMade(const char* item, i64 nowMs) {
    if (!item || !*item) return;
    if (last_ == item) {
        ++run_;
    } else {
        // Something else came off the bench, so the streak is over -- the same
        // rule Planner::NoteRan uses one level up.
        last_ = item;
        run_ = 1;
    }
    lastMs_ = nowMs;
}

bool CraftFocus::Satiated(const char* item, i64 nowMs) const {
    if (!item || !*item || last_.empty()) return false;
    if (last_ != item) return false;
    if (run_ < kFocusRun) return false;
    if (lastMs_ <= 0) return false;
    return nowMs - lastMs_ < kFocusFadeMs;
}

void CraftFocus::NoteNoRoute(const char* item) {
    if (!item || !*item) return;
    for (std::pair<std::string, i32>& e : noRoute_) {
        if (e.first == item) { ++e.second; return; }
    }
    noRoute_.emplace_back(item, 1);
}

bool CraftFocus::Unreachable(const char* item) const {
    if (!item || !*item) return false;
    for (const std::pair<std::string, i32>& e : noRoute_) {
        if (e.first == item) return e.second >= kNoRouteStrikes;
    }
    return false;
}

CraftIntent ChooseCraft(const prof::Profession& p, const Observation& obs,
                        i32 batch, const CraftFocus* focus) {
    CraftIntent out;
    // A fully-stocked recipe this life has just spent a sitting on. Kept, not
    // discarded: it is the answer if nothing else can be made.
    CraftIntent satiated;
    // The best candidate seen so far whose inputs are NOT all present, kept so
    // a fully-stocked recipe later in the list can win instead.
    CraftIntent firstWorkable;
    // ...and the best whose shortfall is at least PURCHASABLE, which beats one
    // that is short of something no vendor may sell.
    CraftIntent firstBuyable;
    if (batch < 1) batch = 1;

    for (const std::string& made : p.produces) {
        // ONLY WHAT MAY BE SOLD -- TO ANYONE.
        //
        // This asked faucet::AllowedForItem, which answers "will an NPC pay
        // for this", and used the answer for "is this worth making". The
        // parenthesis that used to stand here said a player-market good would
        // belong once the player market could complete a sale, and "it cannot
        // yet". IT CAN: Tarath sold 48 logs to Durnholde for 40gp on
        // 2026-08-30, and wave 2 logged goal_completed=TRADE_WITH_PLAYER for
        // Elvar and Odessa on 2026-09-01.
        //
        // The stale premise cost three lives their entire trade. A tailor, a
        // merchant/tinker and a lumberjack/carpenter make NOTHING an NPC may
        // buy -- that is the deliberate shape of Revolution's economy, not a
        // gap -- so every entry of their `produces` was skipped here and the
        // need model reported "this life makes nothing sellable" all session
        // (Aelia x44, Odessa x8, wave 2 triage clusters 7).
        //
        // The right question is "would ANYONE buy it": an NPC route or a
        // player-market one. This opens no NPC counter -- selling still asks
        // AllowedForItem/MaySellToNpc, unchanged.
        //
        // The recipe is looked up FIRST now, because the class rule below
        // leans on it: an item nothing in the registry names is accepted only
        // when it is genuinely this trade's own work, and having a recipe on
        // this life's own `produces` list is what establishes that.
        const prod::Recipe* r = prod::FindRecipe(made.c_str());
        if (!r) continue;

        // A ROUTE THAT HAS ALREADY REFUSED IS NOT A CANDIDATE.
        //
        // The recipe graph says i_board is made of one log; it does not know
        // whether CRAFT can reach the carpentry menu entry that makes it. When
        // the goal has come back three times saying it cannot, offering the
        // same output again is how 45 refusals and no boards happen (wave
        // 2026-09-02). Skip it and let the next entry of `produces` have the
        // sitting. See CraftFocus::NoteNoRoute.
        if (focus && focus->Unreachable(made.c_str())) continue;

        const faucet::SaleRoute route = faucet::RouteForItem(made.c_str());
        const bool sellable =
            route == faucet::SaleRoute::Npc ||
            route == faucet::SaleRoute::PlayerMarket ||
            (route == faucet::SaleRoute::Unrecorded &&
             faucet::OutputClassIsPlayerMarket(p.id.c_str()));
        if (!sellable) continue;
        // Gathered things are not crafted things. A fisher's "produces" holds
        // fish, and fish come out of the sea.
        if (!r->inputs[0].item) continue;
        // A FIGHTER'S CLOTH IS NOT A CRAFT SITTING. The melee schools list
        // i_cloth in `produces` so they can SELL it (owner ruling 2026-09-02:
        // warriors shear, kill, carve, spin, weave and cut; tailors buy the
        // cloth). Chosen here it would raise NeedCloth's player-first WTB for
        // a bolt and a Craft sitting with no menu -- HARVEST_WOOL owns the
        // whole chain for them, from the sheep.
        // (Same four names as IsWoolChainMaterial; spelled out because some
        // test binaries link Identity.cpp without Needs.cpp.)
        if (WantsToHunt(p) &&
            (made == "i_wool" || made == "i_yarn_ball" ||
             made == "i_cloth_bolt" || made == "i_cloth"))
            continue;

        // The recipe's own skill requirements, against what the SERVER last
        // reported. Never against the build plan: a plan is an intention.
        const bool skillsOk =
            (r->skillId < 0 || obs.SkillTenths(r->skillId) >= r->skillTenths) &&
            (r->skillId2 < 0 || obs.SkillTenths(r->skillId2) >= r->skillTenths2);
        if (!skillsOk) {
            if (!out.item) {
                out.item = nullptr;
                out.why = "the skills for everything this life makes are short";
            }
            continue;
        }

        // WHAT CAN ACTUALLY BE MADE BEATS WHAT COMES FIRST IN THE LIST.
        //
        // This used to return on the first recipe with inputs, whatever the
        // pack held -- so the ORDER of `produces` silently decided what a life
        // could ever make. A fisher lists i_fish_cut_raw before
        // i_fish_cut_cooked, and DoFish cuts every whole fish the instant it
        // is caught, so the pack essentially never holds i_fish_big_1. Marla
        // therefore landed on i_fish_cut_raw, reported it short of a whole
        // fish, and returned -- with 36 raw steaks and two kindling in her
        // pack and cooking never once considered. Ten times in one run:
        //
        //   BLOCKED_NEED BUY_SUPPLIES: short of an input no NPC may
        //   legitimately sell it (i_fish_cut_raw needs 5 x i_fish_big_1)
        //
        // She sold raw steaks at 2gp while VENDOR_B_COOK and VENDOR_B_FISHER
        // both carry BUY=i_fish_cut_cooked,{4 24} (tm_vend.scp:798 and :1104)
        // -- the same two NPCs she was already selling to.
        //
        // So: remember the first workable candidate, but keep looking, and
        // take the first whose inputs are all present. Falling back to the
        // earlier one preserves the old answer whenever nothing is fully
        // stocked, which is what the "inputs are short" callers expect.
        CraftIntent here;
        here.item = r->output;
        here.skillsMet = true;
        for (const prod::Ingredient& in : r->inputs) {
            if (!in.item || in.qty <= 0) continue;
            const i32 want = in.qty * batch;
            const i32 have = market::QtyOf(obs.pack, in.item);
            if (have < want) {
                prod::Ingredient shortfall;
                shortfall.item = in.item;
                shortfall.qty = want - have;
                here.missing.push_back(shortfall);
            }
        }
        if (here.missing.empty()) {
            here.why = "every input is in the pack";
            // ROTATE THE FOCUS. Everything else about this loop is unchanged;
            // this is the one place a WORKABLE candidate is now allowed to
            // step aside for the next workable one.
            if (focus && focus->Satiated(here.item, obs.nowMs)) {
                if (!satiated.item) {
                    satiated = here;
                    satiated.why = "every input is in the pack, but this life "
                                   "has just spent a sitting on it";
                }
                continue;
            }
            return here;
        }
        here.why = "inputs are short";

        // A SHORTFALL YOU CAN BUY BEATS ONE NOBODY SELLS.
        //
        // Being short is not one state. i_fish_cut_raw is short of a WHOLE
        // FISH, which no NPC may legitimately sell -- that is a dead end, and
        // NeedSupplies scores it 0.0 and says so. i_fish_cut_cooked is short
        // of KINDLING, which any provisioner sells for a few coins. Treating
        // those as equally good candidates is how Marla ended up parked on
        // the dead end with steaks in her pack:
        //
        //   BLOCKED_NEED BUY_SUPPLIES: short of an input no NPC may
        //   legitimately sell it (i_fish_cut_raw needs 5 x i_fish_big_1)
        //
        // ...while the recipe one line further down her produces list needed
        // only a purchasable thing. So rank the fallbacks: a recipe whose
        // shortfall can be bought is preferred over one whose cannot, and the
        // unbuyable one is kept only so the old message still appears when
        // nothing better exists.
        const bool buyable =
            !here.missing.empty() &&
            econ::CanBuyFromNPC(here.missing.front().item).allowed;
        if (buyable) {
            if (!firstBuyable.item) firstBuyable = here;
        } else if (!firstWorkable.item) {
            firstWorkable = here;
        }
    }
    // NOTHING ELSE IS WORKABLE, so the sitting continues. Repeating oneself
    // beats standing idle, and this is what keeps the rotation a preference
    // rather than a ban.
    if (satiated.item) return satiated;
    if (firstBuyable.item) return firstBuyable;
    if (firstWorkable.item) return firstWorkable;
    if (!out.why || !*out.why) out.why = "this life makes nothing sellable";
    return out;
}

}  // namespace uo::life
