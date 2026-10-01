# Build plan: an equilibrium-first CodinGame Poker bot

Status, 2026-10-01: nothing in this plan is built yet. It replaces the first plan
([`archive/plan_v1.md`](../archive/plan_v1.md)), which added a per-bot exploitation layer. Unresolved
questions are in [`open_questions.md`](open_questions.md).

Provenance labels on numbers:

- **reproduced**: re-run in this repository by the script named next to it;
- **research**: taken from the 2026-09-30 research run;
- **estimate**: a guess, to be replaced by a measurement.

---

## 0. Stance

- **Goal: play as close to an equilibrium as can be computed in 50 ms and stored in 100k
  characters, and find out how far that gets on the leaderboard.**
- **No opponent modelling.** The bot does not profile, fingerprint or adapt to individual opponents.
  When it needs a belief about an opponent's hand, it assumes the opponent plays our own strategy.
  This is the Ataraxos assumption ("self-play-consistent" beliefs). A small ε floor keeps an
  unexpected action from zeroing out a range.
- **What "equilibrium" means at each table size:**
  - **Heads-up (HU)** is two-player zero-sum: tournament equity is linear in chips, U ≈ stack/4800.
    So Nash is well defined, it cannot lose in expectation (within its abstraction), and its
    distance from equilibrium (exploitability) is measurable. This phase decides 1st vs 2nd in nearly
    every game (§1, fact 4), so it gets the most effort.
  - **3-4 players** have no unique or "safe" equilibrium: one player can shift utility between the
    others (Szafron et al., AAMAS 2013), and Pluribus's methods carry no guarantee outside two-player
    zero-sum. The target is the standard sit-and-go one: ICM Nash for jam/fold, and
    self-play-consistent strategies elsewhere. These are judged by results, not guarantees.
- **Why this can work without modelling anyone.** Equilibrium play profits from every opponent's
  mistakes without having to identify them. The field's mistakes are large: the top bots are rules,
  charts and equity-versus-pot-odds, and none runs a solver
  ([`research/field.md`](research/field.md)). The accepted trade-off is that it will not take the
  maximum from any one opponent.

---

## 1. Facts that shape the plan

| # | fact | number | source |
|---|---|---|---|
| 1 | Every player who is not the BB posts the SB. Standard charts do not apply. | Preflop pot 1.5 / 2 / 2.5 BB for 2 / 3 / 4 players; completing costs 0.5 BB. | `Board.java:204-218`; `sim/poker_sim.py`, 381/381 replays reproduced |
| 2 | Scoring is on placement. | TrueSkill payouts at equal ratings: (1,0), (1,.5,0), (1,.6444,.3556,0). Calling off an all-in needs 0.600 equity with 3 equal stacks, 0.646 with 4, and 0.737 for big vs big (600/1800/1800/600). | reproduced, `solvers/trueskill_payouts.py`, `solvers/icm2.py` |
| 3 | Eliminations happen at short stacks. | The busted player started the hand with ≤10 BB in 44 / 54 / 53% of busts and ≤20 BB in 70 / 79 / 69% (2p / 3p / 4p starts, 120 games). | reproduced, `analysis/elim_depth.py` |
| 4 | Almost every game ends heads-up, mostly at 8-30 BB. | 130/131 3p and 147/154 4p games reach HU. Their HU decisions have a median effective stack of 18 / 14 BB, and 45 / 49% of them are at 8-30 BB. | reproduced, `analysis/hu_phase.py` |
| 5 | Decision mix (371 games, 72,341 decisions). | HU preflop: ≤8 BB 4.4%, 8-30 BB 7.3%, >30 BB 7.3%. HU postflop 14.9%. 3-4p preflop: ≤15 BB 5.3%, >15 BB 36.4%. 3-4p postflop 24.4%. | reproduced, `analysis/regimes.py` |
| 6 | HU jam/fold Nash. | 10 BB: SB jams 58.3%, BB calls 37.5%. The SB's value inside the jam/fold game is +0.056 / +0.017 / −0.045 / −0.127 / −0.183 BB/hand at 5 / 7 / 10 / 15 / 20 BB, a lower bound on its full-game value. Restricting to jam/fold costs at most 1.4% win probability at ≤6.7 BB (Miltersen-Sørensen). | reproduced, `solvers/pf.py`; [`research/literature.md`](research/literature.md) |
| 7 | Games are short. | Median 40 / 47 / 46 hands (2p / 3p / 4p starts, 371 games); 0 of 381 reached the 600-decision cap. | reproduced, `analysis/replay_stats.py analysis371` |
| 8 | Runtime primitives are cheap. | `pe7c.hpp`: 2,658 minified chars, 80-83 ms start-up, 214-246 M random 7-card evals/s under CodinGame flags. All 1,326 combos against a weighted range: river 14 µs, turn 0.72-0.76 ms, flop 18 ms. | reproduced, `cpp/` ([`cpp/README.md`](../cpp/README.md)) |
| 9 | Code space is tight. | Crossfish (UTTT) needed 48,760 chars of minified code for a simpler game. CJK14 packs 14 bits per character. | crossfish `documentation/minification.md` |

