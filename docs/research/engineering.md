# Engineering budgets: what fits in 100k characters and 50 ms

> Measured on CodinGame on 2026-10-01 (M0.1 probe): `__cplusplus=202002`; Monte Carlo 31M
> trials/s, about half of this sandbox; turns 2-8 ms; evaluator start-up 2.3 s before the first
> input (80 ms here), so the bot now builds its tables in a background thread. See
> [`../../bot/README.md`](../../bot/README.md).

These are the hard limits of a CodinGame Poker bot and what we have measured fits inside them: the
platform limits, the character budget, table sizes, the hand evaluator, runtime primitives,
per-turn budgets and offline training cost. Sources:

- `research[3]` in [`raw/workflow_results.json`](raw/workflow_results.json);
- re-measurements in [`cpp/README.md`](../../cpp/README.md);
- crossfish's `documentation/minification.md` (the owner's UTTT engine);
- the research critique, which revised the code budget.

**All timings are from a 4-vCPU Xeon VM at 2.1 GHz with g++ 13.3**, built with CodinGame's flags
unless noted. CodinGame itself uses g++ 11.2 on an unknown CPU. The bot must calibrate on its
first turn (§6).

The per-item character budget and the per-turn algorithm are in [`../plan.md`](../plan.md) §7 and
§4. This document holds the measurements behind them.

---

## 1. Platform limits

| limit | value | source |
|---|---|---|
| Source size | 100,000 characters, **counted in UTF-16 units** | crossfish `minification.md` §4.1 |
| Memory | 768 MB | CodinGame help centre (archived 2026-02-16) |
| C++ compiler | g++ 11.2 with `-std=gnu++17 -Werror=return-type -g -pthread`, **no `-O`**. The help page says "mode C++20"; the gnu++17 line is the CI-verified one. | crossfish `minification.md:713-734` |
| Other languages | Python 3.11.5 with NumPy 1.20.2; Java 21.0.4; Rust 1.90.0 | help centre |
| Time | 1000 ms for a bot's first decision, then 50 ms per decision. **A timeout eliminates the bot.** | [rules.md §1](rules.md#1-parameters) |
| Decisions | At most 139 by one bot in one game; median 60 per bot per game (120-game set, ad hoc) | `data/decisions.jsonl.gz` |

**Consequences for C++** (crossfish `minification.md` §13):

- `#pragma GCC optimize("O3")` goes *before* the includes.
- Hot helpers are marked `__attribute__((always_inline))`. At a global `-O0`, GCC inlines nothing
  else.
- No std containers in hot code.
- `#pragma GCC target("avx2,...")` goes *after* the includes.

Without these rules, crossfish's shipped bot searched about a fifth of the nodes its local tests
measured. AVX2 is known to work on CodinGame.

## 2. Character budget

**Encoding.** CJK14 packs **14 bits per counted character**, using the first 16,384 CJK Unified
Ideographs. Base64 gives 6.0 bits per character and ASCII85 6.4 (crossfish `minification.md:248-267`).
Every payload figure below assumes CJK14. With base64, each one would shrink by a factor of 2.3.

**The crossfish reference point** (`minification.md:143-144`, `545-551`):

| part | characters |
|---|---|
| Minified code (engine, NNUE runtime, book reader) | 48,760 |
| NNUE: 35,243 params = 54,159 bytes, about 12.3 bits per parameter | 30,948 |
| Opening book | 13,456 |
| Macro net | 1,758 |
| **Total** | **94,922** |

**Two budgets.**

| | research (`research[3]`) | critique (more realistic) |
|---|---|---|
| Minified code | ~20k (15-25k) | **35-45k** |
| Safety margin | 3k | 3k |
| Tables (equity, charts, opponent library) | ~8.3k | ~22-26k |
| **Left for a network** | **~69k chars ≈ 0.97 Mbit** | **~25-35k chars** |
| Network at int8 | ~120k params | **~40-60k params** |

The critique column is quoted as the critique stated it. Its rows do not add up exactly: 100k minus
35-45k code, a 3k margin and 22-26k of tables leaves 26-40k, not 25-35k. The plan's itemised budget
([`../plan.md`](../plan.md) §7: 18-24k of tables, no opponent library) leaves ≈28-44k, about 49-77k int8 parameters. Both
lead to the same working assumption of 40-60k parameters until a skeleton has been measured.

