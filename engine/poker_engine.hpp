#pragma once
// poker_engine.hpp - C++ port of the CodinGame "Poker" referee (wala-fr/CodingamePoker @ ac30d97),
// method for method the same as sim/poker_sim.py, which is the validated Python port.
//
// Board holds the rules (blinds, betting, action replacement, side pots, elimination ranks) with no
// deck and no RNG, so the bot's tracker (tracker.hpp) can drive it from stdin.  Engine adds the deck,
// the inputs and the Referee.gameTurn loop.
//
// Game loop: Engine eng(n, seed); eng.run(agents) with one Agent per seat.  Agent::act receives the
// Obs the real bot would get on stdin (Obs::to_stdin renders the exact text) and returns the bot's
// raw output line, or false for a timeout.  Rounds, the 600-decision cap with refund, NONE rounds,
// blind posting (every non-BB player posts the SB), action replacement, side pots, the odd chip,
// elimination ranks and the SHA1PRNG deck are all reproduced; engine/check.py proves it against
// poker_sim.py (random-action fuzz) and against the 381 recorded games.
//
// Cards are ints 0..51 = 4*rank + suit, rank 0..12 = 2..A, suit 0..3 = C,D,H,S (the referee's
// CardUtils order and pe7c's encoding).
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "sha1prng.hpp"

namespace pk {

// ----------------------------------------------------------------------------- parameters
constexpr int SMALL_BLIND = 5, BIG_BLIND = 10;    // model/variable/Parameter.java
constexpr int TOTAL_BUY_IN = 4800;
constexpr int HAND_NB_BY_LEVEL = 10, LEVEL_MULT = 2;
constexpr int RAISE_CAP = 10;
constexpr int MAX_TURN = 600;                      // game/RefereeParameter.java (decisions per game)
constexpr int MAX_REFEREE_TURN = 10000;            // frames
constexpr const char* RANKS = "23456789TJQKA";
constexpr const char* SUITS = "CDHS";

inline std::string card_str(int c) { return std::string{RANKS[c >> 2], SUITS[c & 3]}; }
inline int card_from(const char* s) {               // "AS" -> 51; -1 if not a card
  const char* r = std::strchr(RANKS, s[0]);
  const char* u = std::strchr(SUITS, s[1]);
  if (!s[0] || !s[1] || !r || !u) return -1;
  return (int)(r - RANKS) * 4 + (int)(u - SUITS);
}

// ----------------------------------------------------------------------------- hand evaluation
// Same ordering as poker_sim.eval5 (standard rankings, wheel = 5-high straight): category<<20 then
// the tuple's rank fields in 4-bit nibbles, so two values compare like the Python tuples.
inline uint32_t eval5(const int* c) {
  int rs[5], cnt[15] = {0};
  bool flush = true;
  for (int i = 0; i < 5; i++) rs[i] = (c[i] >> 2) + 2, cnt[rs[i]]++, flush &= (c[i] & 3) == (c[0] & 3);
  std::sort(rs, rs + 5, [](int a, int b) { return a > b; });
  int uniq = 0;
  for (int r = 14; r >= 2; r--) uniq += cnt[r] > 0;
  int straight_hi = 0;
  if (uniq == 5) {
    if (rs[0] - rs[4] == 4) straight_hi = rs[0];
    else if (rs[0] == 14 && rs[1] == 5 && rs[4] == 2) straight_hi = 5;
  }
  // counts sorted by (count, rank) descending
  int shape[5], ordered[5], k = 0;
  for (int n = 4; n >= 1; n--)
    for (int r = 14; r >= 2; r--)
      if (cnt[r] == n) shape[k] = n, ordered[k] = r, k++;
  auto pack = [](int cat, const int* v, int n) {
    uint32_t x = (uint32_t)cat << 20;
    for (int i = 0; i < n; i++) x |= (uint32_t)v[i] << (16 - 4 * i);
    return x;
  };
  if (straight_hi && flush) return pack(8, &straight_hi, 1);
  if (k == 2 && shape[0] == 4) return pack(7, ordered, 2);
  if (k == 2 && shape[0] == 3) return pack(6, ordered, 2);
  if (flush) return pack(5, rs, 5);
  if (straight_hi) return pack(4, &straight_hi, 1);
  if (k == 3 && shape[0] == 3) return pack(3, ordered, 3);
  if (k == 3 && shape[0] == 2) return pack(2, ordered, 3);
  if (k == 4) return pack(1, ordered, 4);
  return pack(0, rs, 5);
}

inline uint32_t best7(const int* hole, const std::vector<int>& board) {  // best of C(7,5)
  int all[7] = {hole[0], hole[1], board[0], board[1], board[2], board[3], board[4]};
  uint32_t best = 0;
  int five[5];
  for (int a = 0; a < 7; a++)
    for (int b = a + 1; b < 7; b++) {
      int k = 0;
      for (int i = 0; i < 7; i++)
        if (i != a && i != b) five[k++] = all[i];
      best = std::max(best, eval5(five));
    }
  return best;
}

// ----------------------------------------------------------------------------- model
struct Player {
  int id = 0, stack = 0;
  int total = 0;            // totalBetAmount (whole hand) == "chipInPot" on stdin
  int rnd = 0;              // roundBetAmount (current street)
  bool folded = false, allin = false, spoken = false, eliminated = false, timeout = false;
  int elim_rank = -1, score = 0;
  int hand[2] = {-1, -1};
  int n_hand = 0;
  bool can_act() const { return !folded && !allin; }
};

struct Obs {
  // init (meaningful on the first call for this player)
  int small_blind = 0, big_blind = 0, hand_nb_by_level = 0, level_mult = 0, buy_in = 0, first_bb_id = 0, player_nb = 0, player_id = 0;
  // per turn
  int round = 0, hand_nb = 0;
  std::vector<int> stacks, chip_in_pot;
  std::string board, cards;
  std::vector<std::string> actions;    // "round handNb playerId ACTION BOARD"
  std::vector<std::string> showdowns;  // "handNb BOARD CARDS"
  std::vector<std::string> possible;

