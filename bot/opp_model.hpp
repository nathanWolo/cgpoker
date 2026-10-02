#pragma once
// opp_model.hpp - real-time opponent model, learned within the game from what each opponent did and from
// the hole cards the referee reveals after every hand (folded hands too).
//
// Per opponent and situation it keeps a count and a sum: how often they raised first in, limped, folded to a
// raise, folded to a bet, bet when checked to, folded to a shove; and the strength percentile (0 = best, by
// bot/pf_rank.hpp) of the hands they were revealed to have done each of those with.  Every estimate is shrunk
// toward the bot's fixed self-consistent assumption with a prior weight of a few observations, so two hands
// change little and fifteen change a lot.  A range is described the way the bot's beliefs already are, as the
// top fraction of hands: a top-f range has mean percentile f/2, so the width of the hands behind an action is
// twice the mean revealed percentile (a bot that raises any two has mean 0.5 -> width 1, uniform).
#include <algorithm>
#include <cmath>
#include <string>
#include "../engine/tracker.hpp"
#include "pf_rank.hpp"

namespace bot {

inline int om_hand_class(int c0, int c1) {
  int r0 = c0 >> 2, r1 = c1 >> 2, hi = std::max(r0, r1), lo = std::min(r0, r1);
  if (r0 == r1) return r0 * 13 + r0;
  return (c0 & 3) == (c1 & 3) ? hi * 13 + lo : lo * 13 + hi;
}

struct Est { double n = 0, s = 0; void add(double x) { n += 1; s += x; } };
// shrinkage estimate: prior mean `p` with the weight of `k` observations
inline double est(const Est& e, double p, double k) { return (p * k + e.s) / (k + e.n); }

struct OppStats {
  Est open, limp;                 // unopened preflop, forced to act (not the BB's option): s = raises / s = limps, n = opportunities
  Est open_pct, limp_pct;         // revealed percentile of the hands they raised first in / limped with
  Est vsraise, reraise;           // facing a preflop raise (not a shove): s = folds / s = re-raises; n = opportunities
  Est call_pct, reraise_pct;      // revealed percentile of the hands they called a raise / re-raised with
  Est jam_pct;                    // revealed percentile of the hands they moved all-in with preflop (any spot)
  Est vsjam, calljam_pct;         // facing a preflop all-in-sized call: s = folds; percentile of the hands they called with
  Est ck, vsbet;                  // postflop, checked to: s = bets; postflop, facing a bet: s = folds
  int hands = 0;
};

struct OppModel : pk::Tracker::SettleHook {
  OppStats o[4];
  double k_freq = 4, k_pct = 3;   // prior weights (observations)

  static double pct(int c0, int c1) { return pf::PF_PCT100[om_hand_class(c0, c1)] / 100.0; }
  static double clamp(double x, double lo, double hi) { return std::min(hi, std::max(lo, x)); }

  // ---- beliefs (each with the bot's fixed assumption as the prior)
  double open_width(int p, double prior) const { return clamp(2 * est(o[p].open_pct, prior / 2, k_pct), 0.05, 1.0); }
  double limp_width(int p, double prior) const { return clamp(2 * est(o[p].limp_pct, prior / 2, k_pct), 0.1, 1.0); }
  double call_width(int p, double prior) const { return clamp(2 * est(o[p].call_pct, prior / 2, k_pct), 0.1, 1.0); }
  double threebet_width(int p, double prior) const { return clamp(2 * est(o[p].reraise_pct, prior / 2, k_pct), 0.03, 1.0); }
  double jam_width(int p, double prior) const { return clamp(2 * est(o[p].jam_pct, prior / 2, k_pct), 0.03, 1.0); }
  double calljam_width(int p, double prior) const { return clamp(2 * est(o[p].calljam_pct, prior / 2, k_pct), 0.03, 1.0); }
  double bet_freq(int p, double prior = 0.4) const { return est(o[p].ck, prior, k_freq); }
  double fold_to_bet(int p, double prior = 0.45) const { return est(o[p].vsbet, prior, k_freq); }
  double fold_to_raise(int p, double prior = 0.5) const { return est(o[p].vsraise, prior, k_freq); }
  double fold_to_jam(int p, double prior) const { return est(o[p].vsjam, prior, k_freq); }
  double open_freq(int p, double prior = 0.35) const { return est(o[p].open, prior, k_freq); }

  // ---- the observation: the finished hand's actions with everyone's cards known
  void on_settle(pk::Tracker& t) override {
    int bb = t.bb;
    for (const auto& a : t.hand_log) {
      if (a.hand != t.hand_nb || a.pid == t.me || a.pid < 0) continue;
      OppStats& s = o[a.pid];
      const pk::Player& pl = t.players[a.pid];
      bool known = pl.n_hand == 2 && pl.hand[0] >= 0 && pl.hand[1] >= 0;
      double q = known ? pct(pl.hand[0], pl.hand[1]) : -1;
      bool fold = a.type == pk::A_FOLD, allin = a.type == pk::A_ALL_IN || (a.type == pk::A_BET && a.allin);
      bool raise = a.type == pk::A_BET || a.type == pk::A_ALL_IN;
      int stack_before = a.stack_after + a.added;
      if (a.street == 0) {
        if (!a.raised) {
          if (a.call > 0) {                                  // first in, must act: fold / limp / raise / jam
            s.open.n++; s.limp.n++;
            if (raise) { s.open.s++; if (known) s.open_pct.add(q); }
            else if (a.type == pk::A_CALL) { s.limp.s++; if (known) s.limp_pct.add(q); }
          }
          if (allin && known) s.jam_pct.add(q);
        } else if (a.call >= stack_before) {                 // facing an all-in-sized call
          s.vsjam.n++;
          if (fold) s.vsjam.s++; else if (known) s.calljam_pct.add(q);
        } else {                                             // facing a raise
          s.vsraise.n++; s.reraise.n++;
          if (fold) s.vsraise.s++;
          else if (raise) { s.reraise.s++; if (known) s.reraise_pct.add(q); if (allin && known) s.jam_pct.add(q); }
          else if (known) s.call_pct.add(q);
        }
      } else if (a.call == 0) { s.ck.n++; if (raise) s.ck.s++; }
      else { s.vsbet.n++; if (fold) s.vsbet.s++; }
    }
    for (int p = 0; p < t.n; p++) if (p != t.me && !t.players[p].eliminated) o[p].hands++;
    (void)bb;
  }

  // a short description for the diagnostics line
  std::string note(int p) const {
    char buf[160];
    snprintf(buf, sizeof buf, "om%d h%d open%.2f(%.0f) limp%.2f(%.0f) jam%.2f(%.0f) f2r%.2f(%.0f) bet%.2f(%.0f) f2b%.2f(%.0f)", p, o[p].hands,
             open_width(p, 0.35), o[p].open_pct.n, limp_width(p, 0.8), o[p].limp_pct.n, jam_width(p, 0.3), o[p].jam_pct.n,
             fold_to_raise(p), o[p].vsraise.n, bet_freq(p), o[p].ck.n, fold_to_bet(p), o[p].vsbet.n);
    return buf;
  }
};

}  // namespace bot
