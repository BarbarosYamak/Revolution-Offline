"""Prepare and admit a stable 70/30 fleet in reviewed 20, 50, 100, 122 stages.

Accounts are created through normal login, never through edits to shard saves.
Credentials remain under local/dev and travel only through child environments.
Each stage requires an explicit --admit command; inspect --status before advancing.

Population manager (--live): instead of a fixed stage, every character logs
in during its own play hours (include/uo/persona.h, mirrored below) and plays
until its window closes, capped by --max-online and free memory. --plan prints
the hourly population the schedules imply, without launching anything.
"""
import argparse
from collections import Counter, deque
import ctypes
import datetime
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import subprocess
import sys
import time

BOT = Path(__file__).resolve().parents[1]
# A worktree under .claude/worktrees/<agent>/ has no runtime/, local/ or
# bot_data of its own: pin the project root and shared state to the main
# checkout so a smoke from a worktree plays the same persistent characters.
_MAIN = Path('C:/Projects/RevolutionOffline/bot/uo-client')
if '.claude' in BOT.parts and 'worktrees' in BOT.parts:
    BOT = _MAIN
ROOT = BOT.parents[1]
CREDS = ROOT / 'local/dev/fleet100_credentials.json'
ROSTER = BOT / 'run_gates/roster100.tsv'
COMBAT = {'fencer': 12, 'macer': 12, 'archer': 11, 'warlock': 17, 'mage': 18}
CRAFT = {'miner_smith': 6, 'lumberjack_swordsman': 5, 'full_crafter': 4,
         'tailor': 4, 'alchemist': 4, 'scribe': 4, 'merchant_tinker': 3}
# Owner 2026-09-06: the five catalogue rows the 100-block omitted are admitted
# as an additional block after the first 100, so the 70/30 stages keep their
# original composition and the whole 17-row catalogue is observed.
EXTRA_COMBAT = {'pk': 5, 'tamer': 5, 'treasure_hunter': 4}
EXTRA_CRAFT = {'fisher': 5, 'mage_blacksmith': 3}
COMBAT_FAMILIES = set(COMBAT) | set(EXTRA_COMBAT)
FLEET_SIZE = 100 + sum(EXTRA_COMBAT.values()) + sum(EXTRA_CRAFT.values())
ALL_FAMILIES = sorted(set(COMBAT) | set(CRAFT) | set(EXTRA_COMBAT) | set(EXTRA_CRAFT))


def read_roster(path):
    return [line.split('\t') for line in path.read_text().splitlines()
            if line and not line.startswith('#')]


def account_passwords():
    text = (ROOT / 'runtime/accounts/sphereaccu.scp').read_text(errors='replace')
    result = {}
    for match in re.finditer(r'^\[([^\]]+)\]\s*\n(.*?)(?=^\[|\Z)', text, re.M | re.S):
        password = re.search(r'^PASSWORD=(.+)$', match[2], re.M | re.I)
        if password:
            result[match[1].lower()] = password[1].strip()
    return result


