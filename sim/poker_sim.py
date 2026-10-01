"""
Faithful Python port of the CodinGame "Poker" referee (github.com/wala-fr/CodingamePoker, commit ac30d97).

Mirrors com.codingame.game.Referee.gameTurn frame-by-frame so that the global "round" counter,
the 600-round cancellation, NONE rounds, blind posting, action replacement, side pots, odd chips,
elimination ranks and the SHA1PRNG-seeded deck shuffle are all reproduced.

Usage:
    sim = PokerSim(n_players, seed)          # seed = the long in refereeInput "seed=..."
    result = sim.run(agents)                 # agents: list of callables obs -> "CALL" / "BET 200" / ...
Each agent receives an Obs (dataclass) that contains exactly what the real bot would see on stdin.
"""
from __future__ import annotations
from dataclasses import dataclass, field
from itertools import combinations
from typing import Callable, List, Optional

# ----------------------------------------------------------------------------- parameters
SMALL_BLIND, BIG_BLIND = 5, 10            # model/variable/Parameter.java:14-15
TOTAL_BUY_IN = 4800                        # Parameter.java:16
HAND_NB_BY_LEVEL, LEVEL_MULT = 10, 2       # Parameter.java:18-19
RAISE_CAP = 10                             # Parameter.java:24
MAX_TURN = 600                             # game/RefereeParameter.java:8
MAX_REFEREE_TURN = 10000                   # RefereeParameter.java:7 (frames)

RANKS = "23456789TJQKA"
SUITS = "CDHS"
ALL_CARDS = [r + s for r in RANKS for s in SUITS]   # CardUtils.java:309-315 (rank-major)


# ----------------------------------------------------------------------------- RNG
# The CG SDK's MultiplayerGameManager.getRandom() is java.security.SecureRandom("SHA1PRNG") seeded with
# setSeed(seed) (codingame-game-engine MultiplayerGameManager.java:57-62), NOT java.util.Random.
import hashlib


class SHA1PRNG:
    """Bit-exact port of sun.security.provider.SecureRandom (SHA1PRNG) + java.util.Random.nextInt(bound)."""

    def __init__(self, seed: int):
        seed_bytes = bytes(((seed >> (8 * i)) & 0xFF) for i in range(8))   # SecureRandom.longToByteArray (LE)
        self.state = bytearray(hashlib.sha1(seed_bytes).digest())
        self.buf = b""

    def _next_block(self):
        out = hashlib.sha1(bytes(self.state)).digest()
        last, zf = 1, False
        for i in range(20):                  # updateState: NB Java adds *signed* bytes, carry = v >> 8 (can be -1)
            sv = self.state[i] - 256 if self.state[i] > 127 else self.state[i]
            ov = out[i] - 256 if out[i] > 127 else out[i]
            v = sv + ov + last
            t = v & 0xFF
            zf |= self.state[i] != t
            self.state[i] = t
            last = v >> 8
        if not zf:
            self.state[0] = (self.state[0] + 1) & 0xFF
        return out

    def next_bytes(self, k):
        while len(self.buf) < k:
            self.buf += self._next_block()
        r, self.buf = self.buf[:k], self.buf[k:]
        return r

    def next(self, bits: int) -> int:
        nb = (bits + 7) // 8
        v = int.from_bytes(self.next_bytes(nb), "big")
        return v >> (nb * 8 - bits)

    def next_int(self, bound: int) -> int:
        r = self.next(31)
        m = bound - 1
        if bound & m == 0:
            return (bound * r) >> 31
        u = r
        while True:
            r = u % bound
            if u - r + m < (1 << 31):
                return r
            u = self.next(31)


def java_shuffle(lst, rnd):
    for i in range(len(lst), 1, -1):       # Collections.shuffle (RandomAccess branch)
        j = rnd.next_int(i)
        lst[i - 1], lst[j] = lst[j], lst[i - 1]


