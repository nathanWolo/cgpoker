# tools/: submission bundling, replay download and game-id lists

## bundle.py and cg_minify.py

```sh
python3 tools/bundle.py --minify      # = make bot
```

`bundle.py` inlines `bot/main.cpp`'s repository-local includes into one file, `build/cg/poker_bundled.cpp`,
compiles it with CodinGame's flags, and with `--minify` runs `cg_minify.py` on it to produce
`build/cg/poker_min.cpp`, the file to paste into CodinGame (27k characters for the M0 bot). `cg_minify.py`
is crossfish's minifier (identifier renaming, whitespace packing) plus `--keep NAMES` for identifiers
it must not rename, which `bundle.py` uses for nested `std::chrono` names. See [`bot/README.md`](../bot/README.md).


## fetch_replays.py

Downloads public CodinGame Poker replays into `data/replays/<gameId>.json.gz`. It uses CodinGame's
undocumented web API, so **check CodinGame's terms of service before any bulk use**; the tool prints
the same reminder on every run.

- **Endpoints.** `gameResult/findByGameId [gameId, null]` returns one replay, with the referee seed and
  every bot's stdout. `gamesPlayersRanking/findLastBattlesByTestSessionHandle [handle, null]` returns an
  agent's recent battles.
- **Politeness.** Requests are single-threaded, at least 2 s apart (`--delay` can only raise this), and
  ids already in `data/replays` are skipped, so an interrupted run can simply be restarted.
- **Storage.** Each replay is stored exactly as returned, gzipped with `mtime=0`. A response without
  `frames` is reported and not saved.

```sh
python3 tools/fetch_replays.py ids --dry-run            # ids in tools/sel_games.txt that are missing locally
#   371 ids, 371 already in data/replays, 0 to fetch
python3 tools/fetch_replays.py ids --file tools/sim_games.txt
python3 tools/fetch_replays.py ids 906530651 906530518
python3 tools/fetch_replays.py top --agents 8 --per-agent 15 --dry-run
#   would list last battles of #1 Waffle3z (testSessionHandle 7733...) and fetch up to 15 new finished games
python3 tools/fetch_replays.py top --agents 37 --per-agent 0 --save-battles  # refresh data/battles/<nick>.json.gz only
```

`top` reads agents from a leaderboard snapshot. The default is
`data/snapshots/2026-09-30/cg_lb.json`; use `--leaderboard` for another. For each agent it keeps the
first `--per-agent` finished (`done`) battles that are not already downloaded.

This replaces the scratch `fetch_replays.sh` and `poker_research/fetch_replays.py`. The shell script ran 4 parallel
curls, 0.2 s apart, into a hard-coded scratch path. The Python script called `findLastBattlesByAgentId`
0.3 s apart. The new tool has only been checked with `--help`, `--dry-run` and a mocked-network test of
throttling, skipping and error handling. It has not been run against the live API.

## Game-id lists

`data/replays/` holds 381 games, the union of two lists:

| list | games | source | used for |
|---|---|---|---|
| `sel_games.txt` | 371 | Chosen from the 2,495 games in the battle lists of the top 37 agents (`data/battles/`) and fetched by the scratch `fetch_replays.sh`. The selection rule was not preserved. | `analysis/` (Java-replayer statistics) |
| `sim_games.txt` | 120 | Fetched by the scratch `poker_research/fetch_replays.py` (top 8 agents, up to 15 recent games each, 2026-09-30) | `sim/validate_replays.py` 120/120 and the committed `data/decisions.jsonl.gz` |
| `m1_games.txt` | 87 | M1's placement games (2026-10-01) | `analysis/postmortem.py`, `headsup.py`, `clone_fit.py schedule` |
| `m21_games.txt` | 84 | M2.1's placement games (2026-10-02) | the same, and `retrace.py` |
| `om1_games.txt` | 84 | OM1's placement games (our battle list, 2026-10-02) | `analysis/postmortem.py`, `retrace.py`, `om_impact.py`, `om_notes.py`, `clone_fit.py schedule` |

The two lists share 110 games. Every id in both lists appears in `data/battles/`.
