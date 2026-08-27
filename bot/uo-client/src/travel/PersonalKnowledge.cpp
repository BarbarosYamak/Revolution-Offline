#include "travel/PersonalKnowledge.h"

#include <cstring>

namespace uo::travel {

namespace {

i32 Chebyshev(i32 ax, i32 ay, i32 bx, i32 by) {
    const i32 dx = ax > bx ? ax - bx : bx - ax;
    const i32 dy = ay > by ? ay - by : by - ay;
    return dx > dy ? dx : dy;
}

// Sightings older than this are not trusted to be where they were. An NPC
// wanders inside its spawner's home range, so a few minutes is generous.
constexpr usize kMaxSightings = 64;

} // namespace

void PersonalKnowledge::NoteVisit(const char* placeId, i64 nowMs) {
    if (!placeId || !*placeId) return;
    for (VisitRecord& v : visits_) {
        if (v.placeId == placeId) {
            v.lastVisitMs = nowMs;
            ++v.visits;
            return;
        }
    }
    VisitRecord v;
    v.placeId = placeId;
    v.lastVisitMs = nowMs;
    v.visits = 1;
    visits_.push_back(std::move(v));
}

bool PersonalKnowledge::HasVisited(const char* placeId) const {
    if (!placeId) return false;
    for (const VisitRecord& v : visits_)
        if (v.placeId == placeId) return true;
    return false;
}

void PersonalKnowledge::NoteService(wm::Service s, u32 serial,
                                    const char* title, i32 x, i32 y, i8 z,
                                    i64 nowMs) {
    if (s == wm::Service::None || !serial) return;
    for (ServiceSighting& v : sightings_) {
        if (v.serial == serial) {
            v.service = s;
            v.x = x; v.y = y; v.z = z;
            v.seenMs = nowMs;
            if (title && *title) v.title = title;
            return;
        }
    }
    ServiceSighting v;
    v.service = s;
    v.serial = serial;
    v.title = title ? title : "";
    v.x = x; v.y = y; v.z = z;
    v.seenMs = nowMs;
    sightings_.push_back(std::move(v));

    // Bounded: this is a working memory of who is nearby, not a census.
    if (sightings_.size() > kMaxSightings) {
        usize oldest = 0;
        for (usize i = 1; i < sightings_.size(); ++i)
            if (sightings_[i].seenMs < sightings_[oldest].seenMs) oldest = i;
        sightings_.erase(sightings_.begin() + static_cast<long long>(oldest));
    }
}

void PersonalKnowledge::ForgetService(u32 serial) {
    for (usize i = 0; i < sightings_.size(); ++i) {
        if (sightings_[i].serial == serial) {
            sightings_.erase(sightings_.begin() + static_cast<long long>(i));
            return;
        }
    }
}

const ServiceSighting* PersonalKnowledge::RecentService(wm::Service s,
                                                        i64 nowMs,
                                                        i64 maxAgeMs) const {
    const ServiceSighting* best = nullptr;
    for (const ServiceSighting& v : sightings_) {
        if (v.service != s) continue;
        if (maxAgeMs > 0 && nowMs - v.seenMs > maxAgeMs) continue;
        if (!best || v.seenMs > best->seenMs) best = &v;
    }
    return best;
}

void PersonalKnowledge::NoteRune(const KnownRune& r) {
    if (!r.serial) return;
    for (KnownRune& k : runes_) {
        if (k.serial == r.serial) { k = r; return; }
    }
    runes_.push_back(r);
}

void PersonalKnowledge::ForgetRune(u32 serial) {
    for (usize i = 0; i < runes_.size(); ++i) {
        if (runes_[i].serial == serial) {
            runes_.erase(runes_.begin() + static_cast<long long>(i));
            return;
        }
    }
}

const KnownRune* PersonalKnowledge::BestRuneFor(i32 x, i32 y,
                                                i32 maxDist) const {
    const KnownRune* best = nullptr;
    i32 bestD = 0;
    for (const KnownRune& k : runes_) {
        if (!k.marked || !k.destinationKnown) continue;
        const i32 d = Chebyshev(x, y, k.x, k.y);
        if (maxDist > 0 && d > maxDist) continue;
        if (!best || d < bestD) { best = &k; bestD = d; }
    }
    return best;
}

bool PersonalKnowledge::OwnsMarkedRune() const {
    for (const KnownRune& k : runes_)
        if (k.marked) return true;
    return false;
}

void PersonalKnowledge::SetHome(i32 x, i32 y, i8 z, const char* placeId) {
    homeSet_ = true;
    homeX_ = x;
    homeY_ = y;
    homeZ_ = z;
    homePlaceId_ = placeId ? placeId : "";
}

bool PersonalKnowledge::Home(i32* x, i32* y, i8* z) const {
    if (!homeSet_) return false;
    if (x) *x = homeX_;
    if (y) *y = homeY_;
    if (z) *z = homeZ_;
    return true;
}

void PersonalKnowledge::NoteDeath(i32 x, i32 y, i8 z, const char* regionId,
                                  i64 nowMs) {
    death_ = DeathRecord{};
    death_.valid = true;
    death_.x = x;
    death_.y = y;
    death_.z = z;
    death_.regionId = regionId ? regionId : "";
    death_.timeMs = nowMs;
}

void PersonalKnowledge::NoteCorpse(u32 serial, i32 x, i32 y, i8 z) {
    if (!death_.valid) return;
    death_.corpseSerial = serial;
    // Trust the corpse's own position over the death position: the server
    // decides where the body lands, and it is not always the tile we fell on.
    death_.x = x;
    death_.y = y;
    death_.z = z;
}

void PersonalKnowledge::NoteCorpseRecoveryAttempt() {
    if (death_.valid) ++death_.recoveryAttempts;
}

void PersonalKnowledge::ClearDeath() { death_ = DeathRecord{}; }

void PersonalKnowledge::NoteDanger(i32 x, i32 y, i32 radius, i64 untilMs,
                                   const char* why) {
    DangerNote n;
    n.x = x;
    n.y = y;
    n.radius = radius > 0 ? radius : 8;
    n.expiresMs = untilMs;
    n.why = why ? why : "";
    danger_.push_back(std::move(n));
}

wm::Danger PersonalKnowledge::DangerAt(i32 x, i32 y, i64 nowMs) const {
    for (const DangerNote& n : danger_) {
        if (nowMs >= n.expiresMs) continue;
        if (Chebyshev(x, y, n.x, n.y) <= n.radius)
            return wm::Danger::RecentlyDangerous;
    }
    // "Nothing bad has happened here" is not the same as "this is safe", and
    // the difference matters to a planner that has to weigh an unknown route.
    return wm::Danger::Unknown;
}

void PersonalKnowledge::ExpireDanger(i64 nowMs) {
    usize keep = 0;
    for (usize i = 0; i < danger_.size(); ++i) {
        if (nowMs < danger_[i].expiresMs) {
            if (keep != i) danger_[keep] = danger_[i];
            ++keep;
        }
    }
    danger_.resize(keep);
}

void PersonalKnowledge::Clear() {
    visits_.clear();
    sightings_.clear();
    runes_.clear();
    danger_.clear();
    death_ = DeathRecord{};
    homeSet_ = false;
    homePlaceId_.clear();
}

} // namespace uo::travel
