"""Replay a CodinGame Poker game (seed + recorded bot outputs) through the referee's own Java model classes.

    replayer/build.sh                                      # once (replay() also builds on demand)
    python3 replayer/replay_game.py 906530651              # print the event log of one game (id or path)
    python3 replayer/replay_game.py --check                # all data/replays: do final scores match CG's?

replay(path) -> (replay dict, Replayer stdout); the output format is documented in replayer/README.md.
"""
import argparse, glob, gzip, json, os, re, subprocess, sys, threading
from concurrent.futures import ThreadPoolExecutor

BASE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(BASE)
BUILD = os.path.join(BASE, "build")
REPLAY_DIR = os.path.join(REPO, "data", "replays")

def load(path):
    with (gzip.open if path.endswith(".gz") else open)(path, "rt") as f:
        return json.load(f)

def resolve(a):
    """A replay path, or a bare game id -> data/replays/<id>.json.gz."""
    return os.path.join(REPLAY_DIR, f"{a}.json.gz") if a.isdigit() and not os.path.exists(a) else a

def to_input(d):
    seed = re.search(r'seed=(-?\d+)', d['refereeInput']).group(1)
    n = len(d['agents'])
    lines = [seed, str(n)]
    for f in d['frames']:
        s = f.get('stdout') or ''
        summ = f.get('summary') or ''
        a = f.get('agentId', -1)
        if s.strip():
            lines.append(f"{a}\t{s.splitlines()[0]}")
        elif 'did not output in time' in summ or 'timeout' in summ.lower():
            m = re.search(r'\$(\d)', summ)
            lines.append(f"{m.group(1) if m else a}\t__TIMEOUT__")
    return '\n'.join(lines) + '\n'

_build_lock = threading.Lock()

def ensure_built():
    with _build_lock:          # replay() is called from thread pools (analysis/analyze.py)
        if not os.path.exists(os.path.join(BUILD, "com", "codingame", "game", "Replayer.class")):
            subprocess.run([os.path.join(BASE, "build.sh")], check=True, stdout=sys.stderr)

def replay(path):
    ensure_built()
    d = load(resolve(path))
    inp = to_input(d)
    r = subprocess.run(['java', '-cp', BUILD, 'com.codingame.game.Replayer'], input=inp, capture_output=True, text=True)
    return d, r.stdout

def final_scores(out):
    """Scores from the SCORES line (None if the replay stopped early, e.g. on an ERR line)."""
    for l in out.splitlines():
        if l.startswith("SCORES\t"):
            return [int(x) for x in l.split("\t")[1:]]
    return None

def check(path):
    d, o = replay(path)
    sc = final_scores(o)
    ok = sc is not None and [float(x) for x in sc] == [float(x) for x in d["scores"]]
    err = next((l for l in o.splitlines() if l.startswith("ERR")), None)
    return path, ok, sc, d["scores"], err

if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("replays", nargs="*", help="replay paths or game ids (--check default: all of data/replays)")
    ap.add_argument("--check", action="store_true", help="compare the replayed final scores with the recorded ones")
    ap.add_argument("-j", "--jobs", type=int, default=8)
    a = ap.parse_args()
    if not a.check:
        if len(a.replays) != 1:
            ap.error("give exactly one replay (or use --check)")
        print(replay(a.replays[0])[1])
        sys.exit(0)
    ensure_built()
    paths = [resolve(p) for p in a.replays] or sorted(glob.glob(os.path.join(REPLAY_DIR, "*.json.gz")))
    with ThreadPoolExecutor(a.jobs) as ex:
        res = list(ex.map(check, paths))
    for path, ok, sc, cg, err in res:
        if not ok:
            print("MISMATCH", os.path.basename(path), "replayer", sc, "codingame", cg, err or "")
    print(f"{sum(r[1] for r in res)}/{len(res)} replays reproduce the recorded final scores")
    sys.exit(0 if res and all(r[1] for r in res) else 1)
