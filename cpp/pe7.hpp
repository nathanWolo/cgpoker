// pe7.hpp - compact 5..7-card Texas hold'em evaluator (readable reference version of pe7c.hpp).
// Card c = 4*rank + suit, rank 0..12 = 2..A.  Result: 1..7462, higher is better
// (the standard 7462 equivalence classes; category = see cat()).
// Non-flush: additive rank keys (the 13 constants are OMPEval's, which make every
// 0..7-card rank multiset sum unique) -> perfect hash (row displacement, built at
// init) -> u16 table.  Flush: 13-bit suit mask -> u16 table.  Hands are built
// incrementally (H + card) so enumeration costs one add + one lookup per eval.
// Tables are computed at start-up (no payload).
// Reference / experiment version with std containers: not CodinGame-ready (use pe7c.hpp, which
// builds the same classes).  Options: -DPE_SH=n sets the perfect-hash row width to 2^n keys
// (default 12 = OMPEval's; pe7c.hpp uses 8), -DPE_DIRECT uses a 67 MB direct key->class table
// instead of the hash.  Used by bench.cpp (-DPE_REF) and prof.cpp.
// Attribution: rank keys, hand layout, flush table and row-displacement perfect hash follow
// OMPEval by Timo A. (https://github.com/zekyll/OMPEval, ISC license; see cpp/THIRD_PARTY.md).
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>
#ifndef PE_AI
#define PE_AI __attribute__((always_inline)) inline
#endif
namespace pe {
typedef uint64_t u64; typedef uint32_t u32; typedef uint16_t u16;
static const u32 RK[13] = {0x2000, 0x8001, 0x11000, 0x3a000, 0x91000, 0x176005, 0x366000,
                           0x41a013, 0x47802e, 0x479068, 0x48c0e4, 0x48f211, 0x494493};
static const u32 MAXK = 4 * 0x494493u + 3 * 0x48f211u;
#ifndef PE_SH
#define PE_SH 12
#endif
static const int SH = PE_SH;                    // perfect-hash row shift
static u16 FL[8192];                            // flush mask -> class
#ifdef PE_DIRECT
static u16 LK[MAXK + 1];                        // direct table, 67 MB, ~74k entries touched
#else
static u16 LK[1 << 18];                         // hashed non-flush table
#endif
static u32 OFF[(MAXK >> SH) + 1];               // row offsets
static u32 LKN;                                 // used size of LK
struct H { u64 k, m; };                         // k: rank key | suit nibbles<<32 ; m: suit masks
static H C[52];
static const H E = {0x3333ull << 32, 0};        // empty hand (nibbles start at 3: >=8 => flush)
PE_AI H add(H a, int c) { return {a.k + C[c].k, a.m | C[c].m}; }
PE_AI H add(H a, H b) { return {a.k + b.k - E.k, a.m | b.m}; }
PE_AI u16 ev(const H& h) {
  u64 f = (h.k >> 32) & 0x8888;
  if (f) return FL[(h.m >> (4 * __builtin_ctzll(f) & ~15)) & 0x1fff];
  u32 k = (u32)h.k;
#ifdef PE_DIRECT
  return LK[k];
#else
  return LK[k + OFF[k >> SH]];
#endif
}
// slow reference value of a rank-count vector (non-flush) or mask (flush)
static u32 top(u32 mask, int n) { u32 v = 0; for (int r = 12; r >= 0 && n; r--) if (mask >> r & 1) v = v << 4 | r, n--; while (n--) v <<= 4; return v; }
static int straight(u32 m) { m = m << 1 | (m >> 12 & 1); /* bit0 = low ace */
  for (int r = 13; r >= 4; r--) if ((m >> (r - 4) & 31) == 31) return r - 1; return -1; }
static u32 slowc(const int* c) {
  u32 pres = 0, q = 0, t = 0, p = 0; int nq = 0, nt = 0, np = 0;
  for (int r = 0; r < 13; r++) { if (c[r]) pres |= 1 << r; if (c[r] == 4) q |= 1 << r, nq++; else if (c[r] == 3) t |= 1 << r, nt++; else if (c[r] == 2) p |= 1 << r, np++; }
  if (nq) { int r = 31 - __builtin_clz(q); return 7 << 20 | r << 16 | top(pres & ~(1u << r), 1) << 12; }
  if (nt && (nt > 1 || np)) { int r = 31 - __builtin_clz(t); u32 rest = (t & ~(1u << r)) | p; return 6 << 20 | r << 16 | (31 - __builtin_clz(rest)) << 12; }
  int s = straight(pres); if (s >= 0) return 4 << 20 | s << 16;
  if (nt) { int r = 31 - __builtin_clz(t); return 3 << 20 | r << 16 | top(pres & ~(1u << r), 2) << 8; }
  if (np >= 2) { int a = 31 - __builtin_clz(p), b = 31 - __builtin_clz(p & ~(1u << a)); return 2 << 20 | a << 16 | b << 12 | top(pres & ~(1u << a) & ~(1u << b), 1) << 8; }
  if (np) { int a = 31 - __builtin_clz(p); return 1 << 20 | a << 16 | top(pres & ~(1u << a), 3) << 4; }
  return top(pres, 5);
}
static u32 slowf(u32 m) { int s = straight(m); return s >= 0 ? 8 << 20 | s << 16 : 5 << 20 | top(m, 5); }
static int cat(u16 v);                          // defined after init (class boundaries)
static u16 CATB[10];
static void init() {
  for (int c = 0; c < 52; c++) C[c] = {RK[c >> 2] + (1ull << (32 + 4 * (c & 3))), 1ull << (16 * (c & 3) + (c >> 2))};
  std::vector<std::pair<u32, u32>> nf;          // (key, code) for n = 5..7
  int cnt[13] = {0};
  auto rec = [&](auto&& self, int r, int n, u32 key) -> void {
    if (r == 13) { if (n >= 5) nf.push_back({key, slowc(cnt)}); return; }
    for (int k = 0; k <= 4 && n + k <= 7; k++) { cnt[r] = k; self(self, r + 1, n + k, key + k * RK[r]); }
    cnt[r] = 0;
  };
  rec(rec, 0, 0, 0);
  std::vector<u32> codes; for (auto& x : nf) codes.push_back(x.second);
  for (u32 m = 0; m < 8192; m++) if (__builtin_popcount(m) >= 5) codes.push_back(slowf(m));
  std::sort(codes.begin(), codes.end()); codes.erase(std::unique(codes.begin(), codes.end()), codes.end());
  auto cls = [&](u32 code) { return (u16)(std::lower_bound(codes.begin(), codes.end(), code) - codes.begin() + 1); };
  for (u32 m = 0; m < 8192; m++) FL[m] = __builtin_popcount(m) >= 5 ? cls(slowf(m)) : 0;
  for (int k = 0; k < 10; k++) CATB[k] = cls((u32)k << 20);
  // perfect hash: rows of 2^SH keys, placed largest-first at the lowest offset where every
  // slot is empty or already holds the same class (OMPEval's scheme)
  std::vector<std::vector<std::pair<u32, u16>>> rows((MAXK >> SH) + 1);
  for (auto& x : nf) rows[x.first >> SH].push_back({x.first & ((1 << SH) - 1), cls(x.second)});
  std::vector<u32> ord; for (u32 i = 0; i < rows.size(); i++) if (rows[i].size()) ord.push_back(i);
  std::stable_sort(ord.begin(), ord.end(), [&](u32 a, u32 b) { return rows[a].size() > rows[b].size(); });
#ifdef PE_DIRECT
  for (auto& x : nf) LK[x.first] = cls(x.second); LKN = MAXK + 1; return;
#endif
  LKN = 0;
  std::vector<u32> nx((1 << 18) + 1); for (u32 i = 0; i < nx.size(); i++) nx[i] = i;   // next free slot
  auto fnd = [&](u32 i) { while (nx[i] != i) i = nx[i] = nx[nx[i]]; return i; };
  for (u32 i : ord) {
    auto& R = rows[i]; std::sort(R.begin(), R.end()); u32 e0 = R[0].first, o;
    for (u32 f = fnd(e0);; f = fnd(f + 1)) {        // first entry goes to a free slot
      o = f - e0; bool ok = true;
      for (auto& e : R) { u16 v = LK[e.first + o]; if (v && v != e.second) { ok = false; break; } }
      if (ok) break;
    }
    for (auto& e : R) { u32 j = e.first + o; LK[j] = e.second; nx[j] = j + 1; LKN = std::max(LKN, j + 1); }
    OFF[i] = o - (i << SH);
  }
}
static int cat(u16 v) { int k = 8; while (k > 0 && v < CATB[k]) k--; return k; } // 0 high .. 8 straight flush
}  // namespace pe
