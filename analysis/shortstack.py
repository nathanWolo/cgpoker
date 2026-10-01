"""Short-stack preflop decisions in our live games with 3-4 players: situation x action, and the chips they cost.

    python3 analysis/shortstack.py --pseudo flawedaxioms [--max-bb 12] [--battles data/cache/battles_flawedaxioms.json]

Replays every finished game (analysis/postmortem.py's reconstruction) and tabulates each preflop decision we made
with 3-4 players alive and our start-of-hand stack <= max-bb BB: whether the pot was unopened, limped, raised or
jammed before us, what we did, and the net chips of those hands.
"""
import argparse, collections, json, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(REPO, "sim"))
from postmortem import analyse            # noqa: E402
from replay_io import REPLAY_DIR          # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pseudo", required=True)
    ap.add_argument("--battles", default=os.path.join(REPO, "data", "cache", "battles_flawedaxioms.json"))
    ap.add_argument("--max-bb", type=float, default=12)
    a = ap.parse_args()
    battles = json.load(open(a.battles))
    tab = collections.defaultdict(lambda: collections.Counter())
    net = collections.defaultdict(float)
    seen = set()
    games = 0
    for b in battles:
        if not b.get("done"):
            continue
        path = os.path.join(REPLAY_DIR, f"{b['gameId']}.json.gz")
        if not os.path.exists(path):
            continue
        r = analyse(path, a.pseudo)
        games += 1
        me = r["me"]
        for d in r["decisions"]:
            if d["street"] != 0:
                continue
            h = r["hands"].get(d["hand"])
            if not h:
                continue
            st = h["start"]["stacks"]
            alive = sum(1 for x in st if x > 0)
            if alive < 3:
                continue
            my_bb = st[me] / d["bb"]
            if my_bb > a.max_bb:
                continue
            sb = d["bb"] // 2
            blinds = (alive - 1) * sb + d["bb"]
            if d["call"] == 0:
                sit = "bb option (no raise)"
            elif d["call"] <= sb:
                sit = "unopened" if d["pot"] <= blinds else "limped before us"
            elif d["call"] >= d["stack"]:
                sit = "all-in sized bet before us"
            else:
                sit = "raised before us"
            act = d["shown"].split("_")[0]
            tab[sit][act] += 1
            key = (r["game"], d["hand"])
            if key not in seen:
                seen.add(key)
                net[sit] += h["net"][me] / d["bb"]
    print(f"{games} games; preflop decisions with 3-4 alive and our stack <= {a.max_bb:g} BB:")
    for sit, c in sorted(tab.items(), key=lambda kv: -sum(kv[1].values())):
        tot = sum(c.values())
        acts = "  ".join(f"{k} {v / tot:.0%}" for k, v in c.most_common())
        print(f"  {sit:30s} n={tot:4d}  {acts}   net of those hands {net[sit]:+.0f} BB")


if __name__ == "__main__":
    main()
