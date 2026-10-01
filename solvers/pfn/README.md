# solvers/pfn: ICM push/fold equilibrium for 3-4 players

The charts the bot uses when 3 or 4 players are alive, the pot is unopened or only jams have gone in,
and stacks are short (M1 of [`docs/plan.md`](../../docs/plan.md)).

| file | what |
|---|---|
| `tables.cpp` | The equity and card-removal tables (`data/cache/pfn/`, about 1 min): `w2`/`w3` exact counts of compatible combo pairs/triples per class pair/triple, `eq3` Monte Carlo 3-way showdown shares for every class triple (2,000 trials each), `eq4b` 4-way shares between 20 strength buckets, `bucket`. |
| `pfn.hpp` | The game and the fictitious-play solver. |
| `pfn.cpp` | CLI: `test` (heads-up check against `pf.py`), `solve`, `time`, `grid`. |
| `run_grids.sh` | The three grids the bot embeds (3 players of 3, 3 of 4, 4 of 4; about 1.5 h on 4 cores), written to `data/pfn/` (committed, so the header is reproducible without the solve). |
| `distil.py` | `data/pfn/grid_*.bin` to `bot/pfn_tables.hpp`: one class ranking per node, one threshold per grid point, CJK14-packed. |

`make pfn-tables` runs all of it.

## The game

One hand. Every non-BB player posts the small blind (0.5 BB) and the BB posts 1 BB, as in this
referee ([`docs/research/rules.md`](../../docs/research/rules.md)). Players act in order (first to act
is the player after the BB, the BB last) and either fold or move all-in; facing an all-in they call
(all-in) or fold. 3 players have 6 decision nodes, 4 players 14. Terminal values are Malmuth-Harville
ICM (`cpp/icm.hpp`) of the resulting stacks with the TrueSkill payouts (`solvers/trueskill_payouts.py`):
(1, .5, 0) for 3 of 3, (1, .6444, .3556) for 3 of 4, (1, .6444, .3556, 0) for 4 of 4. Side pots are
settled exactly for a given finishing order.

Approximations, all usual for push/fold solvers:

- Card removal is exact for the two other hands of a 3-way showdown given our own hand (`w3`) and
  pairwise with our hand otherwise (`w2`); a fold tells nothing about the folder's cards.
- In a 3-way all-in the main pot goes by the 3-way shares (`eq3`), the side pot by the pairwise
  equity of its two contenders; 4-way all-ins are evaluated between strength buckets (`eq4b`).
- The value of folding does not depend on the folded hand.
- Ties are folded into the win shares.

## The solver

Fictitious play: every node best-responds to the others' average strategy, and the average moves
by 1/t. The convergence gap `eps` is the reach-weighted gain a best response would make over the
average strategy, summed over all nodes, in prize-pool units (the pool is 1.5 for 3 players and 2
for 4). 300 iterations give eps about 3·10⁻⁴ (3 players); 200 give about 3·10⁻⁴ (4 players).

**Check**: with 2 players and payouts (1, 0) ICM is chip EV, so the solver must reproduce `pf.py`.
It does, to the same mixed hands: SB jam / BB call widths 71.3/62.0% at 5 BB, 58.4/37.6% at 10 BB,
40.3/21.7% at 20 BB (`pfn test`, part of `make pfn-tables`).

Speed: 0.015 s per iteration for 3 players, 0.06 s for 4 (the two-caller sums over 169² hand pairs
dominate), so a 4-player stack configuration takes about 12 s.

## Example (3 players, 10/10/10 BB, payouts 1/.5/0)

```
D first in (jam)         29.4%   weakest: K4s A4o J8s A7o T8s A3o
SB first in after f      82.8%   weakest: 95o Q2o J4o T5o J3o 53o
SB after j (call)         5.4%   TT+ AQ+ 99 AJs
BB after jf (call)        7.1%
BB after fj (call)       23.5%
BB after jj (call)        0.5%   AA
```

The SB's 83% first-in jam is the dead money: with the dealer's blind in the pot the SB risks 10 BB
to win 2, and the BB's ICM calling range is 23%. With 4 players at 10 BB the first player jams 57%
of hands and gets called 4-8% by each opponent: this is what the chart will do against the current
field, and it is why the bot's old heuristic open-or-fold play was leaving chips on the table
short-stacked at 3-4 players.

## The charts (`distil.py`)

Stack grids, every player, in BB: 3 players {2.5, 3.5, 5, 7, 10, 14, 20} (343 points), 4 players
{2.5, 4, 6, 9, 13, 20} (1,296 points). Per node the 169 classes are ranked once by their average jam
frequency over the grid; per grid point a threshold (jam with the first T classes) minimises the EV
lost against the solver's own hand values. The loss is reported next to the solver's eps; for the
3-player grid it is 1.1·10⁻⁴ of the pool per hand against an eps of 3.2·10⁻⁴, so the threshold form
costs nothing measurable. The bot (`bot/bot.hpp`, `pfn_threshold`) interpolates the threshold
multilinearly in log-stack between grid points, so a 12 BB stack gets a chart between the 10 and
14 BB ones, and clamps stacks to the grid (anything above 20 BB is treated as 20 BB).
