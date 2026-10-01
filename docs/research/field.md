# The CodinGame Poker field (snapshot 2026-09-30)

This document describes the field we play in: the leaderboard, ranking mechanics, how close the
top is, and where games are decided. Per-bot style profiles and weaknesses were moved to
[`archive/profiling/field_profiles.md`](../../archive/profiling/field_profiles.md) on 2026-10-01,
because the plan no longer models individual bots. Sources: `research[1]` in
[`raw/workflow_results.json`](raw/workflow_results.json), re-checked against the data in this repo.

**Data and sample sizes.** Read every percentage with its sample size in mind.

| dataset | what | size |
|---|---|---|
| `data/snapshots/2026-09-30/cg_lb.json` | Leaderboard dump | 192 bots |
| `data/battles/<nick>.json.gz` | Recent battle lists of the top 37 bots | 69-443 battles per bot; 2,495 unique finished games |
| `data/replays/` (`tools/sel_games.txt`) | Replays rebuilt with every hole card by `replayer/` → `data/cache/replayed.pkl` | 371 games (86 HU, 131 3p, 154 4p), 15,969 hands |

The 371 replays were chosen from the top 37 bots' battle lists, so they are **heavily weighted
toward the top 6**. The selection rule was not preserved (`tools/README.md`). Waffle3z appears in
4,843 hands, but many top-league bots appear in only 150-900 hands. Statistics for those bots
have wide error bars.

Provenance labels:

- **reproduced**: re-run here by the named script;
- **ad hoc**: a one-off computation on repo data, with no committed script (none remain in this
  document: §1-§2 and §5 are now reproduced by `analysis/leaderboard.py` and `analysis/pairwise.py`);
- **research**: taken from the research run and not re-run.

---

## 1. Leaderboard structure

- **Size.** 192 bots in 2 leagues. The top league has 42 bots. The lower league has 150 players
  plus its boss (`divisionAgentsCount` 151).
