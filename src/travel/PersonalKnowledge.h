#pragma once

// ---------------------------------------------------------------------------
// PersonalKnowledge — what THIS character knows, as opposed to what is true.
//
// The Atlas is the world: it is shared, immutable and identical for everyone.
// This is the other half, and it is per-session by construction. A bot knows a
// mine because it has been there, knows a healer because it has seen one,
// owns a rune because it marked one, and remembers a corpse because it died.
// None of that may leak between characters, so this object is a plain member
// of Client and is never global, static or shared.
//
// It is also deliberately small. M2.5 needs enough personal state to prove the
// isolation rule and to support corpse recovery and rune travel later; it is
// not a memory system, and the LLM/social layers are M3+.
// ---------------------------------------------------------------------------

#include "uo/types.h"
#include "uo/world_model.h"

#include <string>
#include <vector>

namespace uo::travel {

// A recall rune this character owns. `marked` is only ever set from what the
// server told us about the item -- a rune's destination is the shard's to
// know, and inventing coordinates for one would be exactly the synthetic
// travel this milestone forbids.
struct KnownRune {
    u32         serial = 0;
    u16         graphic = 0;
    std::string name;        // Sphere renames a marked rune to its region
    bool        marked = false;
    i32         x = 0;       // only meaningful once the destination is known
    i32         y = 0;
    i8          z = 0;
    bool        destinationKnown = false;
};

// Where and when this character last died, and the corpse if we saw it. Kept
// so a future corpse-recovery policy has something to reason about; M2.5 only
// exposes navigation to it.
struct DeathRecord {
    bool        valid = false;
    i32         x = 0, y = 0;
    i8          z = 0;
    std::string regionId;
    u32         corpseSerial = 0;
    i64         timeMs = 0;
    int         recoveryAttempts = 0;
};

// A place this character has actually stood in, with when. Distinct from the
// atlas knowing the place exists.
struct VisitRecord {
    std::string placeId;
    i64         lastVisitMs = 0;
    int         visits = 0;
};

// A live sighting of a service provider. Overrides the atlas position while
// fresh: the atlas says where the shop is, this says where the shopkeeper
// actually was.
struct ServiceSighting {
    wm::Service service = wm::Service::None;
    u32         serial = 0;
    std::string title;      // paperdoll title that identified the trade
    i32         x = 0, y = 0;
    i8          z = 0;
    i64         seenMs = 0;
};

// Somewhere that went badly. Coarse on purpose -- see wm::Danger.
struct DangerNote {
    i32 x = 0, y = 0;
    i32 radius = 0;
    i64 expiresMs = 0;
    std::string why;
};

class PersonalKnowledge {
public:
    // --- places ------------------------------------------------------------
    void NoteVisit(const char* placeId, i64 nowMs);
    bool HasVisited(const char* placeId) const;
    const std::vector<VisitRecord>& Visits() const { return visits_; }

    // --- live service sightings -------------------------------------------
    void NoteService(wm::Service s, u32 serial, const char* title,
                     i32 x, i32 y, i8 z, i64 nowMs);
    void ForgetService(u32 serial);
    // Freshest sighting of `s` within `maxAgeMs`, or null.
    const ServiceSighting* RecentService(wm::Service s, i64 nowMs,
                                         i64 maxAgeMs) const;

    // --- runes -------------------------------------------------------------
    void NoteRune(const KnownRune& r);
    void ForgetRune(u32 serial);
    const std::vector<KnownRune>& Runes() const { return runes_; }
    // A marked rune whose destination we know and that lands within `maxDist`
    // of (x, y). Null when this character owns nothing that helps.
    const KnownRune* BestRuneFor(i32 x, i32 y, i32 maxDist) const;
    bool OwnsMarkedRune() const;

    // --- home --------------------------------------------------------------
    void SetHome(i32 x, i32 y, i8 z, const char* placeId);
    bool HasHome() const { return homeSet_; }
    bool Home(i32* x, i32* y, i8* z) const;
    const std::string& HomePlaceId() const { return homePlaceId_; }

    // --- death / corpse ----------------------------------------------------
    void NoteDeath(i32 x, i32 y, i8 z, const char* regionId, i64 nowMs);
    void NoteCorpse(u32 serial, i32 x, i32 y, i8 z);
    void NoteCorpseRecoveryAttempt();
    void ClearDeath();
    const DeathRecord& LastDeath() const { return death_; }

    // --- danger ------------------------------------------------------------
    void NoteDanger(i32 x, i32 y, i32 radius, i64 untilMs, const char* why);
    wm::Danger DangerAt(i32 x, i32 y, i64 nowMs) const;
    void ExpireDanger(i64 nowMs);
    usize DangerNoteCount() const { return danger_.size(); }

    void Clear();

private:
    std::vector<VisitRecord>     visits_;
    std::vector<ServiceSighting> sightings_;
    std::vector<KnownRune>       runes_;
    std::vector<DangerNote>      danger_;
    DeathRecord                  death_;
    bool        homeSet_ = false;
    i32         homeX_ = 0, homeY_ = 0;
    i8          homeZ_ = 0;
    std::string homePlaceId_;
};

} // namespace uo::travel
