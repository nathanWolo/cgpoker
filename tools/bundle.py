"""Bundle bot/main.cpp into one CodinGame source file.

    python3 tools/bundle.py                      # -> build/cg/poker_bundled.cpp (readable, local includes inlined)
    python3 tools/bundle.py --minify             # also build/cg/poker_min.cpp via tools/cg_minify.py (crossfish's, + --keep)

Inlines repository-local quoted includes recursively (each file once, dropping its `#pragma once`),
leaves system includes alone, and reports the character count against CodinGame's 100k cap
(counted in UTF-16 code units, which is len() of the Python str).  --minify runs tools/cg_minify.py
(crossfish's minifier: identifier renaming, whitespace packing) and checks that the result still
compiles with CodinGame's flags.
"""
import argparse, os, re, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
INC = re.compile(r'^\s*#\s*include\s*"([^"]+)"\s*(?://.*)?$')
CG_FLAGS = ["-std=gnu++20", "-Werror=return-type", "-g", "-pthread"]   # CodinGame reports __cplusplus=202002
CG_LIBS = ["-lm", "-lpthread", "-ldl", "-lcrypt"]


def inline(path, seen):
    out = []
    base = os.path.dirname(path)
    for line in open(path, encoding="utf-8"):
        m = INC.match(line.rstrip("\r\n"))
        if not m:
            out.append(line)
            continue
        p = os.path.realpath(os.path.join(base, m.group(1)))
        if not os.path.isfile(p):
            out.append(line)
            continue
        if p in seen:
            continue
        seen.add(p)
        nested = inline(p, seen)
        nested = re.sub(r"^\s*#\s*pragma\s+once\s*(?:\r?\n|$)", "", nested, count=1)
        out.append(f"// ---- {os.path.relpath(p, REPO)}\n" + nested + ("\n" if not nested.endswith("\n") else ""))
    return "".join(out)


def compile_check(src, exe):
    cmd = ["g++"] + CG_FLAGS + ["-o", exe, src] + CG_LIBS
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        print(r.stderr[:4000]); return False
    return True


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--src", default=os.path.join(REPO, "bot", "main.cpp"))
    ap.add_argument("--out-dir", default=os.path.join(REPO, "build", "cg"))
    ap.add_argument("--minify", action="store_true")
    a = ap.parse_args()
    os.makedirs(a.out_dir, exist_ok=True)
    bundled = inline(os.path.realpath(a.src), {os.path.realpath(a.src)})
    bp = os.path.join(a.out_dir, "poker_bundled.cpp")
    open(bp, "w", encoding="utf-8").write(bundled)
    ok = compile_check(bp, os.path.join(a.out_dir, "poker_bundled"))
    print(f"{os.path.relpath(bp, REPO)}: {len(bundled):,} chars ({len(bundled.encode()):,} bytes), "
          f"compiles with CodinGame flags: {ok}, cap 100,000")
    if a.minify:
        mini = os.path.join(HERE, "cg_minify.py")
        mp = os.path.join(a.out_dir, "poker_min.cpp")
        keep = "steady_clock,time_point,duration,milli,duration_cast,memory_order_relaxed,atomic,thread"
        r = subprocess.run([sys.executable, mini, bp, "-o", mp, "--keep", keep], capture_output=True, text=True)
        print(r.stdout.strip() or r.stderr.strip()[-2000:])
        if r.returncode:
            sys.exit(1)
        text = open(mp, encoding="utf-8").read()
        ok2 = compile_check(mp, os.path.join(a.out_dir, "poker_min"))
        print(f"{os.path.relpath(mp, REPO)}: {len(text):,} chars, compiles with CodinGame flags: {ok2}")
        ok = ok and ok2
    sys.exit(0 if ok else 1)
