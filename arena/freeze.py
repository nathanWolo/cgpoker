"""Freeze the current bot as bot/bot_prev.hpp, the arena's reference opponent ("prev").

    python3 arena/freeze.py [bot.hpp to freeze]      (default: the current bot/bot.hpp)

The arena compiles dev (bot/bot.hpp, namespace bot) and prev (bot/bot_prev.hpp, namespace prev) into
one binary, so the frozen copy gets its own namespaces: bot -> prev, and the jam/fold tables are
inlined as pf_prev.  Shared headers (engine, evaluator) stay shared.  Hill-climbing loop: freeze,
edit only bot.hpp, prove correctness, SPRT dev against prev (docs/plan.md).
"""
import os, re
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
BOT = os.path.join(REPO, "bot")

import sys
SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.join(BOT, "bot.hpp")   # e.g. a `git show <commit>:bot/bot.hpp` dump
src = open(SRC, encoding="utf-8").read()
pf = ""
for name in ("pf_rank.hpp", "pf_tables.hpp"):
    t = open(os.path.join(BOT, name), encoding="utf-8").read()
    pf += re.sub(r"^\s*#\s*pragma\s+once\s*\n", "", t, count=1).replace("namespace pf", "namespace pf_prev")
    src = src.replace(f'#include "{name}"\n', "")
# code headers that live in namespace bot (hu_play.hpp) are inlined and renamed with it; pure table headers
# (pfn_tables.hpp, hu_tables.hpp: namespaces pfn_t, hu_t) stay shared with dev
for name in ("hu_play.hpp",):
    if f'#include "{name}"\n' in src:
        t = open(os.path.join(BOT, name), encoding="utf-8").read()
        t = re.sub(r"^\s*#\s*pragma\s+once\s*\n", "", t, count=1)
        src = src.replace(f'#include "{name}"\n', t)
src = src.replace("namespace bot", "namespace prev").replace("pf::", "pf_prev::")
src = re.sub(r"^\s*#\s*pragma\s+once\s*\n", "", src, count=1)
head = ("#pragma once\n// bot_prev.hpp - FROZEN copy of bot/bot.hpp made by arena/freeze.py; do not edit.\n"
        "// Namespaces renamed (bot -> prev, pf -> pf_prev) so the arena can hold both versions.\n")
out = os.path.join(BOT, "bot_prev.hpp")
open(out, "w", encoding="utf-8").write(head + pf + src)
print("wrote", os.path.relpath(out, REPO))
