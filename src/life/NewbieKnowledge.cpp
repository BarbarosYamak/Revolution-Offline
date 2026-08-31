#include "uo/newbie_knowledge.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace uo::life {

namespace {

i32 Chebyshev(i32 ax, i32 ay, i32 bx, i32 by) {
    return std::max(std::abs(ax - bx), std::abs(ay - by));
}

// Profession::gathers -> wm::ResourceKind. Deliberately a small, local copy
// of Runner.cpp's kGatherKinds (Runner.cpp, "ask the atlas what THIS life's
// work is, not where the trees are") rather than a shared header: the table
// is three rows, unlikely to grow without a matching atlas resource kind
// appearing too, and this file must stay Client-free so it can be linked
// into a no-server unit test.
wm::ResourceKind ResourceKindForGathers(const std::string& gathers) {
    if (gathers == "logs") return wm::ResourceKind::Lumber;
    if (gathers == "ore")  return wm::ResourceKind::Mining;
    if (gathers == "fish") return wm::ResourceKind::Fishing;
    return wm::ResourceKind::None;
}

void SeedServicePlace(Memory& memory, const world_atlas::Atlas& atlas,
                      const char* kind, wm::Service service, i32 anchorX,
                      i32 anchorY, i64 nowMs) {
    const wm::Place* p = atlas.NearestPlaceWithService(
        service, anchorX, anchorY, kNewbieKnowledgeRadius);
    if (!p) return;
    memory.NotePlace(kind, p->name.c_str(), p->position.x, p->position.y,
                     p->position.z, nowMs);
}

}  // namespace

void SeedNewbieKnowledge(PersistentState& state, const prof::Profession* profession,
                         const std::string& homeCity,
                         const world_atlas::Atlas& atlas, i64 nowMs) {
    // No home, no "day one" knowledge to seed -- a character without a home
    // city has nothing to be a newcomer TO yet.
    if (homeCity.empty()) return;
    if (!atlas.Ready()) return;

    // ANCHOR ON THE HOME REGION, NOT ON WHEREVER THE CHARACTER HAPPENS TO BE
    // STANDING. The old SeedCommonKnowledge measured from the live player
    // position, which is only ever the same as home by coincidence -- the
    // shard's own chargen spawn point (map0_starts.scp) is not driven by
    // Profession::homeCities at all.
    const wm::Region* home = atlas.FindRegion(homeCity.c_str());
    if (!home) return;
    const i32 anchorX = home->center.x;
    const i32 anchorY = home->center.y;

    // (a) the home bank.
    SeedServicePlace(state.memory, atlas, "common_knowledge_bank",
                     wm::Service::Banker, anchorX, anchorY, nowMs);

    // (c) the home healer and provisioner -- the two other counters a
    // newcomer to town would already know how to find.
    SeedServicePlace(state.memory, atlas, "common_knowledge_healer",
                     wm::Service::Healer, anchorX, anchorY, nowMs);
    SeedServicePlace(state.memory, atlas, "common_knowledge_provisioner",
                     wm::Service::Provisioner, anchorX, anchorY, nowMs);

    // (b) up to kNewbieResourceHints resource areas near home that yield
    // what this life gathers -- LEADS, never stands. A profession that buys
    // its inputs instead (gathers.empty()) or predates the catalogue
    // (profession == nullptr) gets none of this, same as
    // Runner::SeedCommonKnowledge always required.
    if (!profession || profession->gathers.empty()) return;
    const wm::ResourceKind kind = ResourceKindForGathers(profession->gathers);
    if (kind == wm::ResourceKind::None) return;

    std::vector<const wm::Place*> areas;
    for (const wm::Place& p : atlas.Places()) {
        if (!p.Yields(kind)) continue;
        if (Chebyshev(anchorX, anchorY, p.position.x, p.position.y) >
            kNewbieKnowledgeRadius)
            continue;
        areas.push_back(&p);
    }
    std::sort(areas.begin(), areas.end(),
             [anchorX, anchorY](const wm::Place* a, const wm::Place* b) {
                 return Chebyshev(anchorX, anchorY, a->position.x, a->position.y) <
                        Chebyshev(anchorX, anchorY, b->position.x, b->position.y);
             });

    int seeded = 0;
    for (const wm::Place* p : areas) {
        if (seeded >= kNewbieResourceHints) break;
        state.memory.HintResource(profession->gathers.c_str(), p->name.c_str(),
                                  p->position.x, p->position.y, p->position.z,
                                  nowMs);
        ++seeded;
    }
}

}  // namespace uo::life