Placement is decided mostly in the short-stack and HU rows (facts 3-4), even though they are a
minority of decisions (fact 5).

---

## 2. Strategy by regime

| regime | strategy | how it is computed | milestone |
|---|---|---|---|
| HU, effective ≤ ~8 BB | Jam/fold Nash: a jam threshold and a call threshold per hand class | `solvers/pf.py` offline; embedded (~0.2k chars) | M1 |
| HU, 8-30 BB | Equilibrium of an abstracted HU short-stack game, preflop and postflop | offline CFR+/MCCFR; preflop as tables, postflop distilled into a small net; river re-solved at runtime | M2 |
| HU, >30 BB | The same solver on a deeper stack grid, or the M4 self-play net | offline | M2 / M4 |
| 3-4p, effective ≤ T(N, position) (start at 10-15 BB) | ICM jam/fold Nash | offline grid solve, distilled to 2-4k chars | M1 |
| 3-4p, deeper preflop | Open, complete, defend and 3-bet charts for this blind structure | offline CFR+ on a preflop tree with postflop leaf values. Heuristic: no convergence guarantee at 3-4 players. | M3 |
| 3-4p postflop | Balanced rules against self-consistent ranges: bluff a fraction s/(1+2s) of a bet of s × pot, defend by minimum defence frequency, river exact. | rules | M3; replaced by the M4 net if it wins |

Everywhere:

- **Utility.** ICM with the TrueSkill payouts; heads-up, chips.
- **Beliefs.** For each opponent, a 1,326-combo vector updated by Bayes' rule with *our own*
  strategy's likelihood of each observed action, plus an ε floor. No per-opponent statistics are
  kept.
- **Mixing.** Actions are sampled from the strategy. Equilibrium strategies mix, and a bot that
  always takes the argmax is exploitable.

---

## 3. Milestones

Every milestone is a bot that can be submitted. Each has two kinds of gate:

1. **Equilibrium quality**: exploitability, measured as described in §5.
2. **Results**: a local SPRT against the previous version (H1 = +2 pp pairwise finish-ahead rate,
   α = β = 0.05), non-inferiority against the generic opponents, and then a live submission to see
   where it lands.

Durations are estimates.

### M0: engine, harness and baseline (≈1-1.5 weeks)

1. **C++ engine.** Port `sim/poker_sim.py` with every quirk ([`sim/README.md`](../sim/README.md)).
   - Gate 1: a differential fuzz of 1e6 random-action hands against `poker_sim.py`, covering side
     pots, the odd chip, action replacement, the raise cap, NONE rounds, the 600-cap refund and the
     bust-order tie rule.
   - Gate 2: 381/381 replays, the check that `sim/validate_replays.py` and
     `replayer/replay_game.py --check` run.
