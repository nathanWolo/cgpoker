# analysis/

Statistics of the live CodinGame Poker field (public replays fetched on 2026-09-30) and of the game's structure.
All scripts run from any working directory. Paths, the replay loader and the named game sets live in `common.py`.

## Pipeline

```
data/replays/<gameId>.json.gz ──analyze.py (Java referee via replayer/replay_game.py)──> data/cache/replayed.pkl
data/cache/replayed.pkl ──> stats2.py, steal_se.py, hu_phase.py, regimes.py
data/replays + sim/poker_sim.py ──> elim_depth.py, hand_outcomes.py, replay_stats.py, stack_depth.py
data/snapshots/2026-09-30/cg_lb.json + data/battles ──> leaderboard.py, pairwise.py   (no replays or cache needed)
```

`data/cache/` is gitignored. Rebuild it with `python3 analysis/analyze.py` (or `make cache`).

The per-bot style scripts (`stats.py`, `stats3.py`) and the opponent model moved to
[`archive/profiling/`](../archive/profiling/README.md) on 2026-10-01: the plan no longer profiles
individual bots.

**Game sets.** The research numbers were computed on two overlapping subsets of the 381 replays in `data/replays`. Scripts default to the subset their numbers came from. Pass `all`, the other set name, or a path to an id list to change it.

| name | id list | games | used by |
|---|---|---|---|
| `analysis371` | `tools/sel_games.txt` | 371 (86 HU, 131 3p, 154 4p) | `analyze.py`, so everything that reads `replayed.pkl` |
| `validation120` | `tools/sim_games.txt` | 120 (27 / 45 / 48) | `elim_depth.py`, `hand_outcomes.py`, `replay_stats.py`, `stack_depth.py empirical` |

The two sets share 110 games.

**Driver output.** Each game in `replayed.pkl` is `(gameId, names, ranks, scores, text)`. `text` is the tab-separated event log from `replayer/` (`START`, `HAND`, `ACT`, `END`, `GAMEOVER`/`CANCEL`, `SCORES`); the format is documented in `replayer/README.md`. Names are CodinGame pseudos, and the league boss appears as `BOSS<agentId>`.

**`preeq.tsv`.** Monte Carlo preflop equity of each of the 169 hand classes against 1, 2 and 3 random hands. `analyze.py` turns column 1 (heads-up) into `PCT`, a "top X% of hands" percentile weighted by combos. The file was produced by `replayer/src/com/codingame/game/PreEq.java`. `java -cp replayer/build com.codingame.game.PreEq 40000 > analysis/preeq.tsv` reproduces it byte for byte in about 6 s.

## Scripts and observed output

Timings are wall-clock in this 4-core container. Every output below is byte-identical to the original scratch scripts run on the same inputs, except `pairwise.py` (same values, reformatted) and the new `leaderboard.py`; see the note at the end.

| script | runtime | what it measures |
|---|---|---|
| `analyze.py [set]` | 57 s | Replays each game through the referee's own Java classes → `data/cache/replayed.pkl`. Prints `replayed 371 bad 0`. |
| `stats2.py` | <1 s | Showdown share, all-in rates, BB won per hand by effective-stack bucket, mean finishing place. |
| `steal_se.py` | <1 s | First-in raises: P(everyone folds) and the opener's net BB with standard errors, by table size, size bucket and hand bucket. |
| `elim_depth.py [set]` | 2 s | Start-of-hand stack (BB) of each eliminated player (timeouts excluded). |
| `hand_outcomes.py [set]` | 2 s | How hands end (fold pre/postflop, showdown) and all-in rates by blind level. |
| `replay_stats.py [set]` | 2 s | Hands and rounds per game, 600-round-cap hits, actions per hand, action mix by level. Parsed from the replay JSON, with no simulation. |
| `stack_depth.py [theory\|empirical\|policy\|all] [set]` | 31 s (all) | `theory`: blinds, average stack in BB and M by hand, saved as `stack_depth_table.txt`. `empirical`: effective stack at each real decision. `policy`: game length for simple bots in `poker_sim`. |
| `hu_phase.py` | <1 s | 3-4 player games that reach heads-up, the HU share of their decisions, and the effective stack at HU decisions (plan §2 fact 4). |
| `regimes.py` | <1 s | Decision mix by regime: HU vs 3-4 live players, preflop vs postflop, effective-stack bucket (plan §2 fact 13). |
| `pairwise.py` | <1 s | From `data/battles/`: table-size mix, pairwise finish-ahead rate p of the top 10, the score ~ p fit over the top 8, head-to-head results, wins by table size. |
| `leaderboard.py` | <1 s | From `cg_lb.json`: league sizes, scores, languages, submission dates, the top 16 (`docs/research/field.md` §1). |

Key numbers (default sets):

