"""Decision mix by regime: heads-up vs 3-4 live players, preflop vs postflop, effective-stack bucket.

Usage: python3 analysis/regimes.py       (reads data/cache/replayed.pkl from analysis/analyze.py; <1 s)

Port of the research run's one-off regimes.py (absolute path replaced; same counting). Effective stack =
min(own, largest other) start-of-hand stack, in BB; the live-player count is the number of seats that start
the hand with chips.
"""
import pickle
from collections import Counter

from common import REPLAYED_PKL

g = pickle.load(open(REPLAYED_PKL, "rb"))
C = Counter()
tot = 0
for gid, names, ranks, scores, text in g:
    start = {}
    bb = 10
    for line in text.split("\n"):
        f = line.split("\t")
        if f[0] == "HAND":
            bb = int(f[3])
            start = {}
            for s in f[7:]:
                i, stk, c = s.split(":")
                if c != "OUT":
                    start[int(i)] = int(stk)
        elif f[0] == "ACT":
            pid = int(f[3])
            board = f[4]
            me = start[pid]
            oth = max(v for k, v in start.items() if k != pid)
            eff = min(me, oth) / bb
            n = len(start)
            pre = board == "-"
            if n == 2:
                reg = ("HU pre <=8" if pre and eff <= 8 else "HU pre 8-30" if pre and eff <= 30
                       else "HU pre >30" if pre else "HU post")
            else:
                reg = "3-4p pre <=15" if pre and eff <= 15 else "3-4p pre >15" if pre else "3-4p post"
            C[reg] += 1
            tot += 1
for k, v in sorted(C.items()):
    print("%-15s %6d %.3f" % (k, v, v / tot))
print(tot)
