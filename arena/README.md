# arena/: local games, duplicate seeds, paired SPRT

```sh
make arena                                   # build/arena/arena (native -O3)
build/arena/arena --games 1000 --threads 4 --trials 5000 --opp station,jammer,random,folder
build/arena/arena --games 400 --mode pair --dup --sprt 0.02 --trials 20000 --opp station,jammer,random
build/arena/arena --games 40 --ms 20 --trials 100000      # fidelity mode: wall-clock budget per decision
python3 arena/freeze.py                      # freeze bot/bot.hpp as bot/bot_prev.hpp ("prev")
make arena-refs                              # frozen M1, M2.1, M2.3, OM1 from git -> build/arena/frozen/ (m1 m21 m23 om1)
make clones                                  # clones of the live bots -> data/clones/clones.txt (analysis/clone_fit.py)
build/arena/arena --games 4000 --dup --trials 5000 --opp clones --variants dev,om1,m1      # versions vs the live field
build/arena/arena --games 2016 --dup --trials 5000 --schedule data/clones/sched_om1.txt --variants om1,m1
```

| option | meaning |
|---|---|
| `--mix 41,34,25` | share of 4p / 3p / 2p tables (the live arena's mix) |
| `--opp a,b,c` | opponent pool, assigned round robin to the non-dev seats: `prev`, `station` (calls everything), `jammer` (preflop all-in 35% else fold, postflop call), `maniac` (shoves every hand preflop, calls postflop: most of the lower league), `random` (uniform over the offered actions), `folder`, `bigbet`, `limper`; `clone:<player>` (that live bot's fitted policy, below); `clones` (per seat a clone drawn by how often we meet that player live, distinct players at a table); `m1`, `m21`, `m23`, `om1` (frozen versions, when built) |
| `--dup` | play every seat rotation of each (seed, lineup) |
| `--mode pair` | also play each game with `prev` in dev's seat; report the paired payout difference (= `--variants dev,prev`) |
| `--variants a,b,c` | play every game once with each listed bot in dev's seat (`dev`, `prev`, `m1`, `m21`, `m23`, `om1`); report each, and its paired difference against the first, overall and by table size |
| `--schedule FILE` | game g takes its table size and opponents from line g mod L of FILE (`n name1 name2 ...`, a live run's tables from `analysis/clone_fit.py schedule`); each name plays as its clone, or the field model without one |
| `--clones FILE` | the fitted clones (default `data/clones/clones.txt`) |
| `ARENA_CLONECHECK=1` (env) | per situation, the clones' realised action frequencies next to their models' mean probabilities |
| `ARENA_TAGS=1` (env) | decision-tag histogram of each bot version |
| `ARENA_HANDLOG=path` (env) | per-hand ledger of the seat under test: players alive, depth, position, what happened before its first preflop action, that action, how the hand went, net big blinds (`analysis/arena_hands.py` aggregates it by regime) |
| variant `dev/key=value/...` | the dev bot with parameters changed (`open`, `iso`, `iso_bb`, `iso_limper`, `3bet`, `mwfold`, `om`, `hu_call`, `hu_callw`, `hu_gate`, `jf`, `evjam`, `evjam_m`): a parameter sweep in one run, on identical games |
| variant `hyb:<player>:<regime>` | the dev bot except in one regime, where that player's clone decides: `pre3s`/`pre3d` (preflop, 3-4 alive, <= / > 20 BB), `post3`, the same with `2` for heads-up, optionally narrowed to a clone situation (`pre2s.open`, `pre2s.bbopt`, `pre2s.vsraise`, `pre2s.vsjam`). A causal test of where a policy gives away payout |
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

## Clones of the live field (`clone.hpp`, `clone_data.cpp`, `analysis/clone_fit.py`)

A clone is a stochastic policy fitted to one live bot's recorded decisions. `clone_data` replays all 811
recorded games through the engine (811/811 exactly), each seat with a Tracker as the bot has, and writes the
features of every decision with the action taken (170,487 decisions; ours are left out of the fit). Per
situation (preflop unopened / big blind's option / facing a raise / facing an all-in call; postflop checked
to / facing a bet / facing an all-in call) the policy is a multinomial logit over fold-or-check, call, bet,
all-in on 18 features: hand strength (preflop percentile; postflop equity against a random hand), hand shape
(pair, suited, ace, broadway; made pair, two pair+, draw, top pair), effective depth, opponents, position, the
size faced, preflop aggressor, street, the heads-up phase. A player's weights are the field model's plus a
deviation fitted with an L2 penalty chosen per situation on held-out games. Bet sizes are drawn from the
player's recorded sizes made with the nearest strength and depth, in the unit the player sizes in (pot, big
blind or own stack: most top bots size in pot fractions, MaxFerrer in big blinds, UnTypedScript in fractions
of its stack). The features are computed by the same code for the training data and in play.

Validation (2026-10-02, 43 players with at least 8 recorded games):

- **Prediction**: held-out log-loss per decision 0.320, against 0.751 for the field model and 1.249 for
  uniform; each of the 42 players with held-out games is better predicted by their own model than by the field's
  (Tuo 0.92 -> 0.12, BrandV 0.77 -> 0.38).
- **Implementation**: realised clone action frequencies match the model probabilities in every situation.
- **Our bot sees the same game**: on a live run's schedule, the share of each of our bot's decision tags
  against the clones matches its live share within about one percentage point (M2.1 and OM1; e.g. M2.1's
  heads-up table calls 5.7% live / 5.7% arena, OM1's bluffs 5.7% / 5.3%).
- **Results**: each version replayed on its own live run's schedule (24 x 84 games, all rotations):

| run | arena | live | 2p arena / live |
|---|---|---|---|
| M1 on M1's schedule | 0.580 | 0.603 | 0.607 / 0.594 |
| OM1 on OM1's schedule | 0.612 | 0.622 | 0.612 / 0.609 |
| M2.1 on M2.1's schedule | 0.661 | 0.570 | 0.645 / 0.333 |

  OM1's finish-ahead rate against each opponent matches live where live has enough games (Waffle3z 0.52 live /
  0.51 arena, BrandV 0.50 / 0.56, Tux4711 0.65 / 0.67, Zylo 0.58 / 0.58). M2.1's live heads-up collapse (6 of
  18) is not reproduced: under the arena's estimate it has probability 0.9%, and the run's overall payout is
  2.1 standard errors below the prediction. Either it was bad luck, or something the clones lack: they are
  stationary, while some live bots may adapt to an opponent within a game.

The clones are the arena's realistic opponents; the scripted ones remain as extreme cases.

**Seeds (fixed 2026-10-02).** The game generator was seeded with seed x its own increment, so seed S + 1 replayed
seed S's stream shifted by one draw: runs with seeds 71 and 72 shared 393 of their first 400 games, 61 and 71
shared 398. Each run's paired comparison was valid, but a "confirmation on another seed" was largely the same
games. The seed now enters with a different multiplier (seeds 1 and 2 share none of 400 games); results before
the fix are reproducible only with the old binary.
