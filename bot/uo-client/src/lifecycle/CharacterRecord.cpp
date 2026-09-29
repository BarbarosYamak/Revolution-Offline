#include "uo/lifecycle.h"
#include "uo/vendor_policy.h"
#include "uo/world_model.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>

namespace uo::life {

namespace {

constexpr const char* kMagic = "revolution-offline-character";

// ---------------------------------------------------------------------------
// names
// ---------------------------------------------------------------------------

constexpr const char* kAspirationNames[] = {
    "unknown",
    "lumberjack_swordsman",
};
static_assert(sizeof(kAspirationNames) / sizeof(kAspirationNames[0]) ==
              static_cast<usize>(Aspiration::Count));

constexpr const char* kObjectiveNames[] = {
    "none",
    "recover_corpse",
    "survive",
    "eat",
    "rearm",
    "unload",
    "earn",
    "train",
    "pursue",
};
static_assert(sizeof(kObjectiveNames) / sizeof(kObjectiveNames[0]) ==
              static_cast<usize>(ObjectiveKind::Count));

constexpr const char* kLoadStatusNames[] = {"ok", "not_found", "corrupt", "invalid"};
static_assert(sizeof(kLoadStatusNames) / sizeof(kLoadStatusNames[0]) ==
              static_cast<usize>(LoadStatus::Count));

// Enums go to disk by NAME, never by ordinal: inserting a value in the middle
// of an enum must not silently re-map every saved character.
template <typename E>
bool EnumFromName(std::string_view name, const char* (*nameOf)(E), E* out) {
    for (int i = 0; i < static_cast<int>(E::Count); ++i) {
        const E e = static_cast<E>(i);
        const char* n = nameOf(e);
        if (n && name == n) { *out = e; return true; }
    }
    return false;
}

// ---------------------------------------------------------------------------
// field escaping -- a name with a tab in it must not shift every column after
// ---------------------------------------------------------------------------

void AppendEscaped(std::string* out, std::string_view s) {
    for (char ch : s) {
        switch (ch) {
            case '\\': out->append("\\\\"); break;
            case '\t': out->append("\\t");  break;
            case '\n': out->append("\\n");  break;
            case '\r': out->append("\\r");  break;
            default:   out->push_back(ch);  break;
        }
    }
}

bool Unescape(std::string_view s, std::string* out) {
    out->clear();
    out->reserve(s.size());
    for (usize i = 0; i < s.size(); ++i) {
        const char ch = s[i];
        if (ch != '\\') { out->push_back(ch); continue; }
        if (++i >= s.size()) return false;
        switch (s[i]) {
            case '\\': out->push_back('\\'); break;
            case 't':  out->push_back('\t'); break;
            case 'n':  out->push_back('\n'); break;
            case 'r':  out->push_back('\r'); break;
            default:   return false;
        }
    }
    return true;
}

// One output line, built field by field.
class Line {
public:
    explicit Line(const char* kind) { s_.append(kind); }
    Line& Str(std::string_view v) { s_.push_back('\t'); AppendEscaped(&s_, v); return *this; }
    Line& Int(i64 v) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v));
        s_.push_back('\t');
        s_.append(buf);
        return *this;
    }
    Line& Bool(bool v) { return Int(v ? 1 : 0); }
    void To(std::string* out) { out->append(s_); out->push_back('\n'); }

private:
    std::string s_;
};

// One input line, consumed field by field. Any failure sticks, so a record
// parser can read every field and check once at the end.
class Fields {
public:
    explicit Fields(std::string_view line) {
        usize start = 0;
        for (;;) {
            const usize tab = line.find('\t', start);
            if (tab == std::string_view::npos) {
                parts_.push_back(line.substr(start));
                break;
            }
            parts_.push_back(line.substr(start, tab - start));
            start = tab + 1;
        }
    }

    std::string_view Kind() const { return parts_.empty() ? std::string_view{} : parts_[0]; }
    usize Count() const { return parts_.size(); }
    bool  Ok() const { return ok_; }
    // Every field consumed and nothing left over.
    bool  Done() const { return ok_ && next_ == parts_.size(); }
    const std::string& Error() const { return error_; }

    std::string Str() {
        std::string out;
        if (!Take()) return out;
        if (!Unescape(parts_[next_ - 1], &out)) Fail("bad escape sequence");
        return out;
    }

    i64 Int() {
        if (!Take()) return 0;
        const std::string_view v = parts_[next_ - 1];
        if (v.empty()) { Fail("empty number"); return 0; }
        std::string tmp(v);
        char* end = nullptr;
        errno = 0;
        const long long n = std::strtoll(tmp.c_str(), &end, 10);
        if (errno != 0 || !end || *end != '\0') { Fail("not a number"); return 0; }
        return static_cast<i64>(n);
    }

    // Integers that must fit a narrower type are range-checked, not truncated.
    i64 IntIn(i64 lo, i64 hi) {
        const i64 v = Int();
        if (ok_ && (v < lo || v > hi)) Fail("number out of range");
        return v;
    }

