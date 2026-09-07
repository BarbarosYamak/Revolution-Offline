"""Five-minute live smoke for named characters from roster100/roster30.

    py -3 tools/smoke.py Alder,Kharazar [--minutes 5] [--directory artifacts/<dir>]

Wraps fleet_ramp.admit()/watch(): real login through the normal client
protocol, one process per character, grades on logout, writes results.md.
Refuses to start while another uo_client.exe is running (one live smoke at a
time -- the shard and build-m1/uo_client.exe are shared).  Owner rule
2026-09-07: every fix brief ends with the agent's own live smoke.
"""
import argparse, pathlib, subprocess, sys, time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import fleet_ramp as fr  # noqa: E402


def live_clients():
    out = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq uo_client.exe'],
                         capture_output=True, text=True).stdout
    return out.count('uo_client.exe')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('names', help='comma-separated character names')
    ap.add_argument('--minutes', type=int, default=5)
    ap.add_argument('--directory', type=pathlib.Path)
    args = ap.parse_args()
    names = [n.strip() for n in args.names.split(',') if n.strip()]
    rows = fr.read_roster(fr.ROSTER) + fr.read_roster(fr.BOT / 'run_gates/roster30.tsv')
    seen, roster = set(), []
    for row in rows:
        if row[0] in names and row[0] not in seen:
            seen.add(row[0]); roster.append(row)
    missing = [n for n in names if n not in seen]
    if missing:
        sys.exit(f'not in roster100/roster30: {missing}')
    if live_clients():
        sys.exit('another uo_client.exe is running -- one live smoke at a time')
    directory = args.directory or (fr.BOT / 'artifacts' /
                                   f'smoke_{"_".join(names)}_{time.strftime("%Y%m%d_%H%M")}')
    directory = directory.resolve()
    print(f'smoke: {len(roster)} character(s), {args.minutes} min -> {directory}', flush=True)
    fr.admit(roster, directory, len(roster), args.minutes)
    fr.watch(directory)
    print(f'smoke: done -> {directory / "results.md"}', flush=True)


if __name__ == '__main__':
    main()
