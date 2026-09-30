#pragma once
#include "uo/types.h"

#include <algorithm>
#include <cctype>
#include <string>

// PLAYER VENDORS: what an owner asks, what it lists, what a buyer pays, and
// how a passer-by recognises one.
//
// Revolution had player vendors in houses and a searchable cooperative over
// them (REVOLUTION_RULESET_PROFILE.md:100, /tezgahtarlar_kooperatifi), with a
// trade/vendor tax of about 10% around 2010 (:101; 5% is the 2016 figure).
// Pricing an item is what lists it (TNS_DONOR_AUDIT.md:182-213).
//
// BLOCKED ON THE SERVER TODAY: the player-vendor system was not ported to this
// runtime and no NPC sells a vendor deed (tm_vend snapshot :42-48, :1256-1257,
// :1300). So owners cannot exist yet. Buyers can still notice a vendor someone
// placed and remember it -- market knowledge learned by walking past, never
// global data.
//
// No price here is a Revolution fact. Every number comes from what THIS
// character has seen: an NPC shop's price, an NPC's buy price, a price it
// heard or paid a player (market::PriceBook).
namespace uo::vendors {

inline constexpr i32 kTaxPct = 10;       // ~10% around 2010

// A player vendor's title as the paperdoll shows it. Sphere's stock player
// vendor reads "<name> the vendor" / "<owner>'s vendor"; NPC shopkeepers are
// titled by their trade ("the provisioner") and never match.
inline bool IsPlayerVendorTitle(const std::string& title) {
    std::string t;
    for (unsigned char c : title) t.push_back(static_cast<char>(std::tolower(c)));
    return t.find(" the vendor") != std::string::npos || t.find("'s vendor") != std::string::npos ||
           t.find("tezgahtar") != std::string::npos;
}

struct PriceInputs {
    i32 npcSellsFor = -1;      // what an NPC shop charges for it; -1 = never seen
    i32 npcBuysFor = -1;       // what an NPC pays for it; -1 = none seen / none buys
    i32 playerPrice = -1;      // what players were seen to pay or quote
    i32 craftedFloor = 0;      // the owner's input cost, if it made the thing
};

// What the owner still gets after the vendor's tax.
inline i32 NetAfterTax(i32 price) { return price - price * kTaxPct / 100; }

// The asking price, or 0 = "no idea what this is worth: do not list it".
// Undercut an NPC shop a little; otherwise ask what players pay; otherwise
// twice what an NPC would give. Never so low that selling to an NPC, or the
// inputs, would have paid more once the tax is taken.
inline i32 AskingPrice(const PriceInputs& in) {
    i32 ask = 0;
    if (in.npcSellsFor > 0) ask = std::max(1, in.npcSellsFor * 9 / 10);
    else if (in.playerPrice > 0) ask = in.playerPrice;
    else if (in.npcBuysFor > 0) ask = in.npcBuysFor * 2;
    else if (in.craftedFloor > 0) ask = in.craftedFloor * 3 / 2;
    if (ask <= 0) return 0;
    const i32 floorNet = std::max(in.npcBuysFor, in.craftedFloor);
    while (floorNet > 0 && NetAfterTax(ask) <= floorNet) ask += std::max(1, ask / 20);
    return ask;
}

struct StockCandidate {
    i32  qty = 0;
    bool neededByOwner = false;   // a tool, a reagent it casts with, food it eats
    i32  askingPrice = 0;         // AskingPrice(...)
};

inline bool ShouldList(const StockCandidate& c) {
    return c.qty > 0 && !c.neededByOwner && c.askingPrice > 0;
}

// A buyer's side: buy from a player vendor only if it beats the NPC shop (or
// no NPC sells it), it is within what the thing is worth to this character,
// and it fits the budget.
inline bool WorthBuying(i32 price, i32 npcSellsFor, i32 worthToMe, i32 budget) {
    if (price <= 0 || price > budget || price > worthToMe) return false;
    return npcSellsFor <= 0 || price <= npcSellsFor;
}

}  // namespace uo::vendors