def prepare():
    roster = read_roster(ROSTER) if ROSTER.exists() else []
    if len(roster) >= FLEET_SIZE:
        return roster
    existing = read_roster(BOT / 'run_gates/roster30.tsv') + roster
    passwords = account_passwords()
    used = {row[0].lower() for row in existing}
    for path in (BOT / 'bot_data').glob('*/state.json'):
        used.add(path.parent.name.split('.', 1)[-1].lower())
    world = (ROOT / 'runtime/save/spherechars.scp').read_text(errors='replace')
    used.update(n.lower() for n in re.findall(r'^NAME=(.+)$', world, re.M))
    book = (ROOT / 'docs/revolution_offline_namebook.md').read_text(encoding='utf-8')
    names = iter(n for n in re.findall(r'^\d{4}\. ([A-Za-z]{3,16})$', book, re.M)
                 if n.lower() not in used)
    credentials = json.loads(CREDS.read_text()) if CREDS.exists() else {}
    serial = 1

    def fill(family, count):
        nonlocal serial
        rows = [r for r in existing if r[2] == family][:count]
        # Retain the three canary identities in the earliest stage.
        rows.sort(key=lambda r: r[0] not in ('Hector', 'Aurelius', 'Odessa'))
        for name, account, _ in rows:
            if account.lower() not in passwords and account not in credentials:
                raise RuntimeError('missing credentials for existing account ' + account)
        while len(rows) < count:
            account = f'RevScale100_{serial:03d}'
            serial += 1
            if account.lower() in passwords or account in credentials:
                continue
            name = next(names)
            while name.lower() in used:
                name = next(names)
            used.add(name.lower())
            # Source-X stores at most MAX_ACCOUNT_PASSWORD_ENTER (16) chars.
            credentials.setdefault(account, secrets.token_hex(8))
            rows.append([name, account, family])
        return rows

    if len(roster) < 100:
        pools = {family: deque(fill(family, count))
                 for family, count in (COMBAT | CRAFT).items()}
        def group(families):
            out = []
            while any(pools[f] for f in families):
                for f in families:
                    if pools[f]:
                        out.append(pools[f].popleft())
            return deque(out)
        fighters, crafters = group(COMBAT), group(CRAFT)
        # Each ten admissions is exactly seven combat and three craft.
        roster = []
        for _ in range(10):
            roster.extend(fighters.popleft() for _ in range(7))
            roster.extend(crafters.popleft() for _ in range(3))
    present = {r[0] for r in roster}
    for family, count in (EXTRA_COMBAT | EXTRA_CRAFT).items():
        roster.extend(r for r in fill(family, count) if r[0] not in present)
    CREDS.write_text(json.dumps(credentials, indent=2))
    ROSTER.write_text('# name\taccount\tprofession; 70 combat / 30 craft, then '
                      f'{sum(EXTRA_COMBAT.values()) + sum(EXTRA_CRAFT.values())} '
                      'extra (pk/tamer/treasure_hunter/fisher/mage_blacksmith)\n' +
                      ''.join('\t'.join(r) + '\n' for r in roster))
    return roster


# ---- personas and play schedules --------------------------------------------
# An exact mirror of include/uo/persona.h. tests/data/persona_vectors.tsv is
# checked by both tests/persona.cpp and tests/test_fleet_ramp.py, so a change
# to one generator without the other fails a test instead of silently giving
# the manager and the character two different evenings.
RHYTHMS = ('evening', 'late_night', 'afternoon', 'weekender', 'morning')
MON, TUE, WED, THU, FRI, SAT, SUN = (1 << d for d in range(7))
WEEKDAYS = MON | TUE | WED | THU | FRI
WEEKEND = SAT | SUN
EVERY_DAY = WEEKDAYS | WEEKEND
DAY_MIN = 24 * 60
WEEK_MIN = 7 * DAY_MIN
_M32 = 0xFFFFFFFF


def identity_id(account, character):
    """Mirror of life::MakeIdentityId."""
    def clean(text):
        # Byte by byte, as the C++ does, so a non-ASCII name maps the same.
        return ''.join(chr(b).lower() if chr(b).isascii() and chr(b).isalnum() else
                       chr(b) if chr(b) in '-_' else '_' for b in text.encode('utf-8'))
    return clean(account) + '.' + clean(character)


def fnv1a(text):
    h = 2166136261
    for byte in text.encode('utf-8'):
        h = ((h ^ byte) * 16777619) & _M32
    return h


class Draw:
    def __init__(self, seed):
        self.state = seed or 0x9E3779B9

    def next(self):
        x = self.state
        x ^= (x << 13) & _M32
        x ^= x >> 17
        x ^= (x << 5) & _M32
        self.state = x
        return x

    def below(self, n):
        return self.next() % n if n > 0 else 0

    def range(self, lo, hi):
        return lo + self.below(hi - lo + 1)


def _rest_day(draw):
    return (MON, TUE, WED, THU, FRI)[draw.below(5)]


