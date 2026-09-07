#pragma once

// ---------------------------------------------------------------------------
// WHAT A SPELL IS, WHAT IT COSTS, AND WHETHER THIS CHARACTER MAY PRACTISE WITH
// IT.
//
// THE FIRST DEFECT (live wave 2026-09-02, all four mages). PRACTICE_SKILL cast
// a spell every six seconds without ever asking what it CONSUMES:
//
//     [life] practice: casting spell 6 at myself to raise Magery (50.0, mana 48)
//     System: You lack Sulfurous Ash for this spell
//
// -- Aurelius 156 times, Illyria 298, Elara 322, Selene 310. runtime/sphere.ini
// :1136 reads ReagentsRequired=1; a mage on this shard pays for its casts.
//
// THE SECOND DEFECT (owner ruling 2026-09-02: "for mage to cast there are lots
// of skills, don't hard code Create Food"). The fix for the first defect was a
// HAND-PICKED TABLE OF TWELVE self-safe spells compiled into this header. A
// mage then practised with whatever those twelve happened to be, at whatever
// circle, forever -- not with its book and not at its level.
//
// So there is no table in this file any more. The spell list is DATA, exported
// from the shard's own script by tools/spellgen.py into
// data/revolution_spells.tsv (64 spells, circles 1-8), and loaded at runtime
// the way data/revolution_creatures.tsv already is. Every column is read off
// runtime/scripts/spells/spells_magery.scp: RESOURCES (reagents, no quantity
// prefix, so one of each per cast), MANAUSE, FLAGS and
// `SKILLREQ=MAGERY <n>` -- whose values are 10.0/20.0/.../80.0 with exactly
// eight spells at each, which is where `circle` comes from.
// ---------------------------------------------------------------------------

#include "uo/market.h"
#include "uo/types.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

