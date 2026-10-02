#pragma once
// tracker.hpp - the bot's state tracker: a pk::Board driven by the stdin lines.
//
// Stdin is one snapshot at our turn, so the tracker replays everything that happened since the
// last turn through the same rules the referee uses: it starts each hand (positions, blinds), applies
// every action line (an ALL-IN line carries no amount: the Board knows the stack), handles NONE
// rounds, settles finished hands from their showdown line (all hole cards are revealed), and then
// checks itself against the snapshot: round, hand, stacks, chips in pot, board, and the offered
// actions, which pins down the minimum raise and the raise cap.  engine/check.py runs a Tracker per
// seat alongside the engine on every fuzz and replay decision.
//
// After apply(): players[], board, pot, blinds, positions, next_player (== me) and
// possible_actions() describe the current decision; hole() gives our cards.
#include <sstream>

#include "poker_engine.hpp"

namespace pk {

// Parse one turn of stdin into an Obs (InputSender.sendInputs layout).  `first` reads the 8 init
// lines too; keep the same Obs across turns, since later turns rely on its player_nb.  Returns
// false at end of input.
inline bool read_obs(std::istream& in, bool first, Obs& o) {
  auto getline = [&](std::string& s) { if (!std::getline(in, s)) return false; if (!s.empty() && s.back() == '\r') s.pop_back(); return true; };
  std::string s;
  if (first) {
    if (!(in >> o.small_blind >> o.big_blind >> o.hand_nb_by_level >> o.level_mult >> o.buy_in >> o.first_bb_id >> o.player_nb >> o.player_id)) return false;
  }
  if (o.player_nb < 2 || o.player_nb > 4) return false;
  if (!(in >> o.round >> o.hand_nb)) return false;
  o.stacks.assign(o.player_nb, 0); o.chip_in_pot.assign(o.player_nb, 0);
  for (int i = 0; i < o.player_nb; i++) in >> o.stacks[i] >> o.chip_in_pot[i];
  in >> o.board >> o.cards;
  int k;
  in >> k; getline(s);                                  // rest of the count line
  o.actions.clear(); for (int i = 0; i < k; i++) { getline(s); o.actions.push_back(s); }
  in >> k; getline(s);
  o.showdowns.clear(); for (int i = 0; i < k; i++) { getline(s); o.showdowns.push_back(s); }
  in >> k; getline(s);
  o.possible.clear(); for (int i = 0; i < k; i++) { getline(s); o.possible.push_back(s); }
  return (bool)in;
}

class Tracker : public Board {
 public:
  int me = -1;
  int first_bb = -1;
  int last_hand_round = 0;
  bool started = false;
  bool decision_pending = false;
  bool desynced = false;                 // a check failed this hand: stacks/pot were resynced from stdin
  int mismatches = 0;                    // total failed checks (dev builds log them)
  std::string last_error;

  const int* hole() const { return players[me].hand; }

  // "AD_QH_2S_X_X" -> {card ids, -1 for X}
  static std::vector<int> parse_cards(const std::string& b) {
    std::vector<int> out;
    size_t i = 0;
    while (i <= b.size()) {
      size_t j = b.find('_', i);
      std::string tok = b.substr(i, j == std::string::npos ? std::string::npos : j - i);
      if (!tok.empty()) out.push_back(tok == "X" ? -1 : card_from(tok.c_str()));
      if (j == std::string::npos) break;
      i = j + 1;
    }
    return out;
  }

  enum Ev { EV_SETTLE, EV_NONE, EV_CANCEL, EV_DECISION, EV_CONTINUE };

  // Referee.gameTurn between decisions, one branch per call; EV_CONTINUE means call again.
  Ev step() {
    if (decision_pending) return EV_DECISION;
    if (calc_winnings) return EV_SETTLE;                 // (A)/(D): needs the showdown line
    if (over) {                                          // (B) initBoard
      reset_hand(); init_blind(); calculate_next_player(); deal_first(); calc_next = false;
      return EV_CONTINUE;
    }
    size_t n_cards = board.size();
    end_turn();
    if (n_cards != board.size()) {                       // (C) all-in runout
      if (last_hand_round != hand_nb) { turn++; last_hand_round = hand_nb; return EV_NONE; }
      return EV_CONTINUE;
    }
    if (calc_winnings) return EV_SETTLE;
    if (turn == MAX_TURN) { cancelled = true; game_over = true; return EV_CANCEL; }
    if (deal()) { calculate_next_player(); calc_next = false; return EV_CONTINUE; }
    if (calc_next) calculate_next_player();
    calc_next = true;
    decision_pending = true;
    return EV_DECISION;
  }

