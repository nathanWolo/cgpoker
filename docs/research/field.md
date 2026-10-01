# The CodinGame Poker field (snapshot 2026-09-30)

This document describes who we are playing against: the leaderboard, how the top bots play, how
close the top is, and the field's common weaknesses. Sources: `research[1]` in
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

Provenance labels follow [`../plan.md`](../plan.md):

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

The numbers below are reproduced with `analysis/stats.py` and `analysis/stats3.py` on the 371
replays. "Hands" is that bot's sample size. Column definitions:

- **VPIP / PFR**: share of hands where the bot voluntarily put chips in / raised preflop.
- **open**: share of unopened pots the bot raises or shoves.
- **fold vs raise**: share of times the bot folds when it faces a preflop raise.
- **call AI**: share of all-ins faced that the bot calls; n is the number of all-ins faced.
- **bet when checked to**: postflop.
- **fold vs bet**: postflop, facing a bet.

| # | bot | hands | VPIP / PFR | open | fold vs raise | call AI (n) | bet when checked to | fold vs bet | notes |
|---|---|---|---|---|---|---|---|---|---|
| 1 | Waffle3z | 4,843 | .70 / .64 | .91 | .51 | .33 (169) | .97 | .62 | Loose-aggressive steal machine. Opens to 2 BB 22%, 5 BB 15%, 6 BB 11%. Postflop bets 0.5 pot 36%, 0.1 pot 26%. Calls all-ins with a median top-21% hand. Prints no messages. |
| 2 | kovi | 4,082 | .26 / .12 | .13 | .79 | .36 (167) | .51 | .44 | Tight, rule-based. Limps 22% of unopened pots. Postflop bets are always pot-sized. |
| 3 | BrandV | 3,682 | .63 / .41 | .64 | .21 | .63 (51) | .35 | .44 | Loose. Calls raises 68%. First preflop action is ALL-IN 66% at 10-20 BB and 90% below 10 BB. |
| 4 | Zylo | 4,458 | .40 / .32 | .54 | .72 | .32 (151) | .33 | .45 | Tight-aggressive. |
| 5 | Tuo | 3,714 | .18 / .18 | .20 | .85 | .59 (58) | .00 | 1.00 | Pure push/fold, even 120-240 BB deep. Median shove depth 20 BB; still shoves 8% of first actions at 50 BB or more. Postflop it only checks or folds. |
| 6 | JuMaKre | 2,817 | .53 / .32 | .46 | .45 | .59 (37) | .33 | .41 | Monte Carlo equity vs pot odds (from its messages). |
| 9 | MaxFerrer | 1,688 | .55 / .54 | .72 | .64 | .57 (28) | .25 | .82 | GTO push/fold chart plus exploit rules (from its messages). Opens to 2.5 BB 90% of the time. |
| 10 | AGSigma | 655 | .09 / .09 | .11 | .97 | .17 (65) | .00 | 1.00 | Pure push/fold. |

Debug messages give away several methods. Message templates, with counts (reproduced from
`data/cache/stats.pkl`):

| bot | templates |
|---|---|
| kovi | `# rule # #` (2,427), `# rnd` (259), `# shortfold-vs-raise` (236), `# anti-maniac` (145), `# push #bb #way` (92) |
| JuMaKre | `check e=#` (1,036), `pf call e=# need=#` (430), `fold e=# po=#` (402) |
| supernakash | `eq: #,#s` (2,853), i.e. about 45 ms of Monte Carlo per turn |
| kozlov-ma | `eq=# (raw # #) po=# edge=+# tex=# call` |
| MaxFerrer | `XPT.PF.MB` (642), `GTO.PF.CF` (455), `GTO.PF.RSHV` (252) |
| Tux4711 | `D - F #`, `D - O #`, ... |

Waffle3z, BrandV, Zylo, Tuo and AGSigma print nothing. **No top bot shows signs of a solver or a
learned policy.** The field is rules, charts and equity-versus-pot-odds.

**Where Waffle3z wins** (reproduced, `analysis/stats2.py`). It nets +0.46 BB/hand overall. By
effective stack:

