# Build plan: CodinGame Poker bot

Status, 2026-10-01: nothing in this plan is built yet. The repository holds the research tools that the
plan cites. The plan starts from `plan.recommended_plan` in
[`research/raw/workflow_results.json`](research/raw/workflow_results.json): the "Ladder" spine, the
"Glasshouse" exploitation machinery and an optional Ataraxos training kit. Every fix from the
research critique (`critique.*` in the same file) is folded in, and the plan is re-ordered where the
critique showed that the original order was wrong. Unresolved questions are in
[`open_questions.md`](open_questions.md).

Provenance labels on numbers:

- **reproduced**: re-run in this repository by the script named next to it.
- **ad hoc**: computed from repository data with a one-off script that is not committed. None of
  the plan's numbers carry this label any more: the scripts behind them are now
  `analysis/pairwise.py`, `analysis/hu_phase.py` and `analysis/regimes.py`.
- **research**: taken from the research run, with the scratch script ported into this repo where one
  is named.
- **estimate**: a guess, to be replaced by a measurement.

---

## 0. Strategic stance: near-Nash anchor, bounded and optional exploitation

The owner prefers near-Nash play over heavy exploitation. The plan is built around that preference:

1. **Every milestone ships a complete bot with exploitation off (η = 0).** That bot is the
   *anchor* π₀. It uses an equilibrium wherever an equilibrium is both meaningful and computable:
   heads-up (HU) play, and short-stack jam/fold with ICM. Everywhere else it uses solver-derived or
   principled heuristics.
2. **Exploitation is one optional module (M4).** It can move away from π₀ only by a bounded amount.
   The bound depends on how confident the opponent model is and on how much we would lose if the
   model were wrong (§6). The module is first enabled only in jam, call-off and river spots, where
   q̂ needs no rollouts: it is exact HU and on the river, and a Monte Carlo estimate with a known SE
   in multiway jam/call-off spots (§3, §6 Rule A).
   It ships only if it passes the same gates as the anchor, plus a misidentification test and a
   real-opponent check.
3. **"Nash" means different things at different table sizes.**
   - In the HU phase, U ≈ stack/4800, so each hand is close to two-player zero-sum in chips. An
     equilibrium anchor there has a real worst-case guarantee, within its action abstraction.
   - With 3-4 players, no equilibrium is "safe": one player can transfer utility between the
     others (Szafron et al., AAMAS 2013, 3-player Kuhn), and the Pluribus methods carry no
     guarantee outside two-player zero-sum games. The 3-4-player anchor (ICM jam/fold equilibrium,
     heuristic charts) is self-play-consistent play, not a guarantee.
   - This matters because the HU phase decides 1st vs 2nd in almost every game (§2, fact 4). So the
     anchor is strongest exactly where much of the placement payoff is decided.

**Evidence for enabling bounded exploitation at all:**

| evidence | number | source |
|---|---|---|
| Opponents are fixed programs, with no memory across games (each game starts a fresh process). Their public debug messages and style statistics show rules, precomputed charts (MaxFerrer's `GTO.PF.RSHV` push/fold chart) and MC-equity-vs-pot-odds bots. Some adapt within a game (kovi's `anti-maniac` rule). | No top bot shows signs of a solver or learned policy. Waffle3z prints no messages, so it is classified by style only. | research; `analysis/stats.py`, `analysis/stats3.py` |
| Every hole card is revealed after every hand, folded hands included, so every opponent decision is an exactly labelled (hand, context, action) triple with no showdown-censoring bias. | `SHOW_FOLDED_CARDS=true` | referee `ShowDownInfo.java:50`; `sim/poker_sim.py` |
| The opponent model is much better than a population model. | Next-action log-loss: population 0.983 → library types + online correction 0.653 nats. Bots outside the library: 0.935 → 0.652. Top-1 identification among 17 bots after 5/10/20/40 decisions: 43/66/81/90%. | reproduced, `oppmodel/oppmodel2.py` |
| Exploitation gains are large next to equilibrium edges. | HU 10 BB jam/fold: the best response to a nit (calls top 15%) gains **+0.278 BB/hand** over Nash. Nash's own SB value in the jam/fold game is −0.045. | reproduced, `solvers/exploit.py`, `solvers/pf.py` |
| The leash is real and has a price. | One MMD step against the nit: η=3 gains +0.042 with a worst case of −0.067 (Nash −0.045); η=10 gains +0.252 with a worst case of −0.328. | reproduced, `solvers/mmdstep2.py` |
| The field has extreme styles. | Fold rate against opens ≤3 BB: kovi 0.78, Zylo 0.68, BrandV 0.09. SmogyY and UnTypedScript call 100% of the all-ins they face (n = 9 and 12). | reproduced, `analysis/stats3.py`, `analysis/stats.py` |

**Evidence that argues for caution (and for the near-Nash default):**

- Replay steal EVs are noise. A 4p open ≤2 BB nets +2.89 ± 1.64 BB (n=64). Bottom-40% hands opening
  ≤3 BB at 4p net −1.14 ± 0.87 (n=247). Reproduced, `analysis/steal_se.py`.
- The identification rates are in-sample: the library and the targets come from the same replay
  pool. Bots also resubmit: #1, #3 and #6 (and #10, #12) did between 25 and 30 Sept.
- Behaviour clones extrapolate off-distribution, and one live submission measures p only to about
  ±5 pp.
