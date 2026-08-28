#include "uo/life.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#if defined(_WIN32)
#  include <direct.h>
#  define UO_MKDIR(p) _mkdir(p)
#else
#  include <sys/stat.h>
#  include <sys/types.h>
#  define UO_MKDIR(p) mkdir(p, 0775)
#endif

namespace uo::life {

namespace {

json::Value SkillsToJson(const std::vector<SkillTarget>& skills) {
    json::Value arr = json::Value::MakeArray();
    for (const SkillTarget& s : skills) {
        json::Value o = json::Value::MakeObject();
        o.Set("skill", static_cast<i64>(s.skillId));
        o.Set("tenths", static_cast<i64>(s.tenths));
        arr.Push(std::move(o));
    }
    return arr;
}

std::vector<SkillTarget> SkillsFromJson(const json::Value& v) {
    std::vector<SkillTarget> out;
    for (usize i = 0; i < v.Size(); ++i) {
        const json::Value& e = v.At(i);
        SkillTarget s;
        s.skillId = static_cast<int>(e["skill"].AsInt(0));
        s.tenths  = static_cast<i32>(e["tenths"].AsInt(0));
        out.push_back(s);
    }
    return out;
}

bool EnsureDir(const std::string& path) {
    if (path.empty()) return false;
    // Create every missing component, so a fresh checkout with no bot_data/
    // still saves on the first tick instead of failing quietly.
    std::string acc;
    for (usize i = 0; i <= path.size(); ++i) {
        const bool end = (i == path.size());
        const char c = end ? '\0' : path[i];
        if (end || c == '/' || c == '\\') {
            if (!acc.empty() && acc != "." && acc != "..") {
                // A drive prefix like "C:" is not a directory to create.
                const bool driveOnly = acc.size() == 2 && acc[1] == ':';
                if (!driveOnly) UO_MKDIR(acc.c_str());
            }
            if (!end) acc.push_back('/');
            continue;
        }
        acc.push_back(c);
    }
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Serialisation. Every reader defaults what is absent, so a state.json
// written by an older build loads rather than being discarded.
// ---------------------------------------------------------------------------

json::Value ToJson(const PersistentState& st) {
    json::Value root = json::Value::MakeObject();
    root.Set("schema_version", static_cast<i64>(st.schemaVersion));

    {
        json::Value id = json::Value::MakeObject();
        id.Set("identity_id", st.identity.identityId);
        id.Set("account_name", st.identity.accountName);
        id.Set("character_name", st.identity.characterName);
        id.Set("created_at_ms", st.identity.createdAtMs);
        id.Set("first_seen_at_ms", st.identity.firstSeenAtMs);
        id.Set("last_seen_at_ms", st.identity.lastSeenAtMs);
        id.Set("total_play_time_ms", st.identity.totalPlayTimeMs);
        id.Set("sessions", static_cast<i64>(st.identity.sessions));
        root.Set("identity", std::move(id));
    }

    {
        json::Value p = json::Value::MakeObject();
        p.Set("family", st.plan.family);
        p.Set("skills", SkillsToJson(st.plan.skills));
        p.Set("unresolved_tenths", static_cast<i64>(st.plan.unresolvedTenths));
        p.Set("target_str", static_cast<i64>(st.plan.targetStr));
        p.Set("target_dex", static_cast<i64>(st.plan.targetDex));
        p.Set("target_int", static_cast<i64>(st.plan.targetInt));
        p.Set("create_str", static_cast<i64>(st.plan.createStr));
        p.Set("create_dex", static_cast<i64>(st.plan.createDex));
        p.Set("create_int", static_cast<i64>(st.plan.createInt));
        p.Set("create_skills", SkillsToJson(st.plan.createSkills));
        root.Set("build_plan", std::move(p));
    }

    {
        json::Value m = json::Value::MakeObject();

        json::Value places = json::Value::MakeArray();
        for (const KnownPlace& k : st.memory.Places()) {
            json::Value o = json::Value::MakeObject();
            o.Set("kind", k.kind);
            o.Set("name", k.name);
            o.Set("x", static_cast<i64>(k.x));
            o.Set("y", static_cast<i64>(k.y));
            o.Set("z", static_cast<i64>(k.z));
            o.Set("learned_ms", k.learnedMs);
            o.Set("last_verified_ms", k.lastVerifiedMs);
            o.Set("visits", static_cast<i64>(k.visits));
            places.Push(std::move(o));
        }
        m.Set("places", std::move(places));

        json::Value res = json::Value::MakeArray();
        for (const KnownResourceSource& k : st.memory.Resources()) {
            json::Value o = json::Value::MakeObject();
            o.Set("resource", k.resource);
            o.Set("x", static_cast<i64>(k.x));
            o.Set("y", static_cast<i64>(k.y));
            o.Set("z", static_cast<i64>(k.z));
            o.Set("last_success_ms", k.lastSuccessMs);
            o.Set("last_seen_ms", k.lastSeenMs);
            o.Set("successes", static_cast<i64>(k.successes));
            o.Set("failures", static_cast<i64>(k.failures));
            o.Set("hinted", k.hinted);
            o.Set("label", k.label);
            res.Push(std::move(o));
        }
        m.Set("resources", std::move(res));

        json::Value sup = json::Value::MakeArray();
        for (const KnownSupplier& k : st.memory.Suppliers()) {
            json::Value o = json::Value::MakeObject();
            o.Set("need", k.need);
            o.Set("name", k.name);
            o.Set("source_type", k.sourceType);
            // The serial is stored so a return visit can be recognised, and
            // it is REVALIDATED on use -- never trusted. A serial is a fact
            // about a past observation, not a handle.
            o.Set("serial", static_cast<i64>(k.serial));
            o.Set("x", static_cast<i64>(k.x));
            o.Set("y", static_cast<i64>(k.y));
            o.Set("z", static_cast<i64>(k.z));
            o.Set("observed_quantity", static_cast<i64>(k.observedQuantity));
            o.Set("observed_price", static_cast<i64>(k.observedPricePerUnit));
            o.Set("last_verified_ms", k.lastVerifiedMs);
            o.Set("policy_allows", k.policyAllows);
            sup.Push(std::move(o));
        }
        m.Set("suppliers", std::move(sup));

        json::Value dgr = json::Value::MakeArray();
        for (const DangerMemory& k : st.memory.Dangers()) {
            json::Value o = json::Value::MakeObject();
            o.Set("x", static_cast<i64>(k.x));
            o.Set("y", static_cast<i64>(k.y));
            o.Set("radius", static_cast<i64>(k.radius));
            o.Set("threat", k.threat);
            o.Set("heat", k.heat);
            o.Set("at_ms", k.atMs);
            dgr.Push(std::move(o));
        }
        m.Set("danger", std::move(dgr));

        json::Value evs = json::Value::MakeArray();
        for (const LifeEvent& k : st.memory.Events()) {
            json::Value o = json::Value::MakeObject();
            o.Set("kind", k.kind);
            o.Set("detail", k.detail);
            o.Set("place", k.place);
            o.Set("x", static_cast<i64>(k.x));
            o.Set("y", static_cast<i64>(k.y));
            o.Set("at_ms", k.atMs);
            evs.Push(std::move(o));
        }
        m.Set("events", std::move(evs));

        root.Set("memory", std::move(m));
    }

    {
        json::Value g = json::Value::MakeObject();
        g.Set("kind", GoalKindName(st.goal.kind));
        g.Set("active", st.goal.active);
        g.Set("started_at_ms", st.goal.startedAtMs);
        g.Set("attempts", static_cast<i64>(st.goal.attempts));
        g.Set("progress", static_cast<i64>(st.goal.progress));
        g.Set("failure_reason", st.goal.failureReason);
        root.Set("goal", std::move(g));
    }

    {
        // Named so nobody mistakes it for an authority: these are the last
        // figures the SERVER gave us, kept only to print a reconciliation
        // diff on the next login.
        json::Value s = json::Value::MakeObject();
        s.Set("gold", static_cast<i64>(st.lastKnownGold));
        s.Set("str", static_cast<i64>(st.lastKnownStr));
        s.Set("dex", static_cast<i64>(st.lastKnownDex));
        s.Set("int", static_cast<i64>(st.lastKnownInt));
        s.Set("skills", SkillsToJson(st.lastKnownSkills));
        s.Set("x", static_cast<i64>(st.lastKnownX));
        s.Set("y", static_cast<i64>(st.lastKnownY));
        s.Set("dead", st.lastKnownDead);
        root.Set("last_server_report", std::move(s));
    }

    root.Set("checkpoint_ms", st.checkpointMs);
    root.Set("death_count", static_cast<i64>(st.deathCount));
    root.Set("recent_deaths", static_cast<i64>(st.recentDeaths));
    root.Set("last_death_ms", st.lastDeathMs);

    json::Value sessions = json::Value::MakeArray();
    for (const SessionSummary& s : st.sessions) {
        json::Value o = json::Value::MakeObject();
        o.Set("started_ms", s.startedMs);
        o.Set("ended_ms", s.endedMs);
        o.Set("goals_attempted", static_cast<i64>(s.goalsAttempted));
        o.Set("goals_completed", static_cast<i64>(s.goalsCompleted));
        o.Set("goals_failed", static_cast<i64>(s.goalsFailed));
        o.Set("gold_start", static_cast<i64>(s.goldStart));
        o.Set("gold_end", static_cast<i64>(s.goldEnd));
        o.Set("skill_tenths_start", static_cast<i64>(s.skillTenthsStart));
        o.Set("skill_tenths_end", static_cast<i64>(s.skillTenthsEnd));
        o.Set("logs_gathered", static_cast<i64>(s.logsGathered));
        o.Set("deaths", static_cast<i64>(s.deaths));
        o.Set("places_learned", static_cast<i64>(s.placesLearned));
        o.Set("suppliers_learned", static_cast<i64>(s.suppliersLearned));
        o.Set("clean_logout", s.cleanLogout);
        sessions.Push(std::move(o));
    }
    root.Set("sessions", std::move(sessions));

    return root;
}

bool FromJson(const json::Value& v, PersistentState* out, std::string* err) {
    if (!out) return false;
    if (!v.isObject()) {
        if (err) *err = "state root is not a JSON object";
        return false;
    }

    const i64 version = v["schema_version"].AsInt(0);
    if (version <= 0) {
        if (err) *err = "state has no schema_version";
        return false;
    }
    if (version > kSchemaVersion) {
        // Refusing forward is deliberate. Silently loading a newer file with
        // this build's reader would drop fields it does not know about and
        // then write them away on the next save.
        if (err) {
            char buf[128];
            std::snprintf(buf, sizeof(buf),
                          "state schema_version %lld is newer than this build's %d",
                          static_cast<long long>(version), kSchemaVersion);
            *err = buf;
        }
        return false;
    }

    PersistentState st;
    st.schemaVersion = static_cast<int>(version);

    const json::Value& id = v["identity"];
    st.identity.identityId      = id["identity_id"].AsString();
    st.identity.accountName     = id["account_name"].AsString();
    st.identity.characterName   = id["character_name"].AsString();
    st.identity.createdAtMs     = id["created_at_ms"].AsInt(0);
    st.identity.firstSeenAtMs   = id["first_seen_at_ms"].AsInt(0);
    st.identity.lastSeenAtMs    = id["last_seen_at_ms"].AsInt(0);
    st.identity.totalPlayTimeMs = id["total_play_time_ms"].AsInt(0);
    st.identity.sessions        = static_cast<i32>(id["sessions"].AsInt(0));

    const json::Value& p = v["build_plan"];
    st.plan.family            = p["family"].AsString();
    st.plan.skills            = SkillsFromJson(p["skills"]);
    st.plan.unresolvedTenths  = static_cast<i32>(p["unresolved_tenths"].AsInt(0));
    st.plan.targetStr         = static_cast<i32>(p["target_str"].AsInt(0));
    st.plan.targetDex         = static_cast<i32>(p["target_dex"].AsInt(0));
    st.plan.targetInt         = static_cast<i32>(p["target_int"].AsInt(0));
    st.plan.createStr         = static_cast<i32>(p["create_str"].AsInt(0));
    st.plan.createDex         = static_cast<i32>(p["create_dex"].AsInt(0));
    st.plan.createInt         = static_cast<i32>(p["create_int"].AsInt(0));
    st.plan.createSkills      = SkillsFromJson(p["create_skills"]);

    const json::Value& m = v["memory"];
    {
        const json::Value& a = m["places"];
        for (usize i = 0; i < a.Size(); ++i) {
            const json::Value& e = a.At(i);
            KnownPlace k;
            k.kind = e["kind"].AsString();
            k.name = e["name"].AsString();
            k.x = static_cast<i32>(e["x"].AsInt(0));
            k.y = static_cast<i32>(e["y"].AsInt(0));
            k.z = static_cast<i8>(e["z"].AsInt(0));
            k.learnedMs = e["learned_ms"].AsInt(0);
            k.lastVerifiedMs = e["last_verified_ms"].AsInt(0);
            k.visits = static_cast<i32>(e["visits"].AsInt(0));
            st.memory.MutablePlaces().push_back(std::move(k));
        }
    }
    {
        const json::Value& a = m["resources"];
        for (usize i = 0; i < a.Size(); ++i) {
            const json::Value& e = a.At(i);
            KnownResourceSource k;
            k.resource = e["resource"].AsString();
            k.x = static_cast<i32>(e["x"].AsInt(0));
            k.y = static_cast<i32>(e["y"].AsInt(0));
            k.z = static_cast<i8>(e["z"].AsInt(0));
            k.lastSuccessMs = e["last_success_ms"].AsInt(0);
            k.lastSeenMs = e["last_seen_ms"].AsInt(0);
            k.successes = static_cast<i32>(e["successes"].AsInt(0));
            k.failures = static_cast<i32>(e["failures"].AsInt(0));
            // Absent in v1, and false is the right default there: everything a
            // v1 file holds was written from observation, never seeded.
            k.hinted = e["hinted"].AsBool(false);
            k.label = e["label"].AsString();
            st.memory.MutableResources().push_back(std::move(k));
        }
    }
    {
        const json::Value& a = m["suppliers"];
        for (usize i = 0; i < a.Size(); ++i) {
            const json::Value& e = a.At(i);
            KnownSupplier k;
            k.need = e["need"].AsString();
            k.name = e["name"].AsString();
            k.sourceType = e["source_type"].AsString();
            k.serial = static_cast<u32>(e["serial"].AsInt(0));
            k.x = static_cast<i32>(e["x"].AsInt(0));
            k.y = static_cast<i32>(e["y"].AsInt(0));
            k.z = static_cast<i8>(e["z"].AsInt(0));
            k.observedQuantity = static_cast<i32>(e["observed_quantity"].AsInt(0));
            k.observedPricePerUnit = static_cast<i32>(e["observed_price"].AsInt(0));
            k.lastVerifiedMs = e["last_verified_ms"].AsInt(0);
            k.policyAllows = e["policy_allows"].AsBool(false);
            st.memory.MutableSuppliers().push_back(std::move(k));
        }
    }
    {
        const json::Value& a = m["danger"];
        for (usize i = 0; i < a.Size(); ++i) {
            const json::Value& e = a.At(i);
            DangerMemory k;
            k.x = static_cast<i32>(e["x"].AsInt(0));
            k.y = static_cast<i32>(e["y"].AsInt(0));
            k.radius = static_cast<i32>(e["radius"].AsInt(0));
            k.threat = e["threat"].AsString();
            // Clamp on LOAD as well as on write. The cap was added after a
            // live character had already accumulated 499.89 at one spot, and a
            // cap that only applies to new notes would leave that character
            // permanently afraid of its own forest.
            k.heat = std::min(kMaxDangerHeat, e["heat"].AsDouble(0.0));
            k.atMs = e["at_ms"].AsInt(0);
            st.memory.MutableDangers().push_back(std::move(k));
        }
    }
    {
        const json::Value& a = m["events"];
        for (usize i = 0; i < a.Size(); ++i) {
            const json::Value& e = a.At(i);
            LifeEvent k;
            k.kind = e["kind"].AsString();
            k.detail = e["detail"].AsString();
            k.place = e["place"].AsString();
            k.x = static_cast<i32>(e["x"].AsInt(0));
            k.y = static_cast<i32>(e["y"].AsInt(0));
            k.atMs = e["at_ms"].AsInt(0);
            st.memory.MutableEvents().push_back(std::move(k));
        }
    }

    // --- v1 -> v2 migration ------------------------------------------------
    //
    // v1 wrote a "resource source" for any tick with trees in view, so a v1
    // file holds dozens of stands that never yielded anything -- and the
    // planner preferred them over asking the atlas, which is how one character
    // spent four sessions working scrub 210 tiles short of the real woods.
    //
    // A stand that never produced is not knowledge. Drop it, and let the
    // character re-earn or re-seed. Anything with a success is kept: that WAS
    // earned, and it is the only part of a v1 resource list worth carrying.
    if (version < 2) {
        std::vector<KnownResourceSource>& res = st.memory.MutableResources();
        const usize before = res.size();
        res.erase(std::remove_if(res.begin(), res.end(),
                                 [](const KnownResourceSource& r) {
                                     return !r.hinted && r.successes <= 0;
                                 }),
                  res.end());
        if (before != res.size()) {
            std::printf("[life] migrated v1 -> v2: dropped %llu unproven resource "
                        "record(s), kept %llu that actually yielded\n",
                        static_cast<unsigned long long>(before - res.size()),
                        static_cast<unsigned long long>(res.size()));
        }
    }

    {
        const json::Value& g = v["goal"];
        const std::string kindName = g["kind"].AsString();
        st.goal.kind = GoalKind::IdleBriefly;
        for (int i = 0; i < static_cast<int>(GoalKind::Count); ++i) {
            if (kindName == GoalKindName(static_cast<GoalKind>(i))) {
                st.goal.kind = static_cast<GoalKind>(i);
                break;
            }
        }
        st.goal.active = g["active"].AsBool(false);
        st.goal.startedAtMs = g["started_at_ms"].AsInt(0);
        st.goal.attempts = static_cast<i32>(g["attempts"].AsInt(0));
        st.goal.progress = static_cast<i32>(g["progress"].AsInt(0));
        st.goal.failureReason = g["failure_reason"].AsString();
    }

    {
        const json::Value& s = v["last_server_report"];
        st.lastKnownGold   = static_cast<i32>(s["gold"].AsInt(0));
        st.lastKnownStr    = static_cast<i32>(s["str"].AsInt(0));
        st.lastKnownDex    = static_cast<i32>(s["dex"].AsInt(0));
        st.lastKnownInt    = static_cast<i32>(s["int"].AsInt(0));
        st.lastKnownSkills = SkillsFromJson(s["skills"]);
        st.lastKnownX      = static_cast<i32>(s["x"].AsInt(0));
        st.lastKnownY      = static_cast<i32>(s["y"].AsInt(0));
        st.lastKnownDead   = s["dead"].AsBool(false);
    }

    st.checkpointMs = v["checkpoint_ms"].AsInt(0);
    st.deathCount   = static_cast<i32>(v["death_count"].AsInt(0));
    st.recentDeaths = static_cast<i32>(v["recent_deaths"].AsInt(0));
    st.lastDeathMs  = v["last_death_ms"].AsInt(0);

    {
        const json::Value& a = v["sessions"];
        for (usize i = 0; i < a.Size(); ++i) {
            const json::Value& e = a.At(i);
            SessionSummary s;
            s.startedMs = e["started_ms"].AsInt(0);
            s.endedMs   = e["ended_ms"].AsInt(0);
            s.goalsAttempted = static_cast<i32>(e["goals_attempted"].AsInt(0));
            s.goalsCompleted = static_cast<i32>(e["goals_completed"].AsInt(0));
            s.goalsFailed    = static_cast<i32>(e["goals_failed"].AsInt(0));
            s.goldStart = static_cast<i32>(e["gold_start"].AsInt(0));
            s.goldEnd   = static_cast<i32>(e["gold_end"].AsInt(0));
            s.skillTenthsStart = static_cast<i32>(e["skill_tenths_start"].AsInt(0));
            s.skillTenthsEnd   = static_cast<i32>(e["skill_tenths_end"].AsInt(0));
            s.logsGathered = static_cast<i32>(e["logs_gathered"].AsInt(0));
            s.deaths = static_cast<i32>(e["deaths"].AsInt(0));
            s.placesLearned = static_cast<i32>(e["places_learned"].AsInt(0));
            s.suppliersLearned = static_cast<i32>(e["suppliers_learned"].AsInt(0));
            s.cleanLogout = e["clean_logout"].AsBool(false);
            st.sessions.push_back(s);
        }
        if (st.sessions.size() > kMaxSessions) {
            st.sessions.erase(
                st.sessions.begin(),
                st.sessions.begin() +
                    static_cast<long>(st.sessions.size() - kMaxSessions));
        }
    }

    *out = std::move(st);
    return true;
}

// ---------------------------------------------------------------------------
// Store
// ---------------------------------------------------------------------------

std::string Store::DirFor(const std::string& identityId) const {
    std::string d = root_;
    if (!d.empty() && d.back() != '/' && d.back() != '\\') d.push_back('/');
    d += identityId;
    return d;
}

std::string Store::PathFor(const std::string& identityId) const {
    return DirFor(identityId) + "/state.json";
}

bool Store::Save(const PersistentState& st, std::string* err) const {
    if (st.identity.identityId.empty()) {
        if (err) *err = "cannot save: identity_id is empty";
        return false;
    }
    EnsureDir(DirFor(st.identity.identityId));
    const std::string text = ToJson(st).Serialize(2);
    if (!json::WriteFileAtomic(PathFor(st.identity.identityId).c_str(), text)) {
        if (err) *err = "could not write " + PathFor(st.identity.identityId);
        return false;
    }
    return true;
}

bool Store::Exists(const std::string& identityId) const {
    std::string text;
    return json::ReadFile(PathFor(identityId).c_str(), &text);
}

bool Store::Load(const std::string& identityId, PersistentState* out,
                 std::string* err) const {
    if (!out) return false;
    std::string text;
    if (!json::ReadFile(PathFor(identityId).c_str(), &text)) {
        // Not an error: a character that has never played has no state.
        if (err) err->clear();
        return false;
    }
    json::ParseError perr;
    const json::Value v = json::Parse(text, &perr);
    if (perr.failed) {
        if (err) {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "state.json parse error at byte %llu: %s",
                          static_cast<unsigned long long>(perr.offset),
                          perr.message.c_str());
            *err = buf;
        }
        return false;
    }
    return FromJson(v, out, err);
}

// ---------------------------------------------------------------------------
// Login reconciliation. The server wins every field it owns.
// ---------------------------------------------------------------------------

namespace {

std::string I(i64 v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v));
    return buf;
}

std::string Tenths(i32 v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", v / 10.0);
    return buf;
}

}  // namespace

