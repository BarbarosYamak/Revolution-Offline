"""Prepare and admit a stable 70/30 fleet in reviewed 20, 50, 100 stages.

Accounts are created through normal login, never through edits to shard saves.
Credentials remain under local/dev and travel only through child environments.
Each stage requires an explicit --admit command; inspect --status before advancing.
"""
import argparse
from collections import Counter, deque
import ctypes
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import subprocess
import time

BOT = Path(__file__).resolve().parents[1]
ROOT = BOT.parents[1]
CREDS = ROOT / 'local/dev/fleet100_credentials.json'
ROSTER = BOT / 'run_gates/roster100.tsv'
COMBAT = {'fencer': 12, 'macer': 12, 'archer': 11, 'warlock': 17, 'mage': 18}
CRAFT = {'miner_smith': 6, 'lumberjack_swordsman': 5, 'full_crafter': 4,
         'tailor': 4, 'alchemist': 4, 'scribe': 4, 'merchant_tinker': 3}


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
    if ROSTER.exists():
        return read_roster(ROSTER)
    existing = read_roster(BOT / 'run_gates/roster30.tsv')
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
    pools = {}
    serial = 1
    for family, count in (COMBAT | CRAFT).items():
        rows = [r for r in existing if r[2] == family][:count]
        # Retain the three canary identities in the earliest stage.
        rows.sort(key=lambda r: r[0] not in ('Hector', 'Aurelius', 'Odessa'))
        for name, account, _ in rows:
            if account.lower() not in passwords:
                raise RuntimeError('missing credentials for existing account ' + account)
        while len(rows) < count:
            account = f'RevScale100_{serial:03d}'
            serial += 1
            if account.lower() in passwords:
                continue
            name = next(names)
            while name.lower() in used:
                name = next(names)
            used.add(name.lower())
            credentials.setdefault(account, secrets.token_hex(12))
            rows.append([name, account, family])
        pools[family] = deque(rows)
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
    CREDS.write_text(json.dumps(credentials, indent=2))
    ROSTER.write_text('# name\taccount\tprofession; 70 combat / 30 craft\n' +
                      ''.join('\t'.join(r) + '\n' for r in roster))
    return roster


def memory_free_gib():
    class Memory(ctypes.Structure):
        _fields_ = [('length', ctypes.c_ulong), ('load', ctypes.c_ulong)] + [
            (n, ctypes.c_ulonglong) for n in
            ('total', 'available', 'page_total', 'page_available', 'virtual_total',
             'virtual_available', 'extended')]
    data = Memory()
    data.length = ctypes.sizeof(data)
    if not ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(data)):
        raise OSError('GlobalMemoryStatusEx failed')
    return data.available / 2**30


def status(directory):
    results = []
    for path in directory.glob('*.console.txt'):
        text = path.read_text(errors='replace')
        results.append({'name': path.name.removesuffix('.console.txt'),
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
    print(json.dumps(data, indent=2))


def admit(roster, directory, target, minutes):
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
        state = BOT / 'bot_data' / (account + '.' + name) / 'state.json'
        before = directory / (name + '.state_before.json')
        if state.exists():
            shutil.copy2(state, before)
        else:
            before.write_text('{"bank":[]}')
        env = dict(os.environ)
        env['UO_BOT_PASS_' + name.upper()] = passwords[account.lower()]
        command = [str(BOT / 'build-m1/uo_client.exe'), '--headless',
                   '--host', '127.0.0.1', '--port', '2593',
                   '--session', f'{account}::{name}::{name}:{family}',
                   '--create-char', '--autonomous', '--bot-data', str(BOT / 'bot_data'),
                   '--life-minutes', str(minutes), '--mul-dir', str(ROOT / 'runtime/mul'),
                   '--data-dir', str(BOT / 'data'), '--log', str(directory / (name + '.log'))]
        with (directory / (name + '.console.txt')).open('wb') as con, \
             (directory / (name + '.err.txt')).open('wb') as err:
            process = subprocess.Popen(command, cwd=BOT, env=env, stdout=con, stderr=err,
                                       creationflags=subprocess.DETACHED_PROCESS |
                                                     subprocess.CREATE_NEW_PROCESS_GROUP)
        admitted[name] = {'pid': process.pid, 'at': time.time(), 'family': family,
                          'account': account, 'minutes': minutes}
        record.write_text(json.dumps(admitted, indent=2))
        print(f'admitted {len(admitted)}/{target}: {name} ({family})', flush=True)
        time.sleep(3)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path)
    parser.add_argument('--admit', type=int, choices=(20, 50, 100))
    parser.add_argument('--minutes', type=int, default=45)
    parser.add_argument('--status', action='store_true')
    args = parser.parse_args()
    roster = prepare()
    if args.admit or args.status:
        if not args.directory:
            parser.error('--directory is required for admission/status')
        if args.admit:
            admit(roster, args.directory.resolve(), args.admit, args.minutes)
        if args.status:
            status(args.directory.resolve())
    else:
        print('Prepared:', len(roster), 'accounts;', dict(Counter(r[2] for r in roster)))
