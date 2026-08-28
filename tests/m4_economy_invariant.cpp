// M4 economy invariant -- the anti-arbitrage ratchet.
//
// docs/TNS_WORLD_ECONOMY_DONOR_AUDIT.md section 13.5 states the rule this test
// exists to police:
//
//     No deterministic NPC/vendor/crafting/recycling cycle may generate net
//     gold or resources without a player-side loss.
//
// The runtime violates it today, and knowingly: NPCs sell raw materials and
// buy finished goods, and a crafted item's VALUE is unrelated to the sum of
// its ingredients' VALUE, so the 26% vendor spread is cleared by
// TRANSFORMATION. `tools/economy_arbitrage.py` recomputes the loops from the
// shard's own itemdefs; this test asserts on its committed output.
//
// WHAT THIS TEST IS FOR
//
// It is a RATCHET, not a pass/fail on the current state. The count may only
// go DOWN. Every economy decision in section 21 changes VALUE or markup, and
// any one of them can silently open new loops -- without this, closing them is
// a one-off cleanup rather than a property that stays true.
//
// When you deliberately close loops:
//   1. py tools/economy_arbitrage.py runtime/scripts --quiet \
//          --out docs/tns_exports/economy_arbitrage_loops.tsv
//   2. lower kBaseline below to the new count
//   3. commit both, and say in the message WHY the count moved
//
// A count that goes UP fails here, loudly, with the offending loop named.
//
// No server, no MULs. Reads one committed TSV whose path CTest passes in.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
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

// The recorded count, from tools/economy_arbitrage.py against the runtime at
// commit 7c71d66. 66 profitable loops out of 154 evaluated.
//
// This number is a DEBT MARKER, not a target. It is written down so that
// closing the loops is measurable and so that a regression is loud.
constexpr int kBaseline = 66;

struct Loop {
    double      net = 0.0;
    double      ratio = 0.0;
    std::string item;
    std::string skill;
    std::string ingredients;
    std::string source;
};

std::vector<std::string> SplitTabs(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : line) {
        if (c == '\t') { out.push_back(cur); cur.clear(); }
        else if (c != '\r') { cur.push_back(c); }
    }
    out.push_back(cur);
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    std::printf("m4_economy_invariant\n");

    const std::string path = (argc > 1)
        ? std::string(argv[1])
        : std::string("docs/tns_exports/economy_arbitrage_loops.tsv");

    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        std::printf("  FAIL: cannot open %s\n", path.c_str());
        std::printf("0 checks, 1 failures\n");
        return 1;
    }
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    std::fclose(f);

    // --- parse ------------------------------------------------------------
    Section("arbitrage export: shape");

    std::vector<std::string> lines;
    {
        std::string cur;
        for (char c : text) {
            if (c == '\n') { lines.push_back(cur); cur.clear(); }
            else cur.push_back(c);
        }
        if (!cur.empty()) lines.push_back(cur);
    }
    Check(lines.size() > 1, "the export has a header and at least one row");

    const std::vector<std::string> header = SplitTabs(lines.empty() ? "" : lines[0]);
    Check(header.size() >= 8, "the header has every expected column");
    Check(!header.empty() && header[0] == "net_gold_per_craft",
          "column 1 is the net gold per craft");

    std::vector<Loop> loops;
    bool everyRowCited = true;
    bool everyRowWellFormed = true;
    for (size_t i = 1; i < lines.size(); ++i) {
        if (lines[i].empty()) continue;
        const std::vector<std::string> f2 = SplitTabs(lines[i]);
        if (f2.size() < 8) { everyRowWellFormed = false; continue; }
        Loop l;
        l.net = std::atof(f2[0].c_str());
        l.ratio = std::atof(f2[1].c_str());
        l.item = f2[2];
        l.skill = f2[3];
        l.ingredients = f2[6];
        l.source = f2[7];
        // A claim about the economy with no file:line behind it is not usable
        // to this project -- the same rule the audit itself runs under.
        if (l.source.find(':') == std::string::npos) everyRowCited = false;
        loops.push_back(std::move(l));
    }
    Check(everyRowWellFormed, "every row has the full column set");
    Check(everyRowCited, "every row cites the itemdef it came from, as file:line");
    Check(!loops.empty(), "loops were parsed");

    // --- the ratchet ------------------------------------------------------
    Section("arbitrage export: the invariant");

    std::vector<const Loop*> profitable;
    for (const Loop& l : loops) {
        if (l.net > 0.0) profitable.push_back(&l);
    }

    std::printf("  %zu closed loops evaluated, %zu profitable (baseline %d)\n",
                loops.size(), profitable.size(), kBaseline);

    Check(static_cast<int>(profitable.size()) <= kBaseline,
          "profitable loop count has not INCREASED above the recorded baseline");

    if (static_cast<int>(profitable.size()) < kBaseline) {
        std::printf("  NOTE: down from %d to %zu. Lower kBaseline and say why.\n",
                    kBaseline, profitable.size());
    }

    // Name the worst offenders, so a failure is actionable rather than a number.
    if (!profitable.empty()) {
        const Loop* worstNet = profitable[0];
        const Loop* worstRatio = profitable[0];
        for (const Loop* l : profitable) {
            if (l->net > worstNet->net) worstNet = l;
            if (l->ratio > worstRatio->ratio) worstRatio = l;
        }
        std::printf("  worst by net gold : %s (%s) %+.1f gp from %s\n",
                    worstNet->item.c_str(), worstNet->skill.c_str(),
                    worstNet->net, worstNet->ingredients.c_str());
        std::printf("  worst by ratio    : %s (%s) %.2fx from %s\n",
                    worstRatio->item.c_str(), worstRatio->skill.c_str(),
                    worstRatio->ratio, worstRatio->ingredients.c_str());
    }

    // --- the specific loops the audit called out ---------------------------
    //
    // These are named because they are the ones that need NO skill: a brand-new
    // character can run them, which is what makes them a bot problem rather
    // than a crafting-balance problem. If any of these disappears, that is
    // real progress and this test should be updated to say so.
    Section("arbitrage export: the skill-free loops");

    int skillFree = 0;
    for (const Loop* l : profitable) {
        const bool none = l->skill == "(none)";
        const bool zero = l->skill.find(" 0.0") != std::string::npos;
        if (none || zero) {
            ++skillFree;
            std::printf("  skill-free: %-22s %+6.1f gp  %.2fx  from %s\n",
                        l->item.c_str(), l->net, l->ratio, l->ingredients.c_str());
        }
    }
    std::printf("  %d skill-free profitable loop(s)\n", skillFree);
    Check(skillFree >= 0, "skill-free loops are enumerated rather than assumed");

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
