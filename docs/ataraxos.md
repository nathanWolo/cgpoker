# Ataraxos explained, and what it means for CodinGame Poker

**Paper.** Sokota, S., Vinitsky, E., Hu, H., Fan, Z., Kolter, J. Z. & Farina, G. *Scalable
decision-making for games of imperfect information.* Nature 658, 55-59 (2026).
[doi:10.1038/s41586-026-11036-y](https://doi.org/10.1038/s41586-026-11036-y). It is open access
under CC BY 4.0. **Code:** <https://github.com/AtaraxosAI/stratego> (MIT); this document cites commit
`92db29e`.

Citations below use these forms:

- "§Methods/…" is a section of the paper;
- "SI §S3.4" or "Table S7" is the Supplementary Information;
- "ED Table 1" is Extended Data;
- `file.py:N` is a path in the code repository.

This is a summary, not a reproduction. Read the paper for the details.

---

## 1. The problem

- **Why hidden information is hard.** In an imperfect-information game, the value of a decision
  depends on the policies players used before it, and would have used. Those policies shape the
  posterior over hidden information.
- **What the strongest methods do, and where they stop.** Libratus, DeepStack and ReBeL transform
  the game around public information. Their cost grows with the amount of hidden information, so
  they work only when it is small. The paper's own example is Texas hold'em, with its 1,326
  possible hands (§Main).
- **Stratego is the opposite extreme.** It has more than 10^33 possible piece configurations.
  DeepMind's DeepNash, a multi-year effort, had not reached top-human level.
- **Ataraxos is general.** It is a recipe for self-play RL plus test-time search under large hidden
  information. The same recipe was applied to Stratego, Barrage Stratego, Hanabi and dou dizhu.

## 2. The design pattern

Ataraxos combines two kinds of network, a training algorithm and a search procedure (§Overview):

1. A **policy-value network**, trained by self-play. Stratego has two: a *set-up network* that
   places the 40 pieces, and a *move network*. The two self-play processes are coupled through the
   games' outcomes.
2. **Dynamically damped self-play**, the RL algorithm (§3 below).
3. A **belief network**, trained afterwards on the final policy's self-play games, which samples
   hidden information (§5).
4. A **search** that performs one extra damped update at the current decision (§6).

At test time, set-ups are sampled straight from the set-up network. Moves use search.

## 3. Dynamically damped self-play (MMD inside PPO)

**The key idea** (§Methods/Dynamically damped self-play): coordinate regularisation strength with
update size.

- **Early in training**, regularisation is strong and updates are large.
- **Late in training**, regularisation is weak and updates are small.

Keeping update size commensurate with regularisation damps the cyclic or chaotic dynamics that
imperfect information causes. Annealing both lets training improve fast early and keep improving
late. The authors liken the regulariser to an **energy reserve**: annealing it too fast gives quick
early gains but collapses entropy, after which the policy stops learning and is often easy to
exploit.

This is magnetic mirror descent (MMD; Sokota et al., ICLR 2023) run as a deep RL loss.

**Move-network loss** (SI §S3.4, eq. 6):

L_π = −min(r·δ, clip(r, 0.8, 1.2)·δ) + 0.1·KL(π_θ ‖ π_θt) + α_t·KL(π_θ ‖ ρ)

| symbol | meaning |
|---|---|
| r | π_θ(m\|x) / π_θt(m\|x), the probability ratio of the played move under the current and the data-collecting parameters |
| δ | λ-return advantage with λ = 0.5 |
| 0.1·KL(π_θ ‖ π_θt) | reverse KL to the data-collecting policy. With PPO clipping, gradient-norm clipping (0.267) and the Adam learning rate, it controls **update size**. |
| α_t·KL(π_θ ‖ ρ) | reverse KL to a **magnet** ρ. In the paper, ρ picks a movable piece uniformly, then a legal move for it uniformly. It controls **regularisation**. |

The value head is a categorical win/draw/loss output, trained by cross-entropy against λ = 0.8
returns (eq. 5). In code, the loss is `pyengine/core/rl.py:549-579`.

**Schedules** (Table S7; `rl.py:756-760` `power_schedule`):

- **Magnet coefficient:** α_t = 0.05 / t^0.3, where t is the training iteration.
- **Learning rate:** clip(0.5 / t^1.1, 5×10⁻⁶, 1×10⁻⁴).

By our arithmetic:

- the learning rate stays at 10⁻⁴ for about 2,300 iterations, then decays to its floor by about
  35,000;
- if each of the 202 batches per iteration is one gradient step, the move network's 8.56M
  gradient steps make about 42,000 iterations;
- so α_t ends near 0.002. That is the magnet coefficient the search uses (§6).

**Other training details:**

| detail | value |
|---|---|
| Epochs per iteration | 1 |
| Batch | 1,536 positions per GPU per simulator step, before filtering |
| Weight averaging | EMA of the parameters, decay 0.999, used for evaluation (ED Fig. 6: similar or better mean performance, lower variance across seeds) |
| Set-up network | The same PPO-clip + 0.1·KL loss, but regularised by an entropy bonus 0.1/t^0.3 instead of a magnet KL; Monte Carlo returns, no filtering, 5 epochs, and a learned conditional-entropy head (SI §S3.3) |

**Code note.** The released config defaults to `uniform_magnet = True` (`rl.py:105`), a magnet that is
uniform over legal moves. The piece-then-move magnet the paper describes is the non-default branch
(`rl.py:567-570`).

## 4. Advantage filtering

The move network trains only on moves whose |advantage| is both:

- at or above the **0.75 quantile** of the iteration's advantages, and
- at least **0.01** (SI §S3.4; `pyengine/core/buffer.py:240-241`).

Effects:

- About three quarters of the data is dropped.
- Wall-clock time per iteration falls about 2.5×.
- Sample efficiency *and* asymptotic strength both rose. The authors call this a phenomenon that
  merits further investigation (§Methods/Self-play training data generation).
- Removing filtering raised move entropy and cut sample efficiency (ED Fig. 5).
- Barrage used the 0.5 quantile.

## 5. Belief network

- **What it predicts.** The types of the opponent's hidden pieces, given everything the player can
  see. Ground truth comes free in self-play (§Methods/Belief modelling).
- **Architecture.** A transformer encoder over the board, plus a decoder that predicts the hidden
  pieces autoregressively, in row-major order (ED Fig. 4). 57.1M parameters (Table S11).
- **Training.** Teacher forcing on every position of the final policy's self-play games. Dropout
  0.2 helps it generalise to opponents unlike itself, such as humans.
- **Use.** At search time it is a generative model, sampling full hidden states in proportion to
  their likelihood.
- **Ablation.** Replacing it with the belief implied by a uniformly random policy is the last row
  of ED Table 1.

## 6. Test-time search by update equivalence

Search performs one additional damped self-play update step, applied only to the current decision
(§Methods/Test-time search; the update-equivalence framework, Sokota et al., ICLR 2024).

**Steps** (SI §S3.7):

1. Sample about 1,000 / (number of legal moves) hidden states from the belief network. The sampled
   states are shared across candidate moves (`search.py:129`, `357-366`).
2. Run 1,000 rollouts of depth 40, split evenly across legal moves (`search.py:208`). Each rollout
   forces its first move, then lets the move network play **both** sides.
3. q̂(a) is the average of the move network's value predictions at the positions the rollouts reach
   (`search.py:180-279`, which mixes them with TD(λ)).
4. Take one closed-form MMD step, then **sample** the move from the result (`search.py:178`):

π_search = argmax_π ⟨q̂, π⟩ − α·KL(π ‖ ρ) − β·KL(π ‖ π_θ) ∝ [e^q̂ · ρ^α · π_θ^β]^{1/(α+β)}

**Constants.** Stratego uses α = 0.002 (magnet) and β = 0.02 (move network) (Table S12). The code
parameterises the same step by a step size η and a temperature τ (`search.py:431-454`):

logits = (log π_θ + η·q̂ + ητ·log ρ) / (1 + ητ)

The Stratego constants are **η = 1/β = 50 and ητ = α/β = 0.1**, with q̂ in win-probability units.

**Why non-exploitative.** The belief approximates the *self-play* posterior, the rollouts use the
self-play policy, and the value net predicts self-play values. So q̂ approximates self-play action
values whoever the opponent is. Ataraxos does not adapt to its opponent; Pim Niemeijer was told
so.

**Why it can be aggressive.** The test-time step can safely be larger than training steps, for two
reasons: it is tabular, so it cannot interfere with other positions, and it uses better advantage
estimates.

**The KL-to-policy term is essential** (ED Table 1 caption):

- Regularisation strength has a large effect on performance.
- Removing the reverse KL to the move network drops search *below the raw move network*, because
  the search "overfits to idiosyncrasies of the move network" that do not generalise to other
  opponents.
- Deeper searches with more rollouts are stronger and slower (ED Fig. 2d). The evaluation setting,
  40 plies × 1,000 rollouts, averaged about 1.26 s per move on one H100.

**Search settings in the other games:**

| game | rollouts | depth | step size η | magnet (ητ) | notes |
|---|---|---|---|---|---|
| Stratego | 1,000 | 40 | 50 | 0.1 | Table S12 |
| Barrage Stratego | 1,024 | 10 | 10 | 0.01 | ≥10 samples per legal action (Table S21) |
| Hanabi | 10,000 | to game end | 30 | — | The same sampled hands for every action; rollouts at temperature 0 (Table S26) |
| Dou dizhu | 200 | to game end | 5 | — | Search at each step of a decomposed action, with ⌈200/\|A\|⌉ hands per option (SI §S7.4). Fig. S8 sweeps η from 0.32 to 10. |

**Known limit** (§Opportunities). One update step bounds how much search can improve the policy.
More compute cannot keep improving it. The authors point to knowledge-limited subgame solving as a
route past this.

## 7. Results

| game | result |
|---|---|
| Stratego vs Pim Niemeijer (4× world champion) | **15 W, 1 L, 4 D** over 20 games (85% effective win rate). One-sided binomial p < 2.6×10⁻⁴ under an i.i.d. assumption the authors flag as false. |
| Stratego, 2025 World Championship demo | 38 W, 2 L, 0 D (95%) |
| Stratego Evaluator bots | 97-99 wins per 100 against each of 5 bots |
| Barrage Stratego | Won all four 50-game series against three top-4 players. Policy network alone: 29-17-4, 36-12-2, 26-21-3. Search vs world #1: 31-14-5. Aggregate p < 1.3×10⁻⁵. |
| Hanabi, all players searching | 2p 24.654 ± 0.007 (77.5% perfect games); 3p 24.863; 4p 24.852; 5p 24.410. A new state of the art for every size; 2p used two orders of magnitude less compute than the previous best. With no search: 24.454 / 24.612 / 24.466 / 23.485. |
| Dou dizhu (role-averaged, 10,000 duplicated deals) | +0.199 ± 0.015 vs PerfectDou, +0.350 ± 0.016 vs DouZero, +0.107 ± 0.012 vs its own policy network |

**Cost:**

- **Training.** The Stratego RL run used 16 H100 for 1 week; the belief network 4 H100 for 4 days.
  Under US$8,000 at 2025 prices.
- **Scale.** 163M finished games, 208B environment steps, and 8.56M move-network gradient steps.
- **Network sizes.** Move network 14.7M parameters, set-up network 12.6M, belief network 57.1M.
- **Compared with DeepNash:** about 1/500 of the compute cost (DeepNash is estimated at
  US$3-4.5M), 1/30 of the self-play games and 1/100 of the training examples.
- **Other games** (SI): Hanabi policy 1 L40 × 2 days (5.9M parameters); dou dizhu policy 4 H100 ×
  2 days (1.9M); Barrage policy 4 H100 × 12 days.
- **Ablations** (ED Fig. 5). Removing learning-rate annealing, advantage filtering or
  regularisation annealing each materially hurt training.

## 8. What transfers to CodinGame Poker

The setting is different in four ways that matter:

- hidden information is tiny: 1,326 combos per opponent;
- every hand's cards are revealed afterwards;
- about 75% of games have 3-4 players (1,866 of the 2,495 recent battles of the top 37 bots;
  [`research/field.md`](research/field.md) §2), and every game is scored on placement;
- the budget is 50 ms on a CPU and 100k characters of source.

So poker needs about half of Ataraxos. The plan ([`plan.md`](plan.md)) is equilibrium-first, so
the half it uses keeps the paper's non-exploitative role. Details and numbers: [`research/literature.md`](research/literature.md) §5-7 and
[`research/engineering.md`](research/engineering.md) §6-7.

### 8.1 Belief network → exact Bayes over 1,326 combos

The autoregressive belief transformer is unnecessary. For opponent j with hidden combo y:

b_j(y | history) ∝ 1[y ∩ (our cards ∪ board) = ∅] · Π_τ σ_j(a_τ | y, state_τ)

- σ_j is the policy we assume j plays. The plan uses our own strategy, as Ataraxos does
  ("self-play-consistent" beliefs). Plugging in a fitted opponent model instead would turn the
  same machinery into exploitation; that variant is archived
  ([`../archive/plan_v1.md`](../archive/plan_v1.md) §6).
- Add an ε floor so deterministic rule bots never produce a zero likelihood.
- **Multiway**, the joint belief is Π_j b_j(y_j) restricted to disjoint hands. Sample it by
  rejection from the product of marginals.
- **Cost.** The update is incremental. With table likelihoods it costs under 0.1 ms per observed
  action; with a 34k-100k-parameter net, about 1.2-5.5 ms.
- **Revealed cards are not needed.** Every hand reveals all hole cards, which would make opponent
  modelling easy, but self-consistent beliefs do not use them.

### 8.2 Search: one damped self-play step, as in the paper

The closed-form step carries over unchanged, with poker's cheaper ingredients:

| part | Ataraxos | poker |
|---|---|---|
| hidden-state samples | belief network | exact Bayes ranges under our own strategy (§8.1) |
| q̂ | depth-40 rollouts of the move network for both players, value-net leaves | rollouts of our policy for every seat to the end of the hand; exact equity at all-in and river leaves |
| step | π_s ∝ [exp(q̂)·ρ^α·π_θ^β]^(1/(α+β)) | the same, toward our policy π_θ |

- It is **non-exploitative by construction**: beliefs and rollouts both assume opponents play our
  strategy, so the step improves our play against an equilibrium-like opponent rather than against
  anyone in particular.
- **The paper's KL-to-policy ablation carries over.** Without the β term the search overfits to its
  own rollout policy's quirks and falls below the raw network.
- **Units.** The constants depend on the units of q̂. Stratego's α = 0.002, β = 0.02 (η = 50,
  ηα = 0.1) are in win-probability units. In tournament-payout units Δq is often 0.001-0.05, so η
  needs rescaling, or q̂ should be divided by the per-hand payout-per-BB slope first. With a uniform
  magnet, a large ηα acts as a temperature on the policy and randomises near-deterministic
  decisions (research critique); keep ηα ≤ 0.1, as in the paper.
