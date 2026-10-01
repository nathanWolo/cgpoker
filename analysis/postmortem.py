"""Post-mortem of our bot's live CodinGame games.

    python3 analysis/postmortem.py --pseudo flawedaxioms [--battles data/cache/battles_<pseudo>.json]
        [--leaderboard data/cache/lb_now.json] [--hands] [--busts]

Loads the finished games from the battle list (gamesPlayersRanking/findLastBattlesByTestSessionHandle
JSON, see tools/fetch_replays.py) whose replays are in data/replays/, replays each through
sim/poker_sim.py (so every hole card is known) and reports: reproduction check, placements and payout
by table size, opponents with their leaderboard ranks and the finish-ahead rate against each, timeouts
and replaced outputs, how each bust happened, chip results by effective stack, and our action mix.
"""
import argparse, collections, json, os, statistics, sys
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(REPO, "sim"))
from poker_sim import PokerSim, best7                                # noqa: E402
from replay_io import REPLAY_DIR, load_replay, recorded_actions     # noqa: E402

PAY = {2: [1, 0], 3: [1, .5, 0], 4: [1, .6444, .3556, 0]}


def placements(scores):
    """placement (0 = first) per seat from final scores; ties share the average payout slot"""
    n = len(scores)
    out = []
    for i in range(n):
        better = sum(s > scores[i] for s in scores); equal = sum(s == scores[i] for s in scores)
        out.append((better, equal))
    return out


def payout(scores, seat):
    n = len(scores); better, equal = placements(scores)[seat]
    return sum(PAY[n][better:better + equal]) / equal