  // The exact stdin text the referee sends for this decision (InputSender.sendInputs).
  std::string to_stdin(bool first) const {
    std::string s;
    auto line = [&](const std::string& x) { s += x; s += '\n'; };
    auto num = [&](int x) { line(std::to_string(x)); };
    if (first) {
      num(small_blind); num(big_blind); num(hand_nb_by_level); num(level_mult);
      num(buy_in); num(first_bb_id); num(player_nb); num(player_id);
    }
    num(round); num(hand_nb);
    for (size_t i = 0; i < stacks.size(); i++) line(std::to_string(stacks[i]) + " " + std::to_string(chip_in_pot[i]));
    line(board); line(cards);
    num((int)actions.size()); for (auto& a : actions) line(a);
    num((int)showdowns.size()); for (auto& a : showdowns) line(a);
    num((int)possible.size()); for (auto& a : possible) line(a);
    return s;
  }
};

struct Agent {
  virtual ~Agent() {}
  // Fill `out` with the bot's raw output line (may include ";message"); return false for a timeout.
  virtual bool act(const Obs& obs, std::string& out) = 0;
};

enum ActType { A_FOLD, A_CHECK, A_ALL_IN, A_BET, A_CALL, A_TIMEOUT };
inline const char* act_name(ActType t) {
  static const char* n[] = {"FOLD", "CHECK", "ALL-IN", "BET", "CALL", "TIMEOUT"};
  return n[t];
}

struct RoundInfo { int turn, hand, pid; std::string shown, board; };
struct LogEntry { int turn, hand, pid; std::string shown; };

// The rules of one table: everything in the referee's Board/ActionUtils/WinningCalculator that does
// not touch the deck.  Card identities come from draw(), which a Board alone does not know (-1).
class Board {
 public:
  int n = 0;
  std::vector<Player> players;
  int sb = SMALL_BLIND, bb = BIG_BLIND, level = 1, hand_nb = 0, bb_id = 0, sb_id = -1, dealer_id = -1;
  bool over = true, calc_winnings = false, deal_card = false, calc_next = false, game_over = false, cancelled = false;
  int turn = 0;
  int pot = 0;
  std::vector<int> board;                            // card ids; -1 = dealt but not yet known (tracker)
  int last_round_raise = 0, last_total_round_bet = 0, last_raiser = -1, raise_nb = 0, last_player = -1, next_player = -1;

