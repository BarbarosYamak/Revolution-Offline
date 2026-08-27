// uo_econdump -- export the compiled Revolution economy graph as TSV.
//
// M3.8 Phase 9.
//
// SOURCE OF TRUTH: THE COMPILED GRAPH, NOT THESE FILES.
//
// The brief asks for one of two arrangements -- generate the exports from
// canonical definitions, or make the data files canonical inputs -- and says to
// pick one and say so. This picks the first, deliberately:
//
//   * production.h / Production.cpp and vendor_policy.cpp are already the
//     things 96 assertions run against. A recipe that is wrong there fails a
//     test today.
//   * every Recipe carries an `evidence` string, asserted non-empty by test.
//     A TSV cannot enforce that; a compiled table can.
//   * two editable sources of the same truth drift, and the drift is silent.
//     The vendor matrix already documents 608 items across 7 provenance
//     classes; a second hand-maintained copy is a liability, not an asset.
//
// So these TSVs are a VIEW. They exist so that tools, humans and future
// milestones can read, diff and plan against the economy without linking the
// client -- and `m38_exports` regenerates them in memory and fails if the
// committed files disagree, so the view cannot silently rot.
//
// Regenerate with:  uo_econdump <out-dir>

#include "uo/production.h"
#include "uo/vendor_policy.h"
#include "uo/mounts.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

// A TSV field may not contain a tab or a newline; nothing in the graph does,
// but an evidence string is free text and one stray tab would silently shift
// every later column. Cheap to guarantee rather than hope for.
std::string Clean(const char* s) {
    std::string out;
    if (!s) return out;
    for (const char* p = s; *p; ++p) {
        const char c = *p;
        out += (c == '\t' || c == '\n' || c == '\r') ? ' ' : c;
    }
    return out;
}

bool WriteResources(const std::string& dir) {
    const std::string path = dir + "/revolution_resources.tsv";
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::fprintf(stderr, "cannot write %s\n", path.c_str()); return false; }

    std::fprintf(f, "item\tprovenance\tnpc_policy\tis_raw\tevidence\n");

    // Every output in the graph, plus every ingredient that nothing produces --
    // the leaves. A leaf is where the economy actually touches the world
    // (ore, logs, wool, cotton), so omitting them would hide the gathering half.
    for (const auto& r : uo::prod::KnownRecipes()) {
        const auto ruling = uo::econ::CanUseNPCVendorFor(r.output);
        std::fprintf(f, "%s\t%s\t%s\t%d\t%s\n",
                     r.output,
                     uo::prod::ProvenanceName(r.provenance),
                     uo::econ::VendorClassName(ruling.klass),
                     uo::prod::IsRawResource(r.output) ? 1 : 0,
                     Clean(r.evidence).c_str());
    }
    for (const auto& r : uo::prod::KnownRecipes()) {
        for (const auto& in : r.inputs) {
            if (!in.item) continue;
            if (uo::prod::FindRecipe(in.item)) continue;   // not a leaf
            const auto ruling = uo::econ::CanUseNPCVendorFor(in.item);
            std::fprintf(f, "%s\t%s\t%s\t1\tleaf: nothing in the graph produces it\n",
                         in.item,
                         uo::prod::ProvenanceName(uo::prod::ProvenanceOf(in.item)),
                         uo::econ::VendorClassName(ruling.klass));
        }
    }
    std::fclose(f);
    return true;
}

// A recipe with skillId -1 imposes NO skill gate -- several genuinely do not,
// notably spinning wool, which is a plain item-use. Printing that as
// "skill -1, min_skill 0.0" reads as "skill zero", which is a different and
// wrong claim.
std::string SkillId(int id)     { return id < 0 ? std::string("none") : std::to_string(id); }
std::string SkillMin(int id, int tenths) {
    if (id < 0) return "";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", tenths / 10.0);
    return buf;
}

