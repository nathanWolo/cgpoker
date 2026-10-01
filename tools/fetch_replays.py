"""Download public CodinGame Poker replays into data/replays/<gameId>.json.gz.

Two sources of game ids:
  ids   explicit ids, or a file of ids (default tools/sel_games.txt)
  top   the last battles of the top-N agents of a leaderboard snapshot
        (gamesPlayersRanking/findLastBattlesByTestSessionHandle), keeping up to K finished games each

Each replay is gameResult/findByGameId, stored byte-for-byte as returned (gzipped). Existing files are
skipped, requests are sequential and at least --delay (>= 2) seconds apart, and --dry-run makes no requests.

    python3 tools/fetch_replays.py ids --dry-run
    python3 tools/fetch_replays.py ids 906530651 906530518
    python3 tools/fetch_replays.py top --agents 8 --per-agent 15 --dry-run
"""
import argparse, gzip, json, os, sys, time, urllib.request

API = "https://www.codingame.com/services/"
TOOLS = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(TOOLS)
OUT = os.path.join(REPO, "data", "replays")
BATTLES = os.path.join(REPO, "data", "battles")
DEFAULT_IDS = os.path.join(TOOLS, "sel_games.txt")
DEFAULT_LB = os.path.join(REPO, "data", "snapshots", "2026-09-30", "cg_lb.json")
MIN_DELAY = 2.0

TOS_NOTE = ("note: this uses CodinGame's undocumented public web API. Check CodinGame's terms of service "
            "before any bulk use; keep requests slow and sequential.")


class Client:
    """Sequential POSTs to the CodinGame services API, at least `delay` seconds apart."""

    def __init__(self, delay):
        self.delay = max(MIN_DELAY, delay)
        self.last = 0.0

    def post_raw(self, service, payload):
        wait = self.last + self.delay - time.monotonic()
        if wait > 0:
            time.sleep(wait)
        req = urllib.request.Request(API + service, data=json.dumps(payload).encode(),
                                     headers={"Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(req, timeout=60) as r:
                return r.read()
        finally:
            self.last = time.monotonic()


def replay_path(gid):
    return os.path.join(OUT, f"{gid}.json.gz")


def have(gid):
    return os.path.exists(replay_path(gid))


def save_gz(path, raw):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".part"
    with gzip.GzipFile(tmp, "wb", mtime=0) as f:
        f.write(raw)
    os.replace(tmp, path)


def fetch_game(client, gid):
    """Download one replay; returns True if saved."""
    try:
        raw = client.post_raw("gameResult/findByGameId", [int(gid), None])
        g = json.loads(raw)
        if not isinstance(g, dict) or "frames" not in g:
            raise ValueError(f"unexpected response {raw[:120]!r}")
    except Exception as e:
        print(f"fail {gid}: {e}", file=sys.stderr)
        return False
    save_gz(replay_path(gid), raw)
    return True


def read_ids(items, ids_file):
    ids = []
    if ids_file:
        with open(ids_file) as f:
            ids += [l.strip() for l in f if l.strip() and not l.startswith("#")]
    ids += items
    return list(dict.fromkeys(ids))          # de-duplicate, keep order


def cmd_ids(a):
    ids = read_ids(a.ids, a.file)
    todo = [g for g in ids if not have(g)]
    print(f"{len(ids)} ids, {len(ids) - len(todo)} already in data/replays, {len(todo)} to fetch")
    if a.dry_run:
        for g in todo:
            print("would fetch", g)
        return
    client, got = Client(a.delay), 0
    for g in todo:
        got += fetch_game(client, g)
    print(f"fetched {got}/{len(todo)}")


def cmd_top(a):
    with open(a.leaderboard) as f:
        users = json.load(f)["users"][:a.agents]
    if a.dry_run:
        for u in users:
            print(f"would list last battles of #{u.get('rank')} {u['pseudo']} "
                  f"(testSessionHandle {u['testSessionHandle']}) and fetch up to {a.per_agent} new finished games")
        return
    client = Client(a.delay)
    for u in users:
        try:
            raw = client.post_raw("gamesPlayersRanking/findLastBattlesByTestSessionHandle",
                                  [u["testSessionHandle"], None])
            battles = json.loads(raw)
        except Exception as e:
            print(f"fail battles {u['pseudo']}: {e}", file=sys.stderr)
            continue
        if a.save_battles:
            save_gz(os.path.join(BATTLES, f"{u['pseudo']}.json.gz"), raw)
        got = 0
        for b in battles:
            if got >= a.per_agent:
                break
            gid = b["gameId"]
            if not b.get("done") or have(gid):
                continue
            got += fetch_game(client, gid)
        print(u["pseudo"], got, file=sys.stderr)


def main():
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--delay", type=float, default=MIN_DELAY, help=f"seconds between requests (min {MIN_DELAY})")
    common.add_argument("--dry-run", action="store_true", help="list what would be fetched; no network requests")
    ap = argparse.ArgumentParser(description=__doc__, epilog=TOS_NOTE, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("ids", parents=[common], help="fetch explicit game ids", epilog=TOS_NOTE)
    p.add_argument("ids", nargs="*", help="game ids")
    p.add_argument("--file", help=f"file with one id per line (default when no ids given: {os.path.relpath(DEFAULT_IDS, REPO)})")
    p.set_defaults(func=cmd_ids)
    p = sub.add_parser("top", parents=[common], help="fetch recent games of the top leaderboard agents", epilog=TOS_NOTE)
    p.add_argument("--leaderboard", default=DEFAULT_LB, help="leaderboard JSON with users[].testSessionHandle")
    p.add_argument("--agents", type=int, default=8, help="top-N agents")
    p.add_argument("--per-agent", type=int, default=15, help="max new finished games per agent")
    p.add_argument("--save-battles", action="store_true", help="also store each battle list in data/battles/<nick>.json.gz")
    p.set_defaults(func=cmd_top)
    a = ap.parse_args()
    if a.cmd == "ids" and not a.ids and not a.file:
        a.file = DEFAULT_IDS
    print(TOS_NOTE, file=sys.stderr)
    a.func(a)


if __name__ == "__main__":
    main()
