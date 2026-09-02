#!/usr/bin/env python3
"""rev.py -- named targets for the repeated build / gate / grade sequences.

    python tools/rev.py build                 # vcvars + cmake --build build-m1
    python tools/rev.py reconfigure           # cmake -S . -B build-m1 (new test files)
    python tools/rev.py test                  # ctest --test-dir build-m1
    python tools/rev.py gate  CHAR=Draver [MINUTES=5]
    python tools/rev.py gates [MINUTES=5] [CHARS=Draver,Vorar] [STAGGER=3]
    python tools/rev.py wait  [CHARS=...] [TIMEOUT=2400]
    python tools/rev.py grade [CHARS=...]     # grade_life.py per char, summary table

Targets may be chained: `python tools/rev.py build test`.
KEY=VALUE arguments apply to every target on the line.

Every target echoes what it does and is safe to re-run:
  build/test/reconfigure are incremental by construction (cmake/ctest);
  gate refuses to relaunch a character whose console is still live;
  grade only reads.

Account and family for a character come from run_gates/roster30.tsv
(name<TAB>account<TAB>family), so a gate never needs them spelled out.
Launcher shape and grading rules: run_gates/gate.bat, docs/LIFE_GATES.md.
"""
import os
import subprocess
import sys
import time
from pathlib import Path

BOT = Path(__file__).resolve().parents[1]              # bot/uo-client
ROOT = BOT.parents[1]                                   # RevolutionOffline
BUILD = BOT / "build-m1"
EXE = BUILD / "uo_client.exe"
GATES = BOT / "run_gates"
ROSTER = GATES / "roster30.tsv"
BOT_DATA = BOT / "bot_data"
VCVARS = r"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
DEFAULT_PASS = "Gen3Fr3shRevBots"
LIVE_WINDOW_S = 120     # console touched inside this window and no logout => still running


def say(msg):
    print("[rev] " + msg, flush=True)


def die(msg, code=2):
    print("[rev] ERROR: " + msg, file=sys.stderr, flush=True)
    sys.exit(code)


# --------------------------------------------------------------------------
# helpers
# --------------------------------------------------------------------------

def run_vc(cmdline):
    """Run `cmdline` inside a cmd.exe that has called vcvars64 (cmake/ctest
    are not on PATH; a bare call silently no-ops)."""
    if not Path(VCVARS).exists():
        die("vcvars64.bat not found at %s" % VCVARS)
    full = 'call "%s" >nul && cd /d "%s" && %s' % (VCVARS, BOT, cmdline)
    say("cmd> " + cmdline)
    # pass one string: list form would backslash-escape the quotes for cmd.exe
    return subprocess.call('cmd /c "%s"' % full)


def read_roster():
    if not ROSTER.exists():
        die("roster missing: %s" % ROSTER)
    rows = {}
    for line in ROSTER.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) < 3:
            continue
        name, account, family = parts[0].strip(), parts[1].strip(), parts[2].strip()
        rows[name.lower()] = (name, account, family)
    return rows


def pick_chars(kv, roster):
    chars = kv.get("CHARS") or kv.get("CHAR")
    if not chars:
        return [roster[k] for k in roster]
    out = []
    for c in chars.split(","):
        c = c.strip()
        if not c:
            continue
        if c.lower() not in roster:
            die("%s is not in %s" % (c, ROSTER.name))
        out.append(roster[c.lower()])
    return out


def console_of(name):
    return GATES / ("g_%s.console.txt" % name)


def state_of(account, name):
    return BOT_DATA / ("%s.%s" % (account, name)) / "state.json"


def before_of(name):
    return GATES / ("g_%s.state_before.json" % name)


def finished(name):
    con = console_of(name)
    if not con.exists():
        return False
    try:
        return "logout_complete" in con.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return False


def live(name):
    con = console_of(name)
    if not con.exists() or finished(name):
        return False
    return (time.time() - con.stat().st_mtime) < LIVE_WINDOW_S


# --------------------------------------------------------------------------
# targets
# --------------------------------------------------------------------------

def t_build(kv):
    before = EXE.stat().st_mtime if EXE.exists() else 0
    say("build: cmake --build build-m1")
    rc = run_vc("cmake --build build-m1")
    if rc != 0:
        die("build failed rc=%d" % rc, rc)
    after = EXE.stat().st_mtime if EXE.exists() else 0
    if after == before:
        say("build: uo_client.exe unchanged (%s) -- nothing to rebuild"
            % time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(after)))
    else:
        say("build: uo_client.exe -> %s"
            % time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(after)))
    return 0


def t_reconfigure(kv):
    say("reconfigure: cmake -S . -B build-m1 (picks up new test files)")
    rc = run_vc("cmake -S . -B build-m1")
    if rc != 0:
        die("reconfigure failed rc=%d" % rc, rc)
    return 0


def t_test(kv):
    say("test: ctest --test-dir build-m1 --output-on-failure")
    rc = run_vc("ctest --test-dir build-m1 --output-on-failure")
    say("test: ctest rc=%d" % rc)
    return rc