ReconcileReport Reconcile(PersistentState* st, const Observation& obs) {
    ReconcileReport rep;
    if (!st) return rep;

    rep.firstEverLogin = (st->identity.firstSeenAtMs == 0);

    auto line = [&rep](const char* field, std::string persisted, std::string server,
                       const char* result) {
        ReconcileLine l;
        l.field = field;
        l.persisted = std::move(persisted);
        l.server = std::move(server);
        l.result = result;
        if (l.persisted != l.server) rep.driftFields++;
        rep.lines.push_back(std::move(l));
    };

    // --- gold --------------------------------------------------------------
    line("gold", I(st->lastKnownGold), I(obs.gold), "server value accepted");
    st->lastKnownGold = obs.gold;

    // --- stats -------------------------------------------------------------
    line("str", I(st->lastKnownStr), I(obs.str), "server value accepted");
    line("dex", I(st->lastKnownDex), I(obs.dex), "server value accepted");
    line("int", I(st->lastKnownInt), I(obs.intel), "server value accepted");
    st->lastKnownStr = obs.str;
    st->lastKnownDex = obs.dex;
    st->lastKnownInt = obs.intel;

    // --- skills ------------------------------------------------------------
    //
    // Every skill the plan targets is reported, whether or not it moved, so
    // the log answers "did this session progress" without arithmetic.
    for (const SkillTarget& t : st->plan.skills) {
        i32 persisted = 0;
        for (const SkillTarget& s : st->lastKnownSkills) {
            if (s.skillId == t.skillId) { persisted = s.tenths; break; }
        }
        const i32 server = obs.SkillTenths(t.skillId);
        // Never lower server progression to match an older save. The server
        // is the only thing that grants a skill point, so a save that is
        // BEHIND is simply out of date -- that is the normal case after an
        // unclean shutdown, and it must not roll anything back.
        char field[32];
        std::snprintf(field, sizeof(field), "skill_%d", t.skillId);
        line(field, Tenths(persisted), Tenths(server),
             server >= persisted ? "server value accepted"
                                 : "server value accepted (save was ahead; server wins)");
    }
    st->lastKnownSkills = obs.skills;

    // --- position and life state ------------------------------------------
    {
        char pos[48], srv[48];
        std::snprintf(pos, sizeof(pos), "%d,%d", st->lastKnownX, st->lastKnownY);
        std::snprintf(srv, sizeof(srv), "%d,%d", obs.x, obs.y);
        line("position", pos, srv, "server value accepted");
    }
    st->lastKnownX = obs.x;
    st->lastKnownY = obs.y;

    line("alive", st->lastKnownDead ? "dead" : "alive", obs.dead ? "dead" : "alive",
         "server value accepted");
    st->lastKnownDead = obs.dead;

    // --- the persisted intention ------------------------------------------
    //
    // A restored goal is a HYPOTHESIS about what to do next, not a resumable
    // process. Anything mid-flight is dropped; the planner re-selects on the
    // first tick from real observation.
    if (st->goal.active) {
        const char* why = nullptr;
        if (obs.dead && st->goal.kind != GoalKind::RecoverCorpse &&
            st->goal.kind != GoalKind::Survive) {
            why = "character is dead; a work goal cannot resume";
        } else if (st->goal.kind == GoalKind::RecoverCorpse && !obs.corpseKnown) {
            why = "corpse is no longer known to this session";
        } else if (st->goal.kind == GoalKind::GatherLogs && !obs.axeInPack &&
                   !obs.axeEquipped) {
            why = "no axe: the gathering goal is no longer satisfiable";
        }
        if (why) {
            rep.goalDropped = true;
            rep.goalDropReason = why;
            st->goal.active = false;
            st->goal.failureReason = why;
        } else {
            // Kept as an intention, but its clock and attempt counters are
            // transient and start fresh -- a goal does not carry a stale
            // 40-minute age across a restart.
            st->goal.startedAtMs = obs.nowMs;
            st->goal.attempts = 0;
        }
    }

    // --- death-spiral decay ------------------------------------------------
    if (st->recentDeaths > 0 && st->lastDeathMs > 0 &&
        obs.nowMs - st->lastDeathMs > 60 * 60 * 1000) {
        line("recent_deaths", I(st->recentDeaths), "0",
             "decayed: an hour without dying");
        st->recentDeaths = 0;
    }

    // --- identity bookkeeping ---------------------------------------------
    if (st->identity.firstSeenAtMs == 0) st->identity.firstSeenAtMs = obs.nowMs;
    st->identity.lastSeenAtMs = obs.nowMs;

    return rep;
}

}  // namespace uo::life
