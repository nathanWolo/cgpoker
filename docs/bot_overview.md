# How the bot works, end to end (M2, 2026-10-01)

One turn, from stdin to stdout, with the parts that make it work. File references are to this
repository; the numbers are the ones measured in [`bot/README.md`](../bot/README.md),
[`engine/README.md`](../engine/README.md) and [`cpp/README.md`](../cpp/README.md).

## 1. The turn pipeline

Every turn, `bot/main.cpp` does five things:

1. **Parse stdin** into a `pk::Obs` (`engine/tracker.hpp`, `read_obs`): round, hand number, every
   seat's stack and chips-in-pot, the board, our hole cards, every action since our last turn, the
   showdown lines of hands that finished since then, and the list of legal actions the referee
   offers.
2. **Replay the game** through the tracker so the bot has the full state, not just the snapshot.
3. **Decide** (`bot::Bot::act` in `bot/bot.hpp`): form a belief about the opponents' hands, estimate
   our equity by Monte Carlo, apply the decision rules.
4. **Emit** one line, mapped so the referee never has to correct it.
5. **Log** diagnostics to stderr (dev builds): time used, trials run, equity, which rule fired.

## 2. The state tracker (`engine/tracker.hpp`)

CodinGame's input is a snapshot at our turn plus the actions since we last acted. That is not
enough to play well: `ALL-IN` lines carry no amount, several opponent actions can arrive at once
across streets, and the minimum raise depends on the whole betting sequence.

So the tracker is the referee's own rules (`pk::Board` in `engine/poker_engine.hpp`, the C++ port
of the referee) driven by the stdin lines. It starts each hand (positions, blinds), applies each
action line, handles the referee's odd corners (all-in runouts with no decisions, the
600-decision cap), and settles each finished hand from its showdown line, since every player's
hole cards are revealed after every hand.

Then it checks itself against the snapshot: round, hand, every stack, every chips-in-pot, the
board, and the offered actions, which pins down the minimum raise and the raise cap. It has never
failed: 0 mismatches over 633k fuzz decisions and all 74k decisions of the 381 recorded games
(`engine/check.py`). If it ever fails live, it resyncs from the snapshot rather than crashing.

A detail it gets right that standard poker intuition misses: in this referee every player who is
not the big blind posts the small blind, so the pot before anyone acts is 1.5 / 2 / 2.5 BB with
2 / 3 / 4 players ([`research/rules.md`](research/rules.md)).

## 3. Hand evaluation

Two evaluators with the same ordering:

- **`cpp/pe7c.hpp`**: table-driven 7-card evaluation (OMPEval's rank-key method), about 240M
  evaluations/s. Its tables are built at start-up, which took 2.3 s on CodinGame against 80 ms
  here.
- **`cpp/eval7_slow.hpp`**: table-free, pure bit tricks (suit masks for flushes, a shifted AND for
  straights, rank counts for pairs), about 14M evaluations/s at CodinGame's flags, zero start-up.

`main.cpp` builds the fast tables in a detached thread from the moment the process starts, and the
bot reads an atomic flag each turn: fast tables if ready, otherwise the slow evaluator. They agree
on 300k random comparisons (`cpp/test_eval7_slow.cpp`), so switching mid-game changes nothing but
speed. No turn ever waits on initialisation.

## 4. Equity by Monte Carlo

Equity is our expected share of the pot at showdown. Each trial deals the rest of the board and the
opponents' hands, evaluates everyone, and scores 1 / (1 + ties) if we are best.

- Opponents' hands come either from the full deck ("uniform") or from a range (section 5).
- Dealing is a partial Fisher-Yates shuffle of only the cards needed.
- The loop runs in batches and checks the clock between them: up to 100k trials (50k against a
  range), or until the budget expires, currently 15 ms. On CodinGame 100k trials take about 3 ms.
- The RNG is splitmix64, re-seeded each turn from the round number and our hole cards, so a given
  situation always gets the same random stream. A live decision can be replayed bit for bit
  offline, and the arena's duplicate-seed games stay correlated.

## 5. Beliefs: what do they hold?

This is the part that changed most after the first live games, and where the equilibrium-first,
no-opponent-modelling stance ([`plan.md`](plan.md) §0) shows. The bot never remembers anything
about a specific opponent. It asks what an action *means*, assuming the opponent plays roughly as
we would:

| situation | assumed holding |
|---|---|
| nobody has bet (checked to us, or we are first in) | uniform random |
| a preflop open / a 3-bet | the top 35% / 20% of starting hands, ranked by `bot/pf_rank.hpp` (equity against a random hand) |
| a preflop shove at a depth where we shove ourselves (≤ 25 BB) | the Nash jam range at that depth (`bot/pf_tables.hpp`) |
| a shove deeper than that | our strategy never does this, so it is an off-tree action: the uniform (ε-floor) belief |
| a postflop bet of x pot | the top 65 − 35·min(1, x)% of the ~1,225 possible opponent combos, ranked by made-hand strength on the board |

The first version used a "sensible" tight range for deep shoves. The arena showed that it bleeds
against the lower league's any-two shovers (it folded and then open-folded the next hand), and it
was not the self-consistent belief anyway. The postflop ranking ignores draws, a known limitation.

The range sampler draws each opponent's hand from the range, rejecting card conflicts;
`top_range` ranks all combos with the evaluator each time (about 20 µs).

## 6. The decision rules

In order:

1. **Heads-up at 8-150 BB effective: the solved game.** Preflop and every postflop street come from
   one MCCFR solution of an abstract heads-up game (`solvers/hu/`, [README](../solvers/hu/README.md)):
   fold / limp / raise / all-in preflop, check / half-pot bet / all-in postflop, 169 preflop classes
   and 10 buckets per postflop street by expected showdown equity, solved at 9 stack points from 8 to
   120 BB and interpolated (the 120 BB tables beyond). The tracker logs every action of the hand; `bot/hu_play.hpp` maps them onto the
   tree's history, computes our bucket (300 runouts, exact on the river), and *samples* the action
   from the node's probabilities, so the strategy is mixed as an equilibrium is. A history the tree
   lacks (a 4-bet) falls through to the rules below. Below 8 BB the heads-up jam/fold Nash of
   `bot/pf_tables.hpp` still applies (the small blind jams or folds, the big blind calls or folds).
2. **3-4 players, preflop: the ICM push/fold chart.** Facing a jam, always (unless it is small next
   to our stack with players still to act, where pot odds apply); first in, over limpers and against
   a raise when our stack is ≤ 20 BB. A limp counts as a fold and a raise as a jam, since the chart's
   tree has neither; short-stacked, the answer to either is jam or fold anyway. The chart (`bot/pfn_tables.hpp`) comes from
   `solvers/pfn/`, a fictitious-play solver of the exact jam/fold game of this referee (every
   non-BB posts the small blind) with ICM leaves: 6 decision nodes for 3 players, 14 for 4, solved
   on a grid of every player's stack (343 and 1,296 configurations) and distilled to one class
   ranking per node plus a threshold per grid point, interpolated in log-stack at runtime. A
   re-raise, or a raise while we are deep, is off the tree and falls through to the rules below.
   This is what the live M0.1b games asked for: with ≤ 12 BB and 3-4 players the bot limped 78%
   of unopened pots and called half the raises it faced; 15 of 84 games ended with it blinded down
   below 2 BB.
3. **A big call (≥ 40% of our stack).**
   - *3-4 players alive*: ICM. Malmuth-Harville tournament equity with the payouts that
     CodinGame's placement-only TrueSkill implies, (1, .5, 0) and (1, .644, .356, 0), with a bust
     paid at its place (M0.1b paid it 0, which made the bot too tight calling off 4-handed). It compares
     EV(call) = e·ICM(win) + (1 − e)·ICM(lose) with ICM(fold), with the pot capped at what we can
     actually win. This is what stops the bot taking 160 BB coin flips while two other bots are
     busy busting each other.
   - *Heads-up*: chips are tournament equity, so pot odds, plus a margin rising to 0.04 at ≥ 50 BB
     deep (the option value of waiting for a better spot).
4. **Facing a smaller bet**: raise pot-sized with equity > 0.75 against the range; call with
   equity > odds + 0.02; else fold.
5. **Unopened preflop, not in the BB**: open to 2.5 BB with equity > 0.50 (+0.03 per extra
   opponent), else complete if the cheap call is priced, else fold.
6. **Checked to**: bet 0.6 pot with equity > 0.55 (+0.04 per extra opponent), else check. The
   threshold was 0.62; against passive tables the bot was checking down winners.

## 7. Output mapping

The referee replaces illegal outputs rather than rejecting them, and some replacements are traps:
`CHECK` when facing a bet becomes `FOLD`, and `FOLD` is never replaced even when checking is free.
So the bot prints `CHECK` only when it is offered, `ALL-IN` whenever an amount covers its stack (a
`CALL` for the whole stack would be rewritten), and `BET x` at or above the offered minimum. The
engine counts replacements; the bot has had zero in every arena run and in all 91 live games of M0.

A detail that bit once: pot odds when the opponent's shove is bigger than our stack. The naive
formula counts chips we cannot win; the fix counts each opponent's chips only up to our own
commitment. The first version of that fix got our own commitment wrong and the bot folded QQ to
shoves. The arena's paired test caught it before it went live.

## 8. CodinGame constraints that shaped the code

- **100k characters.** The bot is bundled into one file by `tools/bundle.py` (76k readable, 34k
  minified). The minifier (`tools/cg_minify.py`) renames identifiers, so the code uses no macros
  (it mangles them inconsistently) and the bundler passes a keep-list for nested `std::chrono`
  names it would otherwise rename.
- **No `-O` flag.** CodinGame compiles unoptimised (as C++20, it reports `__cplusplus=202002`);
  `#pragma GCC optimize("O3")` at the top of the file optimises function bodies, and hot helpers
  are `always_inline`.
- **50 ms per turn, elimination on timeout.** Hence the explicit time budget, the trial caps and the
  background table build.
- **One source file, no state between games.** Each game starts a fresh process.

## 9. What it does not do yet

- Postflop with 3-4 players is still heuristics around equity-vs-range.
  The heads-up solution's abstraction is coarse: one bet size besides all-in, no non-all-in raises
  postflop, equity buckets that do not tell a draw from a made hand of the same equity.
- Multiway, the big-call ICM rule approximates side pots; the push/fold chart settles them exactly
  but models everyone as jam-or-fold, so a short stack facing a min-raise gets the heuristics.
- The flop/turn range ranking ignores draws.
- Outside the heads-up solution the rules are deterministic given the cards, which a strong
  opponent could exploit. Equilibrium strategies mix.

## 10. How it is verified

Every layer has a differential test: the C++ engine against the Python referee port (3,000 random
games, identical decision for decision) and the 381 recorded games; the tracker against the engine
on every one of those decisions; the two evaluators against each other and the engine; the bundled
and minified files against each other on a real stdin; and every policy change against the frozen
previous version in paired games with the same seeds and seats (`arena/`).
