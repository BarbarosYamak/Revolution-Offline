#!/usr/bin/env python
"""Grade one completed session against its profession's basic life contract.

Usage:
    python tools/grade_archetype.py console.txt state_before.json state_after.json --family mage

This is deliberately narrower than grade_life.py.  A contract asks whether a
new character safely completed the work loop that defines *that* profession.
It does not award unrelated exploration, idle goals, or generic bank growth.
"""
import argparse
import json
import re
import sys


# Each primary stage is OR-of-regexes.  The expressions are emitted by the
# runner, not inferred from packet chatter.  Keep this table in sync with
# docs/ARCHETYPE_CERTIFICATION.md whenever a profession loop changes.
CONTRACTS = {
    "miner_smith": {
        "label": "mine ore, then turn it into a smith output",
        "stages": (("MINE", (r"mine: ORE at",)),
                   ("SMITH", (r"smelt: .*ingot", r"craft: made i_(dagger|spear_short|cutlass)\b"))),
    },
    "lumberjack_swordsman": {
        "label": "gather logs, then make a usable wood output",
        "stages": (("GATHER_LOGS", (r"first logs gathered at", r"session_summary .* logs=\+[1-9]")),
                   ("WOOD_OUTPUT", (r"craft: made i_(board|club|bow)\b", r"earn_gold: sold .* i_(log|board|club|bow)\b"))),
    },
    "full_crafter": {
        "label": "mine an input, then make or sell a crafted output",
        "stages": (("MINE", (r"mine: ORE at",)),
                   ("CRAFT_OR_SELL", (r"craft: made i_", r"earn_gold: sold .* i_"))),
    },
    "fisher": {
        "label": "catch fish, then cook or sell it",
        "stages": (("CATCH", (r"fish: caught one at",)),
                   ("COOK_OR_SELL", (r"craft: made i_fish_cut_cooked\b", r"earn_gold: sold .* i_fish"))),
    },
    "mage": {
        "label": "safely kill a target while maintaining a defensive buff",
        "stages": (("KILL", (r"hunt: confirmed kill",)),
                   ("BUFF", (r"caster_buff: cast=(Night Sight|Reactive Armor|Protection|Bless|Magic Reflection)",))),
    },
    "warlock": {
        "label": "safely kill a target while maintaining a defensive buff",
        "stages": (("KILL", (r"hunt: confirmed kill",)),
                   ("BUFF", (r"caster_buff: cast=(Night Sight|Reactive Armor|Protection|Bless|Magic Reflection)",))),
    },
    "fencer": {
        "label": "safely complete a combat kill",
        "stages": (("KILL", (r"hunt: confirmed kill",)),),
    },
    "macer": {
        "label": "safely complete a combat kill",
        "stages": (("KILL", (r"hunt: confirmed kill",)),),
    },
    "pk": {
        "label": "safely complete a combat kill",
        "stages": (("KILL", (r"hunt: confirmed kill",)),),
    },
    "archer": {
        "label": "make ammunition or a bow, then complete a combat kill",
        "stages": (("RANGED_OUTPUT", (r"craft: made i_(arrow_shaft|arrow|bow)\b",)),
                   ("KILL", (r"hunt: confirmed kill",))),
    },
    "scribe": {
        "label": "make a spell scroll",
        "stages": (("SCROLL", (r"craft: made i_scroll_",)),),
    },
    "alchemist": {
        "label": "make a potion",
        "stages": (("POTION", (r"craft: made i_potion_",)),),
    },
    "mage_blacksmith": {
        "label": "mine ore, then turn it into a smith output",
        "stages": (("MINE", (r"mine: ORE at",)),
                   ("SMITH", (r"smelt: .*ingot", r"craft: made i_(dagger|spear_short|cutlass)\b"))),
    },
    "tailor": {
        "label": "weave cloth, then make a tailored output",
        "stages": (("WEAVE", (r"bandages: weaving yarn into cloth at a loom",)),
                   ("TAILOR_OUTPUT", (r"craft: made i_(sash|robe|leather_tunic)\b",))),
    },
    "merchant_tinker": {
        "label": "make a tinker output",
        "stages": (("TINKER_OUTPUT", (r"craft: made i_(gears|lockpick|tinker_tools|pickaxe|scissors|sewing_kit|pen_and_ink|barrel_tap|barrel_hoops|keg_potion)\b",)),),
    },
    "tamer": {
        "label": "successfully tame an animal",
        "stages": (("TAME", (r"tame: success ", r"goal_completed=TAME_ANIMAL progress=[1-9]\d*")),),
    },
    # There is intentionally no weak fallback.  See the matching roadmap
    # entry: the client has no map/dig/chest goal or corresponding evidence.
    "treasure_hunter": {
        "blocked": "no cartography, digging, or treasure-chest loop is implemented",
    },
}


