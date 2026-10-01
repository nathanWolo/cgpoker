# Per-bot profiles (archived)

Moved here from `docs/research/field.md` on 2026-10-01. The current plan
([`../../docs/plan.md`](../../docs/plan.md)) is equilibrium-first and does not model or profile
individual bots, so these sections are kept only as a record of the 2026-09-30 research. The
scripts that produce them are `stats.py` and `stats3.py` in this directory (they read
`data/cache/replayed.pkl`; build it with `make cache`).

## A. How the top bots play

The numbers below are reproduced with `analysis/stats.py` and `analysis/stats3.py` on the 371
replays. "Hands" is that bot's sample size. Column definitions:

- **VPIP / PFR**: share of hands where the bot voluntarily put chips in / raised preflop.
- **open**: share of unopened pots the bot raises or shoves.
- **fold vs raise**: share of times the bot folds when it faces a preflop raise.
- **call AI**: share of all-ins faced that the bot calls; n is the number of all-ins faced.
- **bet when checked to**: postflop.
- **fold vs bet**: postflop, facing a bet.

| # | bot | hands | VPIP / PFR | open | fold vs raise | call AI (n) | bet when checked to | fold vs bet | notes |
|---|---|---|---|---|---|---|---|---|---|
| 1 | Waffle3z | 4,843 | .70 / .64 | .91 | .51 | .33 (169) | .97 | .62 | Loose-aggressive steal machine. Opens to 2 BB 22%, 5 BB 15%, 6 BB 11%. Postflop bets 0.5 pot 36%, 0.1 pot 26%. Calls all-ins with a median top-21% hand. Prints no messages. |
| 2 | kovi | 4,082 | .26 / .12 | .13 | .79 | .36 (167) | .51 | .44 | Tight, rule-based. Limps 22% of unopened pots. Postflop bets are always pot-sized. |
| 3 | BrandV | 3,682 | .63 / .41 | .64 | .21 | .63 (51) | .35 | .44 | Loose. Calls raises 68%. First preflop action is ALL-IN 66% at 10-20 BB and 90% below 10 BB. |
| 4 | Zylo | 4,458 | .40 / .32 | .54 | .72 | .32 (151) | .33 | .45 | Tight-aggressive. |
| 5 | Tuo | 3,714 | .18 / .18 | .20 | .85 | .59 (58) | .00 | 1.00 | Pure push/fold, even 120-240 BB deep. Median shove depth 20 BB; still shoves 8% of first actions at 50 BB or more. Postflop it only checks or folds. |
| 6 | JuMaKre | 2,817 | .53 / .32 | .46 | .45 | .59 (37) | .33 | .41 | Monte Carlo equity vs pot odds (from its messages). |
| 9 | MaxFerrer | 1,688 | .55 / .54 | .72 | .64 | .57 (28) | .25 | .82 | GTO push/fold chart plus exploit rules (from its messages). Opens to 2.5 BB 90% of the time. |
| 10 | AGSigma | 655 | .09 / .09 | .11 | .97 | .17 (65) | .00 | 1.00 | Pure push/fold. |

Debug messages give away several methods. Message templates, with counts (reproduced from
`data/cache/stats.pkl`):

| bot | templates |
|---|---|
| kovi | `# rule # #` (2,427), `# rnd` (259), `# shortfold-vs-raise` (236), `# anti-maniac` (145), `# push #bb #way` (92) |
| JuMaKre | `check e=#` (1,036), `pf call e=# need=#` (430), `fold e=# po=#` (402) |
| supernakash | `eq: #,#s` (2,853), i.e. about 45 ms of Monte Carlo per turn |
| kozlov-ma | `eq=# (raw # #) po=# edge=+# tex=# call` |
| MaxFerrer | `XPT.PF.MB` (642), `GTO.PF.CF` (455), `GTO.PF.RSHV` (252) |
| Tux4711 | `D - F #`, `D - O #`, ... |

Waffle3z, BrandV, Zylo, Tuo and AGSigma print nothing. **No top bot shows signs of a solver or a
learned policy.** The field is rules, charts and equity-versus-pot-odds.

**Where Waffle3z wins** (reproduced, `analysis/stats2.py`). It nets +0.46 BB/hand overall. By
effective stack:

| effective stack | BB/hand | hands |
|---|---|---|
| <10 BB | +0.06 | 583 |
| 20-40 BB | +0.56 | 1,007 |
| 40-80 BB | +0.94 | 1,146 |

So stealing and aggression pay off in the middle phase. These means have no standard errors and
are noise-dominated.

**The live lower-league boss** (agent 5117680) behaves differently from the
`CALL;CALLING STATION` template in the referee repo. It limps 43% of unopened pots and folds 95%
when raised (n = 55, 343 hands).

## B. Common weaknesses

Reproduced with `analysis/stats3.py` and `analysis/stats.py` unless marked.

**Over-folding to small preflop opens** (fold rate facing an open to ≤3 BB):

| folds a lot | | folds little | |
|---|---|---|---|
| kovi | .78 (n=1,021) | Waffle3z | .35 (n=1,039) |
| insomnia_rooster | .77 (n=173) | BrandV | .09 (n=794) |
| Zylo | .68 (n=854) | babaaurhum | .07 (n=54) |
| wala | .67 (n=92) | SmogyY | .00 (n=99) |
| M_C | .64 (n=144) | UnTypedScript | .00 (n=95) |
| MaxFerrer | .55 (n=429) | | |

A pure steal breaks even at these fold rates:

| open to | HU | 3p | 4p |
|---|---|---|---|
| 2 BB | 50% | 43% | 37.5% |
| 2.5 BB | 57% | 50% | 44% |

These figures count the opener's own posted SB, which comes back on a successful steal (research
plan; the research's "Design 2" figures of 67/57/50% were wrong).

In the replays, opens to (2, 3] BB win the blinds uncontested 44.1% of the time HU (n = 1,609),
34.4% at 3p (n = 1,812) and 23.7% at 4p (n = 832) (`analysis/steal_se.py`). The steal EVs
themselves are noise-dominated: for example, 4p opens to ≤2 BB net +2.89 ± 1.64 BB (n = 64).

**Over-folding to small postflop bets** (facing about half pot or less):

| bot | fold rate | n |
|---|---|---|
| insomnia_rooster | .86 | 69 |
| MaxFerrer | .83 | 152 |
| Tux4711 | .71 | 297 |
| fr3sh2d3atH | .57 | 605 |
| Waffle3z | .54 | 367 |

**Limpers and calling stations** (share of unopened pots limped): kozlov-ma .87 (n = 200),
trictrac .80 (96), babaaurhum .71 (221), gpoussel_ .65 (1,115), supernakash .60 (726). Value-bet
them thin and do not bluff them.

**Maniacs.**

- SmogyY raises 94% of unopened pots, to a median 50.5 BB.
- UnTypedScript raises 95%, to a median 25.5 BB.
- Both called every all-in they faced, but n = 9 and n = 12. Their median calling hands are in the
  top 61% and 67%.
- Call off wider against them.

**Exploitable patterns in the top two.**

- Waffle3z bets 97% when checked to, then folds 62% when it faces a bet. That invites
  check-raises.
- Tuo and AGSigma only check or fold postflop, and fold 85% and 97% when raised preflop. Their blinds can be stolen.

**Timeouts happen in the top league.** deuwii timed out on its first action in game 906358479, and
[CG]CohereCommandA-Python in game 906523528. A timeout eliminates the bot.