namespace uo::spell {

// FLAGS, exactly the tokens that occur in spells_magery.scp. A token this list
// does not know makes the whole spell UNKNOWN (see `unknownFlags`) and the
// practice chooser then refuses to touch it -- a flag we cannot read might be
// the one that makes the spell dangerous or criminal.
enum SpellFlag : u32 {
    kFlagGood          = 1u << 0,
    kFlagHarm          = 1u << 1,
    kFlagDamage        = 1u << 2,
    kFlagCurse         = 1u << 3,
    kFlagField         = 1u << 4,
    kFlagSummon        = 1u << 5,
    kFlagArea          = 1u << 6,
    kFlagPlayerOnly    = 1u << 7,
    kFlagTargChar      = 1u << 8,
    kFlagTargXyz       = 1u << 9,
    kFlagTargObj       = 1u << 10,
    kFlagTargItem      = 1u << 11,
    kFlagTargDead      = 1u << 12,
    kFlagBless         = 1u << 13,
    kFlagHeal          = 1u << 14,
    kFlagResist        = 1u << 15,
    kFlagTick          = 1u << 16,
    kFlagDirAnim       = 1u << 17,
    kFlagFxTarg        = 1u << 18,
    kFlagFxBolt        = 1u << 19,
    kFlagNoUnparalyze  = 1u << 20,
    kFlagNoPrecast     = 1u << 21,
};

// A spell as the shard defines it. `reagents` stays a nullptr-terminated array
// of C strings so every call site written for the old hard-coded table keeps
// compiling; the strings live in the loader's own pool.
struct SpellDef {
    int         spell = 0;
    const char* name = "";
    const char* defname = "";
    const char* reagents[6] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    int         circle = 0;      // SKILLREQ / 10.0
    i32         minSkillTenths = 0;
    int         mana = 0;
    u32         flags = 0;
    bool        unknownFlags = false;
};

inline std::vector<SpellDef>& SpellTable() {
    static std::vector<SpellDef> t;
    return t;
}

// Pointer-stable storage for the strings the table points into.
inline std::deque<std::string>& SpellStrings() {
    static std::deque<std::string> s;
    return s;
}

inline u32 FlagBit(const std::string& tokenLower, bool* known) {
    struct Row { const char* tok; u32 bit; };
    static const Row kRows[] = {
        {"spellflag_good", kFlagGood},
        {"spellflag_harm", kFlagHarm},
        {"spellflag_damage", kFlagDamage},
        {"spellflag_curse", kFlagCurse},
        {"spellflag_field", kFlagField},
        {"spellflag_summon", kFlagSummon},
        {"spellflag_area", kFlagArea},
        {"spellflag_playeronly", kFlagPlayerOnly},
        {"spellflag_targ_char", kFlagTargChar},
        {"spellflag_targ_xyz", kFlagTargXyz},
        {"spellflag_targ_obj", kFlagTargObj},
        {"spellflag_targ_item", kFlagTargItem},
        {"spellflag_targ_dead", kFlagTargDead},
        {"spellflag_bless", kFlagBless},
        {"spellflag_heal", kFlagHeal},
        {"spellflag_resist", kFlagResist},
        {"spellflag_tick", kFlagTick},
        {"spellflag_dir_anim", kFlagDirAnim},
        {"spellflag_fx_targ", kFlagFxTarg},
        {"spellflag_fx_bolt", kFlagFxBolt},
        {"spellflag_nounparalyze", kFlagNoUnparalyze},
        {"spellflag_noprecast", kFlagNoPrecast},
    };
    for (const Row& r : kRows) {
        if (tokenLower == r.tok) { if (known) *known = true; return r.bit; }
    }
    if (known) *known = false;
    return 0;
}

// --- loading -----------------------------------------------------------------

inline int LoadSpellTableFromText(const std::string& tsv) {
    std::vector<SpellDef>& table = SpellTable();
    table.clear();
    SpellStrings().clear();
    usize pos = 0;
    bool first = true;
    while (pos <= tsv.size()) {
        usize nl = tsv.find('\n', pos);
        if (nl == std::string::npos) nl = tsv.size();
        std::string line = tsv.substr(pos, nl - pos);
        pos = nl + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) { if (nl >= tsv.size()) break; continue; }
        if (first) { first = false; continue; }          // header
        std::vector<std::string> col;
        usize p = 0;
        while (true) {
            usize t = line.find('\t', p);
            if (t == std::string::npos) { col.push_back(line.substr(p)); break; }
            col.push_back(line.substr(p, t - p));
            p = t + 1;
        }
        if (col.size() < 8) { if (nl >= tsv.size()) break; continue; }
        SpellDef d;
        d.spell = std::atoi(col[0].c_str());
        SpellStrings().push_back(col[1]);
        d.defname = SpellStrings().back().c_str();
        SpellStrings().push_back(col[2]);
        d.name = SpellStrings().back().c_str();
        d.circle = std::atoi(col[3].c_str());
        d.minSkillTenths = static_cast<i32>(std::atoi(col[4].c_str()));
        d.mana = std::atoi(col[5].c_str());
        // flags: pipe-separated, already lowercase out of the generator.
        usize fp = 0;
        while (fp < col[6].size() || (fp == 0 && !col[6].empty())) {
            usize bar = col[6].find('|', fp);
            std::string tok = col[6].substr(
                fp, bar == std::string::npos ? std::string::npos : bar - fp);
            if (!tok.empty()) {
                bool known = false;
                d.flags |= FlagBit(tok, &known);
                if (!known) d.unknownFlags = true;
            }
            if (bar == std::string::npos) break;
            fp = bar + 1;
        }
        // reagents: comma-separated defnames, at most five.
        usize rp = 0, ri = 0;
        while (ri < 5 && rp <= col[7].size() && !col[7].empty()) {
            usize c = col[7].find(',', rp);
            std::string tok = col[7].substr(
                rp, c == std::string::npos ? std::string::npos : c - rp);
            if (!tok.empty()) {
                SpellStrings().push_back(tok);
                d.reagents[ri++] = SpellStrings().back().c_str();
            }
            if (c == std::string::npos) break;
            rp = c + 1;
        }
        d.reagents[ri] = nullptr;
        if (d.spell > 0) table.push_back(d);
        if (nl >= tsv.size()) break;
    }
    return static_cast<int>(table.size());
}

