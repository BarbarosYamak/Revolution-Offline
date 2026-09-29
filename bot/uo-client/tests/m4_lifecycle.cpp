// M4 step 1: the persistent character record.
//
//   * the first archetype's targets obey the Revolution profile
//   * validation refuses targets that would steer training wrong
//   * the text format round-trips every field, and rejects a truncated write
//   * knowledge crosses a logout with the RIGHT age -- two sessions, two
//     unrelated steady clocks, one wall clock between them
//   * a saved file cannot make a vendor usable that current policy refuses
//   * files are replaced atomically and a corrupt life is never mistaken for
//     no life
//
// No server, no MULs, no world data. Writes only under the system temp dir.

#include "uo/lifecycle.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what);
    }
}

void Section(const char* name) { std::printf("[%s]\n", name); }

using namespace uo;

constexpr i64 kHour = 60LL * 60 * 1000;
// A fixed wall time so the tests do not depend on today's date.
constexpr i64 kWall0 = 1790000000000LL;

life::CharacterRecord Sample() {
    life::CharacterRecord r = life::NewLumberjackSwordsman("Ahmet", "revolutionbot01", kWall0);
    r.sessions = 3;
    r.lastLoginWallMs = kWall0 + 5 * kHour;
    r.objective = {life::ObjectiveKind::Earn, "i_log", kWall0 + 6 * kHour, 2};
    r.lastLogout = {true, 1490, 1555, 30, "britain_bank", true, kWall0 + 7 * kHour};
    r.deaths.push_back({2000, 800, 5, "forest_yew", 0x40001234u, 1, kWall0 + kHour});
    r.routes.push_back({"service:banker", 4, 1, 0, kWall0 + 2 * kHour, "no path"});

    supply::Supplier s;
    s.kind = supply::SupplierKind::NpcVendor;
    s.serial = 0x0000ABCDu;
    s.x = 1450; s.y = 1600; s.z = 20;
    s.name = "Tomas\tthe \\carpenter\n";   // every character the escaper handles
    s.what = "i_hatchet";
    s.observedQuantity = 12;
    s.observedPricePerUnit = 23;
    s.observedAtMs = kWall0 + 3 * kHour;
    s.lastVerifiedMs = kWall0 + 4 * kHour;
    s.failures = 1;
    s.policyClass = econ::VendorClass::BasicCraftTool;
    s.policyAllows = true;
    r.suppliers.push_back(s);

    r.visits.push_back({"britain_bank", kWall0 + 7 * kHour, 9});
    travel::ServiceSighting sig;
    sig.service = wm::Service::Banker;
    sig.serial = 0x000011AAu;
    sig.title = "the banker";
    sig.x = 1427; sig.y = 1685; sig.z = 0;
    sig.seenMs = kWall0 + 7 * kHour;
    r.sightings.push_back(sig);
    travel::KnownRune rune;
    rune.serial = 0x40000077u;
    rune.graphic = 0x1F14;
    rune.name = "Britain";
    rune.marked = true;
    rune.x = 1490; rune.y = 1555; rune.z = 30;
    rune.destinationKnown = true;
    r.runes.push_back(rune);
    r.dangers.push_back({2000, 800, 12, kWall0 + 30 * kHour, "died to a grizzly"});
    r.homeSet = true;
    r.homeX = 1490; r.homeY = 1555; r.homeZ = 30;
    r.homePlaceId = "britain_bank";
    r.lastDeath.valid = true;
    r.lastDeath.x = 2000; r.lastDeath.y = 800; r.lastDeath.z = 5;
    r.lastDeath.regionId = "forest_yew";
    r.lastDeath.corpseSerial = 0x40001234u;
    r.lastDeath.timeMs = kWall0 + kHour;
    r.lastDeath.recoveryAttempts = 1;
    return r;
}

