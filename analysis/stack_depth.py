"""Stack-depth tables for CodinGame Poker.

Part 1 (theory): blinds by hand, average stack in BB, preflop dead money and M-ratio for 2/3/4 alive players
          (remember: EVERY non-BB player posts the small blind each hand -> preflop pot = (N+1)/2 BB).
          `theory` output is saved as stack_depth_table.txt.
Part 2 (empirical): replays real top-bot games through poker_sim.py and measures, per decision,
          the effective stack in BB and the number of players alive.
Part 3 (policy study): hands / rounds / 600-round-cap rate for simple bots (calling station = league boss).
Usage: python3 analysis/stack_depth.py [theory|empirical|policy|all] [game_set]
       (game_set for `empirical`: default validation120, the 120 replays behind README.md; 'all' = every replay)
"""
import glob, json, os, random, statistics as st, sys
from collections import defaultdict, Counter
from common import load_json, replay_paths, use_sim
use_sim()
from poker_sim import PokerSim, SMALL_BLIND, BIG_BLIND, TOTAL_BUY_IN
from replay_io import recorded_actions


def blinds(hand):
    lvl = hand // 10                      # Board.increaseLevel: level up when handNb % 10 == 0
    return SMALL_BLIND << lvl, BIG_BLIND << lvl


def theory(max_hand=80):
    print("hand | SB/BB      | avg stack/BB with N alive: N=2 / N=3 / N=4 | M = stack/preflop pot (pot = (N+1)/2 BB): N=2 / N=3 / N=4")
    for h in range(1, max_hand + 1):
        sb, bb = blinds(h)
        cells = []
        ms = []
        for n in (2, 3, 4):
            stack = TOTAL_BUY_IN / n
            cells.append(f"{stack / bb:7.2f}")
            ms.append(f"{stack / ((n - 1) * sb + bb):6.2f}")
        print(f"{h:4d} | {sb:5d}/{bb:<5d}| {' / '.join(cells)} | {' / '.join(ms)}")


def empirical(game_set="validation120"):
    paths = replay_paths(game_set)
    by_start = defaultdict(list)
    for p in paths:
        g = load_json(p)
        seed = int(g["refereeInput"].strip().split("=")[1])
        n = len(g["agents"])
        queue = recorded_actions(g)                  # [(player, output)], output None = recorded timeout
        sim = PokerSim(n, seed)
        recs = []

        def mk(pid):
            def agent(obs):
                me = sim.players[pid]
                mine = me.stack + me.total
                others = [q.stack + q.total for q in sim.players if q.id != pid and not q.folded]
                eff = min(mine, max(others)) / sim.bb
                alive = sum(not q.eliminated for q in sim.players)
                recs.append((obs.hand_nb, alive, eff, len(sim.board)))
                return queue.pop(0)[1]
            return agent

        sim.run([mk(i) for i in range(n)])
        by_start[n].append(recs)

    for n in sorted(by_start):
        recs = [r for g in by_start[n] for r in g]
        tot = len(recs)
        print(f"\n== games starting with {n} players: {len(by_start[n])} games, {tot} decisions")
        for lo, hi in ((0, 5), (5, 10), (10, 15), (15, 25), (25, 50), (50, 100), (100, 1e9)):
            k = sum(lo <= r[2] < hi for r in recs)
            print(f"   effective stack {lo:>3}-{hi if hi < 1e9 else 'inf':>3} BB: {k / tot:6.1%} of decisions")
        pre = sum(r[3] == 0 for r in recs)
        print(f"   preflop decisions: {pre / tot:.1%}")
        ac = Counter(r[1] for r in recs)
        print("   decisions by #players alive:", {k: f"{v / tot:.0%}" for k, v in sorted(ac.items())})
        # median effective stack by hand bucket
        buckets = defaultdict(list)
        for r in recs:
            buckets[(r[0] - 1) // 10].append(r[2])
        print("   median eff. stack (BB) by hands 1-10,11-20,...:",
              [round(st.median(v), 1) for k, v in sorted(buckets.items()) if k < 8])


def policy_study(n_games=200):
    def calling_station(obs):
        return "CALL"                                   # config/Boss.java:54 (CALL -> CHECK when possible)

    def make_random(rng):
        def agent(obs):
            a = rng.choice(obs.possible)
            if a.startswith("BET_"):
                a = f"BET {int(int(a[4:]) * rng.choice([1, 1, 1.5, 2, 3]))}"
            return a
        return agent

    def make_passive(rng):
        def agent(obs):                                  # limp/check/call small bets, fold to big ones
            if "CHECK" in obs.possible:
                return "CHECK"
            pot = sum(obs.chip_in_pot)
            me = obs.player_id
            call = max(obs.chip_in_pot) - obs.chip_in_pot[me]
            return "CALL" if call <= 0.3 * pot else ("CALL" if rng.random() < 0.3 else "FOLD")
        return agent

    for name, factory in (("calling station (boss)", lambda rng: calling_station),
                          ("passive check/call", make_passive), ("uniform random legal", make_random)):
        for n in (2, 3, 4):
            rng = random.Random(n)
            hands, rounds, capped = [], [], 0
            for i in range(n_games):
                sim = PokerSim(n, rng.getrandbits(63) - (1 << 62))
                res = sim.run([factory(rng) for _ in range(n)])
                hands.append(res["hands"])
                rounds.append(res["rounds"])
                capped += res["cancelled"]
            print(f"{name:24s} N={n}: hands mean {st.mean(hands):5.1f} (max {max(hands)}), "
                  f"rounds mean {st.mean(rounds):5.1f}, 600-cap reached {capped / n_games:.0%}")


if __name__ == "__main__":
    what = sys.argv[1] if len(sys.argv) > 1 else "all"
    if what in ("theory", "all"):
        theory()
    if what in ("empirical", "all"):
        empirical(sys.argv[2] if len(sys.argv) > 2 else "validation120")
    if what in ("policy", "all"):
        policy_study()