- A plain push/fold bot already holds its own. Tuo (#5) finishes ahead of Waffle3z (#1) 24/43
  times (56%) and of kovi 11/18 times. Reproduced, `analysis/pairwise.py` on `data/battles/`.

Conclusion: build and ship the anchor first. Allow deviations only in exact-q̂ spots, with a budget
of "risk only what you have won". Widen exploitation only where ablations prove it pays.

---

## 1. Goal and metric

- **Objective.** Maximise TrueSkill placement, not chips. The utility is U = Σ_p pay_p · P(place p).
  At equal ratings the payouts are (1,0), (1,.5,0) and (1,.6444,.3556,0) for 2, 3 and 4 players
  (reproduced, `solvers/trueskill_payouts.py`; `solvers/icm2.py` uses the rounded .645/.355, and
  its thresholds are unchanged at 3 decimals). HU, U ≈ stack/4800. At unequal ratings the 4p
  payouts shift, e.g. to (1,.623,.325,0) for μ=30 against 29/28/27 at σ=1 (Q-B1 in [`open_questions.md`](open_questions.md)).
- **Primary local metric.** The pairwise finish-ahead rate p against the opponent pool.
- **Secondary metrics.** Mean payout utility; p by table size; all-in-adjusted chip EV per regime
  (effective stack <15, 15-30, 30-60, >60 BB); timeout count.
- **Leaderboard mapping.** Over the top 8 by rank, score ≈ 21.13 + 15.30·p with r = 0.961
  (reproduced, `analysis/pairwise.py` on `data/battles/` and
  `data/snapshots/2026-09-30/cg_lb.json`; the research quoted
  21.12 + 15.31·p). This is a rough guide, not a target:
  - p is measured against each bot's matchmade opponents, so it falls as a bot climbs;
  - AGSigma (#10, score 27.67) has p = 0.598 (n = 189), which the fit would place at #2.
- **Reference point.** Waffle3z (#1, 30.65) has p = 0.611 (n = 429 pairwise comparisons, binomial
  SE 0.024; comparisons within one game are correlated, so the true SE is larger).

---

## 2. Key facts driving the design

| # | fact | number | source |
|---|---|---|---|
| 1 | Every player who is not the BB posts the SB. | Preflop pot 1.5 / 2 / 2.5 BB for 2/3/4 players. Not mentioned in the statement. | `Board.java:204-218`; `sim/poker_sim.py`; 381/381 replays reproduced by `sim/validate_replays.py` and `replayer/replay_game.py --check` |
| 2 | 4800 chips; BB = 10·2^⌊h/10⌋; 50 ms per turn (1000 ms first turn); a timeout eliminates; the cap is 600 decisions, counted globally. | Median 40/47/46 hands for 2/3/4p (371 games). 0/381 games hit the cap. | `analysis/stats.py`, `analysis/replay_stats.py`; [`sim/README.md`](../sim/README.md) |
| 3 | Eliminations happen at short stacks. | Start-of-hand stack of the eliminated player ≤10 BB: 44/54/53%; ≤20 BB: 70/79/69% (2p/3p/4p, 120 games). | reproduced, `analysis/elim_depth.py` |
| 4 | Almost every 3-4p game reaches heads-up, mostly at ≤30 BB. | 130/131 3p and 147/154 4p games reach HU. HU decisions are 26% / 19% of those games' decisions, at a median effective stack of 18 / 14 BB, with 69% / 81% at ≤30 BB and 45% / 49% at 8-30 BB. HU decides 1st vs 2nd, worth 0.5 (3p) or 0.356 (4p) payout. | reproduced, `analysis/hu_phase.py` on `data/cache/replayed.pkl` (the critique's figures reproduce exactly) |
| 5 | Most decisions are deep. | 64/52/51% of decisions are at >50 BB effective (2p/3p/4p, 120 games). | reproduced, `analysis/stack_depth.py empirical` |
| 6 | ICM makes calling off expensive at 3-4p. | Equity needed to call off an all-in: 3 equal stacks 0.600; 4 equal 0.646; 4p big vs big (600,1800,1800,600) 0.737. | reproduced, `solvers/icm2.py` |
| 7 | HU jam/fold Nash (chip EV). | 10 BB: SB jams 58.3%, BB calls 37.5%. SB value *inside the jam/fold game*: +0.056 / +0.017 / −0.045 / −0.127 / −0.183 BB/hand at 5/7/10/15/20 BB. This is a lower bound on the SB's full-game value (§11). | reproduced, `solvers/pf.py`, `solvers/eq169.bin` |
| 8 | Field defend rates make small steals opponent-dependent. | Break-even fold rate for a pure steal: 2 BB open 50/43/37.5%, 2.5 BB open 57/50/44% (HU/3p/4p). Opens ≤3 BB win uncontested 44.1/34.4/23.7% (n=1609/1812/832). | arithmetic; `analysis/steal_se.py` |
| 9 | 7-card evaluation is cheap and needs no payload. | `pe7c.hpp`: 2,658 minified chars; 80-83 ms start-up and 214-246 M random evals/s under CodinGame flags (research: 62-85 ms, 236-284 M/s). All 133,784,560 7-card hands give the right category counts, and ordering agrees with OMPEval and PHEvaluator. | reproduced, `cpp/test_eval.cpp`, `cpp/bench.cpp`, `cpp/cmp.cpp`, `make -C cpp cgsize` ([`cpp/README.md`](../cpp/README.md)) |
| 10 | Runtime equity primitives. | All 1,326 hands vs a weighted range: river 14 µs, turn 0.72-0.76 ms, flop 18 ms (research: 16 µs, 0.88 ms, 20.6 ms). HU MC 31 M trials/s. The 169×169 table takes 4-4.5 s on 4 threads to build at 20k trials per pair, so it must be embedded (8.1k chars). | reproduced, `cpp/rvr.cpp`, `cpp/bench.cpp`, `cpp/pushfold.cpp` ([`cpp/README.md`](../cpp/README.md)) |
| 11 | The code budget is tight. | Crossfish (UTTT) minified code was 48,760 chars for a simpler game. CJK14 packs 14 bits per character. | crossfish `documentation/minification.md:547, 259` |
| 12 | Duplicate seeds help less than hoped. | Variance ratio 0.61 (HU seat swap) and 0.80 (4p rotation), measured with deterministic toy bots, so a best case. | reproduced, `eval/dup.py`, `eval/dup4.py` |
| 13 | Decision mix by regime (371 games, 72,341 decisions). | HU preflop: ≤8 BB 4.4%, 8-30 BB 7.3%, >30 BB 7.3%. HU postflop 14.9%. 3-4p preflop: ≤15 BB 5.3%, >15 BB 36.4%. 3-4p postflop 24.4%. Effective stack = min(own, largest other) at hand start. | reproduced, `analysis/regimes.py` on `data/cache/replayed.pkl` |

Decision share is not the same as importance. Placement is decided mostly in the short-stack and
HU rows (facts 3-4), which are a minority of decisions.

---

## 3. Architecture by regime

| regime | anchor π₀ (η = 0 bot) | q̂ when exploitation is on | milestone |
|---|---|---|---|
| HU, effective ≤ ~8 BB | HU jam/fold Nash thresholds, embedded (~194 chars) | exact: 169×169 table with card removal, µs | M1 |
| HU, 8-30 BB | Solved HU preflop game {fold, limp, 2x, 2.5x, jam} with postflop leaf values (§4, M2) | exact for jam/call-off; one-ply modelled response otherwise | M2 |
| HU, >30 BB | Deep charts plus postflop blueprint | one-ply; river exact | M2 |
| 3-4p, effective ≤ T(N,pos) (start at 10-15 BB, tuned) | Distilled offline ICM jam/fold equilibrium | multiway MC with side pots and ICM leaves (Monte Carlo, not exact) | M1 |
| 3-4p deep preflop | Dead-money charts (heuristic; solved with realisation-factor leaves) | one-ply | M3 |
| Postflop, any N | Equity-vs-range blueprint: value and bluff ratios, small c-bets, fold discipline | river exact (14 µs), turn exact (0.75 ms), flop MC; rule-policy rollouts only if M4b proves them | M2-M3 |
| Round > ~560 (passive tables only) | Variance reduction if the current stacks rank us better than an early bust (the unfinished hand is refunded at 600) | — | M1 |

**In-bot modules.**

| module | role | basis in repo |
|---|---|---|
| Engine and state tracker | Exact rules; replays each hand from start-of-hand stacks; asserts against the stdin snapshot | `sim/poker_sim.py` (C++ port) |
| Evaluator | 7-card ranks, per-street 1,326-combo strength buckets | `cpp/pe7c.hpp` |
| Equity | 169×169 table (embedded), range-vs-range sweep, MC with splitmix64/xoshiro (not the biased xorshift variant in `cpp/cmp.cpp`) | `solvers/eq169.bin`, `cpp/rvr.cpp` |
| Utility | Malmuth-Harville ICM with TrueSkill payouts; per-hand slope s = ∂U/∂chips·BB; later the learned W | `solvers/icm2.py` |
| Anchor tables | HU thresholds; HU 8-30 BB strategy; distilled 3-4p jam/fold model; charts; postflop rules | `solvers/pf.py` + new solvers |
| Range tracker | 1,326-vector per opponent, ε floor; the anchor/population belief from M2; type×combo from M4 | — |
| Opponent model (M4) | Library posterior, Dirichlet residual, bet-size fingerprint, confidence | `oppmodel/oppmodel2.py` |
| Exploitation step (M4) | §6 | `solvers/mmdstep2.py`, `solvers/mmd_ab.py` |
| Emitter and guards | Safe output mapping, deadlines, fallback | — |

**Ataraxos pieces, and where they go:**

- **Used:** the one-step update-equivalence/MMD step, as the exploitation leash (M4); belief-sampled
  evaluation, in its exact 1,326-combo form.
- **Optional (M6):** MMD-in-PPO self-play, the place-distribution value head, advantage filtering,
  rollout search.
- **Not used:** the belief transformer, depth-40 rollouts, multi-million-parameter nets, the setup
  network.

---

## 4. Milestones and gates

Every milestone produces a submittable bot. Gates use the arena and statistics of §8. The default
milestone gate is an SPRT with H0 Δp = 0, H1 Δp = +2 pp, α = β = 0.05, plus pooled non-inferiority
against the previous version. Durations are estimates.

### M0: engine, I/O skeleton, arena, baseline, and the harvest (≈1-1.5 weeks)

1. **Harvest first, because battle history disappears.**
   - Check CodinGame's terms of service for automated replay download and automated IDE play (Q-C1).
   - If they allow it, start a throttled background harvest in week 1: `tools/fetch_replays.py top`,
     at least 2 s between requests, for the top ~50 agents, weekly and before every submission.
   - Keep a time-stamped archive (`data/snapshots/<date>/`, `data/battles/`, `data/replays/`) and
     never overwrite it. Tag every game with each player's `submissionId`; battle lists carry it,
     which allows version-aware library fits.
   - If the terms forbid it, stop, and continue with the 381 games already here.
2. **C++ engine.** Port `sim/poker_sim.py` with every quirk (see [`sim/README.md`](../sim/README.md)).
   - Gate 1: a differential fuzz of 1e6 random-action hands against `poker_sim.py`, covering pots,
     side pots, the odd chip, action replacement, the raise cap, NONE rounds, the 600 cap with
     refund, and the bust-order tie rule.
   - Gate 2: 381/381 replays, the same check as `sim/validate_replays.py` and
     `replayer/replay_game.py --check`.
3. **In-bot state tracker.**
   - Replay each hand through the engine from start-of-hand stacks. Settle every finished hand from
     its showdown line, including hands in which we made no decision (folded to our BB, forced
     all-in by a blind, NONE rounds).
   - ALL-IN amounts come from the engine. Stack deltas do not work, because stdin is one snapshot at
     our turn and several opponent actions across streets can arrive between our turns
     (`InputSender.java:70-73`).
   - Assert that the engine's (stack, chipInPot) matches stdin and that its minimum raise matches
     the offered `BET_x`, every turn.
   - Gate: zero mismatches on the stdin rendered by `sim/poker_sim.py` `obs_to_stdin` for every
     decision in the 381 games: 74,347 records from `sim/reconstruct.py`, of which the committed
     `data/decisions.jsonl.gz` holds 22,916.
4. **I/O skeleton, submitted to CodinGame as early as possible.** It measures the platform
   (Q-A1 to Q-A4):
   - First turn: print `__cplusplus`, the pe7c build time, MC trials/s and MLP µs/eval to stderr.
   - Every turn: log the time from the first line read to the flush, and the wall time between turns.
   - Pondering test: a background thread increments a counter. Log how much it advances between our
     turns, and compare the turn-latency distribution with the thread on and off.
   - Output mapping is always safe (§5, step 8).
   - Build-time flag: dev builds write debug output to stderr only. **Submitted builds print no
     `;message`.** The research inferred kovi's, JuMaKre's and MaxFerrer's methods from theirs.
5. **Evaluator and equity table.**
   - `cpp/pe7c.hpp`, with `cpp/test_eval.cpp` in CI.
   - Before embedding `solvers/eq169.bin`, cross-check it against a pe7c-based build
     (`cpp/pushfold.cpp`). `eq.c` uses its own evaluator and 20k MC trials per pair (SE ≈ 0.0035),
     and 8-bit quantisation adds steps of 0.0039.
   - Use one 169-class indexing everywhere: `eq.c` and `pushfold.cpp` order the classes differently.
6. **Local arena.**
   - The C++ engine running in-process bots.
   - Duplicate seeds with seat rotation, plus a paired design: candidate and baseline play the same
     seeds and seatings against the same opponents.
   - Bot RNG seeded from a hash of the observation (game seed, hand, round, cards). This keeps
     duplicates correlated, and makes live decisions reproducible offline for post-mortems.
   - Table mix 41/34/25% (4p/3p/HU).
   - Zoo v0: an exact port of gpoussel's `poker.ts` (gpoussel/copro @ `a43f874`, MIT; keep its
     copyright notice in the port), plus scripted archetypes: Tuo-style push/fold,
     kovi-style nit, Waffle3z-style LAG, BrandV-style jammer, maniac, calling station and the league
     boss.
   - SPRT with crossfish `tools/sprt_merge.py` logic.
   - Re-measure the duplicate variance ratio with real, stochastic bots (`eval/dup.py`,
     `eval/dup4.py` give 0.61/0.80 only for deterministic toy bots).