def make_persona(ident):
    draw = Draw(fnv1a(ident))
    roll = draw.below(100)
    rhythm = ('evening' if roll < 40 else 'late_night' if roll < 60 else
              'afternoon' if roll < 75 else 'weekender' if roll < 90 else 'morning')
    risk = draw.range(-15, 15)
    sociability = draw.range(15, 95)
    windows = []
    if rhythm == 'evening':
        rest = _rest_day(draw)
        windows.append((WEEKDAYS & ~rest, 19 * 60 + draw.range(0, 90), draw.range(120, 240)))
        windows.append((WEEKEND, 16 * 60 + draw.range(0, 180), draw.range(240, 360)))
    elif rhythm == 'late_night':
        windows.append((EVERY_DAY, 21 * 60 + draw.range(0, 90), draw.range(150, 270)))
    elif rhythm == 'afternoon':
        rest = _rest_day(draw)
        windows.append((WEEKDAYS & ~rest, 15 * 60 + draw.range(0, 90), draw.range(120, 180)))
        windows.append((WEEKEND, 12 * 60 + draw.range(0, 60), draw.range(240, 300)))
    elif rhythm == 'weekender':
        windows.append((FRI, 20 * 60 + draw.range(0, 60), draw.range(180, 300)))
        windows.append((WEEKEND, 11 * 60 + draw.range(0, 120), draw.range(360, 480)))
    else:
        windows.append((EVERY_DAY, 8 * 60 + draw.range(0, 240), draw.range(90, 150)))
        a = _rest_day(draw)
        b = _rest_day(draw)
        if b == a:
            b = MON if a == FRI else a << 1
        windows.append((a | b, 20 * 60 + draw.range(0, 60), draw.range(90, 150)))
    return {'rhythm': rhythm, 'risk_shift': risk, 'sociability': sociability,
            'windows': windows}


def describe(persona):
    names = ('Mo', 'Tu', 'We', 'Th', 'Fr', 'Sa', 'Su')
    return ' '.join(''.join(names[d] for d in range(7) if days & (1 << d)) +
                    f'@{start // 60:02d}:{start % 60:02d}+{length}'
                    for days, start, length in persona['windows'])


def minutes_left(persona, weekday, minute):
    now = (weekday % 7) * DAY_MIN + minute
    best = 0
    for days, start, length in persona['windows']:
        for day in range(7):
            if days & (1 << day):
                since = (now - (day * DAY_MIN + start)) % WEEK_MIN
                if since < length:
                    best = max(best, length - since)
    return best


def minutes_until_next(persona, weekday, minute):
    if minutes_left(persona, weekday, minute):
        return 0
    now = (weekday % 7) * DAY_MIN + minute
    gaps = [(day * DAY_MIN + start - now) % WEEK_MIN
            for days, start, _ in persona['windows'] for day in range(7) if days & (1 << day)]
    return min(gaps) if gaps else -1


def persona_for(account, name, bot_data=None):
    """The character's saved persona if it has one (an owner may have edited
    it), otherwise the one its identity implies -- which is what the client
    will write on its first login."""
    ident = identity_id(account, name)
    for folder in ((bot_data / ident, bot_data / (account + '.' + name)) if bot_data else ()):
        path = folder / 'state.json'
        if path.exists():
            try:
                saved = json.loads(path.read_text(encoding='utf-8')).get('persona')
            except (OSError, ValueError):
                saved = None
            if saved and saved.get('windows'):
                return {'rhythm': saved.get('rhythm', 'evening'),
                        'risk_shift': saved.get('risk_shift', 0),
                        'sociability': saved.get('sociability', 50),
                        'windows': [(w['days'], w['start_min'], w['length_min'])
                                    for w in saved['windows']]}
    return make_persona(ident)


# ---- the era clock (include/uo/era.h) ----------------------------------------
# Which day of Revolution's history the fleet lives in. A fixed date by
# default; with a speed, the shard's calendar advances (e.g. 30 era days per
# real day walks 2008 -> 2016 in about three months). Each client is launched
# with its day and keeps it for the session.
ERA_DEFAULT = '2010-06-01'
ERA_POPULATION = BOT / 'data' / 'era_population.tsv'


def era_date(start, per_day=0.0, since=None, now=None):
    """start: 'YYYY-MM-DD'; per_day: era days per real day; since/now: epoch s."""
    day = datetime.date.fromisoformat(start)
    if per_day and since is not None:
        now = time.time() if now is None else now
        day += datetime.timedelta(days=int((now - since) / 86400.0 * per_day))
    return min(day, datetime.date(2016, 12, 31)).isoformat()


