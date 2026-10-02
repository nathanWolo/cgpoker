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
#include "../cpp/icm.hpp"
#include "pfn_tables.hpp"
#include "hu_play.hpp"
#include "opp_model.hpp"
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
  double uni() { return (next() >> 11) * (1.0 / 9007199254740992.0); }
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

// ---- ICM push/fold charts for 3-4 players (bot/pfn_tables.hpp, solved by solvers/pfn): decoded once per process
struct PfnGame {
  int N = 0, nt = 0, L = 0, nn = 0, P = 0;
  const double* lv = nullptr; const int* nodes = nullptr;
  std::vector<unsigned char> rank, pos, thr;   // rank[node][169]; pos[node][class] = rank position; thr[node][P]
};
// 14 payload bits per CJK character (tools/cjk14.py); other characters are skipped
int cjk14_decode(const char* s, unsigned char* out, int out_max) {
  int n = 0, bits = 0; unsigned acc = 0;
  for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
    if ((*p & 0xF0) != 0xE0) continue;
    unsigned code = ((p[0] & 15u) << 12) | ((p[1] & 63u) << 6) | (p[2] & 63u);
    p += 2;
    acc = (acc << 14) | (code - 0x4E00u); bits += 14;
    while (bits >= 8) { if (n >= out_max) return -1; bits -= 8; out[n++] = (unsigned char)(acc >> bits); }
    acc &= (1u << bits) - 1;
  }
  return n;
}
inline const std::vector<PfnGame>& pfn_games() {
  static const std::vector<PfnGame> G = [] {
    std::vector<PfnGame> v;
    for (int gi = 0; gi < pfn_t::NG; gi++) {
      const pfn_t::GameT& t = pfn_t::GAMES[gi];
      PfnGame g; g.N = t.N; g.nt = t.nt; g.L = t.L; g.nn = t.nn; g.lv = t.levels; g.nodes = t.nodes;
      g.P = 1; for (int i = 0; i < g.N; i++) g.P *= g.L;
      int need = g.nn * 169 + g.nn * g.P;
      std::vector<unsigned char> buf(need + 2);
      if (cjk14_decode(t.data, buf.data(), (int)buf.size()) < need) continue;   // corrupt: chart unusable
      g.rank.assign(buf.begin(), buf.begin() + g.nn * 169);
      g.thr.assign(buf.begin() + g.nn * 169, buf.begin() + need);
      g.pos.assign(g.nn * 169, 0);
      for (int k = 0; k < g.nn; k++) for (int r = 0; r < 169; r++) g.pos[k * 169 + g.rank[k * 169 + r]] = (unsigned char)r;
      v.push_back(std::move(g));
    }
    return v;
  }();
  return G;
}
// number of classes to jam at node k for stacks st[0..N) (BB, action order): multilinear in log-stack between levels
inline double pfn_threshold(const PfnGame& g, int k, const double* st) {
  int lo[4]; double fr[4];
  for (int i = 0; i < g.N; i++) {
    double s = std::min(std::max(st[i], g.lv[0]), g.lv[g.L - 1]);
    int j = 0; while (j + 2 < g.L && g.lv[j + 1] <= s) j++;
    lo[i] = j; fr[i] = (std::log(s) - std::log(g.lv[j])) / (std::log(g.lv[j + 1]) - std::log(g.lv[j]));
  }
  double T = 0;
  for (int c = 0; c < (1 << g.N); c++) {
    double w = 1; int idx = 0;
    for (int i = 0; i < g.N; i++) { int bit = c >> i & 1; w *= bit ? fr[i] : 1 - fr[i]; idx = idx * g.L + lo[i] + bit; }
    if (w > 0) T += w * g.thr[k * g.P + idx];
  }
  return T;
}
inline const PfnGame* pfn_find(int N, int nt) {
  for (const PfnGame& g : pfn_games()) if (g.N == N && g.nt == nt) return &g;
  return nullptr;
}