The critique's case for more code: the bot must contain a full NLHE engine (side pots, the
min-raise and replacement rules), the parser, pe7c (2.7k), MC and exact equity, ICM, the push/fold
machinery, the range tracker, library inference and fingerprinting, the MMD step, a postflop
blueprint, the CJK14 decoder, time guards and net inference. Crossfish needed 48,760 characters for
a *simpler* game. The 3-4-player charts are also bigger than the research assumed: 4-player alone is
about 10 × 4 × 6 × 169 × 2 bits ≈ 5.8k characters.

**Parameters by precision** for the research budget (about 69k characters after tables):

| precision | parameters |
|---|---|
| crossfish's Rice/GPTQ, 12.3 bits | ~78k |
| int8 | ~120k |
| ~6-bit Rice | ~160k |
| int4 | ~240k |

Useful conversions at int8, which costs 8/14 ≈ 0.571 characters per parameter:

- 60k params ≈ 34.3k characters;
- 80k ≈ 45.7k;
- the 128-256-256-8 MLP (100,872 params) ≈ 57.6k.

At 12-14 bits per parameter, the same 100k-parameter net would take about 88k characters and not
fit. **Plan on 40-60k int8 parameters until a minified M0-M3 skeleton has been measured**
([`../plan.md`](../plan.md) §7).

## 3. Table sizes

| table | size | verdict |
|---|---|---|
| 169×169 HU preflop equity (14,196 unique entries) at 8 bits | 8,112 chars | **Embed.** Building it at start-up is not viable: 6,000 trials per pair takes 1.31-1.36 s on 4 threads (`cpp/pushfold.cpp`); the research's 20,000 took 4.0 s. |
| HU Nash push/fold thresholds (169 × 2 × 8 bits) | ~194 chars | **Embed** (anchor and fallback) |
| 3-handed ICM push/fold charts on a 10-level stack grid | ~4k chars | Possible |
| 4-handed ICM push/fold charts | ~86k chars | **Does not fit.** Solve offline and distil to a compact model of ~2-4k chars (critique). |
| Flop hand-strength buckets (1.29M canonical hand/flop pairs) | — | **Does not fit.** Compute at runtime. |
| SKPokerEval `RankData.h` / the referee's Java port | 315,381 B / 409,994 B | Does not fit |
| OMPEval offset table | 102,599 B | Does not fit; rebuilding it at start-up takes 1.1-2.7 s (§4) |
| PHEvaluator tables | 310,788 + 27,199 B | Does not fit |
| Two-plus-two lookup | 130 MB | Does not fit |

## 4. Hand evaluator

**We ship `cpp/pe7c.hpp`.** It uses OMPEval's method, which is ISC-licensed; see
`cpp/THIRD_PARTY.md`:

- 13 additive rank keys;
- a row-displacement perfect hash with 256-wide rows, offsets computed at start-up;
- an 8,192-entry flush table.

It has **no payload**. Every table is built in `init()`. It minifies to **2,658 characters**.

| measurement | research | re-measured (CG flags) |
|---|---|---|
| `init()` start-up | 62-85 ms | 80-83 ms (one outlier at 112 ms) |
| Lookup tables | ~1.04 MB (LK 71,855 used u16, OFF 131,049 u32, FL 8,192 u16) | same |
| Random 7-card evals, including building the hand | 236-284 M/s | 214-246 M/s |
| Sequential enumeration | 1.0-1.25 G/s | 1.21-1.27 G/s |

**Correctness** (`make test` in `cpp/`):

- All 133,784,560 seven-card hands give exactly the right counts for all 9 categories, and 4,824
  distinct classes.
- The 6-card and 5-card enumerations also match.
- On 1M random hands, the 7-card value equals the best of the 21 five-card subsets.
- There are 0 ordering disagreements with OMPEval and with PHEvaluator over 524,288 random pairs.

**Alternatives,** measured on the same machine:

- **OMPEval** is faster (303-371 M/s) but needs its 102,599-byte offset table. Rebuilding its
  4096-wide rows at start-up takes 1.1-1.2 s natively and **2.7 s under CodinGame flags**, too slow
  for the first turn.
- A 67 MB direct table starts in 69-75 ms but runs at only 102-121 M/s random.
- PHEvaluator runs at 39-49 M/s.