  Board() {}
  Board(int n_, int first_bb) : n(n_) {
    if (n < 2 || n > 4) throw std::invalid_argument("n must be 2..4");
    players.resize(n);
    for (int i = 0; i < n; i++) players[i].id = i, players[i].stack = TOTAL_BUY_IN / n;
    bb_id = first_bb;
  }
  virtual ~Board() {}
  virtual int draw() { return -1; }

  // ------------------------------------------------------------------ Board helpers
  void reset_round() {
    for (auto& p : players) p.rnd = 0, p.spoken = false;
    last_round_raise = 0; last_total_round_bet = 0; last_raiser = -1; raise_nb = 0;
  }
  void reset_hand() {
    board.clear();
    for (auto& p : players) {
      p.n_hand = 0; p.hand[0] = p.hand[1] = -1;
      p.eliminated = p.stack == 0;
      p.allin = false;
      p.folded = p.stack == 0;
      p.total = 0;
    }
    over = false; pot = 0;
    reset_round();
    hand_nb++;
    if (hand_nb % HAND_NB_BY_LEVEL == 0) level++, sb *= LEVEL_MULT, bb *= LEVEL_MULT;   // Board.increaseLevel
    init_positions();
  }
  void init_positions() {
    if (hand_nb > 1) {                               // calculateNextBigBlindId
      do bb_id = (bb_id + 1) % n; while (players[bb_id].stack == 0);
    }
    sb_id = dealer_id = -1;
    int nb = 0;
    for (int i = 0; i < n; i++) {
      int idx = ((bb_id - 1 - i) % n + n) % n;
      if (!players[idx].folded) {
        nb++;
        if (sb_id == -1) sb_id = idx;
        else if (dealer_id == -1) dealer_id = idx;
      }
    }
    if (nb == 2) dealer_id = sb_id;
    last_player = -1; next_player = bb_id;
    last_total_round_bet = bb; last_round_raise = sb; last_raiser = -1; raise_nb = 1;
  }
  void init_blind() {
    // every non-folded player posts at least the SMALL blind (Board.initBlind)
    for (auto& p : players)
      if (!p.folded) {
        int bet = p.id == bb_id ? bb : sb;
        if (p.stack < (p.id == sb_id ? sb : bb)) bet = p.stack;
        bet_chips(p, bet);
      }
  }
  bool is_first_bet() const { return last_raiser == -1; }
  void bet_chips(Player& p, int value) {
    value = std::min(value, p.stack);
    p.stack -= value; p.total += value; p.rnd += value;
    if (p.stack == 0) p.allin = true;
    int raise = p.rnd - last_total_round_bet;
    if ((is_first_bet() && raise >= bb) || (!is_first_bet() && raise >= last_round_raise)) {
      last_round_raise = raise; last_raiser = p.id; last_total_round_bet += raise; raise_nb++;
    }
    pot += value;
  }
  void deal_first() {
    for (int k = 0; k < 2; k++)
      for (int i = 0; i < n; i++) {
        Player& p = players[(dealer_id + 1 + i) % n];
        if (!p.folded) p.hand[p.n_hand++] = draw();
      }
  }
  int max_total() const { int m = 0; for (auto& p : players) m = std::max(m, p.total); return m; }
  bool no_more_can_act() const {
    int k = 0;
    for (auto& p : players) k += p.can_act();
    if (k <= 1) {
      int mx = max_total();
      for (auto& p : players) if (p.can_act() && p.total < mx) return false;
      return true;
    }
    return false;
  }
  void calculate_next_player() {
    int idx = next_player + 1;
    next_player = -1;
    if (!no_more_can_act())
      for (int i = 0; i < n; i++, idx++) {
        const Player& p = players[idx % n];
        if (p.can_act()) { next_player = p.id; break; }
      }
  }
  int not_folded() const { int k = 0; for (auto& p : players) k += !p.folded; return k; }
  bool is_turn_over() const {
    int mx = max_total();
    for (auto& p : players)
      if (p.can_act()) {
        if (!p.spoken) {
          bool other = false;
          for (auto& q : players) other |= q.can_act() && q.id != p.id;
          if (!other) { if (p.total < mx) return false; }
          else return false;
        }
        if (p.total < mx) return false;
      }
    return true;
  }
  void end_turn() {
    if (next_player != -1) last_player = next_player, players[next_player].spoken = true;
    deal_card = false;
    if (not_folded() == 1) calc_winnings = true;
    else if (is_turn_over()) {
      if (no_more_can_act()) {
        while (board.size() < 5) deal_board_cards();
        calc_winnings = true;
      } else if (board.size() == 5) calc_winnings = true;
      else { deal_card = true; last_player = -1; next_player = dealer_id; }
    }
  }
  void deal_board_cards() {
    draw();                                           // burn
    int k = board.empty() ? 3 : 1;
    for (int i = 0; i < k; i++) board.push_back(draw());
    reset_round();
  }
  bool deal() {
    bool ret = deal_card;
    if (ret) deal_board_cards();
    deal_card = false;
    return ret;
  }
  bool preflop() const { return board.empty(); }
  int call_amount(const Player& p) const {
    if (preflop()) return std::max(bb, max_total()) - p.total;
    return max_total() - p.total;
  }
  bool check_possible() const { return call_amount(players[next_player]) == 0; }
  bool raise_cap() const { return raise_nb > RAISE_CAP; }