    bool Bool() {
        const i64 v = Int();
        if (ok_ && v != 0 && v != 1) Fail("boolean must be 0 or 1");
        return v == 1;
    }

    void Fail(const char* why) {
        if (ok_) { ok_ = false; error_ = why; }
    }

private:
    bool Take() {
        if (!ok_) return false;
        if (next_ >= parts_.size()) { Fail("too few fields"); return false; }
        ++next_;
        return true;
    }

    std::vector<std::string_view> parts_;
    usize next_ = 1;   // parts_[0] is the record kind
    bool  ok_ = true;
    std::string error_;
};

i8  ToI8(i64 v)  { return static_cast<i8>(v); }
i32 ToI32(i64 v) { return static_cast<i32>(v); }
u32 ToU32(i64 v) { return static_cast<u32>(v); }

constexpr i64 kI32Min = -2147483647LL - 1;
constexpr i64 kI32Max = 2147483647LL;
constexpr i64 kU32Max = 4294967295LL;
constexpr i64 kI64Min = -9223372036854775807LL - 1;
constexpr i64 kI64Max = 9223372036854775807LL;

}  // namespace

// ---------------------------------------------------------------------------
// names
// ---------------------------------------------------------------------------

const char* AspirationName(Aspiration a) {
    const usize i = static_cast<usize>(a);
    return i < static_cast<usize>(Aspiration::Count) ? kAspirationNames[i] : "?";
}

Aspiration AspirationFromName(std::string_view name) {
    Aspiration a = Aspiration::Unknown;
    EnumFromName(name, &AspirationName, &a);
    return a;
}

const char* ObjectiveKindName(ObjectiveKind k) {
    const usize i = static_cast<usize>(k);
    return i < static_cast<usize>(ObjectiveKind::Count) ? kObjectiveNames[i] : "?";
}

ObjectiveKind ObjectiveKindFromName(std::string_view name) {
    ObjectiveKind k = ObjectiveKind::None;
    EnumFromName(name, &ObjectiveKindName, &k);
    return k;
}

const char* LoadStatusName(LoadStatus s) {
    const usize i = static_cast<usize>(s);
    return i < static_cast<usize>(LoadStatus::Count) ? kLoadStatusNames[i] : "?";
}

// ---------------------------------------------------------------------------
// the first archetype
// ---------------------------------------------------------------------------

CharacterRecord NewLumberjackSwordsman(const char* name, const char* account,
                                       i64 wallNowMs) {
    CharacterRecord r;
    r.name = name ? name : "";
    r.account = account ? account : "";
    r.aspiration = Aspiration::LumberjackSwordsman;
    r.createdWallMs = wallNowMs;

    // The five skills M4_LIFECYCLE_PLAN.md section 1 names, each to 100.0.
    // That is 500 of the 700 budget. The plan says "toward a 700-point build"
    // and names only these five, so the remaining 200.0 is UNALLOCATED on
    // purpose -- not filled with a guess. It is decided when the character is
    // close enough to 500 for the choice to matter, and on evidence.
    r.targetBuild = {
        {rules::kSwordsmanship, 1000},
        {rules::kTactics,       1000},
        {rules::kLumberjacking, 1000},
        {rules::kHealing,       1000},
        {rules::kAnatomy,       1000},
    };

    // 100 / 100 / 25: the one warrior split that appears TWICE among the ten
    // attested builds in REVOLUTION_RULESET_PROFILE.md section 4 (a warlock
    // thread and a thief thread). Not a swordsman-specific source -- none has
    // been found -- but it is the full-STR, full-DEX shape of the cap, and the
    // plan's own measurement points at DEX for survival.
    r.targetStr = 100;
    r.targetDex = 100;
    r.targetInt = 25;

    // Itemdefs as the shard names them (data/revolution_vendor_policy.tsv),
    // both purchasable under the vendor policy. The hatchet is the job AND a
    // Swordsmanship weapon; bandages are what Healing consumes.
    r.equipment = {
        {"i_hatchet", 0, true},
        {"i_bandage", 1, true},
    };

    // Bot tuning, not Revolution mechanics: enough held back to replace an axe
    // and buy food after a death, the rest banked where death cannot take it.
    r.economy.goldReserve = 100;
    r.economy.bankAboveGold = 500;
    r.economy.unloadAtWeightPercent = 80;
    return r;
}

// ---------------------------------------------------------------------------
// validation
// ---------------------------------------------------------------------------

