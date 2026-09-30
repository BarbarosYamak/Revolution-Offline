#pragma once
#include "uo/types.h"

#include <cmath>
#include <string>
#include <vector>

// HOUSES: save up, buy a deed from an architect, and place it outside town.
//
// What this tree is known to do (sphere.ini:331-353): AutoHouseKeys=1 (the
// key arrives on placement), MaxHousesAccount=1, MaxHousesPlayer=1. Deeds are
// sold by architects, which the atlas files under the Carpenter service
// (AtlasGenMain.cpp:593). The deed item is the classic 0x14F0.
//
// BLOCKED ON THE SERVER TODAY: the vendor snapshot in this repo
// (artifacts/tm_vend_pre_restore_2026-09-02.scp:135-157) has every house-deed
// SELL line commented out, so an architect may list none. The bot then simply
// learns "no deed on sale" and stops asking for a day -- it never conjures one.
//
// UNKNOWN: Revolution's house prices, sizes, placement zones, decay and
// lockdown rules (/ev_cesitleri, /ev_kurallari were never captured). So the
// wealth line below is a tunable, and placement success is read only from the
// server: a multi appearing at the spot.
namespace uo::housing {

inline constexpr u16 kDeedGraphic = 0x14F0;
// Total wealth (pack + bank) before a character starts shopping for a house,
// and what it always keeps back. DERIVED tuning, not a Revolution figure.
inline constexpr i32 kWealthToShop = 30000;
inline constexpr i32 kKeepAfterHouse = 3000;
inline constexpr int kMaxSitesPerSession = 12;

struct Wealth {
    i32  totalGold = 0;      // what the character believes it has, box included
    bool ownsHouse = false;  // it placed one (its own memory)
    bool deedInPack = false;
};

inline bool WantsHouse(const Wealth& w) {
    if (w.ownsHouse) return false;
    return w.deedInPack || w.totalGold >= kWealthToShop;
}

// What it may spend on the deed itself.
inline i32 DeedBudget(i32 totalGold) {
    return totalGold > kKeepAfterHouse ? totalGold - kKeepAfterHouse : 0;
}

struct Site { i32 x = 0, y = 0; };

// Where to try, in order: rings 40..100 tiles out from the home town, sixteen
// bearings each, starting at a bearing chosen from the identity so a fleet
// does not queue for the same field. Deterministic: the same character tries
// the same places in the same order every session.
inline std::vector<Site> Candidates(i32 homeX, i32 homeY, u32 identityHash) {
    std::vector<Site> out;
    const int start = static_cast<int>(identityHash % 16u);
    for (int ring = 40; ring <= 100; ring += 20) {
        for (int k = 0; k < 16; ++k) {
            const double a = (start + k) % 16 * (3.14159265358979 * 2.0 / 16.0);
            Site s;
            s.x = homeX + static_cast<i32>(std::lround(std::cos(a) * ring));
            s.y = homeY + static_cast<i32>(std::lround(std::sin(a) * ring));
            if (s.x > 8 && s.y > 8 && s.x < 5100 && s.y < 4080) out.push_back(s);
        }
    }
    return out;
}

enum class Step : u8 { Wait = 0, BuyDeed, Travel, UseDeed, Place, NextSite, Done, GiveUp };

inline const char* StepName(Step s) {
    static const char* k[] = {"wait", "buy_deed", "travel", "use_deed", "place", "next_site", "done", "give_up"};
    return k[static_cast<int>(s)];
}

struct Sight {
    bool ownsHouse = false;
    bool deedInPack = false;
    i32  deedBudget = 0;
    bool deedErrandFailed = false;   // architects asked; no deed within budget
    int  siteIndex = 0, siteCount = 0;
    i32  tilesToSite = 0;
    bool cursorActive = false;       // the server's 0x99 placement cursor is up
    bool placedHere = false;         // a multi appeared at this site after we tried
    int  triesHere = 0;              // placement attempts at this site
    bool busy = false;
};

struct Plan { Step step = Step::Wait; const char* reason = ""; };

inline Plan Decide(const Sight& s) {
    if (s.ownsHouse || s.placedHere) return {Step::Done, "the house stands"};
    if (s.busy) return {Step::Wait, "an action or trip is in flight"};
    if (!s.deedInPack) {
        if (s.deedErrandFailed) return {Step::GiveUp, "no house deed on sale within budget"};
        if (s.deedBudget <= 0) return {Step::GiveUp, "cannot afford a deed and keep a reserve"};
        return {Step::BuyDeed, "buy a deed from an architect"};
    }
    if (s.siteIndex >= s.siteCount || s.siteIndex >= kMaxSitesPerSession)
        return {Step::GiveUp, "no site accepted the house this session"};
    if (s.tilesToSite > 2) return {Step::Travel, "walk to the next site"};
    if (s.cursorActive) return {Step::Place, "answer the placement cursor"};
    if (s.triesHere >= 1) return {Step::NextSite, "refused here: try the next site"};
    return {Step::UseDeed, "use the deed"};
}

}  // namespace uo::housing