| effective stack | BB/hand | hands |
|---|---|---|
| <10 BB | +0.06 | 583 |
| 20-40 BB | +0.56 | 1,007 |
| 40-80 BB | +0.94 | 1,146 |

So stealing and aggression pay off in the middle phase. These means have no standard errors and
are noise-dominated.

**The live lower-league boss** (agent 5117680) behaves differently from the
`CALL;CALLING STATION` template in the referee repo. It limps 43% of unopened pots and folds 95%
when raised (n = 55, 343 hands).

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

Reproduced with `analysis/stats3.py` and `analysis/stats.py` unless marked.

**Over-folding to small preflop opens** (fold rate facing an open to ≤3 BB):

| folds a lot | | folds little | |
|---|---|---|---|
| kovi | .78 (n=1,021) | Waffle3z | .35 (n=1,039) |
| insomnia_rooster | .77 (n=173) | BrandV | .09 (n=794) |
| Zylo | .68 (n=854) | babaaurhum | .07 (n=54) |
| wala | .67 (n=92) | SmogyY | .00 (n=99) |
| M_C | .64 (n=144) | UnTypedScript | .00 (n=95) |
| MaxFerrer | .55 (n=429) | | |

A pure steal breaks even at these fold rates:

| open to | HU | 3p | 4p |
|---|---|---|---|
| 2 BB | 50% | 43% | 37.5% |
| 2.5 BB | 57% | 50% | 44% |

These figures count the opener's own posted SB, which comes back on a successful steal (research
plan; the research's "Design 2" figures of 67/57/50% were wrong).

In the replays, opens to (2, 3] BB win the blinds uncontested 44.1% of the time HU (n = 1,609),
34.4% at 3p (n = 1,812) and 23.7% at 4p (n = 832) (`analysis/steal_se.py`). The steal EVs
themselves are noise-dominated: for example, 4p opens to ≤2 BB net +2.89 ± 1.64 BB (n = 64).

**Over-folding to small postflop bets** (facing about half pot or less):

| bot | fold rate | n |
|---|---|---|
| insomnia_rooster | .86 | 69 |
| MaxFerrer | .83 | 152 |
| Tux4711 | .71 | 297 |
| fr3sh2d3atH | .57 | 605 |
| Waffle3z | .54 | 367 |

**Limpers and calling stations** (share of unopened pots limped): kozlov-ma .87 (n = 200),
trictrac .80 (96), babaaurhum .71 (221), gpoussel_ .65 (1,115), supernakash .60 (726). Value-bet
them thin and do not bluff them.

**Maniacs.**

- SmogyY raises 94% of unopened pots, to a median 50.5 BB.
- UnTypedScript raises 95%, to a median 25.5 BB.
- Both called every all-in they faced, but n = 9 and n = 12. Their median calling hands are in the
  top 61% and 67%.
- Call off wider against them.

**Exploitable patterns in the top two.**

- Waffle3z bets 97% when checked to, then folds 62% when it faces a bet. That invites
  check-raises.
- Tuo and AGSigma only check or fold postflop, and fold 85% and 97% when raised preflop. Their blinds can be stolen.

**Timeouts happen in the top league.** deuwii timed out on its first action in game 906358479, and
[CG]CohereCommandA-Python in game 906523528. A timeout eliminates the bot.

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
4. **The weaknesses have direct counters:**
   - small steals against the over-folders;
   - small, frequent postflop bets against bots that fold 70-86% to them;
   - thin value against stations;
   - wider call-offs against maniacs;
   - check-raises against Waffle3z.
5. **Every hand reveals all cards**, so classifying opponents online (nit, push/fold, station,
   maniac, LAG) within one 40-70-hand game is realistic. Names are not in the input. Open-size
   signatures fingerprint bots: MaxFerrer opens to 2.5 BB 90% of the time, kozlov-ma to 2.0 BB 100%.
6. **The target moves.** #1, #3 and #6 resubmitted in the week before the snapshot, and battle
   histories vanish when a bot resubmits. Harvest replays early and keep timestamps (research
   critique).

Unresolved questions about the field (the boss's code and score, the exact score formula, what
happened on 2026-09-25) are tracked in [`../open_questions.md`](../open_questions.md).
