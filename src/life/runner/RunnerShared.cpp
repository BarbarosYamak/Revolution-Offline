#include "RunnerInternal.h"

namespace uo::life {
namespace runner_detail {


std::unordered_map<std::string, double>& SeededCreatureDanger() {
    static std::unordered_map<std::string, double> table;
    return table;
}

void LoadSeededCreatureDanger(const std::string& dataDir) {
    std::unordered_map<std::string, double>& t = SeededCreatureDanger();
    if (!t.empty()) return;
    std::FILE* f = std::fopen((dataDir + "/revolution_creatures.tsv").c_str(), "rb");
    if (!f) return;
    char line[512];
    bool first = true;
    while (std::fgets(line, sizeof(line), f)) {
        if (first) { first = false; continue; }          // header
        std::string row(line);
        // defname 	 name 	 danger 	 ...
        const usize t1 = row.find('	');
        if (t1 == std::string::npos) continue;
        const usize t2 = row.find('	', t1 + 1);
        if (t2 == std::string::npos) continue;
        const usize t3 = row.find('	', t2 + 1);
        const std::string name = row.substr(t1 + 1, t2 - t1 - 1);
        const std::string dg =
            row.substr(t2 + 1, (t3 == std::string::npos ? row.size() : t3) - t2 - 1);
        if (name.empty()) continue;
        std::string key;
        for (char c : name)
            key.push_back(static_cast<char>(std::tolower(
                static_cast<unsigned char>(c))));
        t[key] = std::atof(dg.c_str());
        // ... and the taming requirement, which is the LAST column.
        const usize lastTab = row.find_last_of('	');
        if (lastTab != std::string::npos && lastTab > t2) {
            const double tam = std::atof(row.c_str() + lastTab + 1);
            if (tam >= 0.0) SeededTaming()[key] = tam;
        }
    }
    std::fclose(f);
}

// The TAMING requirement from the same table, or -1 for a creature that
// cannot be tamed at all. 109 of the 450 carry one.
std::unordered_map<std::string, double>& SeededTaming() {
    static std::unordered_map<std::string, double> table;
    return table;
}

std::vector<Pasture>& Pastures() {
    static std::vector<Pasture> table;
    return table;
}

void LoadPastures(const std::string& dataDir) {
    std::vector<Pasture>& t = Pastures();
    if (!t.empty()) return;
    std::FILE* f =
        std::fopen((dataDir + "/revolution_pastures.tsv").c_str(), "rb");
    if (!f) return;
    char line[256];
    bool first = true;
    while (std::fgets(line, sizeof(line), f)) {
        if (first) { first = false; continue; }              // header
        Pasture p;
        int mapId = 0;
        // x \t y \t map \t count \t radius \t label
        if (std::sscanf(line, "%d\t%d\t%d\t%d\t%d", &p.x, &p.y, &mapId,
                        &p.count, &p.radius) != 5)
            continue;
        if (mapId != 0) continue;   // the bots only ever play map 0
        t.push_back(p);
    }
    std::fclose(f);
}

std::vector<TameCluster>& Tamables() {
    static std::vector<TameCluster> table;
    return table;
}

void LoadTamables(const std::string& dataDir) {
    std::vector<TameCluster>& t = Tamables();
    if (!t.empty()) return;
    std::FILE* f =
        std::fopen((dataDir + "/revolution_tamables.tsv").c_str(), "rb");
    if (!f) return;
    char line[512];
    bool first = true;
    while (std::fgets(line, sizeof(line), f)) {
        if (first) { first = false; continue; }              // header
        // x \t y \t map \t count \t radius \t label \t defname \t taming_req
        std::vector<std::string> col;
        const std::string row(line);
        usize start = 0;
        while (start <= row.size()) {
            const usize tab = row.find('\t', start);
            if (tab == std::string::npos) {
                std::string last = row.substr(start);
                while (!last.empty() &&
                       (last.back() == '\n' || last.back() == '\r'))
                    last.pop_back();
                col.push_back(last);
                break;
            }
            col.push_back(row.substr(start, tab - start));
            start = tab + 1;
        }
        if (col.size() < 8) continue;
        if (std::atoi(col[2].c_str()) != 0) continue;   // bots play map 0 only
        TameCluster c;
        c.x = std::atoi(col[0].c_str());
        c.y = std::atoi(col[1].c_str());
        c.count = std::atoi(col[3].c_str());
        c.radius = std::atoi(col[4].c_str());
        c.label = col[5];
        c.req = std::atof(col[7].c_str());
        t.push_back(c);
    }
    std::fclose(f);
}

std::string Fmt2(const char* fmt, ...) {
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return std::string(buf);
}

// WHICH ERRAND ACTUALLY MAKES A THING THIS LIFE PRODUCES.
//
// Only consulted once market::RouteForInput has already said SelfProduce --
// i.e. the item is in this character's own `produces` list -- so this is
// choosing between that character's own goals, never inventing a capability.
// The prefixes are the same defname families SupplierTradeFor above matches
// on, and each maps to the goal that already exists for it: ore is dug (Mine),
// ingots are smelted from ore (Smelt, which chains back to Mine through
// NeedOre), fish are caught (Fish), timber is chopped (GatherLogs). Anything
// else a profession produces, it produces at a craft menu.
GoalKind ProducingGoalFor(const std::string& item) {
    if (item.rfind("i_ore_", 0) == 0)   return GoalKind::Mine;
    if (item.rfind("i_ingot_", 0) == 0) return GoalKind::Smelt;
    if (item.rfind("i_fish", 0) == 0)   return GoalKind::Fish;
    if (item == "i_log" || item == "i_board") return GoalKind::GatherLogs;
    // THE TEXTILE CHAIN IS A STATION CHAIN, NOT A MENU CRAFT. A ball of yarn
    // comes off a spinning wheel, a bolt off a loom, and cloth off a pair of
    // scissors -- all three are Provenance::WorldProcessed in Production.cpp
    // (:56, :67, :71) with no skill and no craft gump anywhere. Left routed to
    // Craft, DoCraft could only answer REFUSE_MISSING_RECIPE: Aelia did it
    // three times in 130 ms for i_cloth_bolt while MAKE_CLOTH, the goal that
    // owns the loom, sat unpicked (artifacts/domakecloth_shear_to_target_
    // 2026-09-02.md, defect 2). Exactly Draver's i_ingot_iron/SMELT case.
    if (item == "i_yarn_ball" || item == "i_cloth_bolt" || item == "i_cloth")
        return GoalKind::MakeCloth;
    return GoalKind::Craft;
}

}  // namespace runner_detail
}  // namespace uo::life
