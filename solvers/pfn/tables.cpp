// tables.cpp - the lookup tables the N-player push/fold solver needs, cached under data/cache/pfn/:
//   w2.bin    169x169 u16: number of non-conflicting combo pairs (class a, class b)
//   w3.bin    169^3  u16: number of non-conflicting combo triples (a, b, c), exact
//   eq3.bin   169^3  float x3: Monte Carlo 3-way showdown shares (a, b, c) -> share of a, b, c
//             (ties split), TRIALS per unordered triple, stored for every ordering
//   eq4b.bin  B^4 float x4: 4-way shares between strength buckets (B = 20 buckets of the 169 classes
//             by equity vs random, pf_rank order), for the rare four-way all-ins
// Class index r1*13+r2 as in solvers/eq.c: pair r1==r2, suited r1>r2, offsuit r1<r2.
//   tables [trials=2000] [out_dir=data/cache/pfn]
#pragma GCC optimize("O3")
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>
#include "../../cpp/pe7c.hpp"
#include "../../bot/pf_rank.hpp"

static int NC[169]; static int CB[169][12][2];      // combos per class
static void build_combos() {
  for (int c = 0; c < 169; c++) {
    int r1 = c / 13, r2 = c % 13, k = 0;
    if (r1 == r2) { for (int a = 0; a < 4; a++) for (int b = a + 1; b < 4; b++) CB[c][k][0] = r1 * 4 + a, CB[c][k][1] = r1 * 4 + b, k++; }
    else if (r1 > r2) { for (int s = 0; s < 4; s++) CB[c][k][0] = r1 * 4 + s, CB[c][k][1] = r2 * 4 + s, k++; }
    else { for (int a = 0; a < 4; a++) for (int b = 0; b < 4; b++) if (a != b) CB[c][k][0] = r2 * 4 + a, CB[c][k][1] = r1 * 4 + b, k++; }
    NC[c] = k;
  }
}
struct Rng { uint64_t x; uint64_t next() { uint64_t z = (x += 0x9E3779B97F4A7C15ull); z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; return z ^ (z >> 31); } int below(int n) { return (int)((next() >> 32) * (uint64_t)n >> 32); } };