  bool fail(const std::string& what) {
    mismatches++; last_error = what; desynced = true;
    return false;
  }

  // "AD_QH_2S_X_X": record known board cards; false on a contradiction
  bool fill_board(const std::string& b) {
    size_t i = 0, idx = 0;
    while (i < b.size()) {
      size_t j = b.find('_', i);
      std::string tok = b.substr(i, j == std::string::npos ? std::string::npos : j - i);
      if (tok != "X" && !tok.empty()) {
        int c = card_from(tok.c_str());
        if (c < 0) return fail("bad card " + tok);
        if (idx >= board.size()) return fail("board longer than tracked: " + b + " vs " + board_str());
        if (board[idx] == -1) board[idx] = c;
        else if (board[idx] != c) return fail("board card differs: " + b + " vs " + board_str());
      }
      idx++;
      if (j == std::string::npos) break;
      i = j + 1;
    }
    return true;
  }

  // This hand's actions in order, for strategy lookups and the opponent model: who did what on which street
  // (board size), the chips it added, the actor's whole-hand commitment and stack afterwards, whether it left
  // them all-in, and the situation before it: the amount to call, the pot, and whether a raise was pending.
  struct HandAct { int hand, street, pid, type, added, total_after, stack_after; bool allin; int call, pot_before; bool raised; };
  std::vector<HandAct> hand_log;
  // Called at the end of settle(), when every player's hole cards for the hand are known (the referee reveals
  // folded hands too) and hand_log still holds the hand's actions: the opponent model's observation point.
  struct SettleHook { virtual void on_settle(Tracker& t) = 0; virtual ~SettleHook() {} };
  SettleHook* hook = nullptr;

  // Apply a shown (post-replacement) action from an action line to next_player.
  bool apply_shown(const std::string& a) {
    int pid = next_player, street = board.size(), before = pid >= 0 ? players[pid].total : 0;
    int call_before = pid >= 0 ? call_amount(players[pid]) : 0, pot_before = pot;
    bool raised = last_raiser != -1 && last_raiser != pid;
    ActType t; int amt = 0;
    if (a == "FOLD") t = A_FOLD;
    else if (a == "CHECK") t = A_CHECK;
    else if (a == "CALL") t = A_CALL;
    else if (a == "ALL-IN") t = A_ALL_IN;
    else if (a == "TIMEOUT") t = A_TIMEOUT;
    else if (a.rfind("BET_", 0) == 0) { t = A_BET; amt = atoi(a.c_str() + 4); }
    else return fail("unknown action " + a);
    do_action(t, amt);
    if (pid >= 0) {
      if (!hand_log.empty() && hand_log.back().hand != hand_nb) hand_log.clear();
      const Player& p = players[pid];
      hand_log.push_back({hand_nb, street, pid, (int)t, p.total - before, p.total, p.stack, p.allin, call_before, pot_before, raised});
    }
    return true;
  }

  // Settle hand `hand_nb` from its showdown line "h BOARD c0_c1_E_E_..."
  bool settle(const std::string& line) {
    std::istringstream is(line);
    int h; std::string b, cards;
    is >> h >> b >> cards;
    if (h != hand_nb) return fail("showdown for hand " + std::to_string(h) + " while tracking " + std::to_string(hand_nb));
    if (!fill_board(b)) return false;
    std::vector<std::string> tok;
    size_t i = 0;
    while (i <= cards.size()) { size_t j = cards.find('_', i); tok.push_back(cards.substr(i, j == std::string::npos ? std::string::npos : j - i)); if (j == std::string::npos) break; i = j + 1; }
    if ((int)tok.size() != 2 * n) return fail("showdown cards: " + cards);
    for (int p = 0; p < n; p++) {
      if (tok[2 * p] == "E") { if (!players[p].eliminated) return fail("E_E for a live player"); continue; }
      int c0 = card_from(tok[2 * p].c_str()), c1 = card_from(tok[2 * p + 1].c_str());
      if (c0 < 0 || c1 < 0) return fail("bad showdown cards: " + cards);
      players[p].hand[0] = c0; players[p].hand[1] = c1; players[p].n_hand = 2;
    }
    if (not_folded() > 1 && board.size() == 5)
      for (int k = 0; k < 5; k++) if (board[k] < 0) return fail("showdown with unknown board card");
    if (hook) hook->on_settle(*this);                    // every hand is known now; hand_log still holds its actions
    calculate_player_winnings();                         // sets over = true
    if (is_game_over()) game_over = true;
    return true;
  }

