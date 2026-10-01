"""Per-hand outcome statistics of real top-bot games (replayed exactly through poker_sim):
how hands end (fold preflop / fold later / showdown), and how often a preflop all-in happens, by blind level.
Usage: python3 analysis/hand_outcomes.py [game_set]  (default validation120 = the 120 replays the numbers in README.md came from; 'all' = every replay in data/replays)"""
import glob, json, os, sys
from collections import defaultdict, Counter
from common import load_json, replay_paths, use_sim
use_sim()
from poker_sim import PokerSim
from replay_io import recorded_actions

class Probe(PokerSim):
    def calculate_player_winnings(self):
        if self.calc_winnings:
            nf = self.not_folded()
            street = len(self.board)
            allin = any(p.allin for p in self.players if not p.eliminated)
            key = "fold-preflop" if nf == 1 and street == 0 else ("fold-postflop" if nf == 1 else "showdown")
            self.out.append((self.hand_nb, sum(not p.eliminated for p in self.players), key,
                             self.preflop_allin, allin))
        return super().calculate_player_winnings()

    def do_action(self, t, amount):
        if t == "ALL_IN" and not self.board:
            self.preflop_allin = True
        super().do_action(t, amount)

    def reset_hand(self):
        super().reset_hand()
        self.preflop_allin = False

stats = defaultdict(Counter)
for path in replay_paths(sys.argv[1] if len(sys.argv) > 1 else "validation120"):
    g = load_json(path)
    seed = int(g["refereeInput"].strip().split("=")[1]); n = len(g["agents"])
    q = [out for _, out in recorded_actions(g)]          # None = recorded timeout (PokerSim plays TIMEOUT)
    sim = Probe.__new__(Probe); sim.out = []; sim.preflop_allin = False
    Probe.__init__(sim, n, seed)
    sim.run([lambda o: q.pop(0)] * n)
    for h, alive, key, pfa, anyallin in sim.out:
        lvl = min(h // 10, 6)
        stats[lvl][key] += 1
        stats[lvl]["preflop-allin"] += pfa
        stats[lvl]["any-allin"] += anyallin
        stats[lvl]["hands"] += 1
for lvl in sorted(stats):
    c = stats[lvl]; t = c["hands"]
    print(f"hands {max(1,10*lvl):2d}-{10*lvl+9 if lvl<6 else '..'}: n={t:4d}  fold-preflop {c['fold-preflop']/t:4.0%}  "
          f"fold-postflop {c['fold-postflop']/t:4.0%}  showdown {c['showdown']/t:4.0%}  "
          f"preflop ALL-IN action {c['preflop-allin']/t:4.0%}  hand with any all-in {c['any-allin']/t:4.0%}")
