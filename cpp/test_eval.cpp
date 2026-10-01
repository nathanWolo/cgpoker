// Exhaustive correctness test for pe7c.hpp; exit status 0 = pass.
//   1. all C(52,7) = 133,784,560 seven-card hands: the 9 category counts and 4,824 distinct classes;
//   2. all C(52,6) =  20,358,520 six-card hands:   the 9 category counts and 6,075 distinct classes;
//   3. all C(52,5) =   2,598,960 five-card hands:  the 9 category counts and all 7,462 classes;
//   4. 1M random 7-card hands: ev(7 cards) equals the max of ev() over its 21 five-card subsets,
//      and add(board, hole) (two hands built separately from E) equals the card-by-card build.
// Category is read off the standard class boundaries, so (1)-(3) also check that ev() uses the
// standard 1..7462 numbering.  Reference counts: 7 and 5 cards as in the Wikipedia "Poker
// probability" tables; 6 cards as commonly tabulated (they sum to C(52,6)).  Ordering against
// other evaluators is cmp.cpp's job (needs fetch_third_party.sh).
#ifdef CGMODE
#pragma GCC optimize("O3")
#endif
#include <cstdio>
#include <chrono>
#include "pe7c.hpp"
#ifdef CGMODE
#pragma GCC target("avx2,bmi,bmi2,popcnt,lzcnt")
#endif
using namespace pe;
static double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
static const char* NAME[9] = {"high card", "pair", "two pair", "trips", "straight", "flush", "full house", "quads", "straight flush"};
static int cat(u16 v) { static const int B[9] = {1, 1278, 4138, 4996, 5854, 5864, 7141, 7297, 7453}; int k = 8; while (k > 0 && v < B[k]) k--; return k; }
static long long cc[9]; static char seen[7463]; static long long bad_range;
static inline void tally(u16 v) { if (v < 1 || v > 7462) { bad_range++; return; } cc[cat(v)]++; seen[v] = 1; }
static bool report(int ncards, const long long* ref, int ref_distinct, double secs) {
  bool ok = bad_range == 0; long long tot = 0; int distinct = 0;
  for (int i = 1; i <= 7462; i++) distinct += seen[i];
  printf("%d-card hands (%.2f s):\n", ncards, secs);
  for (int i = 0; i < 9; i++) { tot += cc[i]; printf("  %-14s %10lld  %s\n", NAME[i], cc[i], cc[i] == ref[i] ? "ok" : "MISMATCH"); ok &= cc[i] == ref[i]; }
  printf("  total %lld, distinct classes %d (expect %d) %s, out-of-range values %lld\n", tot, distinct, ref_distinct,
         distinct == ref_distinct ? "ok" : "MISMATCH", bad_range);
  ok &= distinct == ref_distinct;
  for (int i = 0; i < 9; i++) cc[i] = 0;
  for (int i = 0; i < 7463; i++) seen[i] = 0;
  bad_range = 0;
  return ok;
}
int main() {
  double t0 = now(); init(); double t1 = now();
  printf("init %.2f ms\n", (t1 - t0) * 1e3);
  bool ok = true;
  // 1. seven cards
  t0 = now();
  for (int a = 0; a < 52; a++) { H ha = add(E, a);
  for (int b = a + 1; b < 52; b++) { H hb = add(ha, b);
  for (int c = b + 1; c < 52; c++) { H hc = add(hb, c);
  for (int d = c + 1; d < 52; d++) { H hd = add(hc, d);
  for (int e = d + 1; e < 52; e++) { H he = add(hd, e);
  for (int f = e + 1; f < 52; f++) { H hf = add(he, f);
  for (int g = f + 1; g < 52; g++) tally(ev(add(hf, g))); }}}}}}
  const long long ref7[9] = {23294460, 58627800, 31433400, 6461620, 6180020, 4047644, 3473184, 224848, 41584};
  ok &= report(7, ref7, 4824, now() - t0);
  // 2. six cards
  t0 = now();
  for (int a = 0; a < 52; a++) { H ha = add(E, a);
  for (int b = a + 1; b < 52; b++) { H hb = add(ha, b);
  for (int c = b + 1; c < 52; c++) { H hc = add(hb, c);
  for (int d = c + 1; d < 52; d++) { H hd = add(hc, d);
  for (int e = d + 1; e < 52; e++) { H he = add(hd, e);
  for (int f = e + 1; f < 52; f++) tally(ev(add(he, f))); }}}}}
  const long long ref6[9] = {6612900, 9730740, 2532816, 732160, 361620, 205792, 165984, 14664, 1844};
  ok &= report(6, ref6, 6075, now() - t0);
  // 3. five cards
  t0 = now();
  for (int a = 0; a < 52; a++) { H ha = add(E, a);
  for (int b = a + 1; b < 52; b++) { H hb = add(ha, b);
  for (int c = b + 1; c < 52; c++) { H hc = add(hb, c);
  for (int d = c + 1; d < 52; d++) { H hd = add(hc, d);
  for (int e = d + 1; e < 52; e++) tally(ev(add(hd, e))); }}}}
  const long long ref5[9] = {1302540, 1098240, 123552, 54912, 10200, 5108, 3744, 624, 40};
  ok &= report(5, ref5, 7462, now() - t0);
  // 4. seven cards vs best five of seven; combined vs incremental build
  t0 = now();
  u64 rs = 88172645463325252ull; long long bad5 = 0, badc = 0; const int N = 1000000;
  for (int t = 0; t < N; t++) {
    int cs[7]; u64 used = 0;
    for (int j = 0; j < 7; j++) { int c; do { rs ^= rs << 7; rs ^= rs >> 9; c = (int)(((rs >> 32) * 52) >> 32); } while (used >> c & 1); used |= 1ull << c; cs[j] = c; }
    H h = E; for (int j = 0; j < 7; j++) h = add(h, cs[j]);
    u16 v7 = ev(h), best = 0;
    for (int x = 0; x < 7; x++) for (int y = x + 1; y < 7; y++) {   // drop cards x and y
      H h5 = E; for (int j = 0; j < 7; j++) if (j != x && j != y) h5 = add(h5, cs[j]);
      u16 v5 = ev(h5); if (v5 > best) best = v5;
    }
    bad5 += v7 != best;
    H bd = E, hole = E; for (int j = 0; j < 5; j++) bd = add(bd, cs[j]); hole = add(add(hole, cs[5]), cs[6]);
    badc += ev(add(bd, hole)) != v7;
  }
  printf("%d random 7-card hands (%.2f s): ev != best-of-21 five-card subsets: %lld %s; add(board,hole) != incremental: %lld %s\n",
         N, now() - t0, bad5, bad5 ? "MISMATCH" : "ok", badc, badc ? "MISMATCH" : "ok");
  ok &= bad5 == 0 && badc == 0;
  printf("%s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
