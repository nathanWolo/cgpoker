# M2.2 experiment (2026-10-01, overnight): merged postflop tables and a second bet size

Not shipped. Kept here because the merged representation is the way to richer abstractions later.

- `export_hu_merged.py`: exports preflop nodes per stack point (as the shipped exporter does) but
  postflop nodes merged on (street, pot bucket, stack-behind bucket, actions available, this
  street's history), pot and stack on a log-1.25 grid, probabilities averaged over the nodes sharing
  a key. With the shipped one-size solution: 866 keys from 2,856 nodes, postflop 34.8k to 20.7k bytes,
  the submission 91.4k to 85.3k characters.
- `hu_play_merged.hpp` and `bot_hpp_two_sizes.diff`: the bot side. Postflop lookups by the key
  computed from the real pot and stacks (the opponent's bet mapped to half pot / pot / all-in by the
  nearer size in log terms), a pot-sized bet 'b' in the rebuilt tree and in the history mapping.
- The solver keeps the pot-sized bet behind `hu --two-sizes`.

Measured, paired against M2.1 (perfect-recall one-size tables, 150 BB gate):

| variant | 2-player games | all sizes |
|---|---|---|
| merged tables, same one-size solution | −0.000 ± 0.008 (4,000 games) | |
| merged tables, two-size solution, 400M iterations | **−0.011 ± 0.006** (8,000 games, SPRT fail) | +0.005 ± 0.003 |

So the merge is harmless and the second bet size, at the same iteration budget, is worse: the tree is
2.5× larger (2,088 nodes / 9,810 information sets at 20 BB against 528 / 3,810) and converges less,
and 5.4 nodes are averaged per key against 3.3. To retry: more iterations (the 9-stack two-size
solve took 23 min at 400M), measure exploitability properly (`hu solve S ITERS POOL 1000000`), and
compare at equal convergence.
