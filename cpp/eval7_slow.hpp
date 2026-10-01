#pragma once
// eval7_slow.hpp - table-free 7-card evaluator (no initialisation at all).  Returns a value with the
// same ordering as poker_sim.eval5 / engine best7: category << 20, then the deciding ranks in 4-bit
// fields (ranks 2..14).  Roughly 10x slower than pe7c, but needs no start-up: the bot uses it until
// the pe7c tables are built in the background.  Cards are 4*rank + suit as everywhere else.
#include <cstdint>

namespace e7 {

__attribute__((always_inline)) inline int straight_top(uint32_t m) {   // m: 13-bit rank mask (bit 0 = deuce)
  uint32_t w = m << 1 | (m >> 12 & 1);                                  // ace also plays low
  uint32_t s = w & w >> 1 & w >> 2 & w >> 3 & w >> 4;                   // bit i set: straight topped by rank bit i (in w)
  if (!s) return 0;
  return 31 - __builtin_clz(s) + 1;                                     // 2..14 as rank value (w bit i = rank i+1)
}
__attribute__((always_inline)) inline uint32_t top_bits(uint32_t m, int n) {  // the n highest set bits, packed as ranks
  uint32_t v = 0;
  for (int i = 0; i < n; i++) { int b = 31 - __builtin_clz(m); v = v << 4 | (b + 2); m &= ~(1u << b); }
  return v;
}

inline uint32_t ev7(const int* c) {
  uint32_t sm[4] = {0, 0, 0, 0}; int cnt[13] = {0};
  for (int i = 0; i < 7; i++) sm[c[i] & 3] |= 1u << (c[i] >> 2), cnt[c[i] >> 2]++;
  uint32_t all = sm[0] | sm[1] | sm[2] | sm[3];
  for (int s = 0; s < 4; s++)
    if (__builtin_popcount(sm[s]) >= 5) {                               // flush (only one suit can have 5 of 7)
      int st = straight_top(sm[s]);
      if (st) return 8u << 20 | (uint32_t)st << 16;
      return 5u << 20 | top_bits(sm[s], 5);
    }
  uint32_t quad = 0, trips = 0, pairs = 0;
  for (int r = 0; r < 13; r++) {
    if (cnt[r] == 4) quad |= 1u << r; else if (cnt[r] == 3) trips |= 1u << r; else if (cnt[r] == 2) pairs |= 1u << r;
  }
  if (quad) { int q = 31 - __builtin_clz(quad); return 7u << 20 | (uint32_t)(q + 2) << 16 | top_bits(all & ~(1u << q), 1) << 12; }
  if (trips && (pairs || __builtin_popcount(trips) > 1)) {
    int t = 31 - __builtin_clz(trips);
    uint32_t rest = (trips & ~(1u << t)) | pairs;
    return 6u << 20 | (uint32_t)(t + 2) << 16 | top_bits(rest, 1) << 12;
  }
  int st = straight_top(all);
  if (st) return 4u << 20 | (uint32_t)st << 16;
  if (trips) { int t = 31 - __builtin_clz(trips); return 3u << 20 | (uint32_t)(t + 2) << 16 | top_bits(all & ~(1u << t), 2) << 8; }
  if (__builtin_popcount(pairs) >= 2) {
    int p1 = 31 - __builtin_clz(pairs); uint32_t rest = pairs & ~(1u << p1); int p2 = 31 - __builtin_clz(rest);
    return 2u << 20 | (uint32_t)(p1 + 2) << 16 | (uint32_t)(p2 + 2) << 12 | top_bits(all & ~(1u << p1) & ~(1u << p2), 1) << 8;
  }
  if (pairs) { int p = 31 - __builtin_clz(pairs); return 1u << 20 | (uint32_t)(p + 2) << 16 | top_bits(all & ~(1u << p), 3) << 4; }
  return top_bits(all, 5);
}

}  // namespace e7
