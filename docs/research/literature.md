# Literature map for a CodinGame Poker bot

This document maps the poker and imperfect-information literature onto our setting: no-limit
hold'em, a 2-4 player sit-and-go with doubling blinds, TrueSkill placement, 50 ms per turn and a
100k-character source limit. For each line of work it says what transfers and what it would cost.
It starts from `research[2]` in [`raw/workflow_results.json`](raw/workflow_results.json), with the
research critique's corrections applied. Each correction is marked **(critique)**. The Ataraxos
paper has its own explainer, [`../ataraxos.md`](../ataraxos.md).

Provenance labels: **reproduced** (re-run in this repo by the named script), **research** (taken from
the research run), **literature** (stated in the cited paper).

---

## 1. What is special about this setting

- **Small hidden state.** Each opponent's hidden state is one of at most C(52,2) = 1,326 hole-card
  combos. The Ataraxos paper itself names hold'em's 1,326 hands as the case where public-belief
  methods are affordable (main text). Exact Bayesian range tracking is cheap here, so no
  learned belief model is needed.
- **Every card is revealed.** All hole cards, folded ones included, are shown after every hand
  ([rules.md §10](rules.md#10-what-each-player-learns)). Opponent-model likelihoods are exact and
  unbiased.
- **The payoff is placement.** TrueSkill rewards finishing order, not chips
  ([field.md §3](field.md#3-ranking-mechanics)).
  - Heads-up (HU), P(win) is linear in chips, so chip EV is right.
  - At 3-4 players it is not: ICM risk premia are large (§2).
- **Two regimes** ([rules.md §12](rules.md#12-game-length-and-where-games-are-decided)). Most
  *decisions* are deep: 51-64% are above 50 BB. Most *eliminations* are short: 69-79% happen at
  ≤20 BB.
- **Data per opponent is scarce.** Each opponent makes a median of 55-63 decisions per game, only
  14-23 of them postflop (research). Names are not in the input.

## 2. Short-stack play: push/fold and ICM

**Heads-up.**

- **Miltersen & Sørensen (AAMAS 2007)** (literature, as restated by Ganzfried-Sandholm 2008).
  - Setting: an HU tournament at ≤6.7 BB effective (8000 chips, 300/600 blinds).
  - Restricting yourself to jam/fold costs at most 1.4% of win probability.
  - The optimal strategy randomises in at most one hand per state, and there is no fixed hand
    ranking.
  - HU, single-hand and tournament strategies are almost identical.
- **Our HU jam/fold Nash, in chip EV** (reproduced, `solvers/pf.py`). It uses a 169×169 equity
  table built by Monte Carlo (`solvers/eq.c`, `eq169.bin`) with exact combo and card-removal
  weights, solved by fictitious play:

  | eff. stack | 3 BB | 5 BB | 7 BB | 10 BB | 12 BB | 15 BB | 20 BB | 25 BB |
  |---|---|---|---|---|---|---|---|---|
  | SB jams | 77.7% | 71.4% | 66.2% | 58.3% | 53.5% | 45.7% | 40.2% | 36.1% |
  | BB calls | 92.8% | 62.1% | 48.5% | 37.5% | 33.0% | 28.2% | 21.7% | 17.4% |
  | SB value (BB/hand) | +0.052 | +0.056 | +0.017 | −0.045 | −0.082 | −0.127 | −0.183 | −0.229 |

  **(critique)** The SB value is the value *inside the jam/fold game*. The research concluded that
  "pure jam/fold is adequate only up to ~8 BB", but that does not follow. What matters is the gap
  between jam/fold and the unrestricted HU game, which has not been computed. A negative jam/fold
  value is only a lower bound on the SB's full-game value. The ~8-10 BB boundary is a
  **hypothesis** to test with a full short-stack HU solve.

**Three or more players.**

- **Ganzfried & Sandholm (AAMAS 2008)** (literature).
  - Setting: 3-player jam/fold tournaments at 7.5 BB (13,500 chips, 300/600 blinds), 946 stack
    states.
  - Method: fictitious play inside value iteration, initialised from ICM. No player gains more
    than $0.0488 by deviating, which is under 0.1% of the maximum payoff.
  - ICM itself is off by $0.37 on average and up to $2.99 per $100 pool.
  - Tournament and single-hand frequencies differ widely. For example, BB jams after jam/jam with
    frequency 0.021 in the tournament vs 0.197 in a single hand.
  - Their IJCAI 2009 follow-up gives provably-equilibrium variants (ε = 0.5% of the entry fee).
  - Convergence was observed offline, over hours. Fictitious play has no guarantee at 3 or more
    players.
- **ICM (Malmuth-Harville)** (Harville 1973; Malmuth 1987) turns stacks into finishing-place
  probabilities. We use it with TrueSkill payouts. At equal ratings the payouts are (1,0),
  (1,.5,0) and (1,.6444,.3556,0) (reproduced, `solvers/trueskill_payouts.py`).
- **Equity needed to call off an all-in**, with no dead money (reproduced, `solvers/icm2.py`):

  | situation | needed |
  |---|---|
  | HU | 50.0% |
  | 3 equal stacks | 60.0% |
  | 4 equal stacks | 64.6% |
  | 4p (600, 1800, 1800, 600), big stack vs big stack | 73.7% |
  | same, short vs short | 55.7% |
  | 4p (2400, 1200, 800, 400), leader calls the short stack | 51.9% |
  | 3 left of 4 (1600 × 3) | 59.2% |

  Chip-EV calling ranges are far too loose at 3-4 players.
- **FGS** (future game simulation; HoldemResources/ICMIZER) adds simulation of the following hands
  to ICM, so that blinds, position and rotation count. A learned finishing-place value head does
  the same job.
  - **(critique)** Fit such a model on simulated tournaments, or with player-strength features.
    Fitting it on raw arena outcomes mixes skill with chips, because chip leaders are
    disproportionately the stronger bots.

**What this means for runtime** (critique):

- **Multiway is not exact.** "Exact jam or call-off as a sum over caller subsets using the equity
  table" holds only HU. The 169×169 table has no 3-4-way equities or side-pot outcomes, and ICM needs
  the joint bust distribution. In 3-4-player pots this is **Monte Carlo, not exact**.
- **A runtime 3-4p ICM equilibrium does not fit.** A 4-player jam/fold tree has about 14 decision
  nodes × 169 classes with multiway leaves. That costs about 0.24 s per iteration, or 12-48 s for
  50-200 iterations: about 1000× over budget. Instead, solve offline on a grid of (N, position,
  stacks, hands to the next level) and distil the result into a compact model of 2-4k characters.
- **Preflop-only solves need postflop leaf values.** Raw all-in equity at the flop overvalues
  out-of-position flats and limps. Use equity-realisation factors estimated from a postflop
  blueprint, then re-solve.

## 3. Deep heads-up methods and their cost

| system | method | offline cost | at play time | result |
|---|---|---|---|---|
| DeepStack (Science 2017) | Continual re-solving with value nets. Lookahead limited to fold/call/2-3 bets/all-in. | Turn net trained on 10M solved turn games, 6,144 CPU cores, >175 core-years | ~10^7 decision points re-solved in <5 s on a GTX 1080 | 486 mbb/g vs pros (AIVAT); LBR cannot exploit it |
| Libratus (Science 2018) | Blueprint plus nested safe subgame solving | see note | see note | 147 mbb/hand vs 4 pros over 120,000 hands |
| Modicum / depth-limited solving (NeurIPS 2018) | Depth-limited solving with multi-valued leaves | 700 core-hours | 4-core CPU, 16 GB, ~20 s per hand | Beats Baby Tartanian8 (2M core-hours, 18 TB RAM) and Slumbot (250k core-hours, 2 TB RAM) |
| ReBeL (NeurIPS 2020) | RL plus search over public belief states | data generation on up to 128 machines × 8 GPUs | see note | +45 ± 5 mbb/g vs Slumbot, +9 ± 4 vs Baby Tartanian8; poker code not released |
| Student of Games (Sci. Adv. 2023) | Growing-tree CFR plus learned values | TPU scale | see note | +7 ± 3 mbb/hand vs Slumbot over 3.1M hands; LBR finds no exploit |
| Pluribus (Science 2019) | MCCFR blueprint plus depth-limited search, 6-max | 12,400 core-hours (~$144), <512 GB | 2 CPUs, 1-33 s per subgame, ~20 s per hand | +48 mbb/g (SE 25) vs pros |
| AlphaHoldem (AAAI 2022) | End-to-end self-play RL (Trinal-Clip PPO, K-best historical pool) | 3 days on 8 GPUs + 64 CPU cores; 6.5B samples | 2.9 ms per decision on one GPU | +111.56 mbb/h vs Slumbot; +16.91 vs a DeepStack reimplementation |

All rows are literature, as summarised in `research[2]`. The research summary puts DeepStack,
Libratus, ReBeL, Student of Games and Pluribus together at 10^4-10^6 core-hours or GPU/TPU clusters
to train, and seconds per decision to play. It has no separate figures for the cells marked
"see note".

- **Tabular blueprints are far too big** for 100k characters.
- **Re-solving is too slow.** Every search-based system needs seconds per decision. Modicum, the
  cheapest principled one, is about 400× over our 50 ms.
- **End-to-end RL is the realistic route.** AlphaHoldem shows it works in HU no-limit, with fast
  inference. That is also the Ataraxos route, with a much smaller network here (see
  [engineering.md](engineering.md)).

## 4. Multiplayer caveats

- Pluribus states that its algorithms are not guaranteed to converge to a Nash equilibrium outside
  two-player zero-sum games.
- In 3-player Kuhn poker there is a parameterised family of equilibria in which one player can
  transfer utility to a second player at the third's expense (Szafron, Gibson & Sturtevant,
  AAMAS 2013). This "kingmaker" effect has no 2-player analogue.
- So at 3-4 players **equilibrium play is not "safe"**. MMD's convergence guarantee holds only in
  two-player zero-sum games, which here means the HU phase, where U ≈ stack/4800. At 3-4 players,
  CFR+, fictitious play and MMD-PPO are heuristics. Rank is won through ICM-aware survival and
  through exploitation.
- About 95-99% of 3-4p games reach an HU phase, at a median effective stack of 14-18 BB, so the HU
  phase matters in almost every game (critique; [`../plan.md`](../plan.md) §2 fact 4).

## 5. MMD and update equivalence

**Magnetic mirror descent** (Sokota et al., ICLR 2023):

- Closed-form step: π_{t+1} ∝ [π_t · ρ^{αη} · e^{η q_t}]^{1/(1+αη)}, where ρ is the "magnet"
  policy, α the magnet strength and η the step size.
- **Benchmarks:** Kuhn, 2×2 Abrupt Dark Hex, 4-sided Liar's Dice and Leduc. MMD converges
  exponentially fast to a quantal response equilibrium (QRE).
- **With annealing** of the temperature, or a moving magnet, MMD is "competitive with (or better
  than) CFR"; CFR+ is still best.
- **Leduc exploitability 0.08:** F-FoReL needs about 200,000 iterations, MMD under 1,000.
- **Deep RL experiments** were on Dark Hex and Phantom Tic-Tac-Toe, not poker.

**Update equivalence** (Sokota, Farina, Wu, Hu, Wang, Kolter & Brown, ICLR 2024):

- Decision-time search that performs one step of a learning algorithm at the current decision.
- The MMD version is U(π, q) ∝ [π · e^{ηq} · ρ^{ηα}]^{1/(1+αη)}.
- Tabular analogues converge empirically to AQRE and, with annealing, toward Nash on Kuhn and
  Leduc. There is **no formal two-player zero-sum safety proof and no hold'em experiment**.
- Hanabi: 24.62 ± 0.02 with two orders of magnitude less search. η = 50, α = 0.01 in Dark Hex and
  Phantom Tic-Tac-Toe.
- In the form (log π + ηq + ηα log ρ)/(1 + ηα), Ataraxos's Stratego constants are η = 50 and
  ηα = 0.1 (see [`../ataraxos.md`](../ataraxos.md)).

**Policy gradient as a baseline** (Rudolph et al., arXiv 2502.08938). Over 7,000 runs and 345k
CPU-hours, NFSP, PSRO, ESCHER and R-NaD did not beat generic policy-gradient methods
(MMD/PPO/PPG). The games went up to 27M infostates; none was poker.

**Our one-step MMD demo: the step as an exploitation operator** (reproduced,
`solvers/mmdstep2.py`). The setting is HU 10 BB jam/fold:

- the anchor π₀ is Nash smoothed with ε = 0.02;
- α = 0.02 and ρ is uniform;
- q is exact under the opponent model.

| opponent | η | gain over Nash (BB/hand) | worst-case value (Nash: −0.045) |
|---|---|---|---|
| nit (calls top 15%) | 3 | +0.042 | −0.067 |
| nit | 10 | +0.252 (91% of the best response) | −0.328 |
| nit | ∞ | +0.278 (the best response) | −0.378 |
| calls top 70% | 10 | +0.062 (86% of the best response) | −0.060 |

- **η is a safety dial.** A small η gives a small gain and a small worst-case loss.
- **The magnet is immaterial only at small ηα.** Runs at α = 0 and α = 0.02 are almost identical
  (`solvers/mmd_ab.py`). In this demo the ε = 0.02 smoothing alone costs 0.006-0.022 BB/hand at
  η = 0.
- **Exploitation is worth far more than equilibrium edges.** The best response to a nit gains
  +0.278 BB/hand at 10 BB and +0.198 at 15 BB (reproduced, `solvers/exploit.py`).

**Safety bound** (research; verified by derivation in the critique). With α = 0 the step is the
maximiser of ⟨π, q̂⟩ − KL(π‖π_θ)/η. So:

- KL(π_s‖π_θ) ≤ η·(max_a q̂ − ⟨π_θ, q̂⟩) ≤ η·R̂, where R̂ is the range of q̂.
- By Pinsker, for any true action-value vector q* with range R*:
  ⟨π_s − π_θ, q*⟩ ≥ −(R*/2)·√(2η·R̂).

This bound is local, per decision.

**(critique) Two ways the step goes wrong in prize units.**

1. **A uniform magnet randomises a deterministic anchor.**
   - With ρ uniform, log ρ is the same for every action and cancels. The step becomes
     softmax((log π₀ + ηq̂)/(1 + ηα)): the anchor is flattened by a temperature of 1 + ηα.
   - The demo above is safe because it works in BB units, where η ≤ 10 and ηα ≤ 0.2.
   - In prize units Δq̂ is about 0.001-0.05, so η must be about 100-5000, and then ηα = 2-100.
   - Example: an ε = 0.005 anchor (log-odds 5.29), η = 300, Δq̂ = 0.005 in the anchor's favour,
     α = 0.02. The bot plays the *non-anchor* action 27% of the time.
   - **Fix:** set ρ = π₀, or keep ηα ≤ 0.1. With ρ = π₀ the step is π₀·exp(ηq̂/(1+ηα)), a pure
     KL-leashed step, and the same inputs keep the anchor at 99.6%.
2. **A noise-capped η makes the step inert.**
   - Flipping a near-deterministic anchor needs η·Δq̂ > ln(0.995/0.005) ≈ 5.3.
   - The research plan's cap η ≤ 0.3/SE(Δq̂) then demands Δq̂/SE of about 10-18, which a few hundred
     rollouts per action will almost never reach.
   - So exploitation would fire only where q̂ is exact: jam, call-off and the river.
   - **Fix:** either say that this is the intent and drop rollouts, or use soft anchors
     (softmax of blueprint EVs) or a switching test.

## 6. Opponent modelling and safe exploitation

| method | idea | source |
|---|---|---|
| Restricted Nash Response | Assume the opponent plays the model with probability p, and best-respond robustly | Johanson, Zinkevich & Bowling, NIPS 2007 |
| Data-Biased Response | Per-infoset confidence from observation counts, which fixes RNR's overfitting | Johanson & Bowling, AISTATS 2009 |
| Safe opponent exploitation | Risk at most what has been won above the equilibrium value | Ganzfried & Sandholm, ACM TEAC 3(2), 2015 |
| Bayes' Bluff | Posterior over opponent strategies, with Dirichlet or informed priors | Southey et al., UAI 2005 |
| Bayesian opponent exploitation | Exact best response to a Dirichlet posterior | Ganzfried & Sun, arXiv 1603.03491 |
| Online implicit agent modelling | A portfolio of strategies plus a bandit; would have won the 2011 ACPC HULHE exploitation event | Bard et al., AAMAS 2013 |
| Showdown censoring | Folds hide cards, so showdown data are missing not at random and passive per-card estimators are inconsistent | Guo, arXiv 2608.09954 (2026) |
| Certified restricted responses | Budgeted, confidence-scheduled exploitation; in Leduc, 6.2× the gain of a binary gate within the same budget | Li & Huang, arXiv 2607.28520 (2026) |

What transfers:

- **No censoring bias.** CodinGame reveals folded cards, so every opponent decision is a labelled
  (hand, context, action) triple. Guo's bias does not arise.
- **Strong priors are essential**, given 55-63 decisions per opponent per game. Use a population
  model, or a library of leaderboard bots with a posterior over which one we face. Open-size
  signatures help fingerprint bots ([field.md §8](field.md#8-implications)).
- **The library model measures well** (reproduced, `oppmodel/oppmodel2.py`, leave-one-game-out on
  the 371 replays). Next-action log-loss, in nats per decision:

  | decisions | population | online only | library types + online correction | static "oracle" table |
  |---|---|---|---|---|
  | 1-10 | 0.972 | 0.916 | 0.718 | 0.579 |
  | 11-25 | 0.978 | 0.835 | 0.586 | 0.598 |
  | 26-50 | 1.000 | 0.831 | 0.648 | 0.671 |
  | 51+ | 0.976 | 0.815 | 0.674 | 0.706 |
  | overall | 0.983 | 0.839 | 0.653 | 0.652 |

  Top-1 identification among 17 library bots after 5 / 10 / 20 / 40 decisions: 43 / 66 / 81 / 90%.

  **(critique) The "oracle" is not an upper bound.** It is a static per-bot Dirichlet table. It
  beats the library model over the first 10 decisions and loses after that. The equal overall
  averages (0.652 vs 0.653) come from two opposite errors cancelling:
  - identification costs about 0.14 nats over the first 10 decisions;
  - after that, the coarse context set limits accuracy, not identification.

  The library and the targets also come from the same replay pool, so live fingerprinting will be
  harder than this suggests. Split by time to measure it honestly.
- **Missing contexts** (critique). `oppmodel2.py` splits depth only at 15 BB. It cannot tell a
  5 BB call-off range from a 12 BB one, which is where exploitation pays most. Add depth buckets
  for jam and call-off, for example ≤6, 6-12, 12-20 and >20 BB.
- **Budget from expected gain, not realised gain** (critique). "Risk only what you have won" must
  use expected gifts from opponent mistakes: all-in equity substitution or AIVAT-style correction
  with the revealed cards. Never use raw chip outcomes, which card luck dominates.
- **Plugging the model into the MMD step** (research). Use σ_j = λ_j·π̂_j + (1 − λ_j)·π_θ with
  DBR-style confidence λ_j = n_j/(n_j + n_0). Use the same σ_j in the belief and in the rollouts.
  Keep the anchor as the KL target. η → 0 then recovers the anchor, and η → ∞ gives the best
  response to the model.
- **Multiway confidence is unspecified** (critique). The rule for combining confidence across 2-3
  opponents with different models is not yet defined.

## 7. Ataraxos components, sorted for poker

See [`../ataraxos.md`](../ataraxos.md) for the paper itself.

| component | verdict for CodinGame Poker |
|---|---|
| One-step MMD / update-equivalence step | **Use**, as a leashed exploitation operator around a sound anchor, with ρ = anchor (§5) |
| KL-to-policy term in search | **Use.** Its ablation is the paper's key lesson: without it, search overfits. In poker, what it overfits to is opponent-model error. |
| Learned belief network | **Replace** with exact Bayes over 1,326 combos per opponent. Multiway: a joint belief by rejection sampling on card removal. |
| MMD-in-PPO self-play with annealed magnet and power-law schedules | **Optional, later**, for deep 3-4p play. Guarantees hold only in two-player zero-sum. |
| Categorical value head | **Adapt** into a distribution over finishing places (a learned FGS) |
| Advantage filtering | **Only after variance reduction** (privileged critic, all-in EV substitution); on raw returns it selects card luck |
| Depth-40 rollouts; multi-million-parameter nets; set-up network | **Skip.** Hands end within 4 streets, and the nets cannot fit in 100k characters. |
| Continuous opponent bet sizes | Feed them to the net as inputs, so no action translation is needed. Only our own outputs are discretised. |

## References

- Bard, N., Johanson, M., Burch, N. & Bowling, M. Online implicit agent modelling. AAMAS 2013.
- Brown, N. & Sandholm, T. Superhuman AI for heads-up no-limit poker: Libratus beats top professionals. *Science* 359, 418-424 (2018).
- Brown, N. & Sandholm, T. Superhuman AI for multiplayer poker. *Science* 365, 885-890 (2019).
- Brown, N., Sandholm, T. & Amos, B. Depth-limited solving for imperfect-information games. NeurIPS 2018.
- Brown, N., Bakhtin, A., Lerer, A. & Gong, Q. Combining deep reinforcement learning and search for imperfect-information games (ReBeL). NeurIPS 2020.
- Ganzfried, S. & Sandholm, T. Computing an approximate jam/fold equilibrium for 3-player no-limit Texas hold'em tournaments. AAMAS 2008.
- Ganzfried, S. & Sandholm, T. Computing equilibria in multiplayer stochastic games of imperfect information. IJCAI 2009.
- Ganzfried, S. & Sandholm, T. Safe opponent exploitation. *ACM Trans. Econ. Comput.* 3(2) (2015).
- Ganzfried, S. & Sun, Q. Bayesian opponent exploitation in imperfect-information games. arXiv:1603.03491.
- Guo, J. Safe observation capacity for opponent exploitation under showdown censoring. arXiv:2608.09954 (2026).
- Harville, D. A. Assigning probabilities to the outcomes of multi-entry competitions. *JASA* 68 (1973). Malmuth, M. *Gambling Theory and Other Topics* (1987). <https://en.wikipedia.org/wiki/Independent_Chip_Model>
- ICMIZER. How the FGS (future game simulation) calculator works. <https://www.icmizer.com/en/blog/how-fgs-future-game-simulation-calculator-works/>
- Johanson, M., Zinkevich, M. & Bowling, M. Computing robust counter-strategies. NIPS 2007.
- Johanson, M. & Bowling, M. Data biased robust counter strategies. AISTATS 2009.
- Li, B. & Huang, L. Agents that certify their own exploits: confidence-scheduled restricted responses for safe opponent exploitation. arXiv:2607.28520 (2026).
- Miltersen, P. B. & Sørensen, T. B. A near-optimal strategy for a heads-up no-limit Texas hold'em poker tournament. AAMAS 2007.
- Moravčík, M. et al. DeepStack: expert-level artificial intelligence in heads-up no-limit poker. *Science* 356, 508-513 (2017).
- Rudolph et al. (with Farina, Vinitsky and Sokota). Reevaluating policy gradient methods for imperfect-information games. arXiv:2502.08938.
- Schmid, M. et al. Student of Games: a unified learning algorithm for both perfect and imperfect information games. *Sci. Adv.* (2023).
- Sokota, S. et al. A unified approach to reinforcement learning, quantal response equilibria, and two-player zero-sum games (MMD). ICLR 2023.
- Sokota, S., Farina, G., Wu, D. J., Hu, H., Wang, K. A., Kolter, J. Z. & Brown, N. The update-equivalence framework for decision-time planning. ICLR 2024.
- Sokota, S., Vinitsky, E., Hu, H., Fan, Z., Kolter, J. Z. & Farina, G. Scalable decision-making for games of imperfect information (Ataraxos). *Nature* 658, 55-59 (2026). doi:10.1038/s41586-026-11036-y
- Southey, F. et al. Bayes' bluff: opponent modelling in poker. UAI 2005.
- Szafron, D., Gibson, R. & Sturtevant, N. A parameterized family of equilibrium profiles for three-player Kuhn poker. AAMAS 2013.
- Zhao, E., Yan, R., Li, J., Li, K. & Xing, J. AlphaHoldem: high-performance artificial intelligence for heads-up no-limit poker via end-to-end reinforcement learning. AAAI 2022.
