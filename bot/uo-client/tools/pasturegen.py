#!/usr/bin/env python3
"""Derive sheep pastures from the live Sphere world save.

The tailor's cloth chain starts at a sheep, and a bot may not be told where
sheep are by a hard-coded constant -- the owner's rule is that spawn
locations come from the world save / atlas, never from reasoning.  This
script reads runtime/save/sphereworld.scp, takes every WORLDCHAR whose
defname begins with `c_sheep`, clusters them by proximity, and writes one
row per flock to data/revolution_pastures.tsv.

    python tools/pasturegen.py [--save-dir DIR] [--out FILE]

Output columns (tab separated, one header line):

    x  y  map  count  radius  label

`count` is how many sheep the save holds inside the cluster and `radius` is
the largest Chebyshev distance from the cluster centre to one of its
members, so the runner can pick an arrive radius that actually lands
inside the flock.  Rows are sorted by count, biggest flock first: a bot
with no local knowledge should walk to the place with the most sheep.

Pure stdlib, deterministic, no heuristics beyond the join distance below.
"""

import argparse
import os
import re
import sys

# Two sheep within this many tiles of each other belong to the same flock.
# 24 is the width of a screen and a bit: close enough that a character
# standing at the centre can walk to any of them without a fresh journey,
# far enough that the three real Yew flocks do not split into ones and twos.
JOIN_DIST = 24
# A single wandering sheep is not a pasture. Below this a cluster is
# dropped: walking across the map for one animal that has probably moved
# since the save is a wasted trip.
MIN_FLOCK = 4

SECTION = re.compile(r"(?m)^\[(worldchar\s+c_sheep\w*)\]", re.IGNORECASE)
POS = re.compile(r"(?m)^P=(-?\d+),(-?\d+)(?:,(-?\d+))?(?:,(\d+))?")


def read_sheep(path):
    with open(path, encoding="latin-1") as fh:
        text = fh.read()
    out = []
    for m in SECTION.finditer(text):
        body = text[m.end():text.find("\n[", m.end()) if text.find("\n[", m.end()) >= 0 else len(text)]
        p = POS.search(body)
        if not p:
            continue
        out.append((int(p.group(1)), int(p.group(2)), int(p.group(4) or 0)))
    return out


def cluster(points):
    """Single-link clustering at JOIN_DIST, Chebyshev metric."""
    unassigned = list(points)
    flocks = []
    while unassigned:
        seed = unassigned.pop()
        members = [seed]
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
        flocks.append(members)
    return flocks


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    repo = os.path.abspath(os.path.join(here, "..", "..", ".."))
    ap = argparse.ArgumentParser()
    ap.add_argument("--save-dir",
                    default=os.path.join(repo, "runtime", "save"))
    ap.add_argument("--out",
                    default=os.path.join(here, "..", "data",
                                         "revolution_pastures.tsv"))
    args = ap.parse_args()

    path = os.path.join(args.save_dir, "sphereworld.scp")
    if not os.path.exists(path):
        sys.exit("no world save at %s" % path)
    sheep = read_sheep(path)
    if not sheep:
        sys.exit("no c_sheep* WORLDCHAR sections in %s" % path)

    rows = []
    for members in cluster(sheep):
        if len(members) < MIN_FLOCK:
            continue
        cx = sum(p[0] for p in members) // len(members)
        cy = sum(p[1] for p in members) // len(members)
        radius = max(max(abs(p[0] - cx), abs(p[1] - cy)) for p in members)
        rows.append((cx, cy, members[0][2], len(members), radius))
    rows.sort(key=lambda r: (-r[3], r[0], r[1]))

    with open(args.out, "w", encoding="ascii", newline="\n") as fh:
        fh.write("x\ty\tmap\tcount\tradius\tlabel\n")
        for x, y, mp, n, r in rows:
            fh.write("%d\t%d\t%d\t%d\t%d\tflock of %d\n" % (x, y, mp, n, r, n))
    print("%d sheep -> %d pastures -> %s" % (len(sheep), len(rows), args.out))


if __name__ == "__main__":
    main()