RecordCheck ValidateRecord(const CharacterRecord& r, const rules::Profile& p) {
    RecordCheck c;
    if (r.name.empty()) { c.why = "character has no name"; return c; }
    if (r.aspiration == Aspiration::Unknown && r.archetype.empty()) {
        c.why = "neither an aspiration nor an archetype";
        return c;
    }

    // The same skill twice would let a build look like 700 while training one
    // skill to 200.
    for (usize i = 0; i < r.targetBuild.size(); ++i)
        for (usize j = i + 1; j < r.targetBuild.size(); ++j)
            if (r.targetBuild[i].skillId == r.targetBuild[j].skillId) {
                c.why = "target build lists skill " +
                        std::to_string(r.targetBuild[i].skillId) + " twice";
                return c;
            }

    const rules::BuildCheck b = rules::ValidateBuild(p, r.targetBuild);
    if (!b.ok) {
        c.why = std::string("target build: ") + rules::ViolationName(b.violation) +
                " (skill " + std::to_string(b.skillId) + ", total " +
                std::to_string(b.totalTenths) + ")";
        return c;
    }

    const i32 stats[3] = {r.targetStr, r.targetDex, r.targetInt};
    const char* names[3] = {"STR", "DEX", "INT"};
    i32 total = 0;
    for (int i = 0; i < 3; ++i) {
        if (stats[i] < 0 || stats[i] > p.perStatCap) {
            c.why = std::string("target ") + names[i] + " " +
                    std::to_string(stats[i]) + " outside 0.." +
                    std::to_string(p.perStatCap);
            return c;
        }
        total += stats[i];
    }
    if (total > p.totalStatCap) {
        c.why = "target stats total " + std::to_string(total) + " over " +
                std::to_string(p.totalStatCap);
        return c;
    }

    if (r.economy.unloadAtWeightPercent <= 0 || r.economy.unloadAtWeightPercent > 100) {
        c.why = "unload weight percent outside 1..100";
        return c;
    }
    if (r.economy.goldReserve < 0 || r.economy.bankAboveGold < 0) {
        c.why = "negative gold goal";
        return c;
    }

    c.ok = true;
    return c;
}

// ---------------------------------------------------------------------------
// clocks
// ---------------------------------------------------------------------------

Clock NowClock(i64 steadyNowMs) {
    Clock c;
    c.steadyMs = steadyNowMs;
    c.wallMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch()).count();
    return c;
}

// ---------------------------------------------------------------------------
// live objects <-> record
// ---------------------------------------------------------------------------

void Capture(CharacterRecord* r, const travel::PersonalKnowledge& k,
             const supply::Registry& reg, const Clock& c) {
    r->suppliers.clear();
    for (supply::Supplier s : reg.All()) {
        s.observedAtMs = c.ToWall(s.observedAtMs);
        s.lastVerifiedMs = c.ToWall(s.lastVerifiedMs);
        r->suppliers.push_back(std::move(s));
    }

    r->visits.clear();
    for (travel::VisitRecord v : k.Visits()) {
        v.lastVisitMs = c.ToWall(v.lastVisitMs);
        r->visits.push_back(std::move(v));
    }

    r->sightings.clear();
    for (travel::ServiceSighting s : k.Sightings()) {
        s.seenMs = c.ToWall(s.seenMs);
        r->sightings.push_back(std::move(s));
    }

    r->runes = k.Runes();

    r->dangers.clear();
    for (travel::DangerNote n : k.Dangers()) {
        if (c.steadyMs >= n.expiresMs) continue;   // already over; not worth a line
        n.expiresMs = c.ToWall(n.expiresMs);
        r->dangers.push_back(std::move(n));
    }

    r->homeSet = k.HasHome();
    if (r->homeSet) {
        k.Home(&r->homeX, &r->homeY, &r->homeZ);
        r->homePlaceId = k.HomePlaceId();
    }

    r->lastDeath = k.LastDeath();
    r->lastDeath.timeMs = c.ToWall(r->lastDeath.timeMs);

    // Merge the last death into the history. Matching is by place and a
    // two-second window, not exact equality: the same death goes through two
    // different clock pairings (load, then save) and may come back a
    // millisecond off.
    const travel::DeathRecord& d = r->lastDeath;
    if (d.valid) {
        DeathEntry* same = nullptr;
        for (DeathEntry& e : r->deaths) {
            const i64 dt = e.wallMs > d.timeMs ? e.wallMs - d.timeMs : d.timeMs - e.wallMs;
            if (dt <= 2000 && e.x == d.x && e.y == d.y) { same = &e; break; }
        }
        if (!same) {
            DeathEntry e;
            e.x = d.x; e.y = d.y; e.z = d.z;
            e.regionId = d.regionId;
            e.wallMs = d.timeMs;
            r->deaths.push_back(std::move(e));
            same = &r->deaths.back();
        }
        same->corpseSerial = d.corpseSerial;
        same->recoveryAttempts = d.recoveryAttempts;
        // A corpse sighting can move the death to the corpse's own tile.
        same->x = d.x; same->y = d.y; same->z = d.z;
        if (r->deaths.size() > kMaxDeaths)
            r->deaths.erase(r->deaths.begin(),
                            r->deaths.begin() +
                                static_cast<long long>(r->deaths.size() - kMaxDeaths));
    }

    r->lastSavedWallMs = c.wallMs;
}

