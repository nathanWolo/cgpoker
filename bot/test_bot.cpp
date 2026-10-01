// test_bot.cpp - unit checks for the bot's building blocks: ICM against solvers/icm2.py's numbers and
// the HU jam/fold tables against solvers/pf.py's widths.  Exit 1 on failure.
#include <cmath>
#include <cstdio>
#include "bot.hpp"

static int fails = 0;
static void check(const char* name, double got, double want, double tol) {
  bool ok = std::fabs(got - want) <= tol;
  printf("%s %s: got %.4f want %.4f\n", ok ? "ok  " : "FAIL", name, got, want);
  fails += !ok;
}
// required equity to call off an all-in of size min(stack a, stack b): (E_fold - E_lose) / (E_win - E_lose)
static double req(const double* s0, int n, int a, int b, const double* pay) {
  double w[4], l[4]; for (int i = 0; i < n; i++) w[i] = l[i] = s0[i];
  double m = std::min(s0[a], s0[b]);
  w[a] += m; w[b] -= m; l[a] -= m; l[b] += m;
  double e0 = icm::icm(s0, n, a, pay), ew = icm::icm(w, n, a, pay), el = icm::icm(l, n, a, pay);
  return (e0 - el) / (ew - el);
}
int main() {
  const double P3[] = {1, .5, 0}, P4[] = {1, .645, .355, 0}, P4o[] = {1, .645, .355};   // icm2.py's rounding
  double e3[] = {1600, 1600, 1600}, e4[] = {1200, 1200, 1200, 1200}, big[] = {2400, 1200, 800, 400}, bb[] = {600, 1800, 1800, 600}, one_out[] = {3000, 900, 900};
  check("3p equal", req(e3, 3, 0, 1, P3), 0.600, 0.0015);
  check("4p equal", req(e4, 4, 0, 1, P4), 0.646, 0.0015);
  check("4p big calls short", req(big, 4, 0, 3, P4), 0.519, 0.0015);
  check("4p 2nd calls short", req(big, 4, 1, 3, P4), 0.532, 0.0015);
  check("4p big vs big", req(bb, 4, 1, 2, P4), 0.737, 0.0015);
  check("4p short vs short", req(bb, 4, 0, 3, P4), 0.557, 0.0015);
  check("4p one out mid vs mid", req(one_out, 3, 1, 2, P4o), 0.530, 0.0015);
  check("4p one out equal", req(e3, 3, 0, 1, P4o), 0.592, 0.0015);
  // busted seats: `stacks` holds the players alive at hand start; a 0 busts now.  4p game, 3 alive, I bust: 3rd = 0.3556
  double busted[] = {0, 2000, 2800};
  check("busted 3 alive of 4", icm::icm(busted, 3, 0, 4), 0.3556, 1e-9);
  double busted4[] = {0, 2000, 1800, 1000};
  check("busted 4 alive of 4", icm::icm(busted4, 4, 0, 4), 0.0, 1e-9);
  double busted2[] = {0, 0, 2800, 2000};
  check("two bust together 4p share 3rd+4th", icm::icm(busted2, 4, 0, 4), (0.3556 + 0) / 2, 1e-9);
  double lone[] = {4800, 0};
  check("last one standing", icm::icm(lone, 2, 0, 4), 1.0, 1e-9);
  // the bot's wrapper: seat 1 busted earlier, seat 0 busts now
  double v[] = {0, 0, 2000, 2800}; bool as[] = {true, false, true, true};
  check("bot icm_of busted earlier", bot::Bot::icm_of(v, as, 4, 0), 0.3556, 1e-9);
  double hu[] = {1800, 3000};
  check("HU linear", icm::icm(hu, 2, 0, 2), 1800.0 / 4800, 1e-9);
  // HU jam/fold table widths (share of the 1,326 combos) vs solvers/pf.py / export_pf.py
  auto width = [](const unsigned short* t, int k) { int c = 0; for (int i = 0; i < 169; i++) c += (t[i] >> k & 1) * ((i / 13 == i % 13) ? 6 : (i / 13 > i % 13) ? 4 : 12); return c / 1326.0; };
  int k10 = -1, k5 = -1, k20 = -1;
  for (int i = 0; i < pf::PF_NS; i++) { if (pf::PF_STACKS[i] == 10) k10 = i; if (pf::PF_STACKS[i] == 5) k5 = i; if (pf::PF_STACKS[i] == 20) k20 = i; }
  check("pf 10BB SB jam", width(pf::PF_JAM, k10), 0.584, 0.002);
  check("pf 10BB BB call", width(pf::PF_CALL, k10), 0.376, 0.002);
  check("pf 5BB SB jam", width(pf::PF_JAM, k5), 0.713, 0.002);
  check("pf 5BB BB call", width(pf::PF_CALL, k5), 0.620, 0.002);
  check("pf 20BB SB jam", width(pf::PF_JAM, k20), 0.403, 0.002);
  check("pf 20BB BB call", width(pf::PF_CALL, k20), 0.217, 0.002);
  // the 3-player ICM push/fold chart at 10/10/10 BB (a grid point): widths of the solver's own output
  // (solvers/pfn/pfn.cpp solve 3 3 10 10 10: D jams 29.4%, SB first in 82.8%, SB calls D 5.4%, BB calls fj 23.5%)
  if (const bot::PfnGame* g = bot::pfn_find(3, 3)) {
    double st[3] = {10, 10, 10};
    auto w = [&](int nd) {
      int k = -1; for (int q = 0; q < g->nn; q++) if (g->nodes[q] == nd) k = q;
      double T = bot::pfn_threshold(*g, k, st), c = 0;
      for (int h = 0; h < 169; h++) c += (g->pos[k * 169 + h] + 0.5 < T) * ((h / 13 == h % 13) ? 6 : (h / 13 > h % 13) ? 4 : 12);
      return c / 1326;
    };
    check("pfn 3p D first in", w(0), 0.294, 0.03);
    check("pfn 3p SB after f", w(1), 0.828, 0.03);
    check("pfn 3p SB after j", w(2), 0.054, 0.02);
    check("pfn 3p BB after fj", w(3 + 2), 0.235, 0.03);
    check("pfn 3p BB after jf", w(3 + 1), 0.071, 0.02);
    // between grid points the threshold interpolates: 12 BB must lie between the 10 and 14 BB charts
    double s10[3] = {10, 10, 10}, s12[3] = {12, 12, 12}, s14[3] = {14, 14, 14};
    double t10 = bot::pfn_threshold(*g, 0, s10), t12 = bot::pfn_threshold(*g, 0, s12), t14 = bot::pfn_threshold(*g, 0, s14);
    check("pfn interpolation", (t12 >= std::min(t10, t14) && t12 <= std::max(t10, t14)) ? 1 : 0, 1, 0);
    printf("     D first-in threshold classes: 10BB %.1f  12BB %.1f  14BB %.1f\n", t10, t12, t14);
  } else { printf("FAIL no 3p chart\n"); fails++; }
  printf("%s (%d failures)\n", fails ? "FAIL" : "PASS", fails);
  return fails ? 1 : 0;
}
