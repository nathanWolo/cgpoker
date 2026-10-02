"""Freeze a bot version as one header with its own namespaces, so the arena can hold several versions at once.

    python3 arena/freeze.py                                   # bot/bot.hpp -> bot/bot_prev.hpp, namespace prev
    python3 arena/freeze.py --commit 40d4864 --ns m1          # M1 from git -> build/arena/frozen/m1.hpp
    python3 arena/freeze.py [--commit SHA | --dir DIR] [--ns NAME] [--out PATH]

The bot's own headers (bot/*.hpp, included by name) are inlined recursively, in include order, each once, and
their namespaces renamed: bot -> NAME, and pf / pfn_t / hu_t -> pf_NAME / pfn_t_NAME / hu_t_NAME, so the frozen
bot keeps its own tables when dev's change.  Shared headers (engine, evaluator, ICM) stay shared: their include
paths are rewritten relative to the output file.  --commit reads the bot/ files of that commit (git show), --dir
a directory holding them; the default is the working tree.  Hill-climbing loop: freeze, edit only bot.hpp,
prove correctness, SPRT dev against prev (docs/plan.md).
"""
import argparse, os, re, subprocess
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
RENAMED = ("pf", "pfn_t", "hu_t")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--commit"); ap.add_argument("--dir"); ap.add_argument("--ns", default="prev"); ap.add_argument("--out")
    a = ap.parse_args()
    out = a.out or (os.path.join(REPO, "bot", "bot_prev.hpp") if a.ns == "prev" and not a.commit else
                    os.path.join(REPO, "build", "arena", "frozen", f"{a.ns}.hpp"))

    def read(name):
        if a.commit:
            return subprocess.check_output(["git", "-C", REPO, "show", f"{a.commit}:bot/{name}"]).decode("utf-8")
        return open(os.path.join(a.dir or os.path.join(REPO, "bot"), name), encoding="utf-8").read()

    done = set()
    inc = re.compile(r'^[ \t]*#[ \t]*include[ \t]+"([^"]+)"[ \t]*$', re.M)
    out_dir = os.path.dirname(os.path.abspath(out))

    def expand(name):
        text = re.sub(r"^\s*#\s*pragma\s+once\s*\n", "", read(name), count=1)
        def sub(m):
            path = m.group(1)
            if "/" not in path:                                  # a bot/ header: inline it once
                if path in done or path.startswith("bot_prev"): return ""
                done.add(path)
                return expand(path)
            target = os.path.normpath(os.path.join(REPO, "bot", path))   # shared header: same file, new relative path
            return f'#include "{os.path.relpath(target, out_dir)}"'
        return inc.sub(sub, text)

    done.add("bot.hpp")
    src = expand("bot.hpp")
    src = re.sub(r"\bnamespace bot\b", f"namespace {a.ns}", src)
    src = re.sub(r"\bbot::", f"{a.ns}::", src)
    for ns in RENAMED:
        src = re.sub(rf"\bnamespace {ns}\b", f"namespace {ns}_{a.ns}", src)
        src = re.sub(rf"\b{ns}::", f"{ns}_{a.ns}::", src)
    what = f"commit {a.commit}" if a.commit else (a.dir or "bot/")
    head = (f"#pragma once\n// FROZEN copy of the bot ({what}) made by arena/freeze.py; do not edit.\n"
            f"// Namespaces renamed (bot -> {a.ns}, {', '.join(f'{x} -> {x}_{a.ns}' for x in RENAMED)}) so the arena can hold several versions.\n")
    os.makedirs(out_dir, exist_ok=True)
    open(out, "w", encoding="utf-8").write(head + src)
    print("wrote", os.path.relpath(out, REPO), f"({len(done) - 1} bot headers inlined)")


if __name__ == "__main__":
    main()