inline bool SpellTableLoaded() { return !SpellTable().empty(); }

// Reads data/revolution_spells.tsv once. Idempotent: later calls are free.
inline int LoadSpellTable(const std::string& dataDir) {
    if (SpellTableLoaded()) return static_cast<int>(SpellTable().size());
    const std::string path = dataDir + "/revolution_spells.tsv";
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return 0;
    std::string text;
    char buf[4096];
    usize n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    std::fclose(f);
    return LoadSpellTableFromText(text);
}

inline const SpellDef* DefForSpell(int spell) {
    for (const SpellDef& d : SpellTable())
        if (d.spell == spell) return &d;
    return nullptr;
}

// --- what a fight may stand on -----------------------------------------------
//
// WHICH TARGET FLAG A SPELL CARRIES IS NOT WHICH TARGET IT TAKES.
//
// The first version of this test demanded `spellflag_targ_char`, and that is
// how Aurelius fought a whole session with two rungs. On this shard's export
// (data/revolution_spells.tsv) Harm and Magic Arrow are `spellflag_targ_char`
// but Fireball, Poison, Lightning and Energy Bolt are `spellflag_targ_obj` --
// the flag says the cursor accepts an object, and a character IS an object, so
// every one of them lands on a mobile exactly the same way. Filtering on
// targ_char alone deleted circles 3, 4 and 6 from every mage's ladder
// (run_gates/g_Aurelius.console.txt 2026-09-07 00:44: 14 casts, ids 10/11/12
// only, with Fireball, Poison and Lightning all in the book).
//
// What still has to be excluded is a target this character is not aiming at:
// a ground tile (targ_xyz), an item, a corpse, an area, a field, a summon.
inline bool TargetsAMobile(const SpellDef& d) {
    if (d.unknownFlags) return false;
    if (!(d.flags & (kFlagTargChar | kFlagTargObj))) return false;
    if (d.flags & (kFlagArea | kFlagField | kFlagSummon | kFlagTargDead |
                   kFlagTargXyz | kFlagTargItem))
        return false;
    return true;
}

// A RUNG THE ATTACK LADDER MAY STAND ON.
//
// `spellflag_damage` is not the whole of "this hurts": Poison is
// `spellflag_harm|spellflag_tick` with no damage flag, because Sphere applies
// it over time rather than in one blow. Requiring the damage flag is why
// Poison was never a rung and reached the fight only through a separate opener
// gated on the Poisoning SKILL -- which is the wrong skill entirely, since
// Poison the spell is cast out of Magery.
inline bool IsAttackRung(const SpellDef& d) {
    if (!TargetsAMobile(d)) return false;
    if (!(d.flags & kFlagHarm)) return false;
    return (d.flags & (kFlagDamage | kFlagTick)) != 0;
}

// The other half: something this character may cast on itself to stay alive.
inline bool IsHealRung(const SpellDef& d) {
    if (!TargetsAMobile(d)) return false;
    if (d.flags & kFlagHarm) return false;
    return (d.flags & kFlagHeal) != 0;
}

// --- what a caster carries ---------------------------------------------------

// THE EIGHT. Every Magery reagent on this shard, by defname, as the spell
// table and LackNeedleFor below both name them.
inline const char* const* Reagents(int* count) {
    static const char* const kAll[] = {
        "i_reag_black_pearl", "i_reag_blood_moss", "i_reag_garlic",
        "i_reag_ginseng",     "i_reag_mandrake_root", "i_reag_nightshade",
        "i_reag_spider_silk", "i_reag_sulfur_ash",
    };
    if (count) *count = 8;
    return kAll;
}