  // Process one turn's stdin.  Returns false if any check failed (state resynced from the snapshot).
  bool apply(const Obs& o) {
    bool ok = true;
    if (!started) {
      static_cast<Board&>(*this) = Board(o.player_nb, o.first_bb_id);
      me = o.player_id; first_bb = o.first_bb_id; started = true;
    }
    desynced = false;
    auto find_showdown = [&](int h) -> const std::string* {
      for (auto& s : o.showdowns) if (atoi(s.c_str()) == h) return &s;
      return nullptr;
    };
    // run the referee up to the next event, settling hands as their showdown lines allow
    auto advance = [&]() -> Ev {
      for (;;) {
        Ev e = step();
        if (e == EV_SETTLE) {
          const std::string* sd = find_showdown(hand_nb);
          if (!sd) { fail("no showdown line for hand " + std::to_string(hand_nb)); return EV_CANCEL; }
          if (!settle(*sd)) return EV_CANCEL;
          if (game_over) return EV_CANCEL;
          continue;
        }
        if (e != EV_CONTINUE) return e;
      }
    };
    for (auto& line : o.actions) {
      std::istringstream is(line);
      int r, h, pid; std::string a, b;
      is >> r >> h >> pid >> a >> b;
      Ev e = advance();
      if (e == EV_CANCEL) { ok = false; break; }
      if (a == "NONE") {
        if (e != EV_NONE || turn != r || hand_nb != h) { ok = fail("NONE line out of sync: " + line); break; }
        if (!fill_board(b)) { ok = false; break; }
        continue;
      }
      if (e != EV_DECISION || turn + 1 != r || hand_nb != h || next_player != pid) {
        ok = fail("action line out of sync (turn " + std::to_string(turn) + " hand " + std::to_string(hand_nb) +
                  " next " + std::to_string(next_player) + "): " + line);
        break;
      }
      turn++; last_hand_round = hand_nb; decision_pending = false;
      if (!fill_board(b)) { ok = false; break; }
      if (!apply_shown(a)) { ok = false; break; }
    }
    if (ok) {
      Ev e = advance();
      if (e != EV_DECISION || next_player != me || turn + 1 != o.round || hand_nb != o.hand_nb)
        ok = fail("snapshot out of sync: tracked turn " + std::to_string(turn) + " hand " + std::to_string(hand_nb) +
                  " next " + std::to_string(next_player) + " vs stdin round " + std::to_string(o.round) + " hand " + std::to_string(o.hand_nb));
    }
    if (ok) {
      // our hole cards
      if (o.cards.size() == 5) {
        int c0 = card_from(o.cards.c_str()), c1 = card_from(o.cards.c_str() + 3);
        if (c0 < 0 || c1 < 0) ok = fail("bad hole cards " + o.cards);
        else { players[me].hand[0] = c0; players[me].hand[1] = c1; players[me].n_hand = 2; }
      }
      ok = ok && fill_board(o.board);
      int shown = 0; for (int c : parse_cards(o.board)) shown += c >= 0;
      if (ok && shown != (int)board.size()) ok = fail("board size: " + o.board + " vs " + board_str());
      for (int p = 0; ok && p < n; p++)
        if (players[p].stack != o.stacks[p] || players[p].total != o.chip_in_pot[p])
          ok = fail("stacks differ at seat " + std::to_string(p) + ": tracked " + std::to_string(players[p].stack) + "/" +
                    std::to_string(players[p].total) + " stdin " + std::to_string(o.stacks[p]) + "/" + std::to_string(o.chip_in_pot[p]));
      if (ok && possible_actions() != o.possible) {
        std::string a, b; for (auto& x : possible_actions()) a += x + ","; for (auto& x : o.possible) b += x + ",";
        ok = fail("possible actions differ: tracked " + a + " stdin " + b);
      }
    }
    if (!ok) resync(o);
    return ok;
  }

  // Overwrite what the snapshot gives when the replay went wrong (stacks, chips, board); the
  // betting state (min raise, raise cap) stays approximate until the next hand.
  void resync(const Obs& o) {
    desynced = true;
    if (!started) return;
    for (int p = 0; p < n && p < (int)o.stacks.size(); p++) players[p].stack = o.stacks[p], players[p].total = o.chip_in_pot[p];
    turn = o.round - 1; hand_nb = o.hand_nb; next_player = me; decision_pending = true; calc_winnings = false; over = false;
    board.clear();
    for (int c : parse_cards(o.board)) if (c >= 0) board.push_back(c);
    pot = 0; for (auto& p : players) pot += p.total;
  }
};

}  // namespace pk
