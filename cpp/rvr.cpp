// Exact range-vs-range equity: for every hero combo (1326), equity vs a weighted villain range,
// enumerating all remaining board runouts. Per full board: evaluate each live combo, counting-sort by
// strength, then one blocker-aware sweep (per-card cumulative villain weight). CG flags + pragma.
#ifdef CGMODE
#pragma GCC optimize("O3")
#endif
#include <cstdio>
#include <chrono>
#include <cmath>
#include "pe7c.hpp"
#ifdef CGMODE
#pragma GCC target("avx2,bmi,bmi2,popcnt,lzcnt")
#endif
#define AI __attribute__((always_inline)) inline
using namespace pe;
static double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
int CA[1326], CB[1326];
float W[1326];            // villain range weights
double EQ[1326], NB[1326]; // accumulated equity (in villain-weight units) and normaliser
static u16 S[1326]; static int ord_[1326], cntv[7464];
// one full board (5 cards in mask bm, hand bh): accumulate hero equity vs W
AI void board(H bh, u64 bm) {
  int n = 0; static int live[1326];
  for (int i = 0; i < 1326; i++) if (!((bm >> CA[i] | bm >> CB[i]) & 1)) { S[i] = ev(add(add(bh, CA[i]), CB[i])); live[n++] = i; }
  // counting sort by strength via a small 2-pass radix (13-bit values)
  static int tmp[1326], c1[128], c2[64];
  for (int k = 0; k < 128; k++) c1[k] = 0; for (int k = 0; k < 64; k++) c2[k] = 0;
  for (int t = 0; t < n; t++) c1[S[live[t]] & 127]++, c2[S[live[t]] >> 7]++;
  for (int k = 1; k < 128; k++) c1[k] += c1[k - 1]; for (int k = 1; k < 64; k++) c2[k] += c2[k - 1];
  for (int t = n - 1; t >= 0; t--) tmp[--c1[S[live[t]] & 127]] = live[t];
  for (int t = n - 1; t >= 0; t--) ord_[--c2[S[tmp[t]] >> 7]] = tmp[t];
  // sweep ascending: below = total villain weight strictly weaker; card[c] = same restricted to combos holding c
  double tot = 0, card[52] = {0}; for (int t = 0; t < n; t++) { int i = live[t]; tot += W[i]; }
  double below = 0, cb[52] = {0}; double ctot[52] = {0};
  for (int t = 0; t < n; t++) { int i = live[t]; ctot[CA[i]] += W[i]; ctot[CB[i]] += W[i]; }
  for (int t = 0; t < n;) {
    int u = t; while (u < n && S[ord_[u]] == S[ord_[t]]) u++;
    double tie = 0; static double tiec[52]; // ties count half
    for (int k = t; k < u; k++) { int i = ord_[k]; tiec[CA[i]] = tiec[CB[i]] = 0; }
    for (int k = t; k < u; k++) { int i = ord_[k]; tie += W[i]; tiec[CA[i]] += W[i]; tiec[CB[i]] += W[i]; }
    for (int k = t; k < u; k++) {
      int i = ord_[k], a = CA[i], b = CB[i];
      double win = below - cb[a] - cb[b];
      // villain combos in the same group not sharing a card with hero
      double tg = tie - tiec[a] - tiec[b] + W[i];
      double valid = tot - ctot[a] - ctot[b] + W[i];
      EQ[i] += win + 0.5 * tg; NB[i] += valid;
    }
    for (int k = t; k < u; k++) { int i = ord_[k]; below += W[i]; cb[CA[i]] += W[i]; cb[CB[i]] += W[i]; }
    t = u; (void)card;
  }
}
int main(int argc, char** argv) {
  init();
  int n = 0; for (int a = 0; a < 52; a++) for (int b = a + 1; b < 52; b++) CA[n] = a, CB[n] = b, n++;
  for (int i = 0; i < 1326; i++) W[i] = 1.0f;      // uniform villain range
  int fb[3] = {0, 9, 22}; H f = E; u64 fm = 0; for (int c : fb) f = add(f, c), fm |= 1ull << c;
  // river
  { double t0 = now(); int reps = 2000;
    for (int r = 0; r < reps; r++) { for (int i = 0; i < 1326; i++) EQ[i] = NB[i] = 0; board(add(add(f, 35), 48), fm | 1ull << 35 | 1ull << 48); }
    double t1 = now(); printf("river rvr (1 board): %.1f us\n", (t1 - t0) / reps * 1e6); }
  // turn
  { double t0 = now(); int reps = 50;
    for (int r = 0; r < reps; r++) { for (int i = 0; i < 1326; i++) EQ[i] = NB[i] = 0; H t = add(f, 35); u64 tm = fm | 1ull << 35;
      for (int rv = 0; rv < 52; rv++) if (!(tm >> rv & 1)) board(add(t, rv), tm | 1ull << rv); }
    double t1 = now(); printf("turn rvr (48 boards): %.2f ms\n", (t1 - t0) / reps * 1e3); }
  // flop
  { double t0 = now(); int reps = 3;
    for (int r = 0; r < reps; r++) { for (int i = 0; i < 1326; i++) EQ[i] = NB[i] = 0;
      for (int t = 0; t < 52; t++) if (!(fm >> t & 1)) for (int rv = t + 1; rv < 52; rv++) if (!(fm >> rv & 1)) board(add(add(f, t), rv), fm | 1ull << t | 1ull << rv); }
    double t1 = now(); printf("flop rvr (1176 boards): %.2f ms\n", (t1 - t0) / reps * 1e3);
    // sanity: AsAh (51,50) equity vs random on 2c 4d 7h (0,9,22) should equal bench's flop number
    int k = 0; for (int i = 0; i < 1326; i++) if (CA[i] == 50 && CB[i] == 51) k = i;
    printf("  AA eq vs random on flop = %.4f (bench exact: 0.8637)\n", EQ[k] / NB[k]); }
  return 0;
}
