# sim/: Python port of the CodinGame Poker referee

`poker_sim.py` is a Python port of the referee
([wala-fr/CodingamePoker](https://github.com/wala-fr/CodingamePoker) @ `ac30d97`, also at
`third_party/CodingamePoker`). It follows `Referee.gameTurn` frame by frame and reproduces the
`SecureRandom("SHA1PRNG")` deck bit-exactly. Given a replay's seed and the bots' recorded outputs, it
reproduces the game exactly, including every player's hole cards. Pure Python 3, no dependencies.

| file | what |
|---|---|
| `poker_sim.py` | `PokerSim(n, seed).run(agents)`; each agent is a callable `Obs -> "CALL" \| "BET 200" \| ... \| None` (None = timeout). `obs_to_stdin` renders the exact stdin a bot receives. |
| `validate_replays.py` | Differential test against real games. It checks who acts at every step, that all recorded outputs are consumed, and the final scores. |
| `reconstruct.py` | Replays → complete-information decision records (JSONL, one per bot decision). |
| `replay_io.py` | Shared helpers: find and load `data/replays/<gameId>.json.gz`, and extract the recorded actions, including timeouts. |

## Run (from the repo root)

```sh
python3 sim/validate_replays.py                    # all data/replays (381 games), ~5 s; exit 1 on any mismatch
#   381/381 replays reproduced exactly (turn order + final scores)
python3 sim/validate_replays.py --ids tools/sim_games.txt      # the original 120-game set -> 120/120
python3 sim/validate_replays.py 906358479          # ids or paths; mismatches are printed as "MISMATCH {...}"

python3 sim/reconstruct.py                         # -> data/cache/decisions.jsonl (gitignored)
#   decisions written: 74347 from 381 games -> data/cache/decisions.jsonl
python3 sim/reconstruct.py --ids tools/sim_games.txt --gz      # rebuilds the committed data/decisions.jsonl.gz
#   decisions written: 22916 from 120 games -> data/decisions.jsonl.gz

python3 sim/poker_sim.py                           # demo: 3 calling stations hit the 600-round cap
#   {'hands': 51, 'rounds': 600, 'cancelled': True, 'scores': [490, 1360, 2950], ...}
```

`data/decisions.jsonl.gz` (committed) holds the 22,916 decisions from the 120 games in
`tools/sim_games.txt`. It was written by `reconstruct.py --ids tools/sim_games.txt --gz` (gzip
`mtime=0`), so rerunning that command with the same zlib rewrites identical bytes, and the
decompressed JSONL is identical in any case; `make reconstruct` checks the decompressed content.
The all-381 output (74,347 records) is written to `data/cache/` only.

Record fields: `game n player name round hand sb bb dealer bb_id stacks chip_in_pot board cards
all_hole_cards possible output`. `stacks` and `chip_in_pot` are what the bot saw on stdin. `all_hole_cards`
holds every seat's cards, or null for an eliminated seat. `output` is the bot's raw action text before the
referee's replacement rules, with any `;message` removed.

## Validation history

- The original scratch run was 120/120 on the 120-game set.
- On all 381 games the harness as first copied scored 379/381. Both failures were games where a bot
  timed out on its first turn (906358479 deuwii, 906523528 [CG]CohereCommandA-Python). The harness
  never passed those timeouts to the sim; the port itself was fine.
- `replay_io.recorded_actions` now reads the timed-out player from the summary line
  ("`$<id> did not output in time!`"), the same way `replayer/replay_game.py` does, and passes `None`.
  With that change all 381 games reproduce. `reconstruct.py` writes no record for a timed-out turn.
- None of the 381 games reaches the 600-round cap. The cap path is covered only by the demo above and
  by differential runs against the Java replayer (`replayer/README.md`).

## Rule quirks the port reproduces

Full spec with referee line references: [docs/research/rules.md](../docs/research/rules.md).

- **Every non-BB player posts the small blind** (`Board.initBlind`). The preflop pot is 1.5/2/2.5 BB
  with 2/3/4 players. Blinds start at 5/10 and double every 10 hands. The stack is 4800/N each.
- **A round is one bot decision, counted across the whole game.** At round 600 the unfinished hand is
  cancelled and refunded, and survivors score their stacks. A hand where nobody can act (all-in from
  the blinds) still uses one round and is logged as `NONE`. If that happens at round 601, the Java
  referee crashes with an array index error; the port raises `RuntimeError` instead.
- **`BET x` puts in x more chips.** It does not mean "raise to x". Illegal actions are replaced,
  not rejected: CALL becomes CHECK or ALL-IN, a BET at or below the call becomes CALL, an undersized
  raise becomes the minimum raise, and anything unparseable becomes FOLD. Raises are capped at
  `raiseNb > 10`. After a short all-in, only the last full raiser is limited to call or fold.
- **Side pots are split in layers.** The odd chip goes to the first winner after the dealer.
- **Scores.** A busted player scores `eliminationRank - N`. Players busting in the same hand are
  ordered by the chips they put in that hand; exact ties share a score. Survivors score their stacks.
- **A timeout** sets the stack to 0 and folds the bot, which eliminates it.
- **RNG.** The SDK's `SecureRandom("SHA1PRNG")` is seeded with the replay seed. It is not
  `java.util.Random`. It picks the first BB with `nextInt(N)`, then each hand does a
  `Collections.shuffle` plus a random cut.