def era_population_scale(date, path=None):
    """Owner-supplied share of the fleet on line in that era, from
    data/era_population.tsv (`year<TAB>scale`). How busy Revolution was in
    each year is UNKNOWN, so without the file every year is 1.0."""
    path = ERA_POPULATION if path is None else path
    year = int(date[:4])
    try:
        rows = [line.split('\t') for line in path.read_text().splitlines()
                if line.strip() and not line.startswith('#')]
    except OSError:
        return 1.0
    table = {int(r[0]): float(r[1]) for r in rows if len(r) >= 2}
    return table.get(year, 1.0)


def plan_hours(personas, weekday):
    """How many characters would be on line at half past each hour."""
    return [sum(1 for p in personas if minutes_left(p, weekday, h * 60 + 30))
            for h in range(24)]


def decide(roster, personas, running, now, max_online, cooldown_until,
           min_minutes=15, max_session=240):
    """Who to launch this tick: pure, so it is tested without processes.

    roster: [name, account, family] rows; personas: name -> persona;
    running: set of names on line; now: datetime (local shard clock);
    cooldown_until: name -> epoch seconds before which it may not relaunch.
    Returns [(row, minutes)], at most enough to reach max_online. When more
    characters want to play than may, the day's order is a stable hash of
    name and date, so a different subset gets the evening each day."""
    weekday, minute = now.weekday(), now.hour * 60 + now.minute
    stamp = now.timestamp()
    wanting = []
    for row in roster:
        name = row[0]
        if name in running or cooldown_until.get(name, 0) > stamp:
            continue
        left = minutes_left(personas[name], weekday, minute)
        if left >= min_minutes:
            wanting.append((fnv1a(name + now.strftime('%Y-%m-%d')), row, min(left, max_session)))
    wanting.sort(key=lambda item: item[0])
    room = max(0, max_online - len(running))
    return [(row, minutes) for _, row, minutes in wanting[:room]]


def memory_free_gib():
    class Memory(ctypes.Structure):
        _fields_ = [('length', ctypes.c_ulong), ('load', ctypes.c_ulong)] + [
            (n, ctypes.c_ulonglong) for n in
            ('total', 'available', 'page_total', 'page_available', 'virtual_total',
             'virtual_available', 'extended')]
    if os.name != 'nt':
        try:
            for line in Path('/proc/meminfo').read_text().splitlines():
                if line.startswith('MemAvailable:'):
                    return int(line.split()[1]) / 2**20
        except OSError:
            pass
        return 64.0
    data = Memory()
    data.length = ctypes.sizeof(data)
    if not ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(data)):
        raise OSError('GlobalMemoryStatusEx failed')
    return data.available / 2**30


def status(directory, verbose=True):
    results = []
    admitted = json.loads((directory / 'admitted.json').read_text())
    for name in admitted:
        path = directory / (name + '.console.txt')
        text = path.read_text(errors='replace')
        error = (directory / (name + '.err.txt')).read_text(errors='replace')
        results.append({'name': path.name.removesuffix('.console.txt'),
                        'in_world': 'needs considered:' in text,
                        'login_denied': 'LOGIN DENIED' in error,
                        'logged_out': 'event logout_complete: acked' in text,
                        'spins': text.count('goal_spinning='),
                        'deaths': text.count('event death_location:'),
                        'kills': text.count('hunt: confirmed kill'),
                        'crafts': text.count('craft: made '),
                        'sales': text.count('earn_gold: sold '),
                        'stale_seconds': round(time.time() - path.stat().st_mtime),
                        'summary': re.findall(r'session_summary .*', text)[-1:]})
    data = {'free_memory_gib': round(memory_free_gib(), 2), 'bots': results}
    (directory / 'status.json').write_text(json.dumps(data, indent=2))
    if verbose:
        print(json.dumps(data, indent=2))
    return data


def snapshot_logged_out_states(directory, admitted, bots):
    """Freeze each bot's state at its first observed acknowledged logout."""
    for row in bots:
        if not row['logged_out']:
            continue
        name = row['name']
        destination = directory / (name + '.state_after.json')
        if destination.exists():
            continue
        config = admitted[name]
        source = BOT / 'bot_data' / (config['account'] + '.' + name) / 'state.json'
        if source.exists():
            shutil.copy2(source, destination)


