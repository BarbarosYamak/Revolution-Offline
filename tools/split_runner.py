#!/usr/bin/env python3
"""Mechanically split src/life/Runner.cpp into per-goal-family translation units.

ZERO behaviour change is the whole point: this script only MOVES text. It never
rewrites a statement. The two textual edits it is allowed to make are

  1. `inline` added to free-function definitions that move into the shared
     header (otherwise every TU emits the same symbol and the link fails), and
  2. the anonymous namespace becomes the NAMED namespace `runner_detail`, with
     `using namespace runner_detail;` re-opened inside `uo::life` in every
     family TU so unqualified lookup in the method bodies is unchanged.

THE TRAP THIS SCRIPT EXISTS TO AVOID. Four tables in the old anonymous
namespace -- SeededCreatureDanger(), SeededTaming(), Pastures(), Tamables() --
are function-local statics filled once by Runner::Configure and read from
DoTameAnimal / DoMakeCloth / DoSurvive. Copy those accessors into a header and
each TU gets its OWN static: Configure fills Core.cpp's copy and Tame.cpp reads
an empty one, silently, with no compiler diagnostic. So the accessors and their
loaders are DECLARED in the header and DEFINED exactly once, in
RunnerShared.cpp.

Run from bot/uo-client:  python tools/split_runner.py
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src", "life", "Runner.cpp")
OUTDIR = os.path.join(ROOT, "src", "life", "runner")

# The anonymous-namespace preamble: `namespace {` .. `}  // namespace`, the
# constexpr tables, structs and small helpers every family reads.
ANON_OPEN = "namespace {"
ANON_CLOSE = "}  // namespace"

# Entities that must live in RunnerShared.cpp (one definition, one static).
STATEFUL = [
    "SeededCreatureDanger",
    "LoadSeededCreatureDanger",
    "SeededTaming",
    "Pastures",
    "LoadPastures",
    "Tamables",
    "LoadTamables",
]

# name -> family file. Names are `Runner::X` for methods, bare for free helpers.
FAMILY = {}


def _fam(fname, names):
    for n in names:
        FAMILY[n] = fname


_fam("Core.cpp", [
    "Runner::Runner", "Runner::~Runner", "Runner::LogLine", "Runner::Configure",
    "Runner::Observe", "Runner::ArmAxe", "Runner::MaintainBuildLocks",
    "Runner::SeedNewbieKnowledge", "Runner::LearnFromObservation",
    "Runner::IsDeadTarget", "Runner::IsUnreachable", "Runner::MarkUnreachable",
    "Runner::Checkpoint", "Runner::EndSession", "Runner::Tick",
    "Runner::LogGoalHistogram", "Runner::LogSpinIfDetected", "Runner::LogPlan",
    "Runner::LogErrandReason", "Runner::HandOff",
    "Runner::VetoTripOverSessionBudget", "Runner::LogGoalChange",
    "Runner::RunGoal", "Runner::RestTick", "Runner::DoExplore",
    "Runner::DoIdle", "Runner::DoTravel",
])
_fam("Survive.cpp", [
    "Runner::DoSurvive", "Runner::DoHeal", "Runner::DoRecoverCorpse",
])
_fam("Gear.cpp", [
    "ToolVendor", "kToolVendors", "VendorForTool", "Runner::DoGetTool",
    "Runner::DoBuyMount",
    "Runner::CutResurrectionRobe", "Runner::WearBasicClothing",
    "Runner::DoReplaceEquipment", "CreateFoodReagentShort",
    "Runner::DoGetFood", "Runner::LifeNeedsGraphic", "Runner::RoleOfGraphic",
    "ArmorFor", "Runner::HasBasicArmor", "Runner::MayWear",
    "Runner::DoUpgradeGear",
])
_fam("Bank.cpp", [
    "Runner::IssueBankItemMove", "Runner::SettleBankItemMove", "Runner::DoBank",
])
_fam("Economy.cpp", [
    "Runner::PlayersDeclined", "Runner::SellersDeclined",
    "Runner::MaterialSaleGateFor", "Runner::DoEarnGold",
    "Runner::MarketPlaceUsable", "Runner::AtMarketBank", "Runner::NearAnyBank",
    "Runner::ForgetBankedStock", "Runner::DoTradeWithPlayer",
    "Runner::DriveOpenTrade", "Runner::ResetTradeState", "SupplierTradeFor",
    "Runner::DoBuySupplies", "Runner::FetchCoinForPurchase",
])
_fam("Gather.cpp", [
    "Runner::DoGatherLogs", "Runner::DoSmelt", "FishInPack", "Runner::DoFish",
    "Runner::DoMine",
])
_fam("Cloth.cpp", [
    "Runner::DoMakeBandages", "WoolForShortfall", "Runner::ReachStation",
    "Runner::DoMakeCloth",
])
_fam("Craft.cpp", ["Runner::DoCraft"])
_fam("Train.cpp", [
    "Runner::DoTrainCombat", "Runner::DoTrainAtNpc", "Runner::BookHasSpell",
    "Runner::BookHasGraphic", "Runner::StandDownFromScrollShopping",
    "Runner::BuyScrollFrom", "Runner::PickPracticeSpell",
    "Runner::DoFillSpellbook", "Runner::DoPracticeSkill",
])
_fam("Tame.cpp", ["Runner::DoTameAnimal"])

# Free helpers used by more than one family: they go to RunnerShared.cpp and are
# declared in RunnerInternal.h.
SHARED_HELPERS = {
    # Fmt2 is called from Runner::LogGoalChange (Core) and DoMakeCloth (Cloth).
    "Fmt2": "std::string Fmt2(const char* fmt, ...);",
    # ProducingGoalFor is called from DoBuySupplies (Economy) and DoCraft.
    "ProducingGoalFor": "GoalKind ProducingGoalFor(const std::string& item);",
}

# --- generic C++ top-level chunker ------------------------------------------

_SIG = re.compile(
    r"^(?:[A-Za-z_~][\w:~<>,&*\s]*?)\b"
    r"(Runner::~?\w+|~?\w+)\s*\(")
_STRUCT = re.compile(r"^(?:struct|class|enum)\s+(\w+)")
_VAR = re.compile(r"^(?:inline\s+)?(?:constexpr|const|static)\b.*?\b(k\w+|\w+)\s*(?:\[|=|;)")


def strip_code(line, state):
    """Return the line with string/char literals and comments blanked out, so
    brace counting never trips over a `{` inside a comment or a "}" literal."""
    out = []
    i = 0
    n = len(line)
    while i < n:
        c = line[i]
        if state["block"]:
            if c == "*" and i + 1 < n and line[i + 1] == "/":
                state["block"] = False
                i += 2
                continue
            i += 1
            continue
        if c == "/" and i + 1 < n and line[i + 1] == "/":
            break
        if c == "/" and i + 1 < n and line[i + 1] == "*":
            state["block"] = True
            i += 2
            continue
        if c == '"' or c == "'":
            q = c
            i += 1
            while i < n:
                if line[i] == "\\":
                    i += 2
                    continue
                if line[i] == q:
                    i += 1
                    break
                i += 1
            continue
        out.append(c)
        i += 1
    return "".join(out)


def chunk(lines, lo, hi):
    """Split lines[lo:hi] into top-level chunks. A chunk ends at the line that
    returns brace depth to zero (or at a depth-0 `;` for a braceless decl), so
    every leading comment block travels with the definition that FOLLOWS it --
    which is how this file is written."""
    chunks = []
    state = {"block": False}
    depth = 0
    start = lo
    seen_code = False
    for i in range(lo, hi):
        code = strip_code(lines[i], state)
        if code.strip():
            seen_code = True
        for c in code:
            if c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
        if depth == 0 and seen_code:
            s = code.strip()
            if s.endswith("}") or s.endswith("};") or s.endswith(";"):
                chunks.append((start, i + 1))
                start = i + 1
                seen_code = False
    if start < hi:
        chunks.append((start, hi))
    return chunks


def head_line(seg):
    """Index within `seg` of the line that opens the definition: the first
    line that is neither blank, nor a comment, nor an indented continuation."""
    for i, s in enumerate(seg):
        t = s.rstrip("\r\n")
        if not t or t.startswith((" ", "\t", "//", "/*", "*", "#")):
            continue
        return i
    return None


def is_function_head(line):
    """True for `T Name(args) {` / `T Name(` and false for a table or struct.
    A definition head opens a parameter list before it ever assigns, so the
    test is: there is a `(` and no `=` in front of it. Tables in this file
    read `constexpr T kName[] = {` -- no parenthesis at all."""
    t = line.rstrip("\r\n")
    if t.startswith(("struct", "class", "enum", "using", "typedef", "inline")):
        return False
    p = t.find("(")
    if p < 0:
        return False
    return "=" not in t[:p]


def name_of(lines, a, b):
    """The identifier a chunk defines: first non-comment signature-ish line."""
    for i in range(a, b):
        s = lines[i].rstrip("\n")
        if not s or s.startswith("//") or s.startswith(" ") or s.startswith("\t"):
            continue
        if s.startswith("/*") or s.startswith("*"):
            continue
        if s.startswith("#"):
            continue
        m = re.search(r"\b(Runner::~?\w+)\s*\(", s)
        if m:
            return m.group(1)
        m = _STRUCT.match(s)
        if m:
            return m.group(1)
        m = _SIG.match(s)
        if m:
            return m.group(1)
        m = _VAR.match(s)
        if m:
            return m.group(1)
        if s.startswith("namespace {"):
            # anonymous helper block: name it after its first inner definition
            for j in range(i + 1, b):
                t = lines[j].rstrip("\n")
                if not t or t.startswith("//") or t.startswith("}"):
                    continue
                m = _SIG.match(t)
                if m:
                    return m.group(1)
            return "namespace"
    return None


def main():
    with open(SRC, "r", encoding="utf-8", newline="") as f:
        text = f.read()
    lines = text.splitlines(keepends=True)

    # locate the anonymous preamble and the closing of namespace uo::life
    a_open = next(i for i, l in enumerate(lines) if l.rstrip("\r\n") == ANON_OPEN)
    a_close = next(i for i, l in enumerate(lines)
                   if l.rstrip("\r\n") == ANON_CLOSE and i > a_open)
    life_close = max(i for i, l in enumerate(lines)
                     if l.rstrip("\r\n") == "}  // namespace uo::life")

    life_open = next(i for i, l in enumerate(lines)
                     if l.rstrip("\r\n") == "namespace uo::life {")
    includes = "".join(lines[:life_open])
    preamble = lines[a_open + 1:a_close]            # inside the anon namespace
    body_lo, body_hi = a_close + 1, life_close

    # ---- carve the preamble: stateful accessors/loaders out to the .cpp -----
    pre_chunks = chunk(preamble, 0, len(preamble))
    header_parts, shared_parts, decls = [], [], []
    for a, b in pre_chunks:
        nm = name_of(preamble, a, b)
        seg = "".join(preamble[a:b])
        if nm in STATEFUL:
            # keep the comment block with the definition in the .cpp; declare
            # only the signature in the header.
            sig = None
            for i in range(a, b):
                s = preamble[i].rstrip("\r\n")
                if _SIG.match(s) and s.endswith("{"):
                    sig = s[:-1].rstrip() + ";\n"
                    break
            if sig is None:
                # a pure forward declaration (plus the comment block it carries)
                # -- that belongs in the header verbatim.
                header_parts.append(seg)
                continue
            # The declaration keeps the ORIGINAL position in the header --
            # Pastures() returns std::vector<Pasture>&, so it cannot be hoisted
            # above `struct Pasture`. The comment block and the body travel
            # together into RunnerShared.cpp.
            header_parts.append(sig)
            shared_parts.append(seg)
        else:
            # A free FUNCTION definition in a header needs `inline` or every
            # family TU emits the symbol and the link fails. Tables and structs
            # must NOT get it: they stay exactly as they were written.
            out = list(preamble[a:b])
            h = head_line(out)
            if h is not None and is_function_head(out[h]):
                out[h] = "inline " + out[h]
            header_parts.append("".join(out))

    hdr = []
    hdr.append("// Generated by tools/split_runner.py -- the shared internals of the\n")
    hdr.append("// Runner goal families. Everything here came verbatim out of the old\n")
    hdr.append("// src/life/Runner.cpp anonymous namespace; the only edits are `inline`\n")
    hdr.append("// on the free functions and the namespace becoming named.\n")
    hdr.append("//\n")
    hdr.append("// The four TABLE ACCESSORS below hold function-local statics that\n")
    hdr.append("// Runner::Configure fills and other families read. They are declared here\n")
    hdr.append("// and DEFINED ONCE in RunnerShared.cpp on purpose: an inline/header copy\n")
    hdr.append("// would give every translation unit its own empty table.\n")
    hdr.append("#pragma once\n\n")
    hdr.append(includes)
    hdr.append("namespace uo::life {\n")
    hdr.append("namespace runner_detail {\n")
    hdr.append("\n// --- defined once in RunnerShared.cpp --------------------------------\n")
    for d in SHARED_HELPERS.values():
        hdr.append(d + "\n")
    hdr.append("\n")
    hdr.append("".join(header_parts))
    hdr.append("\n}  // namespace runner_detail\n")
    hdr.append("}  // namespace uo::life\n")

    # ---- carve the method body ---------------------------------------------
    body_chunks = chunk(lines, body_lo, body_hi)
    files = {}
    unknown = []
    for a, b in body_chunks:
        nm = name_of(lines, a, b)
        seg = "".join(lines[a:b])
        if nm in SHARED_HELPERS:
            # Fmt2 sat in its own `namespace { ... }` in the old single TU. In
            # RunnerShared.cpp that would make it internal-linkage and the
            # families could not call it, so the wrapper is unwrapped here --
            # runner_detail already gives it the same "not part of the public
            # surface" meaning.
            body = lines[a:b]
            h = head_line(body)
            if h is not None and body[h].rstrip("\r\n") == "namespace {":
                last = max(i for i in range(h, len(body))
                           if body[i].rstrip("\r\n") == "}  // namespace")
                body = body[:h] + body[h + 1:last] + body[last + 1:]
            shared_parts.append("".join(body))
            continue
        if nm is None and not seg.strip():
            continue                     # trailing separator blank line
        tgt = FAMILY.get(nm)
        if tgt is None:
            unknown.append((nm, a + 1, b))
            continue
        files.setdefault(tgt, []).append(seg)

    if unknown:
        for nm, a, b in unknown:
            print("UNASSIGNED %r at line %d-%d" % (nm, a, b))
        raise SystemExit("refusing to write a partial split")

    os.makedirs(OUTDIR, exist_ok=True)
    with open(os.path.join(OUTDIR, "RunnerInternal.h"), "w",
              encoding="utf-8", newline="") as f:
        f.write("".join(hdr))

    def tu(body, extra=""):
        return ('#include "RunnerInternal.h"\n\n'
                "namespace uo::life {\n"
                "// The families were one translation unit until the split; the\n"
                "// using-directive keeps unqualified lookup in these bodies identical\n"
                "// to what the old anonymous namespace gave them.\n"
                "using namespace runner_detail;\n\n"
                + extra + body + "\n}  // namespace uo::life\n")

    with open(os.path.join(OUTDIR, "RunnerShared.cpp"), "w",
              encoding="utf-8", newline="") as f:
        f.write('#include "RunnerInternal.h"\n\n'
                "namespace uo::life {\n"
                "namespace runner_detail {\n\n"
                + "".join(shared_parts)
                + "\n}  // namespace runner_detail\n"
                  "}  // namespace uo::life\n")

    written = []
    for name, parts in sorted(files.items()):
        p = os.path.join(OUTDIR, name)
        with open(p, "w", encoding="utf-8", newline="") as f:
            f.write(tu("".join(parts)))
        written.append(p)

    for p in written + [os.path.join(OUTDIR, "RunnerShared.cpp"),
                        os.path.join(OUTDIR, "RunnerInternal.h")]:
        with open(p, "r", encoding="utf-8") as f:
            n = len(f.readlines())
        print("%6d  %s" % (n, os.path.relpath(p, ROOT)))


if __name__ == "__main__":
    main()