  // ------------------------------------------------------------------ ActionUtils
  std::vector<std::string> possible_actions() const {
    int pid = next_player;
    const Player& p = players[pid];
    int call = call_amount(p);
    bool cap = raise_cap();
    std::vector<std::string> out;
    bool allin = false;
    if (call > 0) {
      if (call < p.stack) out.push_back("CALL");
      else if (!cap) out.push_back("ALL-IN"), allin = true;
    }
    if (last_raiser != pid && !cap) {
      if (!allin) out.push_back("ALL-IN");
      int min_raise = is_first_bet() ? bb : last_round_raise;
      int min_amount = min_raise + last_total_round_bet - p.rnd;
      if (min_amount < p.stack) out.push_back("BET_" + std::to_string(min_amount));
    }
    if (check_possible()) out.push_back("CHECK");
    out.push_back("FOLD");
    return out;
  }

  // Action.create / ActionInfo.create on the first ';'-part of the output, upper-cased and trimmed.
  // Returns {type, amount, error}; an unparseable string is FOLD with error = true.
  struct Parsed { ActType t; int amount; bool err; };
  static Parsed parse(std::string s) {
    // Referee: outputs[0].toUpperCase().trim()
    for (auto& ch : s) ch = (char)toupper((unsigned char)ch);
    size_t a = 0, b = s.size();
    while (a < b && (unsigned char)s[a] <= ' ') a++;
    while (b > a && (unsigned char)s[b - 1] <= ' ') b--;
    s = s.substr(a, b - a);
    if (s.rfind("BET_", 0) == 0) {                   // Action.create: replaceAll("BET_", "BET ")
      std::string t;
      for (size_t i = 0; i < s.size();) {
        if (s.compare(i, 4, "BET_") == 0) t += "BET ", i += 4; else t += s[i++];
      }
      s = t;
    }
    std::vector<std::string> tok;                    // split(" ", -1): keeps empty tokens
    size_t start = 0;
    for (;;) {
      size_t sp = s.find(' ', start);
      tok.push_back(s.substr(start, sp == std::string::npos ? std::string::npos : sp - start));
      if (sp == std::string::npos) break;
      start = sp + 1;
    }
    ActType t;
    const std::string& h = tok[0];
    if (h == "FOLD") t = A_FOLD; else if (h == "CHECK") t = A_CHECK;
    else if (h == "ALL-IN" || h == "ALL_IN") t = A_ALL_IN; else if (h == "BET") t = A_BET;
    else if (h == "CALL") t = A_CALL; else if (h == "TIMEOUT") t = A_TIMEOUT;
    else return {A_FOLD, 0, true};
    if (tok.size() > 2) return {A_FOLD, 0, true};
    if (t == A_BET) {
      if (tok.size() != 2) return {A_FOLD, 0, true};
      int64_t v = 0; bool neg = false; size_t i = 0;
      const std::string& d = tok[1];                 // Integer.parseInt: optional sign, ASCII digits
      if (!d.empty() && (d[0] == '-' || d[0] == '+')) neg = d[0] == '-', i = 1;
      if (i >= d.size()) return {A_FOLD, 0, true};
      for (; i < d.size(); i++) {
        if (d[i] < '0' || d[i] > '9') return {A_FOLD, 0, true};
        v = v * 10 + (d[i] - '0');
        if (v > (1LL << 31)) return {A_FOLD, 0, true};
      }
      if (neg) v = -v;
      if (v < -(1LL << 31) || v >= (1LL << 31)) return {A_FOLD, 0, true};
      return {A_BET, (int)v, false};
    }
    if (tok.size() > 1) return {A_FOLD, 0, true};
    return {t, 0, false};
  }

