// Personas and play schedules (uo/persona.h): deterministic, legal windows,
// a believable population curve, and the vectors that pin the C++ generator
// to tools/fleet_ramp.py's mirror of it.
//
//   persona <vectors.tsv>            check against the pinned vectors
//   persona <vectors.tsv> --write    regenerate them (only on a deliberate change)

#include "uo/persona.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace uo;
using namespace uo::persona;

static int g_checks = 0, g_failures = 0;
static void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_failures; std::printf("  FAIL: %s\n", what); }
}

static std::string Line(const std::string& id) {
    const Persona p = Make(id);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "\t%s\t%d\t%d\t", RhythmName(p.rhythm), p.riskShift, p.sociability);
    return id + buf + Describe(p);
}

static std::vector<std::string> VectorIds() {
    std::vector<std::string> ids;
    for (int i = 1; i <= 60; ++i) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "revscale100_%03d.name%d", i, i * 7);
        ids.push_back(buf);
    }
    ids.push_back("hector.hector");
    ids.push_back("aurelius.aurelius");
    ids.push_back("odessa.odessa");
    return ids;
}

int main(int argc, char** argv) {
    std::printf("[determinism]\n");
    {
        const Persona a = Make("acc.ayse"), b = Make("acc.ayse");
        Check(Describe(a) == Describe(b) && a.riskShift == b.riskShift && a.sociability == b.sociability,
              "same identity, same persona");
        Check(Fnv1a("a") == 0xE40C292Cu, "FNV-1a 32 matches the reference value for \"a\"");
    }
    std::printf("[legal windows]\n");
    int rhythms[static_cast<int>(Rhythm::Count)] = {};
    int hourly[7][24] = {};
    const int kPeople = 2000;
    bool allSane = true, allPlay = true;
    for (int i = 0; i < kPeople; ++i) {
        const Persona p = Make("fleet.char" + std::to_string(i));
        ++rhythms[static_cast<int>(p.rhythm)];
        if (p.riskShift < -15 || p.riskShift > 15 || p.sociability < 0 || p.sociability > 100) allSane = false;
        for (const Window& w : p.windows)
            if (!w.days || w.startMin < 0 || w.startMin >= kDayMin || w.lengthMin <= 0 || w.lengthMin > 8 * 60)
                allSane = false;
        const i32 week = WeeklyMinutes(p);
        if (week < 5 * 60 || week > 40 * 60) allPlay = false;
        for (int d = 0; d < 7; ++d)
            for (int h = 0; h < 24; ++h) hourly[d][h] += Playing(p, d, h * 60 + 30);
    }
    Check(allSane, "every window, nerve and sociability is in range");
    Check(allPlay, "everyone plays between 5 and 40 hours a week");
    for (int r = 0; r < static_cast<int>(Rhythm::Count); ++r)
        Check(rhythms[r] > kPeople / 20, "every rhythm occurs");
    std::printf("[population curve]\n");
    {
        // Tuesday: quiet at 05:30, busy at 21:30; Saturday afternoon busier
        // than Tuesday afternoon.
        Check(hourly[1][5] < hourly[1][21] / 5, "weekday dawn is nearly empty next to the evening");
        Check(hourly[1][21] > kPeople / 4, "a weekday evening has at least a quarter of the fleet on");
        Check(hourly[5][15] > hourly[1][15], "Saturday afternoon is busier than Tuesday afternoon");
        std::printf("  Tuesday hourly online of %d:", kPeople);
        for (int h = 0; h < 24; ++h) std::printf(" %d", hourly[1][h]);
        std::printf("\n");
    }
    std::printf("[clock]\n");
    {
        Persona p; p.set = true;
        p.windows.push_back({kSun, 23 * 60, 120});   // Sunday 23:00 into Monday 01:00
        Check(MinutesLeft(p, 6, 23 * 60 + 30) == 90, "inside a window: minutes left");
        Check(MinutesLeft(p, 0, 30) == 30, "a Sunday-night window runs into Monday (week wraps)");
        Check(!Playing(p, 0, 61), "and ends on time");
        Check(MinutesUntilNext(p, 6, 22 * 60) == 60, "an hour until the window opens");
        Check(MinutesUntilNext(p, 0, 60) == 6 * kDayMin + 22 * 60, "next week's window after it closes");
        Persona none; none.set = true;
        Check(MinutesUntilNext(none, 0, 0) == -1 && !Playing(none, 0, 0), "no windows: never plays");
    }
    std::printf("[temperament]\n");
    {
        Persona p; p.set = true; p.riskShift = 15;
        Check(Nerve(0.9, p) == 0.95 && Nerve(0.5, p) > 0.64 && Nerve(0.5, p) < 0.66, "nerve shift, clamped");
        p.riskShift = -15;
        Check(Nerve(0.1, p) == 0.05, "nobody has zero nerve");
        Persona unset;
        Check(Nerve(0.4, unset) == 0.4 && SocialUrgency(unset) == 0.65, "no persona: the old fixed values");
        p.sociability = 100;
        const double hi = SocialUrgency(p);
        p.sociability = 0;
        Check(hi > SocialUrgency(p) && SocialUrgency(p) >= 0.45 && hi <= 0.85 + 1e-9, "sociability scales the urge to meet");
    }
    std::printf("[vectors shared with tools/fleet_ramp.py]\n");
    if (argc > 1) {
        const bool write = argc > 2 && std::strcmp(argv[2], "--write") == 0;
        std::string expected;
        for (const std::string& id : VectorIds()) expected += Line(id) + "\n";
        if (write) {
            std::ofstream(argv[1], std::ios::binary) << expected;
            std::printf("  wrote %s\n", argv[1]);
        } else {
            std::ifstream in(argv[1], std::ios::binary);
            std::stringstream ss; ss << in.rdbuf();
            std::string got = ss.str();
            got.erase(std::remove(got.begin(), got.end(), '\r'), got.end());
            Check(!got.empty(), "the vector file exists");
            Check(got == expected, "the generator still produces the pinned vectors");
        }
    }
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