## 5. Runtime primitives

Single thread, CodinGame flags. "Research" is `research[3]`; "re-measured" is `cpp/README.md`
(2026-09-30/10-01).

| primitive | research | re-measured |
|---|---|---|
| MC HU preflop equity | 31-32 M trials/s (AKs vs random 0.671) | 31.3-31.5 M trials/s |
| Exact equity, one hand vs a random hand, turn | 0.063-0.12 ms | 0.064-0.066 ms |
| Same, flop | 2.4-3.6 ms | 3.07-3.24 ms |
| Exact HU preflop all-in, hand vs hand (1,712,304 boards) | — | 3.75-3.85 ms |
| All 1,326 combos vs a weighted range, exact, river | 16.2 µs | 14.3-14.4 µs |
| Same, turn | 0.88 ms | 0.72-0.76 ms |
| Same, flop | 20.6 ms | 17.9-18.2 ms |
| HU fictitious play, one iteration over 169×169 | 86 ms per 2,000 iterations | 43-48 µs |

**fp32 AVX2 MLP forward pass** (`cpp/mlp.cpp`), in µs per evaluation:

| net (params) | batch 1 | batch 64 | evaluations per 40 ms |
|---|---|---|---|
| 64-64-64-8 (8,840) | 0.52-0.57 | 0.43 | ~70-93k |
| 128-128-128-8 (34,056) | 1.83-2.17 | 1.28-1.45 | ~18-31k |
| 128-256-256-8 (100,872) | 5.27-5.54 | 3.93-4.14 | ~7.2-10.5k |
| 256-256-256-8 (133,640) | 7.11-10.29 | 5.07-5.39 | ~3.9-7.9k |
| 256-512-256-8 (264,968) | 14.4-16.0 | 10.1-10.4 | ~2.5-4.0k |

The last column is our arithmetic from the timings.

Use **splitmix64 or xoshiro** for Monte Carlo. The `x ^= x << 7; x ^= x >> 9` xorshift variant was
biased: 72o vs AA came out 0.109 vs 0.119 with splitmix64, where exact spot checks give
0.110-0.126 (research).

## 6. What fits in a turn

**Usable time.**

- The research put the usable budget at 35-42 ms out of the 50 ms.
- The plan's placeholders are: stop computing at 33 ms, flush output by 38 ms. Both are to be
  reset from p99.9 latency measured on CodinGame (plan §5).
- Check the clock every ~16-64 rollouts. Keep a fallback move ready from the start of the turn.

**First turn (1000 ms):**

- evaluator `init()`: 80-83 ms;
- CJK14 decode and dequantisation: under 1 ms for 100k params (research);
- a 20-50 ms benchmark of evaluations/s and µs per MLP evaluation, to scale every budget below;
- in dev builds, print `__cplusplus` to stderr to settle the C++20 vs gnu++17 question.

**Per-turn budgets.**

