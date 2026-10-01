"""Replay our live games through a bot binary and record what it would do and why (its stderr tag) at each of our
decisions, next to what the live bot did.

    python3 analysis/retrace.py --pseudo flawedaxioms --ids data/cache/m21_ids.txt --exe build/cg/poker_bundled.exe
                                [--out data/cache/retrace_m21.jsonl]

The game is replayed with the recorded actions (so it is exactly the live game); at each of our turns the exact
stdin the referee sent is also fed to the binary, whose answer and diagnostics line are recorded.  With the same
build and seed as the live bot, table-driven decisions (hu-*, pf-*, pfn-*) reproduce exactly; Monte Carlo ones can
differ by the time budget.  Output: one JSON line per decision with game, hand, round, street, pot, call, stack,
live, cards, board, recorded action, bot action, tag, equity, and the hand's net chips for us (BB).
"""
import argparse, json, os, re, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(REPO, "sim"))
from poker_sim import PokerSim, obs_to_stdin   # noqa: E402
from replay_io import REPLAY_DIR, load_replay, recorded_actions   # noqa: E402
import postmortem   # noqa: E402

TAG_RE = re.compile(r"^r(\d+) h(\d+) .*?trials=(\S+) eq=(\S+) (\S+)")


def retrace(gid, pseudo, exe):
    path = os.path.join(REPLAY_DIR, f"{gid}.json.gz")
    g = load_replay(path)
    seed = int(g["refereeInput"].strip().split("=")[1])
    n = len(g["agents"])
    names = {a["index"]: (a["codingamer"]["pseudo"] if a.get("codingamer") else f"agent{a['index']}") for a in g["agents"]}
    me = next(i for i, nm in names.items() if nm == pseudo)
    q = recorded_actions(g)
    sim = PokerSim(n, seed)
    err_path = os.path.join(REPO, "build", "cg", f"retrace_{gid}.err")
    err = open(err_path, "w")
    p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=err, text=True)
    first = [True]
    recs = []
    hand_start = {}

    def mk(pid):
        def agent(obs):
            who, out = q.pop(0)
            if obs.hand_nb not in hand_start:
                hand_start[obs.hand_nb] = [pp.stack + pp.total for pp in sim.players]
            if pid == me and out is not None:
                try:
                    p.stdin.write(obs_to_stdin(obs, first[0])); p.stdin.flush(); first[0] = False
                    ans = p.stdout.readline().strip()
                except (BrokenPipeError, OSError):
                    ans = None
                pot = sum(pp.total for pp in sim.players)
                call = sim.call_amount(sim.players[me])
                live = sum(not pp.folded for pp in sim.players)
                recs.append(dict(game=gid, n=n, hand=obs.hand_nb, round=obs.round, street=len(sim.board), pot=pot, call=call,
                                 stack=sim.players[me].stack, bb=sim.bb, live=live, cards=obs.cards, board=obs.board,
                                 rec=out.split(";")[0], bot=ans, possible=obs.possible))
            return out
        return agent

    res = sim.run([mk(i) for i in range(n)])
    try:
        p.stdin.close()
    except Exception:
        pass
    p.wait(timeout=10); err.close()
    tags = {}
    for line in open(err_path):
        m = TAG_RE.match(line)
        if m:
            tags[int(m.group(1))] = (m.group(5), m.group(4), m.group(3))
    os.remove(err_path)
    # net chips per hand
    hs = sorted(hand_start)
    net = {}
    for i, h in enumerate(hs):
        nxt = hand_start[hs[i + 1]] if i + 1 < len(hs) else [pp.stack for pp in sim.players]
        net[h] = nxt[me] - hand_start[h][me]
    for r in recs:
        t = tags.get(r["round"], ("?", "", ""))
        r["tag"], r["eq"], r["trials"] = t
        r["net_bb"] = net.get(r["hand"], 0) / r["bb"]
        r["start_bb"] = hand_start[r["hand"]][me] / r["bb"]
        r["alive"] = sum(1 for s in hand_start[r["hand"]] if s > 0)
    return recs, res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pseudo", required=True)
    ap.add_argument("--ids", required=True)
    ap.add_argument("--exe", default=os.path.join(REPO, "build", "cg", "poker_bundled.exe"))
    ap.add_argument("--out", default=os.path.join(REPO, "data", "cache", "retrace.jsonl"))
    a = ap.parse_args()
    ids = [int(x) for x in open(a.ids).read().split()]
    n_dec = n_same = 0
    with open(a.out, "w") as f:
        for gid in ids:
            if not os.path.exists(os.path.join(REPLAY_DIR, f"{gid}.json.gz")):
                continue
            recs, res = retrace(gid, a.pseudo, a.exe)
            for r in recs:
                f.write(json.dumps(r) + "\n")
                n_dec += 1; n_same += r["bot"] is not None and r["bot"].replace("ALL_IN", "ALL-IN") == r["rec"].replace("ALL_IN", "ALL-IN")
    print(f"{n_dec} decisions, bot reproduces the live action in {n_same} ({100 * n_same / max(1, n_dec):.1f}%); wrote {os.path.relpath(a.out, REPO)}")


if __name__ == "__main__":
    main()
