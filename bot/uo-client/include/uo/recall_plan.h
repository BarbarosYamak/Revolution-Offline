#pragma once
#include "uo/types.h"

#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

// WHICH RUNEBOOK PAGE, IF ANY, SHORTENS THIS TRIP.
//
// The travel layer could already recall from a runebook (M3.9) but matched a
// page only by NAME against the trip's label, and the runner labels its trips
// "supplier", "forge", "bank" -- never "Britain". So a mage with a full book
// walked 1,900 tiles past its own rune. A page also carries the point it was
// marked at (texts[6+3N] of the book's gump, e.g. "1490,1555,30"), and that
// is what a player actually uses: "which of my runes lands nearest where I
// am going?".
//
// Pure: the client reads the gump and the backpack, this decides.
namespace uo::recall {

struct Page {
    int         page = 0;       // 1..8
    std::string name;
    i32         x = 0, y = 0;
    bool        filled = false; // marked, with a readable point
};

// First two integers in the text: "1490,1555,30" / "(1490, 1555)" -> 1490,1555.
inline bool ParsePoint(const std::string& text, i32* x, i32* y) {
    i32 v[2] = {0, 0};
    int got = 0;
    const char* p = text.c_str();
    while (*p && got < 2) {
        if (*p >= '0' && *p <= '9') {
            char* end = nullptr;
            v[got++] = static_cast<i32>(std::strtol(p, &end, 10));
            p = end;
        } else {
            ++p;
        }
    }
    if (got < 2 || v[0] <= 0 || v[1] <= 0 || v[0] > 7168 || v[1] > 4096) return false;
    *x = v[0]; *y = v[1];
    return true;
}

inline i32 Tiles(i32 ax, i32 ay, i32 bx, i32 by) {
    const i32 dx = std::abs(ax - bx), dy = std::abs(ay - by);
    return dx > dy ? dx : dy;
}

// Recall only when it clearly pays: the landing point must be close to the
// goal and the leftover walk much shorter than walking from here.
inline constexpr i32 kMaxLandingTiles = 120;   // a rune "in that town"
inline constexpr i32 kMinSavingTiles = 150;    // shorter trips just walk

struct Choice {
    int page = 0;           // 0 = walk (or gate)
    i32 walkAfter = 0;      // tiles from the landing point to the goal
    i32 walkNow = 0;        // tiles from here to the goal
};

inline Choice BestPage(const std::vector<Page>& pages, i32 fromX, i32 fromY, i32 toX, i32 toY) {
    Choice c;
    c.walkNow = Tiles(fromX, fromY, toX, toY);
    for (const Page& p : pages) {
        if (!p.filled) continue;
        const i32 after = Tiles(p.x, p.y, toX, toY);
        if (after > kMaxLandingTiles || c.walkNow - after < kMinSavingTiles) continue;
        if (!c.page || after < c.walkAfter) { c.page = p.page; c.walkAfter = after; }
    }
    return c;
}

// Can this character pay for an uncharged Recall? One of each reagent per
// cast times the era's count (uo/era.h RecallReagentsEach).
inline bool HasRecallReagents(u32 blackPearl, u32 bloodMoss, u32 mandrake, i32 each) {
    const u32 need = each > 0 ? static_cast<u32>(each) : 1u;
    return blackPearl >= need && bloodMoss >= need && mandrake >= need;
}

// Should the character open its book to learn what is in it? Once per
// session, when nothing else is happening -- a player glances at their book
// at the bank, not mid-fight.
inline bool ShouldReadBook(bool haveBook, bool alreadyRead, bool safe, bool busy, i64 nowMs, i64 lastTryMs) {
    return haveBook && !alreadyRead && safe && !busy && (lastTryMs == 0 || nowMs - lastTryMs >= 120000);
}

// ---- marking our own runes ---------------------------------------------------------
// A rune (i_rune_marker 0x1F14) is created blank; Mark renames it to the region
// it was marked in (REVOLUTION_GAMEPLAY_TRUTH.md:280, :487-501: NAME=Britain).
// So the server's label is the evidence: the stock name means blank.
inline constexpr u16 kRuneGraphic = 0x1F14;
inline constexpr i32 kMarkSkillTenths = 600;   // Mark: Magery 60.0 (data/revolution_spells.tsv)
inline constexpr i32 kMarkMana = 20;

inline bool LooksBlankRune(const std::string& label) {
    std::string t;
    for (unsigned char c : label) t.push_back(static_cast<char>(std::tolower(c)));
    return t == "recall rune" || t == "a recall rune" || t == "rune" || t == "a rune" ||
           t.find("blank") != std::string::npos;
}

struct MarkSight {
    bool haveBlankRune = false;
    i32  mageryTenths = 0, mana = 0;
    bool reagents = false;          // one each of black pearl, blood moss, mandrake
    bool atHome = false;            // standing in the home region
    bool safe = false, busy = false;
    bool ownRuneNearHome = false;   // a rune we marked already lands near home
};

inline bool ShouldMark(const MarkSight& s) {
    return s.haveBlankRune && s.mageryTenths >= kMarkSkillTenths && s.mana >= kMarkMana &&
           s.reagents && s.atHome && s.safe && !s.busy && !s.ownRuneNearHome;
}

}  // namespace uo::recall