// HOW MANY OF EACH A MAGE CARRIES (owner ruling, 2026-09-06,
// .claude/agent-memory/revolution-god/mage-reagent-stock.md): sixty to seventy
// of EVERY one of the eight, not a working set sized off the one spell it
// happens to be practising. A mage who owns the reagents for Lightning and not
// for Fireball has a ladder with a hole in it, and the hole is only visible at
// the moment it needs the rung.
//
// The two that get seventy are the ones a mage actually burns fastest:
// mandrake root and black pearl are in Recall as well as in the ladder
// (Lightning takes mandrake, Fireball and Energy Bolt take black pearl), so
// the travel budget and the fight budget come out of the same pile. Bloodmoss
// is Recall's other half and stays at the base band -- it is spent only on
// travel.
inline constexpr i32 kReagentBandBase = 60;
inline constexpr i32 kReagentBandBurner = 70;

// WEALTH, NOT A CONSTANT (owner rule: thresholds are derived per character).
// A band of 60-70 across eight kinds is ~520 units; at the 3 gp Aurelius was
// charged that is ~1,560 gold of stock, which is most of a poor mage's purse
// and pocket change to a rich one. The steps below are a POLICY CHOICE about
// where "rich" begins, not a measured threshold: a mage who can restock the
// whole pouch several times over carries more of it, because the walk to the
// shop costs more than the reagents do.
inline i32 ReagentBandFor(const char* item, i32 gold) {
    if (!item) return 0;
    i32 band = (std::strcmp(item, "i_reag_mandrake_root") == 0 ||
                std::strcmp(item, "i_reag_black_pearl") == 0)
                   ? kReagentBandBurner
                   : kReagentBandBase;
    if (gold >= 30000)      band = band * 2;
    else if (gold >= 10000) band = band + band / 2;
    return band;
}

// WHEN TO GO SHOPPING FOR IT. Not "one short" -- a band checked exactly would
// send a mage back to the counter after every fight, and the Britain mage's
// shelf does not restock that fast anyway. A third of the band left is the
// point at which the next hunt is at risk, which is the same low-water shape
// the bank top-up already used (kReagentCarry / 5, Needs.cpp).
inline i32 ReagentRestockFloor(const char* item, i32 gold) {
    const i32 band = ReagentBandFor(item, gold);
    return band / 3;
}

// --- who may be practised on --------------------------------------------------
//
// Cast at oneself, alone, with nobody else involved. That rules out every harm
// flag (Revolution crime rules: a harmful spell aimed at an innocent is a
// crime, and aimed at oneself is self-harm) and every spell that needs a
// target this character is not: a ground tile, an item, a corpse.
//
// It also requires the spell to be BENEFICIAL -- good, bless or heal. That is
// a stronger rule than "not harmful", and it is deliberate: `spellflag_
// playeronly` alone also covers Incognito, Summon Creature and Dispel Field,
// which either change who this character appears to be or put something in the
// world. Practice must not do either. Create Food is in that same group and
// stays where it belongs, in the food errand (Runner::DoGetFood).
inline bool SafeToPractiseOnSelf(const SpellDef& d) {
    if (d.unknownFlags) return false;                       // UNKNOWN -> skip
    if (d.flags & (kFlagHarm | kFlagDamage | kFlagCurse | kFlagField |
                   kFlagSummon))
        return false;
    if (d.flags & (kFlagTargXyz | kFlagTargObj | kFlagTargItem | kFlagTargDead))
        return false;                                       // not a self target
    if (!(d.flags & (kFlagGood | kFlagBless | kFlagHeal))) return false;
    return true;
}