7. **Baseline bot.**
   - MC equity against a population range versus pot odds, with the correct dead-money pots.
   - HU jam/fold thresholds at ≤8 BB.
   - ICM call-off thresholds.
8. **Size probe.** Minify the skeleton with crossfish `tools/cg_minify.py` and record characters per
   module, to recalibrate §9.
9. **Re-run the field analyses after every harvest.** The scripts behind §2 facts 4 and 13 and the
   §1 score fit are committed (`analysis/hu_phase.py`, `analysis/regimes.py`,
   `analysis/pairwise.py`); add a rating-adjusted p (Q-B3).

**Gates:**

- 0 timeouts and 0 replaced actions in 1k fidelity-mode games.
- At least +5 pp pairwise against the gpoussel port (SPRT).
- The live submission promotes past the boss.
- p99.9 turn time below the deadline on the gcc 11.2 docker gate (crossfish `tools/cg_gate`).

### M1: short-stack core, η = 0 (≈1.5 weeks)

1. **ICM utility.**
   - Port `solvers/icm2.py` to C++, with bust-order ties.
   - Unit tests: 0.600, 0.646, 0.737, 0.557, 0.519, 0.592.
2. **HU jam/fold, embedded.**
   - Embed per-hand SB-jam and BB-call stack thresholds (169 × 2 × 8 bits ≈ 194 chars), generated by
     `solvers/pf.py`. No runtime fictitious play.
   - Unit tests against `pf.py`: 10 BB 58.3/37.5%; 5 BB 71.4/62.1%; 20 BB 40.2/21.7%.
   - Check that one threshold per hand is enough: equilibria can mix one hand per state and need no
     fixed hand ranking (Miltersen-Sørensen).
   - HU has no extra dead money (pot 1.5 BB), so `pf.py`'s chip-EV solution applies as is.
