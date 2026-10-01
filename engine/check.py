"""Differential tests for engine/poker_engine.hpp against sim/poker_sim.py and the recorded games.

    python3 engine/check.py fuzz [--games 2000] [--seed 1]   random-action games: the C++ engine generates
                                                             the outputs, poker_sim.py replays them; every
                                                             post-replacement action, hand/round count,
                                                             cancellation and final score must match
    python3 engine/check.py replays [ids...] [--ids FILE]    all 381 recorded games through the C++ engine:
                                                             turn order and final scores must match CodinGame's
    python3 engine/check.py all                              both (the Makefile target)

Builds build/engine/check from engine/check.cpp when needed. Exit status 1 on any mismatch.
"""
import argparse, os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(REPO, "sim"))
from poker_sim import PokerSim                       # noqa: E402
from replay_io import load_replay, recorded_actions, replay_paths, CACHE_DIR   # noqa: E402

BIN = os.path.join(REPO, "build", "engine", "check")


def build():
    src = os.path.join(HERE, "check.cpp")
    hdrs = [os.path.join(HERE, "poker_engine.hpp"), os.path.join(HERE, "sha1prng.hpp")]
    if os.path.exists(BIN) and all(os.path.getmtime(BIN) >= os.path.getmtime(f) for f in [src] + hdrs):
        return
    os.makedirs(os.path.dirname(BIN), exist_ok=True)
    cmd = ["g++", "-std=gnu++17", "-O2", "-Wall", "-Wextra", "-o", BIN, src]
    print("$", " ".join(cmd))
    subprocess.check_call(cmd)


def esc(s):
    return s.replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n").replace("\r", "\\r")


def unesc(s):
    out, i = [], 0
    while i < len(s):
        if s[i] == "\\" and i + 1 < len(s):
            n = s[i + 1]
            out.append({"t": "\t", "n": "\n", "r": "\r"}.get(n, n)); i += 2
        else:
            out.append(s[i]); i += 1
    return "".join(out)


def parse_log(path):
    """-> list of dict(id, n, seed, acts[(pid, text|None)], S[(turn,hand,pid,shown)], R or None, X, O)"""
    games = []
    for line in open(path, encoding="utf-8"):
        line = line.rstrip("\n")
        tag, rest = line[:1], line[2:]
        if tag == "G":
            gid, n, seed = rest.split(" ")
            games.append(dict(id=gid, n=int(n), seed=int(seed), acts=[], S=[], R=None, X=None, O=None, T=(0, '')))
        elif tag == "A":
            pid, _, text = rest.partition(" ")
            games[-1]["acts"].append((int(pid), None if text == "\\0" else unesc(text)))
        elif tag == "S":
            t, h, p, shown = rest.split(" ", 3)
            games[-1]["S"].append((int(t), int(h), int(p), shown))
        elif tag == "R":
            v = rest.split(" ")
            games[-1]["R"] = dict(hands=int(v[0]), rounds=int(v[1]), cancelled=bool(int(v[2])), scores=[int(x) for x in v[3:]])
        elif tag == "X":
            games[-1]["X"] = rest
        elif tag == "O":
            ok, pos, tot = rest.split(" ")
            games[-1]["O"] = (ok == "1", int(pos), int(tot))
        elif tag == "T":
            nf, _, err = rest.partition(" ")
            games[-1]["T"] = (int(nf), err)
    return games


def sim_replay(n, seed, acts):
    """Replay recorded outputs through poker_sim.py. -> (log, result|None, error|None, order_ok)"""
    queue = list(acts)
    bad = []

    def mk(pid):
        def agent(obs):
            if not queue:
                bad.append("ran out"); return "FOLD"
            who, out = queue.pop(0)
            if who != pid:
                bad.append(("expected", who, "asked", pid, obs.round))
            return out
        return agent

    sim = PokerSim(n, seed)
    try:
        res = sim.run([mk(i) for i in range(n)])
    except RuntimeError as e:
        return sim.log, None, str(e), not bad and not queue
    return sim.log, res, None, not bad and not queue


