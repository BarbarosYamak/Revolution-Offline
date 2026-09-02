#!/usr/bin/env python3
"""Derive TAMABLE-CREATURE clusters from the live Sphere world save.

Rhea's taming goal walked to data/revolution_pastures.tsv, and that table is
sheep only: it was generated for the tailor's wool chain, every flock sits in
the Yew farmland, and Yew is not a legal home city.  A tamer at Taming 50 can
take far more than a sheep -- horses, cows, pigs, goats, dogs, hinds, rats --
and most of those graze within a short walk of Britain.

"rhea can tame a lot of things not just sheep" (project owner, 2026-09-02).

This reads the shard's own save (never a reasoned-about coordinate: the
owner's standing rule, broken four times before -- see the bot-brain memory
`scenario-constants-from-evidence`), keeps every WORLDCHAR whose chardef
carries a TAMING requirement in data/revolution_creatures.tsv, clusters them
by species and proximity, and writes data/revolution_tamables.tsv.

    python tools/tamablegen.py [--save-dir DIR] [--out FILE] [--summary]

Output columns (tab separated, one header line):

    x  y  map  count  radius  label  defname  taming_req

One row is one HERD OF ONE KIND: clustering per defname keeps `taming_req`
meaningful, because a cow and a horse standing in the same field are not
interchangeable to a tamer whose skill sits between the two requirements.

Save layout follows tools/world_query.py (repo root): characters live in
sphereworld.scp and spherechars.scp, and a section's `P=` is a world
position only when it has no `CONT=` parent.

Pure stdlib, deterministic, no heuristics beyond the two constants below.
"""

import argparse
import os
import re
import sys

# Two animals of the same kind within this many tiles belong to the same herd.
# Smaller than pasturegen's 24 because this table is used to AIM a walk: the
# runner arrives at the centre and scans 12 tiles, so a cluster wider than
# that would advertise animals the arrival cannot see.
JOIN_DIST = 15
# Two is a herd. A tamer only needs one animal to succeed on, but a lone
# spawn that has wandered since the save turns the trip into nothing; two
# gives the walk a second chance without demanding a whole flock.
MIN_HERD = 2

SAVE_FILES = ["sphereworld.scp", "spherechars.scp"]

SECTION = re.compile(r"(?mi)^\[worldchar\s+([A-Za-z0-9_]+)\s*\]")
POS = re.compile(r"(?mi)^P=(-?\d+),(-?\d+)(?:,(-?\d+))?(?:,(\d+))?")
CONT = re.compile(r"(?mi)^CONT=")
# Atlas REGION row: REGION <defname> <kind> <n> <x> <y> <z> <city> <label>
ATLAS_REGION = re.compile(r"^REGION\t([^\t]*)\t([^\t]*)\t[^\t]*\t"
                          r"(-?\d+)\t(-?\d+)\t(-?\d+)\t([^\t]*)\t")


def read_tamable_defs(path):
    """defname -> (display name, taming requirement) for tamable chardefs."""
    out = {}
    with open(path, encoding="latin-1") as fh:
        first = True
        for line in fh:
            if first:
                first = False
                continue
            cols = line.rstrip("\n").split("\t")
            if len(cols) < 8:
                continue
            try:
                req = float(cols[7])
            except ValueError:
                continue
            if req < 0.0:
                continue                      # not tamable at all
            out[cols[0].strip().lower()] = (cols[1].strip(), req)
    return out


def read_chars(save_dir, tamable):
    """[(defname, x, y, map)] for every saved tamable character."""
    found = []
    for name in SAVE_FILES:
        path = os.path.join(save_dir, name)
        if not os.path.exists(path):
            continue
        with open(path, encoding="latin-1") as fh:
            text = fh.read()
        for m in SECTION.finditer(text):
            defname = m.group(1).lower()
            if defname not in tamable:
                continue
            end = text.find("\n[", m.end())
            body = text[m.end():end if end >= 0 else len(text)]
            if CONT.search(body):             # inside a container: not a place
                continue
            p = POS.search(body)
            if not p:
                continue
            found.append((defname, int(p.group(1)), int(p.group(2)),
                          int(p.group(4) or 0)))
    return found