# ----------------------------------------------------------------------------- hand evaluation
def eval5(cards):
    """Returns a comparable tuple; same ordering as FiveCardHand.value (standard poker ranking)."""
    rs = sorted((RANKS.index(c[0]) + 2 for c in cards), reverse=True)
    flush = len({c[1] for c in cards}) == 1
    uniq = sorted(set(rs), reverse=True)
    straight_hi = 0
    if len(uniq) == 5:
        if uniq[0] - uniq[4] == 4:
            straight_hi = uniq[0]
        elif uniq == [14, 5, 4, 3, 2]:
            straight_hi = 5
    counts = sorted(((rs.count(r), r) for r in uniq), reverse=True)
    shape = [c for c, _ in counts]
    ordered = [r for _, r in counts]
    if straight_hi and flush:
        return (8, straight_hi)
    if shape == [4, 1]:
        return (7, *ordered)
    if shape == [3, 2]:
        return (6, *ordered)
    if flush:
        return (5, *rs)
    if straight_hi:
        return (4, straight_hi)
    if shape == [3, 1, 1]:
        return (3, *ordered)
    if shape == [2, 2, 1]:
        return (2, *ordered)
    if shape == [2, 1, 1, 1]:
        return (1, *ordered)
    return (0, *rs)


def best7(hole, board):
    return max(eval5(c) for c in combinations(list(hole) + list(board), 5))


# ----------------------------------------------------------------------------- model
@dataclass
class P:
    id: int
    stack: int
    total: int = 0          # totalBetAmount (whole hand) == "chipInPot" input
    rnd: int = 0            # roundBetAmount (current street)
    folded: bool = False
    allin: bool = False
    spoken: bool = False
    eliminated: bool = False
    timeout: bool = False
    elim_rank: int = -1
    score: int = 0
    hand: list = field(default_factory=list)

    def can_act(self):
        return not self.folded and not self.allin


@dataclass
class Obs:
    # init (only meaningful on the first call for this player)
    small_blind: int
    big_blind: int
    hand_nb_by_level: int
    level_mult: int
    buy_in: int
    first_bb_id: int
    player_nb: int
    player_id: int
    # per turn
    round: int
    hand_nb: int
    stacks: List[int]
    chip_in_pot: List[int]
    board: str
    cards: str
    actions: List[str]      # "round handNb playerId ACTION BOARD"
    showdowns: List[str]    # "handNb BOARD CARDS"
    possible: List[str]


