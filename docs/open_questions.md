# Open questions

These are the unresolved questions behind [`plan.md`](plan.md). They come from every `open_questions`
list in [`research/raw/workflow_results.json`](research/raw/workflow_results.json), the critique's
`missing_items`, and issues found while writing the plan. Each entry gives why the question matters,
how to resolve it, and the milestone where it should be resolved. IDs (Q-A1, ...) are referenced
from the plan.

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
- *Resolve:* tabulate the table-size mix by rating band from the harvested battle lists over time,
  and from our own battles after each submission.
- *When:* M0 harvest, then ongoing.

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
- *Resolve:* the first live submission answers whether M0 promotes. Fit a boss archetype from its
  replays for the zoo.
- *When:* M0.

---

## C. Data, terms of service and the field

**Q-C1. Do CodinGame's terms allow bulk replay download and automated IDE play?**
- *Why:* the harvest (`tools/fetch_replays.py`), library refreshes, live post-mortems and the
  CGBenchmark-style real-opponent release gate all rely on undocumented endpoints:
  `findLastBattlesByTestSessionHandle`, `gameResult/findByGameId`, and IDE play.
- *Resolve:* read the current terms and, if they are unclear, ask CodinGame before any bulk use.
  If they disallow it:
  - continue with the 381 local games and our own battles;
  - replace the real-opponent gate with manual IDE spot checks.
- *When:* week 1, before the harvest starts.

**Q-C2. What are the rate limits of those endpoints?**
- *Why:* battle history disappears when a bot resubmits, so a refresh has to finish within days. A
  throttle that is too aggressive risks a block.
- *Resolve:* run at the tool's minimum of 2 s between requests and back off on any error. Record
  observed failures. `tools/fetch_replays.py` has so far been tested only with `--dry-run` and
  mocked network calls, never against the live API.
- *When:* M0.

**Q-C3. How fast does a bot library go stale?**
- *Why:* the identification rates of 43/66/81/90% (after 5/10/20/40 decisions) and 0.653 nats were
  measured inside one 371-game window, with library and targets from the same pool. #1, #3 and #6
  (Waffle3z, BrandV, JuMaKre) resubmitted between 25 and 30 September.
- *Resolve:*
  1. Once there are two or more weekly harvests, fit on week *t* and score on week *t+1*, tagged by
     `submissionId`.
  2. Measure how much log-loss and identification degrade, and set the refresh cadence and the
     recency weighting from that.
- *When:* M4 (needs the archive started in M0).

**Q-C4. What do the top authors say about their approaches?**
- *Why:* the forum thread has 0 replies. Discussion probably happens on CodinGame's Discord, which
  the research could not access. Why 10 bots were submitted on 2026-09-25 is unknown: an event, a
  challenge, or a language update.
- *Resolve:* watch the forum and Discord. This is low priority, because style statistics from
  replays matter more.
- *When:* opportunistic.

**Q-C5. Are 371 games enough for per-matchup and depth-specific statistics?**
- *Why:* coarse style statistics are fine. Per-matchup profits, and ranges at specific depths for
  less active bots, have wide error bars (e.g. Tuo against kovi rests on n = 18).
- *Resolve:* grow the archive (Q-C1) and always report standard errors (`analysis/steal_se.py`
  style).
- *When:* ongoing.

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

**Q-D5. Do the postflop realisation factors converge, and do they depend on opponent type?**
- *Why:* the preflop solves (M2 HU, M3 3-4p) value flop leaves at pot · R(position, SPR, N) ·
  equity. R comes from blueprint self-play and is re-measured once after the re-solve. Against the
  real field, realisation differs: stations realise less, nits fold more.
- *Resolve:*
  1. Track how much R and the preflop strategy change between solve iterations 1 and 2.
  2. Measure R against the zoo archetypes, and decide whether type-specific R belongs in M4.
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
- *Resolve:* in M5, train W on simulated zoo tournaments, starting from ICM, and A/B it against ICM
  in the arena.
- *When:* M5.

