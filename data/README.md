# data/

Public CodinGame Poker data fetched on 2026-09-30 from CodinGame's public but undocumented web API,
plus one derived file. **Check CodinGame's terms of service before any bulk harvesting**
(`docs/open_questions.md` Q-C1). Everything here is committed except `cache/`.

| path | what | used by |
|---|---|---|
| `replays/<gameId>.json.gz` | 381 replays, each the `gameResult/findByGameId` response (gzip, `mtime=0`). The 371 games of `tools/sel_games.txt` are stored byte-for-byte as returned; the 10 games that are only in `tools/sim_games.txt` were saved by the research run after a Python `json.dump` round trip (same content, different whitespace and `\u` escaping). Each holds the referee seed (`refereeInput`), every frame with the bots' stdout, and the final scores and ranks. The id lists are `tools/sel_games.txt` (371) and `tools/sim_games.txt` (120); see `tools/README.md`. | `sim/`, `replayer/`, `analysis/` |
| `battles/<nick>.json.gz` | The recent battle lists (`gamesPlayersRanking/findLastBattlesByTestSessionHandle`) of the top 37 agents: 69-443 battles each, 2,495 unique finished games. Each battle lists its players with `position` (0 = first) and `submissionId`. | `analysis/pairwise.py`, `tools/fetch_replays.py` |
| `decisions.jsonl.gz` | 22,916 complete-information decision records from the 120 games in `tools/sim_games.txt`, written by `python3 sim/reconstruct.py --ids tools/sim_games.txt --gz` (`make reconstruct` checks it). Record format: `sim/README.md`. | parser and tracker tests (plan M0) |
| `snapshots/2026-09-30/cg_lb.json` | The full leaderboard (192 bots, both leagues): rank, score, league, language, submission `creationTime`, `agentId`, `testSessionHandle`, and each player's public CodinGame profile. | `analysis/leaderboard.py`, `analysis/pairwise.py`, `tools/fetch_replays.py top` |
| `snapshots/2026-09-30/lb_league0.json` | A second fetch of the same leaderboard a few minutes later. Ranks 1-42 are identical; 60 lower-league scores differ by up to 0.42 and a few lower-league ranks swap. Kept as fetched; no script reads it. | — |
| `snapshots/2026-09-30/cg_statement.txt` | The puzzle statement as plain text. | `docs/research/rules.md` |
| `snapshots/2026-09-30/cg_findProgressByPrettyId.json` | Puzzle metadata (`Puzzle/findProgressByPrettyId`): creation date, author, forum link, attempt count, the statement HTML. Its `viewer` field, CodinGame's compiled game-viewer JavaScript (174 KB), was replaced by a placeholder so that no CodinGame SDK or referee code is redistributed here; every other field is as fetched. | `docs/research/field.md` §1 |
| `cache/` | Regenerated artifacts (`replayed.pkl`, `stats.pkl`, `decisions.jsonl`, ...). Gitignored; rebuild with `make cache reconstruct`. | |

Two Waffle3z battle-list snapshots from the research run (`battles_waffle.json`, `lb_waffle.json`)
were byte-identical to `battles/Waffle3z.json.gz` and were not kept.

The leaderboard and battle lists contain other players' public profile data (pseudo, country, school,
company). That is one more reason this repository stays private.
