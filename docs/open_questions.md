# Open questions

These are the unresolved questions behind [`plan.md`](plan.md), the equilibrium-first plan. They
come from the research run ([`research/raw/workflow_results.json`](research/raw/workflow_results.json)),
its critique, and writing the plan. Each entry says why the question matters, how to resolve it,
and the milestone where it should be resolved. IDs (Q-A1, ...) are referenced from the plan.

Questions about opponent modelling, bot fingerprinting and replay harvesting were dropped on
2026-10-01 together with that part of the plan (see [`../archive/`](../archive/README.md); the old
list is in git history). Remaining IDs were kept so old references still resolve; gaps in the
numbering are the dropped questions.

Status, 2026-10-01: all open.

---

## A. Platform and protocol

**Q-A1. Does CodinGame keep a bot process running between its turns (pondering)?**
- *Why:* if a background thread can work on opponents' time, the 3-4p solving and rollout budgets
  change completely. The referee and SDK sources don't settle it, and a web result only suggests that
  processes are not shut down between turns.
- *Resolve:* in the M0 I/O skeleton, a background thread increments a counter. Log the counter
  delta and the wall time between our turns, and compare the turn-latency distribution with the
  thread on and off. Only build on it if work progresses between turns *and* latency does not
  suffer.
- *When:* M0.

**Q-A2. What is the real timeout margin?**
- *Why:* a timeout eliminates the player, and it has happened in the top league (deuwii, game
  906358479). The referee pins SDK 4.4.2; the current SDK master adds `SOFT_TIMELIMIT_EXTRA` = 50 ms.
  I/O and scheduling overhead and the frequency of latency spikes are unknown. The plan's 33 ms
  compute stop and 38 ms flush are placeholders.
- *Resolve:* log per-turn latency (first line read → flush) over several hundred live turns in the
  M0 skeleton. Set the deadlines from p99.9 with a margin, then re-check after every release.
- *When:* M0, then every release.

**Q-A3. Which C++ mode does CodinGame compile with?**
- *Why:* the help page (archived 2026-02) says g++ 11.2 "mode C++20". The owner's CI-verified line
  is `-std=gnu++17 -Werror=return-type -g -pthread` with no `-O`. Language features and the pragma
  placement depend on it.
- *Resolve:* print `__cplusplus` to stderr on the first turn of the M0 skeleton. Keep the crossfish
  gcc 11.2 docker gate as the local reference.
- *When:* M0.

**Q-A4. How fast is CodinGame's CPU compared with the research sandbox?**
- *Why:* every µs figure (pe7c, `cpp/rvr.cpp`, `cpp/mlp.cpp`) was measured on a Xeon at 2.1 GHz with
  g++ 13.3, not 11.2. AVX2 is known to work on CodinGame, since the UTTT NNUE uses it.
- *Resolve:* run a 20-50 ms benchmark on the first turn (evaluator evals/s, MC trials/s, MLP
  µs/eval) and scale budgets from it. Re-time the primitives on the gcc 11.2 docker gate.
- *When:* M0.

**Q-A5. What happens when the referee crashes on a NONE round exactly at round 600?**
- *Why:* if a hand with no decisions falls on round 600, the round becomes 601 and `roundInfos[601]`
  overflows. The platform's handling of a crashed referee is unknown.
- *Resolve:* nothing to do unless it is seen live. It has never happened (0/381 games reach the cap).
  The C++ engine should raise, as `sim/poker_sim.py` does.
- *When:* only if it is observed.

**Q-A6. How does matchmaking choose the table size, and does the mix depend on rating?**
- *Why:* the plan uses 41/34/25% (4p/3p/HU) from 2,495 recent battles of the top 37 bots. Waffle3z's
  last 134 battles were 43/38/19%. The arena mix and the per-size weighting of gains both depend on
  this.
- *Resolve:* tabulate the table-size mix from our own battles after each submission.
- *When:* after the first submissions.

---

## B. Ranking and utility

**Q-B1. How does CodinGame's TrueSkill value 2nd vs 3rd vs 4th, and handle ties?**
- *Why:* the plan uses equal-rating TrueSkill μ deltas: (1,0), (1,.5,0), (1,.6444,.3556,0)
  (`solvers/trueskill_payouts.py`). At unequal ratings they shift, e.g. to (1,.623,.325,0) for μ=30
  against 29/28/27 at σ=1. Tied scores (same-hand busts with equal chips) also need handling. The
  call-off thresholds and all ICM decisions depend on these numbers.
