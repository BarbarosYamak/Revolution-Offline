#pragma once

#include "uo/types.h"
#include <algorithm>
#include <string>
#include <vector>

namespace uo::social {

enum class Activity : u8 { None, Hunt, Train, Spar, Poison };
enum class Encounter : u8 { Greeting, Help, Trade, Attack };

struct Relationship {
    std::string name;
    i32 trust = 0;
    i32 encounters = 0;
    i64 lastGoodwillMs = 0;
    bool foe = false;
};

inline const Relationship* Find(const std::vector<Relationship>& people, const std::string& name) {
    for (const auto& person : people) if (person.name == name) return &person;
    return nullptr;
}

inline void Remember(std::vector<Relationship>& people, const std::string& name,
                     Encounter encounter, i64 nowMs) {
    if (name.empty()) return;
    auto it = std::find_if(people.begin(), people.end(), [&](const auto& p) { return p.name == name; });
    if (it == people.end()) {
        // Keep established friends and foes when casual acquaintances overflow.
        if (people.size() >= 128) {
            auto disposable = std::min_element(people.begin(), people.end(), [](const auto& a, const auto& b) {
                return (a.foe ? 101 : std::abs(a.trust)) < (b.foe ? 101 : std::abs(b.trust));
            });
            if (disposable->foe) return;
            people.erase(disposable);
        }
        people.push_back({name});
        it = people.end() - 1;
    }
    if (encounter == Encounter::Attack) {
        it->foe = true;
        it->trust = -100;
        return;
    }
    // Speech cannot erase aggression, or farm friendship every client tick.
    if (it->foe || (it->lastGoodwillMs > 0 && nowMs >= it->lastGoodwillMs &&
        nowMs - it->lastGoodwillMs < 60000)) return;
    it->lastGoodwillMs = nowMs;
    it->encounters = std::min(100000, it->encounters + 1);
    it->trust = std::min(100, it->trust + (encounter == Encounter::Trade ? 15 :
                                        encounter == Encounter::Help ? 10 : 2));
}

inline bool SafeToSocialize(bool dead, bool attacked, i32 hostiles, double health, double weight) {
    return !dead && !attacked && hostiles == 0 && health >= 0.8 && weight < 0.8;
}

// A capped build can gain one skill while another is locked down. Compare
// individual skills; the total alone would report no training progress.
template<class Skills>
inline i32 TrainingGains(const Skills& before, const Skills& after) {
    i32 gained = 0;
    for (const auto& skill : after) for (const auto& old : before) {
        if (skill.skillId == old.skillId) { gained += std::max(0, skill.tenths - old.tenths); break; }
    }
    return gained;
}

inline bool IsInvitation(const std::string& text, Activity* activity) {
    if (text == "Anyone for Poison spell practice? Healing and Anatomy above 60 required. Cure between casts.") { *activity = Activity::Poison; return true; }
    if (text == "Anyone for consensual iron-armour sparring? Stop when hurt.") { *activity = Activity::Spar; return true; }
    if (text == "Anyone for a graveyard hunt? Meet here.") { *activity = Activity::Hunt; return true; }
    if (text == "Anyone for training and healing practice? Meet here.") { *activity = Activity::Train; return true; }
    return false;
}

inline std::string JoinReply(const std::string& leader, Activity activity) {
    if (activity == Activity::Poison) return leader + ": I consent to Poison practice; my Healing and Anatomy are above 60. I'll cure between casts.";
    if (activity == Activity::Spar) return leader + ": I consent to sparring. Stop when hurt.";
    return leader + (activity == Activity::Hunt ? ": I'll join your hunt." : ": I'll join your training.");
}

} // namespace uo::social
