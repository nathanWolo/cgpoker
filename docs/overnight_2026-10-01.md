# Overnight report, 2026-10-01 (for the morning review)

What changed while you slept, in order; each step was validated in the arena and committed. The
numbers are paired-game payout differences (same seeds and seats, both bots swapped in), ± one
standard error; "pass" is the SPRT at H1 = +0.01 payout per game.

## 1. M1 live result and what it fixed
- M0.1b finished its placement run at rank 5 of 194 (score 29.15). Its leak was short-stack play at
  3-4 players: it limped 78% of unopened pots at ≤ 12 BB and called half the raises it faced;
  15 of 84 games ended blinded down below 2 BB (`analysis/shortstack.py`).
- M1 (committed before you slept): ICM push/fold charts for 3-4 players, +0.021 ± 0.003 per game
  against M0.1b at all sizes.
- **M1 went live** (you submitted it before sleeping) and finished its 87 placement games at
  **rank 2 of 194, score 30.66** (M0.1b: rank 5, 29.15). Mean payout 0.603 against 0.570, pooled
  finish-ahead 0.607 against 0.554. The short-stack leak is gone: at ≤ 10 BB effective it now wins
  0.33 BB per hand (was −0.08), at 10-20 BB 0.17 (was −0.06), 3 blind-downs instead of 15, and the
  unopened short-stack pots are 53% fold / 47% jam instead of 78% limp. The chips now go the other
  way deep: −0.30 BB per hand at 20-50 BB and −0.38 above 50 BB (was +0.25 / +0.89), partly because
  rank 2 means harder opponents, partly noise (87 games), and partly the heuristic deep play. That
  is what M2 and M2.1 below address heads-up; 3-4-player deep play is still heuristic.

## 2. M2: the solved heads-up game (8-30 BB)
- `solvers/hu/`: an abstract no-limit heads-up game (fold / limp / raise / all-in preflop, check /
  half-pot / all-in postflop, 169 preflop classes, 10 equity buckets per street) solved by
  external-sampling MCCFR in minutes; exploitability measured by best response, about 0.01-0.02
  BB/hand at 8-20 BB. The bot maps the hand onto the tree, buckets its hand at runtime, and
  *samples* the mixed strategy (`bot/hu_play.hpp`).
- Against M1: +0.020 ± 0.005 per 2-player game (pass), +0.005 ± 0.002 at all sizes.

## 3. M2.1: the same game to 120 BB
- Solving 40-120 BB too and playing the tables up to 150 BB effective: against M1 in 2-player
  games +0.045 ± 0.006 (pass), at all sizes +0.008 ± 0.003 (pass). The shipped tables are
  9 stack points at 400M iterations.
- Tried ranking by expected hand strength (draws count) instead of made-hand rank: +0.000 ± 0.003 against M2 at all sizes, so the simpler ranking stays.
- Final confirmation against M2: **+0.025 ± 0.006 payout per 2-player game** (8,000 paired games, SPRT pass) and +0.005 ± 0.002 over all table sizes (9,470 games; the gain is in heads-up play, which 3-4-player games reach late and short).
- **Submit `submissions/m2_1_min.cpp`** (91,412 characters characters of the 100k cap; it compiles with
  CodinGame's flags and plays whole games in the referee port identically to the readable build).

## 4. Tried and not shipped
- M2.2, a second bet size with the tables merged on pot and stack (`archive/m2_2/`): the merge is
  free (−0.000 ± 0.008 against M2.1) and shrinks the submission by 6k characters, but the pot-sized
  bet it paid for lost −0.011 ± 0.006 per 2-player game at the same 400M iterations (the tree is
  2.5× larger and converges less). The shipped bot stays M2.1; the retry is more iterations and a
  proper exploitability comparison.
- M3.1, 3-4 player preflop at 20-100 BB by CFR+ with equity-realisation leaves (`solvers/pfd/`):
  works, but the leaf model decides the answer and makes it limp half its hands at 20-30 BB; the
  live games showed no leak at those depths, so it stays an experiment.

## 5. Where the plan stands
- M0, M1, M2 done; M2.1 done; M3 preflop charts built but not shipped; M4 (self-play net) not
  started; M5 refinements open.
- Next steps I would take: (a) check the M2.1 live result with `analysis/postmortem.py` and
  `analysis/shortstack.py`; (b) a richer heads-up abstraction (a second bet size, non-all-in
  raises) once the table budget allows it, by merging postflop nodes on pot and stack-to-pot ratio;
  (c) measure equity realisation by simulating our own postflop play, which is what the 3-4 player
  preflop solve needs to become trustworthy.

## How to reproduce
`make hu-tables` (20 min), `make pfn-tables` (1.5 h), `make bot-test`, `make bot`,
`build/arena/arena --mode pair --dup --opp prev --sprt 0.01 ...` with `arena/freeze.py` to freeze a
reference. Everything is on `main`.
