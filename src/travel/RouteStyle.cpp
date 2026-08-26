#include "uo/route_style.h"

namespace uo::routing {

const char* PreferenceName(Preference p) {
    switch (p) {
        case Preference::Shortest:      return "shortest";
        case Preference::RoadPreferred: return "road";
        case Preference::LowCongestion: return "uncrowded";
        case Preference::LowRisk:       return "safe";
        case Preference::Mixed:         return "mixed";
        default:                        return "?";
    }
}

namespace {

// FNV-1a. Chosen because it is short, stable across platforms and has no
// hidden state -- the same name must give the same style on every machine.
u64 HashName(const char* s) {
    u64 h = 1469598103934665603ull;
    if (!s) return h;
    for (; *s; ++s) {
        // Fold case so "Bob" and "bob" are the same character's habits.
        char c = *s;
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        h ^= static_cast<u64>(static_cast<unsigned char>(c));
        h *= 1099511628211ull;
    }
    return h ? h : 1;
}

// Mix three values into a well-distributed 64-bit result. Used for spatial
// bias, so neighbouring tiles must not produce neighbouring values -- if they
// did, the "bias" would just be a smooth gradient and every character would
// drift the same way.
u64 Mix(u64 a, u64 b, u64 c) {
    u64 h = a ^ (b * 0x9E3779B97F4A7C15ull) ^ (c * 0xC2B2AE3D27D4EB4Full);
    h ^= h >> 33; h *= 0xFF51AFD7ED558CCDull;
    h ^= h >> 33; h *= 0xC4CEB9FE1A85EC53ull;
    h ^= h >> 33;
    return h;
}

u64 ZigZag(i32 v) {
    // Map signed coordinates onto unsigned without collisions.
    return static_cast<u64>((static_cast<i64>(v) << 1) ^ (static_cast<i64>(v) >> 63));
}

} // namespace

Style StyleForCharacter(const char* name) {
    Style s;
    s.seed = HashName(name);
    // Spread preferences over the population, but stably per character.
    s.preference = static_cast<Preference>((s.seed >> 7) % static_cast<u64>(Preference::Count));
    // Most people walk roughly straight; a minority wander a little wider.
    const u64 w = (s.seed >> 19) % 100;
    s.laneWidth = static_cast<u8>(w < 55 ? 1 : (w < 90 ? 2 : 0));
    // Shortest-preference characters keep their drift tight whatever the roll,
    // otherwise the preference would not mean anything.
    if (s.preference == Preference::Shortest && s.laneWidth > 1) s.laneWidth = 1;
    return s;
}

i32 Variation::CellBias(i32 x, i32 y, i32 maxBias) const {
    if (maxBias <= 0) return 0;
    const u64 h = Mix(style_.seed, ZigZag(x), ZigZag(y));
    i32 bias = static_cast<i32>(h % static_cast<u64>(maxBias + 1));

    // Preference scales how much the character indulges its own bias.
    switch (style_.preference) {
        case Preference::Shortest:      bias /= 3; break;   // barely deviates
        case Preference::Mixed:         bias = (bias * 2) / 3; break;
        default:                        break;              // full bias
    }
    return bias;
}

usize Variation::Choose(usize count, u32 salt) const {
    if (count == 0) return 0;
    const u64 h = Mix(style_.seed, salt, 0x5BF03635ull);
    return static_cast<usize>(h % static_cast<u64>(count));
}

usize Variation::PickApproach(const std::vector<wm::Point>& candidates, u32 salt) const {
    if (candidates.empty()) return 0;
    // A Shortest character always takes the first (nearest) option the caller
    // offered; everyone else picks their own, stably.
    if (style_.preference == Preference::Shortest) return 0;
    return Choose(candidates.size(), salt);
}

void Variation::NotePassed(i32 x, i32 y, i64 nowMs) {
    for (Recent& r : recent_) {
        if (r.x == x && r.y == y) { r.atMs = nowMs; return; }
    }
    if (recent_.size() >= kRecentMax) {
        // Drop the oldest. Linear, but the list is small and bounded and this
        // runs once per tile walked.
        usize oldest = 0;
        for (usize i = 1; i < recent_.size(); ++i)
            if (recent_[i].atMs < recent_[oldest].atMs) oldest = i;
        recent_[oldest] = Recent{x, y, nowMs};
        return;
    }
    recent_.push_back(Recent{x, y, nowMs});
}

i32 Variation::RecentPenalty(i32 x, i32 y, i64 nowMs) const {
    for (const Recent& r : recent_) {
        if (r.x != x || r.y != y) continue;
        const i64 age = nowMs - r.atMs;
        if (age < 0 || age >= kRecentWindowMs) return 0;
        // Linear decay: freshly walked tiles are the least attractive.
        const i64 left = kRecentWindowMs - age;
        return static_cast<i32>((static_cast<i64>(kRecentPenalty) * left) / kRecentWindowMs);
    }
    return 0;
}

void Variation::ForgetOlderThan(i64 nowMs) {
    usize w = 0;
    for (usize i = 0; i < recent_.size(); ++i) {
        if (nowMs - recent_[i].atMs < kRecentWindowMs) recent_[w++] = recent_[i];
    }
    recent_.resize(w);
}

i32 Variation::TileCost(i32 x, i32 y, i64 nowMs, i32 maxBias) const {
    i32 cost = CellBias(x, y, maxBias);
    // A character that dislikes crowds and repetition weighs its own recent
    // path more heavily; one in a hurry ignores it.
    if (style_.preference != Preference::Shortest)
        cost += RecentPenalty(x, y, nowMs);
    return cost;
}

} // namespace uo::routing