// --------------------------------------------------------------------------
void TestArchetype() {
    Section("archetype: lumberjack / swordsman");
    const life::CharacterRecord r = life::NewLumberjackSwordsman("Ahmet", "acct", kWall0);
    const life::RecordCheck c = life::ValidateRecord(r);
    Check(c.ok, "the first archetype passes its own profile");

    i32 total = 0;
    for (const auto& s : r.targetBuild) total += s.tenths;
    Check(total == 5000, "five named skills at 100.0 = 500.0");
    Check(total <= rules::Revolution().totalSkillCapTenths, "never above the 700 cap");
    Check(r.targetStr + r.targetDex + r.targetInt == 225, "stats spend exactly the 225 cap");
    Check(r.targetStr <= 100 && r.targetDex <= 100 && r.targetInt <= 100, "no stat over 100");

    bool hasAxe = false;
    for (const auto& e : r.equipment) hasAxe |= e.itemdef == "i_hatchet" && e.required;
    Check(hasAxe, "the hatchet is a required tool");
    Check(r.aspiration == life::Aspiration::LumberjackSwordsman, "aspiration set");
    Check(r.createdWallMs == kWall0, "birth date recorded");
}

// --------------------------------------------------------------------------
void TestValidation() {
    Section("validation");
    auto base = [] { return life::NewLumberjackSwordsman("Ahmet", "acct", kWall0); };

    life::CharacterRecord r = base();
    r.targetBuild.push_back({rules::kMagery, 1000});
    r.targetBuild.push_back({rules::kMeditation, 1000});
    r.targetBuild.push_back({rules::kArmsLore, 100});
    Check(!life::ValidateRecord(r).ok, "710.0 is refused");

    r = base();
    r.targetBuild.push_back({rules::kSwordsmanship, 1000});
    Check(!life::ValidateRecord(r).ok, "the same skill twice is refused");

    r = base();
    r.targetBuild.push_back({rules::kMagicResistance, 500});
    Check(!life::ValidateRecord(r).ok, "an inactive Revolution skill is refused");

    r = base();
    r.targetStr = 101; r.targetInt = 24;
    Check(!life::ValidateRecord(r).ok, "a stat over 100 is refused");

    r = base();
    r.targetInt = 26;
    Check(!life::ValidateRecord(r).ok, "226 total stats is refused");

    r = base();
    r.name.clear();
    Check(!life::ValidateRecord(r).ok, "a nameless character is refused");

    r = base();
    r.aspiration = life::Aspiration::Unknown;
    Check(!life::ValidateRecord(r).ok, "an unknown aspiration is refused");

    r = base();
    r.economy.unloadAtWeightPercent = 0;
    Check(!life::ValidateRecord(r).ok, "an unload threshold of 0% is refused");
}