2. **In-bot state tracker.** Replay each hand through the engine from start-of-hand stacks. Stdin is
   one snapshot at our turn: several opponent actions can arrive between our turns, and ALL-IN lines
   carry no amount (`InputSender.java:70-73`). Settle every hand from its showdown line, including
   hands where we made no decision.
   - Every turn, assert that the engine's (stack, chipInPot) and minimum raise match stdin and the
     offered `BET_x`.
   - Gate: zero mismatches over the 74,347 decisions that `sim/reconstruct.py` rebuilds from the 381
     replays (rendered to stdin by `poker_sim.py`'s `obs_to_stdin`).
3. **I/O skeleton, submitted early to measure the platform** (Q-A1 to Q-A4):
   - first turn: `__cplusplus`, pe7c build time and a CPU benchmark, to stderr;
   - every turn: latency from the first line read to the flush;
   - pondering test: does a background thread make progress between our turns, and at what latency
     cost?
   - Submitted builds print no `;message`.
4. **Evaluator and equity table.**
   - `cpp/pe7c.hpp`, with `cpp/test_eval.cpp` in CI.
   - Embed `solvers/eq169.bin` (8.1k chars) after cross-checking it against a pe7c-based build
     (`cpp/pushfold.cpp`).
   - Use one 169-class indexing everywhere; `eq.c` and `pushfold.cpp` order the classes differently.
5. **Local arena.**
   - The C++ engine with in-process bots, duplicate seeds with seat rotation, and an
     observation-hash bot RNG so duplicates stay correlated.
   - Table mix 41 / 34 / 25% (4p / 3p / HU), from the battle lists.
   - SPRT with crossfish `tools/sprt_merge.py` logic.
   - **Opponents:** our previous versions, plus generic scripted bots for sanity and regression
     checks: random-legal, always-call, always-jam, fixed-percentage jam/fold, and MC equity against
     pot odds. None is modelled on a leaderboard bot, and none is a tuning target.
6. **Baseline bot.** MC equity against a uniform range versus pot odds with the correct dead-money
   pots; HU jam/fold thresholds at ≤8 BB; ICM call-off thresholds.
7. **Size probe.** Minify with crossfish `tools/cg_minify.py` and record characters per module (§7).

**Gates:** 0 timeouts and 0 replaced actions in 1k fidelity-mode games; p99.9 turn time under the
deadline on the gcc 11.2 docker gate; the live submission promotes past the lower-league boss.

### M1: short-stack equilibrium (≈1.5 weeks)

1. **ICM utility in C++.** Port `solvers/icm2.py`, including bust-order ties. Unit tests: 0.600,
   0.646, 0.737, 0.557, 0.519, 0.592.
2. **HU jam/fold Nash, embedded.** Per-hand jam and call thresholds generated by `solvers/pf.py`
   (169 × 2 × 8 bits ≈ 0.2k chars), with no runtime solving.
   - Unit tests against `pf.py`: 10 BB 58.3 / 37.5%, 5 BB 71.4 / 62.1%, 20 BB 40.2 / 21.7%.
   - HU has no extra dead money (pot 1.5 BB), so `pf.py`'s chip-EV solution applies as is.
3. **3-4p ICM jam/fold Nash, solved offline on a grid.** Solving at runtime would be about 1000×
   over budget (research critique).
   - Grid: N ∈ {3, 4}; seat roles relative to us; stacks in BB (fine below 25 BB); blind level.
     Model this blind structure: every non-BB posts the SB, and a short poster is all-in.
   - Multiway leaves need the joint finish distribution: Monte Carlo, or tabulated class-triple
     outcome probabilities (Q-D2).
   - Convergence is not guaranteed with 3+ players, so measure it: each player's best-response gain
     must be below 0.1% of the prize pool (the Ganzfried-Sandholm criterion).
   - Distil into 2-4k chars: threshold functions of stacks and position, or a 3-5k-parameter MLP.
     Gate: ICM-EV loss ≤ 0.1% of the pool per decision on held-out grid points (estimate, Q-D3).
4. **Regime boundary T(N, position).** Start at 10-15 BB and tune by SPRT (Q-D4).

**Gates:** HU jam/fold exploitability 0 within the jam/fold game (exact best response); the 3-4p
grid meets the best-response criterion; SPRT against M0; submit.

### M2: heads-up 8-30 BB equilibrium (≈2-3 weeks): the core milestone

Nearly every game ends heads-up at a median of 14-18 BB, and that phase decides 1st vs 2nd (§1,
fact 4). Jam/fold's value there is only a lower bound (fact 6).

1. **Abstract game.**
   - Effective stacks: a grid such as {8, 10, 12, 15, 20, 25, 30} BB, interpolated in between.
   - Preflop: SB {fold, limp, 2x, jam}; BB {check/call, raise, jam, fold}; about 3 raises at most.
   - Postflop: {check, ~⅓ pot, ~¾ pot, all-in}, at most 2 raises per street.
   - Cards: 169 preflop classes; postflop buckets from equity distributions (EHS / EHS² histograms,
     k-means, 8-16 buckets per street), the standard imperfect-recall abstraction.
   - Fix the tree size and bucket counts in week 1, after a size check (§7).
2. **Solver.** MCCFR (external sampling) or CFR+, offline in C++, with the M0 engine for terminal
   values. Convergence is measured by exploitability in the abstract game, where an exact best
   response is affordable.
3. **Shipping it.** Full postflop tables do not fit. Even a modest abstraction has thousands of
   postflop decision nodes × ~10 buckets per stack point, so tens of thousands of entries per stack
   point (estimate). Ship instead:
   - **preflop strategy tables** per stack point (2-4k chars, estimate);
   - **a small postflop policy net distilled from the solution** by supervised training, as crossfish
     distilled its NNUE. The same net format is reused by M4.
   - Opponents' real bet sizes are mapped onto the abstraction by pseudo-harmonic action translation
     (Ganzfried-Sandholm).
4. **Runtime river re-solve.** On the river, solve the remaining subgame with CFR over exact 1,326-combo
   ranges, built from our own strategy's likelihoods, including the off-tree size actually faced.
   - Cost estimate: about 0.1 ms per iteration, so 100-200 iterations fit in budget. To be measured.
   - Keep it only if it lowers measured exploitability (local best response, §5). If unsafe
     re-solving proves exploitable, switch to safe re-solving (Brown & Sandholm, NeurIPS 2017).
5. **Answer Q-D1:** V(unrestricted) − V(jam/fold) at 10 / 15 / 20 BB within the abstraction.

**Gates:**

- exploitability within the abstraction falls with iterations, below a target set after the first
  run;
- local best response in the real game beats jam/fold-only play at every stack point where both
  apply;
- SPRT on HU tables (games that start HU, and 3-4p games once they reach HU) against M1, with
  non-inferiority on 3-4p tables.

### M3: 3-4-player deep play (≈2 weeks)

1. **Preflop charts** for N = 3, 4 by position × facing action × stack bucket (15-150 BB), for this
   blind structure.
   - Solve by CFR+ on a preflop tree. Flop leaves are valued at pot × equity × a realisation factor
     R(position, SPR, N), measured by simulating our own postflop play against itself; iterate once.
   - Heuristic: no convergence guarantee at 3-4 players (Q-D6).
2. **Multiway postflop.** The balanced rules of §2 against self-consistent ranges; river exact
   (range-vs-range, 14 µs); the per-hand ICM slope converts chips to utility.

**Gates:** SPRT against M2; submit.

### M4: the self-play network, the Ataraxos route (≈4-7 weeks plus compute; optional)

Purpose: replace the hand-built parts that are furthest from equilibrium, most likely deep 3-4p
postflop and deep HU, with a policy trained by the paper's dynamically damped self-play. In
two-player zero-sum it converges to a regularised equilibrium; with more players it is a reasonable
self-play-consistent target. See [`ataraxos.md`](ataraxos.md) §8.

1. **Network.** About 40-60k int8 parameters (§7). Inputs include evaluator features the net cannot
   cheaply learn: equity against the self-consistent range, pot odds, stacks in BB, position, N,
   blind level.
2. **Training.**
   - Whole 2-4p tournaments in the C++ engine; reward = placement payout; self-play only.
   - Loss: PPO-clip + 0.1·KL(π‖π_old) + α_t·KL(π‖ρ) + place cross-entropy, with power-law schedules
     for α_t and the learning rate.
   - From the Ataraxos supplement: one epoch per batch, an EMA of the weights (0.999) for evaluation,
     λ-returns (0.5 for the advantage, 0.8 for the value), gradient-norm clipping.
   - Poker-specific: a privileged critic that sees all cards; exact equity substituted for runouts
     once all-in; advantage filtering only after the critic's explained variance exceeds 0.3, and
     ablated (Q-H3).
   - Warm start from the M2 distilled net and the M1/M3 tables.
3. **HU pilot first.** Train on the jam/fold game and measure exact exploitability with `pf.py`'s
   best response (target ≤ 0.02 BB/hand). Then the full HU game, measured against the M2 solution
   (exploitability in M2's abstraction, and local best response).
4. **Test-time search, exactly as in the paper.**
   - Sample opponent hands from the self-consistent Bayes ranges.
   - Roll out each candidate action with the net playing every seat, to the end of the hand, with
     exact equity at all-in and river leaves.
   - q̂ = mean value; one closed-form MMD step toward the net's policy:
     π_s ∝ [exp(q̂)·ρ^α·π_θ^β]^(1/(α+β)).
   - This is non-exploitative by construction. Keep the KL-to-policy term (β): without it the paper's
     search fell below the raw network.

**Gates:** local best response better than the parts it replaces; SPRT against M3.

### M5: refinements

- A learned tournament value W (FGS-style; 2-5k parameters, initialised from ICM), trained on
  simulated self-play tournaments, replacing ICM where it wins (Q-D7).
- Ganzfried-Sandholm value iteration over future hands for the 3-4p jam/fold grid.
- SPSA tuning of T(N, position), bet sizes and abstraction granularity.

---

## 4. Per-turn algorithm

**First turn** (1000 ms limit; cap our own work at ~700 ms): build the pe7c tables, decode the CJK14
payload, run a 20-50 ms CPU benchmark to scale the budgets, then play a normal turn.

**Every turn.** Placeholder deadlines until M0 measures latency: stop compute at 33 ms, flush by
38 ms.

1. **Parse and replay** everything since our last turn through the engine, and assert against stdin.
   On a mismatch, resync to the snapshot and play the fallback for the rest of the hand.
2. **Update beliefs.** For each new opponent action, multiply that opponent's 1,326-combo range by
   our own strategy's probability of the action (ε floor).
3. **Look up the strategy** for the regime (§2): HU thresholds, the distilled jam/fold model, HU
   tables and the postflop net, charts, rules, or the M4 net.
4. **Optional refinement:** the river re-solve (M2+) or the one-step search (M4).
5. **Sample** the action with the observation-hash RNG.
6. **Emit.**
   - Check or call is printed `CALL`; it becomes CHECK or ALL-IN as needed.
   - A fold when checking is free is printed `CHECK`, because FOLD is never replaced.
   - Never print `CHECK` when facing a bet: it becomes FOLD.
   - `BET x` adds x chips (x ≥ the offered minimum); print `ALL-IN` when x ≥ stack.
7. **Guards.** If a deadline arrives, play the step-3 strategy without refinement. If that is
   unavailable, play the baseline rule.

---

## 5. Measuring how close to equilibrium we are

| tool | what it measures | milestone |
|---|---|---|
| Exact best response in the HU jam/fold game | exploitability of HU jam/fold play; 0 for the Nash thresholds (`solvers/pf.py`, `solvers/exploit.py`) | M1 |
| Best response on the 3-4p jam/fold grid | each seat's gain from deviating; Ganzfried-Sandholm criterion < 0.1% of the pool | M1 |
| Exact best response in the abstract HU game | exploitability within the abstraction | M2 |
| Local best response (LBR; Lisý & Bowling, 2017) | a lower bound on real-game exploitability for any strategy, including a net: a greedy one-step best response using rollouts or equity | M2+ |
| Head-to-head against previous versions | regressions | all |
| The live leaderboard | how far it gets us | every release |

The generic scripted opponents are sanity checks, not the objective.

**Practicalities** carried over from plan v1:

- **Fast mode** (fixed compute per decision) screens changes; **fidelity mode** (real binary, real
  time limits) is required for anything search-dependent and for release candidates.
- A search bot spends about 1.5 s or more of CPU per game. Run timed SPRTs on physical cores, one at
  a time (crossfish lesson).
- Duplicate seeds cut variance only to 0.61 (HU) and 0.80 (4p) of the plain value, and that was
  measured with deterministic toy bots, so it is a best case (`eval/dup.py`, `eval/dup4.py`).
- One live submission plays about 100 games: ±5 pp on the pairwise rate, about ±0.75 score points.
  Resubmit only after a local SPRT pass, never in reaction to one submission's rank.

---

## 6. Offline pipeline

| step | tool | output | milestone |
|---|---|---|---|
| Equity table | `solvers/eq.c` → `solvers/eq169.bin`, cross-checked with `cpp/pushfold.cpp`; CJK14 via crossfish `tools/nnue_cjk14.py` | 8.1k-char payload | M0 |
| HU jam/fold | `solvers/pf.py` | thresholds and unit tests | M1 |
| ICM | `solvers/icm2.py`, `solvers/trueskill_payouts.py` | C++ port and tests | M1 |
| 3-4p jam/fold grid | new grid solver and distiller | 2-4k-char model and a test grid | M1 |
| HU short-stack game | new abstraction + MCCFR/CFR+ solver; supervised distillation of the postflop policy | preflop tables; postflop net | M2 |
| 3-4p preflop charts | new CFR+ solver with realisation-factor leaves | charts | M3 |
| Self-play net | the C++ engine, vectorised; costs from `cpp/mlp.cpp`, `cpp/train_cost.py` | int8 net | M4 |
| Pack and gate | crossfish `tools/cg_minify.py`, `tools/nnue_cjk14.py`, `tools/cg_gate`, `tools/cg_perf_gate.py` | single-file submission | every release |

**Data.** The 381 committed replays are for engine and tracker validation and for structural
statistics. No further harvesting is needed.

**Non-goals.**

- No deck or seed prediction. The live RNG is SHA1PRNG with a seed the bots never see; the repo
  rebuilds decks only from the seed a public replay records.
- No vendoring of referee sources (`third_party/CodingamePoker` has no licence file).

---

## 7. Character budget (100k UTF-16 units)

| item | chars | basis |
|---|---|---|
| Code | 35-45k | estimate; crossfish needed 48,760 for a simpler game |
| 169×169 equity table, 8-bit | 8.1k | 14,196 × 8 bits / 14 |
| HU jam/fold thresholds | 0.2k | 169 × 2 × 8 bits |
| Distilled 3-4p jam/fold model | 2-4k | M1 target |
| HU preflop tables (8-30 BB) | 2-4k | estimate; depends on the M2 tree |
| 3-4p preflop charts | 6-8k | 4p alone ≈ 5.8k at 2 bits per entry |
| Safety margin | 3k | |
| **Total without a net** | **≈56-72k** | |
| **Left for the net** (M2 postflop distillation, later M4) | **≈28-44k** | ≈49-77k int8 parameters at 0.571 chars per parameter; plan on 40-60k until measured |

Measure, don't assume: minify the M0 skeleton and every milestone bot with crossfish `cg_minify.py`.
If space runs short, the net is shrunk first, then chart resolution.

---

## 8. Risks

| risk | mitigation |
|---|---|
| A timeout eliminates the bot; CodinGame's CPU, compile mode and latency are unknown. | The M0 skeleton measures them; first-turn benchmark scaling; deadlines from measured p99.9; the gcc 11.2 gate; 5k fidelity games per release. |
| Rule-fidelity bugs (the dead SB, BET = chips added, CHECK→FOLD, short all-in raise rights, ALL-IN without an amount). | Engine fuzz against `sim/poker_sim.py`; 381 exact replays; per-turn tracker assertions. |
| The utility is misspecified (equal-rating payouts; ICM ignores blinds and position). | Sensitivity check (Q-B1); learned W in M5. |
| No guarantee at 3-4 players. | Treat those strategies as heuristics judged by results; the HU phase carries the guarantee. |
| Abstraction error and off-tree bet sizes (opponents bet any amount). | Action translation, the river re-solve, and local best response to measure what is left. |
| The strategy does not fit in 100k chars. | Distil into a net; measure in M2 week 1. |
| The 3-4p grid solve is slow or does not converge. | Time a 3p slice in M1 week 1; coarsen the grid; tabulate multiway outcomes. |
| Self-play RL is unstable or costly. | The HU pilot gate first; M4 is optional. |
| Equilibrium leaves money on the table against weak bots. | Accepted by design (§0). The archived profiling work ([`archive/`](../archive/README.md)) is there if this is revisited. |
| Leaderboard noise. | Decide on local SPRTs; fixed resubmission policy. |

---

## 9. Changes from plan v1

- **Dropped**: the per-bot library, fingerprinting and online opponent model, the bounded
  exploitation step, replay harvesting for library refreshes, clone-based evaluation and the
  real-opponent gate. The tools are in [`archive/profiling/`](../archive/profiling/README.md).
- **Kept**: the M0 engine, tracker and arena; short-stack equilibrium (M1); the HU 8-30 BB solve,
  now the core milestone; 3-4p charts; the Ataraxos training kit, now the main route to deep play and
  used non-exploitatively, as in the paper.
- **Still-relevant corrections from the research critique**:
  - jam/fold's negative value is a lower bound, not proof that unrestricted play is much better
    (Q-D1);
  - multiway call-off equity is Monte Carlo, not exact;
  - 3-4p jam/fold cannot be solved at runtime;
  - ALL-IN amounts must come from replaying the hand, not stack deltas;
  - a search bot's arena throughput is ~1.5 s+ of CPU per game;
  - realistic code size is 35-45k chars;
  - Ataraxos-style PPO uses one epoch per batch (about 3.5P FLOP per sample, not 8P).
- **New**: exploitability measurement, including local best response, is a first-class gate, and
  the HU postflop strategy is distilled into a net because full tables do not fit.