- *Resolve:*
  1. Sensitivity check with `solvers/icm2.py`: do the call-off thresholds and jam ranges move
     materially between equal-rating payouts, rating-conditioned payouts and rank-linear payouts?
  2. Arena A/B of those utilities and the learned W, scored on finish-position metrics.
  3. Collect any CodinGame-staff statement on the exact update (forum thread `t/381`).
- *When:* M1 (sensitivity), M5 (A/B).

**Q-B2. What is the exact displayed score formula, and how many games does a submission play?**
- *Why:* the research found μ − 3σ cited for the initial position, and "10 placement games plus
  about 100 sequential games" from a forum quote. Neither is documented for this arena. Both affect
  how much one submission tells us and the resubmission policy.
- *Resolve:* follow our own submissions: games played over time, score trajectory, and how long the
  score keeps moving after the initial games.
- *When:* the first live submission (M0).

**Q-B3. How much does matchmaking bias the pairwise rate p?**
- *Why:* p is measured against each bot's own matchmade opponents. A bot that climbs faces stronger
  opponents and its p falls. AGSigma (#10) has p = 0.598, which the score fit would put at #2. The
  p → score map (21.13 + 15.30·p, top 8, r = 0.961) is therefore only a rough guide.
- *Resolve:* in our own battles, model p as a function of opponent rating and report
  rating-adjusted p, extending `analysis/pairwise.py` (which computes the fit above).
- *When:* M0, then after each submission.

**Q-B4. What does the lower-league boss play, and what is its score?**
- *Why:* promotion requires finishing ahead of the boss. The live boss (agent 5117680) limps 43% of
  unopened pots and folds 95% when raised, unlike the `CALLING STATION` template in
  `config/Boss.java`. Its score needs a division-room leaderboard endpoint that was not found.
- *Resolve:* the first live submission answers whether M0 promotes.
- *When:* M0.

---

## C. Data and terms of service

**Q-C1. Do CodinGame's terms allow automated replay download?**
- *Why:* the plan no longer needs more replays; the 381 committed ones validate the engine and the
  tracker. Fetching our own bot's battles for post-mortems still uses the undocumented
  `findLastBattlesByTestSessionHandle` and `gameResult/findByGameId` endpoints
  (`tools/fetch_replays.py`, never run against the live API).
- *Resolve:* read the terms before any automated use; otherwise inspect our games by hand in the
  web viewer.
- *When:* before the first post-mortem.

---

## D. Game theory and solving

**Q-D1. How much does unrestricted HU play (limp, min-raise) gain over jam/fold at 8-30 BB?**
- *Why:* `solvers/pf.py` gives the SB's value *inside* the jam/fold game: −0.045, −0.127 and −0.183
  BB/hand at 10, 15 and 20 BB. That is only a lower bound on the SB's full-game value. The plan's
  claim that jam/fold is adequate only to ~8 BB is a hypothesis. HU decisions in 3-4p games sit at a
  median effective stack of 14-18 BB.
- *Resolve:* the M2 solve of {fold, limp, 2x, 2.5x, jam} with realisation-factor leaves. Report
  V(unrestricted) − V(jam/fold) at 10/15/20 BB within the abstraction, and confirm the result in the
  HU arena.
- *When:* M2.

**Q-D2. What does the offline 3-4p ICM jam/fold grid cost, and does it converge?**
- *Why:* runtime solving is infeasible (about 0.24 s per iteration), so M1 depends on an offline
  grid over N, seat roles, stacks and blind level. Multiway leaves need joint finish distributions,
  so the 169×169 table is not enough. Fictitious play has no convergence guarantee with 3+ players;
  Ganzfried-Sandholm only observed convergence.
- *Resolve:*
  1. In M1 week 1, time a 3p slice.
  2. Measure each player's best-response gain against the Ganzfried-Sandholm criterion (<0.1% of
     the pool).
  3. Choose between tabulating class-triple outcome probabilities offline and per-iteration MC.
  4. Measure how often 4-way all-ins occur in the replays, to decide whether 4-way leaves need full
     treatment.
