#pragma once
// bot_prev.hpp - FROZEN copy of bot/bot.hpp made by arena/freeze.py; do not edit.
// Namespaces renamed (bot -> prev, pf -> pf_prev) so the arena can hold both versions.
// pf_tables.hpp - HU jam/fold Nash (chip EV) from solvers/pf.py via solvers/export_pf.py.
// Class index r1*13+r2 (ranks 0..12 = 2..A; pair r1==r2, suited r1>r2, offsuit r1<r2).
// Bit k of PF_JAM[c] / PF_CALL[c]: at effective stack PF_STACKS[k] BB the SB jams / the BB calls a jam.
// S= 2 BB: SB jams 90.3% of hands, BB calls 100.0%, SB value +0.010 BB/hand
// S= 3 BB: SB jams 77.7% of hands, BB calls 92.8%, SB value +0.052 BB/hand
// S= 4 BB: SB jams 73.8% of hands, BB calls 73.2%, SB value +0.066 BB/hand
// S= 5 BB: SB jams 71.3% of hands, BB calls 62.0%, SB value +0.056 BB/hand
// S= 6 BB: SB jams 68.6% of hands, BB calls 54.4%, SB value +0.038 BB/hand
// S= 7 BB: SB jams 66.5% of hands, BB calls 48.4%, SB value +0.017 BB/hand
// S= 8 BB: SB jams 62.0% of hands, BB calls 45.4%, SB value -0.004 BB/hand
// S= 9 BB: SB jams 59.9% of hands, BB calls 40.6%, SB value -0.025 BB/hand
// S=10 BB: SB jams 58.4% of hands, BB calls 37.6%, SB value -0.045 BB/hand
// S=12 BB: SB jams 53.5% of hands, BB calls 33.0%, SB value -0.082 BB/hand
// S=15 BB: SB jams 45.7% of hands, BB calls 28.4%, SB value -0.127 BB/hand
// S=20 BB: SB jams 40.3% of hands, BB calls 21.7%, SB value -0.183 BB/hand
// S=25 BB: SB jams 36.0% of hands, BB calls 17.3%, SB value -0.229 BB/hand
namespace pf_prev {
const int PF_NS = 13;
const int PF_STACKS[13] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 15, 20, 25};
const unsigned short PF_JAM[169] = {8191, 0, 0, 0, 0, 0, 0, 1, 1, 7, 63, 511, 8191, 0, 8191, 0, 0, 0, 0, 1, 1, 3, 15, 63, 1023, 8191, 0, 505, 8191, 1, 1, 1, 1, 1, 3, 15, 63, 1023, 8191, 1, 1017, 4095, 8191, 1, 1, 3, 3, 7, 31, 127, 1023, 8191, 1, 49, 2047, 8191, 8191, 511, 63, 7, 15, 31, 255, 2047, 8191, 1, 1, 1023, 4095, 8191, 8191, 1023, 511, 255, 127, 511, 2047, 8191, 1, 1, 319, 2047, 8191, 8191, 8191, 4095, 2047, 1023, 1023, 2047, 8191, 3, 7, 15, 1023, 8191, 8191, 8191, 8191, 8191, 8191, 4095, 4095, 8191, 31, 63, 511, 1023, 4095, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 127, 511, 1023, 1023, 2047, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 1023, 1023, 2047, 4095, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 2047, 2047, 4095, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191};
const unsigned short PF_CALL[169] = {1023, 1, 1, 1, 1, 1, 1, 3, 3, 7, 15, 127, 2047, 3, 4095, 3, 3, 3, 1, 1, 3, 3, 7, 15, 127, 2047, 3, 3, 8191, 3, 3, 3, 3, 3, 3, 7, 31, 255, 2047, 3, 7, 7, 8191, 3, 3, 3, 3, 3, 15, 31, 511, 4095, 3, 3, 7, 7, 8191, 7, 7, 7, 7, 15, 63, 511, 4095, 3, 3, 7, 7, 15, 8191, 7, 15, 15, 31, 127, 1023, 8191, 3, 3, 7, 7, 15, 31, 8191, 31, 31, 63, 255, 1023, 8191, 3, 7, 7, 7, 15, 31, 127, 8191, 127, 255, 511, 2047, 8191, 7, 7, 15, 15, 31, 63, 255, 511, 8191, 1023, 2047, 4095, 8191, 15, 15, 31, 31, 63, 127, 511, 1023, 2047, 8191, 2047, 8191, 8191, 63, 63, 127, 127, 511, 511, 1023, 2047, 4095, 8191, 8191, 8191, 8191, 511, 511, 1023, 1023, 1023, 2047, 2047, 4095, 8191, 8191, 8191, 8191, 8191, 4095, 4095, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191};
}  // namespace pf_prev
// bot.hpp - the CodinGame Poker bot's decision logic (M0 baseline).  Used by main.cpp (stdin/stdout)
// and by the arena (in-process).  No macros: the minifier renames them inconsistently.
//
// M0 policy: a state tracker (engine/tracker.hpp) replays the hand exactly; Monte Carlo equity
// against uniform random hands (pe7c evaluator) versus pot odds with this game's dead-money pots;
// HU jam/fold Nash thresholds (pf_tables.hpp) at short effective stacks.  Later milestones replace
// the policy, not the plumbing.
#include <chrono>
#include <cstdint>
#include <string>