def fuzz(games, seed):
    build()
    os.makedirs(CACHE_DIR, exist_ok=True)
    log = os.path.join(CACHE_DIR, f"fuzz_{seed}.log")
    subprocess.check_call([BIN, "gen", str(games), str(seed), log])
    gs = parse_log(log)
    bad = 0
    stats = dict(games=len(gs), cancelled=0, raised=0, hands=0, decisions=0, timeouts=0, by_n={2: 0, 3: 0, 4: 0})
    for g in gs:
        plog, pres, perr, _ = sim_replay(g["n"], g["seed"], g["acts"])
        ok = [tuple(x) for x in plog] == g["S"]
        if g["X"] is not None or perr is not None:
            ok = ok and (g["X"] is not None) == (perr is not None)
            stats["raised"] += 1
        else:
            ok = ok and pres["scores"] == g["R"]["scores"] and pres["hands"] == g["R"]["hands"] \
                 and pres["rounds"] == g["R"]["rounds"] and pres["cancelled"] == g["R"]["cancelled"]
            stats["cancelled"] += pres["cancelled"]
            stats["hands"] += pres["hands"]
        stats["decisions"] += len(g["S"])
        stats["timeouts"] += sum(a[1] is None for a in g["acts"])
        if g["T"][0]:
            stats["tracker_fail"] = stats.get("tracker_fail", 0) + g["T"][0]
            if stats["tracker_fail"] <= 5 * g["T"][0]:
                print("TRACKER", g["id"], "n", g["n"], "seed", g["seed"], "fails", g["T"][0], g["T"][1])
            ok = False
        stats["by_n"][g["n"]] += 1
        if not ok:
            bad += 1
            if bad <= 5:
                first = next((i for i, (a, b) in enumerate(zip(plog, g["S"])) if tuple(a) != b), None)
                print("MISMATCH game", g["id"], "n", g["n"], "seed", g["seed"], "first diff at index", first,
                      "py", plog[first] if first is not None and first < len(plog) else None,
                      "cpp", g["S"][first] if first is not None and first < len(g["S"]) else None,
                      "py_res", pres or perr, "cpp_res", g["R"] or g["X"])
    print(f"fuzz: {len(gs) - bad}/{len(gs)} random games identical to poker_sim.py "
          f"(2p/3p/4p {stats['by_n'][2]}/{stats['by_n'][3]}/{stats['by_n'][4]}, {stats['decisions']} decisions, "
          f"{stats['hands']} hands, {stats['cancelled']} hit the 600 cap, {stats['raised']} NONE-at-601 raises, "
          f"{stats['timeouts']} timeouts); tracker checks failed: {stats.get('tracker_fail', 0)}")
    return bad == 0


def replays(paths):
    build()
    os.makedirs(CACHE_DIR, exist_ok=True)
    dump = os.path.join(CACHE_DIR, "replay_inputs.log")
    out = os.path.join(CACHE_DIR, "replay_outputs.log")
    recorded = {}
    with open(dump, "w", encoding="utf-8") as f:
        for p in paths:
            g = load_replay(p)
            seed = int(g["refereeInput"].strip().split("=")[1])
            gid = str(g["gameId"])
            recorded[gid] = [float(x) for x in g["scores"]]
            f.write(f"G {gid} {len(g['agents'])} {seed}\n")
            for pid, text in recorded_actions(g):
                f.write(f"A {pid} \\0\n" if text is None else f"A {pid} {esc(text)}\n")
    subprocess.check_call([BIN, "run", dump, out])
    gs = parse_log(out)
    good = 0
    for g in gs:
        ok = g["R"] is not None and g["O"][0] and [float(x) for x in g["R"]["scores"]] == recorded[g["id"]]
        if not ok:
            print("MISMATCH", g["id"], "n", g["n"], "order", g["O"], "cpp", g["R"] or g["X"], "cg", recorded[g["id"]])
        if g["T"][0]:
            print("TRACKER", g["id"], "n", g["n"], "fails", g["T"][0], g["T"][1])
            ok = False
        good += ok
    tf = sum(g["T"][0] for g in gs)
    print(f"replays: {good}/{len(gs)} recorded games reproduced by the C++ engine (turn order + final scores); "
          f"tracker checks failed: {tf}")
    return good == len(gs) and len(gs) == len(paths)


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("mode", choices=["fuzz", "replays", "all"])
    ap.add_argument("replays", nargs="*", help="replay paths or game ids (default: all)")
    ap.add_argument("--ids")
    ap.add_argument("--games", type=int, default=2000)
    ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args()
    ok = True
    if a.mode in ("fuzz", "all"):
        ok &= fuzz(a.games, a.seed)
    if a.mode in ("replays", "all"):
        ok &= replays(replay_paths(a.replays, a.ids))
    sys.exit(0 if ok else 1)
