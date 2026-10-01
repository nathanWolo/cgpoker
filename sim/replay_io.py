"""Locate and load CodinGame Poker replays (data/replays/<gameId>.json.gz, or plain .json)."""
import glob, gzip, json, os, re

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPLAY_DIR = os.path.join(REPO, "data", "replays")
CACHE_DIR = os.path.join(REPO, "data", "cache")


def load_replay(path):
    """Parsed gameResult/findByGameId JSON; gzip is detected from the extension."""
    opener = gzip.open if path.endswith(".gz") else open
    with opener(path, "rt") as f:
        return json.load(f)


def replay_paths(args=(), ids_file=None):
    """Resolve CLI args (paths or bare game ids) and/or an ids file to replay paths.

    With neither, returns every data/replays/*.json.gz, sorted.
    """
    items = list(args)
    if ids_file:
        with open(ids_file) as f:
            items += [l.strip() for l in f if l.strip() and not l.startswith("#")]
    if not items:
        return sorted(glob.glob(os.path.join(REPLAY_DIR, "*.json.gz")))
    out = []
    for a in items:
        if os.path.exists(a):
            out.append(a)
        elif a.isdigit():
            out.append(os.path.join(REPLAY_DIR, f"{a}.json.gz"))
        else:
            raise FileNotFoundError(a)
    return out


def recorded_actions(g):
    """[(player, first stdout line)] in referee order; a bot timeout yields (player, None).

    A timeout frame has no stdout and a summary "$<id> did not output in time!"; the frame's own agentId
    is not always the timed-out player, so the id is read from the summary (as replayer/replay_game.py does).
    """
    out = []
    for f in g["frames"]:
        s, summ = f.get("stdout") or "", f.get("summary") or ""
        if f.get("agentId", -1) >= 0 and s:
            out.append((f["agentId"], s.split("\n")[0]))
        elif "did not output in time" in summ:
            out.append((int(re.search(r"\$(\d)", summ).group(1)), None))
    return out
