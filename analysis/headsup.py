"""Heads-up phase of our live games: when the game reached two players, our chip share then, and whether we won.

    python3 analysis/headsup.py --pseudo flawedaxioms --ids data/cache/m21_ids.txt [--label M2.1]

Replays every game (analysis/postmortem.py's reconstruction).  For each game that reached a heads-up phase with
us in it: our stack share when it started, the number of heads-up hands, whether we finished first, and our net
chips per heads-up hand by effective stack.  Games that started heads-up count from hand 1.
"""
import argparse, collections, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(REPO, "sim"))
from postmortem import analyse            # noqa: E402
from replay_io import REPLAY_DIR          # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pseudo", required=True)
    ap.add_argument("--ids", required=True)
    ap.add_argument("--label", default="")
    a = ap.parse_args()
    ids = [int(x) for x in open(a.ids).read().split()]
    rows = []
    net_by_eff = collections.defaultdict(lambda: [0.0, 0])
    for gid in ids:
        path = os.path.join(REPLAY_DIR, f"{gid}.json.gz")
        if not os.path.exists(path):
            continue
        r = analyse(path, a.pseudo)
        me = r["me"]
        hands = sorted(r["hands"])
        hu_start = None
        for h in hands:
            st = r["hands"][h]["start"]["stacks"]
            alive = [p for p in range(r["n"]) if st[p] > 0]
            if len(alive) == 2 and me in alive:
                hu_start = h
                break
        if hu_start is None:
            continue
        st = r["hands"][hu_start]["start"]["stacks"]
        share = st[me] / sum(st)
        n_hu = sum(1 for h in hands if h >= hu_start)
        won = r["place"][0] == 0
        for h in hands:
            if h < hu_start:
                continue
            hs = r["hands"][h]["start"]
            s = hs["stacks"]
            alive = [p for p in range(r["n"]) if s[p] > 0]
            if len(alive) != 2:
                continue
            eff = min(s[p] for p in alive) / hs["bb"]
            b = "<=8" if eff <= 8 else "8-15" if eff <= 15 else "15-30" if eff <= 30 else "30-60" if eff <= 60 else ">60"
            net_by_eff[b][0] += r["hands"][h]["net"][me] / hs["bb"]
            net_by_eff[b][1] += 1
        rows.append((gid, r["n"], share, n_hu, won))
    print(f"{a.label}: {len(rows)} games reached heads-up with us")
    for lo, hi in ((0, 0.35), (0.35, 0.5), (0.5, 0.65), (0.65, 1.01)):
        sel = [x for x in rows if lo <= x[2] < hi]
        if sel:
            print(f"  start share {lo:.2f}-{min(hi, 1):.2f}: n={len(sel):3d} won {sum(x[4] for x in sel) / len(sel):.0%}  mean share {sum(x[2] for x in sel) / len(sel):.2f}  mean HU hands {sum(x[3] for x in sel) / len(sel):.1f}")
    for n in (2, 3, 4):
        sel = [x for x in rows if x[1] == n]
        if sel:
            print(f"  {n}p games: n={len(sel)} won {sum(x[4] for x in sel) / len(sel):.0%} mean start share {sum(x[2] for x in sel) / len(sel):.2f}")
    print("  net chips per heads-up hand by effective stack (BB):")
    for b in ("<=8", "8-15", "15-30", "30-60", ">60"):
        if net_by_eff[b][1]:
            print(f"    {b:6s} n={net_by_eff[b][1]:4d}  {net_by_eff[b][0] / net_by_eff[b][1]:+.3f} BB/hand")


if __name__ == "__main__":
    main()
