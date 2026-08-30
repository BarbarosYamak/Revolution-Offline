// Creation-time appearance roll (docs/APPEARANCE_DESIGN.md layer 1).
// Pure logic over an identity string -- no server, no MULs, no world data.

#include "uo/appearance.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace uo;

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FAIL  %s\n", what);
    }
}

bool SameAppearance(const Appearance& a, const Appearance& b) {
    return a.female == b.female && a.skinHue == b.skinHue &&
           a.hairId == b.hairId && a.hairHue == b.hairHue &&
           a.beardId == b.beardId && a.beardHue == b.beardHue &&
           a.shirtHue == b.shirtHue && a.pantsHue == b.pantsHue;
}

bool InRange(u16 v, u16 lo, u16 hi) { return v >= lo && v <= hi; }

// The exact ranges cited in src/life/Appearance.cpp -- Source-X's own
// CChar::InitPlayer clamps for a human character created through the 0x00
// packet.
constexpr u16 kSkinLow = 1002, kSkinHigh = 1058;      // colors_skin {1002 1058}
constexpr u16 kHairHueLow = 1102, kHairHueHigh = 1149; // colors_hair curated range
constexpr u16 kDyeLow = 2, kDyeHigh = 1001;            // colors_all {2 1001}

// Valid style ids, mirroring src/life/Appearance.cpp's own tables (CChar.cpp
// "Create hair" / beard case).
bool IsValidHairId(u16 id, bool female) {
    switch (id) {
        case 0x203B: case 0x203C: case 0x203D:
        case 0x2044: case 0x2045: case 0x2047: case 0x2049: case 0x204A:
            return true;
        case 0x2046: return female;  // buns, female only
        case 0x2048: return !female; // receding, male only
        default: return false;
    }
}

bool IsValidBeardId(u16 id) {
    switch (id) {
        case 0:      // no beard
        case 0x203E: case 0x203F: case 0x2040: case 0x2041:
        case 0x204B: case 0x204C: case 0x204D:
            return true;
        default: return false;
    }
}

} // namespace

int main() {
    // --- Determinism: same identity id -> identical appearance, always. ---
    for (const char* id : {"tarath.smithy_01", "durnholde.ysolde", "a.b"}) {
        const Appearance a1 = AppearanceForIdentity(id);
        const Appearance a2 = AppearanceForIdentity(id);
        const Appearance a3 = AppearanceForIdentity(std::string(id));
        Check(SameAppearance(a1, a2), "same identity id yields identical appearance (call 2)");
        Check(SameAppearance(a1, a3), "same identity id yields identical appearance (call 3, std::string ctor)");
    }

    // --- Diversity: 20 different identity ids must not all collapse to one
    // appearance -- at least one field differs somewhere across the set. ---
    std::vector<std::string> ids;
    for (int i = 0; i < 20; ++i) {
        ids.push_back("account" + std::to_string(i) + ".char" + std::to_string(i));
    }
    std::vector<Appearance> rolled;
    rolled.reserve(ids.size());
    for (const auto& id : ids) rolled.push_back(AppearanceForIdentity(id));

    bool anyDiffer = false;
    for (usize i = 1; i < rolled.size(); ++i) {
        if (!SameAppearance(rolled[0], rolled[i])) { anyDiffer = true; break; }
    }
    Check(anyDiffer, "20 different identity ids differ in at least one field");

    // A stronger diversity check: sex should not be uniformly one value
    // across 20 different ids (50% roll would need a 2^-19 coincidence to
    // fail this honestly).
    int femaleCount = 0;
    for (const auto& ap : rolled) if (ap.female) ++femaleCount;
    Check(femaleCount > 0 && femaleCount < static_cast<int>(rolled.size()),
          "sex varies across 20 different identity ids (not all-male or all-female)");

    // --- Range/validity checks over the whole 20-id set. -------------------
    for (usize i = 0; i < rolled.size(); ++i) {
        const Appearance& ap = rolled[i];
        const std::string label = "id[" + std::to_string(i) + "]=" + ids[i];

        Check(InRange(ap.skinHue, kSkinLow, kSkinHigh),
              ("skin hue within colors_skin {1002 1058} for " + label).c_str());
        Check(InRange(ap.hairHue, kHairHueLow, kHairHueHigh),
              ("hair hue within the shard's curated 1102-1149 range for " + label).c_str());
        Check(InRange(ap.shirtHue, kDyeLow, kDyeHigh),
              ("shirt hue within colors_all {2 1001} for " + label).c_str());
        Check(InRange(ap.pantsHue, kDyeLow, kDyeHigh),
              ("pants hue within colors_all {2 1001} for " + label).c_str());
        Check(IsValidHairId(ap.hairId, ap.female),
              ("hair style id is a valid, sex-appropriate CChar::InitPlayer id for " + label).c_str());

        if (ap.female) {
            Check(ap.beardId == 0 && ap.beardHue == 0,
                  ("female roll carries no beard for " + label).c_str());
        } else {
            Check(IsValidBeardId(ap.beardId),
                  ("beard style id is 0 or a valid CChar::InitPlayer id for " + label).c_str());
            if (ap.beardId != 0) {
                Check(InRange(ap.beardHue, kHairHueLow, kHairHueHigh),
                      ("beard hue shares the hair-hue range for " + label).c_str());
            } else {
                Check(ap.beardHue == 0, ("beardHue is 0 when beardId is 0 for " + label).c_str());
            }
        }
    }

    std::printf("appearance: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
