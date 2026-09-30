// Treasure hunting's pure step machine (uo/treasure.h).
#include "uo/treasure.h"

#include <cstdio>

using namespace uo;
using namespace uo::treasure;

static int g_checks = 0, g_failures = 0;
static void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_failures; std::printf("  FAIL: %s\n", what); }
}

static Sight Ready() {
    Sight s;
    s.haveMap = true; s.cartographyTenths = s.lockpickingTenths = 1000;
    s.haveDigTool = s.haveLockpick = true;
    return s;
}

int main() {
    i32 x = 0, y = 0;
    Check(PinToWorld(900, 900, 1100, 1100, 100, 100, 50, 50, &x, &y) && x == 1000 && y == 1000,
          "a pin's pixel maps onto the world rectangle");
    Check(PinToWorld(0, 0, 5120, 4096, 400, 320, 400, 320, &x, &y) && x == 5120 && y == 4096, "the far corner");
    Check(!PinToWorld(0, 0, 100, 100, 0, 100, 1, 1, &x, &y) && !PinToWorld(0, 0, 100, 100, 50, 50, 60, 1, &x, &y),
          "degenerate maps and off-image pins are refused");
    Check(RequiredTenths(2) == 400 && RequiredTenths(5) == 1000 && RequiredTenths(0) == 0,
          "Revolution levels 2-5 need 40/60/80/100; unknown lets the server decide");

    Sight s = Ready();
    Check(Decide(s).step == Step::Decode, "an unread map is decoded first");
    s.level = 4; s.cartographyTenths = 700;
    Check(Decide(s).step == Step::GiveUp, "Cartography below the level's requirement: give up, do not spam");
    s = Ready(); s.decodeTries = kMaxDecodeTries;
    Check(Decide(s).step == Step::GiveUp, "a map that never decodes is abandoned");
    s = Ready(); s.pointKnown = true; s.tilesToSpot = 300;
    Check(Decide(s).step == Step::Travel, "decoded: go to the spot");
    s.tilesToSpot = 1;
    Check(Decide(s).step == Step::Dig, "on the spot: dig");
    s.haveDigTool = false;
    Check(Decide(s).step == Step::GiveUp, "no shovel or pickaxe: give up");
    s.haveDigTool = true; s.hpFrac = 0.5;
    Check(Decide(s).step == Step::GiveUp, "too hurt to face guardians");
    s = Ready(); s.pointKnown = true; s.digTries = kMaxDigTries;
    Check(Decide(s).step == Step::GiveUp, "dug six times, nothing: give up");
    s = Ready(); s.pointKnown = true; s.chest = 7;
    Check(Decide(s).step == Step::Open, "a chest came up: try the lid");
    s.openTries = 1;
    Check(Decide(s).step == Step::Unlock, "it did not open: pick the lock");
    s.pickTries = 1;
    Check(Decide(s).step == Step::Open, "then the lid again (they alternate)");
    s.haveLockpick = false; s.openTries = 2;
    Check(Decide(s).step == Step::GiveUp, "locked with no lockpick: give up");
    s = Ready(); s.chest = 7; s.pointKnown = true; s.hostilesNear = true;
    Check(Decide(s).step == Step::Wait, "guardians: the fight owns the tick");
    s.hostilesNear = false; s.chestOpen = true; s.chestItemsLeft = 3;
    Check(Decide(s).step == Step::Loot, "open: loot");
    s.weightFrac = 0.95;
    Check(Decide(s).step == Step::Done, "pack full: leave the rest");
    s.weightFrac = 0.2; s.chestItemsLeft = 0;
    Check(Decide(s).step == Step::Done, "emptied: done");
    s = Ready(); s.busy = true;
    Check(Decide(s).step == Step::Wait, "never act over an action in flight");
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