void Apply(const CharacterRecord& r, travel::PersonalKnowledge* k,
           supply::Registry* reg, const Clock& c) {
    k->Clear();
    reg->Clear();

    for (supply::Supplier s : r.suppliers) {
        s.observedAtMs = c.ToSteady(s.observedAtMs);
        s.lastVerifiedMs = c.ToSteady(s.lastVerifiedMs);
        // The vendor policy is CODE, and it may have changed since the save.
        // Re-rule every NPC purchase now, so an old file can never make a
        // vendor usable that the current policy refuses.
        if (s.kind == supply::SupplierKind::NpcVendor) {
            const econ::VendorRuling ruling = econ::CanUseNPCVendorFor(s.what.c_str());
            s.policyClass = ruling.klass;
            s.policyAllows = ruling.allowed;
        }
        reg->Restore(s);
    }

    for (travel::VisitRecord v : r.visits) {
        v.lastVisitMs = c.ToSteady(v.lastVisitMs);
        k->RestoreVisit(v);
    }
    for (travel::ServiceSighting s : r.sightings) {
        s.seenMs = c.ToSteady(s.seenMs);
        k->RestoreSighting(s);
    }
    for (const travel::KnownRune& rune : r.runes) k->NoteRune(rune);

    for (travel::DangerNote n : r.dangers) {
        // Expired while logged out: the forest is not dangerous forever.
        if (n.expiresMs <= c.wallMs) continue;
        n.expiresMs = c.ToSteady(n.expiresMs);
        k->RestoreDanger(n);
    }

    if (r.homeSet)
        k->SetHome(r.homeX, r.homeY, r.homeZ, r.homePlaceId.c_str());

    if (r.lastDeath.valid) {
        travel::DeathRecord d = r.lastDeath;
        d.timeMs = c.ToSteady(d.timeMs);
        k->RestoreDeath(d);
    }
}

void NoteRoute(CharacterRecord* r, const char* destination, bool ok,
               const char* why, i64 wallMs) {
    if (!destination || !*destination) return;
    RouteMemory* m = nullptr;
    for (RouteMemory& e : r->routes)
        if (e.destination == destination) { m = &e; break; }

    if (!m) {
        if (r->routes.size() >= kMaxRoutes) {
            // Forget the destination touched longest ago.
            auto oldest = std::min_element(
                r->routes.begin(), r->routes.end(),
                [](const RouteMemory& a, const RouteMemory& b) {
                    return a.lastWallMs < b.lastWallMs;
                });
            r->routes.erase(oldest);
        }
        RouteMemory e;
        e.destination = destination;
        r->routes.push_back(std::move(e));
        m = &r->routes.back();
    }

    m->lastWallMs = wallMs;
    if (ok) {
        ++m->successes;
        m->consecutiveFailures = 0;
    } else {
        ++m->failures;
        ++m->consecutiveFailures;
        m->lastFailure = why ? why : "";
    }
}

const RouteMemory* FindRoute(const CharacterRecord& r, std::string_view destination) {
    for (const RouteMemory& e : r.routes)
        if (e.destination == destination) return &e;
    return nullptr;
}

// ---------------------------------------------------------------------------
// text format
// ---------------------------------------------------------------------------
//
// One record per line, kind first. Singletons (identity, stats, economy,
// objective, logout, home, last_death) appear at most once; lists repeat. The
// exact column order is the order the writer uses below, and the reader
// mirrors it field for field.