// THE JOURNAL LINE THAT NAMES A REAGENT, as a needle for a case-insensitive
// substring search. Sphere's wording is "You lack %s for this spell"
// (spell_try_noregs) with %s the item's own NAME -- confirmed live as
// "You lack Sulfurous Ash for this spell" and "You lack Mandrake Root for this
// spell". The needle keeps the word "lack" in front so a stray mention of a
// reagent elsewhere in the journal cannot be mistaken for a refusal, and stops
// at the shortest unambiguous stem because the exact spelling of the rest
// ("Spider Silk" / "Spider's Silk", "Bloodmoss" / "Blood Moss") is not settled
// by any itemdef on this shard -- the names come from tiledata.
inline const char* LackNeedleFor(const char* reagent) {
    if (!reagent) return nullptr;
    if (std::strcmp(reagent, "i_reag_black_pearl")   == 0) return "lack black";
    if (std::strcmp(reagent, "i_reag_blood_moss")    == 0) return "lack blood";
    if (std::strcmp(reagent, "i_reag_garlic")        == 0) return "lack garlic";
    if (std::strcmp(reagent, "i_reag_ginseng")       == 0) return "lack ginseng";
    if (std::strcmp(reagent, "i_reag_mandrake_root") == 0) return "lack mandrake";
    if (std::strcmp(reagent, "i_reag_nightshade")    == 0) return "lack nightshade";
    if (std::strcmp(reagent, "i_reag_spider_silk")   == 0) return "lack spider";
    if (std::strcmp(reagent, "i_reag_sulfur_ash")    == 0) return "lack sulfur";
    return nullptr;
}

// --- choosing what to practise with -----------------------------------------

// Everything the choice is allowed to know: what the BOOK holds, what the PACK
// holds, this character's own Magery and mana, which spells the SERVER has
// already refused this session, and how often each has been cast. No hidden
// state -- a bot cannot know a reagent is missing until it has looked in its
// own pack or been told.
struct PracticeSight {
    std::vector<int>           inBook;      // spell numbers read out of the book
    std::vector<market::Stock> pack;        // by itemdef defname
    std::vector<int>           uncastable;  // refused by the server this session
    i32                        magery = 0;  // tenths
    i32                        mana = -1;   // -1 = unknown, do not gate on it
    std::vector<std::pair<int, i32>> casts;  // spell -> times cast this session
};

struct PracticeChoice {
    i32                      manaNeeded = 0; // stocked spell blocked only by mana
    int                      spell = -1;   // what to cast, or -1
    int                      circle = 0;   // the circle it belongs to
    int                      shortFor = -1;// the spell `missing` belongs to
    std::vector<std::string> missing;      // reagents to buy
    const char*              reason = "no spellbook contents";
};

inline i32 QtyOfIn(const std::vector<market::Stock>& pack, const char* item) {
    for (const market::Stock& s : pack) {
        if (s.item == item) return s.qty;
    }
    return 0;
}

inline i32 CastsOf(const PracticeSight& see, int spell) {
    for (const std::pair<int, i32>& c : see.casts)
        if (c.first == spell) return c.second;
    return 0;
}

// THE GAIN WINDOW.
//
// `SKILLREQ=MAGERY <n>` is a HARD gate: below it the server refuses the cast,
// so a spell is only a candidate at all when this character's Magery has
// reached its circle. Above that, harder is better -- which is why the choice
// walks circles DOWNWARD and stops at the first one it can pay for.
//
// How far below the character's skill a spell stops teaching anything is
// UNKNOWN on this shard: `[SKILL 25] ADV_RATE=10.0,200.0,800.0`
// (runtime/scripts/skills/skill25_magery.scp) is a rate triple, not a window,
// sphere.ini carries no gain-window key, and the server source is not in this
// repo, so the exact curve cannot be read. What CAN be read is the spacing of
// the ladder itself: the eight circles sit exactly 10.0 skill apart. The
// window used here is therefore ONE CIRCLE of that measured spacing -- a
// character practises with spells whose requirement is within one circle below
// its own Magery -- and it is a floor on the candidate set, not a claim about
// the gain rate. When a book holds nothing in that window the choice falls
// back to the hardest thing the book does hold, because practising with an
// easy spell beats not practising.
inline i32 CircleSpacingTenths() {
    i32 lo = 0, hi = 0;
    for (const SpellDef& d : SpellTable()) {
        if (d.circle == 1 && (lo == 0 || d.minSkillTenths < lo)) lo = d.minSkillTenths;
        if (d.circle == 2 && (hi == 0 || d.minSkillTenths < hi)) hi = d.minSkillTenths;
    }
    return (hi > lo) ? (hi - lo) : 100;
}

