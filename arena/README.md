# arena/: local games, duplicate seeds, paired SPRT

```sh
make arena                                   # build/arena/arena (native -O3)
build/arena/arena --games 1000 --threads 4 --trials 5000 --opp station,jammer,random,folder
build/arena/arena --games 400 --mode pair --dup --sprt 0.02 --trials 20000 --opp station,jammer,random
build/arena/arena --games 40 --ms 20 --trials 100000      # fidelity mode: wall-clock budget per decision
python3 arena/freeze.py                      # freeze bot/bot.hpp as bot/bot_prev.hpp ("prev")
```

| option | meaning |
|---|---|
| `--mix 41,34,25` | share of 4p / 3p / 2p tables (the live arena's mix) |
| `--opp a,b,c` | opponent pool, assigned round robin to the non-dev seats: `prev`, `station` (calls everything), `jammer` (preflop all-in 35% else fold, postflop call), `maniac` (shoves every hand preflop, calls postflop: most of the lower league), `random` (uniform over the offered actions), `folder` |
| `--dup` | play every seat rotation of each (seed, lineup) |
| `--mode pair` | also play each game with `prev` in dev's seat; report the paired payout difference |
| `--sprt D1` | Gaussian SPRT on the paired differences, H0 mean 0 vs H1 mean D1 payout units, α = β = 0.05 (bounds ±2.94) |
| `--trials K` / `--ms M` | fast mode (fixed Monte Carlo trials) / fidelity mode (wall-clock budget) |
| `--log FILE` | one line per game: variant, seed, lineup, scores, payouts |
| `ARENA_TRACE=1` (env) | print every dev decision of the first game to stderr |
| `BOT_JF=N` (env) | set the dev bot's heads-up jam/fold depth (BB) for tuning |

Payout is the TrueSkill-implied placement value ((1,0), (1,.5,0), (1,.6444,.3556,0); ties share).
"dev finishes ahead of X" is the pairwise finish-ahead rate against each opponent type.

Observed (M0 baseline, 4 threads, this sandbox):

- fast mode, 1,000 games vs station/jammer/random/folder: 426 games/s; mean payout 0.815, first
  place 73%; ahead of station 0.93-0.95, random 0.79-0.81, jammer 0.78, folder 0.85; 0 desyncs,
  0 replaced actions.
- paired dev vs an identical prev, 100 games with `--dup`: mean difference exactly 0.0000
  (n = 312): the pairing and the bots' observation-seeded RNG are deterministic.
- fidelity mode (`--ms 20 --trials 100000`): 2.6 ms mean, 7.5 ms max per decision; the trial cap
  binds before the budget.

The scripted opponents are sanity checks, not tuning targets (docs/plan.md §5). Timed SPRTs should
run alone on physical cores.
