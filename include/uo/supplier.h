#pragma once

#include "uo/types.h"
#include "uo/vendor_policy.h"

#include <string>
#include <vector>

namespace uo::supply {

// ---------------------------------------------------------------------------
// M3.9 Phases 3-4 -- resolve NEEDS to CONCRETE suppliers, and let that
// knowledge go stale.
//
// THE FAILURE THIS REPLACES, which has now happened three times:
//
//   M3.6  need a blank rune -> travel to the Britain "mage" -> a guildmaster
//                              who keeps no shop
//   M3.7  need reagents     -> same place, same result
//   M3.8  need tinker tools -> travel_service tinker -> arrives at
//                              "Justine, the engineer guildmistress",
//                              Vendors_spawns_felucca.scp:28 places ONLY
//                              tinkerguildmaster there. Britain has a tinker
//                              GUILD and no tinker.
//
// Every one of those journeys REPORTED SUCCESS. The bot went where it meant to
// go; it just could not buy anything when it arrived.
//
// THE CATEGORY ERROR: "a place tagged with profession P" is not "an entity that
// will perform transaction T". A profession is a label on a map. A supplier is
// a serial, at a position, whose shop list was seen to contain the thing.
//
// So a Supplier here is only ever created from a VERIFIED OBSERVATION -- we
// opened its shop and read the item in the list. It is never created from an
// atlas tag, a spawn table, or a profession name. That single rule is what
// makes the guildmistress case impossible to repeat: she was never observed
// selling anything, so she can never be returned as a supplier of anything.
// ---------------------------------------------------------------------------

// What a bot wants. Deliberately not "which profession" -- that is the mistake.
enum class NeedKind : u8 {
    Item,       // a specific itemdef, e.g. i_hammer_smith
    Service,    // banking, healing, stabling, training
    Resource,   // a world resource: an ore vein, a tree, a cotton field
    Count,
};

const char* NeedKindName(NeedKind k);

struct Need {
    NeedKind    kind = NeedKind::Item;
    std::string what;          // itemdef defname, service name, or resource name
    i32         quantity = 1;
};

enum class SupplierKind : u8 {
    NpcVendor,
    PlayerVendor,
    PlayerCharacter,   // a known crafter who can make it
    WorldResource,     // a field, vein or grove
    ServiceNpc,        // banker, healer, stablemaster
    Count,
};

const char* SupplierKindName(SupplierKind k);

// How much the record can still be trusted.
//
// Freshness is about the OBSERVATION, not the entity. A vendor still standing
// there is not evidence that it still stocks what it stocked an hour ago:
// Sphere restocks on its own timer, and M3.7 watched a Britain blacksmith carry
// nothing but ingots and unwearable tongs while `tm_vend.scp` lists a hammer.
// Template presence is not stock presence, and past stock is not present stock.
enum class Freshness : u8 {
    VerifiedCurrent,  // seen in the shop list within kVerifiedMs
    Recent,           // seen recently enough to be worth the walk
    Stale,            // old enough that it should be re-verified on arrival
    Invalid,          // we went, and it was not there
    Count,
};

const char* FreshnessName(Freshness f);

// A shop list read minutes ago is a strong claim; an hour later it is a lead.
inline constexpr i64 kVerifiedMs = 5  * 60 * 1000;   // 5 minutes
inline constexpr i64 kRecentMs   = 45 * 60 * 1000;   // 45 minutes

struct Supplier {
    SupplierKind kind = SupplierKind::NpcVendor;
    // CONCRETE. A supplier without a serial or a position is a rumour.
    u32  serial = 0;
    i32  x = 0;
    i32  y = 0;
    i8   z = 0;
    std::string name;          // as the server reported it
    std::string what;          // the need this record answers
    // What we actually saw, not what a template promised.
    i32  observedQuantity = 0;
    i32  observedPricePerUnit = 0;
    i64  observedAtMs = 0;
    i64  lastVerifiedMs = 0;
    // How many times we arrived and it was NOT there. A supplier that has
    // failed is demoted before it is deleted, because one empty restock cycle
    // is not proof the vendor never carries it.
    i32  failures = 0;
    bool invalidated = false;
    // Whether an autonomous Revolution bot may use this at all. A supplier the
    // policy refuses is still WORTH RECORDING -- it is a fact about the world --
    // but must never be returned as usable.
    econ::VendorClass policyClass = econ::VendorClass::Unknown;
    bool policyAllows = false;
};

// A ranked answer. `usable` separates "we know where it is" from "a bot may
// legitimately go and buy it", so a refusal is never mistaken for ignorance.
struct Candidate {
    Supplier  supplier;
    Freshness freshness = Freshness::Stale;
    bool      usable = false;
    std::string why;           // why not, when unusable
    i32       travelTiles = 0;
};

// After M3.7's "one action in flight" lesson: the registry is a plain value
// type with no I/O, so it can be unit-tested without a server.
class Registry {
public:
    // Record a VERIFIED observation: we opened this vendor's list and saw the
    // item in it. This is the only way a supplier enters the registry.
    void RecordVendorStock(u32 serial, const char* name, i32 x, i32 y, i8 z,
                           const char* item, i32 quantity, i32 pricePerUnit,
                           i64 nowMs);

    // Record a world resource we actually stood in (a cotton field, an ore
    // vein). Same rule: observed, not assumed from the atlas.
    void RecordResource(const char* resource, i32 x, i32 y, i8 z, i64 nowMs);

    // We went, and it was not there. Demotes rather than deletes.
    void RecordAbsent(u32 serial, const char* what, i64 nowMs);

    // Best candidates for a need, nearest-and-freshest first, unusable ones
    // included WITH a reason.
    std::vector<Candidate> Resolve(const Need& need,
                                   i32 fromX, i32 fromY, i64 nowMs) const;

    // The single best usable candidate, or nullopt-ish (usable == false).
    Candidate Best(const Need& need, i32 fromX, i32 fromY, i64 nowMs) const;

    usize Size() const { return suppliers_.size(); }
    void  Clear() { suppliers_.clear(); }

    static Freshness FreshnessOf(const Supplier& s, i64 nowMs);

private:
    std::vector<Supplier> suppliers_;
};

}  // namespace uo::supply