def watch(directory):
    """One run only: record progress, grade after logout, never relaunch bots."""
    admitted = json.loads((directory / 'admitted.json').read_text())
    deadline = max(row['at'] + (row['minutes'] + 20) * 60 for row in admitted.values())
    while True:
        data = status(directory, verbose=False)
        snapshot_logged_out_states(directory, admitted, data['bots'])
        done = sum(row['logged_out'] for row in data['bots'])
        print(f'{time.strftime("%H:%M:%S")} {done}/{len(admitted)} logged out; '
              f'{data["free_memory_gib"]} GiB free', flush=True)
        if done == len(admitted) or time.time() >= deadline:
            break
        time.sleep(30)
    combat_count = sum(r['family'] in COMBAT_FAMILIES for r in admitted.values())
    lines = ['# Bot validation results', '',
             f'{len(admitted)} accounts: {combat_count} combat / {len(admitted) - combat_count} crafting.', '',
             'These are generic LIFE-GATE scores, not full end-to-end archetype certification.', '',
             '| Character | Profession | Generic result |', '|---|---|---|']
    family = {name: {'total': 0, 'graded': 0, 'passed': 0}
              for name in ALL_FAMILIES}
    for row in data['bots']:
        name = row['name']
        config = admitted[name]
        summary = family[config['family']]
        summary['total'] += 1
        if not row['logged_out']:
            verdict = 'Incomplete: no acknowledged logout before monitor deadline'
        else:
            after = directory / (name + '.state_after.json')
            if not after.exists():
                verdict = 'Incomplete: no state snapshot after acknowledged logout'
            else:
                result = subprocess.run([sys.executable, str(BOT / 'tools/grade_life.py'),
                    str(directory / (name + '.console.txt')),
                    str(directory / (name + '.state_before.json')), str(after),
                    '--family', config['family']], capture_output=True, text=True,
                    encoding='utf-8', errors='replace')
                (directory / (name + '.grade.txt')).write_text(result.stdout + result.stderr)
                scores = re.findall(r'^.*\d+/\d+.*PASS.*$', result.stdout, re.M)
                failing = re.findall(r'^FAILING RULES:.*$', result.stdout, re.M)
                verdict = '; '.join(scores + failing) or f'grader exit {result.returncode}; see grade file'
                summary['graded'] += 1
                summary['passed'] += result.returncode == 0
        lines.append(f'| {name} | {config["family"]} | {verdict} |')
    lines += ['', '## Per-family generic summary', '',
              '| Profession | Passed | Graded | Admitted |',
              '|---|---:|---:|---:|']
    for name in ALL_FAMILIES:
        row = family[name]
        lines.append(f'| {name} | {row["passed"]} | {row["graded"]} | {row["total"]} |')
    lines += ['', f'Acknowledged logouts: {done}/{len(admitted)}.',
              f'Deaths: {sum(r["deaths"] for r in data["bots"])}. '
              f'Confirmed kills: {sum(r["kills"] for r in data["bots"])}.',
              'A process launch or a completed run alone is not an archetype pass.']
    (directory / 'results.md').write_text('\n'.join(lines), encoding='utf-8')
    print('Wrote results.md and per-character grades.', flush=True)


def launch(exe, bot_data, data_dir, directory, name, account, family, minutes,
           password, stem=None, era=None, no_pvp=False):
    """Start one real client for one character; it logs itself out after
    `minutes` (the runner's session limit) and the process then exits."""
    stem = stem or name
    env = dict(os.environ)
    env['UO_BOT_PASS_' + name.upper()] = password
    command = [str(exe), '--headless',
               '--host', '127.0.0.1', '--port', '2593',
               '--session', f'{account}::{name}::{name}:{family}',
               '--create-char', '--autonomous', '--bot-data', str(bot_data),
               '--life-minutes', str(minutes), '--mul-dir', str(ROOT / 'runtime/mul'),
               '--data-dir', str(data_dir), '--log', str(directory / (stem + '.log'))]
    if era:
        command += ['--era-date', era]
    if no_pvp:
        command.append('--no-pvp')
    flags = (subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP
             if os.name == 'nt' else 0)
    with (directory / (stem + '.console.txt')).open('wb') as con, \
         (directory / (stem + '.err.txt')).open('wb') as err:
        return subprocess.Popen(command, cwd=BOT, env=env, stdout=con, stderr=err,
                                creationflags=flags)


