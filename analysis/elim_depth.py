"""For real top-bot games: at what stack depth (start-of-hand stack / BB) are players eliminated,
and how many hands does the chip leader need? Replays games exactly through poker_sim.
Usage: python3 analysis/elim_depth.py [game_set]  (default validation120 = the 120 replays the numbers in README.md came from; 'all' = every replay in data/replays)"""
import glob, json, os, statistics as st, sys
from collections import defaultdict
from common import load_json, replay_paths, use_sim
use_sim()
from poker_sim import PokerSim
from replay_io import recorded_actions

class Probe(PokerSim):
    def reset_hand(self):
        super().reset_hand()
        self.start_stacks = [p.stack for p in self.players]
        self.start_bb = self.bb

    def calculate_elimination_ranks(self):
        before = {p.id for p in self.players if p.elim_rank >= 0}
        super().calculate_elimination_ranks()
        for p in self.players:
            if p.elim_rank >= 0 and p.id not in before and not p.timeout:
                self.elims.append((self.hand_nb, self.start_stacks[p.id] / self.start_bb,
                                   sum(q.stack > 0 for q in self.players) + 1, len(self.board)))

res = defaultdict(list)
for path in replay_paths(sys.argv[1] if len(sys.argv) > 1 else "validation120"):
    g = load_json(path)
    seed = int(g["refereeInput"].strip().split("=")[1]); n = len(g["agents"])
    q = [out for _, out in recorded_actions(g)]          # None = recorded timeout (PokerSim plays TIMEOUT)
    Probe.elims = None
    sim = Probe.__new__(Probe); sim.elims = []; Probe.__init__(sim, n, seed)
    sim.run([lambda o: q.pop(0)] * n)
    res[n] += sim.elims
for n, e in sorted(res.items()):
    d = sorted(x[1] for x in e)
    print(f"start {n}p: {len(e)} eliminations; eliminated player's start-of-hand stack in BB: "
          f"quartiles {[round(d[len(d)*k//4],1) for k in range(4)]} max {round(d[-1],1)}; "
          f"<=10BB {sum(x<=10 for x in d)/len(d):.0%}, <=20BB {sum(x<=20 for x in d)/len(d):.0%}; "
          f"")
