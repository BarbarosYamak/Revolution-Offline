#pragma once
#include "uo/types.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

// WHO THIS CHARACTER IS WHEN NOBODY IS WATCHING: temperament and the hours
// its player sits at the keyboard.
//
// CLAUDE.md asks every bot to carry a personality and a play schedule. Before
// this file every life of one profession had the same nerve, the same urge to
// chat, and was on line exactly when tools/fleet_ramp.py happened to start
// it: a hundred characters logging in at the same second and out forty-five
// minutes later is a load test, not a shard.
//
// Deterministic from the identity id, like the home city (Core.cpp): the same
// character keeps the same evenings even if its state file is lost, and the
// population manager (tools/fleet_ramp.py) computes the very same schedule
// without asking the character. tests/data/persona_vectors.tsv pins the two
// implementations together; tests/persona.cpp and tests/test_fleet_ramp.py
// both fail if either drifts.
//
// EVIDENCE. When Revolution's players were on line is UNKNOWN: no population
// log or forum census has been found. The rhythms below are DERIVED -- a
// Turkish shard whose players were mostly students and working adults, busiest
// after school/work and at weekends, in the shard's own clock (Europe/Istanbul;
// the host's local time). Every number is a tunable, not a Revolution fact.
namespace uo::persona {

enum class Rhythm : u8 {
    Evening = 0,   // after work: weekday evenings, long weekend late afternoons into the night
    LateNight,     // starts late (21:00+), plays past midnight, every day
    Afternoon,     // after school: weekday afternoons, weekend middays
    Weekender,     // Friday night and long Saturday/Sunday sessions only
    Morning,       // shift worker / free days: morning to midday, plus two evenings
    Count
};

inline const char* RhythmName(Rhythm r) {
    switch (r) {
        case Rhythm::Evening:   return "evening";
        case Rhythm::LateNight: return "late_night";
        case Rhythm::Afternoon: return "afternoon";
        case Rhythm::Weekender: return "weekender";
        case Rhythm::Morning:   return "morning";
        default:                return "unknown";
    }
}

// Day bits: Monday = bit 0 ... Sunday = bit 6 (ISO order, as Python's
// datetime.weekday()). A window starting at 23:00 with 180 minutes runs into
// the next day; the start day is what the mask names.
inline constexpr u8 kMon = 1, kTue = 2, kWed = 4, kThu = 8, kFri = 16, kSat = 32, kSun = 64;
inline constexpr u8 kWeekdays = kMon | kTue | kWed | kThu | kFri;
inline constexpr u8 kWeekend = kSat | kSun;
inline constexpr u8 kEveryDay = kWeekdays | kWeekend;
inline constexpr i32 kDayMin = 24 * 60;
inline constexpr i32 kWeekMin = 7 * kDayMin;

struct Window {
    u8  days = 0;
    i32 startMin = 0;    // minute of the day, 0..1439
    i32 lengthMin = 0;   // may cross midnight
};

struct Persona {
    bool   set = false;
    Rhythm rhythm = Rhythm::Evening;
    // Added to the profession's riskTolerance, in hundredths (-15..+15): two
    // swordsmen need not have the same nerve.
    i32    riskShift = 0;
    // 0..100. How readily this character stops to talk, party up or trade.
    i32    sociability = 50;
    std::vector<Window> windows;
};

// ---- deterministic draws (mirrored exactly by tools/fleet_ramp.py) ----------
inline u32 Fnv1a(const std::string& s) {
    u32 h = 2166136261u;
    for (unsigned char c : s) { h ^= c; h *= 16777619u; }
    return h;
}

struct Draw {
    u32 state;
    explicit Draw(u32 seed) : state(seed ? seed : 0x9E3779B9u) {}
    u32 Next() {                       // xorshift32
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        return state;
    }
    i32 Below(i32 n) { return n > 0 ? static_cast<i32>(Next() % static_cast<u32>(n)) : 0; }
    i32 Range(i32 lo, i32 hi) { return lo + Below(hi - lo + 1); }   // inclusive
};

// Weekdays this rhythm may take off: one per week for the two most regular
// rhythms, so the evening crowd is not identical Monday to Friday.
inline u8 RestDay(Draw& d) {
    static constexpr u8 kDays[] = {kMon, kTue, kWed, kThu, kFri};
    return kDays[d.Below(5)];
}

inline Persona Make(const std::string& identityId) {
    Draw d(Fnv1a(identityId));
    Persona p;
    p.set = true;
    // 40 / 20 / 15 / 15 / 10.
    const i32 roll = d.Below(100);
    p.rhythm = roll < 40 ? Rhythm::Evening
             : roll < 60 ? Rhythm::LateNight
             : roll < 75 ? Rhythm::Afternoon
             : roll < 90 ? Rhythm::Weekender
                         : Rhythm::Morning;
    p.riskShift = d.Range(-15, 15);
    p.sociability = d.Range(15, 95);
    switch (p.rhythm) {
        case Rhythm::Evening: {
            const u8 rest = RestDay(d);
            p.windows.push_back({static_cast<u8>(kWeekdays & ~rest), 19 * 60 + d.Range(0, 90), d.Range(120, 240)});
            p.windows.push_back({kWeekend, 16 * 60 + d.Range(0, 180), d.Range(240, 360)});
            break;
        }
        case Rhythm::LateNight:
            p.windows.push_back({kEveryDay, 21 * 60 + d.Range(0, 90), d.Range(150, 270)});
            break;
        case Rhythm::Afternoon: {
            const u8 rest = RestDay(d);
            p.windows.push_back({static_cast<u8>(kWeekdays & ~rest), 15 * 60 + d.Range(0, 90), d.Range(120, 180)});
            p.windows.push_back({kWeekend, 12 * 60 + d.Range(0, 60), d.Range(240, 300)});
            break;
        }
        case Rhythm::Weekender:
            p.windows.push_back({kFri, 20 * 60 + d.Range(0, 60), d.Range(180, 300)});
            p.windows.push_back({kWeekend, 11 * 60 + d.Range(0, 120), d.Range(360, 480)});
            break;
        case Rhythm::Morning: {
            p.windows.push_back({kEveryDay, 8 * 60 + d.Range(0, 240), d.Range(90, 150)});
            const u8 a = RestDay(d);
            u8 b = RestDay(d);
            if (b == a) b = (a == kFri) ? kMon : static_cast<u8>(a << 1);
            p.windows.push_back({static_cast<u8>(a | b), 20 * 60 + d.Range(0, 60), d.Range(90, 150)});
            break;
        }
        default: break;
    }
    return p;
}

// ---- the clock -----------------------------------------------------------------
// weekday: 0 = Monday. Returns the minutes still to play in the window that
// covers this moment (the longest, if two overlap); 0 = not a play time.
inline i32 MinutesLeft(const Persona& p, int weekday, int minuteOfDay) {
    const i32 now = ((weekday % 7 + 7) % 7) * kDayMin + minuteOfDay;
    i32 best = 0;
    for (const Window& w : p.windows) {
        for (int day = 0; day < 7; ++day) {
            if (!(w.days & (1u << day))) continue;
            const i32 start = day * kDayMin + w.startMin;
            // Minutes since this start on a circular week (Sunday night windows
            // run into Monday morning).
            const i32 since = ((now - start) % kWeekMin + kWeekMin) % kWeekMin;
            if (since < w.lengthMin) best = std::max(best, w.lengthMin - since);
        }
    }
    return best;
}

inline bool Playing(const Persona& p, int weekday, int minuteOfDay) {
    return MinutesLeft(p, weekday, minuteOfDay) > 0;
}

// Minutes until the next window opens (0 if one is open now; -1 if none ever).
inline i32 MinutesUntilNext(const Persona& p, int weekday, int minuteOfDay) {
    if (Playing(p, weekday, minuteOfDay)) return 0;
    const i32 now = ((weekday % 7 + 7) % 7) * kDayMin + minuteOfDay;
    i32 best = -1;
    for (const Window& w : p.windows)
        for (int day = 0; day < 7; ++day) {
            if (!(w.days & (1u << day))) continue;
            const i32 until = ((day * kDayMin + w.startMin - now) % kWeekMin + kWeekMin) % kWeekMin;
            if (best < 0 || until < best) best = until;
        }
    return best;
}

// Minutes on line per week, for the population report.
inline i32 WeeklyMinutes(const Persona& p) {
    i32 total = 0;
    for (const Window& w : p.windows)
        for (int day = 0; day < 7; ++day)
            if (w.days & (1u << day)) total += w.lengthMin;
    return total;
}

// ---- temperament ---------------------------------------------------------------
inline double Nerve(double professionRisk, const Persona& p) {
    const double v = professionRisk + (p.set ? p.riskShift / 100.0 : 0.0);
    return std::min(0.95, std::max(0.05, v));
}

// Urgency of "go and find company", centred on the old fixed 0.65.
inline double SocialUrgency(const Persona& p) {
    if (!p.set) return 0.65;
    return 0.45 + 0.40 * (std::min(100, std::max(0, p.sociability)) / 100.0);
}

// "Mon 19:40+185" -- one line per window, for logs, the TSV vectors and the
// population report.
inline std::string Describe(const Persona& p) {
    static const char* kNames[] = {"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"};
    std::string out;
    for (const Window& w : p.windows) {
        if (!out.empty()) out += ' ';
        for (int day = 0; day < 7; ++day) if (w.days & (1u << day)) out += kNames[day];
        char buf[32];
        std::snprintf(buf, sizeof(buf), "@%02d:%02d+%d", w.startMin / 60, w.startMin % 60, w.lengthMin);
        out += buf;
    }
    return out;
}

}  // namespace uo::persona