// --------------------------------------------------------------------------
void TestRoundTrip() {
    Section("text format: round trip");
    const life::CharacterRecord a = Sample();
    const std::string text = life::Serialize(a);

    life::CharacterRecord b;
    const life::ParseResult p = life::Parse(text, &b);
    Check(p.ok, "a serialized record parses");
    if (!p.ok) std::printf("    line %d: %s\n", p.line, p.error.c_str());
    Check(p.unknownLines == 0, "nothing unknown in our own output");

    Check(b.name == a.name && b.account == a.account, "identity");
    Check(b.aspiration == a.aspiration && b.sessions == 3, "aspiration and sessions");
    Check(b.createdWallMs == a.createdWallMs && b.lastLoginWallMs == a.lastLoginWallMs,
          "identity timestamps");
    Check(b.targetStr == 100 && b.targetDex == 100 && b.targetInt == 25, "target stats");
    Check(b.targetBuild.size() == a.targetBuild.size(), "target build size");
    bool skillsSame = b.targetBuild.size() == a.targetBuild.size();
    for (usize i = 0; skillsSame && i < a.targetBuild.size(); ++i)
        skillsSame = a.targetBuild[i].skillId == b.targetBuild[i].skillId &&
                     a.targetBuild[i].tenths == b.targetBuild[i].tenths;
    Check(skillsSame, "target build values, in order");
    Check(b.equipment.size() == 2 && b.equipment[0].itemdef == "i_hatchet" &&
          b.equipment[0].required, "equipment goals");
    Check(b.economy.goldReserve == a.economy.goldReserve &&
          b.economy.bankAboveGold == a.economy.bankAboveGold &&
          b.economy.unloadAtWeightPercent == a.economy.unloadAtWeightPercent, "economy");
    Check(b.objective.kind == life::ObjectiveKind::Earn && b.objective.target == "i_log" &&
          b.objective.attempts == 2 && b.objective.sinceWallMs == a.objective.sinceWallMs,
          "objective resumes");
    Check(b.lastLogout.valid && b.lastLogout.safe && b.lastLogout.x == 1490 &&
          b.lastLogout.z == 30 && b.lastLogout.placeId == "britain_bank", "logout point");
    Check(b.deaths.size() == 1 && b.deaths[0].corpseSerial == 0x40001234u &&
          b.deaths[0].regionId == "forest_yew", "death history");
    Check(b.routes.size() == 1 && b.routes[0].successes == 4 &&
          b.routes[0].lastFailure == "no path", "route memory");

    Check(b.suppliers.size() == 1, "supplier count");
    if (!b.suppliers.empty()) {
        const supply::Supplier& s = b.suppliers[0];
        Check(s.name == a.suppliers[0].name, "tab, backslash and newline survive in a name");
        Check(s.serial == 0x0000ABCDu && s.what == "i_hatchet" && s.z == 20, "supplier identity");
        Check(s.observedQuantity == 12 && s.observedPricePerUnit == 23 && s.failures == 1,
              "supplier observation");
        Check(s.observedAtMs == a.suppliers[0].observedAtMs &&
              s.lastVerifiedMs == a.suppliers[0].lastVerifiedMs, "supplier timestamps");
        Check(s.policyClass == econ::VendorClass::BasicCraftTool && s.policyAllows,
              "supplier policy");
    }
    Check(b.visits.size() == 1 && b.visits[0].visits == 9, "visits");
    Check(b.sightings.size() == 1 && b.sightings[0].service == wm::Service::Banker &&
          b.sightings[0].title == "the banker", "service sightings");
    Check(b.runes.size() == 1 && b.runes[0].marked && b.runes[0].destinationKnown &&
          b.runes[0].graphic == 0x1F14, "runes");
    Check(b.dangers.size() == 1 && b.dangers[0].radius == 12 &&
          b.dangers[0].why == "died to a grizzly", "danger notes");
    Check(b.homeSet && b.homeX == 1490 && b.homePlaceId == "britain_bank", "home");
    Check(b.lastDeath.valid && b.lastDeath.recoveryAttempts == 1, "last death");

    Check(life::Serialize(b) == text, "serialize(parse(x)) == x");
}

// --------------------------------------------------------------------------
void TestParseRefusals() {
    Section("text format: refusals");
    const std::string good = life::Serialize(Sample());
    life::CharacterRecord out;

    // Truncation: a save killed mid-write must not load as a shorter life.
    std::string cut = good.substr(0, good.rfind("end\n"));
    Check(!life::Parse(cut, &out).ok, "missing 'end' is refused");
    cut = good.substr(0, good.size() / 2);
    Check(!life::Parse(cut, &out).ok, "half a file is refused");

    Check(!life::Parse(good + "visit\tx\t1\t1\n", &out).ok, "content after 'end' is refused");
    Check(!life::Parse("", &out).ok, "an empty file is refused");
    Check(!life::Parse("hello\n", &out).ok, "a foreign file is refused");

    std::string v2 = good;
    v2.replace(v2.find("\t1\n"), 3, "\t2\n");
    Check(!life::Parse(v2, &out).ok, "a newer format version is refused, not guessed at");

    std::string extra = good;
    extra.replace(extra.find("stats\t100\t100\t25"), 16, "stats\t100\t100\t25\t9");
    Check(!life::Parse(extra, &out).ok, "an extra field is refused");

    std::string bad = good;
    bad.replace(bad.find("stats\t100"), 9, "stats\tabc");
    Check(!life::Parse(bad, &out).ok, "a non-number is refused");

    std::string z = good;
    z.replace(z.find("home\t1490\t1555\t30"), 17, "home\t1490\t1555\t300");
    Check(!life::Parse(z, &out).ok, "a z outside i8 is refused, not wrapped");

    std::string asp = good;
    asp.replace(asp.find("lumberjack_swordsman"), 20, "necromancer_supreme_");
    Check(!life::Parse(asp, &out).ok, "an unknown aspiration is refused");

    // Forward compatibility: a kind a newer build added is skipped and counted.
    std::string fwd = good;
    fwd.insert(fwd.find("end\n"), "friend\tMehmet\t5\n");
    const life::ParseResult p = life::Parse(fwd, &out);
    Check(p.ok && p.unknownLines == 1, "an unknown record kind is skipped and counted");

    // Written on Linux, edited in Notepad.
    std::string crlf;
    for (char ch : good) {
        if (ch == '\n') crlf += "\r\n";
        else crlf.push_back(ch);
    }
    life::CharacterRecord c;
    Check(life::Parse(crlf, &c).ok && c.name == "Ahmet", "CRLF line endings are accepted");
}

