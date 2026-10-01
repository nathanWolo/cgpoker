"""Run a built submission file as a CodinGame-style process inside the Python referee port (sim/poker_sim.py).

    python3 tools/run_submission.py build/cg/poker_min.cpp [--compare build/cg/poker_bundled.cpp]
                                    [--games 3] [--players 3] [--seed 1] [--keep-stderr DIR]

Checks the file end to end: it compiles with CodinGame's flags, answers every decision over whole games
(stdin/stdout exactly as the referee sends them), how long its turns take, which decision tags it logs
(DEBUG builds), and with --compare how often a second file (the readable bundle) gives the same answer
to exactly the same stdin.  Answers are not bit-identical between builds because the Monte Carlo budget
is a time budget, so agreement a little under 100% is expected; the equilibrium charts are exact.
"""
import argparse, collections, os, random, subprocess, sys, tempfile, time
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(REPO, "sim"))
from poker_sim import PokerSim, obs_to_stdin  # noqa: E402

CG_FLAGS = ["-std=gnu++20", "-Werror=return-type", "-g", "-pthread"]


def build(src):
    out = os.path.join(REPO, "build", "cg", os.path.splitext(os.path.basename(src))[0] + ".exe")
    if not os.path.exists(out) or os.path.getmtime(out) < os.path.getmtime(src):
        subprocess.check_call(["g++", *CG_FLAGS, "-o", out, src])
    return out


class ProcAgent:
    def __init__(self, exe, err_path):
        self.err = open(err_path, "w")
        self.p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.err, text=True)
        self.first = True
        self.decisions = []      # (round, output)
        self.inputs = []         # the exact stdin chunks, for replaying to another build
        self.ms = []

    def __call__(self, obs):
        text = obs_to_stdin(obs, self.first)
        self.inputs.append(text)
        try:
            self.p.stdin.write(text)
            self.p.stdin.flush()
        except BrokenPipeError:
            return None
        self.first = False
        t0 = time.time()
        line = self.p.stdout.readline()
        self.ms.append(1000 * (time.time() - t0))
        if not line:
            return None
        out = line.strip()
        self.decisions.append((obs.round, out))
        return out

    def close(self):
        try:
            self.p.stdin.close()
        except Exception:
            pass
        self.p.wait(timeout=10)
        self.err.close()
        return self.p.returncode


def replay(exe, inputs, err_path):
    """Feed recorded stdin chunks to another build; -> its answers (identical inputs, so only the Monte Carlo time budget differs)."""
    err = open(err_path, "w")
    p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=err, text=True)
    outs = []
    for text in inputs:
        try:
            p.stdin.write(text); p.stdin.flush()
        except BrokenPipeError:
            break
        line = p.stdout.readline()
        if not line:
            break
        outs.append(line.strip())
    p.stdin.close(); p.wait(timeout=10); err.close()
    return outs


def station(obs):
    return "CALL"


def jammer(rng):
    def agent(obs):
        if obs.board.startswith("X") and rng.random() < 0.35:
            return "ALL-IN"
        return "CHECK" if "CHECK" in obs.possible else ("CALL" if obs.board[0] != "X" else "FOLD")
    return agent


def play(exe, n, seed, err_path):
    sim = PokerSim(n, seed)
    me = ProcAgent(exe, err_path)
    rng = random.Random(seed)
    agents = [me] + [station if i % 2 else jammer(rng) for i in range(1, n)]
    res = sim.run(agents)
    rc = me.close()
    return res, me, rc


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("--compare")
    ap.add_argument("--games", type=int, default=3)
    ap.add_argument("--players", type=int, default=3)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--keep-stderr", help="directory for the bot's stderr per game (default: a temp dir)")
    a = ap.parse_args()
    exe = build(a.src)
    exe2 = build(a.compare) if a.compare else None
    errdir = a.keep_stderr or tempfile.mkdtemp(prefix="cgpoker_")
    os.makedirs(errdir, exist_ok=True)
    tags = collections.Counter()
    total, same, ms_all, ms_max, fails = 0, 0, [], 0, 0
    for g in range(a.games):
        seed = a.seed + g
        res, me, rc = play(exe, a.players, seed, os.path.join(errdir, f"g{seed}.err"))
        none = sum(1 for d in me.decisions if d[1] is None)
        ms_all += me.ms; ms_max = max(ms_max, max(me.ms) if me.ms else 0)
        for line in open(os.path.join(errdir, f"g{seed}.err")):
            parts = line.split()
            for p in parts:
                if p in ("pf-sb", "pf-bb", "pfn-jam", "pfn-jam-lim", "pfn-call", "pfn-rejam", "icm", "open", "complete", "raise", "call", "callff", "bet", "check") or p.startswith("hu-"):
                    tags[p] += 1
        ok = rc == 0 and not none and len(me.decisions) > 0
        fails += not ok
        line = f"game {seed}: {a.players}p hands {res['hands']} decisions {len(me.decisions)} scores {res['scores']} exit {rc} mean turn {sum(me.ms)/max(1,len(me.ms)):.1f} ms"
        if exe2:
            outs = replay(exe2, me.inputs, os.path.join(errdir, f"g{seed}_cmp.err"))
            mine = [d[1] for d in me.decisions]
            agree = sum(1 for x, y in zip(mine, outs) if x == y)
            total += len(mine); same += agree
            line += f" | same answer on the same input: {agree}/{len(mine)}"
        print(line)
    print(f"turn time: mean {sum(ms_all)/max(1,len(ms_all)):.1f} ms, max {ms_max:.1f} ms (includes pipe latency)")
    print("tags:", ", ".join(f"{k} {v}" for k, v in tags.most_common()))
    if exe2:
        print(f"agreement with {a.compare}: {same}/{total} = {100*same/max(1,total):.1f}%")
    print("stderr in", errdir)
    print("PASS" if not fails else f"FAIL ({fails} games with a crash or a missing answer)")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