// The best spell this character holds, may safely cast at itself, has the
// skill and mana for, has not been refused, and can pay for in reagents --
// preferring the highest circle in the gain window and rotating within it so
// one spell is not spammed. When no circle can be paid for, the choice reports
// the reagents of the spells it WOULD have picked (the highest castable
// circle's candidates), which is the shopping list the restock errand buys.
inline PracticeChoice ChoosePracticeSpell(const PracticeSight& see) {
    PracticeChoice out;
    if (SpellTable().empty()) {
        out.reason = "the spell table is not loaded";
        return out;
    }

    // Candidates: in the book, safe on oneself, skill and mana affordable,
    // not refused by the server this session.
    std::vector<const SpellDef*> cand;
    bool anyInBook = false;
    i32 manaNeeded = 0;
    for (int s : see.inBook) {
        const SpellDef* d = DefForSpell(s);
        if (!d) continue;
        anyInBook = true;
        if (!SafeToPractiseOnSelf(*d)) continue;
        if (see.magery < d->minSkillTenths) continue;
        bool refused = false;
        for (int r : see.uncastable) if (r == d->spell) refused = true;
        if (refused) continue;
        if (see.mana >= 0 && see.mana < d->mana) {
            bool stocked = true;
            for (const char* const* r = d->reagents; *r; ++r)
                if (QtyOfIn(see.pack, *r) <= 0) stocked = false;
            if (stocked && (manaNeeded == 0 || d->mana < manaNeeded))
                manaNeeded = d->mana;
            // If both inputs are missing, retain the reagent shopping list.
            // Low mana must not disguise a usable book as an empty one.
            if (!stocked) cand.push_back(d);
            continue;
        }
        cand.push_back(d);
    }
    if (cand.empty()) {
        if (manaNeeded > 0) {
            out.manaNeeded = manaNeeded;
            out.reason = "not enough mana";
            return out;
        }
        out.reason = anyInBook
                         ? "no spell in this book is safe to practise with at "
                           "this Magery and mana"
                         : "no spellbook contents";
        return out;
    }

    int topCircle = 0;
    for (const SpellDef* d : cand) if (d->circle > topCircle) topCircle = d->circle;
    const i32 window = CircleSpacingTenths();
    // The floor of the gain window, never above the top circle actually held.
    const i32 floorSkill = see.magery > window ? see.magery - window : 0;

    // Walk circles downward: highest first, gain window first, then anything.
    for (int pass = 0; pass < 2; ++pass) {
        for (int circle = topCircle; circle >= 1; --circle) {
            std::vector<const SpellDef*> ring;
            for (const SpellDef* d : cand) {
                if (d->circle != circle) continue;
                if (pass == 0 && d->minSkillTenths < floorSkill) continue;
                ring.push_back(d);
            }
            if (ring.empty()) continue;

            // ROTATION: the least-cast spell of this ring, ties by spell
            // number. A player practising does not cast the same word two
            // hundred times running.
            const SpellDef* pick = nullptr;
            i32 fewest = 0;
            std::vector<std::string> ringMissing;
            const SpellDef* cheapest = nullptr;
            std::vector<std::string> cheapestMissing;
            for (const SpellDef* d : ring) {
                std::vector<std::string> missing;
                for (const char* const* r = d->reagents; *r; ++r)
                    if (QtyOfIn(see.pack, *r) <= 0) missing.push_back(*r);
                if (missing.empty()) {
                    const i32 used = CastsOf(see, d->spell);
                    if (!pick || used < fewest) { pick = d; fewest = used; }
                    continue;
                }
                for (const std::string& m : missing) {
                    bool dup = false;
                    for (const std::string& h : ringMissing) if (h == m) dup = true;
                    if (!dup) ringMissing.push_back(m);
                }
                if (!cheapest || missing.size() < cheapestMissing.size()) {
                    cheapest = d;
                    cheapestMissing = missing;
                }
            }
            if (pick) {
                out.spell = pick->spell;
                out.circle = pick->circle;
                out.missing.clear();
                out.shortFor = -1;
                out.reason = "highest circle in the gain window that the book, "
                             "the mana and the pack all allow";
                return out;
            }
            // Nothing in this ring is paid for. Remember its shopping list --
            // the reagents of the spells the chooser WOULD pick -- and keep
            // looking at easier circles for something castable right now.
            if (out.missing.empty()) {
                out.missing = ringMissing;
                out.shortFor = cheapest ? cheapest->spell : -1;
                out.circle = circle;
            }
        }
        if (!out.missing.empty() || pass == 1) break;
    }

    if (manaNeeded > 0) {
        out.manaNeeded = manaNeeded;
        out.missing.clear();
        out.shortFor = -1;
        out.reason = "not enough mana";
        return out;
    }
    if (!out.missing.empty()) {
        out.reason = "out of reagents";
        return out;
    }
    out.reason = "every safe spell in this book was refused this session";
    return out;
}

