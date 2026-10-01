# cgpoker

Home of a bot for the [CodinGame Poker](https://www.codingame.com/multiplayer/bot-programming/poker)
arena: no-limit hold'em sit-and-go for 2-4 players, 4,800 chips in total, blinds 5/10 doubling
every 10 hands (every non-BB player posts the small blind), 50 ms per turn (1,000 ms on the first),
every hole card revealed after each hand, a 100,000-character source limit, and TrueSkill ranking on
finishing place. The repository holds the research and tooling for the bot: an exact Python port of
the referee, a Java replayer that runs real games through the referee's own classes, 381 public
replays, field statistics, small game-theory solvers, an opponent-model benchmark, a 7-card
evaluator with an exhaustive test and speed probes, and the build plan. It follows the same
measure-everything approach as the owner's CodinGame UTTT engine (crossfish): every number in the
docs is labelled with the script that reproduces it.

**Status (2026-10-01):** M0 of [`docs/plan.md`](docs/plan.md) is built: the C++ engine (validated
against the Python port and the 381 replays), the bot's state tracker, a baseline bot that bundles
to a 28k-character CodinGame submission, and the local arena with paired SPRT. The plan (milestones
M0-M5): play as close to an equilibrium as fits in 50 ms and 100k characters, without modelling
individual opponents, and see how far that gets. Next: M1, short-stack equilibrium.

This is a public, just-for-fun project. Note that it includes the bot's plan, field analysis and
profiles of live arena bots, so anyone in the arena can read them.

## Repository map

| path | what |
|---|---|
| [`engine/`](engine/README.md) | C++ port of the referee (`pk::Engine`), the rules alone (`pk::Board`) and the bot's state tracker (`pk::Tracker`); differential checks against `sim/` and the replays |
| [`bot/`](bot/README.md) | The bot: `bot.hpp` (tracker + Monte Carlo equity + policy), `main.cpp` (CodinGame I/O and platform probes), jam/fold tables, the frozen `bot_prev.hpp` |
| [`arena/`](arena/README.md) | Local arena on the engine: dev vs prev and scripted opponents, duplicate seeds, paired SPRT; `freeze.py` |
| [`sim/`](sim/README.md) | Python port of the referee (`poker_sim.py`, SHA1PRNG-exact deck), replay validation, decision reconstruction |
| [`replayer/`](replayer/README.md) | Java driver over the referee's own model classes (compiled from the submodule), per-decision event logs, `PreEq.java` |
| [`analysis/`](analysis/README.md) | Field and game-structure statistics from the replays (style tables, steal rates, bust depths, stack depth, heads-up phase, decision regimes) and from the leaderboard and battle lists (pairwise finish-ahead rates, score fit) |
| [`solvers/`](solvers/README.md) | 169×169 preflop equity (`eq.c`, `eq169.bin`), HU jam/fold Nash, best-response gains, one-step MMD demo, ICM with TrueSkill payouts |
| [`eval/`](eval/README.md) | Duplicate-seed variance reduction measured in the simulator |
| [`cpp/`](cpp/README.md) | `pe7c.hpp` 7-card evaluator to ship, exhaustive test, benchmarks of evaluator / range-vs-range / MLP / push-fold, minified-size probe |
| [`tools/`](tools/README.md) | `bundle.py` (single-file CodinGame submission), `cg_minify.py` (crossfish's minifier + `--keep`), `fetch_replays.py` (throttled replay downloader) and the two game-id lists |
| [`data/`](data/README.md) | `replays/<gameId>.json.gz` (381 games), `battles/<nick>.json.gz` (top-37 battle lists), `snapshots/2026-09-30/` (leaderboard, statement), `decisions.jsonl.gz` (22,916 decisions); `cache/` is regenerated and gitignored |
| [`docs/`](docs/) | Plan, open questions, Ataraxos explainer, research notes (rules, field, literature, engineering) |
| [`archive/`](archive/README.md) | Work the current plan does not use: the first, exploitation-oriented plan, per-bot style statistics and the opponent model |
| `third_party/CodingamePoker` | Git submodule: the referee, [wala-fr/CodingamePoker](https://github.com/wala-fr/CodingamePoker) @ `ac30d97` |

## Quickstart

Needs Python 3 (tested 3.11), a JDK (tested 21), gcc/g++ with C++17 (tested 13.3), GNU make and bash.

```sh
git clone --recurse-submodules https://github.com/nathanWolo/cgpoker.git && cd cgpoker
#   (existing clone: git submodule update --init)
python3 -m venv .venv && . .venv/bin/activate     # optional; .venv/ is gitignored
pip install -r requirements.txt                   # numpy, trueskill (see the note in the file for system Pythons)

make replayer        # compile the Java replayer           -> built 46 classes from 46 sources
make validate        # Python sim and Java replayer        -> 381/381 replays reproduced exactly (both)
make reconstruct     # decision records                    -> 74,347 from 381 games into data/cache/;
                     #   the committed data/decisions.jsonl.gz (22,916 from 120 games) reproduced exactly
make cache           # data/cache/replayed.pkl             -> replayed 371 bad 0
make preeq-check     # analysis/preeq.tsv regenerated byte-identically by PreEq.java
make solvers-check   # every solver script vs its documented numbers (eq.c smoke run at 200 trials)
make cpp-test        # all 133,784,560 7-card (and all 6-, 5-card) hands, CodinGame and native flags -> PASS
make engine-check    # C++ engine + tracker vs poker_sim.py on 3,000 random games and the 381 replays -> all identical
make bot             # build/cg/poker_min.cpp, the CodinGame submission (27k chars), compiled with CodinGame's flags
make arena           # build/arena/arena and a 200-game smoke run of the bot against scripted opponents
make all             # all of the above, about 4 min on 4 cores
make eq169-check     # optional: rebuild eq169.bin at 20k trials/pair (~50 s) and compare bytes
```

Every target exits non-zero on a mismatch. Each directory's README documents the individual
scripts, their run times and their observed output, e.g. `python3 analysis/steal_se.py`,
`python3 analysis/hu_phase.py`, `python3 eval/dup.py 3000`, `make -C cpp run` (benchmarks),
`make -C cpp cmp` (needs `cpp/fetch_third_party.sh`), `make -C cpp cgsize CROSSFISH=<crossfish checkout>`.

## Key findings

Details and provenance in the linked docs; each number names the script that reproduces it.

- **Unusual blinds.** Every player who is not the BB posts the small blind, so the preflop pot is
  1.5 / 2 / 2.5 BB with 2 / 3 / 4 players; the statement does not say so. The 600-decision cap is
  counted across the whole game, but 0 of 381 real games reached it.
  ([rules](docs/research/rules.md), `sim/validate_replays.py`)
- **Exact simulators.** The Python port and the Java replayer each reproduce all 381 public replays
  exactly: who acts at every step, every hole card, and the final scores.
- **Games are short and end short-stacked.** Median 40 / 47 / 46 hands for 2 / 3 / 4 players (371
  games). The eliminated player had ≤10 BB in 44 / 54 / 53% of busts and ≤20 BB in 70 / 79 / 69%
  (120 games). Yet 64 / 52 / 51% of all decisions are made at >50 BB effective.
- **Heads-up decides 1st vs 2nd.** 130/131 three-player and 147/154 four-player games reach
  heads-up, at a median effective stack of 18 / 14 BB (`analysis/hu_phase.py`). HU jam/fold Nash at 10 BB: SB jams
  58.3%, BB calls 37.5% (`solvers/pf.py`).
- **Placement payoff makes calling off expensive.** TrueSkill payouts at equal ratings are
  (1, .5, 0) and (1, .6444, .3556, 0); calling off an all-in needs 0.600 equity with 3 equal stacks,
  0.646 with 4, and 0.737 for big-vs-big at (600, 1800, 1800, 600) (`solvers/icm2.py`).
- **The top is close and non-transitive.** #1 Waffle3z scores 30.65 and #16 27.16; Python bots hold
  #3, #5 and #6. #1 finishes ahead of a given top-league opponent only 61% of the time, and Tuo, a
  pure push/fold bot (#5), finishes ahead of #1 in 24 of 43 shared games (`analysis/pairwise.py`).
  ([field](docs/research/field.md))
- **Jam/fold Nash is solved; deeper heads-up play is the open problem.** HU jam/fold restricted
  play costs at most 1.4% win probability at ≤6.7 BB (Miltersen-Sørensen). Inside the jam/fold
  game the SB's value turns negative above ~8 BB (−0.045 BB/hand at 10 BB, −0.183 at 20 BB;
  `solvers/pf.py`), which is only a lower bound on its full-game value. Where heads-up decisions
  happen (median 14-18 BB) is where a real HU solve should pay.
- **Evaluation is cheap; flop ranges are not.** `pe7c.hpp` is 2,658 minified characters, starts in
  ~80 ms and evaluates 214-246 M random 7-card hands/s under CodinGame's flags. Range vs range for
  all 1,326 combos costs ~14 µs on the river and ~0.75 ms on the turn, but ~18 ms per opponent on the
  flop, so 4-handed flops need sampling ([cpp](cpp/README.md), [engineering](docs/research/engineering.md)).
- **Duplicate seeds help less than hoped.** Seat swapping cuts variance to 0.61× (HU) and rotation
  to 0.80× (4p), and that is with deterministic toy bots, so a best case (`eval/`).
- **Plan.** Equilibrium-first, with no opponent modelling: jam/fold Nash with ICM at short stacks
  (M1), a solved heads-up 8-30 BB game (M2, the core), 3-4-player preflop charts (M3), and
  optionally an Ataraxos-style self-play net with its one-step search (M4). Progress is measured
  by exploitability (exact best responses, local best response) as well as results. Exact Bayes
  over 1,326 combos replaces Ataraxos's belief network ([plan](docs/plan.md) §0,
  [Ataraxos](docs/ataraxos.md) §8).

## Documents

- [`docs/bot_overview.md`](docs/bot_overview.md): how the current bot works end to end: pipeline, tracker, evaluators, Monte Carlo, beliefs, decision rules, output mapping, constraints.
- [`docs/plan.md`](docs/plan.md): the equilibrium-first build plan: milestones and gates, per-turn algorithm, how equilibrium quality is measured, character budget, risks.
- [`docs/open_questions.md`](docs/open_questions.md): the open questions (Q-A1 ... Q-H3) that the plan references.
- [`docs/ataraxos.md`](docs/ataraxos.md): the Ataraxos paper (Nature 2026) explained, and what transfers to poker.
- [`docs/research/rules.md`](docs/research/rules.md): exact rules with referee line references.
- [`docs/research/field.md`](docs/research/field.md): the leaderboard, ranking mechanics and where games are decided (per-bot profiles are archived).
- [`docs/research/literature.md`](docs/research/literature.md): literature map (push/fold, ICM, CFR-family, MMD, safe exploitation).
- [`docs/research/engineering.md`](docs/research/engineering.md): what fits in 100k characters and 50 ms.
- [`docs/research/README.md`](docs/research/README.md): index, plus a note on `raw/workflow_results.json` (raw research-agent output, kept verbatim, with scratch paths).

## Data provenance and third-party material

- **Replays and battle lists** (`data/replays/`, `data/battles/`) are public CodinGame game results,
  fetched on 2026-09-30 from CodinGame's public but undocumented web API
  (`gameResult/findByGameId`, `gamesPlayersRanking/...`); each replay is stored as returned,
  gzipped (10 of the 381 after a JSON round trip; see [`data/README.md`](data/README.md)). `data/snapshots/2026-09-30/` holds the leaderboard and statement as fetched that day,
  except that the CodinGame game-viewer JavaScript embedded in the puzzle metadata was removed
  ([`data/README.md`](data/README.md)). The leaderboard and battle lists include other players'
  public profile data.
  **Check CodinGame's terms of service before any bulk harvesting**; `tools/fetch_replays.py` is
  single-threaded with at least 2 s between requests and has not yet been run against the live API.
- **The referee** is the git submodule `third_party/CodingamePoker`
  ([wala-fr/CodingamePoker](https://github.com/wala-fr/CodingamePoker) @ `ac30d97`). That repository
  has **no license file**, so none of its sources are copied into this repository: `replayer/`
  compiles them from the submodule at build time, and `sim/poker_sim.py` is our own port. Two
  CodinGame SDK files are cited by URL and commit in [`replayer/README.md`](replayer/README.md).
- **OMPEval** (ISC): `cpp/pe7c.hpp` reimplements its method and copies its rank-key constants;
  attribution and the license notice are in [`cpp/THIRD_PARTY.md`](cpp/THIRD_PARTY.md). OMPEval and
  PokerHandEvaluator (Apache-2.0) sources are fetched only for `make -C cpp cmp` and never committed.
- **Papers** (Ataraxos and the literature) are cited by DOI or URL; no paper text is stored here.
