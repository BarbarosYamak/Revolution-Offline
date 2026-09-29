#!/usr/bin/env python3
"""Export the shard's Magery spell table to data/revolution_spells.tsv.

GROUND TRUTH is runtime/scripts/spells/spells_magery.scp -- the file Sphere
itself reads.  Nothing here is typed by hand: spell number, name, reagents,
mana, the SKILLREQ that gates the cast and the FLAGS that say whether a spell
may be aimed at oneself all come out of that file.

Why a file and not a C++ table: the practice goal used to carry a hand-picked
list of twelve "self-safe" spells (include/uo/spellcast.h SelfSafeSpells), so a
mage practised with whatever those twelve happened to be instead of with the
whole book.  Owner ruling 2026-09-02: "for mage to cast there are lots of
skills, don't hard code Create Food."

CIRCLE is derived, not assumed: every [SPELL] block carries
`SKILLREQ=MAGERY <n>` and the values run 10.0, 20.0 ... 80.0 with exactly eight
spells at each -- so circle = SKILLREQ / 10.  (Verified 2026-09-02:
`grep -i "^SKILLREQ" spells_magery.scp | sort | uniq -c` = 8 of each.)

Usage:
    python tools/spellgen.py [--scp <path>] [--out <path>] [--check]

--check prints what WOULD be written and exits non-zero if it differs from the
file on disk, so a stale export is visible without rewriting it.
"""

import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
DEFAULT_SCP = os.path.join(REPO, "runtime", "scripts", "spells", "spells_magery.scp")
DEFAULT_OUT = os.path.join(HERE, "..", "data", "revolution_spells.tsv")

# Sphere script files are case-insensitive; every key is compared lowercased.
KEYS = ("defname", "name", "resources", "flags", "manause", "skillreq")


def strip_comment(text):
    """Sphere comments run to end of line.  This matters: [SPELL 7] reads
    FLAGS=...|spellflag_good//|spellflag_playeronly -- playeronly is COMMENTED
    OUT there, and a parser that ignores `//` would grant a flag the server
    does not."""
    cut = text.find("//")
    if cut >= 0:
        text = text[:cut]
    return text.strip()


def parse(path):
    spells = []
    cur = None
    with open(path, "r", encoding="latin-1") as fh:
        for raw in fh:
            line = strip_comment(raw)
            if line.startswith("["):
                head = line[1:].split("]")[0].strip()
                parts = head.split()
                if len(parts) == 2 and parts[0].lower() == "spell" and parts[1].isdigit():
                    cur = {"spell": int(parts[1])}
                    spells.append(cur)
                else:
                    cur = None
                continue
            if cur is None or "=" not in line:
                continue
            key, _, val = line.partition("=")
            key = key.strip().lower()
            if key in KEYS and key not in cur:
                cur[key] = val.strip()
    return spells


def row_for(s):
    skillreq = s.get("skillreq", "")
    circle = 0
    minskill = 0
    parts = skillreq.split()
    if len(parts) == 2 and parts[0].lower() == "magery":
        try:
            minskill = int(round(float(parts[1]) * 10))
        except ValueError:
            minskill = 0
        circle = minskill // 100
    flags = "|".join(
        f.strip().lower() for f in s.get("flags", "").split("|") if f.strip()
    )
    reagents = ",".join(
        r.strip().lower() for r in s.get("resources", "").split(",") if r.strip()
    )
    mana = s.get("manause", "0")
    try:
        mana = int(float(mana))
    except ValueError:
        mana = 0
    return [
        str(s["spell"]),
        s.get("defname", "").lower(),
        s.get("name", ""),
        str(circle),
        str(minskill),
        str(mana),
        flags,
        reagents,
    ]


HEADER = ["spell", "defname", "name", "circle", "minskill", "mana", "flags", "reagents"]


def render(spells):
    out = ["\t".join(HEADER)]
    for s in sorted(spells, key=lambda x: x["spell"]):
        if "name" not in s:
            continue
        out.append("\t".join(row_for(s)))
    return "\n".join(out) + "\n"


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--scp", default=DEFAULT_SCP)
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args(argv)

    spells = parse(args.scp)
    if not spells:
        print("no [SPELL n] blocks in %s" % args.scp, file=sys.stderr)
        return 2
    text = render(spells)
    if args.check:
        try:
            with open(args.out, "r", encoding="utf-8") as fh:
                have = fh.read()
        except OSError:
            have = ""
        if have != text:
            print("STALE: %s does not match %s" % (args.out, args.scp), file=sys.stderr)
            return 1
        print("up to date: %d spells" % len(spells))
        return 0
    with open(args.out, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)
    print("wrote %s (%d spells)" % (args.out, len(spells)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