  // ActionUtils.calculatePossibleBet: replacement of illegal actions
  std::pair<ActType, int> replace(ActType t, int amount) const {
    if (t == A_FOLD || t == A_TIMEOUT) return {t, amount};
    int pid = next_player;
    const Player& p = players[pid];
    int call = call_amount(p);
    bool cap = raise_cap();
    std::pair<ActType, int> call_action = call < p.stack ? std::make_pair(A_CALL, 0) : std::make_pair(A_ALL_IN, 0);
    if (t == A_CALL) {
      if (check_possible()) return {A_CHECK, 0};
      if (call >= p.stack) return {A_ALL_IN, 0};
      return {A_CALL, 0};
    }
    if (t == A_ALL_IN) {
      std::pair<ActType, int> nw = {A_ALL_IN, 0};
      if (last_raiser == pid && call < p.stack) nw = {A_CALL, 0};
      if (cap) nw = call_action;
      return nw;
    }
    if (t == A_BET) {
      if (amount <= 0) return {A_FOLD, 0};
      if (amount <= call) return call_action;
      if (last_raiser == pid || cap) return call_action;
      if (amount >= p.stack) return {A_ALL_IN, 0};
      int raise = p.rnd + amount - last_total_round_bet;
      int min_raise = is_first_bet() ? bb : last_round_raise;
      if (raise < min_raise) {
        int nb = min_raise + last_total_round_bet - p.rnd;
        return nb >= p.stack ? std::make_pair(A_ALL_IN, 0) : std::make_pair(A_BET, nb);
      }
      return {A_BET, amount};
    }
    // CHECK
    if (!check_possible()) return {A_FOLD, 0};
    return {A_CHECK, 0};
  }

  void do_action(ActType t, int amount) {
    Player& p = players[next_player];
    switch (t) {
      case A_FOLD: p.folded = true; break;
      case A_CHECK: break;
      case A_ALL_IN: bet_chips(p, p.stack); break;
      case A_CALL: bet_chips(p, call_amount(p)); break;
      case A_BET: bet_chips(p, amount); break;
      case A_TIMEOUT: p.stack = 0; p.timeout = true; p.folded = true; break;
    }
  }

  // ------------------------------------------------------------------ winnings
  bool calculate_player_winnings() {
    if (!calc_winnings) return false;
    calc_winnings = false;
    deal();
    std::vector<int> bets(n), win(n, 0);
    std::vector<char> folded(n);
    std::vector<int64_t> value(n, -1);
    for (int i = 0; i < n; i++) bets[i] = players[i].total, folded[i] = players[i].folded;
    if (not_folded() > 1)
      for (auto& p : players)
        if (!p.folded && board.size() == 5) value[p.id] = best7(p.hand, board);
    for (;;) {
      int mn = 1 << 30, mx = 0;
      int64_t best = -2;
      for (int i = 0; i < n; i++)
        if (!folded[i]) mn = std::min(mn, bets[i]), mx = std::max(mx, bets[i]), best = std::max(best, value[i]);
      std::vector<int> winners;
      for (int i = 0; i < n; i++) if (!folded[i] && value[i] == best) winners.push_back(i);
      int tot = 0;
      for (int i = 0; i < n; i++) {
        int t = std::min(bets[i], mn);
        tot += t; bets[i] -= t;
        if (bets[i] == 0) folded[i] = true;
      }
      if (winners.empty()) {                             // impossible in the referee; a desynced Tracker
        for (int i = 0; i < n; i++) win[i] += std::min(players[i].total, mn);   //   gets its chips back
        break;
      }
      int share = tot / (int)winners.size(), rem = tot % (int)winners.size();
      for (int w : winners) win[w] += share;
      int j = dealer_id + 1;                           // odd chips: first winner after the dealer
      while (std::find(winners.begin(), winners.end(), j % n) == winners.end()) j++;
      win[j % n] += rem;
      if (mn == mx) break;
    }
    for (int i = 0; i < n; i++) win[i] += bets[i];      // uncalled remainder returned
    for (auto& p : players) p.stack += win[p.id];
    calculate_elimination_ranks();
    over = true;
    return true;
  }
  void calculate_elimination_ranks() {
    int next_rank = 0;
    for (auto& p : players) next_rank += p.elim_rank >= 0;
    for (;;) {
      int mn = 1 << 30, cnt = 0;
      for (auto& p : players) if (p.elim_rank == -1 && p.stack == 0) mn = std::min(mn, p.total), cnt++;
      if (!cnt) break;
      int grp = 0;
      for (auto& p : players)
        if (p.elim_rank == -1 && p.stack == 0 && p.total == mn) p.elim_rank = next_rank, p.score = next_rank - n, grp++;
      next_rank += grp;
    }
  }
  bool is_game_over() const {
    int alive = 0;
    for (auto& p : players) alive += p.stack != 0;
    return over && alive == 1;
  }

