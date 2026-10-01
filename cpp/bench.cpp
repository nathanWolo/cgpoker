// Evaluator benchmarks: pe7c.hpp by default; pe7.hpp with -DPE_REF (plus -DPE_DIRECT for its
// 67 MB direct table, or -DPE_SH=n for its hash row width).  Build two ways (see Makefile):
//   g++ -std=gnu++17 -O3 -march=native -pthread bench.cpp                  (local best case)
//   g++ -std=gnu++17 -Werror=return-type -g -pthread -DCGMODE bench.cpp   (CodinGame's command line;
//        the optimize/target pragmas below are what a CG submission would carry)
// The category counts printed here are a sanity check; test_eval.cpp is the pass/fail test.
#ifdef CGMODE
#pragma GCC optimize("O3")
#endif
#include <cstdio>
#include <chrono>
#include <cstdlib>
#ifdef PE_REF
#include "pe7.hpp"
#else
#include "pe7c.hpp"
namespace pe { static int cat(u16 v){ static const int B[9]={1,1278,4138,4996,5854,5864,7141,7297,7453}; int k=8; while(v<B[k]) k--; return k; } }
#endif
#ifdef CGMODE
#pragma GCC target("avx2,bmi,bmi2,popcnt,lzcnt")
#endif
#define AI __attribute__((always_inline)) inline
using namespace pe;
static double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
static u64 rs = 88172645463325252ull;
AI u64 rnd() { rs ^= rs << 7; rs ^= rs >> 9; return rs; }
AI int rcard() { return (int)(((rnd() >> 32) * 52) >> 32); }

