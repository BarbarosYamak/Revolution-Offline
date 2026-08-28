#include "uo/life.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace uo::life {

namespace {

i32 TileDist(i32 ax, i32 ay, i32 bx, i32 by) {
    // Chebyshev -- UO's own notion of distance; a diagonal step is one tile.
    return std::max(std::abs(ax - bx), std::abs(ay - by));
}

// Two learned spots within this many tiles are the same spot. Without it a
// forest stand becomes forty near-identical records after one shift.
constexpr i32 kSameSpotTiles = 6;

template <typename T>
void CapOldestFirst(std::vector<T>& v, usize cap, i64 T::*stamp) {
    if (v.size() <= cap) return;
    std::sort(v.begin(), v.end(), [stamp](const T& a, const T& b) {
        return a.*stamp > b.*stamp;   // newest first
    });
    v.resize(cap);
}

}  // namespace

void Memory::NotePlace(const char* kind, const char* name, i32 x, i32 y, i8 z,
                       i64 nowMs) {
    if (!kind || !kind[0]) return;
    for (KnownPlace& p : places_) {
        if (p.kind == kind && TileDist(p.x, p.y, x, y) <= kSameSpotTiles) {
            p.lastVerifiedMs = nowMs;
            p.visits++;
            if (name && name[0]) p.name = name;
            return;
        }
    }
    KnownPlace p;
    p.kind = kind;
    p.name = name ? name : "";
    p.x = x; p.y = y; p.z = z;
    p.learnedMs = nowMs;
    p.lastVerifiedMs = nowMs;
    p.visits = 1;
    places_.push_back(std::move(p));
    CapOldestFirst(places_, kMaxPlaces, &KnownPlace::lastVerifiedMs);
}

void Memory::NoteResource(const char* resource, i32 x, i32 y, i8 z, bool success,
                          i64 nowMs) {
    if (!resource || !resource[0]) return;
    for (KnownResourceSource& r : resources_) {
        if (r.resource == resource && TileDist(r.x, r.y, x, y) <= kSameSpotTiles) {
            r.lastSeenMs = nowMs;
            if (success) { r.successes++; r.lastSuccessMs = nowMs; }
            else         { r.failures++; }
            return;
        }
    }
    KnownResourceSource r;
    r.resource = resource;
    r.x = x; r.y = y; r.z = z;
    r.lastSeenMs = nowMs;
    if (success) { r.successes = 1; r.lastSuccessMs = nowMs; }
    else         { r.failures = 1; }
    resources_.push_back(std::move(r));
    CapOldestFirst(resources_, kMaxResources, &KnownResourceSource::lastSeenMs);
}

void Memory::NoteResourceSeen(const char* resource, i32 x, i32 y, i8 z, i64 nowMs) {
    if (!resource || !resource[0]) return;
    for (KnownResourceSource& r : resources_) {
        if (r.resource == resource && TileDist(r.x, r.y, x, y) <= kSameSpotTiles) {
            r.lastSeenMs = nowMs;
            return;
        }
    }
    KnownResourceSource r;
    r.resource = resource;
    r.x = x; r.y = y; r.z = z;
    r.lastSeenMs = nowMs;
    resources_.push_back(std::move(r));
    CapOldestFirst(resources_, kMaxResources, &KnownResourceSource::lastSeenMs);
}

void Memory::HintResource(const char* resource, const char* label, i32 x, i32 y,
                          i8 z, i64 nowMs) {
    if (!resource || !resource[0]) return;
    for (const KnownResourceSource& r : resources_) {
        // A hint never overwrites anything, least of all a proven stand.
        if (r.resource == resource && TileDist(r.x, r.y, x, y) <= kSameSpotTiles) return;
    }
    KnownResourceSource r;
    r.resource = resource;
    r.label = label ? label : "";
    r.x = x; r.y = y; r.z = z;
    r.hinted = true;
    r.lastSeenMs = nowMs;
    resources_.push_back(std::move(r));
    CapOldestFirst(resources_, kMaxResources, &KnownResourceSource::lastSeenMs);
}