  static std::string cards_str(const int* c, int k, int pad) {
    std::string s;
    for (int i = 0; i < pad; i++) {
      if (i) s += '_';
      s += i < k ? (c[i] < 0 ? "?" : card_str(c[i])) : "X";
    }
    return s;
  }
  std::string board_str() const { return cards_str(board.data(), (int)board.size(), 5); }
};

class Engine : public Board {
 public:
  Sha1Prng rng;
  int first_bb;
  std::vector<int> deck;
  int deck_idx = 0;
  std::vector<RoundInfo> round_infos;                // index = turn (0 unused)
  struct Showdown { std::string board; std::vector<std::string> cards; std::vector<char> elim; };
  std::vector<Showdown> showdowns;                   // index = hand
  std::vector<int> last_sent_round, last_sent_hand;
  int last_hand_round = 0;
  std::vector<LogEntry> log;
  std::vector<int> replaced;                         // per seat: outputs the referee replaced (illegal or unparseable)
  std::vector<std::string> replaced_log;             // the first few, "seat: raw -> shown" (debugging)

  Engine(int n_, int64_t seed) : rng(seed) {
    if (n_ < 2 || n_ > 4) throw std::invalid_argument("n must be 2..4");
    first_bb = rng.next_int(n_);                     // Referee.java: first BB
    static_cast<Board&>(*this) = Board(n_, first_bb);
    round_infos.resize(MAX_TURN + 2);
    showdowns.resize(1);
    last_sent_round.assign(n, 0);
    last_sent_hand.assign(n, 0);
    replaced.assign(n, 0);
    // Referee.init
    reset_hand(); init_deck(); init_blind(); calculate_next_player();
  }
  void init_deck() {
    deck.resize(52);
    for (int i = 0; i < 52; i++) deck[i] = i;          // rank-major, CardUtils order
    rng.shuffle(deck);
    int r = rng.next_int(52);                           // Deck.cut
    std::rotate(deck.begin(), deck.begin() + r, deck.end());
    deck_idx = 0;
  }
  int draw() override { return deck[deck_idx++]; }