std::string Serialize(const CharacterRecord& r) {
    std::string out;
    Line(kMagic).Int(kFormatVersion).To(&out);
    Line("identity").Str(r.name).Str(r.account).Str(AspirationName(r.aspiration))
        .Int(r.createdWallMs).Int(r.sessions).Int(r.lastLoginWallMs)
        .Int(r.lastSavedWallMs).To(&out);
    if (!r.archetype.empty()) Line("archetype").Str(r.archetype).To(&out);
    Line("stats").Int(r.targetStr).Int(r.targetDex).Int(r.targetInt).To(&out);
    for (const rules::BuildSkill& s : r.targetBuild)
        Line("skill").Int(s.skillId).Int(s.tenths).To(&out);
    for (const EquipmentGoal& e : r.equipment)
        Line("equip").Str(e.itemdef).Int(e.priority).Bool(e.required).To(&out);
    Line("economy").Int(r.economy.goldReserve).Int(r.economy.bankAboveGold)
        .Int(r.economy.unloadAtWeightPercent).To(&out);
    Line("objective").Str(ObjectiveKindName(r.objective.kind)).Str(r.objective.target)
        .Int(r.objective.sinceWallMs).Int(r.objective.attempts).To(&out);

    if (r.lastLogout.valid)
        Line("logout").Int(r.lastLogout.x).Int(r.lastLogout.y).Int(r.lastLogout.z)
            .Str(r.lastLogout.placeId).Bool(r.lastLogout.safe)
            .Int(r.lastLogout.wallMs).To(&out);
    if (r.homeSet)
        Line("home").Int(r.homeX).Int(r.homeY).Int(r.homeZ).Str(r.homePlaceId).To(&out);
    if (r.lastDeath.valid)
        Line("last_death").Int(r.lastDeath.x).Int(r.lastDeath.y).Int(r.lastDeath.z)
            .Str(r.lastDeath.regionId).Int(r.lastDeath.corpseSerial)
            .Int(r.lastDeath.timeMs).Int(r.lastDeath.recoveryAttempts).To(&out);

    for (const DeathEntry& d : r.deaths)
        Line("death").Int(d.x).Int(d.y).Int(d.z).Str(d.regionId)
            .Int(d.corpseSerial).Int(d.recoveryAttempts).Int(d.wallMs).To(&out);
    for (const RouteMemory& m : r.routes)
        Line("route").Str(m.destination).Int(m.successes).Int(m.failures)
            .Int(m.consecutiveFailures).Int(m.lastWallMs).Str(m.lastFailure).To(&out);
    for (const supply::Supplier& s : r.suppliers)
        Line("supplier").Str(supply::SupplierKindName(s.kind)).Int(s.serial)
            .Int(s.x).Int(s.y).Int(s.z).Str(s.name).Str(s.what)
            .Int(s.observedQuantity).Int(s.observedPricePerUnit)
            .Int(s.observedAtMs).Int(s.lastVerifiedMs).Int(s.failures)
            .Bool(s.invalidated).Str(econ::VendorClassName(s.policyClass))
            .Bool(s.policyAllows).To(&out);
    for (const travel::VisitRecord& v : r.visits)
        Line("visit").Str(v.placeId).Int(v.lastVisitMs).Int(v.visits).To(&out);
    for (const travel::ServiceSighting& s : r.sightings)
        Line("sighting").Str(wm::ServiceName(s.service)).Int(s.serial).Str(s.title)
            .Int(s.x).Int(s.y).Int(s.z).Int(s.seenMs).To(&out);
    for (const travel::KnownRune& k : r.runes)
        Line("rune").Int(k.serial).Int(k.graphic).Str(k.name).Bool(k.marked)
            .Int(k.x).Int(k.y).Int(k.z).Bool(k.destinationKnown).To(&out);
    for (const travel::DangerNote& n : r.dangers)
        Line("danger").Int(n.x).Int(n.y).Int(n.radius).Int(n.expiresMs).Str(n.why).To(&out);

    Line("end").To(&out);
    return out;
}

