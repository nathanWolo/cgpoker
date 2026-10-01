#pragma once
// bot.hpp - the CodinGame Poker bot's decision logic (M0 baseline).  Used by main.cpp (stdin/stdout)
// and by the arena (in-process).  No macros: the minifier renames them inconsistently.
//
// M0 policy: a state tracker (engine/tracker.hpp) replays the hand exactly; Monte Carlo equity
// against uniform random hands (pe7c evaluator) versus pot odds with this game's dead-money pots;
// HU jam/fold Nash thresholds (pf_tables.hpp) at short effective stacks.  Later milestones replace
// the policy, not the plumbing.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>

#include "../cpp/eval7_slow.hpp"
#include "../cpp/pe7c.hpp"
#include "../engine/tracker.hpp"
#include "pf_rank.hpp"
#include "pf_tables.hpp"

namespace bot {

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
  bool use_fast = true;         // pe7c tables ready (main.cpp builds them in a background thread); else eval7_slow
  double jamfold_max_bb = 12;   // heads-up preflop: jam/fold Nash up to this effective stack (BB); the arena tunes it

  // table-free value of the best 5 of k (5..7) cards
  static uint32_t slow_partial(const int* c, int k) {
    if (k == 7) return e7::ev7(c);
    uint32_t best = 0; int five[5];
    if (k == 5) return pk::eval5(c);
    for (int skip = 0; skip < 6; skip++) {                 // k == 6
      int q = 0; for (int i = 0; i < 6; i++) if (i != skip) five[q++] = c[i];
      best = std::max(best, pk::eval5(five));
    }
    return best;
  }
  __attribute__((always_inline)) inline uint32_t ev7(const int* c) const {
    if (!use_fast) return e7::ev7(c);
    pe::H h = pe::E;
    for (int i = 0; i < 7; i++) h = pe::add(h, c[i]);
    return pe::ev(h);
  }

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
    int need = 5 - n_board + 2 * n_opp;
    int c7[7];
    for (int i = 0; i < n_board; i++) c7[2 + i] = board[i];
    double share = 0;
    int trials = 0;
    for (;;) {
      for (int t = 0; t < 512; t++, trials++) {
        // partial Fisher-Yates for `need` cards
        for (int i = 0; i < need; i++) { int j = i + rng.below(nd - i); int tmp = deck[i]; deck[i] = deck[j]; deck[j] = tmp; }
        for (int i = n_board; i < 5; i++) c7[2 + i] = deck[i - n_board];
        c7[0] = hole[0]; c7[1] = hole[1];
        uint32_t mv = ev7(c7);
        int better = 0, tie = 0;
        for (int o = 0; o < n_opp; o++) {
          int k = 5 - n_board + 2 * o;
          c7[0] = deck[k]; c7[1] = deck[k + 1];
          uint32_t ov = ev7(c7);
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

  // ---- tournament equity: Malmuth-Harville ICM over the players who still have chips
  static double icm_rec(const double* st, int k, double total, int depth, const double* pay, int me_idx, bool* used) {
    // probability-weighted payout for player me_idx; depth = place being assigned
    double v = 0;
    for (int i = 0; i < k; i++) {
      if (used[i] || st[i] <= 0) continue;
      double p = st[i] / total;
      if (i == me_idx) v += p * pay[depth];
      else if (depth + 1 < k) {
        used[i] = true;
        v += p * icm_rec(st, k, total - st[i], depth + 1, pay, me_idx, used);
        used[i] = false;
      }
    }
    return v;
  }
  // stacks[0..n) (0 = busted); returns the tournament equity of seat `seat` in payout units
  static double icm(const double* stacks, int n, int seat) {
    static const double pay2[] = {1, 0}, pay3[] = {1, .5, 0}, pay4[] = {1, .6444, .3556, 0};
    const double* pay = n == 2 ? pay2 : n == 3 ? pay3 : pay4;
    double total = 0; int alive = 0;
    for (int i = 0; i < n; i++) total += stacks[i] > 0 ? stacks[i] : 0, alive += stacks[i] > 0;
    if (stacks[seat] <= 0 || alive == 0) return 0;
    if (alive == 1) return pay[0];
    bool used[4] = {false, false, false, false};
    return icm_rec(stacks, n, total, 0, pay, seat, used);
  }

  // Opponent combos ranked by made-hand strength on the board (preflop: by PF_PCT100).  Fills
  // `out` with the strongest `frac` share of the combos that do not conflict with our cards/board.
  int top_range(const int* hole, const int* board, int n_board, double frac, int (*out)[2]) {
    bool used[52] = {false};
    used[hole[0]] = used[hole[1]] = true;
    for (int i = 0; i < n_board; i++) used[board[i]] = true;
    static thread_local int combo[1326][2]; static thread_local int key[1326]; static thread_local int idx[1326];
    int m = 0;
    int c7[7]; for (int i = 0; i < n_board; i++) c7[2 + i] = board[i];
    for (int a = 0; a < 52; a++) if (!used[a])
      for (int c = a + 1; c < 52; c++) if (!used[c]) {
        combo[m][0] = a; combo[m][1] = c;
        if (n_board >= 3) {                                // made-hand rank on the known board (5-7 cards)
          c7[0] = a; c7[1] = c;
          if (n_board == 5) key[m] = (int)ev7(c7);
          else {                                           // flop/turn: rank by the best 5-of-(2+n_board) cards
            pe::H h = pe::E; for (int i = 0; i < 2 + n_board; i++) h = pe::add(h, c7[i]);
            key[m] = use_fast ? (int)pe::ev(h) : (int)slow_partial(c7, 2 + n_board);
          }
        } else key[m] = 100 - pf::PF_PCT100[hand_class(a, c)];
        idx[m] = m; m++;
      }
    std::sort(idx, idx + m, [&](int x, int y) { return key[x] > key[y]; });
    int k = std::max(1, std::min(m, (int)(frac * m + 0.5)));
    for (int i = 0; i < k; i++) out[i][0] = combo[idx[i]][0], out[i][1] = combo[idx[i]][1];
    return k;
  }

  // Our pot share against n_opp opponents whose hands are drawn uniformly from `range` (k combos).
  double equity_vs_range(const int* hole, const int* board, int n_board, int n_opp, int (*range)[2], int k, const Budget& b, Clock::time_point t0) {
    int deck[52], nd = 0;
    bool used[52] = {false};
    used[hole[0]] = used[hole[1]] = true;
    for (int i = 0; i < n_board; i++) used[board[i]] = true;
    int c7[7];
    for (int i = 0; i < n_board; i++) c7[2 + i] = board[i];
    double share = 0; int trials = 0;
    int oh[3][2];
    for (;;) {
      for (int t = 0; t < 256; t++) {
        // draw opponent hands from the range without card conflicts
        bool ok = true;
        for (int o = 0; o < n_opp && ok; o++) {
          int tries = 0;
          for (;;) {
            int r = rng.below(k); oh[o][0] = range[r][0]; oh[o][1] = range[r][1];
            bool clash = false;
            for (int q = 0; q < o; q++) clash |= oh[q][0] == oh[o][0] || oh[q][0] == oh[o][1] || oh[q][1] == oh[o][0] || oh[q][1] == oh[o][1];
            if (!clash) break;
            if (++tries > 20) { ok = false; break; }
          }
        }
        if (!ok) continue;
        nd = 0;
        for (int c = 0; c < 52; c++) {
          bool u = used[c];
          for (int o = 0; o < n_opp; o++) u |= oh[o][0] == c || oh[o][1] == c;
          if (!u) deck[nd++] = c;
        }
        int need = 5 - n_board;
        for (int i = 0; i < need; i++) { int j = i + rng.below(nd - i); int tmp = deck[i]; deck[i] = deck[j]; deck[j] = tmp; }
        for (int i = n_board; i < 5; i++) c7[2 + i] = deck[i - n_board];
        c7[0] = hole[0]; c7[1] = hole[1];
        uint32_t mv = ev7(c7);
        int better = 0, tie = 0;
        for (int o = 0; o < n_opp; o++) {
          c7[0] = oh[o][0]; c7[1] = oh[o][1];
          uint32_t ov = ev7(c7);
          if (ov > mv) { better = 1; break; }
          tie += ov == mv;
        }
        if (!better) share += 1.0 / (1 + tie);
        trials++;
      }
      if (trials >= b.max_trials / 2) break;
      if (trials >= b.min_trials && b.ms > 0 && std::chrono::duration<double, std::milli>(Clock::now() - t0).count() > b.ms) break;
      if (b.ms <= 0 && trials >= b.min_trials) break;
    }
    last_trials = trials; trials_total += trials;
    return trials ? share / trials : 0.5;
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
    int n = tr.n, stack = me.stack;
    int pot = 0, live = 0, alive = 0, opp_max_start = 0, max_other_total = 0, aggressor_total = 0;
    for (auto& p : tr.players) {
      pot += p.total;
      live += !p.folded;
      alive += p.stack + p.total > 0;
      if (p.id != tr.me && !p.folded) opp_max_start = std::max(opp_max_start, p.stack + p.total), max_other_total = std::max(max_other_total, p.total);
    }
    if (tr.last_raiser >= 0 && tr.last_raiser != tr.me) aggressor_total = tr.players[tr.last_raiser].total;
    int n_opp = std::max(1, live - 1);
    int call = tr.call_amount(me);
    int bb = tr.bb;
    int my_start = stack + me.total;
    int eff = std::min(my_start, opp_max_start);         // effective stack this hand, chips
    double eff_bb = (double)eff / bb;
    bool preflop = tr.board.empty();
    int n_board = (int)tr.board.size();
    const int* hole = me.hand;
    const int* board = tr.board.data();
    if (hole[0] < 0) { last_tag = "nocards"; return can_check ? "CHECK" : "FOLD"; }
    int cls = hand_class(hole[0], hole[1]);
    auto bet = [&](int amount) -> std::string {           // a raise adding `amount` chips, made legal
      if (!can_raise || amount >= stack) return can_allin ? "ALL-IN" : (can_call ? "CALL" : "CHECK");
      return "BET " + std::to_string(std::max(amount, min_bet));
    };
    auto call_s = [&]() -> std::string { return call >= stack ? "ALL-IN" : can_check ? "CHECK" : "CALL"; };
    int call_amt = std::min(call, stack);
    int my_commit = me.total + call_amt;
    double winnable = my_commit;                          // the pot we can actually win: each opponent's chips up to our commitment
    for (auto& p : tr.players) if (p.id != tr.me) winnable += std::min(p.total, my_commit);
    double odds = call_amt > 0 ? call_amt / winnable : 0;

    // ---- heads-up preflop at <= 25 BB: jam/fold Nash (chip EV).  SB first in: jam or fold.  BB facing a jam: call or fold.
    if (preflop && alive == 2 && live == 2 && eff_bb <= jamfold_max_bb) {
      int k = 0;
      for (int i = 1; i < pf::PF_NS; i++)
        if (std::abs(pf::PF_STACKS[i] - eff_bb) < std::abs(pf::PF_STACKS[k] - eff_bb)) k = i;
      const pk::Player* opp = nullptr;
      for (auto& p : tr.players) if (p.id != tr.me && !p.folded) opp = &p;
      if (tr.last_raiser == -1 && tr.me == tr.sb_id && (can_raise || can_allin)) {
        last_tag = "pf-sb"; last_equity = -1; last_trials = 0;
        return (pf::PF_JAM[cls] >> k & 1) ? "ALL-IN" : "FOLD";
      }
      if (tr.me == tr.bb_id && opp && opp->allin && call > 0) {
        last_tag = "pf-bb"; last_equity = -1; last_trials = 0;
        return (pf::PF_CALL[cls] >> k & 1) ? call_s() : "FOLD";
      }
    }

    // ---- the opponent's range: whoever bet last is assumed to hold a strong hand
    double frac = 1.0;                                    // 1 = uniform random
    if (call > 0 && tr.last_raiser >= 0 && tr.last_raiser != tr.me) {
      double bet_bb = (double)std::max(aggressor_total, max_other_total) / bb;
      if (preflop) {
        bool shove = tr.players[tr.last_raiser].allin || bet_bb >= 12;
        if (shove && bet_bb <= pf::PF_STACKS[pf::PF_NS - 1]) {   // a shove at a depth where we shove too: the Nash jam width there
          int k = pf::PF_NS - 1;
          for (int i = 0; i < pf::PF_NS; i++) if (pf::PF_STACKS[i] >= bet_bb) { k = i; break; }
          int cnt = 0; for (int c = 0; c < 169; c++) cnt += (pf::PF_JAM[c] >> k & 1) * ((c / 13 == c % 13) ? 6 : (c / 13 > c % 13) ? 4 : 12);
          frac = std::max(0.15, cnt / 1326.0);
        } else if (shove) frac = 1.0;                     // deeper shoves are off our tree: the uniform (epsilon-floor) belief
        else frac = tr.raise_nb >= 3 ? 0.2 : 0.35;        // open / 3-bet
      } else {
        double rel = (double)call / std::max(1, pot - call);
        frac = std::min(0.65, std::max(0.3, 0.65 - 0.35 * std::min(1.0, rel)));
      }
    }
    static thread_local int range[1326][2];
    double e;
    if (frac < 1.0) {
      int k = top_range(hole, board, n_board, frac, range);
      e = equity_vs_range(hole, board, n_board, n_opp, range, k, b, t0);
    } else e = equity(hole, board, n_board, n_opp, b, t0);
    last_equity = e;

    // ---- facing a bet
    if (call > 0) {
      bool big = call_amt >= 0.4 * stack;                 // calling off (most of) the stack
      if (big) {
        if (alive >= 3) {                                 // tournament equity decides
          double st[4], w[4], l[4];
          for (int i = 0; i < n; i++) st[i] = tr.players[i].stack + tr.players[i].total;
          for (int i = 0; i < n; i++) w[i] = l[i] = st[i];
          // win: we take from each player min(their total, our commit); the rest stays with them
          w[tr.me] = stack - call_amt + my_commit;
          int big_opp = -1;
          for (int i = 0; i < n; i++) if (i != tr.me) {
            double take = std::min(tr.players[i].total, my_commit);
            w[tr.me] += take; w[i] = st[i] - take;
            if (!tr.players[i].folded && (big_opp < 0 || st[i] > st[big_opp])) big_opp = i;
          }
          // lose: our commit goes to the biggest live opponent
          l[tr.me] = stack - call_amt;
          if (big_opp >= 0) l[big_opp] = st[big_opp] + my_commit;
          double f[4]; for (int i = 0; i < n; i++) f[i] = st[i]; f[tr.me] = stack;   // fold: our chips in the pot are gone
          if (big_opp >= 0) f[big_opp] += me.total;
          double ev_call = e * icm(w, n, tr.me) + (1 - e) * icm(l, n, tr.me), ev_fold = icm(f, n, tr.me);
          last_tag = "icm";
          return ev_call > ev_fold + 0.002 ? call_s() : "FOLD";
        }
        double m = 0.04 * std::min(1.0, std::max(0.0, (eff_bb - 15) / 35));   // deep heads-up: the option to wait
        last_tag = "callff";
        return e > odds + m ? call_s() : "FOLD";
      }
      if (preflop && tr.last_raiser == -1) {              // unopened pot, we are not the BB: open or complete
        double thr = 0.50 + 0.03 * (n_opp - 1);
        if (e > thr && can_raise) { last_tag = "open"; return bet((int)(2.5 * bb) - me.rnd); }
        last_tag = "complete";
        return e > odds + 0.03 ? "CALL" : "FOLD";
      }
      if (e > 0.75 && can_raise) { last_tag = "raise"; return bet(pot + call); }
      last_tag = "call";
      return e > odds + 0.02 ? call_s() : "FOLD";
    }
    // ---- check is free: value-bet thinner (a checking opponent is weak)
    double thr = 0.55 + 0.04 * (n_opp - 1);
    if (preflop && tr.me == tr.bb_id && tr.last_raiser == -1) thr = 0.55 + 0.03 * (n_opp - 1);   // BB option
    if (e > thr && can_raise) { last_tag = "bet"; return bet(std::max(bb, (int)(0.6 * pot))); }
    last_tag = "check";
    return "CHECK";
  }
};

}  // namespace bot