  // ------------------------------------------------------------------ input protocol
  void record_showdown() {
    // ShowDownInfo: every non-eliminated player's hole cards are revealed (SHOW_FOLDED_CARDS=true)
    Showdown sd;
    sd.board = board_str();
    for (auto& p : players) sd.cards.push_back(cards_str(p.hand, p.n_hand, p.n_hand)), sd.elim.push_back(p.eliminated);
    if ((int)showdowns.size() <= hand_nb) showdowns.resize(hand_nb + 1);
    showdowns[hand_nb] = sd;
  }
  Obs build_obs(int pid) {
    Obs o;
    o.small_blind = SMALL_BLIND; o.big_blind = BIG_BLIND; o.hand_nb_by_level = HAND_NB_BY_LEVEL;
    o.level_mult = LEVEL_MULT; o.buy_in = TOTAL_BUY_IN / n; o.first_bb_id = first_bb; o.player_nb = n; o.player_id = pid;
    o.round = turn; o.hand_nb = hand_nb;
    for (auto& p : players) o.stacks.push_back(p.stack), o.chip_in_pot.push_back(p.total);
    o.board = board_str();
    o.cards = cards_str(players[pid].hand, players[pid].n_hand, players[pid].n_hand);
    for (int r = last_sent_round[pid] + 1; r < turn; r++) {
      const RoundInfo& ri = round_infos[r];
      o.actions.push_back(std::to_string(ri.turn) + " " + std::to_string(ri.hand) + " " + std::to_string(ri.pid) + " " + ri.shown + " " + ri.board);
    }
    last_sent_round[pid] = turn - 1;
    for (int h = last_sent_hand[pid] + 1; h < hand_nb; h++) {
      const Showdown& sd = showdowns[h];
      std::string parts;
      for (size_t i = 0; i < sd.cards.size(); i++) {
        if (i) parts += '_';
        parts += sd.elim[i] ? "E_E" : sd.cards[i];
      }
      o.showdowns.push_back(std::to_string(h) + " " + sd.board + " " + parts);
    }
    last_sent_hand[pid] = hand_nb - 1;
    o.possible = possible_actions();
    return o;
  }

  // ------------------------------------------------------------------ Referee.gameTurn
  void game_turn(int t, std::vector<Agent*>& agents) {
    if (calculate_player_winnings()) return do_board_over();
    if (over || t == 1) {                              // initBoard
      if (t != 1) { reset_hand(); init_deck(); init_blind(); calculate_next_player(); }
      deal_first();
      calc_next = false;
      return;
    }
    size_t n_cards = board.size();
    end_turn();
    if (n_cards != board.size()) {                     // all-in runout: a NONE round, no inputs
      if (last_hand_round != hand_nb) {
        turn++;
        if (turn > MAX_TURN) throw std::runtime_error("referee would crash: NONE round 601 (InputSender.java:21,30)");
        round_infos[turn] = {turn, hand_nb, -1, "NONE", board_str()};
        last_hand_round = hand_nb;
      }
      return;
    }
    if (calculate_player_winnings()) return do_board_over();
    if (turn == MAX_TURN) {                            // cancel the unfinished hand, end the game
      if (!over) for (auto& p : players) p.stack += p.total, p.total = 0;
      cancelled = true;
      final_scores();
      game_over = true;
      return;
    }
    if (deal()) { calculate_next_player(); calc_next = false; return; }
    if (calc_next) calculate_next_player();
    calc_next = true;
    int pid = next_player;
    turn++;
    Obs obs = build_obs(pid);
    std::string out;
    bool ok = agents[pid]->act(obs, out);
    Parsed pr;
    if (!ok) pr = {A_TIMEOUT, 0, true};
    else pr = parse(out.substr(0, out.find(';')));
    auto rp = replace(pr.t, pr.amount);
    if (ok && (pr.err || rp.first != pr.t || (rp.first == A_BET && rp.second != pr.amount))) {
      replaced[pid]++;
      if (replaced_log.size() < 50) replaced_log.push_back(std::to_string(pid) + ": " + out + " -> " + act_name(rp.first) + (rp.first == A_BET ? "_" + std::to_string(rp.second) : ""));
    }
    std::string shown = act_name(rp.first);
    if (rp.first == A_BET) shown += "_" + std::to_string(rp.second);
    round_infos[turn] = {turn, hand_nb, pid, shown, board_str()};
    last_hand_round = hand_nb;
    do_action(rp.first, rp.second);
    log.push_back({turn, hand_nb, pid, shown});
  }
  void do_board_over() {
    record_showdown();
    if (is_game_over()) { final_scores(); game_over = true; }
  }
  void final_scores() { for (auto& p : players) if (p.stack > 0) p.score = p.stack; }

  struct Result { int hands, rounds; bool cancelled; std::vector<int> scores, stacks; };
  Result run(std::vector<Agent*>& agents) {
    for (int t = 1; !game_over && t <= MAX_REFEREE_TURN; t++) game_turn(t, agents);
    Result r{hand_nb, turn, cancelled, {}, {}};
    for (auto& p : players) r.scores.push_back(p.score), r.stacks.push_back(p.stack);
    return r;
  }
};

}  // namespace pk
