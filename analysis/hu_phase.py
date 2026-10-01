"""Heads-up phase of 3-4 player games: how many games reach heads-up, the HU share of decisions and the
effective stack (BB) at HU decisions.

Usage: python3 analysis/hu_phase.py      (reads data/cache/replayed.pkl from analysis/analyze.py; <1 s)

Port of the research run's one-off hu_phase.py (absolute path replaced; same counting). A hand is heads-up when
exactly 2 seats start it with chips; the effective stack is the smaller start-of-hand stack over the BB.
"""
import pickle, statistics as st

from common import REPLAYED_PKL

g = pickle.load(open(REPLAYED_PKL, "rb"))
res = {}
for gid, names, ranks, scores, text in g:
    N = len(names)
    reach = False
    dec_all = 0
    dec_hu = []
    start = {}
    for line in text.split("\n"):
        f = line.split("\t")
        if f[0] == "HAND":
            bb = int(f[3])
            start = {}
            for s in f[7:]:
                i, stk, c = s.split(":")
                if c != "OUT":
                    start[int(i)] = int(stk)
            if len(start) == 2:
                reach = True
        elif f[0] == "ACT":
            dec_all += 1
            if len(start) == 2:
                v = list(start.values())
                dec_hu.append(min(v) / bb)
    r = res.setdefault(N, dict(games=0, reach=0, dec=0, hu=[]))
    r["games"] += 1
    r["reach"] += reach
    r["dec"] += dec_all
    r["hu"] += dec_hu
for N in sorted(res):
    r = res[N]
    hu = r["hu"]
    print(N, "games", r["games"], "reachHU", r["reach"], "HUdec share %.3f" % (len(hu) / r["dec"]),
          "median eff BB %.1f" % st.median(hu), "<=30BB %.3f" % (sum(x <= 30 for x in hu) / len(hu)),
          "<=15 %.3f" % (sum(x <= 15 for x in hu) / len(hu)), "8-30 %.3f" % (sum(8 < x <= 30 for x in hu) / len(hu)))
