"""Freeze the current bot as bot/bot_prev.hpp, the arena's reference opponent ("prev").

    python3 arena/freeze.py

The arena compiles dev (bot/bot.hpp, namespace bot) and prev (bot/bot_prev.hpp, namespace prev) into
one binary, so the frozen copy gets its own namespaces: bot -> prev, and the jam/fold tables are
inlined as pf_prev.  Shared headers (engine, evaluator) stay shared.  Hill-climbing loop: freeze,
edit only bot.hpp, prove correctness, SPRT dev against prev (docs/plan.md).
"""
import os, re
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
BOT = os.path.join(REPO, "bot")

src = open(os.path.join(BOT, "bot.hpp"), encoding="utf-8").read()
pf = open(os.path.join(BOT, "pf_tables.hpp"), encoding="utf-8").read()
pf = re.sub(r"^\s*#\s*pragma\s+once\s*\n", "", pf, count=1).replace("namespace pf", "namespace pf_prev")
src = src.replace('#include "pf_tables.hpp"\n', "")
src = src.replace("namespace bot", "namespace prev").replace("pf::", "pf_prev::")
src = re.sub(r"^\s*#\s*pragma\s+once\s*\n", "", src, count=1)
head = ("#pragma once\n// bot_prev.hpp - FROZEN copy of bot/bot.hpp made by arena/freeze.py; do not edit.\n"
        "// Namespaces renamed (bot -> prev, pf -> pf_prev) so the arena can hold both versions.\n")
out = os.path.join(BOT, "bot_prev.hpp")
open(out, "w", encoding="utf-8").write(head + pf + src)
print("wrote", os.path.relpath(out, REPO))
