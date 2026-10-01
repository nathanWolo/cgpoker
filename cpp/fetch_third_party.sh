#!/usr/bin/env bash
# Fetch the two evaluators that cmp.cpp checks pe7c.hpp against into cpp/third_party/
# (gitignored; nothing from them is committed).  Pinned to the commits the research numbers
# were measured with.  Only `make cmp` needs them.
#   OMPEval             https://github.com/zekyll/OMPEval             ISC license
#   PokerHandEvaluator  https://github.com/HenryRLee/PokerHandEvaluator  Apache-2.0 license
# PokerHandEvaluator's full checkout is ~700 MB (PLO tables), so only the LICENSE and the
# seven-card C sources are checked out (sparse, shallow, blobless).
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p third_party

OMP_COMMIT=4aec210ff75b0851af0ee170b35a7899e1a4fe8f
PHE_COMMIT=10be452e4c1ee40a6a56f06457f46bff27ca495a

# fetch <dir> <url> <commit> [sparse path patterns...]
fetch() {
  local dir=third_party/$1 url=$2 commit=$3; shift 3
  if [ -d "$dir/.git" ] && [ "$(git -C "$dir" rev-parse HEAD 2>/dev/null)" = "$commit" ]; then
    echo "$dir already at $commit"; return
  fi
  rm -rf "$dir"
  git init -q "$dir"
  git -C "$dir" remote add origin "$url"
  if [ $# -gt 0 ]; then git -C "$dir" sparse-checkout set --no-cone "$@"; fi
  git -C "$dir" fetch -q --depth 1 --filter=blob:none origin "$commit"
  git -C "$dir" checkout -q FETCH_HEAD
  echo "$dir at $(git -C "$dir" rev-parse HEAD)"
}

fetch OMPEval https://github.com/zekyll/OMPEval.git "$OMP_COMMIT"
fetch PokerHandEvaluator https://github.com/HenryRLee/PokerHandEvaluator.git "$PHE_COMMIT" \
  /LICENSE /cpp/include/ \
  /cpp/src/evaluator7.c /cpp/src/hash.c /cpp/src/hash.h /cpp/src/hashtable.c \
  /cpp/src/hashtable7.c /cpp/src/dptables.c /cpp/src/tables.h /cpp/src/tables_bitwise.c