ParseResult Parse(std::string_view text, CharacterRecord* out) {
    ParseResult res;
    CharacterRecord r;
    bool sawHeader = false, sawIdentity = false, sawEnd = false;

    usize pos = 0;
    int lineNo = 0;
    auto fail = [&](const std::string& why) {
        res.ok = false;
        res.line = lineNo;
        res.error = why;
        return res;
    };

    while (pos < text.size()) {
        usize nl = text.find('\n', pos);
        if (nl == std::string_view::npos) nl = text.size();
        std::string_view raw = text.substr(pos, nl - pos);
        pos = nl + 1;
        ++lineNo;
        if (!raw.empty() && raw.back() == '\r') raw.remove_suffix(1);   // CRLF from Windows editors
        if (raw.empty()) continue;

        if (sawEnd) return fail("content after 'end'");

        Fields f(raw);
        const std::string_view kind = f.Kind();

        if (!sawHeader) {
            if (kind != kMagic) return fail("not a character record");
            const i64 v = f.Int();
            if (!f.Done()) return fail("bad header: " + f.Error());
            if (v != kFormatVersion)
                return fail("format version " + std::to_string(v) + " (this build reads " +
                            std::to_string(kFormatVersion) + ")");
            sawHeader = true;
            continue;
        }

        if (kind == "end") {
            sawEnd = true;
        } else if (kind == "identity") {
            if (sawIdentity) return fail("second identity line");
            r.name = f.Str();
            r.account = f.Str();
            const std::string asp = f.Str();
            r.createdWallMs = f.Int();
            r.sessions = ToI32(f.IntIn(0, kI32Max));
            r.lastLoginWallMs = f.Int();
            r.lastSavedWallMs = f.Int();
            if (f.Ok() && !EnumFromName(asp, &AspirationName, &r.aspiration))
                f.Fail("unknown aspiration");
            sawIdentity = true;
        } else if (kind == "archetype") {
            r.archetype = f.Str();
            if (f.Ok() && r.archetype.empty()) f.Fail("empty archetype");
        } else if (kind == "stats") {
            r.targetStr = ToI32(f.IntIn(kI32Min, kI32Max));
            r.targetDex = ToI32(f.IntIn(kI32Min, kI32Max));
            r.targetInt = ToI32(f.IntIn(kI32Min, kI32Max));
        } else if (kind == "skill") {
            rules::BuildSkill s;
            s.skillId = static_cast<int>(f.IntIn(0, 255));
            s.tenths = ToI32(f.IntIn(kI32Min, kI32Max));
            r.targetBuild.push_back(s);
        } else if (kind == "equip") {
            EquipmentGoal e;
            e.itemdef = f.Str();
            e.priority = ToI32(f.IntIn(kI32Min, kI32Max));
            e.required = f.Bool();
            r.equipment.push_back(std::move(e));
        } else if (kind == "economy") {
            r.economy.goldReserve = ToI32(f.IntIn(kI32Min, kI32Max));
            r.economy.bankAboveGold = ToI32(f.IntIn(kI32Min, kI32Max));
            r.economy.unloadAtWeightPercent = ToI32(f.IntIn(kI32Min, kI32Max));
        } else if (kind == "objective") {
            const std::string k = f.Str();
            r.objective.target = f.Str();
            r.objective.sinceWallMs = f.Int();
            r.objective.attempts = ToI32(f.IntIn(0, kI32Max));
            if (f.Ok() && !EnumFromName(k, &ObjectiveKindName, &r.objective.kind))
                f.Fail("unknown objective");
        } else if (kind == "logout") {
            LogoutPoint& l = r.lastLogout;
            l.valid = true;
            l.x = ToI32(f.IntIn(kI32Min, kI32Max));
            l.y = ToI32(f.IntIn(kI32Min, kI32Max));
            l.z = ToI8(f.IntIn(-128, 127));
            l.placeId = f.Str();
            l.safe = f.Bool();
            l.wallMs = f.Int();
        } else if (kind == "home") {
            r.homeSet = true;
            r.homeX = ToI32(f.IntIn(kI32Min, kI32Max));
            r.homeY = ToI32(f.IntIn(kI32Min, kI32Max));
            r.homeZ = ToI8(f.IntIn(-128, 127));
            r.homePlaceId = f.Str();
        } else if (kind == "last_death") {
            travel::DeathRecord& d = r.lastDeath;
            d.valid = true;
            d.x = ToI32(f.IntIn(kI32Min, kI32Max));
            d.y = ToI32(f.IntIn(kI32Min, kI32Max));
            d.z = ToI8(f.IntIn(-128, 127));
            d.regionId = f.Str();
            d.corpseSerial = ToU32(f.IntIn(0, kU32Max));
            d.timeMs = f.Int();
            d.recoveryAttempts = static_cast<int>(f.IntIn(0, kI32Max));
        } else if (kind == "death") {
            DeathEntry d;
            d.x = ToI32(f.IntIn(kI32Min, kI32Max));
            d.y = ToI32(f.IntIn(kI32Min, kI32Max));
            d.z = ToI8(f.IntIn(-128, 127));
            d.regionId = f.Str();
            d.corpseSerial = ToU32(f.IntIn(0, kU32Max));
            d.recoveryAttempts = ToI32(f.IntIn(0, kI32Max));
            d.wallMs = f.Int();
            r.deaths.push_back(std::move(d));
        } else if (kind == "route") {
            RouteMemory m;
            m.destination = f.Str();
            m.successes = ToI32(f.IntIn(0, kI32Max));
            m.failures = ToI32(f.IntIn(0, kI32Max));
            m.consecutiveFailures = ToI32(f.IntIn(0, kI32Max));
            m.lastWallMs = f.Int();
            m.lastFailure = f.Str();
            r.routes.push_back(std::move(m));
        } else if (kind == "supplier") {
            supply::Supplier s;
            const std::string k = f.Str();
            s.serial = ToU32(f.IntIn(0, kU32Max));
            s.x = ToI32(f.IntIn(kI32Min, kI32Max));
            s.y = ToI32(f.IntIn(kI32Min, kI32Max));
            s.z = ToI8(f.IntIn(-128, 127));
            s.name = f.Str();
            s.what = f.Str();
            s.observedQuantity = ToI32(f.IntIn(kI32Min, kI32Max));
            s.observedPricePerUnit = ToI32(f.IntIn(kI32Min, kI32Max));
            s.observedAtMs = f.IntIn(kI64Min, kI64Max);
            s.lastVerifiedMs = f.IntIn(kI64Min, kI64Max);
            s.failures = ToI32(f.IntIn(0, kI32Max));
            s.invalidated = f.Bool();
            const std::string pc = f.Str();
            s.policyAllows = f.Bool();
            if (f.Ok() && !EnumFromName(k, &supply::SupplierKindName, &s.kind))
                f.Fail("unknown supplier kind");
            if (f.Ok() && !EnumFromName(pc, &econ::VendorClassName, &s.policyClass))
                f.Fail("unknown vendor class");
            r.suppliers.push_back(std::move(s));
        } else if (kind == "visit") {
            travel::VisitRecord v;
            v.placeId = f.Str();
            v.lastVisitMs = f.Int();
            v.visits = static_cast<int>(f.IntIn(0, kI32Max));
            r.visits.push_back(std::move(v));
        } else if (kind == "sighting") {
            travel::ServiceSighting s;
            const std::string svc = f.Str();
            s.serial = ToU32(f.IntIn(0, kU32Max));
            s.title = f.Str();
            s.x = ToI32(f.IntIn(kI32Min, kI32Max));
            s.y = ToI32(f.IntIn(kI32Min, kI32Max));
            s.z = ToI8(f.IntIn(-128, 127));
            s.seenMs = f.Int();
            if (f.Ok()) {
                s.service = wm::ServiceFromName(svc.c_str());
                if (s.service == wm::Service::None) f.Fail("unknown service");
            }
            r.sightings.push_back(std::move(s));
        } else if (kind == "rune") {
            travel::KnownRune k;
            k.serial = ToU32(f.IntIn(0, kU32Max));
            k.graphic = static_cast<u16>(f.IntIn(0, 65535));
            k.name = f.Str();
            k.marked = f.Bool();
            k.x = ToI32(f.IntIn(kI32Min, kI32Max));
            k.y = ToI32(f.IntIn(kI32Min, kI32Max));
            k.z = ToI8(f.IntIn(-128, 127));
            k.destinationKnown = f.Bool();
            r.runes.push_back(std::move(k));
        } else if (kind == "danger") {
            travel::DangerNote n;
            n.x = ToI32(f.IntIn(kI32Min, kI32Max));
            n.y = ToI32(f.IntIn(kI32Min, kI32Max));
            n.radius = ToI32(f.IntIn(0, kI32Max));
            n.expiresMs = f.Int();
            n.why = f.Str();
            r.dangers.push_back(std::move(n));
        } else {
            // A kind a newer build wrote. Skipping it keeps an older build
            // able to read the life, at the cost of dropping that one detail.
            ++res.unknownLines;
            continue;
        }

        if (!f.Done()) {
            return fail(std::string(kind) + ": " +
                        (f.Ok() ? std::string("too many fields") : f.Error()));
        }
    }

    if (!sawHeader) return fail("empty file");
    if (!sawIdentity) return fail("no identity line");
    // No `end` means the writer never finished. A shorter life is not a life.
    if (!sawEnd) return fail("truncated: no 'end' line");

    *out = std::move(r);
    res.ok = true;
    return res;
}