def analyse(path, me_name):
    g = load_replay(path)
    seed = int(g["refereeInput"].strip().split("=")[1])
    n = len(g["agents"])
    names = {a["index"]: (a["codingamer"]["pseudo"] if a.get("codingamer") else f"agent{a['index']}") for a in g["agents"]}
    me = next(i for i, nm in names.items() if nm == me_name)
    q = recorded_actions(g)
    sim = PokerSim(n, seed)
    rec = dict(game=g["gameId"], n=n, me=me, names=names, decisions=[], hands={}, timeouts=[], replaced=0)
    hand_start = {}

    def mk(pid):
        def agent(obs):
            who, out = q.pop(0)
            if obs.hand_nb not in hand_start:
                hand_start[obs.hand_nb] = dict(stacks=[p.stack + p.total for p in sim.players], bb=sim.bb, dealer=sim.dealer_id,
                                               bb_id=sim.bb_id, sb_id=sim.sb_id, hands={p.id: list(p.hand) for p in sim.players})
            if pid == me:
                if out is None:
                    rec["timeouts"].append(obs.round)
                else:
                    t, a, err = PokerSim.parse(out.split(";")[0])
                    t2, a2 = sim.replace(t, a)
                    shown = {"ALL_IN": "ALL-IN"}.get(t2, t2) + (f"_{a2}" if t2 == "BET" else "")
                    if err or t2 != t or (t2 == "BET" and a2 != a):
                        rec["replaced"] += 1
                    pot = sum(p.total for p in sim.players)
                    call = sim.call_amount(sim.players[me])
                    live = sum(not p.folded for p in sim.players)
                    rec["decisions"].append(dict(round=obs.round, hand=obs.hand_nb, street=len(sim.board), pot=pot, call=call,
                                                 stack=sim.players[me].stack, bb=sim.bb, live=live, raw=out.split(";")[0], shown=shown,
                                                 cards=obs.cards, board=obs.board, possible=obs.possible))
            return out
        return agent

    res = sim.run([mk(i) for i in range(n)])
    rec["ok"] = [float(s) for s in res["scores"]] == [float(s) for s in g["scores"]] and not q
    rec["scores"] = res["scores"]; rec["hands_played"] = res["hands"]; rec["rounds"] = res["rounds"]; rec["cancelled"] = res["cancelled"]
    # per-hand results for us: net chips, start stack in BB, showdown info
    for h, hs in hand_start.items():
        sd = sim.showdowns.get(h)
        rec["hands"][h] = dict(start=hs, board=sd[0] if sd else None, cards=sd[1] if sd else None)
    # stacks after each hand from showdown order: recompute net per hand via start stacks of the next hand
    hs_sorted = sorted(hand_start)
    for i, h in enumerate(hs_sorted):
        nxt = hand_start[hs_sorted[i + 1]]["stacks"] if i + 1 < len(hs_sorted) else [p.stack for p in sim.players]
        rec["hands"][h]["net"] = [nxt[p] - hand_start[h]["stacks"][p] for p in range(n)]
    rec["elim_rank"] = [p.elim_rank for p in sim.players]
    rec["payout"] = payout(res["scores"], me)
    rec["place"] = placements(res["scores"])[me]
    return rec


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--pseudo", required=True)
    ap.add_argument("--battles")
    ap.add_argument("--leaderboard", default=os.path.join(REPO, "data", "cache", "lb_now.json"))
    ap.add_argument("--hands", action="store_true", help="print every hand we played")
    ap.add_argument("--busts", action="store_true", help="print the hands in which we busted or doubled")
    a = ap.parse_args()
    battles = json.load(open(a.battles or os.path.join(REPO, "data", "cache", f"battles_{a.pseudo}.json")))
    rank = {}
    if os.path.exists(a.leaderboard):
        for u in json.load(open(a.leaderboard))["users"]:
            rank[u["pseudo"]] = (u["rank"], u["score"], u["league"]["divisionIndex"])
    ids = [b["gameId"] for b in battles if b.get("done")]
    recs = []
    for gid in ids:
        p = os.path.join(REPLAY_DIR, f"{gid}.json.gz")
        if not os.path.exists(p):
            print("missing replay", gid); continue
        recs.append(analyse(p, a.pseudo))
    print(f"{len(recs)} finished games of {a.pseudo}; simulator reproduced {sum(r['ok'] for r in recs)}/{len(recs)}")
    bad = [r for r in recs if not r["ok"]]
    for r in bad:
        print("  NOT REPRODUCED", r["game"], "sim", r["scores"])

    # placements / payout by table size
    by_n = collections.defaultdict(list)
    for r in recs:
        by_n[r["n"]].append(r)
    print("\nplacement by table size (payout = TrueSkill-implied placement value):")
    for n in sorted(by_n):
        rs = by_n[n]
        places = collections.Counter(r["place"][0] + 1 for r in rs)
        print(f"  {n}p: {len(rs)} games, mean payout {statistics.mean(r['payout'] for r in rs):.3f}, places "
              + ", ".join(f"{k}:{places[k]}" for k in sorted(places)))
    tot_pay = statistics.mean(r["payout"] for r in recs)
    print(f"  all: mean payout {tot_pay:.3f}; timeouts {sum(len(r['timeouts']) for r in recs)}; replaced outputs {sum(r['replaced'] for r in recs)}; "
          f"cancelled at 600 {sum(r['cancelled'] for r in recs)}; mean hands {statistics.mean(r['hands_played'] for r in recs):.1f}")

    # opponents
    ahead = collections.defaultdict(lambda: [0, 0])
    for r in recs:
        for s, nm in r["names"].items():
            if s == r["me"]: continue
            a_ = ahead[nm]; a_[1] += 1
            a_[0] += 1 if r["scores"][r["me"]] > r["scores"][s] else 0.5 if r["scores"][r["me"]] == r["scores"][s] else 0
    print("\nopponents (leaderboard rank/score/league), finish-ahead rate:")
    for nm, (w, k) in sorted(ahead.items(), key=lambda kv: rank.get(kv[0], (999,))[0]):
        rk = rank.get(nm)
        print(f"  {nm:24s} {('#%d %.2f L%d' % rk) if rk else 'unranked':16s} ahead {w}/{k}")
    pooled = sum(v[0] for v in ahead.values()) / sum(v[1] for v in ahead.values())
    print(f"  pooled finish-ahead rate p = {pooled:.3f}")

    # busts and big hands
    print("\nhow games ended for us:")
    for r in recs:
        me = r["me"]; n = r["n"]
        opp = ", ".join(f"{r['names'][s]}({rank.get(r['names'][s], (0,))[0]})" for s in range(n) if s != me)
        # find our bust hand: the last hand where our start stack > 0 and net == -start
        bust = None
        for h, hd in sorted(r["hands"].items()):
            st = hd["start"]["stacks"][me]
            if st > 0 and hd.get("net") is not None and hd["net"][me] == -st:
                bust = (h, st / hd["start"]["bb"], hd)
        place = r["place"][0] + 1
        line = f"  g{r['game']} {n}p place {place}{'=' if r['place'][1] > 1 else ''} pay {r['payout']:.2f} hands {r['hands_played']} vs {opp}"
        if bust:
            h, bbs, hd = bust
            mine = "_".join(hd["start"]["hands"][me]); board = hd["board"]
            line += f" | BUST hand {h} at {bbs:.1f} BB with {mine} on {board}"
        elif r["cancelled"]:
            line += " | cancelled at round 600"
        print(line)
        if a.busts and bust:
            h = bust[0]
            for d in r["decisions"]:
                if d["hand"] == h:
                    print(f"      r{d['round']} street {d['street']} pot {d['pot']} call {d['call']} stack {d['stack']} live {d['live']} {d['cards']} {d['board']} -> {d['raw']} ({d['shown']})")

    # chip results by effective stack
    buckets = collections.defaultdict(list)
    for r in recs:
        me = r["me"]
        for h, hd in r["hands"].items():
            if hd.get("net") is None: continue
            st = hd["start"]["stacks"]; bb = hd["start"]["bb"]
            if st[me] == 0: continue
            eff = min(st[me], max(s for i, s in enumerate(st) if i != me)) / bb
            b = "<=10" if eff <= 10 else "10-20" if eff <= 20 else "20-50" if eff <= 50 else ">50"
            buckets[b].append(hd["net"][me] / bb)
    print("\nour net chips per hand by effective stack (BB):")
    for b in ["<=10", "10-20", "20-50", ">50"]:
        v = buckets.get(b, [])
        if v: print(f"  {b:6s} n={len(v):4d} mean {statistics.mean(v):+.3f} BB/hand, total {sum(v):+.0f} BB")

    # action mix
    mix = collections.defaultdict(collections.Counter)
    for r in recs:
        for d in r["decisions"]:
            street = ["preflop", "flop", "turn", "river"][min(d["street"], 5) // 1 if d["street"] < 3 else (1 if d["street"] == 3 else 2 if d["street"] == 4 else 3)]
            kind = d["shown"].split("_")[0]
            mix[street][kind] += 1
    print("\nour action mix (post-replacement):")
    for st in ["preflop", "flop", "turn", "river"]:
        c = mix[st]; tot = sum(c.values())
        if tot: print(f"  {st:8s} n={tot:4d} " + " ".join(f"{k} {v / tot:.0%}" for k, v in sorted(c.items(), key=lambda kv: -kv[1])))
    # preflop first-in behaviour and all-in calls
    allin_calls = [d for r in recs for d in r["decisions"] if d["call"] >= d["stack"] and d["call"] > 0]
    called = [d for d in allin_calls if d["shown"] in ("ALL-IN", "CALL")]
    print(f"\nfacing an all-in-sized call: {len(allin_calls)} spots, called {len(called)}")
    if a.hands:
        print("\nall our decisions:")
        for r in recs:
            print(f"-- game {r['game']} ({r['n']}p, we are seat {r['me']})")
            for d in r["decisions"]:
                print(f"   r{d['round']} h{d['hand']} street {d['street']} pot {d['pot']} call {d['call']} stack {d['stack']} {d['cards']} {d['board']} -> {d['raw']} ({d['shown']})")


if __name__ == "__main__":
    main()