// --------------------------------------------------------------------------
// The two-clock problem. Session 1's steady clock reads ~1e9; session 2's
// process restarted and reads ~5e4. Eight wall-clock hours pass in between.
void TestClockAcrossLogout() {
    Section("clocks: knowledge crosses a logout with the right age");

    const life::Clock s1{1000000000LL, kWall0};
    travel::PersonalKnowledge k1;
    supply::Registry reg1;
    reg1.RecordVendorStock(0xABCDu, "Tomas", 1450, 1600, 20, "i_hatchet", 12, 23,
                           s1.steadyMs);
    k1.NoteDanger(2000, 800, 10, s1.steadyMs + 1 * kHour, "short scare");
    k1.NoteDanger(2100, 900, 10, s1.steadyMs + 24 * kHour, "grizzly den");
    k1.NoteVisit("britain_bank", s1.steadyMs);
    k1.NoteService(wm::Service::Healer, 0x11BBu, "the healer", 1480, 1600, 0, s1.steadyMs);

    // Right at save time the vendor observation is current.
    Check(supply::Registry::FreshnessOf(reg1.All()[0], s1.steadyMs) ==
          supply::Freshness::VerifiedCurrent, "fresh when observed");

    life::CharacterRecord r = life::NewLumberjackSwordsman("Ahmet", "acct", kWall0);
    life::Capture(&r, k1, reg1, s1);
    Check(r.suppliers.size() == 1 && r.suppliers[0].lastVerifiedMs == kWall0,
          "captured timestamps are wall-clock");
    Check(r.dangers.size() == 2, "both live dangers captured");
    Check(r.lastSavedWallMs == kWall0, "save time stamped");

    // Session 2: new process, tiny steady reading, eight hours later.
    const life::Clock s2{50000LL, kWall0 + 8 * kHour};
    travel::PersonalKnowledge k2;
    supply::Registry reg2;
    k2.NoteVisit("stale_leftover", 1);     // Apply must clear, not append
    life::Apply(r, &k2, &reg2, s2);

    Check(reg2.Size() == 1, "supplier restored");
    const supply::Freshness fr = supply::Registry::FreshnessOf(reg2.All()[0], s2.steadyMs);
    Check(fr == supply::Freshness::Stale,
          "an 8-hour-old shop list is STALE, not current, after the restart");
    // The failure mode this guards: copying the steady value across unchanged.
    Check(reg2.All()[0].lastVerifiedMs == s2.steadyMs - 8 * kHour,
          "age preserved exactly across clocks");

    Check(k2.DangerNoteCount() == 1, "the expired danger was dropped at load");
    Check(k2.DangerAt(2100, 900, s2.steadyMs) != wm::Danger::Normal &&
          k2.DangerAt(2100, 900, s2.steadyMs) != wm::Danger::Unknown,
          "the 24-hour danger is still live");
    Check(k2.DangerAt(2100, 900, s2.steadyMs + 17 * kHour) == wm::Danger::Normal ||
          k2.DangerAt(2100, 900, s2.steadyMs + 17 * kHour) == wm::Danger::Unknown,
          "and expires on schedule: 24h after it was noted, not 24h after login");
    Check(!k2.HasVisited("stale_leftover") && k2.HasVisited("britain_bank"),
          "Apply replaces knowledge rather than merging into it");
    Check(k2.RecentService(wm::Service::Healer, s2.steadyMs, 9 * kHour) != nullptr,
          "the healer sighting survives");
    Check(k2.RecentService(wm::Service::Healer, s2.steadyMs, 7 * kHour) == nullptr,
          "and is correctly 8 hours old");

    life::Clock zero{123, 456};
    Check(zero.ToWall(0) == 0 && zero.ToSteady(0) == 0, "0 stays 'never' in both directions");
}