3. **3-4p ICM jam/fold, solved offline on a grid.** Runtime solving would be about 1000× over
   budget: about 0.24 s per best-response iteration with multiway MC leaves, so 12-48 s for 50-200
   iterations.
   - Grid: N ∈ {3,4}; seat roles (button/SB/BB/other relative to us); stacks in BB (fine below
     25 BB); blind level.
   - v1: single-hand equilibrium with ICM leaves, solved by fictitious play in the CodinGame
     structure (every non-BB posts the SB; short posters are all-in).
   - v2 (M5): Ganzfried-Sandholm value iteration over future hands.
   - Multiway leaves need the joint finish distribution, so tabulate class-triple outcome
     probabilities offline, or MC.
   - Convergence is not guaranteed with 3+ players. Measure it: every player's best-response gain
     must be <0.1% of the prize pool, the Ganzfried-Sandholm criterion.
4. **Distil.**
   - Fit a compact model: either per-node hand ranking plus a threshold as smooth functions of
     stacks and position, or a 3-5k-parameter MLP. Either is 2-4k chars.
   - Gate: mean ICM-EV loss against the exact offline solution on held-out grid points at most 0.1%
     of the prize pool per decision (an estimate, to be revisited with data).
5. **Runtime.** Compute EVs only for our own hand at the current node:
   - EV(jam), EV(fold), EV(call) against the anchor's ranges;
   - HU: exact from the table with card removal;
   - multiway: range-sampled MC with side pots and ICM leaves, 2-10 ms.

   This gives Q_anc, which feeds the soft-anchor option and, later, q̂.
6. **Regime boundary.** T(N,pos) starts at 10-15 BB for 3-4p and is tuned by SPRT.
7. **Exploitation stays off.** The bot records depth-bucketed fold-to-jam and call-off counts per
   seat, for M4.

**Gate:** SPRT +2 pp against M0; pooled non-inferiority; 0 timeouts.

### M2: heads-up 8-30 BB and the postflop blueprint, η = 0 (≈2 weeks)

This milestone moves ahead of the 3-4p deep charts because of §2 fact 4.

The steps are ordered to break the circular dependency the critique found, where preflop leaf
values needed a postflop blueprint built in the same milestone:

1. **Range tracker.**
   - 1,326 combos per opponent, with card removal and an ε floor.
   - The likelihoods come either from the anchor π₀ or from the population table. A/B them: the
     population table is calibration, not per-opponent exploitation (Q-E8).
2. **Postflop blueprint (equity vs range).**
   - Value-bet when equity against the calling range is above about 0.55.
   - Bluff with a fraction s/(1+2s) of the betting range for a bet of s × pot.
   - Small c-bets on dry boards.
   - Fold discipline from minimum defence frequency against the modelled range.
   - River exact, turn exact, flop MC (`cpp/rvr.cpp` costs).
3. **Realisation factors.** Simulate blueprint against blueprint from flop leaves to get
   R(position, SPR bucket, N): the share of all-in equity a hand actually realises.
4. **HU preflop game at 8-30 BB.**
   - SB actions {fold, limp, 2x, 2.5x, jam}. BB responses {check/call, raise, jam, fold}; fix the
     tree in M2 week 1.
   - Solve by CFR+.
   - Leaf values: all-in uses table equity with card removal; a flop leaf uses pot · R · equity.
   - Re-measure R under the new preflop ranges and re-solve once.
5. **Answer Q-D1.** Measure V(unrestricted) − V(jam/fold) for the SB at 10/15/20 BB. `pf.py` only
   gives the jam/fold value, which is a lower bound.

**Gates:**

- Within the abstraction, our HU strategy's value against a best-responding opponent must be at
  least the jam/fold Nash value at every stack point. An exact best response is affordable in the
  preflop tree.
- SPRT +2 pp on HU tables (HU-start plus 3-4p games entering HU) against M1.
- Non-inferiority on 3-4p tables.

### M3: 3-4p deep preflop and multiway postflop, η = 0 (≈1.5 weeks)

1. **Dead-money charts.**
   - Open, complete, defend and 3-bet ranges for N = 3, 4, by position × facing state × stack bucket
     (30-150 BB).
   - Solve with CFR+ or fictitious play on an abstract preflop game with realisation-factor leaves
     from the M2 machinery, extended to multiway pots.
   - These charts are heuristic, because CFR+ has no convergence guarantee at 3-4 players.
   - Sanity checks: the break-even fold rates in §2 fact 8.
   - Budget about 6-8k chars (4p alone is ≈5.8k at 2 bits per entry).
2. **Multiway postflop.** Extend the blueprint, using the per-hand ICM slope to convert chip EV to
   utility.

**Gate:** SPRT against M2 (+2 pp, or small-effect mode, §8). No regime may have negative
all-in-adjusted chip EV against the nit or station archetypes.

### M4: opponent model and bounded exploitation, optional (≈2 weeks)

**M4a: jam, call-off and river spots only.** q̂ is exact there HU and on the river, and a
range-sampled Monte Carlo estimate (with its SE in Rule A) in multiway jam/call-off spots. This
captures the largest measured gain and needs no rollouts.

M4a needs only the M1 machinery plus the counts M1 records. The critique suggested shipping this
exploit in M1, and it can be pulled forward to straight after M1 if exploitation is wanted sooner.
It is placed after M3 by default because of the near-Nash preference.

1. **Library.**
   - The top 20-25 bots.
   - The `oppmodel/oppmodel2.py` contexts (112) plus stack-depth buckets ≤6, 6-12, 12-20 and
     >20 BB for jam and call contexts. Without them, call-off ranges at 5 BB vs 12 BB cannot be
     told apart.
   - 5 action classes, Dirichlet-shrunk toward the population (n₀ = 4), packed at 4 bits, ≤8k chars.
   - **Split by time:** fit the library on games before a cutoff date. Evaluation clones and
     offline scores use games after it.
2. **Online model.**
   - Type posterior: log w_j(k) += log(0.97·σ_k + 0.006).
   - Dirichlet residual with n₁ = 2.
   - Bet-size fingerprint (names are not in the input).
   - Confidence c_j = max_k P(k|D_j) · n_j/(n_j + 15).
   - An "unknown" type with prior 0.2.
3. **The step** (§6), with unit tests:
   - a deterministic anchor stays deterministic when Δq = 0;
   - with ρ uniform and η in prize units, the critique's 27%-off-anchor example is reproduced (a
     regression test that documents why ρ = π₀);
   - in the demo configuration (BB units, ρ uniform, α = 0.02, ε = 0.02) the step reproduces
     `solvers/mmdstep2.py`: η=3 → +0.042 / −0.067, η=10 → +0.252 / −0.328.
4. **Offline gates.** Time-split log-loss and identification rate. The in-sample 0.653 nats and
   81% at 20 decisions are upper bounds; set the targets once the time-split numbers exist.

**Arena gates**, all against M3:

- SPRT on time-split clones;
- SPRT on scripted archetypes that do not come from the library;
- SPRT on shape-shifter bots that play one type for 20-40 decisions and then switch, which measures
  the cost of misidentification;
- pooled non-inferiority on the archetypes and shape-shifters;
- a real-opponent check that is non-negative (§8; only if Q-C1 allows it).

**M4b: wider spots.** Non-all-in preflop, flop and turn:

- one-ply q̂ from the modelled fold/call/raise mix;
- optionally CRN rule-policy rollouts in two regimes (opponents on the model, or on the anchor), at
  1-2 µs per rollout.

Keep M4b only if an η ablation passes SPRT. The switching rule needs Δq̂ > z·SE, so rollout
estimates rarely trigger a deviation unless their SE is small (Q-E6).

### M5: refinements and the go/no-go (≈1.5-2 weeks)

1. **Learned tournament value W (FGS).**
   - 2-5k parameters, initialised from ICM.
   - Train it on **simulated zoo tournaments**, not arena outcomes: in arena data the chip leader is
     disproportionately the stronger bot, so chips and skill are confounded. Alternatively add
     player-strength or type features.
   - It replaces ICM where it wins an SPRT.
2. **Corrections to M1's distilled model** from Ganzfried-Sandholm value iteration.
3. **SPSA/CLOP tuning**, in small-effect SPRT mode, of T(N,pos), η_max per regime, c_min, z, κ, ε₀,
   n₀/n₁, bet sizes and R factors.
4. **Go/no-go for M6.** Attribute the remaining deficit (arena, and our own reconstructed live
   battles) by regime. Proceed only if more than 40% of it lies in deep or mid 3-4p postflop play.

### M6: optional Ataraxos training kit (≈4-7 weeks plus compute)

1. **MMD-PPO anchor network.**
   - 40-60k int8 parameters, to be confirmed against the measured budget in §9.
   - Warm start: behaviour cloning of the M5 bot.
   - Episodes: whole 2-4p tournaments, with the payout as reward.
   - Loss: PPO-clip + 0.1·KL(π‖π_old) + α_t·KL(π‖ρ) + place cross-entropy, with power-law α_t and
     learning-rate schedules.
   - Ataraxos details the earlier plan left out (Ataraxos supplement): one epoch per batch; an EMA of
     the parameters (0.999) for evaluation and shipping; λ-returns with λ = 0.5 for the advantage
     and 0.8 for the value; gradient-norm clip 0.267.
   - Advantages in BB-equivalents (divided by the local slope s); a privileged critic that sees all
     cards; exact equity substituted for runouts once players are all-in; bootstrap with W at hand
     boundaries.
   - Advantage filtering (top 25% of |A|, at least 0.05 BB) only once the critic's explained variance
     exceeds 0.3, and ablated.
   - With one epoch, the cost is about 3.5P FLOP per sample (2P generation + 0.25·6P update), not
     the 8P the research costed with 4 epochs. Re-time with `cpp/train_cost.py`.
2. **HU pilot gate**, corrected. "Within 0.02 BB/hand of Nash jam/fold" is ill-posed for a
   full-action net. Use one of:
   - train the pilot on the jam/fold game and measure exact exploitability with `pf.py`'s best
     response (≤0.02 BB/hand); or
   - for the full-action net, require value ≥0 against Nash jam/fold over paired SB/BB hands, plus a
     local-best-response check.
3. **Net-based update-equivalence rollout search** (ρ = π_θ, §6).

**Gate:** SPRT against M5; pooled non-inferiority; 0 timeouts in 5k fidelity games at
CodinGame-calibrated speed.

---

## 5. Per-turn algorithm and time budget

The clock starts when the first line of the turn is read. Placeholder deadlines: **stop compute at
33 ms, flush by 38 ms**. Reset both from the p99.9 latency the M0 skeleton measures on CodinGame.

**T0, first turn only** (1000 ms limit; cap our own work at ~700 ms):

- build the pe7c tables (80-83 ms in the sandbox under CodinGame flags);
- decode the CJK14 payload (<5 ms);
- run a 20-50 ms benchmark and scale the MC and rollout counts to CodinGame's CPU;
- read N, id and firstBBId; initialise every seat to the population prior;
- in dev builds, print `__cplusplus` and the timings to stderr;
- then run a normal turn.

**Every turn:**