- *When:* M1.

**Q-D3. How small can the distilled 3-4p jam/fold model be at acceptable EV loss?**
- *Why:* the budget allows 2-4k chars. Full charts would be ~86k for 4p.
- *Resolve:* compare per-node threshold models and 3-5k-parameter MLPs by mean and max ICM-EV loss
  against the exact solution on held-out grid points. The plan's gate is ≤0.1% of the pool per
  decision, an estimate to revisit.
- *When:* M1.

**Q-D4. Where should the regime boundary T(N, position) between jam/fold and open play be?**
- *Why:* T starts at 10-15 BB for 3-4p, from the literature and the elimination statistics.
  Miltersen-Sørensen bound the jam/fold loss only for HU ≤6.7 BB.
- *Resolve:* SPRT a few values in M1, then tune by SPSA in M5. For HU, M2's solution makes the
  boundary implicit.
- *When:* M1, M5.

**Q-D5. Do the postflop realisation factors converge?**
- *Why:* the preflop solves (M2 HU, M3 3-4p) value flop leaves at pot · R(position, SPR, N) ·
  equity. R comes from our own play against itself and is re-measured once after the re-solve.
- *Resolve:*
  Track how much R and the preflop strategy change between solve iterations 1 and 2.
- *When:* M2-M3.

**Q-D6. For 3-4p deep charts, does the solver (CFR+ or fictitious play) matter?**
- *Why:* neither converges in theory at 3-4 players. The charts are heuristic, and the choice might
  not matter compared with leaf-value error.
- *Resolve:* solve both ways and SPRT the charts against each other and against a hand-built
  dead-money baseline.
- *When:* M3.

**Q-D7. Is single-hand ICM enough, or is a learned tournament value W (FGS) needed?**
- *Why:* ICM ignores blinds and position. Ganzfried-Sandholm found it off by up to $2.99 per $100 at
  3p jam/fold. W must be trained on simulated tournaments: arena data confound chips with skill.
- *Resolve:* in M5, train W on simulated self-play tournaments, starting from ICM, and A/B it against ICM
  in the arena.
- *When:* M5.

**Q-D8. How big an HU abstraction fits the character budget, and how much does distillation lose?**
- *Why:* full postflop tables do not fit (plan §2, M2), so M2 ships preflop tables plus a distilled
  postflop net. Both the abstraction size and the net size trade against exploitability.
- *Resolve:* in M2 week 1, count decision nodes × buckets for candidate trees. After solving, measure
  the distilled net's policy KL to the CFR solution and its exploitability in the abstraction
  against the tabular solution's.
- *When:* M2.

**Q-D9. Is unsafe river re-solving good enough, and how many CFR iterations fit?**
- *Why:* re-solving with ranges built from our own strategy is cheap but can be exploitable; safe
  re-solving (Brown & Sandholm 2017) costs more. The ~0.1 ms per iteration figure is an estimate.
- *Resolve:* time the river solver at CodinGame speed; compare blueprint, unsafe and safe re-solving
  by local best response.
- *When:* M2.

**Q-D10. How cheap can local best response be made, and how tight is it?**
- *Why:* LBR is the plan's real-game exploitability yardstick (plan §5). Its cost grows with the
  number of candidate actions and the rollouts per action, and it only gives a lower bound.
- *Resolve:* implement LBR on the C++ engine with exact river equity; check it against exact best
  responses where both exist (the jam/fold game, the abstract HU game).
- *When:* M2.

**Q-D11. Self-consistent beliefs: how much do they cost against bots far from equilibrium?**
- *Why:* ranges are updated with our own strategy's likelihoods. A bot that opens to 50 BB with
  anything looks far too strong under that model. Population-calibrated likelihoods would be
  closer to the field but are a step away from equilibrium play.
- *Resolve:* only if results suggest it: compare the ε floor values, and measure how often
  observed actions have near-zero likelihood under our strategy.
- *When:* M2-M3.

---

## F. Evaluation

**Q-F1. What duplicate-seed variance ratio do the real bots achieve?**
- *Why:* 0.61 (HU) and 0.80 (4p) come from deterministic toy bots (`eval/dup.py`, `eval/dup4.py`).
  Bots that sample their actions decorrelate the duplicates unless their RNG is seeded per
  duplicate. All SPRT game-count estimates depend on this ratio.
