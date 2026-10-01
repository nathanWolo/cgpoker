# replayer/: re-run real games through the referee's own Java classes

`Replayer.java` drives the referee's model classes (`Board`, `ActionUtils`, `WinningCalculator`, ...)
straight from the `third_party/CodingamePoker` submodule (wala-fr/CodingamePoker @ `ac30d97`). It takes a
replay's seed and the recorded bot outputs and prints a tab-separated event log with every hole card.
This is the Java-side oracle for `sim/poker_sim.py`; `analysis/` builds its statistics from this log.

| file | what |
|---|---|
| `build.sh` | Compiles the referee classes listed in `sources.txt`, the files in `src/` and the `stub/` into `build/` (gitignored). |
| `sources.txt` | The referee subset needed, as paths relative to `third_party/CodingamePoker/CodingamePoker/src/main/java`. `Referee.java`, `Player.java`, `input/` and `view/` are left out because they need the CodinGame SDK. |
| `src/com/codingame/game/Replayer.java` | Ours. It repeats the `Referee.gameTurn` loop without the SDK: reads `seed`, `N`, then `player<TAB>output` lines on stdin. |
| `src/com/codingame/game/PreEq.java` | Ours. A Monte-Carlo table of preflop equity against 1-3 random hands, using the referee's SKPokerEval. |
| `stub/org/slf4j/` | Ours. A no-op `Logger`/`LoggerFactory`, so the build needs no slf4j jar. |
| `replay_game.py` | Converts a replay to Replayer input, runs it, and checks the scores. `replay(path) -> (replay_dict, stdout)` is the API that `analysis/` uses. |

**Overlays.** A file under `src/` with the same relative path as a `sources.txt` entry replaces the
upstream file at build time. There are none today. All 42 referee files previously copied here were
byte-identical to upstream `ac30d97`, so they were deleted in favour of the submodule. The submodule has
no LICENSE file, so its sources are not vendored here.

The research also relied on two CodinGame SDK files that are not in this repo. Both match upstream
[CodinGame/codingame-game-engine @ 9e32d14](https://github.com/CodinGame/codingame-game-engine/tree/9e32d14b8845d1a42b6fef61b41f0084d1eaf81c/engine/core/src/main/java/com/codingame/gameengine/core):
- `MultiplayerGameManager.java:57-62` seeds `SecureRandom("SHA1PRNG")` with the game seed.
- `GameManager.java:38,202` sets the 30 s hard quota and uses `firstTurnMaxTime` on a bot's first turn.

## Run (from the repo root; needs a JDK, tested with 21, and `git submodule update --init`)

```sh
replayer/build.sh                               # built 46 classes from 46 sources into .../replayer/build
python3 replayer/replay_game.py 906530651       # event log of one game (id or path); ends at hand 46:
#   GAMEOVER  201
#   SCORES    -2  4800  -3
python3 replayer/replay_game.py --check         # all data/replays, 8 JVMs in parallel, ~1 min; exit 1 on any mismatch
#   381/381 replays reproduce the recorded final scores
java -cp replayer/build com.codingame.game.PreEq 40000 > analysis/preeq.tsv   # byte-identical to the committed file
```

`replay()` builds on demand if `build/` is missing. Timeouts become a `__TIMEOUT__` input line; the
player id is taken from the `$<id> did not output in time!` summary.

The 381-game check also shows that every game used all of its recorded outputs, with no `ERR` lines,
and ended in `GAMEOVER`. None hit the 600-round `CANCEL`. The `CANCEL` scoring path was checked
differentially instead: 30 calling-station games, 20 of them cancelled, were generated with
`sim/poker_sim.py`, and their action logs gave identical `SCORES` in Java. The original scratch
replayer reported 371/371 by matching winners and scores by hand. It did not print scores.

## Output format (one TSV event per line)

| line | fields |
|---|---|
| `START` | `N firstBBId nRecordedActions` |
| `HAND` | `hand sb bb dealerId sbId bbId`, then per seat `i:startStack:cards` (`OUT` if eliminated) |
| `ACT` | `round hand pid board cards bb pot callAmt stackBefore states possible action put alive notFolded dealerId sbId bbId raw`. `states` is `stack/totalBet/roundBet[F]` per seat; `pot` and `possible` are taken before the action; `action` is after the referee's replacement rules; `put` = chips added. |
| `END` | `hand`, then per seat `i:cards:winAmount:stack:F\|-`, then the board |
| `GAMEOVER` | `round` |
| `CANCEL` | `hand` (600-round cap) |
| `SCORES` | the final score per seat, as the referee reports it: stack for survivors, `eliminationRank - N` for busted seats. Added in this repo; it is always the last line. |
| `ERR` | a recorded action did not match who the referee asks next, or the recorded actions ran out |

`CANCEL`/`SCORES` follow `Referee.doCancelLastHand`: `cancelCurrentHand()` then `calculateFinalScores()`.
Rules and quirks: [docs/research/rules.md](../docs/research/rules.md).