def live(roster, directory, max_online, max_session=240, tick=60, exe=None,
         bot_data=None, data_dir=None, clock=datetime.datetime.now, once=False,
         era_start=ERA_DEFAULT, era_per_day=0.0, no_pvp=False):
    """The population manager: keep the shard populated by play schedules.

    Every tick: reap finished clients, then launch whoever's window is open
    (decide()), one every 3 s, while memory allows. A client that exits within
    five minutes, or reports LOGIN DENIED, is not relaunched for an hour, so a
    broken character cannot crash-loop. Nothing is ever killed: each client
    ends its own session at its window's end and logs out properly.
    population.json records who is on line; population_log.tsv one row per
    session for the observer."""
    exe = Path(exe) if exe else BOT / 'build-m1/uo_client.exe'
    bot_data = Path(bot_data) if bot_data else BOT / 'bot_data'
    data_dir = Path(data_dir) if data_dir else BOT / 'data'
    directory.mkdir(parents=True, exist_ok=True)
    passwords = account_passwords()
    if CREDS.exists():
        passwords.update({k.lower(): v for k, v in json.loads(CREDS.read_text()).items()})
    personas = {row[0]: persona_for(row[1], row[0], bot_data) for row in roster}
    running = {}          # name -> (process, started, stem, minutes)
    cooldown = {}
    sessions = directory / 'population_log.tsv'
    era_since = time.time()
    if not sessions.exists():
        sessions.write_text('name\tfamily\trhythm\tstarted\tended\tplanned_min\texit\n')
    while True:
        now = clock()
        era = era_date(era_start, era_per_day, era_since)
        cap = max(1, round(max_online * era_population_scale(era)))
        for name, (process, started, stem, minutes) in list(running.items()):
            code = process.poll()
            if code is None:
                continue
            del running[name]
            err = directory / (stem + '.err.txt')
            denied = err.exists() and 'LOGIN DENIED' in err.read_text(errors='replace')
            short = time.time() - started < 300
            cooldown[name] = time.time() + (3600 if denied or short else 1200)
            family = next(r[2] for r in roster if r[0] == name)
            with sessions.open('a') as out:
                out.write(f'{name}\t{family}\t{personas[name]["rhythm"]}\t'
                          f'{time.strftime("%Y-%m-%d %H:%M", time.localtime(started))}\t'
                          f'{time.strftime("%Y-%m-%d %H:%M")}\t{minutes}\t'
                          f'{"denied" if denied else code}\n')
        for row, minutes in decide(roster, personas, set(running), now, cap,
                                   cooldown, max_session=max_session):
            if memory_free_gib() < 2:
                print('population: holding launches, less than 2 GiB free', flush=True)
                break
            name, account, family = row
            if account.lower() not in passwords:
                cooldown[name] = time.time() + 86400
                print(f'population: no credentials for {account}; skipped today', flush=True)
                continue
            stem = f'{name}.{now.strftime("%Y%m%d-%H%M")}'
            process = launch(exe, bot_data, data_dir, directory, name, account, family,
                             minutes, passwords[account.lower()], stem, era, no_pvp)
            running[name] = (process, time.time(), stem, minutes)
            print(f'{now:%a %H:%M} login {name} ({family}, '
                  f'{personas[name]["rhythm"]}) for {minutes} min', flush=True)
            time.sleep(3)
        (directory / 'population.json').write_text(json.dumps({
            'at': now.isoformat(timespec='minutes'), 'max_online': cap, 'era': era,
            'online': sorted(running),
            'by_family': dict(Counter(next(r[2] for r in roster if r[0] == n) for n in running)),
            'wanting_now': sum(1 for r in roster if minutes_left(
                personas[r[0]], now.weekday(), now.hour * 60 + now.minute) >= 15),
        }, indent=2))
        if once:
            return running
        time.sleep(tick)


