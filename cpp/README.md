# cpp/: hand evaluator, micro-benchmarks and cost probes

This directory holds the C++ side of the research: `pe7c.hpp`, the 7-card evaluator we plan
to ship, its tests, and benchmarks that measure what fits in a CodinGame turn. Each program
builds under two flag sets. One is CodinGame's own command line, which is what matters for the
bot. The other is a native `-O3 -march=native` build for comparison.

## Files

| File | What it is |
| --- | --- |
| `pe7c.hpp` | **The evaluator to ship.** 5 to 7 cards, returns 1..7462 (higher is better). It has no payload, `init()` builds every table at start-up, and it uses no std containers and no macros. Minified, the evaluator is 2,658 characters. Its method follows [OMPEval](https://github.com/zekyll/OMPEval) (ISC); see `THIRD_PARTY.md`. |
| `pe7.hpp` | Readable reference version of the same method, using `std::vector`. It has two options for experiments: `-DPE_SH=n` sets the hash row width (default 12, OMPEval's), and `-DPE_DIRECT` uses a 67 MB direct table. It is not CodinGame-ready. |
| `test_eval.cpp` | **Correctness test** for `pe7c.hpp`; exit status 0 means pass. It enumerates every 7-, 6- and 5-card hand and checks the counts per category and the number of distinct classes. On 1M random hands it also checks that the 7-card value equals the best of the 21 five-card subsets. |
| `bench.cpp` | Evaluator benchmarks covering start-up, sequential enumeration, random hands, Monte Carlo preflop equity, and exact equity against a random hand on the river, turn and flop. `-DPE_REF` switches it to `pe7.hpp`. |
| `rvr.cpp` | Exact range-vs-range equity: the equity of all 1,326 hero combos against a weighted villain range, using a sweep that respects card removal. Timed on the river, turn and flop. |
| `mlp.cpp` | fp32 AVX2 MLP forward-pass cost, µs per evaluation at batch sizes 1, 16 and 64, for five network sizes. |
| `pushfold.cpp` | Builds the 169x169 preflop equity matrix by Monte Carlo (4 threads), then solves HU chip-EV jam/fold Nash by fictitious play at 5, 10, 15 and 20 BB. |
| `ex.cpp` | Exact HU preflop all-in equity over all 1,712,304 boards, with four sample matchups. |
| `prof.cpp` | Start-up profile of `pe7.hpp`: multiset enumeration and the statistics of the hash rows. |
| `cmp.cpp` | Cross-check and speed comparison against OMPEval and PHEvaluator. Optional: their sources are fetched by `fetch_third_party.sh` and never committed. |
| `cgsize.cpp` | Size probe for crossfish's `tools/cg_minify.py`, run by `make cgsize`. |
| `train_cost.py` | Cost of one numpy CPU training step for small MLPs (forward, backward and SGD). |
| `fetch_third_party.sh` | Clones OMPEval and a sparse checkout of PokerHandEvaluator into `third_party/` (gitignored), pinned to the commits that were measured. |
| `THIRD_PARTY.md` | Attribution and the ISC notice for OMPEval. |

## Build and run

```bash
make test          # exhaustive correctness test, CodinGame flags then native (about 1 s each)
make run           # build everything and run every benchmark under both flag sets (about 30 s)
make run-cg        # one flag set only (also: make run-native)
./fetch_third_party.sh && make cmp     # optional: pe7c vs OMPEval vs PHEvaluator
make cgsize CROSSFISH=/path/to/crossfish   # optional: minified size (default ../../crossfish)
make train-cost    # numpy training-step cost
make clean         # removes build/
```

The two flag sets:

- **CodinGame** (`build/cg/`): `g++ -std=gnu++17 -Werror=return-type -g -pthread`, with no `-O`,
  linking `-lm -lpthread -ldl -lcrypt`. This is CodinGame's own command line; see crossfish
  `documentation/minification.md` section 13. Speed comes only from the source's
  `#pragma GCC optimize("O3")`, placed before the includes, and its
  `#pragma GCC target("avx2,bmi,bmi2,popcnt,lzcnt")`, placed after them. Sources where these
  pragmas are conditional get them through `-DCGMODE`. At a global `-O0`, GCC inlines only
  `always_inline` functions, so `pe7c.hpp` marks its hot helpers that way.
- **native** (`build/native/`): `g++ -std=gnu++17 -O3 -march=native -pthread`.

`make cmp` builds natively only, so OMPEval gets its normal optimized build. `prof` is built
natively only because it has no optimize pragma.

## Correctness

`make test` passes under both flag sets:

| Hands | Category counts, high card to straight flush | Distinct classes |
| --- | --- | --- |
| all 133,784,560 7-card | 23,294,460 / 58,627,800 / 31,433,400 / 6,461,620 / 6,180,020 / 4,047,644 / 3,473,184 / 224,848 / 41,584 (all match) | 4,824 (match) |
| all 20,358,520 6-card | 6,612,900 / 9,730,740 / 2,532,816 / 732,160 / 361,620 / 205,792 / 165,984 / 14,664 / 1,844 (all match) | 6,075 (match) |
| all 2,598,960 5-card | 1,302,540 / 1,098,240 / 123,552 / 54,912 / 10,200 / 5,108 / 3,744 / 624 / 40 (all match) | 7,462 (match) |
| 1M random 7-card | 0 hands where `ev` differs from the best of the 21 five-card subsets; 0 where `add(board, hole)` differs from the card-by-card build | |

The test fails as it should when the evaluator is broken. With the flush lookup shifted by
one class, it reports mismatched category and class counts for 7, 6 and 5 cards and exits
with status 1.

`make cmp` found 0 ordering disagreements with OMPEval and with PHEvaluator over 524,288
random pairs of 7-card hands.

## Measured numbers

All measurements were taken on 2026-09-30 and 2026-10-01 on a 4-vCPU Intel Xeon VM at 2.1 GHz
(it has AVX-512, so `-march=native` may use it; the CodinGame pragmas target AVX2), with g++
13.3.0. CodinGame uses g++ 11.2, and its CPU is unknown. Every figure is single-threaded except
the pushfold matrix, which uses 4 threads. Unless a row says otherwise, a range is the min-max
of 3 back-to-back runs of each program per flag set, with nothing else running. Other runs
during development landed up to about 20% outside these ranges, because the VM is shared, so
treat differences under about 20% as noise. The **Research doc** column gives the number
recorded in `docs/research/raw/workflow_results.json` (research[3] key_facts 6-12 and 16).

### Evaluator (`bench`, `bench_ref`, `bench_direct`, `cmp`, `ex`)

| Measurement | CodinGame flags | native -O3 | Research doc |
| --- | --- | --- | --- |
| `pe7c` start-up (`init()`) | 80-83 ms (one outlier at 112 ms) | 62-67 ms | 62-85 ms |
| `pe7c` lookup tables | LK 71,855 of 262,144 u16 used, OFF 131,049 u32, FL 8,192 u16, about 1.04 MB. Start-up scratch brings the static total to 5.8 MB | same | same |
| `pe7c` sequential enumeration of all 7-card hands (one add and one lookup each) | 1.21-1.27 G evals/s | 1.06-1.08 G/s | 1.0-1.25 G/s |
| `pe7c` random 7-card hands (7 adds plus an eval) | 214-246 M evals/s | 236-241 M/s | 236-284 M/s |
| MC HU preflop equity, AKs vs a random hand (2 evals plus card sampling per trial) | 31.3-31.5 M trials/s, eq 0.6711 | 29.4-32.2 M/s | 31-32 M/s, 0.671 |
| River: strength of all 1,081 live combos on one board | 1.12-1.23 µs | 0.90-1.08 µs | n/a |
| Exact equity of one hand vs a uniform random hand, turn (about 48 x 990 evals) | 0.064-0.066 ms | 0.068-0.077 ms | 0.063-0.12 ms |
| Same, flop (about 1.07M evals) | 3.07-3.24 ms | 2.40-2.77 ms | 2.4-3.6 ms |
| Exact HU preflop all-in, one hand vs one hand (1,712,304 boards, `ex`) | 3.75-3.85 ms | 3.39-3.52 ms | n/a |
| `pe7.hpp` with OMPEval's 2^12-wide rows: start-up / random evals | **2.68-2.74 s** / 238-285 M/s | 1.16-1.18 s / 266-280 M/s (`prof`: 1.08 s) | 1.1-1.6 s |
| `pe7.hpp -DPE_DIRECT` (67 MB direct table): start-up / random evals | 69-75 ms / 102-119 M/s | 39-40 ms / 115-121 M/s | 77 ms / 123 M/s |
| `cmp` random evals/s (4 runs): pe7c / OMPEval / PHEvaluator | not built | 173-259 / 303-371 / 39-49 M/s | 242 / 356 / 49 M/s |
| Minified size (`make cgsize`) | 2,658 chars for the evaluator; 2,831 with the test `main`; identical to the research output | n/a | 2,658 chars |

### Range vs range (`rvr`): all 1,326 hero combos vs a weighted villain range, exact

| Street | CodinGame flags | native -O3 | Research doc |
| --- | --- | --- | --- |
| River (1 board) | 14.3-14.4 µs | 14.4-15.6 µs | 16.2 µs |
| Turn (48 rivers) | 0.72-0.76 ms | 0.70-0.79 ms | 0.88 ms |
| Flop (1,176 turn and river pairs) | 17.9-18.2 ms | 18.2-18.9 ms | 20.6 ms |

The sanity line matches. AA's equity against a random hand on the 2c 4d 7h flop is 0.8637 from
`rvr` and 0.8637 from `bench`'s exact enumeration.

### MLP forward pass (`mlp`), fp32 AVX2 FMA, µs per evaluation

| Net (params) | CG B=1 | CG B=16 | CG B=64 | native B=1 | native B=64 | Research doc, CG (B=1 to B=64) |
| --- | --- | --- | --- | --- | --- | --- |
| 64-64-64-8 (8,840) | 0.52-0.57 | 0.42-0.45 | 0.43 | 0.60-0.63 | 0.46-0.47 | 0.54 to 0.43 |
| 128-128-128-8 (34,056) | 1.83-2.17 | 1.32-1.47 | 1.28-1.45 | 1.89-1.92 | 1.51-1.54 | 1.87 to 1.30 |
| 128-256-256-8 (100,872) | 5.27-5.54 | 3.81-4.03 | 3.93-4.14 | 5.44-5.69 | 4.32-4.33 | 5.40 to 3.86 |
| 256-256-256-8 (133,640) | 7.11-10.29 | 5.04-5.34 | 5.07-5.39 | 6.90-7.19 | 5.61-5.67 | 7.05 to 5.29 |
| 256-512-256-8 (264,968) | 14.4-16.0 | 10.1-10.4 | 10.1-10.4 | 14.0-15.1 | 10.9-11.6 | 14.3 to 10.7 |

### Push/fold (`pushfold 6000`) and training cost (`train_cost.py`)

| Measurement | CodinGame flags | native -O3 | Research doc |
| --- | --- | --- | --- |
| 169x169 MC equity matrix, 6,000 trials per pair (SE 0.65%), 4 threads | 1.31-1.36 s (63-65 M trials/s) | 1.23-1.30 s (65-69 M/s) | 20k trials per pair: 4.02 s (71 M trials/s) |
| Same matrix at 20,000 trials per pair (`pushfold 20000`, 1 run, 2026-10-01) | 4.50 s (63 M trials/s) | not run | 4.02 s |
| Matrix spot checks: AA vs KK / AKs vs QQ / 72o vs AA | 0.813 / 0.463 / 0.117 (exact: 0.819 / 0.460 / 0.123) | same | n/a |
| Fictitious play, 2,000 iterations, one stack depth (12 timings) | 86-96 ms (43-48 µs per iteration) | 85-112 ms | 86 ms |
| HU Nash SB jam % / BB call % at 5, 10, 15, 20 BB, 6,000 trials (same every run) | 71.4/61.6, 57.8/37.2, 45.8/27.2, 40.2/20.8 | same | 20k trials: 71.4/61.8 (5 BB), 58.0/37.3 (10 BB), 39.8/20.9 (20 BB) |
| Same at 20,000 trials (`pushfold 20000`) | 71.4/61.8, 58.0/37.3, 45.6/27.4, 39.8/20.9: **identical to the research doc** | not run | as left |
| numpy training step, batch 4096: 34k / 101k / 265k params (1 run) | 88k / 27k / 29k samples/s (numpy 2.4.6, OpenBLAS) | n/a | 91k / 27k / 30k samples/s |

### Where measurements differ from the research doc

- **`pe7.hpp` with 2^12-wide rows starts in 2.7 s under CodinGame flags.** The doc's 1.1-1.6 s
  matches the native build (1.08-1.18 s). Under CodinGame flags, `pe7.hpp`'s `std::vector`
  and `std::sort` calls are not inlined at `-O0`, so start-up takes more than twice as long.
  This supports the doc's decision to ship 2^8-wide rows built without std containers
  (`pe7c.hpp`, about 80 ms). The wider rows do give somewhat faster random lookups
  (238-285 M/s against 214-246 M/s), probably because their OFF table is 32 KB instead of
  512 KB.
- The direct table's random-access speed is 102-121 M/s, against the doc's 123 M/s. Its
  start-up is 69-75 ms under CodinGame flags (doc: 77 ms) and 39-40 ms natively.
- `pe7c` random evaluation under CodinGame flags (214-246 M/s) sits slightly below the doc's
  236-284 M/s; earlier runs in this session gave 242-252 M/s, so this is within VM noise.
  `cmp` varies even more between runs (pe7c 173-259 M/s). The ranking (OMPEval > pe7c >>
  PHEvaluator) and the 0 disagreements are stable.
- `pushfold` reproduces the research doc's push/fold percentages exactly when run with the
  doc's 20,000 trials per pair (research[3] key fact 12: 10 BB 58.0/37.3, 5 BB 71.4/61.8,
  20 BB 39.8/20.9); `make run` uses 6,000 trials, which moves them by up to 0.4 pp. Building the
  20k matrix took 4.50 s here against the doc's 4.02 s. These are not the plan's jam/fold
  numbers: `solvers/pf.py` (58.3/37.5 at 10 BB) weights classes by exact combo and card-removal
  counts and uses `eq.c`'s matrix, while `pushfold.cpp`'s class weights ignore card removal.
- CodinGame flags against native: the pragma build is as fast or faster for sequential
  enumeration, the turn enumeration, `rvr` and the batched MLP, and about the same for random
  lookups. Native is faster on `init()` (62-67 vs 80-83 ms), because at `-O0` its helpers stay
  real calls. It is also roughly 10-30% faster on the 1,081-combo river pass and on the flop
  enumeration.
- The old `pe7.hpp` comment said start-up took "~3 ms". It is about 1.1 s with 2^12 rows; the
  comment has been corrected.

## Budget implications (50 ms per turn, plan for about 40 ms; 1,000 ms first turn)

The figures below use the CodinGame-flags numbers on this VM. CodinGame's CPU may be slower or
faster, so the bot should time its evaluator and MLP on the first turn and scale its budgets
from that.

- **Start-up:** `pe7c` `init()` takes 80-83 ms, with 112 ms in one outlier run. That is about
  8-11% of the 1,000 ms first turn. OMPEval's 2^12-wide hash rebuilt at start-up (2.7 s) does
  not fit. The 67 MB direct table fits (69-75 ms), but its random access is half as fast.
- **Raw evaluation:** about 9-10M random 7-card evaluations, or about 50M incremental ones, in
  40 ms. Heads-up MC equity runs at about 1.25M trials per 40 ms. A 1% standard error needs
  about 2,500 trials (about 0.08 ms), so one equity estimate is cheap even multiway.
- **Exact equity of one hand against a range:** about 0.065 ms on the turn and about 3.1-3.2 ms
  on the flop. About 12 flop computations fit in 40 ms, so all-in and call-off decisions can be
  exact on every street. An exact HU preflop all-in between two known hands takes about 3.8 ms.
- **Range vs range for all 1,326 combos (exact):** about 14 µs on the river and about 0.75 ms
  on the turn, so these can be redone after every action. The flop takes about 18 ms, nearly
  half the budget for one opponent. Doing it for three opponents (about 54 ms) does not fit,
  so 4-handed flops need sampled runouts, or one shared computation reused across opponents.
- **MLP:** a 101k-parameter net costs 5.3-5.5 µs single and 3.8-4.1 µs batched. That is about
  7.2k-10.5k evaluations per 40 ms. Running it on all 1,326 combos (one Bayesian range update)
  costs about 5.1-5.5 ms batched for each opponent action. A 34k net (1.3-2.2 µs) allows
  18k-31k evaluations per 40 ms. A 265k net allows only 2.5k-4.0k.
- **Push/fold:** one fictitious-play iteration over 169x169 costs 43-48 µs. 200-300 iterations
  (9-14 ms) fit in a turn when a new solve is needed. The 169x169 equity matrix should be
  embedded (about 8.1k CJK14 chars, per the research plan) and not computed at start-up:
  6,000 trials per pair already takes 1.3 s on 4 threads.
- **Code size:** the evaluator is 2,658 of the 100,000 source characters.

## History

The research scratch had three evaluator headers. `pe7c_fixed.hpp` was the CodinGame-ready
version: it is `pe7c.hpp` with the `AI` and `PE_SH` macros removed. With those macros,
crossfish's minifier renames the macro names inconsistently, and the minified file fails to
compile (`'U' was not declared in this scope`). The macro-free version minifies to exactly the
research `cgsize_min.cpp`. It is now `pe7c.hpp`, with `#pragma once` on line 1, where the
minifier strips it, and an attribution header. `rvr.cpp` and `bench.cpp` define their own `AI`.
`bench_c.cpp` (`pe7c`) and `bench.cpp` (`pe7`) differed only in the header they used, so they
were merged into one `bench.cpp` with a `-DPE_REF` switch. The research's minified
`cgsize_min.cpp` is now a build output (`make cgsize`).
