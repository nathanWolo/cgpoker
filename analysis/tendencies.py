"""Per-opponent tendencies in the recorded games, and how fast they show within a game.

    python3 analysis/tendencies.py [--max-games N]

Replays every recorded game (sim/poker_sim.py) with every seat's actions and the revealed hole cards, and
computes per (player, game): preflop VPIP, raise, limp and all-in frequencies, fold-to-raise, and postflop
bet-when-checked-to and fold-to-bet frequencies, plus the mean strength percentile (0 = best) of the hands
the player put money in with preflop (from the revealed cards).  Reports the spread across players against
the noise within a player (same player, different games), and the correlation between a game's first 15
hands and the rest of the game (how fast a tendency is readable live).
"""
import argparse, collections, glob, os, sys, statistics
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(REPO, "sim"))
from poker_sim import PokerSim           # noqa: E402
from replay_io import REPLAY_DIR, load_replay, recorded_actions   # noqa: E402

R = "23456789TJQKA"
PCT = None


def load_pct():
    global PCT
    txt = open(os.path.join(REPO, "bot", "pf_rank.hpp")).read()
    arr = txt.split("PF_PCT100[169] = {")[1].split("}")[0]
    PCT = [int(x) for x in arr.split(",")]


def cls(c0, c1):
    r0, s0 = R.index(c0[0]), c0[1]; r1, s1 = R.index(c1[0]), c1[1]
    hi, lo = max(r0, r1), min(r0, r1)
    if r0 == r1: return r0 * 13 + r0
    return hi * 13 + lo if s0 == s1 else lo * 13 + hi


def analyse(path):
    g = load_replay(path)
    seed = int(g["refereeInput"].strip().split("=")[1]); n = len(g["agents"])
    names = {a["index"]: (a["codingamer"]["pseudo"] if a.get("codingamer") else f"agent{a['index']}") for a in g["agents"]}
    q = recorded_actions(g); sim = PokerSim(n, seed)
    recs = []     # (pid, hand, street, kind, facing_raise, call>0, action)

    def mk(pid):
        def agent(obs):
            who, out = q.pop(0)
            if out is None: return out
            me = sim.players[pid]
            mx = max(p.total for p in sim.players); call = mx - me.total
            street = len(sim.board)
            raised = sim.last_raiser != -1 and sim.last_raiser != pid
            t, amt, err = PokerSim.parse(out.split(";")[0])       # the action the referee applies, after replacement (a CHECK facing a bet is a FOLD)
            t2, _ = sim.replace(t, amt)
            a = {"ALL_IN": "ALL-IN"}.get(t2, t2)
            recs.append((pid, obs.hand_nb, street, call > 0, raised, a))
            return out
        return agent
    sim.run([mk(i) for i in range(n)])
    cards = {h: sd[1] for h, sd in sim.showdowns.items()}
    return names, recs, cards, n


