"""Leaderboard snapshot summary (data/snapshots/2026-09-30/cg_lb.json): leagues, scores, languages,
submission dates and the top 16, as quoted in docs/research/field.md section 1.

Usage: python3 analysis/leaderboard.py [leaderboard.json]      (<1 s; runs from any cwd)

`creationTime` is when the bot's current submission was created; `league.divisionIndex` 1 is the top league.
"""
import datetime, json, sys
from collections import Counter

from common import load_json, LEADERBOARD

users = load_json(sys.argv[1] if len(sys.argv) > 1 else LEADERBOARD)["users"]


def day(u):
    return datetime.datetime.fromtimestamp(u["creationTime"] / 1000, datetime.timezone.utc).strftime("%Y-%m-%d")


top = [u for u in users if u["league"]["divisionIndex"] == 1]
low = [u for u in users if u["league"]["divisionIndex"] == 0]
by_rank = {u["rank"]: u for u in users}
print(f"{len(users)} bots: top league {len(top)}, lower league {len(low)} "
      f"(divisionAgentsCount {low[0]['league']['divisionAgentsCount'] if low else '-'})")
print("scores: " + ", ".join(f"#{r} {by_rank[r]['pseudo']} {by_rank[r]['score']:.2f}" for r in (1, 10, 30) if r in by_rank))
print(f"last of the top league: #{top[-1]['rank']} {top[-1]['pseudo']} {top[-1]['score']:.2f}; "
      f"best lower-league bot: #{low[0]['rank']} {low[0]['pseudo']} {low[0]['score']:.2f}")
print(f"#1-#5 spread: {by_rank[1]['score'] - by_rank[5]['score']:.2f} points")
print("languages, all bots:", ", ".join(f"{k} {v}" for k, v in Counter(u["programmingLanguage"] for u in users).most_common()))
print("languages, top league:", ", ".join(f"{k} {v}" for k, v in Counter(u["programmingLanguage"] for u in top).most_common()))
print("CodinGame [CG] bots:", ", ".join(f"#{u['rank']} {u['pseudo']} {u['score']:.2f}" for u in users if u["pseudo"].startswith("[CG]")))
sep = Counter(day(u) for u in users if day(u).startswith("2026-09"))
print(f"submissions created in September 2026: {sum(sep.values())} ({', '.join(f'{d} {n}' for d, n in sorted(sep.items()))})")
print("\ntop 16 (rank, bot, language, score, submission created):")
for u in sorted(users, key=lambda u: u["rank"])[:16]:
    print(f"  {u['rank']:2d} {u['pseudo']:<17} {u['programmingLanguage']:<11} {u['score']:5.2f} {day(u)}")