def cluster(points):
    """Single-link clustering at JOIN_DIST, Chebyshev metric, per map."""
    unassigned = list(points)
    herds = []
    while unassigned:
        members = [unassigned.pop()]
        changed = True
        while changed:
            changed = False
            rest = []
            for p in unassigned:
                if any(p[2] == q[2] and
                       max(abs(p[0] - q[0]), abs(p[1] - q[1])) <= JOIN_DIST
                       for q in members):
                    members.append(p)
                    changed = True
                else:
                    rest.append(p)
            unassigned = rest
        herds.append(members)
    return herds


def city_centres(atlas_path):
    """City -> (x, y) centroid of its atlas REGION rows, map 0 only."""
    acc = {}
    if not os.path.exists(atlas_path):
        return {}
    with open(atlas_path, encoding="latin-1") as fh:
        for line in fh:
            m = ATLAS_REGION.match(line)
            if not m:
                continue
            city = m.group(6).strip()
            if not city:
                continue
            x, y = int(m.group(3)), int(m.group(4))
            sx, sy, n = acc.get(city, (0, 0, 0))
            acc[city] = (sx + x, sy + y, n + 1)
    return {c: (sx // n, sy // n) for c, (sx, sy, n) in acc.items() if n}


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.abspath(os.path.join(here, "..", "..", ".."))
    data = os.path.join(here, "..", "data")
    ap = argparse.ArgumentParser()
    ap.add_argument("--save-dir", default=os.path.join(repo, "runtime", "save"))
    ap.add_argument("--creatures",
                    default=os.path.join(data, "revolution_creatures.tsv"))
    ap.add_argument("--atlas",
                    default=os.path.join(data, "revolution_atlas.txt"))
    ap.add_argument("--out",
                    default=os.path.join(data, "revolution_tamables.tsv"))
    ap.add_argument("--summary", action="store_true",
                    help="print clusters within 150 tiles of each city")
    args = ap.parse_args()

    if not os.path.exists(args.creatures):
        sys.exit("no creature table at %s" % args.creatures)
    tamable = read_tamable_defs(args.creatures)
    if not tamable:
        sys.exit("no tamable chardefs in %s" % args.creatures)

    chars = read_chars(args.save_dir, tamable)
    if not chars:
        sys.exit("no tamable WORLDCHAR sections under %s" % args.save_dir)

    by_def = {}
    for defname, x, y, mp in chars:
        by_def.setdefault(defname, []).append((x, y, mp))

    rows = []
    for defname, pts in by_def.items():
        label, req = tamable[defname]
        for members in cluster(pts):
            if len(members) < MIN_HERD:
                continue
            cx = sum(p[0] for p in members) // len(members)
            cy = sum(p[1] for p in members) // len(members)
            radius = max(max(abs(p[0] - cx), abs(p[1] - cy)) for p in members)
            rows.append((cx, cy, members[0][2], len(members), radius,
                         "%s x%d" % (label, len(members)), defname, req))
    # Hardest-first inside a species-blind sort would be wrong: the runner
    # picks by skill window and distance itself. Sort for a stable, readable
    # file: biggest herd first, then position.
    rows.sort(key=lambda r: (-r[3], r[0], r[1], r[6]))

    with open(args.out, "w", encoding="ascii", newline="\n") as fh:
        fh.write("x\ty\tmap\tcount\tradius\tlabel\tdefname\ttaming_req\n")
        for x, y, mp, n, r, label, defname, req in rows:
            fh.write("%d\t%d\t%d\t%d\t%d\t%s\t%s\t%.1f\n"
                     % (x, y, mp, n, r, label, defname, req))
    print("%d tamable creatures of %d kinds -> %d clusters -> %s"
          % (len(chars), len(by_def), len(rows), args.out))

    if args.summary:
        centres = city_centres(args.atlas)
        for city in sorted(centres):
            cx, cy = centres[city]
            near = [r for r in rows
                    if r[2] == 0 and max(abs(r[0] - cx), abs(r[1] - cy)) <= 150]
            if not near:
                continue
            kinds = {}
            for r in near:
                kinds[r[6]] = kinds.get(r[6], 0) + r[3]
            print("%-14s centre %5d,%-5d  %3d clusters  %s"
                  % (city, cx, cy, len(near),
                     ", ".join("%s(req %.1f) x%d"
                               % (tamable[d][0], tamable[d][1], n)
                               for d, n in sorted(kinds.items(),
                                                  key=lambda kv: -kv[1])[:8])))


if __name__ == "__main__":
    main()
