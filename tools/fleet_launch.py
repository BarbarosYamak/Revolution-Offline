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
    ("lumberjack_swordsman", 6),   # logs, boards, blank scrolls, clubs
    ("miner_smith",          5),   # eats ore + logs -> ingots, spears
    ("mage",                 5),   # eats blank scrolls + reagents -> scrolls
    ("alchemist",            4),   # potions
]

# From revolution_offline_namebook.md, which exists so synthetic characters are
# style-authentic without impersonating former players.
NAMES = [
    "Itheth", "Rhalazar", "Rhalvar", "Zaror", "Loradan",
    "Dorthor", "Aeryn", "Dravazar", "Galrin", "Ravan",
    "Beleth", "Elvrin", "Ardor", "Voris", "Calar",
    "Malazar", "Baelos", "Vorath", "Galthor", "Rhalthor",
]


ROSTER = os.path.join(BOT, "run_m7", "roster.tsv")


def load_roster():
    """The fleet as it was last launched: name -> (account, password, profession).

    THE ROSTER MUST BE STABLE. free_accounts() skips accounts that already have
    a character, so without this the SECOND launch would exclude every bot the
    FIRST launch created and hand back a completely different population --
    twenty new strangers instead of the same twenty lives a day older. That
    would quietly destroy the one thing M4 exists to prove.
    """
    if not os.path.exists(ROSTER):
        return []
    out = []
    with open(ROSTER, encoding="utf-8") as fh:
        for line in fh:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) == 3:
                out.append(tuple(parts))
    return out


def save_roster(rows):
    with open(ROSTER, "w", encoding="utf-8") as fh:
        fh.write("# name\taccount\tprofession -- the fleet identity.\n")
        fh.write("# Stable by design: these accounts have characters now, so\n")
        fh.write("# free_accounts() would skip them and the next launch would\n")
        fh.write("# be twenty strangers instead of the same twenty lives.\n")
        for name, acct, prof in rows:
            fh.write(f"{name}\t{acct}\t{prof}\n")


def account_password(acct):
    """The password for one account, read fresh from the shard's own file.

    Reads it at launch rather than caching it in the roster, so a password
    changed on the shard takes effect and no secret is ever written into
    run_m7/roster.tsv.
    """
    with open(ACCOUNTS, encoding="utf-8", errors="replace") as fh:
        text = fh.read()
    header = "[" + acct + "]"
    i = text.find("\n" + header + "\n")
    if i < 0:
        return None
    body = text[i + len(header) + 2:]
    j = body.find("\n[")
    if j >= 0:
        body = body[:j]
    m = re.search(r"PASSWORD=(\S+)", body)
    return m.group(1) if m else None


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

    outdir = os.path.join(BOT, "run_m7")
    os.makedirs(outdir, exist_ok=True)

    # Existing lives first, in the order they were created, then top up from
    # whatever accounts are still empty. Growing the fleet must not disturb
    # anybody already living in it.
    existing = load_roster()
    roster = list(existing)
    used = {acct for _, acct, _ in existing}
    usednames = {name for name, _, _ in existing}

    if len(roster) < len(plan):
        spare = [a for a in free_accounts() if a[0] not in used]
        spare_names = [n for n in NAMES if n not in usednames]
        need = len(plan) - len(roster)
        if len(spare) < need:
            sys.exit(f"need {need} more free accounts, found {len(spare)}")
        if len(spare_names) < need:
            sys.exit(f"need {need} more names, have {len(spare_names)}")
        # The professions still unfilled, counted against what already exists.
        have = {}
        for _, _, prof in existing:
            have[prof] = have.get(prof, 0) + 1
        want = {}
        for prof in plan:
            want[prof] = want.get(prof, 0) + 1
        topup = []
        for prof, n in want.items():
            topup.extend([prof] * max(0, n - have.get(prof, 0)))
        for i, prof in enumerate(topup[:need]):
            roster.append((spare_names[i], spare[i][0], prof))
        save_roster(roster)
    elif not existing:
        save_roster(roster)

    roster = roster[:len(plan)]

    env = dict(os.environ)
    sessions = []
    for name, acct, prof in roster:
        pw = account_password(acct)
        if not pw:
            sys.exit(f"no password for account {acct}")
        tag = name
        # user:pass:char:scenario:tag:profession -- the password field is left
        # EMPTY on purpose; it travels in the environment instead so it never
        # reaches a process listing.
        sessions.append(f"{acct}::{name}::{tag}:{prof}")
        env["UO_BOT_PASS_" + tag.upper()] = pw

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
