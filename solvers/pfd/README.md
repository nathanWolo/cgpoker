# solvers/pfd: 3-4 player preflop at 20-100 BB (experiment, not shipped)

M3.1 of [`docs/plan.md`](../../docs/plan.md): a preflop-only game for 3 or 4 players with equal
stacks (fold / limp or call / raise to 2.5 BB or 3× / all-in, at most two raises, the BB's option),
chip EV, solved by vector CFR+ over 169-class reach vectors with pairwise card removal, reusing the
M1 solver's equity tables (`make pfn-tables` first). Flop leaves are valued by the plan's
equity-realisation model: pot × equity against the others' ranges × R, with R rising with the hand's
strength percentile (0.55 to 1.15), 1.05 in position / 0.93 out, × 0.92 per extra opponent.

```sh
g++ -std=gnu++20 -O3 -march=native -pthread -o build/pfd/pfd solvers/pfd/pfd.cpp
build/pfd/pfd solve 3 30 600        # 2 s: 310 nodes, 126 decision nodes
build/pfd/pfd solve 4 30 200        # 2,308 nodes, 1,008 decision nodes
```

**Status (2026-10-01, overnight).** It runs and converges in seconds, but the leaf model decides the
answer and is unvalidated: with a plain checkdown leaf the dealer limps 79% and raises 3% at 30 BB
(seeing a flop for 0.5 BB is free equity in that model); with the strength-dependent R the dealer
opens 41% / limps 18% / folds 40% at 60 BB, which looks like a real strategy, but at 20-30 BB it
still limps about half its hands. The plan's remedy, measuring R by simulating our own postflop play,
was not done, and the live games (M0.1b) showed the bot already winning 0.25-0.9 BB per hand at
these depths with 3-4 players, so this was left as an experiment rather than risked in the bot.
The M1 ICM push/fold charts cover 3-4 players up to 20 BB.
