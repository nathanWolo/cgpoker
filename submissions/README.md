# submissions/

Built CodinGame submission files, kept so they can be fetched from GitHub without a build
environment. Regenerate with `make bot` (`tools/bundle.py --minify`).

| file | what | built from |
|---|---|---|
| `m0_probe_min.cpp` | M0 baseline bot, minified (paste into CodinGame). `DEBUG = true`: prints the platform probes and per-turn latency to stderr; `PONDER = false`. | commit 5790491 |
| `m0_probe_bundled.cpp` | the same bot, readable (local includes inlined, not minified) | commit 5790491 |
| `m0_1_min.cpp` | M0.1: ICM for big calls with 3-4 alive, jam/fold heads-up to 12 BB, range beliefs, 15 ms compute budget; `DEBUG = true`, `PONDER = false`. Arena: +0.026 payout/game over M0. | this commit |
| `m0_1_bundled.cpp` | the same, readable | this commit |

To run the pondering probe, set `PONDER = true` near the top of `bot/main.cpp` (or in the bundled
file: `const bool PONDER = true;`) and rebuild or paste the edited bundled file; it is under the cap
unminified too.