- *Resolve:* in M0, re-measure with the baseline bot and the generic opponents, with observation-hash
  RNG seeding.
  Also measure the paired candidate-vs-baseline design.
- *When:* M0.

**Q-F3. How much local evidence justifies a resubmission?**
- *Why:* about 100 live games give ±5 pp on p, roughly ±0.75 score points, while #1-#5 are close
  together. The plan's rule is to resubmit only after a milestone SPRT pass, or after accumulated
  small-effect passes worth ≥ +2 pp.
- *Resolve:* revisit after observing 2-3 submissions' score trajectories (Q-B2). Adjust if the
  ladder settles faster or slower than assumed.
- *When:* after M1.

**Q-F4. What does small-effect SPRT cost in practice?**
- *Why:* the +2 pp H1 has low power for +0.5 to +1 pp refinements (charts, SPSA). The estimate is
  about 7× the games, 20-70k.
- *Resolve:* measure the per-game variance and fidelity-mode throughput with the real bots, then fix
  H1 and the game caps for refinement runs.
- *When:* M5 (estimate in M0).

**Q-F5. What throughput does fidelity mode reach on the available hardware?**
- *Why:* a search bot spends 1.5-2 s of CPU per game. Timed runs must use physical cores only, one
  SPRT at a time (crossfish lesson).
- *Resolve:* benchmark the arena in fidelity mode in M0 and size the gates from it.
- *When:* M0.

---

## G. Engineering budgets

**Q-G1. What is the real minified code size of the bot?**
- *Why:* the research's ~25k-char estimate was optimistic by 1.5-2×. The plan assumes 35-45k, and
  the room for the net (≈40-60k int8 parameters) depends on it.
- *Resolve:* minify the M0 skeleton and the M3 bot with crossfish `tools/cg_minify.py` and record
  characters per module.
- *When:* M0, M3.

**Q-G2. Is 8-bit quantisation enough, or is 6-bit Rice/GPTQ needed?**
- *Why:* this matters for the nets (M2 distillation, M4) and for table precision. The equity table at 8 bits has steps of 0.0039, close to its MC SE of 0.0035.
- *Resolve:*
  1. Measure the policy KL between float and quantised weights (target <0.005) and the arena impact.
  2. Check that the 8-bit equity table changes no M1 jam/call decision compared with float.
- *When:* M0 (table), M2/M4 (net).

**Q-G3. Can range updates with network likelihoods keep up at 4 players?**
- *Why:* about 3 ms per opponent action for a 100k net with the first-layer trick, and up to 6
  actions per turn at 4p, is tight. Table likelihoods cost microseconds.
- *Resolve:* time it in M4 at CodinGame speed. Fall back to 169-class preflop updates or EHS buckets.
- *When:* M4.

---

## H. Training (M4, optional)

**Q-H1. How many samples does MMD-PPO need on 2-4p escalating-blind sit-and-gos with a 40-60k
parameter net?**
- *Why:* the 1e9-1e10 estimate extrapolates from AlphaHoldem and Ataraxos, not from a pilot.
- *Resolve:* run the HU pilot first, measured by exact exploitability in the jam/fold game with
  `solvers/pf.py`'s best response, or value ≥0 against Nash jam/fold plus local best response.
  Scale only if it converges.
- *When:* M4.

**Q-H2. Is MMD-PPO self-play stable and useful at 3-4 players?**
- *Why:* there is no convergence guarantee outside two-player zero-sum games. Self-play may cycle or
  settle into passive conventions.
- *Resolve:*
  1. Monitor against fixed references: the M1-M3 bots and our own Nash solutions.
  2. Ablate self-play against a pool of past checkpoints.
  3. Ship only through the M4 SPRT gate.
- *When:* M4.

**Q-H3. Should advantage filtering be used at all?**
- *Why:* raw poker returns are dominated by card luck, so filtering would select noise.
- *Resolve:* enable it only after the privileged critic and all-in equity substitution are in place
  and the critic's explained variance exceeds 0.3. Ablate filter rates 0, 0.5 and 0.75.
- *When:* M4.