def print_plan(roster, bot_data=None, day=None):
    bot_data = Path(bot_data) if bot_data else BOT / 'bot_data'
    personas = [persona_for(r[1], r[0], bot_data) for r in roster]
    days = ('Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun')
    print('rhythms:', dict(Counter(p['rhythm'] for p in personas)))
    for weekday in ([day] if day is not None else range(7)):
        hours = plan_hours(personas, weekday)
        print(f'{days[weekday]} ' + ' '.join(f'{n:3d}' for n in hours) +
              f'   peak {max(hours)} at {hours.index(max(hours)):02d}:30')
    return personas


def admit(roster, directory, target, minutes, exe=None, bot_data=None, data_dir=None):
    """exe/bot_data/data_dir default to this tree's build-m1 exe and state;
    tools/smoke.py passes a worktree exe with the main tree's state."""
    exe = Path(exe) if exe else BOT / 'build-m1/uo_client.exe'
    bot_data = Path(bot_data) if bot_data else BOT / 'bot_data'
    data_dir = Path(data_dir) if data_dir else BOT / 'data'
    directory.mkdir(parents=True, exist_ok=True)
    record = directory / 'admitted.json'
    admitted = json.loads(record.read_text()) if record.exists() else {}
    passwords = account_passwords()
    passwords.update({k.lower(): v for k, v in json.loads(CREDS.read_text()).items()})
    for name, account, family in roster[:target]:
        if name in admitted:
            continue
        if memory_free_gib() < 2:
            raise RuntimeError('admission paused: less than 2 GiB free memory')
        # Refuse overlap with the three original runs or another gate.
        old = BOT / f'run_gates/g_{name}.console.txt'
        if old.exists() and time.time() - old.stat().st_mtime < 120:
            if 'event logout_complete: acked' not in old.read_text(errors='replace'):
                raise RuntimeError('admission paused: existing live gate for ' + name)
        state = bot_data / (account + '.' + name) / 'state.json'
        before = directory / (name + '.state_before.json')
        if state.exists():
            shutil.copy2(state, before)
        else:
            before.write_text('{"bank":[]}')
        process = launch(exe, bot_data, data_dir, directory, name, account, family,
                         minutes, passwords[account.lower()])
        admitted[name] = {'pid': process.pid, 'at': time.time(), 'family': family,
                          'account': account, 'minutes': minutes}
        record.write_text(json.dumps(admitted, indent=2))
        print(f'admitted {len(admitted)}/{target}: {name} ({family})', flush=True)
        time.sleep(3)
        if 'LOGIN DENIED' in (directory / (name + '.err.txt')).read_text(errors='replace'):
            raise RuntimeError('admission paused: login rejected for ' + name)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path)
    parser.add_argument('--admit', type=int, choices=(20, 50, 100, FLEET_SIZE))
    parser.add_argument('--minutes', type=int, default=45)
    parser.add_argument('--status', action='store_true')
    parser.add_argument('--watch', action='store_true')
    parser.add_argument('--live', action='store_true',
                        help='population manager: log characters in by their play hours')
    parser.add_argument('--max-online', type=int, default=40)
    parser.add_argument('--max-session', type=int, default=240,
                        help='longest single session in minutes')
    parser.add_argument('--era-start', default=ERA_DEFAULT,
                        help="the day of Revolution's history to live in (YYYY-MM-DD)")
    parser.add_argument('--era-days-per-day', type=float, default=0.0,
                        help='advance the era calendar this many days per real day (0 = fixed)')
    parser.add_argument('--no-pvp', action='store_true',
                        help='no PK ambushes and no anti-PK hunting this run')
    parser.add_argument('--plan', action='store_true',
                        help='print the hourly population the schedules imply')
    args = parser.parse_args()
    roster = prepare()
    if args.plan:
        print_plan(roster)
    if args.live:
        if not args.directory:
            parser.error('--directory is required for --live')
        live(roster, args.directory.resolve(), args.max_online, args.max_session,
             era_start=args.era_start, era_per_day=args.era_days_per_day, no_pvp=args.no_pvp)
    elif args.admit or args.status or args.watch:
        if not args.directory:
            parser.error('--directory is required for admission/status')
        if args.admit:
            admit(roster, args.directory.resolve(), args.admit, args.minutes)
        if args.status:
            status(args.directory.resolve())
        if args.watch:
            watch(args.directory.resolve())
    elif not args.plan:
        print('Prepared:', len(roster), 'accounts;', dict(Counter(r[2] for r in roster)))
