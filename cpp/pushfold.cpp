// (a) 169x169 preflop all-in equity matrix by Monte Carlo (class vs class, card removal respected);
// (b) HU Nash push/fold (SB push / BB call, blinds 0.5/1, no ante) by fictitious play, timed per stack depth.
#ifdef CGMODE
#pragma GCC optimize("O3")
#endif
#include <cstdio>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>
#include "pe7c.hpp"
#ifdef CGMODE
#pragma GCC target("avx2,bmi,bmi2,popcnt,lzcnt")
#endif
using namespace pe;
static double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
// class k: pairs 0..12 (rank), suited 13..90, offsuit 91..168
int NCB[169]; int CC[169][12][2];
static float EQ[169][169];
int main(int argc, char** argv) {
  init();
  int k = 13, ks[13][13];
  for (int r = 0; r < 13; r++) ks[r][r] = r;
  for (int a = 12; a >= 0; a--) for (int b = a - 1; b >= 0; b--) ks[a][b] = k++;           // suited: hi,lo
  for (int a = 12; a >= 0; a--) for (int b = a - 1; b >= 0; b--) ks[b][a] = k++;           // offsuit stored [lo][hi]
  for (int c1 = 0; c1 < 52; c1++) for (int c2 = c1 + 1; c2 < 52; c2++) {
    int r1 = c1 >> 2, r2 = c2 >> 2, s1 = c1 & 3, s2 = c2 & 3, hi = r1 > r2 ? r1 : r2, lo = r1 > r2 ? r2 : r1;
    int cl = r1 == r2 ? r1 : s1 == s2 ? ks[hi][lo] : ks[lo][hi];
    CC[cl][NCB[cl]][0] = c1; CC[cl][NCB[cl]++][1] = c2;
  }
  int T = argc > 1 ? atoi(argv[1]) : 6000;   // trials per class pair
  double t0 = now();
  auto work = [&](int tid, int nt) {
    u64 rs = 0x9E3779B97F4A7C15ull * (tid + 1);
    auto rnd = [&]() { u64 z = (rs += 0x9E3779B97F4A7C15ull); z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; return z ^ (z >> 31); };
    for (int i = tid; i < 169; i += nt) for (int j = i + 1; j < 169; j++) {
      double w = 0; int n = 0;
      while (n < T) {
        int a = (int)(((rnd() >> 32) * NCB[i]) >> 32), b = (int)(((rnd() >> 32) * NCB[j]) >> 32);
        int a0 = CC[i][a][0], a1 = CC[i][a][1], b0 = CC[j][b][0], b1 = CC[j][b][1];
        if (a0 == b0 || a0 == b1 || a1 == b0 || a1 == b1) continue;
        u64 used = 1ull << a0 | 1ull << a1 | 1ull << b0 | 1ull << b1; H bd = E;
        for (int q = 0; q < 5; q++) { int c; do c = (int)(((rnd() >> 32) * 52) >> 32); while (used >> c & 1); used |= 1ull << c; bd = add(bd, c); }
        u16 x = ev(add(add(bd, a0), a1)), y = ev(add(add(bd, b0), b1)); w += x > y ? 1 : x == y ? 0.5 : 0; n++;
      }
      EQ[i][j] = w / n; EQ[j][i] = 1 - EQ[i][j];
    }
    for (int i = tid; i < 169; i += nt) EQ[i][i] = 0.5f;
  };
  int nt = 4; std::vector<std::thread> th; for (int t = 0; t < nt; t++) th.emplace_back(work, t, nt); for (auto& x : th) x.join();
  double t1 = now();
  printf("169x169 MC matrix, %d trials/pair (SE %.2f%%): %.2f s on %d threads (%.0f M trials/s total)\n", T, 50 / sqrt((double)T), t1 - t0, nt, 14196.0 * T / (t1 - t0) / 1e6);
  printf("  AA vs KK %.3f (exact 0.819), AKs vs QQ %.3f (0.460), 72o vs AA %.3f (0.123)\n", EQ[12][11], EQ[ks[12][11]][10], EQ[ks[0][5]][12]);
  // combo-count weights (ignoring card removal between classes)
  float wt[169]; for (int i = 0; i < 169; i++) wt[i] = NCB[i];
  for (double S : {5.0, 10.0, 15.0, 20.0}) {
    double ts = now(); int IT = 2000;
    static double P[169], Cl[169]; for (int i = 0; i < 169; i++) P[i] = Cl[i] = 0.5;
    for (int it = 1; it <= IT; it++) {
      // BB best response to average push range P
      double pw = 0; for (int i = 0; i < 169; i++) pw += wt[i] * P[i];
      for (int j = 0; j < 169; j++) {
        double e = 0; for (int i = 0; i < 169; i++) e += wt[i] * P[i] * EQ[j][i];
        e /= pw; double call = e * 2 * S - S, fold = -1; double br = call > fold;
        Cl[j] += (br - Cl[j]) / (it + 1);
      }
      double cw = 0; for (int j = 0; j < 169; j++) cw += wt[j] * Cl[j];
      for (int i = 0; i < 169; i++) {       // SB best response to average call range
        double e = 0, cwi = 0; for (int j = 0; j < 169; j++) { double q = wt[j] * Cl[j]; cwi += q; e += q * EQ[i][j]; }
        double pc = cwi / 1326.0; double push = (1 - pc) * 1 + (cwi > 0 ? pc * ((e / cwi) * 2 * S - S) : 0), fold = -0.5;
        double br = push > fold; P[i] += (br - P[i]) / (it + 1);
      }
    }
    double te = now(); double pp = 0, cc = 0; for (int i = 0; i < 169; i++) pp += wt[i] * P[i], cc += wt[i] * Cl[i];
    printf("HU push/fold %2.0f BB: SB pushes %.1f%%, BB calls %.1f%% of hands; %d FP iterations in %.1f ms\n", S, pp / 13.26, cc / 13.26, IT, (te - ts) * 1e3);
  }
  return 0;
}