const KnownResourceSource* Memory::BestProvenResource(const char* resource,
                                                      i32 fromX, i32 fromY,
                                                      i64 nowMs) const {
    if (!resource) return nullptr;
    const KnownResourceSource* best = nullptr;
    double bestScore = -1e18;
    for (const KnownResourceSource& r : resources_) {
        if (r.resource != resource) continue;
        if (r.successes <= 0) continue;   // PROVEN means it actually yielded
        const double dist = static_cast<double>(TileDist(r.x, r.y, fromX, fromY));
        const double score = static_cast<double>(r.successes) * 20.0 -
                             static_cast<double>(r.failures) * 4.0 - dist * 0.05 -
                             DangerHeatAt(r.x, r.y, nowMs) * 40.0;
        if (score > bestScore) { bestScore = score; best = &r; }
    }
    return best;
}

const KnownResourceSource* Memory::BestHint(const char* resource, i32 fromX,
                                            i32 fromY, i64 nowMs) const {
    if (!resource) return nullptr;
    const KnownResourceSource* best = nullptr;
    double bestScore = -1e18;
    for (const KnownResourceSource& r : resources_) {
        if (r.resource != resource) continue;
        if (!r.hinted) continue;
        const double dist = static_cast<double>(TileDist(r.x, r.y, fromX, fromY));
        // Nearest first, but a lead that has already disappointed drops down
        // the list rather than being walked to again and again.
        const double score = -dist * 0.05 - static_cast<double>(r.failures) * 30.0 -
                             DangerHeatAt(r.x, r.y, nowMs) * 40.0;
        if (score > bestScore) { bestScore = score; best = &r; }
    }
    return best;
}

void Memory::NoteSupplier(const KnownSupplier& s) {
    for (KnownSupplier& k : suppliers_) {
        if (k.serial == s.serial && k.need == s.need) {
            k = s;
            return;
        }
    }
    suppliers_.push_back(s);
    CapOldestFirst(suppliers_, kMaxSuppliers, &KnownSupplier::lastVerifiedMs);
}

void Memory::NoteDanger(i32 x, i32 y, i32 radius, const char* threat, double heat,
                        i64 nowMs) {
    if (heat <= 0.0) return;
    for (DangerMemory& d : danger_) {
        if (TileDist(d.x, d.y, x, y) <= std::max(d.radius, radius)) {
            // Compound onto the DECAYED value, not the raw one -- otherwise a
            // spot that scared us an hour ago is treated as if it just did.
            const double halves =
                static_cast<double>(nowMs - d.atMs) / static_cast<double>(kDangerHalfLifeMs);
            const double current = d.heat * std::pow(0.5, halves);
            // CAPPED. Heat compounds on repeat trouble, which is right, but
            // an unbounded sum is not a memory -- it is a grudge. One live
            // session reached 499.89 at a single spot because a twenty-minute
            // fight added to it on every tick, and the resulting -60 x heat
            // penalty drove the character's own profession to a NEGATIVE
            // score. Four doublings is as afraid as it ever needs to be.
            d.heat = std::min(kMaxDangerHeat, current + heat);
            d.atMs = nowMs;
            d.radius = std::max(d.radius, radius);
            if (threat && threat[0]) d.threat = threat;
            return;
        }
    }
    DangerMemory d;
    d.x = x; d.y = y;
    d.radius = radius;
    d.threat = threat ? threat : "";
    d.heat = heat;
    d.atMs = nowMs;
    danger_.push_back(std::move(d));
    CapOldestFirst(danger_, kMaxDanger, &DangerMemory::atMs);
}

double Memory::DangerHeatAt(i32 x, i32 y, i64 nowMs) const {
    double total = 0.0;
    for (const DangerMemory& d : danger_) {
        if (TileDist(d.x, d.y, x, y) > d.radius) continue;
        if (nowMs < d.atMs) { total += d.heat; continue; }
        const double halves =
            static_cast<double>(nowMs - d.atMs) / static_cast<double>(kDangerHalfLifeMs);
        total += d.heat * std::pow(0.5, halves);
    }
    return total;
}

