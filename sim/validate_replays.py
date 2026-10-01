"""Differential test: replay real CodinGame games through poker_sim.py (seed + recorded bot outputs)
and check that the simulator asks the same player at every step and reproduces the final scores.

    python3 sim/validate_replays.py                         # all data/replays/*.json.gz
    python3 sim/validate_replays.py --ids tools/sim_games.txt
    python3 sim/validate_replays.py 906530651 path/to/game.json.gz
"""
import argparse, os, sys
from poker_sim import PokerSim
from replay_io import load_replay, recorded_actions, replay_paths

def load(path):
    g = load_replay(path)
    seed = int(g["refereeInput"].strip().split("=")[1])
    n = len(g["agents"])
    acts = recorded_actions(g)          # a timed-out turn is (pid, None) -> PokerSim plays TIMEOUT
    summaries = " ".join(f.get("summary", "") for f in g["frames"])
    return g, seed, n, acts, summaries

def check(path):
    g, seed, n, acts, summ = load(path)
    queue = list(acts)
    mism = []

    def make(pid):
        def agent(obs):
            if not queue:
                mism.append(("ran out", pid))
                return "FOLD"
            who, out = queue.pop(0)
            if who != pid:
                mism.append((obs.round, obs.hand_nb, "expected", who, "sim asked", pid))
            return out
        return agent

    sim = PokerSim(n, seed)
    res = sim.run([make(i) for i in range(n)])
    ok_scores = [float(x) for x in res["scores"]] == [float(x) for x in g["scores"]]
    timeouts = "timeout" in summ.lower() or "did not output" in summ.lower()
    return dict(path=os.path.basename(path), n=n, ok_order=not mism and not queue, ok_scores=ok_scores,
                sim_scores=res["scores"], cg_scores=g["scores"], hands=res["hands"], rounds=res["rounds"],
                cancelled=res["cancelled"], leftover=len(queue), first_mismatch=mism[:1], timeouts=timeouts)

if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("replays", nargs="*", help="replay paths or game ids (default: all of data/replays)")
    ap.add_argument("--ids", help="file with one game id per line")
    a = ap.parse_args()
    paths = replay_paths(a.replays, a.ids)
    good = 0
    for p in paths:
        r = check(p)
        good += r["ok_order"] and r["ok_scores"]
        if not (r["ok_order"] and r["ok_scores"]):
            print("MISMATCH", r)
    print(f"{good}/{len(paths)} replays reproduced exactly (turn order + final scores)")
    sys.exit(0 if paths and good == len(paths) else 1)
