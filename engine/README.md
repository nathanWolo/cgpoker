# engine/: the C++ referee port

`poker_engine.hpp` is the C++ port of the CodinGame Poker referee, written method for method after
[`sim/poker_sim.py`](../sim/README.md), the validated Python port. It is the engine for the local
arena, the training environment and, through `Board`, the bot's own state tracker.

| file | what |
|---|---|
| `sha1prng.hpp` | `SecureRandom("SHA1PRNG")`, `Random.nextInt(bound)` and `Collections.shuffle`, bit-exact |
| `poker_engine.hpp` | `pk::Board` (rules: blinds, betting, action replacement, side pots, elimination ranks) and `pk::Engine` (deck, inputs, the `Referee.gameTurn` loop); `pk::Obs::to_stdin` renders the exact stdin a bot gets |
| `tracker.hpp` | `pk::Tracker`: the bot-side state tracker, a `Board` driven by the stdin lines, with per-turn assertions against the stdin snapshot |
| `check.cpp`, `check.py` | the differential tests below |

## Checks

```sh
python3 engine/check.py all              # = make engine-check (~1 min)
python3 engine/check.py fuzz --games 3000 --seed 7
python3 engine/check.py replays [ids...]
```

- **Fuzz against `poker_sim.py`.** The C++ engine plays random-action games (2-4 players; wild,
  passive, aggressive, tight, calling-station and limper styles; 3% odd or illegal outputs such as
  `BET  30`, `BET -5`, `RAISE 20`, `CALL 10`, `ALL_IN`, `BET 99999999999`, empty lines; 0.2%
  timeouts) and records every raw output. `poker_sim.py` replays the outputs. Every
  post-replacement action, hand and round count, cancellation and final score must match.
  Observed (seed 7, 3,000 games): 3,000/3,000 identical; 633,448 decisions; 106,596 hands; 342
  games hit the 600-decision cap; 2 games reached the referee's NONE-round-at-601 crash (both ports
  raise); 295 timeouts; 334 NONE rounds; all five action types and TIMEOUT seen.
- **Recorded games.** All 381 replays in `data/replays/` through the C++ engine: 381/381 reproduce
  the turn order and CodinGame's final scores.
- **Tracker.** In both of the above, a `Tracker` per seat is fed the exact stdin text and must
  agree with the engine every turn (stacks, chips in pot, board, round, hand, the offered actions
  including the minimum raise). See `tracker.hpp`.

Cards are ints 0..51 = 4·rank + suit (rank 0..12 = 2..A, suit C,D,H,S), the referee's `CardUtils`
order and `cpp/pe7c.hpp`'s encoding. Hand evaluation in the engine is the simple best-of-21
`eval5`, with the same ordering as the Python port; the arena may swap in `pe7c`.