class PokerSim:
    def __init__(self, n: int, seed: int, verbose=False):
        assert 2 <= n <= 4
        self.n = n
        self.rng = SHA1PRNG(seed)
        self.verbose = verbose
        first_bb = self.rng.next_int(n)                       # Referee.java:64
        self.first_bb = first_bb
        self.players = [P(i, TOTAL_BUY_IN // n) for i in range(n)]   # PlayerModel.java:38
        self.sb, self.bb, self.level = SMALL_BLIND, BIG_BLIND, 1
        self.hand_nb = 0
        self.bb_id = first_bb
        self.over = True
        self.turn = 0
        self.calc_winnings = False
        self.deal_card = False
        self.calc_next = False
        self.frame = None
        self.game_over = False
        self.cancelled = False
        self.round_infos = {}          # turn -> (turn, hand, pid, action_str, board_str)
        self.showdowns = {}            # hand -> (board_str, [cards per player] or None if eliminated)
        self.last_sent_round = [0] * n
        self.last_sent_hand = [0] * n
        self.last_hand_round = 0
        self.log = []
        # Referee.init
        self.reset_hand()
        self.init_deck()
        self.init_blind()
        self.calculate_next_player()

    # ------------------------------------------------------------------ Board helpers
    def reset_round(self):
        for p in self.players:
            p.rnd = 0
            p.spoken = False
        self.last_round_raise = 0
        self.last_total_round_bet = 0
        self.last_raiser = -1
        self.raise_nb = 0

    def reset_hand(self):
        self.board = []
        for p in self.players:
            p.hand = []
            p.eliminated = p.stack == 0
            p.allin = False
            p.folded = p.stack == 0
            p.total = 0
        self.over = False
        self.pot = 0
        self.reset_round()
        self.hand_nb += 1
        if self.hand_nb % HAND_NB_BY_LEVEL == 0:          # Board.increaseLevel
            self.level += 1
            self.sb *= LEVEL_MULT
            self.bb *= LEVEL_MULT
        self.init_positions()

    def init_positions(self):
        n = self.n
        if self.hand_nb > 1:                               # calculateNextBigBlindId
            while True:
                self.bb_id = (self.bb_id + 1) % n
                if self.players[self.bb_id].stack != 0:
                    break
        self.sb_id = self.dealer_id = -1
        nb = 0
        for i in range(n):
            idx = (self.bb_id - 1 - i) % n
            if not self.players[idx].folded:
                nb += 1
                if self.sb_id == -1:
                    self.sb_id = idx
                elif self.dealer_id == -1:
                    self.dealer_id = idx
        if nb == 2:
            self.dealer_id = self.sb_id
        self.last_player = -1
        self.next_player = self.bb_id
        self.last_total_round_bet = self.bb
        self.last_round_raise = self.sb
        self.last_raiser = -1
        self.raise_nb = 1

    def init_deck(self):
        self.deck = list(ALL_CARDS)
        java_shuffle(self.deck, self.rng)
        r = self.rng.next_int(52)                           # Deck.cut
        self.deck = self.deck[r:] + self.deck[:r]
        self.deck_idx = 0

    def draw(self):
        c = self.deck[self.deck_idx]
        self.deck_idx += 1
        return c

    def init_blind(self):
        # NB: every non-folded player posts at least the SMALL blind (Board.initBlind, Board.java:204-218)
        for p in self.players:
            if not p.folded:
                bet = self.bb if p.id == self.bb_id else self.sb
                if p.stack < (self.sb if p.id == self.sb_id else self.bb):
                    bet = p.stack
                self.bet_chips(p, bet)

    def is_first_bet(self):
        return self.last_raiser == -1

    def bet_chips(self, p: P, value: int):
        value = min(value, p.stack)
        p.stack -= value
        p.total += value
        p.rnd += value
        if p.stack == 0:
            p.allin = True
        raise_ = p.rnd - self.last_total_round_bet
        if (self.is_first_bet() and raise_ >= self.bb) or (not self.is_first_bet() and raise_ >= self.last_round_raise):
            self.last_round_raise = raise_
            self.last_raiser = p.id
            self.last_total_round_bet += raise_
            self.raise_nb += 1
        self.pot += value

    def deal_first(self):
        for _ in range(2):
            for i in range(self.n):
                p = self.players[(self.dealer_id + 1 + i) % self.n]
                if not p.folded:
                    p.hand.append(self.draw())

    def max_total(self):
        return max(p.total for p in self.players)

    def no_more_can_act(self):
        if sum(p.can_act() for p in self.players) <= 1:
            mx = self.max_total()
            return not any(p.can_act() and p.total < mx for p in self.players)
        return False

    def calculate_next_player(self):
        idx = self.next_player + 1
        self.next_player = -1
        if not self.no_more_can_act():
            for _ in range(self.n):
                p = self.players[idx % self.n]
                if p.can_act():
                    self.next_player = p.id
                    break
                idx += 1

    def not_folded(self):
        return sum(not p.folded for p in self.players)

    def is_turn_over(self):
        mx = self.max_total()
        for p in self.players:
            if p.can_act():
                if not p.spoken:
                    if not any(q.can_act() and q.id != p.id for q in self.players):
                        if p.total < mx:
                            return False
                    else:
                        return False
                if p.total < mx:
                    return False
        return True

    def end_turn(self):
        if self.next_player != -1:
            self.last_player = self.next_player
            self.players[self.next_player].spoken = True
        self.deal_card = False
        if self.not_folded() == 1:
            self.calc_winnings = True
        elif self.is_turn_over():
            if self.no_more_can_act():
                while len(self.board) < 5:
                    self.deal_board_cards()
                self.calc_winnings = True
            elif len(self.board) == 5:
                self.calc_winnings = True
            else:
                self.deal_card = True
                self.last_player = -1
                self.next_player = self.dealer_id

    def deal_board_cards(self):
        self.draw()                                        # burn
        for _ in range(3 if not self.board else 1):
            self.board.append(self.draw())
        self.reset_round()

    def deal(self):
        ret = self.deal_card
        if ret:
            self.deal_board_cards()
        self.deal_card = False
        return ret

    def preflop(self):
        return not self.board

    def call_amount(self, p: P):
        if self.preflop():
            return max(self.bb, self.max_total()) - p.total
        return self.max_total() - p.total

    def check_possible(self):
        return self.call_amount(self.players[self.next_player]) == 0

    def raise_cap(self):
        return self.raise_nb > RAISE_CAP

    # ------------------------------------------------------------------ ActionUtils
    def possible_actions(self) -> List[str]:
        pid = self.next_player
        p = self.players[pid]
        call = self.call_amount(p)
        cap = self.raise_cap()
        out = []
        if call > 0:
            if call < p.stack:
                out.append("CALL")
            elif not cap:
                out.append("ALL-IN")
        if self.last_raiser != pid and not cap:
            if "ALL-IN" not in out:
                out.append("ALL-IN")
            min_raise = self.bb if self.is_first_bet() else self.last_round_raise
            min_amount = min_raise + self.last_total_round_bet - p.rnd
            if min_amount < p.stack:
                out.append(f"BET_{min_amount}")
        if self.check_possible():
            out.append("CHECK")
        out.append("FOLD")
        return out

    @staticmethod
    def parse(s: str):
        """Action.create / ActionInfo.create: returns (type, amount) or ('FOLD', 0) on invalid input."""
        s = s.upper().strip()
        if s.startswith("BET_"):
            s = s.replace("BET_", "BET ")
        tok = s.split(" ")
        names = {"FOLD", "CHECK", "ALL_IN", "BET", "CALL", "TIMEOUT"}
        t = "ALL_IN" if tok[0] == "ALL-IN" else tok[0]
        if t not in names or len(tok) > 2:
            return ("FOLD", 0, True)
        if t == "BET":
            if len(tok) != 2:
                return ("FOLD", 0, True)
            try:
                a = int(tok[1])
                if not -2**31 <= a < 2**31:
                    raise ValueError
            except ValueError:
                return ("FOLD", 0, True)
            return ("BET", a, False)
        if len(tok) > 1:
            return ("FOLD", 0, True)
        return (t, 0, False)

    def replace(self, t, amount):
        """ActionUtils.calculatePossibleBet: action replacement rules."""
        if t in ("FOLD", "TIMEOUT"):
            return t, amount
        pid = self.next_player
        p = self.players[pid]
        call = self.call_amount(p)
        cap = self.raise_cap()
        call_action = ("CALL", 0) if call < p.stack else ("ALL_IN", 0)
        if t == "CALL":
            if self.check_possible():
                return "CHECK", 0
            if call >= p.stack:
                return "ALL_IN", 0
            return t, 0
        if t == "ALL_IN":
            new = (t, 0)
            if self.last_raiser == pid and call < p.stack:
                new = ("CALL", 0)
            if cap:
                new = call_action
            return new
        if t == "BET":
            if amount <= 0:
                return "FOLD", 0
            if amount <= call:
                return call_action
            if self.last_raiser == pid or cap:
                return call_action
            if amount >= p.stack:
                return "ALL_IN", 0
            raise_ = p.rnd + amount - self.last_total_round_bet
            min_raise = self.bb if self.is_first_bet() else self.last_round_raise
            if raise_ < min_raise:
                nb = min_raise + self.last_total_round_bet - p.rnd
                return ("ALL_IN", 0) if nb >= p.stack else ("BET", nb)
            return "BET", amount
        # CHECK
        if not self.check_possible():
            return "FOLD", 0
        return "CHECK", 0

    def do_action(self, t, amount):
        p = self.players[self.next_player]
        if t == "FOLD":
            p.folded = True
        elif t == "CHECK":
            pass
        elif t == "ALL_IN":
            self.bet_chips(p, p.stack)
        elif t == "CALL":
            self.bet_chips(p, self.call_amount(p))
        elif t == "BET":
            self.bet_chips(p, amount)
        elif t == "TIMEOUT":
            p.stack = 0
            p.timeout = True
            p.folded = True

    # ------------------------------------------------------------------ winnings
    def calculate_player_winnings(self):
        if not self.calc_winnings:
            return False
        self.calc_winnings = False
        self.deal()
        n = self.n
        bets = [p.total for p in self.players]
        folded = [p.folded for p in self.players]
        value = [None] * n
        if self.not_folded() > 1:
            for p in self.players:
                if not p.folded and len(self.board) == 5:
                    value[p.id] = best7(p.hand, self.board)
        win = [0] * n
        while True:
            live = [bets[i] for i in range(n) if not folded[i]]
            mn, mx = min(live), max(live)
            cand = [i for i in range(n) if not folded[i]]
            best = max((value[i] or (-1,)) for i in cand)
            winners = [i for i in cand if (value[i] or (-1,)) == best]
            tot = 0
            for i in range(n):
                t = min(bets[i], mn)
                tot += t
                bets[i] -= t
                if bets[i] == 0:
                    folded[i] = True
            share, rem = divmod(tot, len(winners))
            for w in winners:
                win[w] += share
            j = self.dealer_id + 1                         # odd chips: first winner after the dealer
            while j % n not in winners:
                j += 1
            win[j % n] += rem
            if mn == mx:
                break
        for i in range(n):
            win[i] += bets[i]                              # uncalled remainder returned
        for p in self.players:
            p.stack += win[p.id]
            p.win_amount = win[p.id] - p.total
        self.calculate_elimination_ranks()
        self.over = True
        return True

    def calculate_elimination_ranks(self):
        next_rank = sum(p.elim_rank >= 0 for p in self.players)
        while True:
            busted = [p for p in self.players if p.elim_rank == -1 and p.stack == 0]
            if not busted:
                break
            mn = min(p.total for p in busted)
            grp = [p for p in busted if p.total == mn]
            for p in grp:
                p.elim_rank = next_rank
                p.score = next_rank - self.n
            next_rank += len(grp)

    def is_game_over(self):
        return self.over and sum(p.stack != 0 for p in self.players) == 1

    # ------------------------------------------------------------------ input protocol
    @staticmethod
    def cards_str(cards, pad=0):
        cs = list(cards) + ["X"] * (pad - len(cards))
        return "_".join(cs)

    def record_showdown(self):
        # ShowDownInfo: every non-eliminated player's hole cards are revealed (SHOW_FOLDED_CARDS=true)
        cards = [None if p.eliminated else list(p.hand) for p in self.players]
        self.showdowns[self.hand_nb] = (self.cards_str(self.board, 5), cards)

    def build_obs(self, pid) -> Obs:
        n = self.n
        acts = []
        for r in range(self.last_sent_round[pid] + 1, self.turn):
            t, h, who, a, b = self.round_infos[r]
            acts.append(f"{t} {h} {who} {a} {b}")
        self.last_sent_round[pid] = self.turn - 1
        sds = []
        for h in range(self.last_sent_hand[pid] + 1, self.hand_nb):
            b, cards = self.showdowns[h]
            parts = []
            for i, c in enumerate(cards):
                parts += ["E", "E"] if c is None else c
            sds.append(f"{h} {b} {'_'.join(parts)}")
        self.last_sent_hand[pid] = self.hand_nb - 1
        return Obs(SMALL_BLIND, BIG_BLIND, HAND_NB_BY_LEVEL, LEVEL_MULT, TOTAL_BUY_IN // n, self.first_bb, n, pid,
                   self.turn, self.hand_nb, [p.stack for p in self.players], [p.total for p in self.players],
                   self.cards_str(self.board, 5), "_".join(self.players[pid].hand), acts, sds,
                   self.possible_actions())

    # ------------------------------------------------------------------ Referee.gameTurn
    def game_turn(self, t, agents):
        if self.calculate_player_winnings():
            return self.do_board_over()
        if self.over or t == 1:                            # initBoard
            self.frame = "DEAL_PLAYER"
            if t != 1:
                self.reset_hand()
                self.init_deck()
                self.init_blind()
                self.calculate_next_player()
            self.deal_first()
            self.calc_next = False
            return
        n_cards = len(self.board)
        self.end_turn()
        if n_cards != len(self.board):                    # all-in runout, no inputs requested
            if self.last_hand_round != self.hand_nb:
                self.turn += 1
                if self.turn > MAX_TURN:
                    # Java: InputSender.roundInfos has MAX_TURN+1 slots -> ArrayIndexOutOfBoundsException (referee crash)
                    raise RuntimeError("referee would crash: NONE round 601 (InputSender.java:21,30)")
                self.round_infos[self.turn] = (self.turn, self.hand_nb, -1, "NONE", self.cards_str(self.board, 5))
                self.last_hand_round = self.hand_nb
            return
        if self.calculate_player_winnings():
            return self.do_board_over()
        if self.turn == MAX_TURN:                          # cancel unfinished hand, end game
            if not self.over:
                for p in self.players:
                    p.stack += p.total
                    p.total = 0
            self.cancelled = True
            self.final_scores()
            self.game_over = True
            return
        if self.deal():
            self.calculate_next_player()
            self.calc_next = False
            return
        if self.calc_next:
            self.calculate_next_player()
        self.calc_next = True
        pid = self.next_player
        self.turn += 1
        obs = self.build_obs(pid)
        try:
            out = agents[pid](obs)
        except TimeoutError:
            out = None
        if out is None:
            t_, a_, err = "TIMEOUT", 0, True
        else:
            t_, a_, err = self.parse(out.split(";")[0])
        t_, a_ = self.replace(t_, a_)
        shown = {"ALL_IN": "ALL-IN"}.get(t_, t_) + (f"_{a_}" if t_ == "BET" else "")
        self.round_infos[self.turn] = (self.turn, self.hand_nb, pid, shown, self.cards_str(self.board, 5))
        self.last_hand_round = self.hand_nb
        self.do_action(t_, a_)
        self.log.append((self.turn, self.hand_nb, pid, shown))

    def do_board_over(self):
        self.record_showdown()
        if self.is_game_over():
            self.final_scores()
            self.game_over = True

    def final_scores(self):
        for p in self.players:
            if p.stack > 0:
                p.score = p.stack

    def run(self, agents: List[Callable[[Obs], Optional[str]]]):
        t = 0
        while not self.game_over and t < MAX_REFEREE_TURN:
            t += 1
            self.game_turn(t, agents)
        return dict(hands=self.hand_nb, rounds=self.turn, cancelled=self.cancelled,
                    scores=[p.score for p in self.players], stacks=[p.stack for p in self.players])


def obs_to_stdin(obs: Obs, first: bool) -> str:
    """Exact stdin text the referee sends for this decision (InputSender.sendInputs, InputSender.java:49-81)."""
    lines = []
    if first:
        lines += [obs.small_blind, obs.big_blind, obs.hand_nb_by_level, obs.level_mult, obs.buy_in,
                  obs.first_bb_id, obs.player_nb, obs.player_id]
    lines += [obs.round, obs.hand_nb]
    lines += [f"{s} {c}" for s, c in zip(obs.stacks, obs.chip_in_pot)]
    lines += [obs.board, obs.cards, len(obs.actions), *obs.actions, len(obs.showdowns), *obs.showdowns,
              len(obs.possible), *obs.possible]
    return "\n".join(str(x) for x in lines) + "\n"


if __name__ == "__main__":
    # demo: 3 calling stations, print the stdin player 0 receives on its first two turns
    seen = {}

    def station(pid):
        def agent(obs):
            if pid == 0 and len(seen) < 2:
                seen[obs.round] = obs_to_stdin(obs, first=not seen)
            return "CALL"
        return agent

    res = PokerSim(3, -7454168821517212000).run([station(i) for i in range(3)])
    for r, txt in seen.items():
        print(f"--- stdin for player 0 at round {r} ---\n{txt}")
    print(res)