def launch_gate(name, account, family, minutes, password):
    if not EXE.exists():
        die("no build: %s" % EXE)
    if live(name):
        say("gate %s: console still live (touched <%ds ago, no logout_complete) -- skip"
            % (name, LIVE_WINDOW_S))
        return False
    state = state_of(account, name)
    before = before_of(name)
    if state.exists():
        before.write_bytes(state.read_bytes())
        say("gate %s: pre-state snapshot -> %s" % (name, before.name))
    else:
        before.write_text('{"bank":[]}', encoding="utf-8")
        say("gate %s: WARNING no state.json at %s -- empty pre-state written" % (name, state))
    args = [
        str(EXE), "--headless",
        "--host", "127.0.0.1", "--port", "2593",
        "--user", account, "--pass", password,
        "--char-name", name,
        "--profession", family,
        "--autonomous",
        "--bot-data", str(BOT_DATA),
        "--life-minutes", str(minutes),
        "--mul-dir", str(ROOT / "runtime" / "mul"),
        "--data-dir", str(BOT / "data"),
        "--tag", "g_" + name,
        "--log", str(GATES / ("g_%s.log" % name)),
    ]
    con = open(console_of(name), "wb")
    err = open(GATES / ("g_%s.err.txt" % name), "wb")
    flags = getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0) | getattr(subprocess, "DETACHED_PROCESS", 0)
    subprocess.Popen(args, stdout=con, stderr=err, cwd=str(BOT), creationflags=flags)
    say("gate %s: launched on %s as %s for %s min" % (name, account, family, minutes))
    return True


def t_gate(kv):
    if "CHAR" not in kv:
        die("gate needs CHAR=<name>")
    roster = read_roster()
    (name, account, family), = pick_chars(kv, roster)
    minutes = kv.get("MINUTES", "5")
    launch_gate(name, account, family, minutes, os.environ.get("UO_BOT_PASS", DEFAULT_PASS))
    say("gate %s: grade later with `python tools/rev.py grade CHARS=%s`" % (name, name))
    return 0


def t_gates(kv):
    roster = read_roster()
    chars = pick_chars(kv, roster)
    minutes = kv.get("MINUTES", "5")
    stagger = float(kv.get("STAGGER", "3"))
    pw = os.environ.get("UO_BOT_PASS", DEFAULT_PASS)
    say("gates: %d characters, %s min each, stagger %.0fs" % (len(chars), minutes, stagger))
    launched = 0
    for name, account, family in chars:
        if launch_gate(name, account, family, minutes, pw):
            launched += 1
            time.sleep(stagger)
    say("gates: launched %d/%d (rest were live). wait with `python tools/rev.py wait`"
        % (launched, len(chars)))
    return 0


def t_wait(kv):
    roster = read_roster()
    chars = [c for c in pick_chars(kv, roster) if console_of(c[0]).exists()]
    timeout = float(kv.get("TIMEOUT", "2400"))
    t0 = time.time()
    say("wait: %d consoles, timeout %.0fs" % (len(chars), timeout))
    while True:
        pending = [c[0] for c in chars if not finished(c[0])]
        if not pending:
            say("wait: all %d reached logout_complete" % len(chars))
            return 0
        if time.time() - t0 > timeout:
            say("wait: TIMEOUT, still pending: " + " ".join(pending))
            return 1
        say("wait: %d pending (%s) %.0fs elapsed"
            % (len(pending), " ".join(pending[:6]) + (" ..." if len(pending) > 6 else ""),
               time.time() - t0))
        time.sleep(60)


def t_grade(kv):
    roster = read_roster()
    chars = [c for c in pick_chars(kv, roster) if console_of(c[0]).exists()]
    if not chars:
        die("grade: no g_<char>.console.txt files to grade")
    say("grade: %d characters" % len(chars))
    rows = []
    for name, account, family in chars:
        con, before, after = console_of(name), before_of(name), state_of(account, name)
        if not before.exists():
            rows.append((name, family, "no state_before", ""))
            continue
        if not after.exists():
            rows.append((name, family, "no state.json", ""))
            continue
        p = subprocess.run([sys.executable, str(BOT / "tools" / "grade_life.py"),
                            str(con), str(before), str(after), "--family", family],
                           capture_output=True, text=True, encoding="utf-8", errors="replace")
        score, failing = "?", ""
        for ln in p.stdout.splitlines():
            if ln.endswith(" PASS") and "/" in ln:
                score = ln.split()[0]
            if ln.startswith("FAILING RULES:"):
                failing = ln[len("FAILING RULES:"):].strip()
        if p.returncode not in (0, 1) and not score.count("/"):
            score = "error rc=%d" % p.returncode
            failing = (p.stderr or p.stdout).strip().splitlines()[-1:] or [""]
            failing = failing[0]
        rows.append((name, family, score, failing))
    w = max(len(r[0]) for r in rows)
    f = max(len(r[1]) for r in rows)
    say("grade: results")
    for name, family, score, failing in rows:
        flag = " (not logged out)" if not finished(name) else ""
        print("  %-*s  %-*s  %-8s %s%s" % (w, name, f, family, score, failing, flag))
    return 0


TARGETS = {
    "build": t_build,
    "reconfigure": t_reconfigure,
    "test": t_test,
    "gate": t_gate,
    "gates": t_gates,
    "wait": t_wait,
    "grade": t_grade,
}


def main(argv):
    targets, kv = [], {}
    for a in argv:
        if "=" in a:
            k, v = a.split("=", 1)
            kv[k.upper()] = v
        elif a in TARGETS:
            targets.append(a)
        else:
            die("unknown target %r; known: %s" % (a, " ".join(TARGETS)))
    if not targets:
        print(__doc__)
        return 2
    rc = 0
    for t in targets:
        rc = TARGETS[t](kv) or 0
        if rc != 0:
            say("%s: rc=%d, stopping" % (t, rc))
            return rc
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
