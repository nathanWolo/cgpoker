# solvers/

This directory holds offline game-theory checks for the short-stack game: the heads-up (HU) preflop equity table, HU jam/fold Nash, exploitation size, a one-step MMD demo, and ICM with TrueSkill payouts. Scripts run from any working directory and need numpy. `trueskill_payouts.py` also needs `trueskill`. The Python scripts all load `eq169.bin` from this directory. `exploit.py`, `mmdstep2.py` and `mmd_ab.py` exec the part of `pf.py` above its main `for S in` loop, so `pf.py` must not contain that string anywhere earlier.

Every number below was observed in this container.

`python3 solvers/check.py` (or `make solvers-check` from the repo root) runs every script below plus a 200-trial `eq.c` smoke run and checks the key output lines against the numbers in this file, in about 20 s; it exits non-zero on any mismatch. `check.py --full` (`make eq169-check`) also rebuilds `eq169.bin` at 20,000 trials per pair (about 50 s) and compares it byte for byte with the committed file. It compiles `eq.c` into `build/solvers/` (gitignored).

## eq.c → eq169.bin

`eq.c` computes a 169×169 matrix of float64 values, `E[i][j]` = P(class i beats class j), with ties counted as ½. It uses Monte Carlo sampling (xorshift with a fixed seed), a uniform combo for class i, and a non-conflicting combo for j.

Class index `r1*13+r2`, with ranks 0-12 = 2..A:

- pair: `r1 == r2`
- suited: `r1 > r2`
- offsuit: `r1 < r2`

```
mkdir -p build && gcc -O2 -o build/eq solvers/eq.c     # from the repo root
build/eq 20000 solvers/eq169.bin    # 47 s single-threaded (gcc 13.3 -O2); byte-identical to the committed eq169.bin
build/eq 200 build/eq_quick.bin    # 0.5 s smoke test
```

The arguments are `[trials_per_pair=20000] [out=eq169.bin]`. Sanity lines at 20k trials: AA vs KK 0.821, AKo vs 22 0.473, AA vs 72o 0.880, QJs vs 22 0.522. The "ref" values the program prints next to these are its own rough hard-coded references.

## Scripts

| script | runtime | observed output |
|---|---|---|
| `pf.py` | 12 s | HU jam/fold Nash in chip EV. Fictitious play runs 3000 iterations over 169 classes, weighted by exact combo and card-removal counts. SB jam % / BB call % / SB value (BB/hand): 3 BB 77.7/92.8/+0.052; 5 BB 71.4/62.1/+0.056; 7 BB 66.2/48.5/+0.017; **10 BB 58.3/37.5/−0.045**; 12 BB 53.5/33.0/−0.082; 15 BB 45.7/28.2/−0.127; 20 BB 40.2/21.7/−0.183; 25 BB 36.1/17.4/−0.229. The SB value is only a lower bound on the SB's full-game value, because the SB is restricted to jam/fold. |
| `exploit.py` | 3 s | Gain of the SB best response over Nash jam/fold against fixed BB calling ranges. 10 BB: vs nit calling top 15% **+0.278** (jams 100%); vs top-70% caller +0.072; vs always-call +0.069 (Nash already earns +0.499). 15 BB: vs nit +0.198, vs top 70% +0.082, vs always-call +0.128. |
| `mmdstep2.py` | 2 s | One-step MMD update at 10 BB, `π ∝ exp[(log π0 + η q + η α log ρ)/(1+ηα)]`. π0 is Nash smoothed with ε=0.02, α=0.02, ρ is uniform, and q is exact under the opponent model. vs nit: η=3 gain **+0.042**, worst case **−0.067** (Nash −0.045); η=10 **+0.252 / −0.328**; η=1e9 +0.278 / −0.378. vs top-70% caller: η=10 +0.062 / −0.060. Rows where the policy saturates (η=1e9, and η=10 against the top-70% caller) print a KL of `nan`, and the first of them a numpy divide-by-zero warning on stderr; this is harmless. |
| `mmd_ab.py ALPHA` | 2 s | The same demo with α taken from the command line and an η=0 row. α=0 and α=0.02 are almost identical (η=10 vs nit: +0.261/−0.343 at α=0, +0.252/−0.328 at α=0.02). |
| `icm2.py` | <1 s | Malmuth-Harville ICM with TrueSkill payouts. Players busted in a hand take the last places. Equity needed to call off an all-in with no dead money: 3 equal stacks **0.600**; 4 equal stacks **0.646**; 4p (600,1800,1800,600) big vs big **0.737**, short vs short 0.557; 4p (2400,1200,800,400) leader calls short 0.519, second calls short 0.532; 3 left of 4, 1600×3: 0.592; (3000,900,900) mid vs mid 0.530. |
| `trueskill_payouts.py` | <1 s | *New*: reproduces the payouts that `icm2.py` hard-codes. Equal ratings give (1,0), (1,.5,0) and **(1,.6444,.3556,0)**, the same at σ=8.33 and σ=1. At unequal ratings (me μ=30 against 29/28/27, others finishing in rating order) the 4-player payouts are (1,.6338,.3405,0) at σ=8.33 and (1,.6233,.3255,0) at σ=1. The second matches the critique's (1,.623,.325,0). |

`icm2.py` uses the rounded payouts (1,.645,.355,0). With the unrounded (1,.6444,.3556,0) the thresholds are unchanged at 3 decimals (0.646, 0.737, 0.592).

To install trueskill 0.4.5 on the Debian/Ubuntu Python here, plain `pip install trueskill` fails with `AttributeError: install_layout`; use `SETUPTOOLS_USE_DISTUTILS=stdlib pip install trueskill`.

## Caveats (from the research critique)

- With a uniform ρ, the magnet term is the same for every action and cancels. α then acts only as a temperature 1+ηα on the anchor. This is harmless in this demo, where η ≤ 10 in BB units and ηα ≤ 0.2. With η in prize units, ηα can reach 2-100, and a uniform ρ then randomises deterministic anchors. The critique recommends setting ρ = π0 or keeping ηα ≤ 0.1.
- The values are chip EV for isolated HU push/fold. They are not the CodinGame placement payoff and do not include blind rotation.

## Removed duplicates

- `icm.py` was the earlier version of `icm2.py`. It gave busted players 0 whatever their finishing place, and used 3-player payouts for a 4-player game with one player already out. Every shared case matches `icm2.py` except (3000,900,900) mid vs mid: 0.531 against 0.530.
- `mmdstep.py` did not run, even in the original scratch directory. It exec'd `exploit.py` up to its first `for S in`, which cut `exploit.py`'s own exec line mid-string and raised a SyntaxError. `mmdstep2.py` is its self-contained copy with identical parameters.