// ---------------------------------------------------------------------------
// files
// ---------------------------------------------------------------------------

LoadResult LoadFile(const std::string& path, CharacterRecord* out) {
    LoadResult res;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        if (ec) {
            // Could not even tell. Treat as corrupt: the one unsafe answer
            // would be "no life here", which invites an overwrite.
            res.status = LoadStatus::Corrupt;
            res.detail = "cannot stat: " + ec.message();
        } else {
            res.status = LoadStatus::NotFound;
        }
        return res;
    }

    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        res.status = LoadStatus::Corrupt;
        res.detail = std::string("cannot open: ") + std::strerror(errno);
        return res;
    }
    std::string text;
    char buf[4096];
    for (;;) {
        const usize n = std::fread(buf, 1, sizeof(buf), f);
        if (n) text.append(buf, n);
        if (n < sizeof(buf)) break;
    }
    const bool readErr = std::ferror(f) != 0;
    std::fclose(f);
    if (readErr) {
        res.status = LoadStatus::Corrupt;
        res.detail = "read error";
        return res;
    }

    CharacterRecord r;
    const ParseResult p = Parse(text, &r);
    if (!p.ok) {
        res.status = LoadStatus::Corrupt;
        res.detail = "line " + std::to_string(p.line) + ": " + p.error;
        return res;
    }
    const RecordCheck c = ValidateRecord(r);
    if (!c.ok) {
        res.status = LoadStatus::Invalid;
        res.detail = c.why;
        return res;
    }
    *out = std::move(r);
    res.status = LoadStatus::Ok;
    if (p.unknownLines)
        res.detail = std::to_string(p.unknownLines) + " unknown line(s) skipped";
    return res;
}

bool SaveFile(const std::string& path, const CharacterRecord& r, std::string* err) {
    const std::string text = Serialize(r);
    const std::string tmp = path + ".tmp";

    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) {
        if (err) *err = "cannot create " + tmp + ": " + std::strerror(errno);
        return false;
    }
    const usize wrote = std::fwrite(text.data(), 1, text.size(), f);
    const bool flushed = std::fflush(f) == 0;
    const bool closed = std::fclose(f) == 0;
    if (wrote != text.size() || !flushed || !closed) {
        if (err) *err = "short write to " + tmp;
        std::error_code ignore;
        std::filesystem::remove(tmp, ignore);
        return false;
    }

    // rename() replaces the destination on both POSIX and the MSVC STL, so the
    // old life is swapped for the new one in a single step.
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        if (err) *err = "cannot replace " + path + ": " + ec.message();
        std::error_code ignore;
        std::filesystem::remove(tmp, ignore);
        return false;
    }
    return true;
}

