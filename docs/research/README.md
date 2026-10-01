# docs/research/

Research notes behind [`../plan.md`](../plan.md). Each document re-checks the research run's claims
against the code and data in this repository and labels every number with where it came from
(**reproduced** by a named repo script, **ad hoc** from a one-off count with no committed script,
**research** as recorded by the research run, or **literature**).

| document | what it covers |
|---|---|
| [`rules.md`](rules.md) | Exact rules read from the referee (`third_party/CodingamePoker` @ `ac30d97`) with line references: blinds (every non-BB player posts the SB), betting and action replacement, the 600-round cap, scoring, RNG, input protocol, and corrections to the research run's citations (§14). |
| [`field.md`](field.md) | The 2026-09-30 leaderboard and how the top bots play: style statistics, pairwise finish-ahead rates, head-to-head results, common weaknesses, where games are decided. |
| [`literature.md`](literature.md) | Push/fold and ICM, deep heads-up methods, multiplayer caveats, MMD and update equivalence, opponent modelling and safe exploitation, mapped onto CodinGame's limits. |
| [`engineering.md`](engineering.md) | What fits in 100k characters and 50 ms: character budget, table sizes, hand evaluator, runtime primitives, per-turn budgets, offline training cost. |
| [`../ataraxos.md`](../ataraxos.md) | The Ataraxos (Stratego) paper explained, and which parts transfer to poker. |
| [`../open_questions.md`](../open_questions.md) | The 42 unresolved questions (Q-A1 ...) that the plan references. |

## `raw/workflow_results.json`: raw agent output, kept verbatim

`raw/workflow_results.json` is the unedited structured output of the research workflow
(`research[4]`, `designs[3]`, `plan`, `critique`). It is a historical record, not a maintained
document:

- **It contains absolute paths into the research run's temporary scratch directory** (under `/tmp`) on the machine the
  research ran on. Those paths do not exist in this repository and are not used by any code here.
- **Some of its claims were later corrected.** The critique inside the same file lists several, and
  the documents above flag every place where a re-run in this repository gave a different number
  (for example the 381-game validation, the 808 vs 810 eliminations and the referee line numbers).
  When the raw file and a document disagree, the document is the one that was checked.

Where the scratch files referenced in the raw output now live:

| scratch location in `workflow_results.json` | in this repository |
|---|---|
| `poker_research/poker_sim.py`, `validate_replays.py`, `reconstruct.py` | `sim/` (plus `sim/replay_io.py`) |
| `poker_research/{elim_depth,hand_outcomes,replay_stats,stack_depth}.py`, `stack_depth_table.txt` | `analysis/` |
| `analyze.py`, `stats.py`, `stats2.py`, `stats3.py`, `steal_se.py`, `preeq.tsv` | `analysis/` |
| `replayer/`, `replay_game.py` | `replayer/` (the referee classes now come from the submodule) |
| `poker_research/evals/` (`pe7*.hpp`, `bench*.cpp`, `rvr.cpp`, `mlp.cpp`, `pushfold.cpp`, `cmp.cpp`, `train_cost.py`, ...) | `cpp/` (`pe7c_fixed.hpp` is now `pe7c.hpp`; see `cpp/README.md`, History) |
| `pf/` (`eq.c`, `eq169.bin`, `pf.py`, `exploit.py`, `icm2.py`, `mmdstep2.py`), `mmd_ab.py` | `solvers/` (`icm.py` and `mmdstep.py` removed as superseded/broken) |
| `exploit/oppmodel2.py` | `oppmodel/` (`oppmodel.py` and `steal.py` removed as superseded) |
| `staged/dup.py`, `staged/dup4.py` | `eval/` |
| `fetch_replays.sh`, `poker_research/fetch_replays.py` | `tools/fetch_replays.py` (rewritten: throttled, resumable) |
| `replays/`, `poker_research/replays/`, `battles/` | `data/replays/<gameId>.json.gz`, `data/battles/<nick>.json.gz` |
| `cg_lb.json`, `lb_league0.json`, `cg_statement.txt`, `cg_findProgressByPrettyId.json` | `data/snapshots/2026-09-30/` (see `data/README.md`; the game-viewer JavaScript inside `cg_findProgressByPrettyId.json` was removed) |
| `battles_waffle.json`, `lb_waffle.json` | not kept: both were byte-identical to `data/battles/Waffle3z.json.gz` |
| `poker_research/decisions.jsonl` | `data/decisions.jsonl.gz` |
| `pairwise.py`, `hu_phase.py`, `regimes.py` | `analysis/` (ported with repo-relative paths; output identical). `analysis/leaderboard.py` is new and reproduces the leaderboard summary in `field.md` §1 |
| `paper.txt`, `supp1.txt`, `lit/`, `stratego/` | deliberately not included: cite the papers by DOI and the code by URL and commit (see `../ataraxos.md`, `literature.md`) |