class Bot {
 public:
  pk::Tracker tr;
  Rng rng{0x1234567ull};
  uint64_t seed0 = 0x1234567ull;          // per-process seed (the arena varies it per game); each turn reseeds from it
  // diagnostics of the last decision
  int last_trials = 0;
  double last_equity = 0;
  std::string last_tag;
  long decisions = 0, trials_total = 0;
  bool use_fast = true;         // pe7c tables ready (main.cpp builds them in a background thread); else eval7_slow
  double jamfold_max_bb = 12;   // heads-up preflop: jam/fold Nash up to this effective stack (BB); the arena tunes it
  OppModel om; bool use_om = true;       // real-time opponent model (opp_model.hpp); the arena's BOT_OM=0 disables it
  std::string last_note;                  // the model's view of the opponent at the last decision (diagnostics)
  bool hu_postflop = false;               // heads-up tables postflop too (false: preflop only, the rules play postflop; the arena's passive opponents crush the tables' postflop play)
  double hu_shove_bb = 1e9;               // facing a preflop all-in deeper than this (BB): the rules (uniform belief) decide, not the tables
  double hu_max_bb = 20;                  // heads-up: the solved strategy (hu_play.hpp) up to this effective stack (BB); 150 lost to M1 against the exploitative field, 20 is neutral
  double pfn_max_bb = 20;                 // 3-4 players, preflop: the ICM push/fold chart decides first-in, over limps and against a raise up to this stack (BB); 20 beat 12 in the arena
  // M3, 3-4 players alive, preflop off the chart: thresholds on our equity against the live opponents' ranges
  double p_open = 0.50, p_open_step = 0.03;   // raise first in if equity > p_open + p_open_step per extra opponent
  double p_iso = 0.50;                        // the same over limpers
  double p_iso_bb = 2.5, p_iso_limper = 0;    // raise-to over limpers: p_iso_bb + p_iso_limper per limper (BB)
  double p_3bet = 0.75;                       // re-raise (pot-sized) facing a raise
  bool mw_fold_all = false;                   // postflop bet rule, several opponents: fold equity = all of them fold (true) or the likeliest folder (false)
  // heads-up jam/fold, small blind first in: jam by expected value against how often and with what the opponent calls,
  // once they have faced hu_jam_gate jams; before that the equilibrium chart.  The priors of the measured rule: the
  // share of hands an unmeasured opponent calls with (-1: the equilibrium calling range) and the width of those hands
  // M3: from the first hand, with the field's priors (unmeasured opponents call a heads-up shove 25% of the time, with
  // hands spread over the top 70%; the live field over-folds to shoves), +0.0022 +- 0.0010 payout against the clones
  double hu_call_prior = 0.25, hu_callw_prior = 0.7; int hu_jam_gate = 0;
  // heads-up up to hu_evjam_bb (0: off): the big blind's option over a limp, and facing a raise short of all-in, shove
  // when that beats checking or the better of calling and folding by hu_evjam_margin pots (expected values below)
  double hu_evjam_bb = 12, hu_evjam_margin = 0;    // M3: on to 12 BB, +0.0008 more (20 BB lost on fresh games)

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
  // expected showdown equity against a random hand: runouts on the flop and turn, exact on the river (the
  // heads-up strategy's card abstraction, solvers/hu)
  double ehs(const int* hole, const int* board, int n_board, int runouts = 300) {
    uint64_t used = 1ull << hole[0] | 1ull << hole[1]; for (int i = 0; i < n_board; i++) used |= 1ull << board[i];
    int deck[52], nd = 0; for (int c = 0; c < 52; c++) if (!(used >> c & 1)) deck[nd++] = c;
    int c7[7]; c7[0] = hole[0]; c7[1] = hole[1]; for (int i = 0; i < n_board; i++) c7[2 + i] = board[i];
    double s = 0; int n = 0;
    if (n_board == 5) {
      uint32_t mine = ev7(c7);
      for (int i = 0; i < nd; i++) for (int j = i + 1; j < nd; j++) { int o7[7] = {deck[i], deck[j], board[0], board[1], board[2], board[3], board[4]}; uint32_t ov = ev7(o7); s += mine > ov ? 1 : mine == ov ? 0.5 : 0; n++; }
      return s / n;
    }
    int need = 5 - n_board + 2;
    for (int r = 0; r < runouts; r++) {
      for (int i = 0; i < need; i++) { int j = i + rng.below(nd - i); std::swap(deck[i], deck[j]); }
      for (int i = 0; i < 5 - n_board; i++) c7[2 + n_board + i] = deck[i];
      uint32_t mine = ev7(c7);
      int o7[7]; o7[0] = deck[5 - n_board]; o7[1] = deck[6 - n_board]; for (int i = 0; i < 5; i++) o7[2 + i] = c7[2 + i];
      uint32_t ov = ev7(o7);
      s += mine > ov ? 1 : mine == ov ? 0.5 : 0; n++;
    }
    return s / n;
  }
  // the hand so far as the heads-up abstraction's history (hu_play.hpp); ok = false if it cannot be mapped
  std::string hu_history(bool& ok) const {
    std::string h; int street = 0; ok = true; int tot[4] = {0, 0, 0, 0};
    // the effective stack at hand start (chips): a raise or bet is mapped to the nearer of the tree's two sizes in
    // log terms (the geometric midpoint), the tree's raise (2.5 BB open, 3 BB over a limp, 3x a raise) or half-pot
    // bet against all-in, where all-in means the EFFECTIVE stack: a 50 BB raise by a 400 BB stack is a shove for a
    // 40 BB player.  (M2.1 compared the raise with the raiser's own stack and called such raises off as 2.5x opens.)
    double eff = 1e18; for (auto& p : tr.players) if (p.stack + p.total > 0) eff = std::min(eff, (double)(p.stack + p.total));
    for (const auto& e : tr.hand_log) {
      if (e.hand != tr.hand_nb) continue;
      int st = e.street == 0 ? 0 : e.street - 2;
      while (street < st) { h += '/'; street++; }
      int mine = e.pid == tr.me ? 0 : 1, other = 1 - mine;   // indices in tot[]: 0 = me, 1 = them
      char c = 0;
      switch (e.type) {
        case pk::A_FOLD: c = 'F'; break;
        case pk::A_CHECK: c = 'K'; break;
        case pk::A_CALL: c = 'C'; break;
        case pk::A_ALL_IN: c = e.total_after <= tot[other] ? 'C' : 'A'; break;         // an all-in that only calls is a call
        case pk::A_BET: {
          if (e.allin) { c = e.total_after <= tot[other] ? 'C' : 'A'; break; }
          size_t sl = h.rfind('/'); std::string cur = sl == std::string::npos ? h : h.substr(sl + 1);
          if (st == 0) {
            double tree_r = cur.empty() ? 2.5 * tr.bb : cur == "C" ? 3.0 * tr.bb : 3.0 * tot[other];
            c = e.total_after > std::sqrt(tree_r * eff) || e.total_after >= 0.6 * eff ? 'A' : 'R';
            break;
          }
          bool facing = !cur.empty() && (cur.back() == 'B' || cur.back() == 'A');
          double pot_before = tot[0] + tot[1];                                     // chips in the pot before this bet
          double behind = eff - std::max(tot[0], tot[1]);                          // the effective stack left to bet
          bool big = e.added > std::sqrt(0.5 * pot_before * std::max(behind, 1.0)) || e.added >= 0.6 * behind;
          if (facing && !big) { ok = false; return h; }   // a small raise of a bet: the tree only knows all-in raises, and
                                                           // answering a min-raise with the fold-to-shove range lost live;
                                                           // off-tree, the pot-odds rules price it
          c = facing || big ? 'A' : 'B';
          break;
        }
        default: ok = false; return h;
      }
      tot[mine] = e.total_after;
      h += c;
    }
    int st_now = tr.board.empty() ? 0 : (int)tr.board.size() - 2;
    while (street < st_now) { h += '/'; street++; }
    return h;
  }
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