int main() {
  double t0 = now(); init(); double t1 = now();
#ifdef PE_REF
  printf("init %.2f ms, LK used %u entries (%u bytes), OFF %zu entries, FL 8192\n", (t1 - t0) * 1e3, LKN, LKN * 2, sizeof(OFF) / 4);
#else
  { u32 mx=0; for(u32 i=0;i<LN;i++) if(LK[i]) mx=i+1; printf("init %.2f ms, LK used %u entries, OFF %zu entries, FL 8192\n", (t1 - t0) * 1e3, mx, sizeof(OFF) / 4); }
#endif
  // ---- 1. exhaustive C(52,7) enumeration, incremental ----
  long long catc[9] = {0}; u64 chk = 0;
  t0 = now();
  for (int a = 0; a < 52; a++) { H ha = add(E, a);
  for (int b = a + 1; b < 52; b++) { H hb = add(ha, b);
  for (int c = b + 1; c < 52; c++) { H hc = add(hb, c);
  for (int d = c + 1; d < 52; d++) { H hd = add(hc, d);
  for (int e = d + 1; e < 52; e++) { H he = add(hd, e);
  for (int f = e + 1; f < 52; f++) { H hf = add(he, f);
  for (int g = f + 1; g < 52; g++) { u16 v = ev(add(hf, g)); chk += v; catc[v >> 10 & 7]++; }}}}}}}
  t1 = now();
  printf("seq enum 133784560 hands: %.3f s = %.0f M eval/s (chk %llu)\n", t1 - t0, 133784560 / (t1 - t0) / 1e6, (unsigned long long)chk);
  // category counts (separate pass, not timed)
  long long cc[9] = {0}; int seen[7463] = {0};
  for (int a = 0; a < 52; a++) { H ha = add(E, a);
  for (int b = a + 1; b < 52; b++) { H hb = add(ha, b);
  for (int c = b + 1; c < 52; c++) { H hc = add(hb, c);
  for (int d = c + 1; d < 52; d++) { H hd = add(hc, d);
  for (int e = d + 1; e < 52; e++) { H he = add(hd, e);
  for (int f = e + 1; f < 52; f++) { H hf = add(he, f);
  for (int g = f + 1; g < 52; g++) { u16 v = ev(add(hf, g)); cc[cat(v)]++; seen[v] = 1; }}}}}}}
  const long long ref[9] = {23294460, 58627800, 31433400, 6461620, 6180020, 4047644, 3473184, 224848, 41584};
  bool ok = true; int distinct = 0; for (int i = 1; i <= 7462; i++) distinct += seen[i];
  for (int i = 0; i < 9; i++) { printf("  cat%d %lld %s\n", i, cc[i], cc[i] == ref[i] ? "ok" : "MISMATCH"); ok &= cc[i] == ref[i]; }
  printf("category counts %s; distinct 7-card classes %d (expect 4824)\n", ok ? "ALL OK" : "FAIL", distinct);
  // ---- 2. random hands from a pregenerated array (7 x uint8), hand built from scratch ----
  const int N = 1 << 20; static unsigned char hs[N][7];
  for (int i = 0; i < N; i++) { u64 used = 0; for (int j = 0; j < 7; j++) { int c; do c = rcard(); while (used >> c & 1); used |= 1ull << c; hs[i][j] = c; } }
  t0 = now(); chk = 0;
  for (int rep = 0; rep < 20; rep++) for (int i = 0; i < N; i++) { H h = E; for (int j = 0; j < 7; j++) h = add(h, hs[i][j]); chk += ev(h); }
  t1 = now(); printf("rand (7 adds + eval): %.0f M eval/s (chk %llu)\n", 20.0 * N / (t1 - t0) / 1e6, (unsigned long long)chk);
  // ---- 3. Monte Carlo HU equity, hero AsKs vs random hand, preflop ----
  int h0 = 4 * 12 + 3, h1 = 4 * 11 + 3; H hero = add(add(E, h0), h1);
  long long T = 20000000; double win = 0; t0 = now();
  for (long long t = 0; t < T; t++) {
    u64 used = (1ull << h0) | (1ull << h1); int cs[7];
    for (int j = 0; j < 7; j++) { int c; do c = rcard(); while (used >> c & 1); used |= 1ull << c; cs[j] = c; }
    H b = E; for (int j = 0; j < 5; j++) b = add(b, cs[j]);
    u16 x = ev(add(b, hero) ), y = ev(add(add(b, cs[5]), cs[6]));
    win += x > y ? 1.0 : x == y ? 0.5 : 0.0;
  }
  t1 = now(); printf("MC preflop AKs vs random: eq %.4f, %.1f M trials/s (2 evals + 7-card sample each)\n", win / T, T / (t1 - t0) / 1e6);
  // ---- 4. river: strength of every live combo on a fixed board (range bucketing / showdown vs range) ----
  int bd[5] = {0, 9, 22, 35, 48}; H hb = E; u64 bu = 0; for (int c : bd) hb = add(hb, c), bu |= 1ull << c;
  static u16 str[52][52]; int reps = 2000; t0 = now(); u64 s = 0;
  for (int r = 0; r < reps; r++) { for (int a = 0; a < 52; a++) if (!(bu >> a & 1)) { H ha = add(hb, a); for (int b = a + 1; b < 52; b++) if (!(bu >> b & 1)) s += str[a][b] = ev(add(ha, b)); } }
  t1 = now(); printf("river: all 1081 combos on a board: %.2f us per board (%llu)\n", (t1 - t0) / reps * 1e6, (unsigned long long)s);
  // ---- 5. turn: exact equity of hero vs uniform range (46 rivers x ~990 combos) ----
  int tb[4] = {0, 9, 22, 35}; H ht = E; u64 tu = 0; for (int c : tb) ht = add(ht, c), tu |= 1ull << c;
  int ha0 = 51, ha1 = 50; tu |= 1ull << ha0 | 1ull << ha1; reps = 200; double eq = 0; t0 = now();
  for (int r = 0; r < reps; r++) {
    double w = 0, n = 0;
    for (int rv = 0; rv < 52; rv++) if (!(tu >> rv & 1)) {
      H b5 = add(ht, rv); u64 u5 = tu | 1ull << rv; u16 hv = ev(add(add(b5, ha0), ha1));
      for (int a = 0; a < 52; a++) if (!(u5 >> a & 1)) { H ha = add(b5, a); for (int b = a + 1; b < 52; b++) if (!(u5 >> b & 1)) { u16 v = ev(add(ha, b)); w += hv > v ? 1 : hv == v ? 0.5 : 0; n++; } }
    }
    eq = w / n;
  }
  t1 = now(); printf("turn exact equity vs random (48x~990 evals): eq %.4f, %.3f ms each\n", eq, (t1 - t0) / reps * 1e3);
  // ---- 6. flop: exact equity hero vs random hand, all turn/river runouts (1,070,190 matchups) ----
  int fb[3] = {0, 9, 22}; H hf = E; u64 fu = 0; for (int c : fb) hf = add(hf, c), fu |= 1ull << c;
  fu |= 1ull << ha0 | 1ull << ha1; reps = 5; t0 = now();
  for (int r = 0; r < reps; r++) {
    double w = 0, n = 0;
    for (int t = 0; t < 52; t++) if (!(fu >> t & 1)) for (int rv = t + 1; rv < 52; rv++) if (!(fu >> rv & 1)) {
      H b5 = add(add(hf, t), rv); u64 u5 = fu | 1ull << t | 1ull << rv; u16 hv = ev(add(add(b5, ha0), ha1));
      for (int a = 0; a < 52; a++) if (!(u5 >> a & 1)) { H ha = add(b5, a); for (int b = a + 1; b < 52; b++) if (!(u5 >> b & 1)) { u16 v = ev(add(ha, b)); w += hv > v ? 1 : hv == v ? 0.5 : 0; n++; } }
    }
    eq = w / n;
  }
  t1 = now(); printf("flop exact equity vs random (~1.07M evals): eq %.4f, %.2f ms each\n", eq, (t1 - t0) / reps * 1e3);
  return 0;
}
