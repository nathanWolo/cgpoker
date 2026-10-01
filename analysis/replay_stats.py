"""Summarise downloaded CodinGame Poker replays: hands/game, rounds/game, 600-cap hits, actions per hand.
Usage: python3 analysis/replay_stats.py [game_set]  (default validation120 = the 120 replays the numbers in README.md came from; 'all' = every replay in data/replays)"""
import json, os, re, glob, collections, statistics as st, sys
from common import load_json, replay_paths
ENT = lambda eid: re.compile(r'(?:^|;|")%d [0-9.]+ T (\'[^\']*\'|[^;"]+)' % eid)

def parse(path):
    g = load_json(path)
    fr = g["frames"]
    n = len(g["agents"])
    # entity ids of the hand / round counters depend on player count (16*n+11, 16*n+15)
    R_HAND, R_ROUND = ENT(16 * n + 11), ENT(16 * n + 15)
    hand, rnd = 0, 0
    per_hand = collections.Counter()
    acts = collections.Counter()           # (level, action type)
    hand_of_action = []
    for f in fr:
        v = f.get("view", "")
        m = R_HAND.findall(v)
        if m:
            hand = int(m[-1].strip("'"))
        m = R_ROUND.findall(v)
        if m:
            rnd = max(rnd, int(m[-1].strip("'")))
        out = f.get("stdout", "")
        if out and f.get("agentId", -1) >= 0:
            per_hand[hand] += 1
            a = out.split(";")[0].strip().upper().split(" ")[0].split("_")[0]
            lvl = hand // 10
            acts[(lvl, a)] += 1
    elim = [json.loads(t) for t in g["tooltips"] if "no more chips" in t or "timeout" in t.lower()]
    return dict(n=n, hands=hand, rounds=rnd, per_hand=per_hand, acts=acts,
                cancelled=rnd >= 600, elim=elim, scores=g["scores"])

def main(game_set="validation120"):
    games = [parse(p) for p in replay_paths(game_set)]
    by_n = collections.defaultdict(list)
    for g in games:
        by_n[g["n"]].append(g)
    for n in sorted(by_n):
        gs = by_n[n]
        hands = [g["hands"] for g in gs]
        rounds = [g["rounds"] for g in gs]
        capped = sum(g["cancelled"] for g in gs)
        print(f"{n} players: games={len(gs)} hands mean={st.mean(hands):.1f} median={st.median(hands)} "
              f"min={min(hands)} max={max(hands)} | rounds mean={st.mean(rounds):.1f} max={max(rounds)} "
              f"| reached 600-round cap: {capped}")
        # actions per hand by level
        lv = collections.defaultdict(list)
        for g in gs:
            for h, c in g["per_hand"].items():
                lv[h // 10].append(c)
        print("   actions/hand by blind level (hands 10k..10k+9):",
              {k: round(st.mean(v), 2) for k, v in sorted(lv.items())})
        # elimination hands
        eh = sorted(int(e["text"].split(":")[0]) for g in gs for e in g["elim"])
        if eh:
            print("   elimination hand quartiles:", [eh[len(eh)*q//4] for q in range(4)] + [eh[-1]])
        acts = collections.Counter()
        for g in gs:
            acts.update(g["acts"])
        for lvl in sorted({k[0] for k in acts}):
            tot = sum(v for k, v in acts.items() if k[0] == lvl)
            frac = {a: round(acts[(lvl, a)] / tot, 2) for a in ("FOLD", "CHECK", "CALL", "BET", "ALL-IN")}
            print(f"   level {lvl} (hands {max(1,10*lvl)}-{10*lvl+9}) n={tot}: {frac}")

if __name__ == "__main__":
    main(*sys.argv[1:2])
