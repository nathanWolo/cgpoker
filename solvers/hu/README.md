# solvers/hu: heads-up 8-120 BB by MCCFR

The strategy the bot plays heads-up at 8-150 BB effective (M2 of [`docs/plan.md`](../../docs/plan.md)):
preflop and all three postflop streets, from one solve of an abstract no-limit game.

| file | what |
|---|---|
| `hu.hpp` | The abstract game, the deal pool, external-sampling MCCFR, best response. |
| `hu.cpp` | CLI: `pool` (sample deals), `solve` (one stack, prints convergence), `grid` (all stacks, in parallel). |
| `export_hu.py` | `data/hu/grid.bin` to `bot/hu_tables.hpp` (4-bit probabilities, CJK14-packed). |

```sh
build/hu/hu pool 2000000 1 data/cache/hu/pool.bin          # 30 s
build/hu/hu grid 8,10,12,15,20,30,40,60,120 400000000 data/hu/grid9.bin data/cache/hu/pool.bin   # about 10 min on 4 cores (make hu-tables)
python3 solvers/hu/export_hu.py --grid data/hu/grid9.bin
```

## The game

Both players start with S BB; the SB (button) posts 0.5, the BB 1.

- Preflop, SB first: fold, limp, raise to 2.5, all-in. BB after a limp: check, raise to 3, all-in.
  Facing a raise: fold, call, 3-bet to 3× (only the BB over the open), all-in. Facing all-in: fold, call.
- Flop, turn, river, BB first: check, bet half the pot, all-in; facing a bet: fold, call, raise all-in
  (`--two-sizes` adds a pot-sized bet: the M2.2 experiment, see `archive/m2_2/`).
- A bet or raise that would put a player all-in is the all-in action. Showdown pays the matched chips.

That is 528 nodes and 3,810 information sets at 10 BB (more at deeper stacks where raises stop
collapsing into all-ins), perfect recall. Cards: the 169 classes preflop; postflop 10 buckets per
street by quantiles of EHS, the hand's expected showdown equity against a random hand (100 Monte Carlo
runouts; exact on the river). Chance is a pool of 2M sampled deals shared by every stack point, with
the buckets and the showdown result precomputed, so the solver's game is the pool-sampled one.

Known limits of the abstraction: one bet size postflop (half pot) besides all-in, no non-all-in raises
postflop, no 4-bets, EHS buckets do not separate draws from made hands of equal equity, and the
opponent's real sizes are mapped onto these (a raise of 60% of the stack or more counts as all-in).

## Solver

External-sampling MCCFR: each iteration samples one deal from the pool and walks the tree for one
player, trying every action at that player's nodes and sampling the other's; regrets are clipped at
zero and the average strategy is weighted linearly in the iteration count. 150M iterations per stack
point, about 100 s per stack point alone (3 min for the seven together); deeper stacks have bigger trees and converge more slowly, so they would profit from more.

**Exploitability** is measured by best response: for each player, the response to the other's average
strategy is fitted on 1M deals of the pool and valued on the other 1M. A response fitted and valued
on the same deals overstates the gain (it fits the sampling noise of rarely reached information
sets), one valued out of sample understates it, so both are reported; the truth is between.

| stack | SB value (BB/hand) | exploitability out-of-sample / in-sample (BB/hand) |
|---|---|---|
| 8 | +0.016 | 0.003 / 0.014 |
| 15 | +0.023 | 0.012 / 0.032 |
| 20 | +0.037 | 0.014 / 0.039 |
| 30 | +0.071 | 0.017 / 0.056 |
| 60 (400M iterations) | +0.117 | 0.006 / 0.068 |

For reference, the SB's value when restricted to jam or fold (`pf.py`) is −0.045 BB/hand at 10 BB,
−0.08 at 12 BB, −0.13 at 15 BB and −0.18 at 20 BB: the unrestricted game is worth 0.05-0.2 BB/hand
more to the SB (the plan's Q-D1).

Deeper stacks (`data/hu/grid_deep.bin`, 40-120 BB at 150M iterations) were tested against the
8-30 BB tables in the arena before the 9-stack solve: see the M2.1 note in `docs/plan.md`.

## Shipping it (`export_hu.py`)

Every decision node of every stack point, 4-bit probabilities per bucket and action, CJK14-packed:
32.8k characters for the nine stacks; the bot rebuilds the tree from the rules and checks a count and
a hash of the histories. A merged representation (postflop nodes keyed by street, pot, stack behind,
actions and street history, 20.7k instead of 34.8k postflop bytes, no measurable cost) is in
`archive/m2_2/` for when a richer abstraction needs the room.

## What the bot does with it (`bot/hu_play.hpp`)

Heads-up with 8-150 BB effective at the start of the hand, the hand so far is mapped onto the tree's
history (the tracker logs every action of the hand), our bucket is the preflop class or the EHS of
our hand on the board (300 runouts, exact on the river, about 50 µs), the node at the two nearest
stack points gives action probabilities that are interpolated in stack, and the action is sampled:
the strategy is mixed, as an equilibrium is. Raises become the tree's sizes (2.5 BB open, 3 BB over
a limp, 3× a raise, half pot postflop); all-in is all-in. A history the tree lacks (a 4-bet, a
timeout) is off-tree and the M0.1/M1 rules decide instead.
