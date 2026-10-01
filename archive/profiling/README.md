# archive/profiling/

Per-bot profiling from the 2026-09-30 research. Archived on 2026-10-01 because the current plan is
equilibrium-first: it does not profile, fingerprint or model individual live bots.

| path | what |
|---|---|
| `stats.py` | Per-player style table (VPIP/PFR, unopened/vs-raise/vs-all-in frequencies, postflop bet/fold, open sizes, shove depth, calling range). Writes `data/cache/stats.pkl`. |
| `stats3.py` | Open-size and bet-size fingerprints, fold rates against small opens and bets, first preflop action by depth, per player. |
| `field_profiles.md` | The "how the top bots play" and "common weaknesses" sections formerly in `docs/research/field.md`. |
| `oppmodel/` | Bot-library + online Dirichlet opponent model, with leave-one-game-out log-loss and identification rates (see its README). |

The scripts still run. They need `data/cache/replayed.pkl` (`make cache`):

```sh
python3 archive/profiling/stats.py
python3 archive/profiling/stats3.py
python3 archive/profiling/oppmodel/oppmodel2.py
```