bool WriteRecipes(const std::string& dir) {
    const std::string path = dir + "/revolution_recipes.tsv";
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::fprintf(stderr, "cannot write %s\n", path.c_str()); return false; }

    // One row per INPUT, not per recipe: a flat table joins and filters without
    // a parser, and a recipe with no inputs (a gathering step) still gets a row
    // so it cannot vanish from a naive query.
    std::fprintf(f,
        "output\toutput_qty\tinput\tinput_qty\tskill\tmin_skill\tskill2\tmin_skill2"
        "\tstation\ttool\ttool_equipped\tprovenance\tevidence\n");

    for (const auto& r : uo::prod::KnownRecipes()) {
        bool wroteAny = false;
        for (const auto& in : r.inputs) {
            if (!in.item) continue;
            wroteAny = true;
            std::fprintf(f, "%s\t%d\t%s\t%d\t%s\t%s\t%s\t%s\t%s\t%s\t%d\t%s\t%s\n",
                         r.output, r.outputQty, in.item, in.qty,
                         SkillId(r.skillId).c_str(), SkillMin(r.skillId, r.skillTenths).c_str(),
                         SkillId(r.skillId2).c_str(), SkillMin(r.skillId2, r.skillTenths2).c_str(),
                         uo::prod::StationName(r.station),
                         uo::prod::ToolName(r.tool),
                         uo::prod::ToolMustBeEquipped(r.tool) ? 1 : 0,
                         uo::prod::ProvenanceName(r.provenance),
                         Clean(r.evidence).c_str());
        }
        if (!wroteAny) {
            std::fprintf(f, "%s\t%d\t\t0\t%s\t%s\t%s\t%s\t%s\t%s\t%d\t%s\t%s\n",
                         r.output, r.outputQty,
                         SkillId(r.skillId).c_str(), SkillMin(r.skillId, r.skillTenths).c_str(),
                         SkillId(r.skillId2).c_str(), SkillMin(r.skillId2, r.skillTenths2).c_str(),
                         uo::prod::StationName(r.station),
                         uo::prod::ToolName(r.tool),
                         uo::prod::ToolMustBeEquipped(r.tool) ? 1 : 0,
                         uo::prod::ProvenanceName(r.provenance),
                         Clean(r.evidence).c_str());
        }
    }
    std::fclose(f);
    return true;
}

bool WriteVendorPolicy(const std::string& dir) {
    const std::string path = dir + "/revolution_vendor_policy.tsv";
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::fprintf(stderr, "cannot write %s\n", path.c_str()); return false; }

    std::fprintf(f, "item\tvendor_class\tnpc_purchase_allowed\tauthenticity_gap\treason\n");
    for (const auto& kv : uo::econ::VendorMatrix()) {
        const auto ruling = uo::econ::CanUseNPCVendorFor(kv.first);
        std::fprintf(f, "%s\t%s\t%d\t%d\t%s\n",
                     kv.first,
                     uo::econ::VendorClassName(kv.second),
                     ruling.allowed ? 1 : 0,
                     ruling.authenticityGap ? 1 : 0,
                     Clean(ruling.reason).c_str());
    }
    std::fclose(f);
    return true;
}

// An unknown must LOOK unknown. Formatting -1 tenths with %.1f prints "-0.1",
// which reads as a real threshold and is exactly the kind of number this
// project has had to withdraw from a published report before.
// `int`, not i32: this anonymous namespace sits at global scope, and the fixed
// -width aliases live in namespace uo. Clean() above compiled only because it
// happens to use no uo types.
std::string Tenths(int t) {
    if (t < 0) return "UNKNOWN";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", t / 10.0);
    return buf;
}

bool WriteMountRules(const std::string& dir) {
    const std::string path = dir + "/revolution_mount_rules.tsv";
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::fprintf(stderr, "cannot write %s\n", path.c_str()); return false; }

    // Both skills, both bounds. A single "revolution_taming" column hid two
    // facts that change what a mount costs: Animal Lore is required at the same
    // value, and Mustang/Shire roll their requirement per animal.
    std::fprintf(f,
        "creature\tbody\ttaming_min\ttaming_max\tlore_min\tlore_max\trandom_range"
        "\treliable_cost\truntime_taming\truntime_laxer\tera\tbot_legal"
        "\tweekly_supply\tsource\n");
    for (const auto& m : uo::mounts::KnownMounts()) {
        std::fprintf(f, "%s\t0x%04X\t%s\t%s\t%s\t%s\t%d\t%s\t%s\t%d\t%s\t%d\t%d\t%s\n",
                     m.defname, m.body,
                     Tenths(m.tamingMinTenths).c_str(),
                     Tenths(m.tamingMaxTenths).c_str(),
                     Tenths(m.loreMinTenths).c_str(),
                     Tenths(m.loreMaxTenths).c_str(),
                     m.randomRange ? 1 : 0,
                     Tenths(uo::mounts::ReliableSkillCost(m.defname)).c_str(),
                     Tenths(m.runtimeTamingTenths).c_str(),
                     uo::mounts::RuntimeIsMorePermissive(m) ? 1 : 0,
                     uo::mounts::EraName(m.era),
                     m.botLegal ? 1 : 0,
                     m.weeklySupply,
                     Clean(m.source).c_str());
    }
    std::fclose(f);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string dir = (argc > 1) ? argv[1] : ".";
    if (!WriteResources(dir))     return 1;
    if (!WriteRecipes(dir))       return 1;
    if (!WriteVendorPolicy(dir))  return 1;
    if (!WriteMountRules(dir))    return 1;
    std::printf("econdump: wrote 4 TSV files to %s\n", dir.c_str());
    return 0;
}
