# play/: play against the bot in your browser

A local poker table: you against 1-3 copies of the bot, with CodinGame's rules, in a browser page served from
your own machine.

```sh
git clone https://github.com/nathanWolo/cgpoker.git && cd cgpoker      # no submodules needed for this
python3 play/server.py --open        # compiles the bot once (10-60 s), then opens http://localhost:8765
```

Needs Python 3.8+ and a C++ compiler: `g++` or `clang++` (macOS: `xcode-select --install`; set `CXX` to pick
one). Nothing to `pip install`. On Windows, use WSL, or a MinGW `g++` on the PATH. Stop the server with Ctrl+C.

| option | default | |
|---|---|---|
| `--bot FILE` | `submissions/m3_0_min.cpp` | which bot to play: any file in `submissions/` (e.g. `om1_min.cpp`, `m1_min.cpp`) |
| `--port N` | 8765 | |
| `--host H` | 127.0.0.1 | only this machine can connect; `0.0.0.0` lets others on your network play |
| `--delay S` | 0.8 | seconds a bot waits before acting (the page has a slider too) |
| `--open` | off | open the page in your default browser |

**What you get.** A table with your seat at the bottom and the bots around it; fold / check / call / raise (a
slider, min, ½ pot, ¾ pot, pot and 3× presets) / all-in, also from the keyboard (F, C, R, A; Space for the next
hand). A hand log, the finished hand with everyone's hole cards (the referee reveals them all, folded hands
too), and **what the bots were thinking**: for each of their decisions in the last hand, the rule that decided
it, their equity estimate against the range they put you on, and, heads-up, their opponent model's read of you
(how wide you raise and limp, how often you fold to raises and bets), which sharpens as the game goes on.

**How it works.** `server.py` runs the game with `sim/poker_sim.py`, the exact Python port of CodinGame's referee:
4,800 chips split evenly, blinds 5/10 doubling every 10 hands, every player but the big blind posts the small
blind, the 600-decision cap. Each bot seat is the compiled submission as its own process, fed the same stdin
CodinGame sends it and answering with one line, so this is the submitted bot itself, not a reimplementation.
The bots' per-decision diagnostics come from their stderr (`DEBUG` builds). Your seat waits for the page, which
polls `/api/state` and posts to `/api/act`, `/api/new`, `/api/continue` and `/api/settings`. A seed in the
new-game dialog replays the same deck.
