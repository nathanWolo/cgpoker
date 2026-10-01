"""Turn public CodinGame Poker replays into complete-information hand histories.

The replay API gives the exact referee seed (refereeInput "seed=...") and every bot's stdout; poker_sim.py
reproduces the SHA1PRNG deck bit-exactly, so we recover every player's hole cards, the full board and all
(post-replacement) actions -> JSONL, one record per decision, suitable for behaviour cloning / belief-net
training / opponent-population modelling.

    python3 sim/reconstruct.py                                 # all replays -> data/cache/decisions.jsonl
    python3 sim/reconstruct.py --ids tools/sim_games.txt --gz  # rebuild the committed data/decisions.jsonl.gz
"""
import argparse, gzip, json, os
from poker_sim import PokerSim
from replay_io import CACHE_DIR, REPO, load_replay, recorded_actions, replay_paths

def reconstruct(path):
    g = load_replay(path)
    seed = int(g["refereeInput"].strip().split("=")[1])
    n = len(g["agents"])
    names = {a["index"]: a["codingamer"]["pseudo"] if a.get("codingamer") else f"agent{a['index']}" for a in g["agents"]}
    q = recorded_actions(g)
    sim = PokerSim(n, seed)
    rows = []

    def mk(pid):
        def agent(obs):
            _, out = q.pop(0)
            if out is None:          # bot timed out: the sim plays TIMEOUT (fold + elimination); no record
                return None
            rows.append(dict(game=g["gameId"], n=n, player=pid, name=names[pid], round=obs.round, hand=obs.hand_nb,
                             sb=sim.sb, bb=sim.bb, dealer=sim.dealer_id, bb_id=sim.bb_id,
                             stacks=obs.stacks, chip_in_pot=obs.chip_in_pot, board=obs.board, cards=obs.cards,
                             all_hole_cards=["_".join(p.hand) if p.hand else None for p in sim.players],
                             possible=obs.possible, output=out.split(";")[0]))
            return out
        return agent

    res = sim.run([mk(i) for i in range(n)])
    assert [float(s) for s in res["scores"]] == [float(s) for s in g["scores"]]
    return rows

if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("replays", nargs="*", help="replay paths or game ids (default: all of data/replays)")
    ap.add_argument("--ids", help="file with one game id per line")
    ap.add_argument("--gz", action="store_true", help="write the committed data/decisions.jsonl.gz instead of the cache")
    ap.add_argument("-o", "--out", help="explicit output path (a .gz suffix means gzip)")
    a = ap.parse_args()
    path = a.out or (os.path.join(REPO, "data", "decisions.jsonl.gz") if a.gz
                     else os.path.join(CACHE_DIR, "decisions.jsonl"))
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    paths = replay_paths(a.replays, a.ids)
    # mtime=0 so that regenerating the .gz gives identical bytes
    out = gzip.GzipFile(path, "wb", mtime=0) if path.endswith(".gz") else open(path, "wb")
    total = 0
    for p in paths:
        for r in reconstruct(p):
            out.write((json.dumps(r) + "\n").encode())
            total += 1
    out.close()
    print(f"decisions written: {total} from {len(paths)} games -> {a.out or os.path.relpath(path, REPO)}")