// --------------------------------------------------------------------------
void TestPolicyReRuled() {
    Section("policy: a saved file cannot grant a purchase");
    life::CharacterRecord r = life::NewLumberjackSwordsman("Ahmet", "acct", kWall0);
    supply::Supplier s;
    s.kind = supply::SupplierKind::NpcVendor;
    s.serial = 0x1234u;
    s.x = 1450; s.y = 1600;
    s.what = "i_unaudited_example_item";   // no ruling: Unknown fails safe
    s.observedAtMs = s.lastVerifiedMs = kWall0;
    // A hand-edited, or older-policy, file claims it is allowed.
    s.policyClass = econ::VendorClass::BasicCraftTool;
    s.policyAllows = true;
    r.suppliers.push_back(s);

    travel::PersonalKnowledge k;
    supply::Registry reg;
    const life::Clock c{1000, kWall0};
    life::Apply(r, &k, &reg, c);
    Check(reg.Size() == 1, "the record is kept -- it is still a fact about the world");
    Check(!reg.All()[0].policyAllows, "but re-ruled by today's policy");
    supply::Need need;
    need.what = "i_unaudited_example_item";
    Check(!reg.Best(need, 1450, 1600, c.steadyMs).usable, "and never returned as usable");
}

// --------------------------------------------------------------------------
void TestDeathHistory() {
    Section("death history");
    life::CharacterRecord r = life::NewLumberjackSwordsman("Ahmet", "acct", kWall0);
    travel::PersonalKnowledge k;
    supply::Registry reg;
    life::Clock c{5000000, kWall0};

    k.NoteDeath(2000, 800, 5, "forest_yew", c.steadyMs);
    life::Capture(&r, k, reg, c);
    life::Capture(&r, k, reg, c);   // an autosave later, same death
    Check(r.deaths.size() == 1, "the same death saved twice is one entry");

    // Survives a logout/login pairing with a slightly different offset.
    life::Clock c2{900, kWall0 + kHour + 1};
    travel::PersonalKnowledge k2;
    life::Apply(r, &k2, &reg, c2);
    life::Capture(&r, k2, reg, c2);
    Check(r.deaths.size() == 1, "and stays one entry across a re-pairing of clocks");

    k2.NoteCorpseRecoveryAttempt();
    life::Capture(&r, k2, reg, c2);
    Check(r.deaths.size() == 1 && r.deaths[0].recoveryAttempts == 1,
          "a recovery attempt updates the entry");

    for (int i = 0; i < 20; ++i) {
        k2.NoteDeath(100 + i, 100, 0, "deep_forest", c2.steadyMs + (i + 1) * 60000);
        life::Capture(&r, k2, reg, c2);
    }
    Check(r.deaths.size() == life::kMaxDeaths, "history is bounded");
    Check(r.deaths.back().x == 119, "and keeps the newest");
}