1. **Parse and replay (≤0.3 ms).**
   - The engine applies everything since our last turn: settle the previous hand from its showdown
     line (side pots, odd chip, busts and their order), post blinds for each new hand, apply each
     action line (post-replacement; an ALL-IN amount is the engine's remaining stack), and NONE
     rounds.
   - Assert that it matches the stdin snapshot and the `BET_x` hint.
   - On a mismatch: log it (dev), resync to the snapshot, and play anchor-only for the rest of the
     hand.
2. **Learn (≤0.5 ms).** For each finished hand, turn every opponent decision into a labelled triple
   (context(y_true), action). Update the type log-weights, the Dirichlet counts (including the
   depth-bucketed jam/call counts), the bet-size signature, and G_t (§6). Before M4 the counts are
   recorded but not used.
3. **Beliefs.**
   - On a new board, recompute the 1,326 combo buckets (~30 µs).
   - For each opponent action, update b_j(y) (anchor/population likelihoods, M2+) and b_j(y,k)
     (model, M4). Table likelihoods cost <0.1 ms.
4. **Anchor.** Build 4-6 candidate actions, mapped to legal outputs, with duplicates removed (sizes
   that collapse to the minimum bet or to ALL-IN). Evaluate π₀ for the current regime (§3).
5. **Fast path.** If exploitation is off, c < c_min, or only one sensible action exists, sample π₀
   with the observation-hash RNG and go to step 8.
6. **q̂, in prize units.**
   - Exact where possible: HU jam/call-off from the table (µs); river against the belief (14 µs);
     turn (0.75 ms).
   - Multiway jam/call-off: range-sampled MC with side pots and ICM leaves (2-10 ms).
   - Otherwise (M4b only): one-ply q̂, or CRN rollouts until 33 ms, checking the clock every 64.
   - Compute q̂_model and q̂_anc on the same samples.
7. **Step (§6), µs.**
8. **Emit (≤0.05 ms).**
   - Check or call is printed `CALL`; it becomes CHECK or ALL-IN as needed.
   - A fold when checking is free is printed `CHECK`, because FOLD is never replaced.
   - Never print `CHECK` when facing a bet: it becomes FOLD.
   - A raise to street total T is printed `BET x` with x = max(BET_min, T − myStreetBet), one space.
     If x ≥ stack, print `ALL-IN`.
   - Submitted builds print no `;message`.
9. **Guards.**
   - If a deadline arrives before step 7, play argmax π₀, which was computed in step 4.
   - If the anchor is unavailable, play the M0 baseline rule (<1 ms).
   - Past round ~560: the variance rule from §3.

| step | typical cost (sandbox, scaled at T0) | hard limit |
|---|---|---|
| T0 | 150-300 ms | 700 ms |
| 1-2 | <1 ms | |
| 3 | <0.1 ms (tables, K≈24 types × 1,326); 1-3 ms per action only with net likelihoods (M6) | |
| 4-5 | <0.1 ms | |
| 6 exact | µs (HU table, river), 0.75 ms (turn), 2-10 ms (multiway MC) | |
| 6 rollouts (M4b/M6) | the rest of the budget | 33 ms |
| 7-8 | µs | flush by 38 ms |

---

## 6. The bounded-exploitation step (M4)

**Inputs, over the candidate set A:**

- the anchor π₀;
- q̂_model(a): opponents play the fitted model;
- q̂_anc(a): opponents play the anchor or population;
- SE(a);
- the confidence c.

**Units.** q̂ is computed in prize units and divided by the local slope s = ∂U/∂chips·BB before the
step, so Δq is in BB-equivalents. One BB is worth 0.002-0.08 payout units depending on level and N
(research, `solvers/icm2.py`), so a single η_max per regime then works across blind levels, and the
`solvers/mmdstep2.py` demo, which is in BB units, applies directly. This resolves the unit conflict
between the research designs.

**Magnet ρ = π₀, never uniform.** With ρ = π₀ the closed form collapses:

π_s ∝ exp{[log π₀ + η·q̂ + ηα·log π₀]/(1+ηα)} = π₀ · exp(η′·q̂), with η′ = η/(1+ηα).

So α only rescales η, and it is set to 0. The step is a KL-regularised best response, and an action
with π₀ = 0 stays at 0, so π₀ needs no ε-smoothing. In the 10 BB demo, smoothing with ε = 0.02
alone cost 0.006-0.022 BB/hand.

A uniform ρ with η in prize units acts as a temperature 1+ηα on the anchor. The critique's example:
ε = 0.005, η = 300, Δq = 0.005, α = 0.02 gives log-odds (5.29 + 1.5)/7 = 0.97, so the bot plays off
the anchor 27% of the time; with ρ = π₀ the same inputs give 99.6% on the anchor. (Stratego's
constants are η = 50, ηα = 0.1.)

**Rule A: near-deterministic anchors** (max π₀ ≥ 0.99: thresholds, charts). Let a₀ = argmax π₀ and
a* = argmax q̂_model. Deviate to a* only if all of these hold:

1. q̂_model(a*) − q̂_model(a₀) ≥ z·SE + m. SE = 0 in HU table and river spots; it is the MC SE
   in multiway jam/call-off spots. m is a small margin in BB.
2. c ≥ c_min.
3. The anchor-regime loss L = q̂_anc(a₀) − q̂_anc(a*) ≤ B_t, where B_t = ε₀ + κ·G_t. After a
   deviation, G_t −= L.
4. (Optional) the deviation is robust across the posterior: q̂_k(a*) ≥ q̂_k(a₀) − δ for every type k
   with P(k|D) ≥ 0.1.

This replaces the earlier η ≤ 0.3/SE cap. With ε ≤ 0.005 anchors, that cap needed Δq/SE ≈ 10-18
before any deviation, so it would never have fired outside exact spots.

**Rule B: mixed anchors** (bluff/value frequencies, indifferent hands). Set π_s ∝ π₀·exp(η′·Δq),
with η′ = η_max(regime)·c. Take the largest η′ in {η′, η′/3, η′/10, 0} such that
⟨π_s − π₀, q̂_anc⟩ ≥ −B_t, and charge that amount to G_t when it is negative. Start at η_max = 3 per
BB, the demo's conservative point, and tune.

**Soft-anchor alternative.** Set π₀ = softmax(Q_anc/τ), so near-indifferent hands mix and Rule B
applies everywhere. Rule A and soft anchors are compared in M4 (Q-E5).

**Several opponents.** c = min_j c_j over the opponents still in the hand whose responses enter q̂.
This is the conservative default (Q-E3).

**G_t is expected gain, not realised chips.**

- It is the sum over this game's finished hands of each hand's **all-in-adjusted** prize-unit
  result, minus the anchor-vs-anchor baseline for that seat and depth, floored at 0. For example,
  with the HU jam/fold anchor at 10 BB the baselines are SB −0.045 BB and BB +0.045 BB.
- All-in-adjusted means exact equity over the remaining boards replaces the dealt runout. Revealed
  cards make this exact.
- AIVAT-style correction on non-all-in streets is an M5 refinement.
- This follows Ganzfried and Sandholm's "risk what you've won in expectation". A realised-chip budget
  would grow after lucky runouts and shrink after unlucky ones.
- Near-Nash default: ε₀ = 0 (strict) and a small κ. Both are tunable.

**Scope.** In HU spots q̂_anc is computed against the Nash anchor, so the budget bounds exactly
what our deviations cost against an equilibrium opponent. That is the right check for model error
("what if they actually play the anchor"). It is not the worst case against an opponent that
adapts to us; the research's Pinsker bound, ⟨π_s − π₀, q*⟩ ≥ −(R*/2)·√(2η·R̂), remains a
secondary check. At 3-4 players the anchor-regime check is a heuristic.

---

## 7. Offline pipeline

| step | tool (repo path) | output | when |
|---|---|---|---|
| 1. Harvest, after the ToS check | `tools/fetch_replays.py` (≥2 s/request, resumable) | `data/replays/`, `data/battles/`, dated `data/snapshots/` | week 1, then weekly and before each submission |
| 2. Reconstruct | `sim/reconstruct.py`; `replayer/replay_game.py` + `analysis/analyze.py` | `data/cache/decisions.jsonl`, `data/cache/replayed.pkl` | after each harvest; `sim/validate_replays.py` and `replay_game.py --check` must stay at 100% |
| 3. Field statistics and drift | `analysis/stats*.py`, `steal_se.py`, `elim_depth.py`, `stack_depth.py`, `hand_outcomes.py` | style tables; diff against the previous snapshot | after each harvest |
| 4. Equity table | `solvers/eq.c` → `solvers/eq169.bin`, cross-checked with `cpp/pushfold.cpp`; CJK14 via crossfish `tools/nnue_cjk14.py` | 8.1k-char payload | M0 |
| 5. HU jam/fold | `solvers/pf.py` (+ `solvers/exploit.py` for best responses) | 194-char thresholds; unit tests | M1 |
| 6. ICM and 3-4p jam/fold grid | `solvers/icm2.py`, `solvers/trueskill_payouts.py`; new grid solver and distiller | distilled model (2-4k chars) + test grid | M1 (v1), M5 (value iteration) |
| 7. HU 8-30 BB game; 3-4p charts | new CFR+ solvers with realisation factors from blueprint self-play in the C++ engine | strategy tables | M2, M3 |
| 8. Library | `oppmodel/oppmodel2.py`, extended with depth buckets, a time split and per-bot export | 4-bit tables (≤8k chars) | M4, refreshed before each submission |
| 9. W (FGS) | simulated zoo tournaments in the C++ engine | 2-5k-param model | M5 |
| 10. Tuning | arena + SPSA/CLOP | knob values | M5 |
| 11. MMD-PPO | C++ vectorised engine; `cpp/mlp.cpp`, `cpp/train_cost.py` for costs | int8 net | M6 |
| 12. Pack and gate | crossfish `tools/cg_minify.py`, `tools/nnue_cjk14.py`, `tools/cg_gate`, `tools/cg_perf_gate.py` | single-file submission | every release |

**Non-goals:**

- **No deck or seed prediction.** The live RNG is SHA1PRNG with a seed the bots never see, and
  prediction is against the spirit of the game. The repo rebuilds decks only from the seed a public
  replay records (`sim/`, `replayer/`); no seed-recovery tooling exists here, and none should be
  written.
- **No vendoring of referee sources.** `third_party/CodingamePoker` has no LICENSE file, so it stays
  a submodule reference.
- `cpp/cmp.cpp` needs OMPEval and PHEvaluator sources, which are not in the repo.

---

## 8. Evaluation methodology

**Correctness.** As in the M0 gates: engine fuzz, 381/381 replays, and tracker assertions on 74,347
decisions. These checks run in CI on every engine change.

**Arena modes.**

- **Fast mode:** fixed compute per decision (a fixed MC trial count, no wall-clock search). Use it to
  screen changes that do not depend on search time.
- **Fidelity mode:** the real binary under real time limits. Required for any change that depends
  on search, and for every release candidate.
- **Realistic throughput.** A search bot spends about 25-33 ms on each of its ~55-60 decisions, so
  1.5-2 s of CPU per game for its seat. 10k fidelity games therefore take about 4-6 core-hours.
- The earlier "~6 four-player games/s per core" applies only to rule bots.
- Run timed games on physical cores only, one SPRT at a time. Parallel timed SPRTs steal CPU and
  bias results (crossfish `documentation/improvement_log.md`).

**Opponents.**

- Scripted archetypes, fitted to replay statistics but not from the library: gpoussel port,
  Tuo-style push/fold, kovi-style nit, Waffle3z-style LAG, BrandV-style jammer, maniac, station,
  boss.
- **Time-split clones**: fitted to games after the library cutoff.
- Shape-shifters (from M4).
- Our previous versions.
- Clones only saw states the real bots reached, so gains measured against them in new lines (small
  steals, check-raises) are suspect until confirmed by a real-opponent run.

**Real-opponent channel (release gate, if the terms of service allow).** CGBenchmark-style IDE play
against the live code of leaderboard agentIds
([forum](https://forum.codingame.com/t/cgbenchmark-tool/2956)), within CodinGame's rate limits. It
is the only local-to-live channel without clone artefacts.

**Variance control.**

- Duplicate seeds with seat rotation.
- Paired candidate/baseline runs on identical seeds.
- Observation-hash bot RNG, so π_s sampling does not break the duplicate correlation.
- Re-measure the variance ratio with the real bots. Use 0.61 and 0.80 only as best cases.
- Optional control variate: all-in luck (realised minus expected equity).

**Statistical gates.**

| test | use | approximate cost |
|---|---|---|
| SPRT, H1 Δp = +2 pp, α = β = 0.05 | milestone gates | 3-10k games |
| Small-effect SPRT, H1 = +0.75 pp | refinements, SPSA steps, charts | about (2/0.75)² ≈ 7× more, i.e. 20-70k games, overnight on many cores |
| Pooled non-inferiority: pooled Δp over the archetypes, one-sided 95% bound ≥ −1 pp | every gate; replaces the per-archetype 1 pp rule, which needed ~1,250-1,500 games per archetype | shares the SPRT games |
| Per-archetype Δp with CIs | reported only, not a gate | — |

**Timing.** p99.9 turn time below the flush deadline on the gcc 11.2 docker gate with CodinGame
flags. 0 timeouts in 5k fidelity games for a release.

**Live monitoring, with no threshold gate.**

- One submission plays about 100 games: ±5 pp on p, about ±0.75 score points.
- Accumulate battles over the submission's lifetime with `findLastBattlesByTestSessionHandle`,
  rebuild them with `sim/reconstruct.py`, and report p against the top 10 and top 42 with CIs,
  per-regime all-in-adjusted chip EV, and fingerprint calibration.

**Resubmission policy, fixed in advance.** Resubmit only after a milestone SPRT pass, or after
accumulated small-effect passes worth ≥ +2 pp locally, and never in reaction to a single
submission's rank.

---

## 9. Character budget (100k UTF-16 units)

| item | chars | basis |
|---|---|---|
| Code | 35-45k | critique estimate; crossfish needed 48,760 for a simpler game. See the breakdown below. |
| 169×169 equity table, 8-bit | 8.1k | 14,196 × 8 bits / 14 |
| HU jam/fold thresholds | 0.2k | 169 × 2 × 8 bits |
| HU 8-30 BB preflop strategy | 1-2k | estimate; depends on the M2 tree |
| Distilled 3-4p jam/fold model | 2-4k | M1 target |
| 3-4p deep charts | 6-8k | 4p alone ≈ 5.8k (10 × 4 × 6 × 169 × 2 bits) |
| Opponent library (M4) | 3-8k | 17 types × 112 contexts ≈ 3k at 4 bits; 20-25 types with depth buckets up to 8k |
| Safety margin | 3k | |
| **Total without a net** | **≈58-78k** | |
| **Left for an optional M6 net** | **≈22-42k** | ≈40-70k int8 parameters at 0.571 chars/param. Plan on 40-60k until measured. |

What the code line has to cover: the engine and tracker, the parser, pe7c (2.7k), MC and exact
equity, ICM, the jam/fold model, the range tracker, library inference and fingerprinting, the step,
the postflop blueprint, the CJK14 decoder, and time guards.

Measure, don't assume: minify the M0 skeleton (M0) and the M3 bot with crossfish `cg_minify.py`
before promising any network size. If the budget is short, cut in this order: the net (M6), library
size, chart resolution.

---

## 10. Risks

| risk | mitigation |
|---|---|
| A timeout eliminates the bot; CodinGame's CPU, compile mode and latency are unknown. | The M0 skeleton measures them; first-turn benchmark scaling; 33/38 ms deadlines reset from p99.9; clock checks per batch; no allocation in the turn loop; the gcc 11.2 gate; 5k fidelity games per release. |
| Rule-fidelity bugs (dead SB, BET = chips added, CHECK→FOLD, short all-in raise rights, ALL-IN without an amount). | Engine fuzz against `sim/poker_sim.py`; 381 exact replays; per-turn tracker assertions. |
| Utility misspecified (equal-rating payouts; ICM ignores blinds and position). | A/B test ICM, rating-conditioned payouts, rank-linear utility and W on finish metrics (Q-B1). |
| No 3-4p guarantee (kingmaker effects). | Treat 3-4p anchors as heuristics and gate them by arena results; the HU phase carries the guarantee. |
| The offline 3-4p grid solve is too slow or does not converge. | Measure on a 3p slice in M1 week 1; Ganzfried-Sandholm criterion; coarsen the grid; tabulate multiway outcomes. |
| Distillation error. | EV-loss gate against exact offline solves. |
| Wrong preflop leaf values. | Measured realisation factors, iterated once; at its 8 BB end, the HU game must agree closely with the jam/fold solution. |
| The library goes stale (frequent resubmissions). | Time split, recency weighting, submissionId tags, an "unknown" type, the Dirichlet residual, confidence-scaled η, weekly refresh. |
| Exploitation backfires (misidentification, adaptive bots such as kovi's `anti-maniac`). | Rule A gates, an expected-gain budget, shape-shifter tests; η = 0 remains a shippable fallback. |
| Arena overfitting and clone artefacts. | Time split, scripted archetypes, the real-opponent channel, live post-mortems. |
| The character budget is squeezed. | Measure early; drop or shrink the M6 net first. |
| ToS or API changes stop the harvest. | Check first; 2 s throttle; local archive; the plan works with the 381 games already here (less fresh). |
| Leaderboard noise and a moving field. | Decide on local SPRT; fixed resubmission policy; accumulate live battles. |
| Debug messages leak our strategy. | Build flag; no `;message` in submissions. |
| The referee crashes on a NONE round at round 600. | Never observed (0/381); the engine raises; ignore unless seen live. |

---

## 11. Corrections to earlier research claims

| earlier claim | correction | evidence |
|---|---|---|
| "Jam/fold is worth −0.045/−0.127/−0.183 BB/hand for the SB at 10/15/20 BB, so pure jam/fold is adequate only up to ~8 BB." | **A hypothesis, not a finding.** `pf.py` computes the SB's value inside the jam/fold game, a lower bound on its full-game value. The gap to unrestricted play was not computed; M2 measures it (Q-D1). | `solvers/pf.py` |
| "Exact q̂ for jam or call-off as a sum over caller subsets using the equity table." | Exact **HU only**. The 169×169 table has no 3-4-way equities or side-pot outcomes, and ICM needs the joint bust distribution. Multiway is Monte Carlo. | `solvers/eq.c`, `solvers/icm2.py` |
| "Types + online 0.653 is essentially equal to an oracle that knows the bot (0.652)." | The oracle is a static per-bot table, not an upper bound. It beats types+online on decisions 1-10 (0.579 vs 0.718) and loses from decision 11 on (0.598 vs 0.586, 0.671 vs 0.648, 0.706 vs 0.674). The equal averages are a coincidence of two opposite errors. Identification costs about 0.14 nats over the first 10 decisions; after that, the coarse contexts limit accuracy. All figures are in-sample. | reproduced, `oppmodel/oppmodel2.py` |
| "ρ uniform or π₀ makes no measurable difference at α ≤ 0.02." | True only at ηα ≤ 0.2 in BB units (`mmdstep2.py`, `mmd_ab.py`). In prize units a uniform ρ randomises deterministic anchors. Use ρ = π₀ (§6). | `solvers/mmd_ab.py` |
| "η = min(η_max·conf, 0.3/SE(Δq̂)) with ε ≤ 0.005 smoothing." | Together these make the step inert wherever rollouts are needed. Replaced by Rule A, Rule B and soft anchors (§6). | arithmetic: flipping needs η·Δq > ln(0.995/0.005) = 5.3 |
| "Runtime 3-4p push/fold: 50-200 best-response iterations, cached per hand." | About 1000× over budget, and a short stack usually decides once per hand. Solve offline and distil (M1). | critique cost estimate |
| "Take ALL-IN amounts from stack/chipInPot deltas." | Stdin is one snapshot at our turn. Replay the hand with the engine. | `InputSender.java:70-73` |
| "Fast mode: about 6 four-player games/s per core." | Rule bots only; a search bot needs ≥1.5 s of CPU per game. | §8 |
| "About 25k code chars; net of ≤70k int8 parameters." | 35-45k code; about 40-60k parameters until measured. | crossfish `minification.md:547` |
| "p = 0.62 gives 30.6, level with today's #1." | Extrapolated from 8 points and dependent on opponents. A rough guide only (§1). | `analysis/pairwise.py` fit; AGSigma counterexample |
| "HU pilot within 0.02 BB/hand of Nash jam/fold." | Ill-posed for a full-action net; corrected gate in M6. | — |
| "A 2.5 BB steal needs 67/57/50% folds" (Design 2). | 57/50/44%: the opener's own posted SB comes back. 2 BB: 50/43/37.5%. | arithmetic |
| "+2.89 BB for opens ≤2 BB at 4p"; "steal wide loses against the field." | Both are noise: +2.89 ± 1.64 (n=64); −1.14 ± 0.87 (n=247). Open width must be decided per opponent and in the arena. | `analysis/steal_se.py` |
| "Duplicate seeds cut variance 1.5-3×" (Design 1); "0.61/0.80 apply to our bot." | 0.61/0.80 were measured with deterministic toy bots and are a best case. A stochastic bot needs per-duplicate RNG seeding, and the ratio must be re-measured. | `eval/dup.py`, `eval/dup4.py` |
| "G_t = realised gain." | Expected gain: all-in-adjusted or AIVAT-style (§6). | Ganzfried-Sandholm, safe exploitation |
| "Regress W on arena tournaments." | Confounds skill with chips. Use simulated zoo tournaments. | — |
| "Live p against the top 10 ≥ 0.58 as a gate"; "no archetype more than 1 pp worse." | Unmeasurable (±5 pp) and unaffordable (12k+ games) respectively. Replaced by live accumulation and pooled non-inferiority. | §8 |
| "8P FLOP per training sample." | That assumed 4 PPO epochs; Ataraxos uses 1, giving about 3.5P. | Ataraxos supplement, Table S7 |
| "4-handed ICM charts (~86k chars) don't fit, so solve at runtime." | Runtime solving is also infeasible. Distil offline solves into 2-4k chars. | M1 |
| "Charts: 2-6k chars." | 4p charts alone are ≈5.8k. Budget 6-8k. | §9 |
| "The top 3 all resubmitted between 25 and 30 Sept." | #1, #3 and #6 did (and #10, #12); kovi (#2) has not resubmitted since 2026-04-05. The conclusion (the field moves; harvest early) stands. | `analysis/leaderboard.py` (`creationTime` in `cg_lb.json`) |
| "59-75% of games are 3-4 players." | About 75%: 1,866 of 2,495 recent battles of the top 37 bots (77% of the 371 replays). | `analysis/pairwise.py`, `analysis/stats.py` |

---

## Appendix: tool index

| path | what it gives the plan |
|---|---|
| `sim/poker_sim.py`, `sim/validate_replays.py`, `sim/reconstruct.py`, `sim/replay_io.py` | Python referee port with byte-exact SHA1PRNG (381/381 replays), decision records, `obs_to_stdin` for parser tests |
| `replayer/` (`replay_game.py`, `Replayer.java`, `PreEq.java`) | Java-side oracle using the referee's own classes (381/381) |
| `analysis/` | field and structure statistics (§2): `stats*.py`, `steal_se.py`, `elim_depth.py`, `stack_depth.py`, `hu_phase.py` (fact 4), `regimes.py` (fact 13), `pairwise.py` and `leaderboard.py` (§1 score fit, table mix, head to head) |
| `solvers/eq.c`, `solvers/eq169.bin` | 169×169 HU preflop equity |
| `solvers/pf.py`, `solvers/exploit.py` | HU jam/fold Nash; best-response gains |
| `solvers/mmdstep2.py`, `solvers/mmd_ab.py` | one-step MMD demo (BB units, ρ uniform) |
| `solvers/icm2.py`, `solvers/trueskill_payouts.py` | ICM with TrueSkill payouts; call-off thresholds |
| `oppmodel/oppmodel2.py` | bot library plus online model benchmark (in-sample) |
| `eval/dup.py`, `eval/dup4.py` | duplicate-seed variance ratios (toy bots) |
| `cpp/pe7c.hpp`, `cpp/test_eval.cpp`, `cpp/bench.cpp`, `cpp/cmp.cpp` | evaluator, exhaustive test, speed, cross-check |
| `cpp/rvr.cpp`, `cpp/pushfold.cpp`, `cpp/mlp.cpp`, `cpp/train_cost.py` | runtime primitive costs; training cost |
| `tools/fetch_replays.py`, `tools/sel_games.txt`, `tools/sim_games.txt` | harvest; named game sets |
| crossfish `tools/` (`sprt_merge.py`, `cg_minify.py`, `nnue_cjk14.py`, `cg_gate`, `cg_perf_gate.py`) | SPRT, minification, packing, compile and speed gates |
