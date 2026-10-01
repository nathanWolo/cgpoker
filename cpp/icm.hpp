#pragma once
// icm.hpp - tournament equity by the Independent Chip Model (Malmuth-Harville), for up to 4 players,
// with the placement payouts that CodinGame's placement-only TrueSkill implies at equal ratings
// (solvers/trueskill_payouts.py): 2p (1,0), 3p (1,.5,0), 4p (1,.6444,.3556,0).
//
// icm(stacks, n, seat, pay): seat's expected payout.  `stacks` holds the n players who were alive at
// the start of the hand (players who busted in earlier hands are left out: they already own the places
// below); `pay` is the payout vector of the whole game (n_total entries, n_total >= n).  A stack of 0
// means "busted in this hand": it takes the next place below everyone still alive (several busting
// together share those places).  Shared by the bot (bot/bot.hpp) and the push/fold solver
// (solvers/pfn/).  No tables, no state.
#include <algorithm>

namespace icm {

const double PAY2[] = {1, 0}, PAY3[] = {1, .5, 0}, PAY4[] = {1, .6444, .3556, 0};
inline const double* payouts(int n_total) { return n_total == 2 ? PAY2 : n_total == 3 ? PAY3 : PAY4; }

// P(seat finishes at `depth` or later places) recursion: the probability that `me` takes each place
inline double rec(const double* st, int n, double total, int depth, const double* pay, int me, bool* used) {
  double v = 0;
  for (int i = 0; i < n; i++) {
    if (used[i] || st[i] <= 0) continue;
    double p = st[i] / total;
    if (i == me) v += p * pay[depth];
    else {
      used[i] = true;
      v += p * rec(st, n, total - st[i], depth + 1, pay, me, used);
      used[i] = false;
    }
  }
  return v;
}

// stacks[0..n) chips of the players alive at hand start (0 = busted this hand); pay: the game's payouts
inline double icm(const double* stacks, int n, int seat, const double* pay) {
  double total = 0; int alive = 0, dead = 0;
  for (int i = 0; i < n; i++) {
    if (stacks[i] > 0) total += stacks[i], alive++; else dead++;
  }
  if (stacks[seat] <= 0) {                             // busted: the places below the alive players, shared
    double s = 0;
    for (int k = alive; k < alive + dead && k < n; k++) s += pay[k];
    return dead ? s / dead : 0;
  }
  if (alive == 1) return pay[0];
  bool used[4] = {false, false, false, false};
  return rec(stacks, n, total, 0, pay, seat, used);
}
// n_total: the players the game started with (chooses the payout vector)
inline double icm(const double* stacks, int n, int seat, int n_total) { return icm(stacks, n, seat, payouts(n_total)); }

}  // namespace icm