**Q-D8. Is Waffle3z's hyper-aggression near-equilibrium for these blinds, or exploitable?**
- *Why:* it raises 90% of unopened pots and bets 97% when checked to. Pure push/fold Tuo finishes
  ahead of it 24/43 times, which suggests it is exploitable. This bears on what the anchor should do
  against a LAG even with η = 0.
- *Resolve:* in the arena, play the M2/M3 anchor against a Waffle3z-style archetype and report
  regime-level chip EV. Check whether a calibrated calling range punishes the LAG without any
  opponent model.
- *When:* M2-M3.

---

## E. Opponent modelling and exploitation

**Q-E1. Can live bots be fingerprinted within 10-20 hands, and is a library worth its characters?**
- *Why:* names are not in the input. In-sample identification is 66% after 10 decisions and 81%
  after 20. A library costs 3-8k chars, while a generic few-parameter adaptive model costs almost
  nothing.
- *Resolve:*
  1. Time-split evaluation (Q-C3) of library + online versus online-only, and of bet-size
     signatures as extra fingerprint features.
  2. An arena ablation of types versus population-only within M4.
- *When:* M4.

**Q-E2. What stack resolution do the jam and call-off contexts need?**
- *Why:* `oppmodel/oppmodel2.py` splits depth only at 15 BB, so it cannot tell call-off ranges at
  5 BB from those at 12 BB, which is where M4a exploitation pays.
- *Resolve:* add buckets (≤6, 6-12, 12-20, >20 BB) to the jam and call contexts. Measure log-loss
  on those decisions alone and the cost in characters.
- *When:* M4 (the counters are recorded from M1).

**Q-E3. How should model confidence combine across 2-3 opponents in a multiway pot?**
- *Why:* the step scales η, or gates switching, by a confidence c, but each opponent has its own
  model and confidence. The plan's default, c = min_j c_j over opponents still in the hand, is
  conservative.