  // tournament equity (cpp/icm.hpp: Malmuth-Harville with the TrueSkill payouts) of `seat` for the hypothetical
  // end-of-hand stacks v[0..n) over all seats; seats that busted in earlier hands (alive_start false) are left
  // out so that a seat busting now takes the right place
  static double icm_of(const double* v, const bool* alive_start, int n, int seat) {
    double c[4]; int m = 0, me = 0;
    for (int i = 0; i < n; i++) if (alive_start[i]) { if (i == seat) me = m; c[m++] = v[i]; }
    return icm::icm(c, m, me, n);
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
        if (n_board >= 3) {                                // made-hand rank on the known board (5-7 cards); ranking by
          c7[0] = a; c7[1] = c;                            // expected hand strength instead measured +0.000 +- 0.003 (M2.1)
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
    tr.hook = use_om ? &om : nullptr;               // the model observes every settled hand
    tr.apply(o);                                     // on failure the tracker resyncs from the snapshot
    const pk::Player& me = tr.players[tr.me];
    // a clean per-turn seed (not XORed into the running state): the same situation always gets the same random stream,
    // whatever the time-budgeted Monte Carlo of earlier turns consumed, so decisions are reproducible across builds
    rng.x = seed0 ^ (uint64_t)o.round * 0x9E3779B97F4A7C15ull ^ (uint64_t)me.hand[0] << 8 ^ (uint64_t)me.hand[1] << 16;
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

    // ---- heads-up preflop at <= jamfold_max_bb: jam/fold Nash (chip EV).  SB first in: jam or fold.  BB facing a jam: call or fold.
    if (preflop && alive == 2 && live == 2 && eff_bb <= jamfold_max_bb) {
      int k = 0;
      for (int i = 1; i < pf::PF_NS; i++)
        if (std::abs(pf::PF_STACKS[i] - eff_bb) < std::abs(pf::PF_STACKS[k] - eff_bb)) k = i;
      const pk::Player* opp = nullptr;
      for (auto& p : tr.players) if (p.id != tr.me && !p.folded) opp = &p;
      if (tr.last_raiser == -1 && tr.me == tr.sb_id && (can_raise || can_allin)) {
        if (use_om && opp && om.o[opp->id].vsjam.n >= hu_jam_gate) {   // measured: jam if +EV against how often and with what they call
          int kc = 0; for (int i = 0; i < 169; i++) kc += (pf::PF_CALL[i] >> k & 1) * ((i / 13 == i % 13) ? 6 : (i / 13 > i % 13) ? 4 : 12);
          double nash_call = kc / 1326.0, pc = hu_call_prior >= 0 ? hu_call_prior : nash_call, pw = hu_callw_prior >= 0 ? hu_callw_prior : pc;
          double c = 1 - om.fold_to_jam(opp->id, 1 - pc), wc = om.calljam_width(opp->id, pw);
          static thread_local int rc[1326][2]; int kk = top_range(hole, board, 0, wc, rc);
          double ec = equity_vs_range(hole, board, 0, 1, rc, kk, b, t0);
          double ev = (1 - c) * 1.5 + c * (ec * (eff_bb + 0.5) - (1 - ec) * (eff_bb - 0.5));   // BB, relative to folding
          last_tag = "pf-sb-om"; last_equity = ec; last_trials = 0;
          return ev > 0 ? "ALL-IN" : "FOLD";
        }
        last_tag = "pf-sb"; last_equity = -1; last_trials = 0;
        return (pf::PF_JAM[cls] >> k & 1) ? "ALL-IN" : "FOLD";
      }
      if (tr.me == tr.bb_id && opp && opp->allin && call > 0 && !(use_om && om.o[opp->id].jam_pct.n >= 3)) {   // measured: the pot-odds rule against their shown jam range
        last_tag = "pf-bb"; last_equity = -1; last_trials = 0;
        return (pf::PF_CALL[cls] >> k & 1) ? call_s() : "FOLD";
      }
    }

    // ---- heads-up, short, the big blind over a limp or anyone facing a raise short of all-in: shove if its expected
    // value (chips) beats the alternative.  Shove: they fold with the opponent model's fold-to-shove frequency (prior:
    // the field's), else we hold our equity against their calling range.  Check / call: our equity against their
    // limping / raising range times the pot, as if fully realised.
    if (hu_evjam_bb > 0 && use_om && preflop && alive == 2 && live == 2 && eff_bb <= hu_evjam_bb && (can_allin || can_raise)
        && !(tr.last_raiser == -1 && tr.me == tr.sb_id)) {
      const pk::Player* opp = nullptr;
      for (auto& p : tr.players) if (p.id != tr.me && !p.folded) opp = &p;
      if (opp && !opp->allin && opp->stack > 0) {
        double pc = hu_call_prior >= 0 ? hu_call_prior : 0.3, pw = hu_callw_prior >= 0 ? hu_callw_prior : pc;
        double F = om.fold_to_jam(opp->id, 1 - pc), wc = om.calljam_width(opp->id, pw);
        static thread_local int rj[1326][2], ro[1326][2];
        int kj = top_range(hole, board, 0, wc, rj); double ec = equity_vs_range(hole, board, 0, 1, rj, kj, b, t0);
        double wo = call > 0 ? om.open_width(opp->id, 0.6) : om.limp_width(opp->id, 0.8);
        int ko = top_range(hole, board, 0, wo, ro); double eo = equity_vs_range(hole, board, 0, 1, ro, ko, b, t0);
        double E = eff, P = pot;
        double ev_jam = F * P + (1 - F) * (ec * 2 * E - (E - me.total));
        double ev_alt = std::max(0.0, eo * (P + call) - call);
        if (ev_jam > ev_alt + hu_evjam_margin * P) { last_tag = "hu-evjam"; last_equity = ec; last_trials = 0; return can_allin ? "ALL-IN" : bet(stack); }
      }
    }

    // ---- heads-up at 8-40 BB effective: the solved strategy (hu_play.hpp); off-tree histories fall through
    if (hu_max_bb > 0 && alive == 2 && live == 2) {
      int opp_start = 0; for (auto& p : tr.players) if (p.id != tr.me && p.stack + p.total > 0) opp_start = p.stack + p.total;
      double eff_start = (double)std::min(my_start, opp_start) / bb;
      if (eff_start >= hu_t::STACKS[0] - 1e-9 && eff_start <= hu_max_bb) {
        bool ok; std::string h = hu_history(ok);
        int st = n_board == 0 ? 0 : n_board - 2;
        if (st > 0 && !hu_postflop) ok = false;
        if (st == 0 && !h.empty() && h.back() == 'A' && eff_start > hu_shove_bb) ok = false;
        if (ok) {
          int bucket = st == 0 ? cls : hu_bucket(st, ehs(hole, board, n_board));
          const HuNode* nd = nullptr;
          char a = hu_decide(std::min(eff_start, hu_t::STACKS[hu_t::NS - 1]), h, bucket, rng.uni(), &nd);
          if (a) {
            last_tag = std::string("hu-") + a; last_equity = -1; last_trials = 0;
            int opp_rnd = 0; for (auto& p : tr.players) if (p.id != tr.me) opp_rnd = std::max(opp_rnd, p.rnd);
            if (a == 'F') return can_check ? "CHECK" : "FOLD";
            if (a == 'K') return can_check ? "CHECK" : call_s();
            if (a == 'C') return call_s();
            if (a == 'A') return can_allin ? "ALL-IN" : bet(stack);
            if (a == 'R') { int target = h.empty() ? (int)(2.5 * bb) : h == "C" ? 3 * bb : 3 * opp_rnd; return bet(target - me.rnd); }
            return bet(pot / 2);                                                   // 'B': half the pot
          }
        }
      }
    }

    // ---- 3-4 players preflop, jam/fold history so far: the ICM push/fold chart (pfn_tables.hpp).  Facing a jam
    // always; first in when our stack is short.  Any limp or raise before us is off the chart's tree.
    if (preflop && (alive == 3 || alive == 4) && (can_raise || can_allin || call > 0)) {
      int order[4], no = 0;                             // action order: after the BB round to the BB; alive seats only
      for (int s = 1; s <= n; s++) { int id = (tr.bb_id + s) % n; if (tr.players[id].stack + tr.players[id].total > 0) order[no++] = id; }
      int mi = -1; for (int j = 0; j < no; j++) if (order[j] == tr.me) mi = j;
      bool ontree = mi >= 0 && no == alive, raised = false, shortie = (double)my_start / bb <= pfn_max_bb; int prefix = 0, limpers = 0;
      for (int j = 0; j < mi && ontree; j++) {
        const pk::Player& p = tr.players[order[j]];
        if (p.folded) continue;
        if (p.allin || (prefix != 0 && p.rnd > bb)) prefix |= 1 << j;   // a jam, or a call of one by a bigger stack
        else if (prefix == 0 && p.rnd <= bb) limpers++;                 // a limp: the chart has no limp node, count it as a fold
        else if (shortie && !raised && p.rnd > bb) { raised = true; prefix |= 1 << j; }   // a raise: short, we answer it jam or fold, as if it were a jam
        else ontree = false;                                            // a raise while we are deep: off the chart's tree
      }
      for (int j = mi + 1; j < no && ontree; j++) if (tr.players[order[j]].spoken) ontree = false;
      const PfnGame* g = ontree ? pfn_find(alive, n) : nullptr;
      bool facing_jam = prefix != 0;
      // a jam much shorter than our stack with players still to act behind us is not the chart's "call" (which
      // commits our whole stack): the pot-odds rules below price that
      bool small_call = facing_jam && !raised && call_amt < 0.4 * stack && mi + 1 < no;
      if (g && !small_call && (facing_jam || shortie)) {
        int nd = (1 << mi) - 1 + prefix, k = -1;
        for (int q = 0; q < g->nn; q++) if (g->nodes[q] == nd) k = q;
        if (k >= 0) {
          double st[4]; for (int j = 0; j < no; j++) st[j] = (double)(tr.players[order[j]].stack + tr.players[order[j]].total) / bb;
          double T = pfn_threshold(*g, k, st);
          bool go = g->pos[k * 169 + cls] + 0.5 < T;
          last_tag = raised ? "pfn-rejam" : facing_jam ? "pfn-call" : limpers ? "pfn-jam-lim" : "pfn-jam"; last_equity = -1; last_trials = 0;
          if (go) return facing_jam && !raised ? call_s() : can_allin ? "ALL-IN" : bet(stack);
          if (can_check) return "CHECK";                                // the BB's free option is never folded
          if (facing_jam || !limpers) return "FOLD";
          // chart says fold over limpers: completing is cheap, let the rules below price it
        }
      }
    }

    // ---- the opponent's range: whoever bet last is assumed to hold a strong hand.  With the opponent model
    // (opp_model.hpp) the fixed assumption is only the prior: the width comes from the hands this opponent has
    // been revealed to take that action with, and when nobody is betting, from their last action this hand.
    auto nash_width = [&](const unsigned short* tab, double depth_bb) {
      int k = pf::PF_NS - 1;
      for (int i = 0; i < pf::PF_NS; i++) if (pf::PF_STACKS[i] >= depth_bb) { k = i; break; }
      int cnt = 0; for (int c = 0; c < 169; c++) cnt += (tab[c] >> k & 1) * ((c / 13 == c % 13) ? 6 : (c / 13 > c % 13) ? 4 : 12);
      return std::max(0.15, cnt / 1326.0);
    };
    // the width of opponent p's range after their last action this hand (1 = uniform)
    auto om_range = [&](int p) -> double {
      double w = 1.0; bool raised_before = false;
      for (const auto& a : tr.hand_log) {
        if (a.hand != tr.hand_nb) continue;
        if (a.pid != p) { if (a.street == 0 && (a.type == pk::A_BET || a.type == pk::A_ALL_IN)) raised_before = true; continue; }
        bool allin = a.type == pk::A_ALL_IN || (a.type == pk::A_BET && a.allin);
        if (a.street == 0) {
          if (allin) w = om.jam_width(p, nash_width(pf::PF_JAM, std::min(25.0, (double)a.total_after / bb)));
          else if (a.type == pk::A_BET) w = a.raised ? om.threebet_width(p, 0.2) : om.open_width(p, 0.35);
          else if (a.type == pk::A_CALL && a.raised) w = std::min(w, om.call_width(p, 0.6));
          else if (a.type == pk::A_CALL && !raised_before) w = om.limp_width(p, 0.8);
        } else if (a.type == pk::A_BET || a.type == pk::A_ALL_IN) {
          double rel = (double)a.added / std::max(1, a.pot_before);
          double base = std::min(0.65, std::max(0.3, 0.65 - 0.35 * std::min(1.0, rel)));
          w = std::min(w, std::min(1.0, std::max(0.15, base * om.bet_freq(p) / 0.4)));
        }
      }
      return w;
    };
    double frac = 1.0;                                    // 1 = uniform random
    if (call > 0 && tr.last_raiser >= 0 && tr.last_raiser != tr.me) {
      int agg = tr.last_raiser;
      double bet_bb = (double)std::max(aggressor_total, max_other_total) / bb;
      if (preflop) {
        bool shove = tr.players[agg].allin || bet_bb >= 12;
        if (shove && bet_bb <= pf::PF_STACKS[pf::PF_NS - 1]) frac = nash_width(pf::PF_JAM, bet_bb);   // a shove at a depth where we shove too
        else if (shove) frac = 1.0;                       // deeper shoves are off our tree: the uniform (epsilon-floor) belief
        else frac = tr.raise_nb >= 3 ? 0.2 : 0.35;        // open / 3-bet
        if (use_om) frac = shove ? om.jam_width(agg, frac) : tr.raise_nb >= 3 ? om.threebet_width(agg, frac) : om.open_width(agg, frac);
      } else {
        double rel = (double)call / std::max(1, pot - call);
        frac = std::min(0.65, std::max(0.3, 0.65 - 0.35 * std::min(1.0, rel)));
        if (use_om) frac = std::min(1.0, std::max(0.15, frac * om.bet_freq(agg) / 0.4));
      }
    } else if (use_om) {                                  // nobody is betting: what the live opponents showed this hand
      double sw = 0; int nw = 0;
      for (auto& p : tr.players) if (p.id != tr.me && !p.folded && p.stack + p.total > 0) { sw += om_range(p.id); nw++; }
      if (nw) frac = sw / nw;
    }
    int om_opp = -1;                                      // the single live opponent (heads-up decisions)
    for (auto& p : tr.players) if (p.id != tr.me && !p.folded && p.stack + p.total > 0) om_opp = om_opp < 0 ? p.id : -2;
    if (use_om && om_opp >= 0) last_note = om.note(om_opp); else last_note.clear();
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
          bool as[4]; for (int i = 0; i < n; i++) as[i] = st[i] > 0;
          double ev_call = e * icm_of(w, as, n, tr.me) + (1 - e) * icm_of(l, as, n, tr.me), ev_fold = icm_of(f, as, n, tr.me);
          last_tag = "icm";
          return ev_call > ev_fold + 0.002 ? call_s() : "FOLD";
        }
        double m = 0.04 * std::min(1.0, std::max(0.0, (eff_bb - 15) / 35));   // deep heads-up: the option to wait
        last_tag = "callff";
        return e > odds + m ? call_s() : "FOLD";
      }
      if (preflop && tr.last_raiser == -1) {              // unopened pot, we are not the BB: open or complete
        int limpers = 0;
        for (auto& p : tr.players) if (p.id != tr.me && p.id != tr.bb_id && !p.folded && p.rnd >= bb) limpers++;
        bool multi = alive >= 3;
        double thr = (multi ? (limpers ? p_iso : p_open) + p_open_step * (n_opp - 1) : 0.50 + 0.03 * (n_opp - 1));
        if (use_om) {                                     // steal more from opponents who fold to raises, less from those who never do
          double f = 0; int nf = 0;
          for (auto& p : tr.players) if (p.id != tr.me && !p.folded && p.stack + p.total > 0) { f += om.fold_to_raise(p.id); nf++; }
          if (nf) thr = std::max(0.40, thr - 0.25 * std::max(0.0, f / nf - 0.5));   // never above the value threshold: a station is raised for value
        }
        double to = multi && limpers ? (p_iso_bb + p_iso_limper * limpers) * bb : 2.5 * bb;
        if (e > thr && can_raise) { last_tag = limpers ? "iso" : "open"; return bet((int)to - me.rnd); }
        last_tag = "complete";
        return e > odds + 0.03 ? "CALL" : "FOLD";
      }
      if (e > (preflop && alive >= 3 ? p_3bet : 0.75) && can_raise) { last_tag = "raise"; return bet(pot + call); }
      last_tag = "call";
      return e > odds + 0.02 ? call_s() : "FOLD";
    }
    // ---- check is free.  With the model, postflop: bet (0.6 pot) if its expected value against this opponent's
    // fold-to-bet frequency and their continuing range beats checking (a station gets value bets with any edge
    // and no bluffs; a folder gets bluffs).  Without it: value-bet thinner (a checking opponent is weak).
    if (use_om && !preflop && can_raise) {
      double f = 1; int nf = 0;
      for (auto& p : tr.players) if (p.id != tr.me && !p.folded && p.stack + p.total > 0) { f = mw_fold_all ? f * om.fold_to_bet(p.id) : std::min(f, om.fold_to_bet(p.id)); nf++; }
      if (nf) {
        double P = pot, B = std::max((double)bb, 0.6 * pot); if (B >= stack) B = stack;
        double frac_c = std::max(0.05, frac * (1 - f)), ec = e;
        if (frac_c < 1.0) { int kk = top_range(hole, board, n_board, frac_c, range); ec = equity_vs_range(hole, board, n_board, n_opp, range, kk, b, t0); }
        double ev_bet = f * P + (1 - f) * (ec * (P + 2 * B) - B), ev_check = e * P;
        // value bets need a small edge; a bluff (betting a hand that is behind) needs a large one until this
        // opponent has been seen to fold enough times
        double nev = 0; for (auto& p : tr.players) if (p.id != tr.me && !p.folded && p.stack + p.total > 0) nev += om.o[p.id].vsbet.n;
        double margin = (e >= 0.5 ? 0.03 : 0.05 + 0.25 * om.k_freq / (om.k_freq + nev)) * P;
        last_tag = ev_bet > ev_check + margin ? (e < 0.4 ? "bluff" : "bet") : "check";
        return last_tag == "check" ? "CHECK" : bet((int)B);
      }
    }
    double thr = 0.55 + 0.04 * (n_opp - 1);
    if (preflop && tr.me == tr.bb_id && tr.last_raiser == -1) thr = 0.55 + 0.03 * (n_opp - 1);   // BB option
    if (e > thr && can_raise) { last_tag = "bet"; return bet(std::max(bb, (int)(0.6 * pot))); }
    last_tag = "check";
    return "CHECK";
  }
};

}  // namespace bot