int main(int argc, char** argv) {
  int trials = argc > 1 ? atoi(argv[1]) : 2000;
  std::string out = argc > 2 ? argv[2] : "data/cache/pfn";
  mkdir(out.c_str(), 0755);
  build_combos();
  pe::init();
  auto t0 = std::chrono::steady_clock::now();
  // ---- w2, w3 exact
  std::vector<uint16_t> w2(169 * 169), w3(169 * 169 * 169);
  for (int a = 0; a < 169; a++) for (int b = 0; b < 169; b++) {
    int n = 0;
    for (int i = 0; i < NC[a]; i++) for (int j = 0; j < NC[b]; j++) {
      uint64_t m = 1ull << CB[a][i][0] | 1ull << CB[a][i][1], mm = 1ull << CB[b][j][0] | 1ull << CB[b][j][1];
      n += !(m & mm);
    }
    w2[a * 169 + b] = n;
  }
  for (int a = 0; a < 169; a++) for (int b = 0; b < 169; b++) {
    std::vector<uint64_t> masks;
    for (int i = 0; i < NC[a]; i++) for (int j = 0; j < NC[b]; j++) {
      uint64_t m = 1ull << CB[a][i][0] | 1ull << CB[a][i][1], mm = 1ull << CB[b][j][0] | 1ull << CB[b][j][1];
      if (!(m & mm)) masks.push_back(m | mm);
    }
    for (int c = 0; c < 169; c++) {
      int n = 0;
      for (uint64_t m : masks) for (int k = 0; k < NC[c]; k++) n += !(m & (1ull << CB[c][k][0] | 1ull << CB[c][k][1]));
      w3[(a * 169 + b) * 169 + c] = n;
    }
  }
  printf("w2/w3 done %.1fs\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
  // ---- eq3 by Monte Carlo over unordered triples, multithreaded
  std::vector<float> eq3(169 * 169 * 169 * 3, 0.f);
  int nthreads = std::max(1u, std::thread::hardware_concurrency());
  std::vector<std::thread> th;
  for (int t = 0; t < nthreads; t++) th.emplace_back([&, t] {
    Rng rng{(uint64_t)t * 7919 + 1};
    for (int a = t; a < 169; a += nthreads) for (int b = a; b < 169; b++) for (int c = b; c < 169; c++) {
      double sh[3] = {0, 0, 0}; int done = 0;
      for (int tr = 0; tr < trials; tr++) {
        int h[3][2]; uint64_t used = 0; bool ok = true;
        int cls[3] = {a, b, c};
        for (int p = 0; p < 3 && ok; p++) {
          int tries = 0;
          for (;;) {
            int k = rng.below(NC[cls[p]]);
            uint64_t m = 1ull << CB[cls[p]][k][0] | 1ull << CB[cls[p]][k][1];
            if (!(m & used)) { used |= m; h[p][0] = CB[cls[p]][k][0]; h[p][1] = CB[cls[p]][k][1]; break; }
            if (++tries > 30) { ok = false; break; }
          }
        }
        if (!ok) continue;
        int deck[52], nd = 0;
        for (int x = 0; x < 52; x++) if (!(used >> x & 1)) deck[nd++] = x;
        for (int i = 0; i < 5; i++) { int j = i + rng.below(nd - i); std::swap(deck[i], deck[j]); }
        pe::H bd = pe::E; for (int i = 0; i < 5; i++) bd = pe::add(bd, deck[i]);
        uint16_t v[3]; uint16_t best = 0;
        for (int p = 0; p < 3; p++) { v[p] = pe::ev(pe::add(pe::add(bd, h[p][0]), h[p][1])); best = std::max(best, v[p]); }
        int nb = 0; for (int p = 0; p < 3; p++) nb += v[p] == best;
        for (int p = 0; p < 3; p++) if (v[p] == best) sh[p] += 1.0 / nb;
        done++;
      }
      if (!done) { sh[0] = sh[1] = sh[2] = 1.0 / 3; done = 1; }   // impossible triple (e.g. three pairs of the same rank)
      float s[3] = {(float)(sh[0] / done), (float)(sh[1] / done), (float)(sh[2] / done)};
      // store all 6 orderings
      int idx[3] = {a, b, c};
      int perm[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
      for (auto& pm : perm) {
        size_t o = ((size_t)idx[pm[0]] * 169 + idx[pm[1]]) * 169 + idx[pm[2]];
        eq3[o * 3 + 0] = s[pm[0]]; eq3[o * 3 + 1] = s[pm[1]]; eq3[o * 3 + 2] = s[pm[2]];
      }
    }
  });
  for (auto& x : th) x.join();
  printf("eq3 done %.1fs\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
  // ---- eq4b: 4-way shares between 20 strength buckets (classes grouped by PF_PCT100)
  const int B = 20;
  std::vector<int> bucket(169);
  for (int c = 0; c < 169; c++) bucket[c] = std::min(B - 1, (int)(pf::PF_PCT100[c] * B / 101));
  std::vector<std::vector<int>> members(B);
  for (int c = 0; c < 169; c++) members[bucket[c]].push_back(c);
  std::vector<float> eq4(B * B * B * B * 4, 0.f);
  th.clear();
  for (int t = 0; t < nthreads; t++) th.emplace_back([&, t] {
    Rng rng{(uint64_t)t * 104729 + 3};
    for (int q = t; q < B * B * B * B; q += nthreads) {
      int bk[4] = {q / (B * B * B), q / (B * B) % B, q / B % B, q % B};
      double sh[4] = {0, 0, 0, 0}; int done = 0;
      for (int tr = 0; tr < trials; tr++) {
        int h[4][2]; uint64_t used = 0; bool ok = true;
        for (int p = 0; p < 4 && ok; p++) {
          int tries = 0;
          for (;;) {
            int cls = members[bk[p]][rng.below((int)members[bk[p]].size())];
            int k = rng.below(NC[cls]);
            uint64_t m = 1ull << CB[cls][k][0] | 1ull << CB[cls][k][1];
            if (!(m & used)) { used |= m; h[p][0] = CB[cls][k][0]; h[p][1] = CB[cls][k][1]; break; }
            if (++tries > 30) { ok = false; break; }
          }
        }
        if (!ok) continue;
        int deck[52], nd = 0;
        for (int x = 0; x < 52; x++) if (!(used >> x & 1)) deck[nd++] = x;
        for (int i = 0; i < 5; i++) { int j = i + rng.below(nd - i); std::swap(deck[i], deck[j]); }
        pe::H bd = pe::E; for (int i = 0; i < 5; i++) bd = pe::add(bd, deck[i]);
        uint16_t v[4]; uint16_t best = 0;
        for (int p = 0; p < 4; p++) { v[p] = pe::ev(pe::add(pe::add(bd, h[p][0]), h[p][1])); best = std::max(best, v[p]); }
        int nb = 0; for (int p = 0; p < 4; p++) nb += v[p] == best;
        for (int p = 0; p < 4; p++) if (v[p] == best) sh[p] += 1.0 / nb;
        done++;
      }
      for (int p = 0; p < 4; p++) eq4[(size_t)q * 4 + p] = done ? (float)(sh[p] / done) : 0.25f;
    }
  });
  for (auto& x : th) x.join();
  printf("eq4b done %.1fs\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
  auto dump = [&](const char* name, const void* p, size_t bytes) {
    FILE* f = fopen((out + "/" + name).c_str(), "wb"); fwrite(p, 1, bytes, f); fclose(f);
  };
  dump("w2.bin", w2.data(), w2.size() * 2); dump("w3.bin", w3.data(), w3.size() * 2);
  dump("eq3.bin", eq3.data(), eq3.size() * 4); dump("eq4b.bin", eq4.data(), eq4.size() * 4);
  std::vector<uint8_t> bk(169); for (int c = 0; c < 169; c++) bk[c] = bucket[c];
  dump("bucket.bin", bk.data(), 169);
  // sanity
  auto cls = [](const char* s) { const char* R = "23456789TJQKA"; int r1 = strchr(R, s[0]) - R, r2 = strchr(R, s[1]) - R; char t = s[2];
    if (r1 == r2) return r1 * 13 + r1; int hi = std::max(r1, r2), lo = std::min(r1, r2); return t == 's' ? hi * 13 + lo : lo * 13 + hi; };
  int aa = cls("AAx"), kk = cls("KKx"), s72 = cls("72o"), ak = cls("AKs");
  size_t o = ((size_t)aa * 169 + kk) * 169 + s72;
  printf("AA vs KK vs 72o: %.3f %.3f %.3f | w3 %d | AA,AKs,KK w3 %d\n", eq3[o * 3], eq3[o * 3 + 1], eq3[o * 3 + 2], w3[o], w3[((size_t)aa * 169 + ak) * 169 + kk]);
  return 0;
}