void Memory::ExpireDanger(i64 nowMs, double floorHeat) {
    danger_.erase(
        std::remove_if(danger_.begin(), danger_.end(),
                       [&](const DangerMemory& d) {
                           if (nowMs < d.atMs) return false;
                           const double halves =
                               static_cast<double>(nowMs - d.atMs) /
                               static_cast<double>(kDangerHalfLifeMs);
                           return d.heat * std::pow(0.5, halves) < floorHeat;
                       }),
        danger_.end());
}

void Memory::NoteEvent(const char* kind, const char* detail, const char* place,
                       i32 x, i32 y, i64 nowMs) {
    if (!kind || !kind[0]) return;
    LifeEvent e;
    e.kind = kind;
    e.detail = detail ? detail : "";
    e.place = place ? place : "";
    e.x = x; e.y = y;
    e.atMs = nowMs;
    events_.push_back(std::move(e));
    // A ring, not a log: the oldest events fall off the front so the file
    // stays bounded no matter how long a character lives.
    if (events_.size() > kMaxEvents) {
        events_.erase(events_.begin(),
                      events_.begin() +
                          static_cast<long>(events_.size() - kMaxEvents));
    }
}

const KnownPlace* Memory::BestPlace(const char* kind) const {
    if (!kind) return nullptr;
    const KnownPlace* best = nullptr;
    for (const KnownPlace& p : places_) {
        if (p.kind != kind) continue;
        if (!best || p.lastVerifiedMs > best->lastVerifiedMs) best = &p;
    }
    return best;
}

const KnownResourceSource* Memory::BestResource(const char* resource, i32 fromX,
                                                i32 fromY, i64 nowMs) const {
    if (!resource) return nullptr;
    const KnownResourceSource* best = nullptr;
    double bestScore = -1e18;
    for (const KnownResourceSource& r : resources_) {
        if (r.resource != resource) continue;
        // Nearest-and-most-productive, penalised by remembered danger. A
        // stand that keeps failing loses to a farther one that works.
        const double dist = static_cast<double>(TileDist(r.x, r.y, fromX, fromY));
        const double productivity =
            static_cast<double>(r.successes) - 2.0 * static_cast<double>(r.failures);
        const double score = productivity * 8.0 - dist * 0.05 -
                             DangerHeatAt(r.x, r.y, nowMs) * 40.0;
        if (score > bestScore) { bestScore = score; best = &r; }
    }
    return best;
}

const KnownSupplier* Memory::BestSupplier(const char* need) const {
    if (!need) return nullptr;
    const KnownSupplier* best = nullptr;
    for (const KnownSupplier& s : suppliers_) {
        if (s.need != need) continue;
        // A supplier the policy refuses is still a fact about the world, but
        // it is never returned as usable (supplier.h's rule).
        if (!s.policyAllows) continue;
        if (!best || s.lastVerifiedMs > best->lastVerifiedMs) best = &s;
    }
    return best;
}

bool Memory::HasEvent(const char* kind) const {
    if (!kind) return false;
    for (const LifeEvent& e : events_) {
        if (e.kind == kind) return true;
    }
    return false;
}

void Memory::Clear() {
    places_.clear();
    resources_.clear();
    suppliers_.clear();
    danger_.clear();
    events_.clear();
}

usize Memory::ApproximateBytes() const {
    usize n = 0;
    for (const KnownPlace& p : places_)          n += sizeof(p) + p.kind.size() + p.name.size();
    for (const KnownResourceSource& r : resources_) n += sizeof(r) + r.resource.size();
    for (const KnownSupplier& s : suppliers_)    n += sizeof(s) + s.need.size() + s.name.size() + s.sourceType.size();
    for (const DangerMemory& d : danger_)        n += sizeof(d) + d.threat.size();
    for (const LifeEvent& e : events_)           n += sizeof(e) + e.kind.size() + e.detail.size() + e.place.size();
    return n;
}

}  // namespace uo::life
