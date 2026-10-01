# oppmodel/

Offline test of an opponent model. It asks two questions about the live field:

1. How much better than a population model is a model that identifies which bot it is facing?
2. How quickly can a bot be fingerprinted from its own decisions within one game?

## Run

```
python3 analysis/analyze.py      # once: builds data/cache/replayed.pkl (371 games; ~1 min)
python3 archive/profiling/oppmodel/oppmodel2.py    # 13 s
```

The script reads `data/cache/replayed.pkl` and takes `PCT` and `hclass` from `analysis/analyze.py`. It runs from any working directory.

## Method (`oppmodel2.py`)

**Decision contexts.** Each decision in the 371 games becomes `(player, context, action)`. The context has these parts:

- street: preflop or postflop
- what the player faces: unopened, small bet, large bet, or all-in
- hand bucket: 5 preflop percentile buckets, or a 0-3 made-hand strength on later streets
- depth: below or above 15 BB
- preflop only: players still to act, capped at 3

There are 112 non-empty contexts. Actions fall into 5 classes: fold, check/call, small bet, large bet, jam.

**Evaluation.** Each target player-game with at least 20 decisions is scored with leave-one-game-out: the whole game is removed from the population table, and the target's decisions are removed from its own bot table.

Five predictors are compared on the target's next action:

- `pop`: population Dirichlet with +0.5 smoothing.
- `online`: Dirichlet with n0=3, centred on `pop`, updated with the target's own decisions so far in this game.
- `types`: Bayesian mixture over the 17 library bots (at least 15 games each) plus `pop`. Each bot table is shrunk toward `pop` with n0=4. The likelihood gets 3% uniform noise.
- `types+online`: `types` corrected with the target's in-game counts (n1=2).
- `oracle`: the target bot's own static table.

## Observed output (identical to the research run)

| log-loss (nats/decision) | pop | online | types | types+online | oracle |
|---|---|---|---|---|---|
| decisions 1-10 | 0.972 | 0.916 | 0.723 | 0.718 | 0.579 |
| 11-25 | 0.978 | 0.835 | 0.595 | 0.586 | 0.598 |
| 26-50 | 1.000 | 0.831 | 0.656 | 0.648 | 0.671 |
| 51+ | 0.976 | 0.815 | 0.689 | 0.674 | 0.706 |
| **overall** | **0.983** | 0.839 | 0.663 | **0.653** | 0.652 |

- Targets not in the library: `pop` 0.935, `types+online` 0.652. Targets in the library: 0.991 and 0.653.
- Top-1 identification among the 17 library bots after 5 / 10 / 20 / 40 of the target's own decisions: **43% / 66% / 81% / 90%** (n = 946 / 946 / 946 / 777).
- A player makes a median of 58 decisions per game (25th percentile 38).

## Caveats (from the research critique)

- **The oracle is not an upper bound.** It is a static per-bot Dirichlet table. It beats `types+online` only over decisions 1-10 (0.579 vs 0.718) and loses from decision 11 on (0.598 vs 0.586, 0.671 vs 0.648, 0.706 vs 0.674). The equal overall averages (0.652 vs 0.653) come from two opposite errors cancelling. Identification costs about 0.14 nats over the first 10 decisions. After that, the coarse context set limits accuracy, not identification.
- **Library and targets come from the same replay pool.** They share the same bot versions, the same day and the same opponents, and are separated only by leave-one-game-out. Fingerprinting a live bot, which may have been resubmitted, will be harder than this suggests. Held-out clones fitted to the same pool would repeat the same leak. The critique's fix is to split by time: build the library from older games and evaluate on newer ones.
- "Out-of-library" targets are players with fewer than 15 games, not held-out bots.

## Removed duplicates

- `oppmodel.py` was the earlier version of `oppmodel2.py`: the same code without the `types+online` predictor or the in-library / out-of-library split. `oppmodel2.py` prints a superset of its output.
- `steal.py` was superseded by `analysis/steal_se.py`, which prints identical tables plus standard errors. The steal and defend-rate numbers are in `analysis/README.md`.