#include "../cpp/pe7c.hpp"
#include "../engine/tracker.hpp"

namespace prev {

typedef std::chrono::steady_clock Clock;

struct Rng {                                           // splitmix64
  uint64_t x;
  __attribute__((always_inline)) inline uint64_t next() {
    uint64_t z = (x += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }
  __attribute__((always_inline)) inline int below(int n) { return (int)((next() >> 32) * (uint64_t)n >> 32); }
};

struct Budget {
  double ms = 20;            // wall-clock budget for the Monte Carlo (from t0); <= 0: trials only
  int max_trials = 100000;   // hard cap on trials
  int min_trials = 2000;     // always run at least this many
};

// Preflop class index as in solvers/eq.c and pf_tables.hpp: pair r*13+r, suited hi*13+lo, offsuit lo*13+hi.
inline int hand_class(int c0, int c1) {
  int r0 = c0 >> 2, r1 = c1 >> 2, hi = r0 > r1 ? r0 : r1, lo = r0 > r1 ? r1 : r0;
  if (r0 == r1) return r0 * 13 + r0;
  return (c0 & 3) == (c1 & 3) ? hi * 13 + lo : lo * 13 + hi;
}

class Bot {
 public:
  pk::Tracker tr;
  Rng rng{0x1234567ull};
  // diagnostics of the last decision
  int last_trials = 0;
  double last_equity = 0;
  std::string last_tag;
  long decisions = 0, trials_total = 0;

  // Our share of the pot at showdown against n_opp uniformly random hands, given the known board.
  double equity(const int* hole, const int* board, int n_board, int n_opp, const Budget& b, Clock::time_point t0) {
    int used[7], nu = 0;
    used[nu++] = hole[0]; used[nu++] = hole[1];
    for (int i = 0; i < n_board; i++) used[nu++] = board[i];
    int deck[52], nd = 0;
    for (int c = 0; c < 52; c++) {
      bool u = false;
      for (int i = 0; i < nu; i++) u |= used[i] == c;
      if (!u) deck[nd++] = c;
    }
    pe::H base = pe::E;
    for (int i = 0; i < n_board; i++) base = pe::add(base, board[i]);
    pe::H mine = pe::add(pe::add(base, hole[0]), hole[1]);
    int need = 5 - n_board + 2 * n_opp;
    double share = 0;
    int trials = 0;
    for (;;) {
      for (int t = 0; t < 512; t++, trials++) {
        // partial Fisher-Yates for `need` cards
        for (int i = 0; i < need; i++) { int j = i + rng.below(nd - i); int tmp = deck[i]; deck[i] = deck[j]; deck[j] = tmp; }
        pe::H bd = base, me = mine;
        for (int i = 0; i < 5 - n_board; i++) bd = pe::add(bd, deck[i]), me = pe::add(me, deck[i]);
        uint16_t mv = pe::ev(me);
        int better = 0, tie = 0;
        for (int o = 0; o < n_opp; o++) {
          int k = 5 - n_board + 2 * o;
          uint16_t ov = pe::ev(pe::add(pe::add(bd, deck[k]), deck[k + 1]));
          if (ov > mv) { better = 1; break; }
          tie += ov == mv;
        }
        if (!better) share += 1.0 / (1 + tie);
      }
      if (trials >= b.max_trials) break;
      if (trials >= b.min_trials && b.ms > 0 &&
          std::chrono::duration<double, std::milli>(Clock::now() - t0).count() > b.ms) break;
      if (b.ms <= 0 && trials >= b.min_trials) break;
    }
    last_trials = trials; trials_total += trials;
    return share / trials;
  }