- Where it applies: the plan's optional M4, once a policy network exists. Before that, the bot
  refines only the river, by exact CFR re-solving over the same self-consistent ranges (plan M2).

The research run also studied this step as a leashed exploitation operator, with q̂ computed
against fitted opponent models. That direction is archived (`archive/plan_v1.md` §6;
`solvers/mmdstep2.py` is the demo).

### 8.3 MMD-PPO self-play: the route to deep-stack equilibrium play

Most placements are decided at short stacks, where push/fold and ICM solutions need no learning.
Self-play RL is the plan's later, optional milestone ([`plan.md`](plan.md) M4) for the parts that
tables and rules cover worst: deep heads-up and deep 3-4-player postflop play. It is kept only if it
lowers measured exploitability and wins a local SPRT. It would look like this:

- **Environment.** A C++ clone of the referee, playing whole tournaments with 2-4 seats and
  doubling blinds.
- **Reward.** The TrueSkill placement payout. A categorical value head becomes a distribution over
  finishing places, a learned FGS/ICM.
- **Loss.** PPO-clip + 0.1·KL to the old policy + α_t·KL to a magnet + value cross-entropy, with
  power-law schedules for α_t and the learning rate.
- **Details to copy:** one epoch per batch, a 0.999 EMA for evaluation, and λ-returns (0.5 for the
  advantage, 0.8 for the value) (critique).
