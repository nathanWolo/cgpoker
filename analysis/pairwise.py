"""Battle-list statistics (data/battles/, data/snapshots/2026-09-30/cg_lb.json): table-size mix, pairwise
finish-ahead rate p of the top 10, the score ~ p fit over the top 8, head-to-head results and wins by table size.

Usage: python3 analysis/pairwise.py          (no replays or cache needed; <1 s; runs from any cwd)

Port of the research run's one-off pairwise.py (absolute paths replaced, same counting rules), plus the
other battle-list numbers quoted in docs/research/field.md sections 2 and 5. A pair (bot, opponent) counts
only when the opponent is in the top 42 (the top league); `position` 0 is first place.
"""
import glob, gzip, json, math, os, statistics
from collections import Counter

import numpy as np

from common import DATA, load_leaderboard

users = load_leaderboard()["users"]
top42 = {u["pseudo"] for u in users if u["rank"] <= 42}
rank = {u["pseudo"]: u["rank"] for u in users}

battles = {}                     # nick -> battle list as fetched
games = {}                       # gameId -> players, finished games only, de-duplicated across lists
for f in sorted(glob.glob(os.path.join(DATA, "battles", "*.json.gz"))):
    with gzip.open(f, "rt") as fh:
        lst = json.load(fh)
    battles[os.path.basename(f)[:-len(".json.gz")]] = lst
    for b in lst:
        if b.get("done"):
            games[b["gameId"]] = b["players"]

sizes = Counter(len(p) for p in games.values())
print(f"battle lists {len(battles)} ({min(map(len, battles.values()))}-{max(map(len, battles.values()))} battles each); "
      f"unique finished games {len(games)}")
print("table sizes: " + ", ".join(f"{n}p {sizes[n]} ({sizes[n] / len(games):.0%})" for n in (4, 3, 2)))


def pairwise(nick):
    w = n = 0
    for ps in games.values():
        me = [p for p in ps if p.get("nickname") == nick]
        if not me:
            continue
        me = me[0]
        for p in ps:
            if p is me or p.get("nickname") not in top42:
                continue
            n += 1
            w += me["position"] < p["position"]
    return (w / n if n else float("nan")), n


print("\npairwise finish-ahead rate p against top-42 opponents (binomial; pairs in one game are correlated)")
rows = []
for u in sorted(users, key=lambda u: u["rank"])[:10]:
    p, n = pairwise(u["pseudo"])
    rows.append((u["rank"], u["pseudo"], u["score"], p, n))
    se = math.sqrt(p * (1 - p) / n) if n else 0.0
    print(f"{u['rank']:3d} {u['pseudo']:<12} score {u['score']:5.2f}  p {p:.3f}  pairs {n:3d}  SE {se:.3f}  +-1.96SE {1.96 * se:.3f}")

x = np.array([r[3] for r in rows[:8]])
y = np.array([r[2] for r in rows[:8]])
c = np.linalg.lstsq(np.vstack([np.ones(8), x]).T, y, rcond=None)[0]
print(f"fit over the top 8: score = {c[0]:.2f} + {c[1]:.2f} * p, r = {np.corrcoef(x, y)[0, 1]:.3f}")


def h2h(a, b):
    w = n = 0
    for ps in games.values():
        pa = [p for p in ps if p.get("nickname") == a]
        pb = [p for p in ps if p.get("nickname") == b]
        if pa and pb:
            n += 1
            w += pa[0]["position"] < pb[0]["position"]
    return w, n


print("\nhead to head (finished ahead / games shared)")
for a, b in [("Tuo", "Waffle3z"), ("Tuo", "kovi"), ("Tuo", "JuMaKre"), ("Tuo", "fr3sh2d3atH"), ("Waffle3z", "Tux4711")]:
    w, n = h2h(a, b)
    print(f"  {a} vs {b}: {w}/{n} ({w / n:.0%})")

print("\nwins by table size, each bot's own battle list (finished games)")
for nick in ("Waffle3z", "kovi"):
    cnt, win = Counter(), Counter()
    for b in battles[nick]:
        if not b.get("done"):
            continue
        n = len(b["players"])
        cnt[n] += 1
        win[n] += [p for p in b["players"] if p.get("nickname") == nick][0]["position"] == 0
    print(f"  {nick} ({sum(cnt.values())} battles): " + ", ".join(f"{n}p {win[n]}/{cnt[n]} ({win[n] / cnt[n]:.0%})" for n in (2, 3, 4)))

opp = [rank.get(p.get("nickname"), 10**6) for b in battles["Waffle3z"] for p in b["players"] if p.get("nickname") != "Waffle3z"]
print(f"\nWaffle3z's opponents: {sum(r <= 42 for r in opp) / len(opp):.0%} in the top 42, median rank {statistics.median(opp):g} (n = {len(opp)})")