  // The decision for this turn.  t0 is when the turn's input arrived.
  std::string act(const pk::Obs& o, const Budget& b, Clock::time_point t0) {
    decisions++;
    tr.apply(o);                                     // on failure the tracker resyncs from the snapshot
    const pk::Player& me = tr.players[tr.me];
    rng.x ^= (uint64_t)o.round * 0x9E3779B97F4A7C15ull ^ (uint64_t)me.hand[0] << 8 ^ (uint64_t)me.hand[1] << 16;
    // what the referee offers
    bool can_check = false, can_raise = false, can_allin = false, can_call = false;
    int min_bet = 0;
    for (auto& a : o.possible) {
      if (a == "CHECK") can_check = true; else if (a == "CALL") can_call = true; else if (a == "ALL-IN") can_allin = true;
      else if (a.compare(0, 4, "BET_") == 0) can_raise = true, min_bet = atoi(a.c_str() + 4);
    }
    int stack = me.stack;
    int pot = 0, live = 0, alive = 0, opp_max_start = 0;
    for (auto& p : tr.players) {
      pot += p.total;
      live += !p.folded;
      alive += p.stack + p.total > 0;
      if (p.id != tr.me && !p.folded) opp_max_start = std::max(opp_max_start, p.stack + p.total);
    }
    int n_opp = live - 1;
    int call = tr.call_amount(me);
    int bb = tr.bb;
    int my_start = stack + me.total;
    int eff = std::min(my_start, opp_max_start);         // effective stack this hand, chips
    double eff_bb = (double)eff / bb;
    bool preflop = tr.board.empty();
    const int* hole = me.hand;

    // ---- HU jam/fold Nash at short effective stacks (chip EV; heads-up, preflop)
    if (preflop && alive == 2 && live == 2 && eff_bb <= 8.0 && hole[0] >= 0) {
      int k = 0;
      for (int i = 1; i < pf_prev::PF_NS; i++)
        if (std::abs(pf_prev::PF_STACKS[i] - eff_bb) < std::abs(pf_prev::PF_STACKS[k] - eff_bb)) k = i;
      int cls = hand_class(hole[0], hole[1]);
      bool first_in = tr.last_raiser == -1;              // nobody has raised: SB to act first
      const pk::Player* opp = nullptr;
      for (auto& p : tr.players) if (p.id != tr.me && !p.folded) opp = &p;
      if (first_in && tr.me == tr.sb_id && (can_raise || can_allin)) {
        last_tag = "pf-sb"; last_equity = -1; last_trials = 0;
        return (pf_prev::PF_JAM[cls] >> k & 1) ? "ALL-IN" : "FOLD";
      }
      if (tr.me == tr.bb_id && opp && opp->allin && call > 0) {
        last_tag = "pf-bb"; last_equity = -1; last_trials = 0;
        return (pf_prev::PF_CALL[cls] >> k & 1) ? (call >= stack ? "ALL-IN" : "CALL") : "FOLD";
      }
    }

    // ---- equity versus pot odds
    double e = hole[0] >= 0 ? equity(hole, tr.board.data(), (int)tr.board.size(), std::max(1, n_opp), b, t0) : 0.5;
    last_equity = e;
    double odds = call > 0 ? (double)call / (pot + call) : 0;
    auto bet = [&](int amount) -> std::string {           // a raise adding `amount` chips, made legal
      if (!can_raise || amount >= stack) return can_allin ? "ALL-IN" : (can_call ? "CALL" : "CHECK");
      return "BET " + std::to_string(std::max(amount, min_bet));
    };
    auto call_or_check = [&]() -> std::string { return can_check ? "CHECK" : "CALL"; };
    if (call > 0) {
      bool allin_call = call >= stack;
      if (allin_call) {                                   // calling off: ICM-style caution multiway
        double margin = alive > 2 ? 0.08 : 0.0;
        last_tag = "callff";
        return e > odds + margin ? "ALL-IN" : "FOLD";    // a CALL for the whole stack is replaced by ALL-IN
      }
      if (preflop && tr.last_raiser == -1) {              // unopened pot, we are not the BB: open or complete
        double thr = 0.50 + 0.03 * (n_opp - 1);
        if (e > thr && can_raise) { last_tag = "open"; return bet((int)(2.5 * bb) - me.rnd); }
        last_tag = "complete";
        return e > odds + 0.03 ? "CALL" : "FOLD";
      }
      if (e > 0.85 && can_raise) { last_tag = "raise"; return bet(pot + call); }
      last_tag = "call";
      return e > odds + 0.03 ? "CALL" : "FOLD";
    }
    // check is free
    double thr = 0.62 + 0.04 * (n_opp - 1);
    if (preflop && tr.me == tr.bb_id && tr.last_raiser == -1) thr = 0.55 + 0.03 * (n_opp - 1);   // BB option
    if (e > thr && can_raise) { last_tag = "bet"; return bet(std::max(bb, (int)(0.6 * pot))); }
    last_tag = "check";
    return call_or_check();
  }
};

}  // namespace prev