| use | cost | verdict |
|---|---|---|
| Raw evaluation | ~9-10M random evals or ~50M incremental per 40 ms | Plenty |
| MC equity to 1% SE (~2,500 trials) | ~0.08 ms | Cheap, even multiway |
| Exact all-in or call-off equity vs a range | µs on the river; 0.065 ms (turn); 3.1-3.2 ms (flop) | Every street can be exact HU |
| Range vs range for all 1,326 combos after every action | 14 µs (river); 0.75 ms (turn); ~18 ms (flop) | The flop takes ~half the budget for one opponent. Three opponents (~54 ms) do not fit: sample runouts or share one computation. |
| Bayesian range update with a **net** likelihood (~1,225 live combos × 1 eval per opponent action) | ~3 ms (100k net) / ~1.2 ms (34k) with crossfish's private-card first-layer cache (research); 5.1-5.5 ms batched without it | Fine HU. Tight 4-handed, where up to ~6 opponent actions arrive per turn. |
| Same with **table** likelihoods | <0.1 ms | The plan's default before any net (plan §5) |
| One-step MMD search, policy rollouts to hand end with exact equity at all-in leaves | ~2,400 rollouts (100k net) / ~5,000 (34k net), i.e. ~400-1,000 per action over 5-6 actions (research) | **Probably optimistic by ~2×** (critique): rollouts also need per-street features for new cards and leaf values per outcome. Precision may still be too low to move a near-deterministic anchor ([literature.md §5](literature.md#5-mmd-and-update-equivalence)). |
| HU push/fold re-solve by fictitious play | 200-300 iterations in 9-14 ms | Fits; embedding the thresholds is simpler |
| 3-4p ICM jam/fold equilibrium at runtime | ~0.24 s per iteration; 50-200 iterations needed | **~1000× over budget** (critique). Solve offline. |

For comparison, Ataraxos used 200 rollouts per move in dou dizhu, 1,000 in Stratego and 10,000 in
Hanabi, with networks of 1.9-14.7M parameters ([`../ataraxos.md`](../ataraxos.md)). The rollout
counts above are similar, with a network about 20-150× smaller than those (~100k parameters).

## 7. Offline training cost

**Cost model** (research): about **8·P FLOP per sample**, for generation plus 4 PPO epochs on the 25%
of samples that survive advantage filtering. At P = 100k that is 0.8 PFLOP per 10^9 samples.
Ataraxos trains its move network for one epoch per batch (supp. Table S7), so this estimate is
conservative (critique).

**Measured throughput:**

| component | throughput |
|---|---|
| numpy CPU training step, batch 4096, 34k / 101k / 265k params | 91k / 27k / 30k samples/s (research); 88k / 27k / 29k (re-measured, numpy 2.4.6) |
| C++ self-play | ~4 µs per decision per core (research estimate) |

**Projected wall time:**

| setup | samples |
|---|---|
| This 4-vCPU box | ~11 h per 10^9 |
| An 8-16-core desktop | ~10^9 per day |
| One rented GPU with a vectorised C++ environment | ~10^10 per day plausible; the environment, not the maths, is the bottleneck |

**Samples needed: probably 10^9-10^10.** This is extrapolated from AlphaHoldem and Ataraxos, not
measured by a pilot:

| system | samples | hardware | model |
|---|---|---|---|
| AlphaHoldem | 6.5B (ablations: 0.65B) | 3 days on 8 GPUs + 64 CPU cores (~4,000 CPU-hours, 580 GPU-hours) | 98 MB |
| Ataraxos Hanabi | — | policy: 1 L40 × 2 days | 5.9M params |
| Ataraxos dou dizhu | — | policy: 4 H100 × 2 days | 1.9M params |
| Ataraxos Stratego | 160M games | RL: 16 H100 × 1 week; belief: 4 H100 × 4 days; under US$8,000 | 14.7M-param move network |

**Evaluation throughput** (critique). A search bot spending 25-33 ms per decision over 55-60
decisions costs at least 1.5 s of CPU per game for its own seat. The "~6 four-player games per
second per core" fast mode applies only to rule bots. Run any search-dependent change's evaluation
across multiple cores.

## 8. Engineering rules that follow

1. **C++, with crossfish's compile rules (§1).** Budget the source as 35-45k code plus a 3k
   margin, then tables, then any network. Measure a minified skeleton before committing to a net
   size.
2. **Ship `pe7c.hpp`.** Embed the 169×169 equity table (8.1k characters) and the HU thresholds
   (~0.2k). Do not embed 4-handed charts; distil them.
3. **Calibrate on the first turn** and scale every per-turn budget to the measured speed. Hard
   time guards are mandatory, because a timeout eliminates the bot.
4. **Prefer exact computation where it is cheap**: HU all-in equity, river and turn range sweeps.
   Use Monte Carlo for multiway pots and flops.
5. **A net is optional.** At 40-60k int8 parameters it is 3-5× smaller than the research assumed.
   A 34k net leaves headroom for 4-handed range updates.
6. **Submitted builds print no `;message`.** Debug tags leak strategy: the research worked out
   kovi's, JuMaKre's and MaxFerrer's methods from theirs (critique; [field.md §4](field.md#4-how-the-top-bots-play)).
7. **Seed the bot's RNG per duplicate** in local evaluation. Otherwise a mixed policy destroys the
   duplicate-seed correlation that `eval/` relies on (critique).

Open engineering questions are tracked in [`../open_questions.md`](../open_questions.md) §G:

- CodinGame CPU speed;
- the real C++ standard;
- the I/O and scheduling share of the 50 ms;
- whether the process can compute between turns;
- whether 8-bit quantisation is enough.
