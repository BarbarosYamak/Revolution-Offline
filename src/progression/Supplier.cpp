#include "uo/supplier.h"

#include <algorithm>
#include <cstring>

namespace uo::supply {

const char* NeedKindName(NeedKind k) {
    switch (k) {
        case NeedKind::Item:     return "ITEM";
        case NeedKind::Service:  return "SERVICE";
        case NeedKind::Resource: return "RESOURCE";
        case NeedKind::Count:    break;
    }
    return "?";
}

const char* SupplierKindName(SupplierKind k) {
    switch (k) {
        case SupplierKind::NpcVendor:       return "NPC_VENDOR";
        case SupplierKind::PlayerVendor:    return "PLAYER_VENDOR";
        case SupplierKind::PlayerCharacter: return "PLAYER_CHARACTER";
        case SupplierKind::WorldResource:   return "WORLD_RESOURCE";
        case SupplierKind::ServiceNpc:      return "SERVICE_NPC";
        case SupplierKind::Count:           break;
    }
    return "?";
}

const char* FreshnessName(Freshness f) {
    switch (f) {
        case Freshness::VerifiedCurrent: return "VERIFIED_CURRENT";
        case Freshness::Recent:          return "RECENT";
        case Freshness::Stale:           return "STALE";
        case Freshness::Invalid:         return "INVALID";
        case Freshness::Count:           break;
    }
    return "?";
}

namespace {

i32 Chebyshev(i32 ax, i32 ay, i32 bx, i32 by) {
    const i32 dx = ax > bx ? ax - bx : bx - ax;
    const i32 dy = ay > by ? ay - by : by - ay;
    return dx > dy ? dx : dy;
}

bool SameWhat(const std::string& a, const char* b) {
    return b && a == b;
}

}  // namespace

Freshness Registry::FreshnessOf(const Supplier& s, i64 nowMs) {
    if (s.invalidated) return Freshness::Invalid;
    const i64 age = nowMs - s.lastVerifiedMs;
    if (age <= kVerifiedMs) return Freshness::VerifiedCurrent;
    if (age <= kRecentMs)   return Freshness::Recent;
    return Freshness::Stale;
}

void Registry::RecordVendorStock(u32 serial, const char* name, i32 x, i32 y, i8 z,
                                 const char* item, i32 quantity,
                                 i32 pricePerUnit, i64 nowMs) {
    if (!serial || !item || !*item) return;

    // The policy ruling is stored WITH the observation. A supplier the policy
    // refuses is still a fact about the world worth remembering -- it just may
    // never be returned as usable. Recording the refusal here means the reason
    // survives to the caller instead of being recomputed and possibly diverging.
    const auto ruling = econ::CanBuyFromNPC(item);

    for (auto& s : suppliers_) {
        if (s.serial == serial && SameWhat(s.what, item)) {
            s.x = x; s.y = y; s.z = z;
            if (name && *name) s.name = name;
            s.observedQuantity = quantity;
            s.observedPricePerUnit = pricePerUnit;
            s.lastVerifiedMs = nowMs;
            // A fresh sighting clears a past absence: vendors restock, and one
            // empty cycle is not a permanent verdict.
            s.invalidated = false;
            s.failures = 0;
            s.policyClass = ruling.klass;
            s.policyAllows = ruling.allowed;
            return;
        }
    }

    Supplier s;
    s.kind = SupplierKind::NpcVendor;
    s.serial = serial;
    s.name = name ? name : "";
    s.x = x; s.y = y; s.z = z;
    s.what = item;
    s.observedQuantity = quantity;
    s.observedPricePerUnit = pricePerUnit;
    s.observedAtMs = nowMs;
    s.lastVerifiedMs = nowMs;
    s.policyClass = ruling.klass;
    s.policyAllows = ruling.allowed;
    suppliers_.push_back(std::move(s));
}

void Registry::RecordResource(const char* resource, i32 x, i32 y, i8 z, i64 nowMs) {
    if (!resource || !*resource) return;
    for (auto& s : suppliers_) {
        if (s.kind == SupplierKind::WorldResource && SameWhat(s.what, resource) &&
            Chebyshev(s.x, s.y, x, y) <= 8) {
            s.lastVerifiedMs = nowMs;
            s.invalidated = false;
            s.failures = 0;
            return;
        }
    }
    Supplier s;
    s.kind = SupplierKind::WorldResource;
    s.what = resource;
    s.x = x; s.y = y; s.z = z;
    s.observedAtMs = nowMs;
    s.lastVerifiedMs = nowMs;
    // A field is not a vendor: no NPC purchase is involved, so the policy has
    // nothing to refuse. Gathering is always legitimate.
    s.policyAllows = true;
    s.policyClass = econ::VendorClass::WorldGathered;
    suppliers_.push_back(std::move(s));
}

void Registry::RecordAbsent(u32 serial, const char* what, i64 nowMs) {
    for (auto& s : suppliers_) {
        if (s.serial != serial || !SameWhat(s.what, what)) continue;
        ++s.failures;
        s.lastVerifiedMs = nowMs;   // we DID verify -- we verified it is absent
        // Demote, do not delete. Sphere restocks on its own timer, and M3.7
        // watched a Britain blacksmith carry nothing but ingots and unwearable
        // tongs while tm_vend.scp lists a hammer. One empty cycle is not proof
        // the vendor never carries it; three is enough to stop walking there.
        if (s.failures >= 3) s.invalidated = true;
        return;
    }
}

std::vector<Candidate> Registry::Resolve(const Need& need, i32 fromX, i32 fromY,
                                         i64 nowMs) const {
    std::vector<Candidate> out;
    for (const auto& s : suppliers_) {
        if (!SameWhat(s.what, need.what.c_str())) continue;

        Candidate c;
        c.supplier = s;
        c.freshness = FreshnessOf(s, nowMs);
        c.travelTiles = Chebyshev(fromX, fromY, s.x, s.y);

        if (c.freshness == Freshness::Invalid) {
            c.usable = false;
            c.why = "invalidated: arrived and it was not stocked";
        } else if (!s.policyAllows) {
            // Kept separate from ignorance on purpose. "We refuse to buy this
            // from an NPC" and "we do not know where to get this" are different
            // answers, and collapsing them would hide an authenticity decision
            // behind a logistics one.
            c.usable = false;
            c.why = std::string("policy refuses NPC purchase (") +
                    econ::VendorClassName(s.policyClass) + ")";
        } else if (s.kind != SupplierKind::WorldResource &&
                   s.observedQuantity < need.quantity) {
            // The quantity gate is about a VENDOR'S SHOP LIST. A world resource
            // has no stock count -- a cotton field is not "4 cotton in stock",
            // it is a place where cotton grows and regrows. Applying the vendor
            // rule to it rejected every field for having quantity 0, which is
            // how this check first failed its own test.
            c.usable = false;
            c.why = "observed stock is below the needed quantity";
        } else {
            c.usable = true;
        }
        out.push_back(std::move(c));
    }

    // Freshest first, then nearest. A verified sighting across town beats a
    // stale one next door: the walk is cheap and arriving to nothing is not.
    std::sort(out.begin(), out.end(), [](const Candidate& a, const Candidate& b) {
        if (a.usable != b.usable) return a.usable;
        if (a.freshness != b.freshness)
            return static_cast<int>(a.freshness) < static_cast<int>(b.freshness);
        return a.travelTiles < b.travelTiles;
    });
    return out;
}

Candidate Registry::Best(const Need& need, i32 fromX, i32 fromY, i64 nowMs) const {
    const auto all = Resolve(need, fromX, fromY, nowMs);
    if (all.empty()) {
        Candidate none;
        none.usable = false;
        // The honest answer when nothing has ever been observed. NOT "go to the
        // profession and hope" -- that is precisely the guess that sent three
        // milestones' worth of bots to guild halls.
        none.why = "no verified supplier has ever been observed for this need";
        return none;
    }
    return all.front();
}

}  // namespace uo::supply