- **stats2.py**: 15,969 hands, of which 12,759 were uncontested and 3,210 went to showdown (80% ended without a showdown). 19.2% of hands contained an all-in and 15.3% a preflop all-in. Waffle3z won +0.46 BB/hand overall, +0.56 at 20-40 BB and +0.94 at 40-80 BB.
- **steal_se.py**: The size bucket `<=3` means open-to in (2, 3] BB, `<=2` means ≤2 BB, and `(<15bb)` splits off openers with less than 15 BB. A `<=3` open wins the blinds uncontested HU 44.1% (n=1609), 3p 34.4% (n=1812) and 4p 23.7% (n=832). The research text calls these "opens of 3 BB or less", but they exclude the `<=2` bucket. Opener net BB/hand: 4p `<=2` +2.89 ± 1.64 (n=64); 4p `<=3` bottom-40% hands −1.14 ± 0.87 (n=247); 4p `<=3` top-10% +7.86 ± 3.17 (n=128); HU `<=3` bottom-40% +0.20 ± 0.30 (n=467). These estimates are noise-dominated.
- **elim_depth.py** (120 games): the eliminated player had ≤10 BB in 44% / 54% / 53% of busts and ≤20 BB in 70% / 79% / 69% (2p / 3p / 4p starts; 27 / 90 / 144 eliminations). The median bust is at 10.9 / 8.4 / 9.7 BB. On `analysis371`: 808 eliminations, ≤10 BB 51% / 60% / 56% and ≤20 BB 76% / 76% / 70%, about 57% / 72% pooled. The research's "56% / 72% of 810 bust-outs" is close to this, but no script here produces it; the 2 missing busts are the timeouts, which this script excludes.
- **hand_outcomes.py** (120): Hands 1-9 end with a preflop fold 44% of the time and reach showdown 19%; 6% contain a preflop all-in. That rate is 26% in hands 30-39, 48% in 40-49 and 54% in 50-59.
- **replay_stats.py** (120): 27 / 45 / 48 games with median 42 / 46 / 46 hands. With `analysis371`: 86 / 131 / 154 games, median 40 / 47 / 46 hands, 0 hit the 600 cap. The most rounds in any game was 230 / 311 / 339 and no game hit the 600 cap. 4-player actions per hand by blind level: 7.3, 6.67, 4.88, 3.23, 2.44, 2.24, 1.61.
- **stack_depth.py**: `theory` output equals `stack_depth_table.txt`. `empirical` (120 games): 11.8% / 15.8% / 17.4% of decisions have an effective stack of ≤15 BB, and 64% / 52% / 51% have more than 50 BB. Median effective stack by 10-hand block for HU: 237, 113, 49.1, 23.2, 11.1, 4.6, 2.2 BB. `policy` (200 simulated games each): 4 calling stations hit the 600-round cap 100% of the time at hand 38, 3 stations 100% at about hand 51, and HU stations 3%.
- **hu_phase.py** (371 games): 86/86 HU, 130/131 3p and 147/154 4p games reach heads-up. HU decisions are 26.1% / 19.1% of the 3p / 4p games' decisions, at a median effective stack of 18.0 / 13.7 BB; 69% / 81% of them are at ≤30 BB and 45% / 49% at 8-30 BB.
- **regimes.py** (371 games, 72,341 decisions): HU preflop ≤8 BB 4.4%, 8-30 BB 7.3%, >30 BB 7.3%; HU postflop 14.9%; 3-4p preflop ≤15 BB 5.3%, >15 BB 36.4%; 3-4p postflop 24.4%.
- **pairwise.py**: 2,495 unique finished games in 37 battle lists, 41% 4p / 34% 3p / 25% HU. p against top-42 opponents: Waffle3z 0.611 (429 pairs), kovi 0.577, BrandV 0.569, Zylo 0.562, Tuo 0.541, JuMaKre 0.553, fr3sh2d3atH 0.495, Tux4711 0.453, MaxFerrer 0.483, AGSigma 0.598. Fit over the top 8: score = 21.13 + 15.30·p, r = 0.961. Tuo finishes ahead of Waffle3z 24/43 and of kovi 11/18.
- **leaderboard.py**: 192 bots (42 in the top league); #1 Waffle3z 30.65, #10 27.67, #30 25.12; 19 submissions created in September 2026, 10 of them on 09-25.

**Our live runs and the arena clones** (`sim/poker_sim.py` reconstruction of every game):

| script | what it does |
|---|---|
| `postmortem.py --pseudo flawedaxioms --battles B` | our games from a battle list: reproduction check, placements and payout by table size, opponents, busts, chips by depth |
| `headsup.py`, `shortstack.py` | the heads-up phase and short-stack play of a run |
| `retrace.py` | replays a run through a bot binary: its decision tag and model note per decision |
| `om_notes.py`, `om_impact.py` | the opponent model's end-of-game reads; where it changed live decisions and what those hands won |
| `tendencies.py` | per-player tendencies and how fast they show within a game |
| `clone_fit.py data / fit / schedule` | the arena clones (`arena/README.md`): training data, fitting with held-out validation, a run's schedule |

`hu_phase.py`, `regimes.py` and `pairwise.py` are the research run's one-off scripts of the same names, with the absolute paths replaced. `hu_phase.py` and `regimes.py` print byte-identical output to the originals. `pairwise.py` computes the same numbers (identical values, reformatted), and also prints the 1.96·SE interval and the other battle-list numbers quoted in `docs/research/field.md` §2 and §5. `leaderboard.py` is new.