// --- how much to buy ---------------------------------------------------------
//
// NOT A GLOBAL CONSTANT. "keep/bank/surplus counts derive from plans, wealth
// and prices per character, not global constants" (project owner). A cast burns
// one of each reagent, so the honest target is THE NUMBER OF CASTS THIS
// CHARACTER STILL EXPECTS TO MAKE THIS SESSION -- which is a RATE times the
// time left, and differs between a mage that practises half its session and a
// mage_blacksmith that practises for five minutes of it.
//
// The rate is OBSERVED: casts made so far over minutes elapsed. Before there is
// anything to observe a prior is used, and the prior is itself derived rather
// than picked -- the practice cadence is one cast per `castPeriodMs`, and a
// goal that has to share the day with earning, selling and eating holds the
// tick about a quarter of the time.
inline i32 ExpectedPracticeCasts(i32 castsSoFar, i64 elapsedMs, i64 remainingMs,
                                 i64 castPeriodMs) {
    if (remainingMs <= 0 || castPeriodMs <= 0) return 0;
    // Tenths of a cast per minute, so the rate survives integer division.
    i64 perMinuteTenths = 0;
    if (elapsedMs >= 60000 && castsSoFar > 0) {
        perMinuteTenths = (static_cast<i64>(castsSoFar) * 10 * 60000) / elapsedMs;
    } else {
        const i64 cadence = (60000 * 10) / castPeriodMs;   // casts/min, tenths
        perMinuteTenths = cadence / 4;                     // the quarter share
    }
    if (perMinuteTenths <= 0) perMinuteTenths = 1;
    const i64 minutesLeftTenths = (remainingMs * 10) / 60000;
    const i64 casts = (perMinuteTenths * minutesLeftTenths) / 100;
    return casts > 0 ? static_cast<i32>(casts) : 1;
}

struct ReagentPlan {
    i32         target = 0;   // per reagent, for the rest of this session
    i32         buy    = 0;   // what to ask the shopkeeper for
    const char* why    = "";
};

// `unitPrice` <= 0 means the character has never seen one quoted; the purse cap
// is then left to the vendor errand, which knows the real price at the moment
// it reads the shelf.
inline ReagentPlan PlanReagentBuy(i32 have, i32 expectedCasts, i32 unitPrice,
                                  i32 spendableGold, int kinds) {
    ReagentPlan p;
    p.target = expectedCasts > 0 ? expectedCasts : 0;
    p.buy = p.target > have ? p.target - have : 0;
    p.why = "one of each per cast, for the casts this session still has room for";
    if (p.buy > 0 && unitPrice > 0 && kinds > 0) {
        const i32 affordable = spendableGold / (unitPrice * kinds);
        if (affordable < p.buy) {
            p.buy = affordable > 0 ? affordable : 0;
            p.why = "capped by the purse at the price this character has seen";
        }
    }
    return p;
}

}  // namespace uo::spell