def stats(recs, cards, pid, hands):
    pre = [r for r in recs if r[0] == pid and r[2] == 0 and r[1] in hands]
    post = [r for r in recs if r[0] == pid and r[2] > 0 and r[1] in hands]
    byhand = collections.defaultdict(list)
    for r in pre: byhand[r[1]].append(r)
    vpip = raise_ = limp = allin = 0; n_pre = len(byhand); fold_to_raise = [0, 0]; played_pct = []
    for h, rs in byhand.items():
        acts = [r[5] for r in rs]
        put = any(a in ("CALL", "BET", "ALL-IN") for a in acts)
        vpip += put; raise_ += any(a in ("BET", "ALL-IN") for a in acts); allin += "ALL-IN" in acts
        limp += acts[0] == "CALL" and not rs[0][4]
        for r in rs:
            if r[4] and r[3]: fold_to_raise[1] += 1; fold_to_raise[0] += r[5] == "FOLD"
        if put and h in cards and cards[h]:
            cs = cards[h]
            hole = cs[pid] if isinstance(cs[0], (list, tuple)) else cs[2 * pid:2 * pid + 2]
            if hole and len(hole) == 2 and all(isinstance(c, str) and len(c) == 2 for c in hole): played_pct.append(PCT[cls(hole[0], hole[1])])
    bet_ck = [0, 0]; fold_bet = [0, 0]
    for r in post:
        if not r[3]: bet_ck[1] += 1; bet_ck[0] += r[5] in ("BET", "ALL-IN")
        else: fold_bet[1] += 1; fold_bet[0] += r[5] == "FOLD"
    f = lambda a, b: a / b if b else None
    return dict(n=n_pre, vpip=f(vpip, n_pre), pfr=f(raise_, n_pre), limp=f(limp, n_pre), allin=f(allin, n_pre),
                fold_to_raise=f(*fold_to_raise), bet_ck=f(*bet_ck), fold_bet=f(*fold_bet),
                played_pct=statistics.mean(played_pct) if played_pct else None)


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--max-games", type=int, default=100000); a = ap.parse_args()
    load_pct()
    rows = []; first_rest = collections.defaultdict(list)
    paths = sorted(glob.glob(os.path.join(REPLAY_DIR, "*.json.gz")))[: a.max_games]
    for path in paths:
        try: names, recs, cards, n = analyse(path)
        except Exception as e: continue
        hands = sorted({r[1] for r in recs})
        for pid in range(n):
            s = stats(recs, cards, pid, set(hands))
            if s["n"] < 15: continue
            rows.append((names[pid], os.path.basename(path), s))
            s1 = stats(recs, cards, pid, set(hands[:15])); s2 = stats(recs, cards, pid, set(hands[15:]))
            if s2["n"] >= 10:
                for k in ("vpip", "pfr", "limp", "allin", "fold_to_raise", "bet_ck", "fold_bet", "played_pct"):
                    if s1[k] is not None and s2[k] is not None: first_rest[k].append((s1[k], s2[k]))
    print(f"{len(paths)} games, {len(rows)} (player, game) rows with >= 15 hands, {len({r[0] for r in rows})} distinct players")
    keys = ("vpip", "pfr", "limp", "allin", "fold_to_raise", "bet_ck", "fold_bet", "played_pct")
    print("\nstat            mean   across-player sd   within-player sd (same player, other games)   first-15 vs rest corr")
    for k in keys:
        vals = [r[2][k] for r in rows if r[2][k] is not None]
        byp = collections.defaultdict(list)
        for r in rows:
            if r[2][k] is not None: byp[r[0]].append(r[2][k])
        means = [statistics.mean(v) for v in byp.values() if len(v) >= 3]
        within = [statistics.pstdev(v) for v in byp.values() if len(v) >= 3]
        pr = first_rest[k]
        corr = None
        if len(pr) > 10:
            xs, ys = zip(*pr); mx, my = statistics.mean(xs), statistics.mean(ys)
            sx, sy = statistics.pstdev(xs), statistics.pstdev(ys)
            corr = sum((x - mx) * (y - my) for x, y in pr) / len(pr) / (sx * sy) if sx and sy else None
        print(f"  {k:14s} {statistics.mean(vals):6.2f}   {statistics.pstdev(means) if means else float('nan'):6.2f}             {statistics.mean(within) if within else float('nan'):6.2f}                                   {corr if corr is None else round(corr, 2)}")
    print("\nplayers with >= 4 games (mean over games): name  games  vpip  pfr  limp  allin  fold2raise  bet_ck  fold_bet  played_pct")
    byp = collections.defaultdict(list)
    for r in rows: byp[r[0]].append(r[2])
    for name, ss in sorted(byp.items(), key=lambda kv: -len(kv[1])):
        if len(ss) < 4: continue
        m = lambda k: statistics.mean([s[k] for s in ss if s[k] is not None]) if any(s[k] is not None for s in ss) else float("nan")
        print(f"  {name:22s} {len(ss):3d}  {m('vpip'):.2f}  {m('pfr'):.2f}  {m('limp'):.2f}  {m('allin'):.2f}  {m('fold_to_raise'):.2f}  {m('bet_ck'):.2f}  {m('fold_bet'):.2f}  {m('played_pct'):.0f}")


if __name__ == "__main__":
    main()