- *Resolve:* ablate min_j against influence weighting (each opponent's share of the q̂ difference)
  and a joint-posterior check, in M4 multiway spots.
- *When:* M4.

**Q-E4. How should G_t be estimated, and what should ε₀ and κ be?**
- *Why:* the risk budget must spend *expected* gifts from opponent mistakes, not realised chips. The
  all-in-adjusted estimator removes runout luck only after all-ins. The anchor-vs-anchor baseline
  per seat and depth has to be tabulated. ε₀ = 0 is the strict, near-Nash default.
- *Resolve:*
  1. Measure the variance of G_t in the arena with and without AIVAT-style corrections on non-all-in
     streets.
  2. Tune κ (and optionally ε₀ > 0) by SPRT against the scripted and shape-shifter opponents.
- *When:* M4, M5.

**Q-E5. Rule A switching or soft anchors?**
- *Why:* near-deterministic anchors make the MMD step inert unless deviations are gated discretely
  (Rule A). A soft anchor, softmax(Q_anc/τ), instead makes the KL leash meaningful everywhere. The
  two trade off gain against risk differently.
- *Resolve:* implement both behind a flag, unit-test that each keeps a deterministic anchor
  deterministic when Δq = 0, and SPRT them.
- *When:* M4.

**Q-E6. Is rollout-based q̂ ever confident enough to trigger a deviation?**
- *Why:* the switching rule needs Δq̂ > z·SE. The research estimated 1,000-2,500 rule-policy
  rollouts per action per regime in ~28 ms, or 2.4-5k network rollouts per turn. Whether a few
  hundred rollouts per action give a small enough SE, given poker's outcome variance, was never
  measured.
- *Resolve:* in M4b, measure the actual SE of CRN rollouts with exact all-in and river leaves on
  logged real spots. Count how often Rule A or Rule B would deviate. Drop M4b if the answer is
  "almost never".
- *When:* M4b.

**Q-E7. Do opponents adapt within a game and punish exploitation?**
- *Why:* kovi has an `anti-maniac` rule. A bot that changes style mid-game makes the type posterior
  stale.
- *Resolve:*
  1. Run shape-shifter archetypes in the arena (one type for 20-40 decisions, then a switch).
  2. In reconstructed replays, test for within-game drift of each bot's action frequencies.
  3. In our own live games, compare opponents' behaviour before and after our deviations.
- *When:* M4.

**Q-E8. Which likelihoods should the η = 0 bot's range tracker use: the anchor's or the population's?**
- *Why:* a near-Nash postflop blueprint still needs opponent ranges. With anchor likelihoods (the
  Ataraxos assumption), a maniac's 50 BB open looks far too strong. Population likelihoods calibrate
  to the field without targeting anyone, but they move the bot off "self-play-consistent" play.
- *Resolve:* A/B both in M2 via SPRT, with per-archetype reports. Pick the default to match the
  near-Nash preference unless the population version wins clearly.
- *When:* M2.

---

## F. Evaluation

**Q-F1. What duplicate-seed variance ratio do the real bots achieve?**
- *Why:* 0.61 (HU) and 0.80 (4p) come from deterministic toy bots (`eval/dup.py`, `eval/dup4.py`).
  Bots that sample their actions decorrelate the duplicates unless their RNG is seeded per
  duplicate. All SPRT game-count estimates depend on this ratio.
- *Resolve:* in M0, re-measure with the baseline bot and the zoo, with observation-hash RNG seeding.
  Also measure the paired candidate-vs-baseline design.
- *When:* M0.

**Q-F2. Do arena gains against clones transfer to live play?**
- *Why:* clones only saw states the real bots reached, so they extrapolate on new lines. In-sample
  library fits leak into "held-out" clones.
- *Resolve:*
  1. Split by time between the library and the clones.
  2. Run the CGBenchmark-style real-opponent channel (Q-C1).
  3. Track the correlation between arena Δp and accumulated live Δp across submissions.
- *When:* M4 onward.

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
  the room for any M6 network (≈40-60k int8 parameters) depends on it.
- *Resolve:* minify the M0 skeleton and the M3 bot with crossfish `tools/cg_minify.py` and record
  characters per module.
- *When:* M0, M3.

**Q-G2. Is 8-bit quantisation enough, or is 6-bit Rice/GPTQ needed?**
- *Why:* this matters only for the M6 network (policy logits, range-update likelihoods) and for
  table precision. The equity table at 8 bits has steps of 0.0039, close to its MC SE of 0.0035.
- *Resolve:*
  1. Measure the policy KL between float and quantised weights (target <0.005) and the arena impact.
  2. Check that the 8-bit equity table changes no M1 jam/call decision compared with float.
- *When:* M0 (table), M6 (net).

**Q-G3. Can range updates with network likelihoods keep up at 4 players?**
- *Why:* about 3 ms per opponent action for a 100k net with the first-layer trick, and up to 6
  actions per turn at 4p, is tight. Table likelihoods (M2-M5) cost microseconds.
- *Resolve:* time it in M6 at CodinGame speed. Fall back to 169-class preflop updates or EHS buckets.
- *When:* M6.

---

## H. Training (M6, optional)

**Q-H1. How many samples does MMD-PPO need on 2-4p escalating-blind sit-and-gos with a 40-60k
parameter net?**
- *Why:* the 1e9-1e10 estimate extrapolates from AlphaHoldem and Ataraxos, not from a pilot.
- *Resolve:* run the HU pilot first, measured by exact exploitability in the jam/fold game with
  `solvers/pf.py`'s best response, or value ≥0 against Nash jam/fold plus local best response.
  Scale only if it converges.
- *When:* M6.

**Q-H2. Is MMD-PPO self-play stable and useful at 3-4 players, and which opponent pool should it
use?**
- *Why:* there is no convergence guarantee outside two-player zero-sum games. Self-play may cycle or
  settle into passive conventions. A pool of K-best checkpoints and clones may make the blueprint too
  exploitative or not exploitative enough against the real field.
- *Resolve:*
  1. Monitor against a fixed baseline pool.
  2. Ablate the pool composition (self-only, ≥50% self, clones).
  3. Ship only through the M6 SPRT gate.
- *When:* M6.

**Q-H3. Should advantage filtering be used at all?**
- *Why:* raw poker returns are dominated by card luck, so filtering would select noise.
- *Resolve:* enable it only after the privileged critic and all-in equity substitution are in place
  and the critic's explained variance exceeds 0.3. Ablate filter rates 0, 0.5 and 0.75.
- *When:* M6.
