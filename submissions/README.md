# submissions/

Built CodinGame submission files, kept so they can be fetched from GitHub without a build
environment. Regenerate with `make bot` (`tools/bundle.py --minify`).

| file | what | built from |
|---|---|---|
| `m0_probe_min.cpp` | M0 baseline bot, minified (paste into CodinGame). `DEBUG = true`: prints the platform probes and per-turn latency to stderr; `PONDER = false`. | commit 5790491 |
| `m0_probe_bundled.cpp` | the same bot, readable (local includes inlined, not minified) | commit 5790491 |
| `m0_1_min.cpp` | M0.1: ICM for big calls with 3-4 alive, jam/fold heads-up to 12 BB, range beliefs, 15 ms compute budget; `DEBUG = true`, `PONDER = false`. Arena: +0.026 payout/game over M0. | commit 7411e61 |
| `m0_1_bundled.cpp` | the same, readable | commit 7411e61 |
| `m0_1b_min.cpp` | M0.1b: same policy as M0.1; the pe7c tables are built in a background thread and a table-free evaluator plays until they are ready (CodinGame's start-up phase took 2.3 s). First-turn probe prints `first_input_at_ms` and `tables_ready`. | this commit |
| `m0_1b_bundled.cpp` | the same, readable | commit 5ec3b63 |
| `m1_min.cpp` | M1: ICM push/fold charts for 3-4 players (solved offline, `solvers/pfn/`) used when facing a jam, first in, over limpers and against a raise up to 20 BB; the big-call ICM pays a bust at its place. `DEBUG = true`, `PONDER = false`. Arena, paired against M0.1b: **+0.021 ± 0.003 payout per game** over 9,469 paired games at all table sizes against copies of M0.1b (SPRT pass; 2-player games are untouched, the gain is in 3- and 4-player games), and −0.006 ± 0.001 against the maniac/jammer/station/random field, where equilibrium jams and tight ICM calls give a little away to any-two callers and shovers. Kept as is, per the equilibrium-first stance. | this commit |
| `m1_bundled.cpp` | the same, readable (100.0k chars, 13 over the cap: submit the minified one) | commit 40d4864 |
| `m2_min.cpp` | M2: the solved heads-up 8-30 BB game (`solvers/hu/`, MCCFR; preflop and postflop, mixed strategy) played heads-up at 8-40 BB effective, on top of M1. `DEBUG = true`, `PONDER = false`. Arena, paired against M1: **+0.020 ± 0.005 payout per game in 2-player games** (8,000 paired games, SPRT pass) and +0.005 ± 0.002 over all table sizes (9,551 games: 3-4-player games reach heads-up late and short, where M1 already played jam/fold); against M0.1b over all sizes +0.035 ± 0.004. | this commit |
| `m2_bundled.cpp` | the same, readable (136k chars, over the cap: submit the minified one) | commit 2ba5741 |
| `m2_1_min.cpp` | M2.1: the heads-up tables extended to 120 BB (9 stack points, 400M iterations) and played up to 150 BB effective. `DEBUG = true`, `PONDER = false`. Arena, paired against M2: **+0.025 ± 0.006 payout per 2-player game** (8,000 paired games, SPRT pass) and +0.005 ± 0.002 over all table sizes (9,470 games; the gain is in heads-up play, which 3-4-player games reach late and short). | this commit |
| `m2_1_bundled.cpp` | the same, readable (over the cap: submit the minified one) | commit b9b662f |
| `m2_3_min.cpp` | M2.3: M2.1 with the raise/bet size translation against the effective stack (M2.1 called 50 BB raises off as 2.5x opens: live rank 13) and a nonlinear probability code. `DEBUG = true`, `PONDER = false`. The solved tables are used preflop only and only up to 20 BB effective: with them everywhere the bot lost 0.08 payout per 2-player game to M1 against the arena's exploitative opponents (−0.19 against passive callers); this setting is neutral against M1 on every opponent set (−0.003 ± 0.002 at all table sizes), so it is M1 plus a safe slice of M2. **Recommended for the next live run: `m1_min.cpp` again**, the strongest measured bot, which also measures how reproducible a placement run's rank is. | this commit |
| `m2_3_bundled.cpp` | the same, readable (over the cap: submit the minified one) | commit 65fca41 |
| `om1_min.cpp` | OM1: M2.3 plus real-time opponent modelling (`bot/opp_model.hpp`): ranges and frequencies measured per opponent from the revealed hole cards, feeding beliefs, bet-or-check by expected value, steals and heads-up jam/fold. `DEBUG = true`, `PONDER = false`. Arena, 2-player paired games: +0.060 ± 0.007 against copies of M2.3 and +0.058 ± 0.007 against copies of M1 (SPRT pass; it learns a fixed-rule bot's ranges and bluffs it where it folds), and within ±0.01 of M2.3 against every zoo opponent (station −0.001, limper −0.001, big raiser −0.003, maniac +0.009, jammer −0.003, random +0.006): M2.3's rules were already tuned against those, and the model does not give any of it back. With the model switched off the bot is M2.3 again (−0.001 ± 0.006 against copies). At all table sizes: +0.035 ± 0.005 against copies of M2.3 (SPRT pass), +0.000 ± 0.002 against the zoo, +0.001 ± 0.002 against the zoo with M1 as the reference. **Live: rank 2 of 194, 30.91** (level with #1; M1 30.66, M2.1 27.44). | commit 9187617 |
| `om1_bundled.cpp` | the same, readable (over the cap: submit the minified one) | commit 9187617 |

To run the pondering probe, set `PONDER = true` near the top of `bot/main.cpp` (or in the bundled
file: `const bool PONDER = true;`) and rebuild or paste the edited bundled file; it is under the cap
unminified too.