- **Scores.** #1 Waffle3z 30.65, #10 27.67, #30 25.12. The top league's last bot scores 7.11. The
  best lower-league bot, OzzGnan (#43), scores 24.47.
- **Languages, all 192 bots.** Python3 90, C++ 26, C# 16, Java 14, JavaScript 14, PHP 7, C 6,
  Rust 5, TypeScript 5, other 9.
- **Languages, top league.** Python3 20, C++ 7, Java 3, C 3, TypeScript 3, JavaScript 2, Rust 2,
  C# 2.
- **CPU time is not what separates the top.** Python bots hold #3, #5 and #6.
- **CodinGame's own LLM bots are in the top league**: [CG]Gemini-Python #26 (25.42),
  [CG]OpenAIGPT4o-Python #35 (21.54), [CG]MistralCodestral-Python #36 (21.30). Five more sit at the
  bottom of the top league, scoring 7.11-15.99. Reaching the top league takes little.
- **Activity.** 19 of the 192 current submissions were created in September 2026, 10 of them on
  2026-09-25. Three of the top 6 resubmitted in the last week: Waffle3z (#1) on 09-25, BrandV (#3)
  on 09-29 and JuMaKre (#6) on 09-30, as did AGSigma (#10) and babaaurhum (#12) on 09-25. kovi
  (#2) has not resubmitted since 2026-04-05. (The research text said "the top 3 all resubmitted";
  `creationTime` in `cg_lb.json` shows that #2 did not.) The puzzle was created on 2023-11-19 by
  wala, who is #16 (`cg_findProgressByPrettyId.json`).
- **Public material.**
  - The forum thread (<https://forum.codingame.com/t/community-puzzle-poker/202371>) had 0 replies
    and 728 views.
  - No post-mortems were found.
  - The only public bot code is [gpoussel/copro](https://github.com/gpoussel/copro)
    `src/contests/cg/multi/poker.ts` (177 lines at commit `a43f874`, MIT license). It
    runs Monte Carlo equity against random hands for 40 ms, bets the pot when equity is above 0.75,
    and calls when equity is above the pot odds plus 0.05. Its author gpoussel_ is #45 (23.88).
  - Discussion probably happens on Discord, which we could not read.

Top 16 (reproduced, `analysis/leaderboard.py` on `cg_lb.json`, which also prints every other number
in this section; "created" is the submission's creation date):

| # | bot | lang | score | created | # | bot | lang | score | created |
|---|---|---|---|---|---|---|---|---|---|
| 1 | Waffle3z | C++ | 30.65 | 2026-09-25 | 9 | MaxFerrer | JS | 28.16 | 2023-12-19 |
| 2 | kovi | C++ | 30.30 | 2026-04-05 | 10 | AGSigma | Py | 27.67 | 2026-09-25 |
| 3 | BrandV | Py | 29.64 | 2026-09-29 | 11 | insomnia_rooster | Py | 27.52 | 2025-07-01 |
| 4 | Zylo | Java | 29.57 | 2026-09-01 | 12 | babaaurhum | C | 27.49 | 2026-09-25 |
| 5 | Tuo | Py | 29.38 | 2023-12-19 | 13 | supernakash | Py | 27.46 | 2026-03-22 |
| 6 | JuMaKre | Py | 29.29 | 2026-09-30 | 14 | SmogyY | C | 27.44 | 2025-12-19 |
| 7 | fr3sh2d3atH | Py | 28.66 | 2024-03-21 | 15 | M_C | Py | 27.35 | 2026-08-06 |
| 8 | Tux4711 | Java | 28.22 | 2024-01-29 | 16 | wala | Java | 27.16 | 2023-11-19 |

## 2. Table sizes and opponents

- **Table sizes.** Of the 2,495 unique battles: 1,012 four-player (41%), 854 three-player (34%),
  629 heads-up (25%) (reproduced, `analysis/pairwise.py`, as is the rest of this section).
- **Waffle3z's own last 134 battles** were 57 four-player, 51 three-player and 26 heads-up.
- **Opponents.** The top bots play almost only top-league bots. 99% of Waffle3z's opponents are in
  the top 42, and their median rank is 6 (the research text says 5).

## 3. Ranking mechanics

- **TrueSkill on finishing order only.** Elimination order decides places, then stack for any
  survivors at the 600-round cap ([rules.md §7](rules.md#7-scoring-elimination-and-timeouts)).
  **Chip margin is ignored.** CodinGame staff confirm TrueSkill mean/deviation ranking
  (<https://forum.codingame.com/t/multiplayer-contests-ranking-system/381>).
- **Each submission:**
  - plays 10 parallel placement games against opponents from the whole leaderboard;
  - then plays about 100 more, one after another;
  - must finish ahead of the boss to be promoted
    (<https://forum.codingame.com/t/how-is-the-rank-for-single-finished-bot-programming-computed/194147>).

  The displayed formula (μ − 3σ or a variant) and the exact game count for this arena are not
  documented. JuMaKre, submitted the same day as the snapshot, already had 84 battles.
- **Payouts.** At equal ratings, TrueSkill's μ updates make a finishing place worth:

  | players | payouts |
  |---|---|
  | 2 | (1, 0) |
  | 3 | (1, .5, 0) |
  | 4 | (1, .6444, .3556, 0) |

  These are reproduced with `solvers/trueskill_payouts.py`. At unequal ratings the 4-player payouts
  shift, for example to (1, .623, .325, 0) at σ = 1 for μ = 30 against 29/28/27. So 2nd vs 3rd
  matters at 3-4 players, and survival (ICM) changes the right ranges
  ([literature.md §2](literature.md#2-short-stack-play-pushfold-and-icm)).
- **Score vs placement.** Over the top 8, score ≈ 21.13 + 15.30·p with r = 0.961, where p is the
  pairwise finish-ahead rate of §5 (reproduced, `analysis/pairwise.py`; the research quoted
  21.12 + 15.31·p, r = 0.962).
  Use it only as a rough guide:
  - p is measured against each bot's own matchmade opponents, so it falls as a bot climbs;
  - AGSigma (#10) has p = 0.598, which the fit would place at #2.
- **Rating noise.** One submission of about 100 games measures p only to about ±5 pp, about ±0.75
  score points (research critique). #1 to #5 are within 1.3 points of each other.

## 4. How the top bots play

Moved to [`archive/profiling/field_profiles.md`](../../archive/profiling/field_profiles.md): the
current plan does not profile individual bots.

## 5. How close the top is, and non-transitivity

**Pairwise finish-ahead rate p.** For each game, count the pairs (bot, top-42 opponent) in which
the bot finished ahead (reproduced, `analysis/pairwise.py` on `data/battles/`):

| # | bot | p | pairs | ± (1.96 binomial SE) |
|---|---|---|---|---|
| 1 | Waffle3z | 0.611 | 429 | 0.046 |
| 2 | kovi | 0.577 | 241 | 0.062 |
| 3 | BrandV | 0.569 | 239 | 0.063 |
| 4 | Zylo | 0.562 | 395 | 0.049 |
| 5 | Tuo | 0.541 | 242 | 0.063 |
| 6 | JuMaKre | 0.553 | 188 | 0.071 |
| 7 | fr3sh2d3atH | 0.495 | 285 | 0.058 |
| 8 | Tux4711 | 0.453 | 223 | 0.065 |
| 9 | MaxFerrer | 0.483 | 211 | 0.067 |
| 10 | AGSigma | 0.598 | 189 | 0.070 |

Pairs within one game are correlated, so the real uncertainty is larger than the binomial interval.
**#1 finishes ahead of a given top-league opponent only about 61% of the time.** #2-#6 are at
54-58%.

**Head to head** (reproduced, `analysis/pairwise.py`; finished ahead / games shared):

| bot | opponent | result |
|---|---|---|
| Tuo (#5, pure push/fold) | Waffle3z (#1) | 24/43 (56%) |
| Tuo | kovi (#2) | 11/18 (61%) |
| Tuo | JuMaKre (#6) | 6/17 (35%) |
| Tuo | fr3sh2d3atH (#7) | 6/17 (35%) |
| Waffle3z | Tux4711 (#8) | 27/37 (73%) |

**Matchups are not transitive.** A plain push/fold bot beats the top two and loses to the
equity-vs-pot-odds bots. The game counts are small.

**Win rate by table size** (reproduced, `analysis/pairwise.py`, each bot's own battle list; random play would give
50 / 33 / 25%):

| bot | HU | 3p | 4p |
|---|---|---|---|
| Waffle3z | 16/26 (62%) | 28/51 (55%) | 24/57 (42%) |
| kovi | 7/11 (64%) | 14/43 (33%) | 9/48 (19%) |

kovi is #2 while winning only 19% of its 4-player games.

## 6. Common weaknesses

Also moved to [`archive/profiling/field_profiles.md`](../../archive/profiling/field_profiles.md).

## 7. Where games are decided

- **Bust-outs happen short.** On the 371 games, 808 eliminations (timeouts excluded) happened with
  the busted player at ≤10 BB 57% of the time and at ≤20 BB 72% (pooled, `analysis/elim_depth.py
  analysis371`). The research quoted 56% and 72% of 810, which no committed script reproduces
  exactly.
- **Few showdowns, some all-ins.** Of 15,969 hands, 80% ended without a showdown, 19.2% contained an
  all-in, and 15.3% contained a preflop all-in (reproduced, `analysis/stats2.py`).
- **Chips move short** (research). 51% of chips won moved at an effective stack under 20 BB.
- **Game length.** Games last a median of 40 / 47 / 46 hands (HU / 3p / 4p starts), and none of the
  371 reached the 600-round cap. The median bust is at hand 34 (research).

The structural reason is in [rules.md §11-12](rules.md#11-stack-depth-by-blind-level): blinds
double every 10 hands and there are only 4,800 chips.

## 8. Implications

1. **The field is beatable but noisy.** No top bot runs a solver. #1's edge is about 61% pairwise,
   and #2-#6 are 54-58%. Proving a gain of 2-6 pp needs hundreds of games, far more than one
   ~100-game submission.
2. **Short-stack play matters most.** Push/fold and reshove play at 5-25 BB for 2-4 players is
   small enough to solve almost exactly offline, and it is where most bust-outs happen.
3. **Optimise placement, not chips.** 75% of tables have 3-4 players, where ICM changes the
   calling ranges.
4. **Bots differ wildly in style.** That is why an equilibrium bot can win without modelling
   anyone: equilibrium play profits from every opponent's mistakes without having to identify them.
5. **The target moves.** #1, #3 and #6 resubmitted in the week before the snapshot. Rankings drift
   even if our bot does not change.

Unresolved questions about the field (the boss's code and score, the exact score formula, what
happened on 2026-09-25) are tracked in [`../open_questions.md`](../open_questions.md).
