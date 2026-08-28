#!/usr/bin/env python3
"""Launch a mixed-profession fleet in ONE uo_client process.

WHY ONE PROCESS

Sixteen separate processes each load their own MULs, atlas and navgrid --
hundreds of megabytes of identical read-only data, sixteen times. The
multi-session path in main.cpp exists precisely so world knowledge is shared
and every session still gets its own socket, parser, world state, movement
sequence, keepalive and log file.

WHY THE POPULATION LOOKS LIKE THIS

M7's whole claim is interdependence, and one bot cannot demonstrate it. The
mix is chosen so the catalogue's real production chains have both ends present
at the same time:

    lumberjack/carpenter  --(i_log)-->      miner_smith
    lumberjack/carpenter  --(i_scroll_blank)--> mage

Producers outnumber consumers deliberately: a chain starved at the top just
looks like idle consumers, and that failure is much harder to read than a
surplus.

The tamer is absent on purpose. [NEWBIE TAMING] is empty on this shard -- it
hands a tamer nothing at all -- so a tamer would spawn with no tools and no
way to begin. That is recorded in docs/M5.md, not worked around here.

CREDENTIALS

Passwords are read from the shard's own account file at launch and passed as
UO_BOT_PASS_<TAG> environment variables. They are never written to a file in
the repository and never appear on the command line, because the session spec
leaves the password field empty and main.cpp falls back to the per-tag
environment variable.
"""

import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
BOT = os.path.join(ROOT, "bot", "uo-client")
ACCOUNTS = os.path.join(ROOT, "runtime", "accounts", "sphereaccu.scp")

# (profession id, how many). Producers first so the chain fills from the top.
POPULATION = [
    ("lumberjack_swordsman", 5),   # logs, boards, blank scrolls, clubs
    ("miner_smith",          4),   # eats ore + logs -> ingots, spears
    ("mage",                 4),   # eats blank scrolls + reagents -> scrolls
    ("alchemist",            3),   # potions
]

# From revolution_offline_namebook.md, which exists so synthetic characters are
# style-authentic without impersonating former players.
NAMES = [
    "Itheth", "Rhalazar", "Rhalvar", "Zaror", "Loradan",
    "Dorthor", "Aeryn", "Dravazar", "Galrin", "Ravan",
    "Beleth", "Elvrin", "Ardor", "Voris", "Calar",
    "Malazar", "Baelos", "Vorath", "Galthor", "Rhalthor",
]


def free_accounts():
    """Accounts on this shard with no character yet, in file order."""
    with open(ACCOUNTS, encoding="utf-8", errors="replace") as fh:
        text = fh.read()
    out = []
    for m in re.finditer(r"\n\[([A-Za-z0-9_]+)\]\n(.*?)(?=\n\[|\Z)", text, re.S):
        name, body = m.group(1), m.group(2)
        if name.lower() in ("admin", "observer"):
            continue
        if "CHARUID=" in body:
            continue          # already has a character; leave it alone
        pw = re.search(r"PASSWORD=(\S+)", body)
        if not pw:
            continue
        out.append((name, pw.group(1)))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--minutes", type=int, default=45,
                    help="session length before each bot logs out cleanly")
    ap.add_argument("--tag", default="fleet",
                    help="log directory under run_m7/")
    ap.add_argument("--size", type=int, default=0,
                    help="cap the fleet (0 = the full POPULATION table)")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    plan = []
    for prof, count in POPULATION:
        plan.extend([prof] * count)
    if args.size:
        plan = plan[:args.size]

    accounts = free_accounts()
    if len(accounts) < len(plan):
        sys.exit(f"need {len(plan)} free accounts, found {len(accounts)}")
    if len(NAMES) < len(plan):
        sys.exit(f"need {len(plan)} names, have {len(NAMES)}")

    outdir = os.path.join(BOT, "run_m7")
    os.makedirs(outdir, exist_ok=True)

    env = dict(os.environ)
    sessions = []
    roster = []
    for i, prof in enumerate(plan):
        acct, pw = accounts[i]
        name = NAMES[i]
        tag = name
        # user:pass:char:scenario:tag:profession -- the password field is left
        # EMPTY on purpose; it travels in the environment instead so it never
        # reaches a process listing.
        sessions.append(f"{acct}::{name}::{tag}:{prof}")
        env["UO_BOT_PASS_" + tag.upper()] = pw
        roster.append((name, acct, prof))

    cmd = [
        os.path.join(BOT, "build-m1", "uo_client.exe"),
        "--headless",
        "--host", "127.0.0.1", "--port", "2593",
        "--create-char",
        "--autonomous",
        "--bot-data", os.path.join(BOT, "bot_data"),
        "--life-minutes", str(args.minutes),
        "--mul-dir", os.path.join(ROOT, "runtime", "mul"),
        "--data-dir", os.path.join(BOT, "data"),
        "--log", os.path.join(outdir, args.tag + ".log"),
    ]
    for spec in sessions:
        cmd += ["--session", spec]

    print(f"fleet of {len(roster)}:")
    width = max(len(n) for n, _, _ in roster)
    for name, acct, prof in roster:
        print(f"  {name:<{width}}  {prof:<22} {acct}")

    if args.dry_run:
        print("\n(dry run; not launching)")
        return 0

    console = os.path.join(outdir, args.tag + ".console.txt")
    errf = os.path.join(outdir, args.tag + ".err.txt")
    print(f"\nconsole -> {console}")
    with open(console, "w") as out, open(errf, "w") as err:
        proc = subprocess.Popen(cmd, stdout=out, stderr=err, env=env, cwd=BOT)
    print(f"pid {proc.pid}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
