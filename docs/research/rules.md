# CodinGame Poker: rules and simulator spec

This is the exact rule set of the CodinGame Poker arena, read from the referee source and checked
against real games. The source of truth is the referee
[wala-fr/CodingamePoker](https://github.com/wala-fr/CodingamePoker) @ `ac30d97`, which is the
`third_party/CodingamePoker` submodule. Two simulators follow it:

- `sim/poker_sim.py` is a Python port. `sim/validate_replays.py` reproduces 381/381 public replays
  exactly: who acts at every step, all recorded outputs consumed, and the final scores. The research
  run reported 120/120 on its smaller set.
- `replayer/` runs the referee's own Java classes. `replayer/replay_game.py --check` also gives
  381/381.

So this spec has been checked against the live server, not just read from the code. Where the
CodinGame statement (`data/snapshots/2026-09-30/cg_statement.txt`) is silent or misleading, the code
wins. Sources: `research[0]` in [`raw/workflow_results.json`](raw/workflow_results.json), plus the
re-checks noted below.

**Citations.** `File.java:N` means a path under
`third_party/CodingamePoker/CodingamePoker/src/main/java/com/codingame/`:

| short name | path |
|---|---|
| `Board`, `WinningCalculator` | `model/object/board/` |
| `Action`, `ActionInfo`, `Deck`, `FiveCardHand`, `PlayerModel` | `model/object/` |
| `ActionType` | `model/object/enumeration/` |
| `ActionUtils` | `model/utils/` |
| `Parameter` | `model/variable/` |
| `Referee`, `RefereeParameter` | `game/` |
| `Game` | `view/object/` |
| `InputSender`, `InputUtils`, `RoundInfo`, `ShowDownInfo` | `input/` |

`GameManager.java` and `MultiplayerGameManager.java` are CodinGame SDK files from
[codingame-game-engine @ 9e32d14](https://github.com/CodinGame/codingame-game-engine/tree/9e32d14b8845d1a42b6fef61b41f0084d1eaf81c/engine/core/src/main/java/com/codingame/gameengine/core).
They are not in this repo. Every line number below was re-checked on 2026-10-01; see
[§14](#14-citation-corrections) for the ones the research text had wrong.

---

## 1. Parameters

| parameter | value | source |
|---|---|---|
| Players | 2-4 | statement |
| Total chips | 4800, so the buy-in is 2400 / 1600 / 1200 for 2 / 3 / 4 players | `Parameter.java:9`, `PlayerModel.java:38` |
| Blinds | Start at 5/10. They double when `handNb % 10 == 0`, i.e. at hands 10, 20, 30, ... | `Parameter.java:7-12`; `Board.java:220-230` |
| Raise cap | `raiseNb > 10` per street | `Parameter.java:17`; `Board.java:621-623` |
| Time | 50 ms per decision. Each player's **first** decision gets 1000 ms. | `RefereeParameter.java:5-6`; `GameManager.java:202` |
| Round cap | 600 decisions, counted across all players | `RefereeParameter.java:9`; `Game.java:148-150` |
| First BB | `nextInt(N)` from the game RNG | `Referee.java:64` |
| Folded cards revealed | yes (`SHOW_FOLDED_CARDS = true`) | `RefereeParameter.java:13` |

Big blind by hand number h: **BB = 10 · 2^⌊h/10⌋, SB = BB/2.** Hands 1-9 are 5/10, hands 10-19 are
10/20, and so on up to 1280/2560 at hands 80-89.

## 2. Blinds and positions

**Every player who is not the BB posts the small blind, every hand** (`Board.java:204-218`; the key
line is 208, `int bet = player.getId() == bbId ? bigBlind : smallBlind;`). The statement only says
"blinds 5/10". Consequences:

- The preflop pot is **1.5 / 2 / 2.5 BB** with 2 / 3 / 4 players. Replays confirm it: the first pot
  is $20 in 3-player games and $25 in 4-player games. So does the referee's own test
  `CodingamePoker/src/test/resources/turnSimulation/7_check/test1.txt`: UTG's preflop CHECK
  becomes FOLD and costs the 10 it posted, at the tests' 10/20 blinds
  (`CodingamePoker/src/test/java/com/wala/poker/model/variable/TestParameter.java:5-6`).
- A non-BB player needs only 0.5 BB more to call: in a 4-player pot, 0.5 BB into 2.5 BB.
- Standard heads-up, 6-max and push/fold charts are miscalibrated for this structure.

Other blind rules:

- **Short blinds.** A player whose stack is below the amount they owe posts everything and is
  all-in (`Board.java:209-214`). The threshold is the SB for the SB seat and the **BB** for everyone
  else, so a non-BB, non-SB player with SB ≤ stack < BB also posts their whole stack.
- **Preflop call.** The call amount is `max(BB, highest total bet) − my total bet`
  (`Board.java:468-479`). Everyone must reach a full BB, even if the BB is all-in for less.

**Positions** (`Board.java:146-192`):

- The BB moves to the next seat that still has chips (`Board.java:183-192`).
- The SB *seat* is the first live seat before the BB. The dealer is the next live seat before that.
- With exactly 2 live players, the dealer is the SB (`Board.java:170-172`).
- There is no dead button and no dead small blind.

**Action order:**

- Preflop, the first player to act is the first live seat after the BB. The referee sets
  `nextPlayerId = bbId` and then advances (`Board.java:174`, `232-253`).
- On later streets, the first live seat after the dealer acts first (`Board.java:329`). Heads-up,
  that means the BB acts first after the flop.
- Seats that are folded or all-in are skipped (`Board.java:240`).

The policy must cover N = 2, 3 and 4 live players in any seat. Positions follow from
`firstBigBlindId` plus the BB rotation rule. A 3-4 player game shrinks to heads-up as players bust.

## 3. Betting

- **`BET x` adds x chips now.** It does not mean "raise to x".
- **Minimum raise.** The `BET_x` the referee offers is the minimum legal raise:
  x = minRaise + lastTotalRoundBet − myRoundBet (`ActionUtils.java:49-53`). minRaise is the BB for the
  street's first bet; after that it is the size of the last *full* raise.
- **Full raise.** A raise is full only if the increase is at least the BB (first bet) or at least
  the last full raise (`Board.java:262-272`). Only a full raise moves `lastTotalRoundBet`,
  `lastRoundRaise` and `lastRoundRaisePlayerId`, and increments `raiseNb`.
- **Short all-ins** change none of those values. Two consequences differ from standard rules:
  - The next player may raise by as little as the last full raise over the *old* level. That can
    be less than a full raise over the all-in.
  - Only the last full raiser is limited to call or fold (`ActionUtils.java:45`, `140-145`,
    `159-161`). Everyone else may re-raise, even players who have already acted.
- **Raise cap.** `raiseNb` starts at 1 preflop, because the BB counts (`Board.java:178`). It resets to
  0 on each later street (`Board.java:121`, `371`). At `raiseNb > 10` nobody can raise: BET and
  ALL-IN are turned into a call (§4).
- **Uncalled chips** are returned when the hand is settled (`WinningCalculator.java:45-52`).

## 4. Output parsing and action replacement

**Parsing.** The bot's line is split on `;`. The second part is the message, shown to everyone.
The first part is upper-cased and trimmed (`Referee.java:161-168`). After that:

- `BET_x` is accepted as well as `BET x` (`Action.java:44-46`).
- `ALL_IN` and `ALL-IN` both work (`ActionType.java:6-12`).
- Anything that does not parse becomes **FOLD** (`ActionInfo.java:76-82`). That includes
  `RAISE 200`, `CALL 10`, `BET  200` with two spaces, `BET` with no amount, and a non-integer amount.

**Replacement.** An illegal action is replaced, not rejected (`ActionUtils.java:108-202`):

| output | situation | becomes | lines |
|---|---|---|---|
| `CALL` | checking is possible | `CHECK` | 129-132 |
| `CALL` | call amount ≥ stack | `ALL-IN` | 133-135 |
| `ALL-IN` | we are the last full raiser and can cover the call | `CALL` | 140-145 |
| `ALL-IN` | raise cap reached | `CALL` (or `ALL-IN` if short) | 146-149 |
| `BET x`, x ≤ 0 | any | `FOLD` | 151-154 |
| `BET x`, x ≤ call amount | any | `CALL` (or `ALL-IN` if short) | 155-158 |
| `BET x` | we are the last full raiser, or the raise cap is reached | `CALL` (or `ALL-IN`) | 159-164 |
| `BET x`, x ≥ stack | any | `ALL-IN` | 165-168 |
| `BET x`, raise below the minimum | any | the minimum `BET`, or `ALL-IN` if that is ≥ stack | 170-190 |
| `CHECK` | facing a bet | **`FOLD`** | 192-198 |
| `FOLD` | any, including when checking is free | never replaced | 112-114 |

Practical rules:

- `CALL` is always safe. It becomes CHECK or ALL-IN as needed.
- Never print `CHECK` when facing a bet: it becomes FOLD.
- To fold when checking is free, print `CHECK`, because FOLD is never replaced.
- Never print the literal word `TIMEOUT`. It parses as `ActionType.TIMEOUT` (`ActionType.java:11`),
  skips validation (`ActionUtils.java:112`) and runs `Board.timeout`, which deletes your stack
  (`Board.java:414-415`, `582-587`). This is from reading the code; it has not been tested.

## 5. How a hand ends

- **One player left.** If everyone else folds, that player wins at once (`Board.java:313-316`).
- **Run-out.** When no more betting is possible (everyone left is all-in or matched), the rest of the
  board is dealt with no inputs (`Board.java:317-322`; `Referee.java:106-120`).
- **NONE round.** A hand with no decisions at all, for example everyone all-in from the blinds,
  still uses one round. It appears in the history as `round hand -1 NONE board`
  (`Referee.java:110-114`; `InputUtils.java:40-42`).
- **Burn cards.** One card is burned before the flop, the turn and the river (`Board.java:364-379`).
  Burn cards are never shown.
- **Side pots** are settled in layers. Take the smallest live contribution, split that layer among
  the best hands, and repeat. Leftover chips go back to the player who put them in. The odd chip
  goes to the first winner clockwise from dealer+1 (`WinningCalculator.java:33-104`).
- **Hand strength** is the best of the 21 five-card subsets, with standard rankings. Suits never
  break ties. The wheel (A-2-3-4-5) ranks below a 6-high straight (`FiveCardHand.java:42-77`).

## 6. Rounds and the 600-round cap

- A **round** is one player decision, counted across all players (`Referee.java:155`).
- The cap is `turn == 600` (`Game.java:148-150`, checked at `Referee.java:127-131`). This matches the
  SDK's total-time quota: 600 × 50 ms = 30,000 ms = `GAME_DURATION_HARD_QUOTA` (`GameManager.java:38`,
  `639-642`).
- Winnings are settled *before* the cap check (`Referee.java:122-126` comes before line 127). So the
  600th decision can still finish a hand.
- An unfinished hand at the cap is **cancelled**: every chip put in, blinds included, is refunded
  (`Board.java:604-611`). Survivors then score their stacks (`Board.java:613-619`).
- **Referee bug.** Suppose the round counter is already 600 and the next hand needs no decisions.
  Its NONE round becomes 601, and `roundInfos[601]` overflows its 601-entry array
  (`InputSender.java:21`, `30`), crashing the referee. This has never been observed. What the
  platform does with a crashed referee is unknown.
- `MAX_REFEREE_TURN = 10000` frames is never binding (`RefereeParameter.java:8`).

The cap never binds between competent bots: 0 of 381 replays reached it. Passive bots do reach it
(§12).

## 7. Scoring, elimination and timeouts

- **Busted players** score `rank − N`, where rank 0 is the first player out (`Board.java:554-580`).
  So a later bust scores better.
- **Same-hand busts** are ordered by chips put in that hand. For an all-in, that is the
  start-of-hand stack: more chips means a better place. Exact ties share a rank and a score.
- **Survivors and the winner** score their stack (`Board.java:613-619`; `Referee.java:284-298`).
- The game ends when one player holds every chip (`Board.java:665-667`), or at the round cap.
- **Timeout.** The player is folded, their remaining stack is deleted from the game, and they are
  eliminated at the end of the hand (`Board.java:582-587`; `Referee.java:170-179`). Timeouts do
  happen in the top league ([field.md §6](field.md#6-common-weaknesses)).

CodinGame ranks bots with TrueSkill on finishing order only. Chip margins do not count. See
[field.md §3](field.md#3-ranking-mechanics) and the payout utilities in
[literature.md §2](literature.md#2-short-stack-play-pushfold-and-icm).

## 8. RNG and deck

- The referee draws from `gameManager.getRandom()` (`Referee.java:60`). The SDK makes that a
  `SecureRandom("SHA1PRNG")` seeded with the game seed (`MultiplayerGameManager.java:57-62`). It is
  **not** `java.util.Random`.
- Draw order:
  1. The first BB is `nextInt(N)` (`Referee.java:64`).
  2. Every hand starts from a fresh deck in order 2C, 2D, ..., AS. It is shuffled with
     `Collections.shuffle` and then cut at `nextInt(52)` (`Deck.java:17-25`, `60-65`;
     `Board.java:125-129`).
- The deck sequence depends only on the seed, never on the players' actions. This is what makes
  duplicate evaluation possible (`eval/`).
- `sim/poker_sim.py` contains a byte-exact Python SHA1PRNG, including Java's signed-byte carry.
- Replays record the seed exactly (refereeInput `seed=...`). So any public replay can be rebuilt with
  every player's hole cards. `sim/reconstruct.py` wrote 22,916 decisions from 120 games
  (`data/decisions.jsonl.gz`) and 74,347 from all 381.
- On the live server, the seed is not known to the bots and SHA1PRNG output cannot be predicted.
  Deck prediction is not a possible exploit, and we do not pursue it.

## 9. Input protocol

Input arrives only when it is your turn (`InputSender.java:49-115`). Cards are written like `AS`,
`TD`, `2C`, joined by `_`.

| when | lines | notes |
|---|---|---|
| First turn only | `smallBlind`, `bigBlind`, `handNbByLevel`, `levelBlindMultiplier`, `buyIn`, `firstBigBlindId`, `playerNb`, `playerId` | always 5, 10, 10, 2, 4800/N, ... (`InputSender.java:53-65`) |
| Every turn | `round`, `handNb` | |
| | N lines `stack chipInPot` | `chipInPot` is that player's total for the **whole hand**, not the current street (`InputSender.java:70-73`) |
| | board | 5 cards, padded with `X` |
| | your 2 hole cards | |
| | `actionNb`, then lines `round hand playerId ACTION board` | Every action since your last turn, **your own last action first**. Shown after replacement. `BET_x` carries an amount; `ALL-IN` does not. A no-decision hand is `-1 NONE`. (`InputSender.java:84-95`; `RoundInfo.java:25-29`) |
| | `showDownNb`, then lines `hand board cards` | One line per hand that ended since your last turn; `cards` is seat 0's two cards, then seat 1's, ..., all joined by `_` (`InputSender.java:97-107`; `ShowDownInfo.java:55-74`) |
| | `possibleActionNb`, then the actions | e.g. `CALL`, `ALL-IN`, `BET_240`, `CHECK`, `FOLD` (`ActionUtils.java:28-61`) |

Output is one line: an action, optionally followed by `;message`.

Points that matter for a state tracker:

- **The snapshot is not enough.** The `stack chipInPot` lines are taken at our turn. Several
  opponent actions, across several streets, can arrive between two of our turns, and `ALL-IN` lines
  carry no amount. Street-level bets and ALL-IN amounts must therefore be recovered by replaying
  each hand with the engine from start-of-hand stacks. Deltas of the snapshot do not work (research
  critique).
- **Every hand settles.** Hands in which we made no decision still have to be settled from their
  showdown lines, so that start-of-hand stacks are always known.
- A bot makes at most 139 decisions per game in the 120-game set (median 60).

## 10. What each player learns

**Showdown lines.** Every showdown line shows **all hole cards of every player dealt into the
hand, including folded hands**, even when the hand ended without a showdown
(`ShowDownInfo.java:46-50`; `SHOW_FOLDED_CARDS`):

- `PlayerModel.reset` marks a player eliminated only at the start of the next hand
  (`PlayerModel.java:43`). So a player who busts in this hand is still shown.
- A player who was eliminated *before* the hand shows `E_E`.
- Board cards that were never dealt stay `X`.

After every hand, a bot therefore knows exactly which hand produced each opponent action. There is
no showdown-censoring bias (see [literature.md §6](literature.md#6-opponent-modelling-and-safe-exploitation)).

**Win-probability display.** The viewer shows each player's chance of winning. Bots never receive
it (`PlayerUI.java:137-147`). It is computed by exact enumeration of the remaining board with the
real hole cards, using a Java port of SKPokerEval (`win_percent/skeval/WinPercentUtils.java:100-205`).
Its lookup tables are 409,994 bytes of source, far over the 100k-character limit.

## 11. Stack depth by blind level

The average stack in BB when N players are alive, and M = stack / preflop pot (the pot is
(N+1)/2 BB):

| hands | SB/BB | avg stack (BB), N = 2 / 3 / 4 | M, N = 2 / 3 / 4 |
|---|---|---|---|
| 1-9 | 5/10 | 240 / 160 / 120 | 160 / 80 / 48 |
| 10-19 | 10/20 | 120 / 80 / 60 | 80 / 40 / 24 |
| 20-29 | 20/40 | 60 / 40 / 30 | 40 / 20 / 12 |
| 30-39 | 40/80 | 30 / 20 / 15 | 20 / 10 / 6 |
| 40-49 | 80/160 | 15 / 10 / 7.5 | 10 / 5 / 3 |
| 50-59 | 160/320 | 7.5 / 5 / 3.75 | 5 / 2.5 / 1.5 |
| 60-69 | 320/640 | 3.75 / 2.5 / 1.88 | 2.5 / 1.25 / 0.75 |
| 70-79 | 640/1280 | 1.88 / 1.25 / 0.94 | 1.25 / 0.62 / 0.38 |

The per-hand table is `analysis/stack_depth_table.txt`, from `python3 analysis/stack_depth.py theory`.

The **median effective stack** at real decisions, in BB, for hands 1-10, 11-20, ..., 61-70
(reproduced with `analysis/stack_depth.py empirical`, 120 games):

| start | 1-10 | 11-20 | 21-30 | 31-40 | 41-50 | 51-60 | 61-70 |
|---|---|---|---|---|---|---|---|
| 2p | 237 | 113 | 49.1 | 23.2 | 11.1 | 4.6 | 2.2 |
| 3p | 157 | 73.5 | 34.6 | 17.4 | 8.2 | 4.5 | 2.1 |
| 4p | 117.5 | 55.5 | 27.6 | 14.7 | 9.1 | 5.7 | 2.5 |

## 12. Game length and where games are decided

All of these are reproduced by the `analysis/` scripts named in each line.

**Game length.**

- 120 recent games of top agents, fetched 2026-09-30 (`tools/sim_games.txt`;
  `analysis/replay_stats.py`):

  | start | games | hands: mean / median / range | rounds: mean / max |
  |---|---|---|---|
  | 2p | 27 | 35.9 / 42 / 3-70 | 124 / 230 |
  | 3p | 45 | 44.5 / 46 / 16-64 | 191 / 311 |
  | 4p | 48 | 46.4 / 46 / 24-70 | 229 / 339 |

- 371 games (`analysis/replay_stats.py analysis371`): median 40 / 47 / 46 hands. 0 games reached the cap.
- Actions per hand in 4-player games, by blind level: 7.3, 6.67, 4.88, 3.23, 2.44, 2.24, 1.61.
- Preflop decisions are 52% / 60% / 63% of all decisions for 2 / 3 / 4-player starts (61% overall,
  13,895 of 22,916).

**Most decisions are deep** (`analysis/stack_depth.py empirical`). The share of decisions at an
effective stack of 15 BB or less is 11.8% / 15.8% / 17.4% for 2 / 3 / 4-player starts. The share
above 50 BB is 64% / 52% / 51%.

**Most eliminations happen short** (`analysis/elim_depth.py`, 120 games). This is the eliminated
player's start-of-hand stack:

| start | eliminations | ≤10 BB | ≤20 BB | median |
|---|---|---|---|---|
| 2p | 27 | 44% | 70% | 10.9 BB |
| 3p | 90 | 54% | 79% | 8.4 BB |
| 4p | 144 | 53% | 69% | 9.7 BB |

**How hands end** (`analysis/hand_outcomes.py`, 120 games):

| hands | fold preflop | showdown | preflop all-in |
|---|---|---|---|
| 1-9 | 44% | 19% | 6% |
| 10-19 | 48% | 18% | 7% |
| 20-29 | 52% | 14% | 8% |
| 30-39 | 67% | 17% | 26% |
| 40-49 | 68% | 25% | 48% |
| 50-59 | 66% | 28% | 54% |

**Passive bots reach the cap** (`analysis/stack_depth.py policy`, 200 simulated games each). Four
calling stations hit the 600-round cap 100% of the time, at about hand 38. Three stations hit it
100% of the time, at about hand 51. Heads-up stations hit it 3% of the time. The league-1 boss
template prints `CALL;CALLING STATION`
(`third_party/CodingamePoker/CodingamePoker/config/Boss.java:54`), but the live boss behaves
differently ([field.md §4](field.md#4-how-the-top-bots-play)).

## 13. Design implications

1. **Solve for the real blind structure.** Every non-BB player posts the SB, so standard charts
   are miscalibrated here. Compute push/fold and open/defend strategies for N = 2, 3, 4 live
   players with exactly this pot.
2. **There are two regimes.** About half of all decisions happen above 50 BB. But 69-79% of
   eliminations happen at 20 BB or less, and from hand 40 on about half of all hands contain a
   preflop all-in. The short-stack preflop game is small, nearly solvable, and decides most
   outcomes.
3. **Optimise placement, not chips.** Use ICM or TrueSkill-payout utilities at 3-4 players (see
   [literature.md](literature.md)). Track `round`: at the cap the unfinished hand is refunded and
   stacks decide the ranking.
4. **Opponent data comes exactly labelled.** Every hand reveals every hole card. Each opponent
   gives about 40-110 labelled decisions per game. Nothing carries over between games, so the prior
   must come from the population.
5. **Engine fidelity.** A C++ engine must reproduce every quirk:
   - every non-BB player posts the SB, and short posters are forced all-in;
   - the preflop call is at least 1 BB;
   - the minimum raise is measured from the last full raise level, and only the last full raiser
     is locked after a short all-in;
   - the raise cap, side pots and the odd chip;
   - NONE rounds, and cancellation and refund at round 600.

   `sim/poker_sim.py` and `sim/validate_replays.py` are its regression oracle.
6. **A small output set is enough.** Every output is made legal by replacement, so FOLD,
   CHECK/CALL, the minimum raise, fractions of the pot, and ALL-IN cover everything. Follow the
   practical rules in §4.
7. **Time.** At most 139 decisions per game at 50 ms each, plus 1000 ms on the first turn. A
   timeout eliminates the bot, so keep hard time guards (see [engineering.md](engineering.md)).
8. **Evaluation.** The deck depends only on the seed, so duplicate seeds with rotated seats reduce
   variance in local testing (`eval/`).

## 14. Citation corrections

The research text (`research[0]` and `research[2]`) cited a few lines that do not match
`ac30d97`. Corrected here:

| claim | research cited | actual |
|---|---|---|
| Blind and buy-in constants | `Parameter.java:14-19` | `Parameter.java:7-12` |
| `RAISE_CAP = 10` | `Parameter.java:24` (research[0]) | `Parameter.java:17` (research[2] had this right) |
| `SHOW_FOLDED_CARDS = true` | `RefereeParameter.java:12` (research[0]) | `RefereeParameter.java:13` |
| Minimum-raise hint | `ActionUtils.java:352-356` | `ActionUtils.java:49-53` (the file has 204 lines) |
| Action replacement | `ActionUtils.java:411-505` | `ActionUtils.java:108-202` |
| Last full raiser locked | `ActionUtils.java:443-448, 462-464` | `ActionUtils.java:45`, `140-145`, `159-161` |
| Unparseable output becomes FOLD | `ActionInfo.java:78-79` | `ActionInfo.java:76-82` (the same code) |

Every other citation used here was checked and matches:

- `Board.java`: 146-192, 204-230, 232-275, 317-329, 468-479, 554-587, 604-623
- `Referee.java`: 60, 64, 106-131, 155, 161-179, 284-298
- `Game.java:148-150`
- `InputSender.java`: 21, 30, 49-115
- `ShowDownInfo.java:46-50`
- `WinningCalculator.java:33-104`
- `Deck.java`: 17-25, 60-65
- `FiveCardHand.java:42-77`
- `Action.java:43-64`
- `ActionType.java:6-12`
- SDK: `GameManager.java`: 38, 202, 639-642; `MultiplayerGameManager.java:57-62`

Open questions about the platform (whether bots can think between turns, the real timeout margin,
how matchmaking picks N, the C++ standard) are tracked in [`../open_questions.md`](../open_questions.md).