def hits(lines, patterns):
    rx = re.compile("(?:" + "|".join(patterns) + ")")
    return [(number, line) for number, line in enumerate(lines, 1) if rx.search(line)]


class Report:
    def __init__(self, family, console):
        self.family = family
        self.console = console
        self.rows = []

    def add(self, rule, status, detail, evidence=()):
        locations = ",".join(str(number) for number, _ in list(evidence)[:5]) or "-"
        self.rows.append((rule, status, detail, locations))

    def emit(self):
        width = max(len(rule) for rule, *_ in self.rows)
        print("ARCHETYPE-CONTRACT  family=%s  console=%s" % (self.family, self.console))
        print("-" * 96)
        for rule, status, detail, locations in self.rows:
            print("%-*s  %-7s  %-55s  lines: %s" %
                  (width, rule, status, detail[:55], locations))
        print("-" * 96)
        blocked = [rule for rule, status, *_ in self.rows if status == "BLOCKED"]
        failed = [rule for rule, status, *_ in self.rows if status == "FAIL"]
        passed = sum(status == "PASS" for _, status, *_ in self.rows)
        if blocked:
            verdict = "BLOCKED"
        elif failed:
            verdict = "FAIL"
        else:
            verdict = "PASS"
        print("VERDICT: %s (%d/%d rows passing)" % (verdict, passed, len(self.rows)))
        if failed:
            print("FAILING RULES: " + " ".join(failed))
        if blocked:
            print("BLOCKED RULES: " + " ".join(blocked))
        return {"PASS": 0, "FAIL": 1, "BLOCKED": 2}[verdict]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("console")
    parser.add_argument("state_before")
    parser.add_argument("state_after")
    parser.add_argument("--family", required=True, choices=sorted(CONTRACTS))
    args = parser.parse_args()

    # State snapshots remain required so each contract is tied to one complete
    # session, and so callers cannot accidentally grade a running process.
    for state in (args.state_before, args.state_after):
        with open(state, encoding="utf-8") as handle:
            json.load(handle)
    with open(args.console, encoding="utf-8", errors="replace") as handle:
        lines = handle.read().splitlines()

    contract = CONTRACTS[args.family]
    report = Report(args.family, args.console)
    if "blocked" in contract:
        report.add("PRIMARY_LOOP", "BLOCKED", contract["blocked"])
        raise SystemExit(report.emit())

    deaths = hits(lines, (r"event death_location:", r"\[life\] disengage=died\b"))
    report.add("SAFE_NO_DEATH", "PASS" if not deaths else "FAIL",
               "death events %d" % len(deaths), deaths)
    spins = hits(lines, (r"goal_spinning=",))
    report.add("SAFE_NO_SPIN", "PASS" if not spins else "FAIL",
               "goal-spin events %d" % len(spins), spins)
    logout = hits(lines, (r"event logout_complete: acked",))
    report.add("CLEAN_LOGOUT", "PASS" if logout else "FAIL",
               "acknowledged logout x%d" % len(logout), logout)
    for stage, patterns in contract["stages"]:
        evidence = hits(lines, patterns)
        report.add("PRIMARY_" + stage, "PASS" if evidence else "FAIL",
                   contract["label"], evidence)
    raise SystemExit(report.emit())


if __name__ == "__main__":
    main()
