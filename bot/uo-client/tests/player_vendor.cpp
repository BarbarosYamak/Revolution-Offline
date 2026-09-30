// Player vendor decisions (uo/player_vendor.h), pure.
#include "uo/player_vendor.h"

#include <cstdio>

using namespace uo;
using namespace uo::vendors;

static int g_checks = 0, g_failures = 0;
static void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_failures; std::printf("  FAIL: %s\n", what); }
}

int main() {
    Check(IsPlayerVendorTitle("Kemal the vendor") && IsPlayerVendorTitle("Ayse's Vendor"), "a player vendor's title");
    Check(!IsPlayerVendorTitle("Aldo the provisioner") && !IsPlayerVendorTitle("Vendorina the mage"),
          "NPC shopkeepers and near-misses are not");
    PriceInputs in; in.npcSellsFor = 100; in.npcBuysFor = 20;
    Check(AskingPrice(in) == 90, "undercut an NPC shop by 10%");
    in = {}; in.playerPrice = 55;
    Check(AskingPrice(in) == 55, "no NPC sells it: ask what players pay");
    in = {}; in.npcBuysFor = 30;
    Check(AskingPrice(in) >= 60, "only an NPC buy price known: ask at least twice it");
    in = {};
    Check(AskingPrice(in) == 0, "no idea of the value: do not list");
    in = {}; in.npcSellsFor = 10; in.npcBuysFor = 9;
    Check(NetAfterTax(AskingPrice(in)) > 9, "never list below what an NPC would pay, after the 10% tax");
    in = {}; in.craftedFloor = 40;
    Check(NetAfterTax(AskingPrice(in)) > 40, "never below the inputs, after tax");
    Check(NetAfterTax(100) == 90, "the vendor keeps ~10% (Revolution around 2010)");
    Check(ShouldList({5, false, 90}) && !ShouldList({5, true, 90}) && !ShouldList({5, false, 0}) &&
          !ShouldList({0, false, 90}), "list surplus only, with a price");
    Check(WorthBuying(80, 100, 120, 500), "cheaper than the NPC shop and worth it: buy");
    Check(!WorthBuying(120, 100, 200, 500), "dearer than the NPC shop: no");
    Check(!WorthBuying(80, -1, 60, 500), "more than it is worth to me: no");
    Check(!WorthBuying(80, -1, 200, 50), "over budget: no");
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
