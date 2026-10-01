"""Repo-relative paths and replay loading shared by analysis/, eval/ and archive/profiling/ scripts (runs from any cwd)."""
import glob, gzip, json, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
DATA = os.path.join(REPO, "data")
REPLAYS = os.path.join(DATA, "replays")                     # <gameId>.json.gz (raw gameResult/findByGameId JSON)
CACHE = os.path.join(DATA, "cache")                         # regenerated artifacts (gitignored)
SNAPSHOT = os.path.join(DATA, "snapshots", "2026-09-30")
LEADERBOARD = os.path.join(SNAPSHOT, "cg_lb.json")
REPLAYED_PKL = os.path.join(CACHE, "replayed.pkl")          # written by analyze.py
STATS_PKL = os.path.join(CACHE, "stats.pkl")                # written by archive/profiling/stats.py
# Named replay subsets (id lists live in tools/): the numbers in the READMEs were computed on these.
GAME_SETS = {
    "analysis371": os.path.join(REPO, "tools", "sel_games.txt"),    # replayed.pkl -> stats2, steal_se, hu_phase, regimes
    "validation120": os.path.join(REPO, "tools", "sim_games.txt"),  # elim_depth, hand_outcomes, replay_stats, stack_depth
}
SIM_DIR = os.path.join(REPO, "sim")
REPLAYER_DIR = os.path.join(REPO, "replayer")


def open_text(path):
    return gzip.open(path, "rt") if path.endswith(".gz") else open(path)


def load_json(path):
    with open_text(path) as f:
        return json.load(f)


def replay_paths(game_set="all"):
    """Replay files for a named set in GAME_SETS ('analysis371', 'validation120'), a path to an id list, or 'all'."""
    if game_set == "all":
        return sorted(glob.glob(os.path.join(REPLAYS, "*.json.gz")) + glob.glob(os.path.join(REPLAYS, "*.json")))
    lst = GAME_SETS.get(game_set, game_set)
    out = []
    for gid in (l.split("#")[0].strip() for l in open(lst)):
        if not gid:
            continue
        p = os.path.join(REPLAYS, gid + ".json.gz")
        out.append(p if os.path.exists(p) else os.path.join(REPLAYS, gid + ".json"))
    return sorted(out)      # file-name order, as the original glob-based scripts used (order matters for tie-breaks)


def use_sim():
    """Make sim/poker_sim.py importable."""
    if SIM_DIR not in sys.path:
        sys.path.insert(0, SIM_DIR)


def load_leaderboard():
    return load_json(LEADERBOARD)
