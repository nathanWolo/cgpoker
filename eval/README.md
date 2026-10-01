# eval/

These scripts measure how much duplicate seeds reduce variance when comparing bots in the local simulator (`sim/poker_sim.py`). The simulator deals from the seed alone, so the same seed can be replayed with the seats rotated. The scripts run from any working directory.

```
python3 eval/dup.py 3000    # HU: 3000 seeds x 2 seatings = 6000 games, 11 s
python3 eval/dup4.py 400    # 4p: 400 seeds x 4 cyclic rotations = 1600 games, 12 s
```

## Observed output (identical to the research run)

| | games | result | SE independent | SE duplicate | variance ratio |
|---|---|---|---|---|---|
| `dup.py` HU seat swap | 6000 | A wins 0.564 | 0.0064 | 0.0050 | **0.61** |
| `dup4.py` 4p rotation | 1600 | bot0 pairwise finish-ahead 0.533 | 0.0099 | 0.0088 | **0.80** |

- The variance ratio is (SE duplicate / SE independent)², for the same number of games.
- HU pair outcomes: A won both seatings 667 times, split 2052 times and lost both 281 times.
- With these ratios, a 1 pp SE on a win rate takes about 1,500 HU games instead of 2,450, or about 1,250 4-player games instead of 1,570.

## Caveat: toy bots

These ratios come from deterministic toy bots, so they are a best case:

- `jammer(th)` goes all-in when a crude hand score is ≥ th, and otherwise checks or folds.
- `tag(raise, call)` min-bets strong hands, calls medium ones, and checks or folds the rest.

A bot that samples from a mixed policy breaks the correlation between duplicate games unless its RNG is seeded per duplicate (research critique). Measure the ratio again with the real bots before relying on it for SPRT game counts.

## Changes from the scratch versions

- `dup.py` imports `poker_sim` from `sim/`, not from `../poker_research` relative to the working directory.
- The `dup.py` experiment now runs only under `if __name__ == '__main__'`. Before, `dup4.py`'s `from dup import ...` also re-ran the HU experiment with `dup4.py`'s seed count. `dup4.py` output is unchanged.
- `dup.py` also prints the variance-ratio line.
