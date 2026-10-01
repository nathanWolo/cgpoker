# Third-party code and ideas in `cpp/`

## OMPEval (ISC license): method used by `pe7c.hpp` and `pe7.hpp`

- Project: OMPEval by Timo A. (zekyll), <https://github.com/zekyll/OMPEval>, commit
  `4aec210ff75b0851af0ee170b35a7899e1a4fe8f`.
- What we use: no OMPEval source files are in this repository. `pe7c.hpp` and `pe7.hpp` are
  a separate implementation of OMPEval's method, and they copy its 13 rank-key constants
  (`RANKS[]`, our `RK[]`). Also from OMPEval: the hand layout (the rank-key sum, 4-bit suit
  counters that start at 3 so `& 0x8888` detects a flush, and per-suit 16-bit rank masks),
  the 8,192-entry flush lookup, and the row-displacement perfect hash from rank key to
  hand class (`PERF_HASH_ROW_OFFSETS`). Each header says this at the top.
- `fetch_third_party.sh` clones OMPEval into `cpp/third_party/` (gitignored) only so that
  `cmp.cpp` can check our results against it.

OMPEval's license notice, reproduced as its license requires:

```text
ISC License
Copyright (c) 2016, Timo A.
Permission to use, copy, modify, and/or distribute this software for any purpose with or without fee is hereby granted, provided that the above copyright notice and this permission notice appear in all copies.
THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
```

## PokerHandEvaluator (Apache-2.0): comparison only

- Project: PokerHandEvaluator by Henry Lee (HenryRLee), <https://github.com/HenryRLee/PokerHandEvaluator>,
  commit `10be452e4c1ee40a6a56f06457f46bff27ca495a`.
- We use nothing from it in our own code. `fetch_third_party.sh` does a sparse clone of its
  LICENSE and the seven-card C sources into `cpp/third_party/` (gitignored). `make cmp` links
  `evaluate_7cards` from those sources to check ordering and compare speed. The fetched
  sources keep their own Apache-2.0 LICENSE file.