std::string RecordFileName(std::string_view characterName) {
    std::string out;
    for (char ch : characterName) {
        const bool keep = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                          (ch >= '0' && ch <= '9');
        out.push_back(keep ? ch : '_');
    }
    if (out.empty()) out = "unnamed";
    out += ".life";
    return out;
}

// ---------------------------------------------------------------------------
// archetypes (M4.5)
// ---------------------------------------------------------------------------

const ArchetypeRow* ArchetypeTable::Find(std::string_view id) const {
    for (const ArchetypeRow& r : rows)
        if (r.id == id) return &r;
    return nullptr;
}

bool ParseArchetypes(std::string_view text, ArchetypeTable* out, std::string* err) {
    ArchetypeTable t;
    usize pos = 0;
    int lineNo = 0;
    auto fail = [&](const std::string& why) {
        if (err) *err = "line " + std::to_string(lineNo) + ": " + why;
        return false;
    };
    while (pos < text.size()) {
        usize nl = text.find('\n', pos);
        if (nl == std::string_view::npos) nl = text.size();
        std::string_view raw = text.substr(pos, nl - pos);
        pos = nl + 1;
        ++lineNo;
        if (!raw.empty() && raw.back() == '\r') raw.remove_suffix(1);
        if (raw.empty()) continue;
        if (lineNo == 1) {
            if (raw.substr(0, 3) != "id\t") return fail("missing header");
            continue;
        }
        // Fields treats the first column as the record kind; here it is the id.
        Fields f(raw);
        ArchetypeRow r;
        r.id = std::string(f.Kind());
        r.kind = f.Str();
        r.style = f.Str();
        r.ref = f.Str();
        r.evidence = f.Str();
        const std::string skills = f.Str();
        r.str = ToI32(f.IntIn(0, 100));
        r.dex = ToI32(f.IntIn(0, 100));
        r.intel = ToI32(f.IntIn(0, 100));
        r.home = f.Str();
        if (!f.Done()) return fail(r.id + ": " + (f.Ok() ? std::string("too many fields") : f.Error()));
        if (r.id.empty()) return fail("empty id");
        if (r.kind != "fighter" && r.kind != "crafter" && r.kind != "gatherer")
            return fail(r.id + ": unknown kind " + r.kind);

        usize s = 0;
        while (s < skills.size()) {
            usize comma = skills.find(',', s);
            if (comma == std::string::npos) comma = skills.size();
            const std::string item = skills.substr(s, comma - s);
            s = comma + 1;
            const usize colon = item.find(':');
            if (colon == std::string::npos) return fail(r.id + ": bad skill '" + item + "'");
            char* end = nullptr;
            const long id = std::strtol(item.c_str(), &end, 10);
            const long tenths = std::strtol(item.c_str() + colon + 1, &end, 10);
            if (id < 0 || id > 57 || tenths < 0 || tenths > 1000) return fail(r.id + ": skill out of range");
            r.skills.push_back({static_cast<int>(id), static_cast<i32>(tenths)});
        }

        // Every template must obey the profile, or nothing loads.
        CharacterRecord probe = NewRecordFromArchetype(r, "probe", "probe", 1);
        const RecordCheck c = ValidateRecord(probe);
        if (!c.ok) return fail(r.id + ": " + c.why);
        if (t.Find(r.id)) return fail(r.id + ": duplicate id");
        t.rows.push_back(std::move(r));
    }
    if (t.rows.empty()) return fail("no archetypes");
    *out = std::move(t);
    return true;
}

bool LoadArchetypes(const std::string& path, ArchetypeTable* out, std::string* err) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        if (err) *err = "cannot open " + path + ": " + std::strerror(errno);
        return false;
    }
    std::string text;
    char buf[4096];
    for (;;) {
        const usize n = std::fread(buf, 1, sizeof(buf), f);
        if (n) text.append(buf, n);
        if (n < sizeof(buf)) break;
    }
    std::fclose(f);
    return ParseArchetypes(text, out, err);
}

CharacterRecord NewRecordFromArchetype(const ArchetypeRow& row, const char* name,
                                       const char* account, i64 wallNowMs) {
    CharacterRecord r;
    r.name = name ? name : "";
    r.account = account ? account : "";
    r.archetype = row.id;
    r.aspiration = row.id == "lumberjack" ? Aspiration::LumberjackSwordsman : Aspiration::Unknown;
    r.createdWallMs = wallNowMs;
    r.targetBuild = row.skills;
    r.targetStr = row.str;
    r.targetDex = row.dex;
    r.targetInt = row.intel;
    // Bandages for anyone who trains Healing; everything else the bot scripts
    // decide from the same table (tools per craft, ammo per style).
    for (const rules::BuildSkill& s : row.skills)
        if (s.skillId == rules::kHealing) r.equipment.push_back({"i_bandage", 1, true});
    r.economy.goldReserve = row.kind == "crafter" ? 150 : 100;
    r.economy.bankAboveGold = row.kind == "crafter" ? 1500 : 800;
    r.economy.unloadAtWeightPercent = 80;
    return r;
}

}  // namespace uo::life
