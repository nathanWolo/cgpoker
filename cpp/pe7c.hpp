#pragma once
// pe7c.hpp - 5..7-card Texas hold'em hand evaluator, CodinGame-ready (the one to ship).
//
// Usage: pe::init() once (builds every table: ~80 ms with CodinGame's flags here, see README.md),
// then build hands incrementally from pe::E:  H h = add(add(E, c0), c1); ... ; u16 v = ev(h);
// Card c = 4*rank + suit, rank 0..12 = 2..A, suit 0..3.  ev() is defined for 5, 6 or 7 distinct
// cards and returns 1..7462, higher is better: the hand's rank among the standard 7,462
// five-card equivalence classes (1..1277 high card, 1278..4137 pair, 4138..4995 two pair,
// 4996..5853 trips, 5854..5863 straight, 5864..7140 flush, 7141..7296 full house,
// 7297..7452 quads, 7453..7462 straight flush).  add(H, H) combines two hands that were each
// built from E (e.g. board + hole cards).  Tables are plain globals: include from one TU only.
//
// CodinGame form: no std containers and no payload (CG compiles at -O0 and the source's
// `#pragma GCC optimize("O3")` only optimizes function bodies, so hot helpers are
// always_inline).  No preprocessor macros either: crossfish's tools/cg_minify.py renames
// macro names inconsistently, which is why the earlier `#define AI` / PE_SH version failed
// to compile after minification.  `#pragma once` must stay on line 1: the minifier strips
// it only there.  pe7.hpp is the readable std::vector reference version of the same method.
//
// Attribution: the method follows OMPEval by Timo A. (https://github.com/zekyll/OMPEval,
// ISC license; notice reproduced in cpp/THIRD_PARTY.md).  Taken from OMPEval: the 13
// additive rank keys RK[] (OMPEval's RANKS constants, chosen so every multiset of 0..7 ranks
// has a unique sum), the hand layout (rank-key sum plus 4-bit suit counters that start at 3,
// so `& 0x8888` flags a flush, plus 16-bit per-suit rank masks), the 8192-entry flush table,
// and the perfect hash from rank key to class by row displacement (rows placed largest first
// at the lowest offset that does not conflict; OMPEval's PERF_HASH_ROW_OFFSETS scheme).
// Differences: rows are 2^8 keys wide instead of OMPEval's 2^12 so the hash can be rebuilt at
// start-up (OMPEval ships a precomputed 102 KB offset table instead), and ev() returns the
// dense 1..7462 class instead of OMPEval's category*4096 + rank.
#include <cstdint>
#include <cstring>
namespace pe {
typedef uint64_t u64; typedef uint32_t u32; typedef uint16_t u16;
const u32 RK[13] = {0x2000, 0x8001, 0x11000, 0x3a000, 0x91000, 0x176005, 0x366000,
                    0x41a013, 0x47802e, 0x479068, 0x48c0e4, 0x48f211, 0x494493};
enum { SH = 8, NR = ((4 * 0x494493 + 3 * 0x48f211) >> SH) + 1, LN = 1 << 18, NM = 73775 };
u16 FL[8192], LK[LN]; u32 OFF[NR];
struct H { u64 k, m; };
H C[52]; const H E = {0x3333ull << 32, 0};
__attribute__((always_inline)) inline H add(H a, int c) { return {a.k + C[c].k, a.m | C[c].m}; }
__attribute__((always_inline)) inline H add(H a, H b) { return {a.k + b.k - E.k, a.m | b.m}; }
__attribute__((always_inline)) inline u16 ev(const H& h) {
  u64 f = h.k >> 32 & 0x8888;
  if (f) return FL[h.m >> (4 * __builtin_ctzll(f) & ~15) & 0x1fff];
  u32 k = h.k; return LK[k + OFF[k >> SH]];
}
int st(u32 m) { m = m << 1 | (m >> 12 & 1); for (int r = 13; r > 3; r--) if ((m >> (r - 4) & 31) == 31) return r - 1; return -1; }
u32 tp(u32 m, int n) { u32 v = 0; for (int r = 12; r >= 0 && n; r--) if (m >> r & 1) v = v << 4 | r, n--; while (n--) v <<= 4; return v; }
int hi(u32 m) { return 31 - __builtin_clz(m); }
u32 slowc(int* c) {                                  // non-flush code from rank counts
  u32 a = 0, q = 0, t = 0, p = 0;
  for (int r = 0; r < 13; r++) { u32 b = 1u << r; if (c[r]) a |= b; if (c[r] == 4) q |= b; if (c[r] == 3) t |= b; if (c[r] == 2) p |= b; }
  if (q) { int r = hi(q); return 7 << 20 | r << 16 | tp(a & ~(1u << r), 1) << 12; }
  if (t && (t & (t - 1) || p)) { int r = hi(t); return 6 << 20 | r << 16 | hi(t & ~(1u << r) | p) << 12; }
  int s = st(a); if (s >= 0) return 4 << 20 | s << 16;
  if (t) { int r = hi(t); return 3 << 20 | r << 16 | tp(a & ~(1u << r), 2) << 8; }
  if (p & (p - 1)) { int x = hi(p), y = hi(p & ~(1u << x)); return 2 << 20 | x << 16 | y << 12 | tp(a & ~(1u << x) & ~(1u << y), 1) << 8; }
  if (p) { int x = hi(p); return 1 << 20 | x << 16 | tp(a & ~(1u << x), 3) << 4; }
  return tp(a, 5);
}
u32 slowf(u32 m) { int s = st(m); return s >= 0 ? 8 << 20 | s << 16 : 5 << 20 | tp(m, 5); }
u32 KY[NM], CD[NM], nk, RS[NR + 1], RE[NM], NX[LN + 1]; int cn[13]; u64 BM[(9 << 20) / 64 + 1]; u32 PR[(9 << 20) / 64 + 1];
void rec(int r, int n, u32 key) {
  if (r == 13) { if (n > 4) KY[nk] = key, CD[nk++] = slowc(cn); return; }
  for (int k = 0; k < 5 && n + k < 8; k++) cn[r] = k, rec(r + 1, n + k, key + k * RK[r]);
  cn[r] = 0;
}
__attribute__((always_inline)) inline u16 cls(u32 c) { return PR[c >> 6] + __builtin_popcountll(BM[c >> 6] & ((1ull << (c & 63)) - 1)) + 1; }
u32 fnd(u32 i) { while (NX[i] != i) i = NX[i] = NX[NX[i]]; return i; }
void init() {
  for (int c = 0; c < 52; c++) C[c] = {RK[c >> 2] + (1ull << (32 + 4 * (c & 3))), 1ull << (16 * (c & 3) + (c >> 2))};
  rec(0, 0, 0);
  for (u32 i = 0; i < nk; i++) BM[CD[i] >> 6] |= 1ull << (CD[i] & 63);           // dense ranks via bitmap
  for (u32 m = 0; m < 8192; m++) if (__builtin_popcount(m) > 4) { u32 c = slowf(m); BM[c >> 6] |= 1ull << (c & 63); }
  for (u32 i = 1; i < sizeof BM / 8; i++) PR[i] = PR[i - 1] + __builtin_popcountll(BM[i - 1]);
  for (u32 m = 0; m < 8192; m++) FL[m] = __builtin_popcount(m) > 4 ? cls(slowf(m)) : 0;
  for (u32 i = 0; i < nk; i++) RS[(KY[i] >> SH) + 1]++;                            // bucket keys by row
  for (u32 i = 0; i < NR; i++) RS[i + 1] += RS[i];
  static u32 fill[NR]; memcpy(fill, RS, sizeof fill);
  for (u32 i = 0; i < nk; i++) RE[fill[KY[i] >> SH]++] = i;
  for (u32 i = 0; i <= LN; i++) NX[i] = i;
  for (int sz = 64; sz > 0; sz--) for (u32 r = 0; r < NR; r++) if (RS[r + 1] - RS[r] == (u32)sz) {  // largest rows first
    u32 m = (1 << SH) - 1, e0 = KY[RE[RS[r]]] & m, o;
    for (u32 f = fnd(e0);; f = fnd(f + 1)) {
      o = f - e0; u32 j = RS[r];
      for (; j < RS[r + 1]; j++) { u32 i = RE[j]; u16 v = LK[(KY[i] & m) + o]; if (v && v != cls(CD[i])) break; }
      if (j == RS[r + 1]) break;
    }
    for (u32 j = RS[r]; j < RS[r + 1]; j++) { u32 i = RE[j], s = (KY[i] & m) + o; LK[s] = cls(CD[i]); NX[s] = s + 1; }
    OFF[r] = o - (r << SH);
  }
}
}  // namespace pe
