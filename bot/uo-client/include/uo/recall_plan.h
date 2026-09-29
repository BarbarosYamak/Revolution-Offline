#pragma once
#include "uo/types.h"

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

}  // namespace uo::recall