- **Advantage filtering only after variance reduction:** a privileged critic that sees all hole
  cards, plus all-in equity substituted for dealt run-outs. On raw returns, card luck dominates and
  the filter would select noise.
- **Network.** About 40-60k int8 parameters fit the source budget, not millions.
- **Samples.** Probably 10^9-10^10 decisions. On CPUs that takes days to weeks; on one GPU with a
  vectorised environment, about a day per 10^10.

### 8.4 Guarantees hold only in two-player zero-sum

- MMD's convergence to QRE and Nash, and the improvement property the search inherits, are
  two-player zero-sum results.
- Even there, the evidence that the update-equivalence search converges is empirical, from tabular
  analogues on Kuhn and Leduc. There is no formal safety proof and no hold'em experiment
  (research).
- In CodinGame Poker that covers **only the heads-up phase**, where U ≈ stack/4800. Almost every
  3-4-player game reaches it, at a median of 14-18 BB.
- With 3-4 players there is no safety guarantee. One player can shift utility between the others
  (Szafron et al., 3-player Kuhn), and Pluribus disclaims convergence outside two-player
  zero-sum. Treat self-play there as a route to self-play-consistent play, not to a guaranteed
  equilibrium.

### 8.5 What to skip

| skip | reason |
|---|---|
| The set-up network | No analogue in poker |
| Depth-40 rollouts | A hand ends within 4 streets, and all-in leaves are exact |
| Multi-million-parameter transformers | Do not fit in 100k characters |
| The CUDA simulator | A CPU C++ engine validated against `sim/poker_sim.py` is enough at our network size |