// --------------------------------------------------------------------------
void TestRoutes() {
    Section("route memory");
    life::CharacterRecord r;
    life::NoteRoute(&r, "service:banker", true, nullptr, 10);
    life::NoteRoute(&r, "service:banker", false, "no path", 20);
    life::NoteRoute(&r, "service:banker", false, "stuck", 30);
    const life::RouteMemory* m = life::FindRoute(r, "service:banker");
    Check(m && m->successes == 1 && m->failures == 2 && m->consecutiveFailures == 2,
          "counts successes, failures and the current failure streak");
    Check(m && m->lastFailure == "stuck", "remembers the latest reason");
    life::NoteRoute(&r, "service:banker", true, nullptr, 40);
    Check(m && m->consecutiveFailures == 0, "an arrival ends the streak");
    life::NoteRoute(&r, "", true, nullptr, 50);
    Check(r.routes.size() == 1, "an empty destination is ignored");

    for (int i = 0; i < 100; ++i) {
        char name[32];
        std::snprintf(name, sizeof(name), "place:%d", i);
        life::NoteRoute(&r, name, true, nullptr, 100 + i);
    }
    Check(r.routes.size() == life::kMaxRoutes, "bounded");
    Check(life::FindRoute(r, "place:99") != nullptr, "newest kept");
    Check(life::FindRoute(r, "service:banker") == nullptr, "oldest evicted");
}

// --------------------------------------------------------------------------
void TestFiles() {
    Section("files");
    std::error_code ec;
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path(ec) / "m4_lifecycle_test";
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const std::string path = (dir / life::RecordFileName("Ahmet")).string();

    life::CharacterRecord out;
    Check(life::LoadFile(path, &out).status == life::LoadStatus::NotFound,
          "no file yet: NotFound, so a life may begin");

    const life::CharacterRecord a = Sample();
    std::string err;
    Check(life::SaveFile(path, a, &err), "save");
    Check(!std::filesystem::exists(path + ".tmp", ec), "no temp file left behind");

    life::LoadResult lr = life::LoadFile(path, &out);
    Check(lr.status == life::LoadStatus::Ok && out.name == "Ahmet" && out.sessions == 3,
          "load what was saved");

    life::CharacterRecord a2 = a;
    a2.sessions = 4;
    Check(life::SaveFile(path, a2, &err), "save over an existing life");
    Check(life::LoadFile(path, &out).status == life::LoadStatus::Ok && out.sessions == 4,
          "the newer life replaced the older one");

    // Truncate on disk, as a killed process would.
    std::string text = life::Serialize(a2);
    std::FILE* f = std::fopen(path.c_str(), "wb");
    std::fwrite(text.data(), 1, text.size() / 2, f);
    std::fclose(f);
    lr = life::LoadFile(path, &out);
    Check(lr.status == life::LoadStatus::Corrupt,
          "a half-written life is Corrupt -- never NotFound, which would invite an overwrite");
    Check(!lr.detail.empty(), "and says why");

    life::CharacterRecord greedy = a;
    greedy.targetInt = 100;   // 300 total
    Check(life::SaveFile(path, greedy, &err), "an invalid record can be written...");
    Check(life::LoadFile(path, &out).status == life::LoadStatus::Invalid,
          "...but loads as Invalid, not as a character");

    Check(life::RecordFileName("Ahmet the Woodsman") == "Ahmet_the_Woodsman.life",
          "file names: spaces become underscores");
    Check(life::RecordFileName("../../etc") == "______etc.life",
          "file names: no path can escape the life directory");
    Check(life::RecordFileName("") == "unnamed.life", "file names: never empty");

    std::filesystem::remove_all(dir, ec);
}

// --------------------------------------------------------------------------
void TestNames() {
    Section("enum names");
    for (int i = 0; i < static_cast<int>(life::ObjectiveKind::Count); ++i) {
        const auto k = static_cast<life::ObjectiveKind>(i);
        Check(life::ObjectiveKindFromName(life::ObjectiveKindName(k)) == k,
              "objective names round-trip");
    }
    Check(life::AspirationFromName("lumberjack_swordsman") ==
          life::Aspiration::LumberjackSwordsman, "aspiration by name");
    Check(life::AspirationFromName("pure_mage") == life::Aspiration::Unknown,
          "an aspiration not yet built is Unknown");
}

}  // namespace

int main() {
    TestArchetype();
    TestValidation();
    TestRoundTrip();
    TestParseRefusals();
    TestClockAcrossLogout();
    TestPolicyReRuled();
    TestDeathHistory();
    TestRoutes();
    TestFiles();
    TestNames();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
