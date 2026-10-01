// test_eval7_slow.cpp - eval7_slow.hpp must order hands exactly like pe7c and like the engine's best7.
// Random 7-card hands: every pair's comparison must agree across the three evaluators.
#pragma GCC optimize("O3")
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <chrono>
#include "pe7c.hpp"
#include "eval7_slow.hpp"
#include "../engine/poker_engine.hpp"

static uint64_t x = 0x1234567;
static uint64_t rnd() { uint64_t z = (x += 0x9E3779B97F4A7C15ull); z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; return z ^ (z >> 31); }

int main(int argc, char** argv) {
  int n = argc > 1 ? atoi(argv[1]) : 300000;
  pe::init();
  int bad = 0;
  std::vector<int> b(5);
  for (int t = 0; t < n; t++) {
    int deck[52]; for (int i = 0; i < 52; i++) deck[i] = i;
    for (int i = 0; i < 14; i++) { int j = i + rnd() % (52 - i); int tmp = deck[i]; deck[i] = deck[j]; deck[j] = tmp; }
    const int* h1 = deck; const int* h2 = deck + 7;
    pe::H a = pe::E, c = pe::E;
    for (int i = 0; i < 7; i++) a = pe::add(a, h1[i]), c = pe::add(c, h2[i]);
    int cmp_pe = (pe::ev(a) > pe::ev(c)) - (pe::ev(a) < pe::ev(c));
    uint32_t s1 = e7::ev7(h1), s2 = e7::ev7(h2);
    int cmp_slow = (s1 > s2) - (s1 < s2);
    for (int i = 0; i < 5; i++) b[i] = h1[i + 2];
    uint32_t e1 = pk::best7(h1, b);
    for (int i = 0; i < 5; i++) b[i] = h2[i + 2];
    uint32_t e2 = pk::best7(h2, b);
    int cmp_eng = (e1 > e2) - (e1 < e2);
    if (cmp_pe != cmp_slow || cmp_pe != cmp_eng) {
      if (bad < 10) printf("DISAGREE pe %d slow %d engine %d  (slow %08x vs %08x)\n", cmp_pe, cmp_slow, cmp_eng, s1, s2);
      bad++;
    }
  }
  // speed
  auto t0 = std::chrono::steady_clock::now();
  uint64_t acc = 0; int deck[52]; for (int i = 0; i < 52; i++) deck[i] = i;
  for (int t = 0; t < 2000000; t++) {
    for (int i = 0; i < 7; i++) { int j = i + rnd() % (52 - i); int tmp = deck[i]; deck[i] = deck[j]; deck[j] = tmp; }
    acc += e7::ev7(deck);
  }
  double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  printf("%s: %d random pairs, %d disagreements; eval7_slow %.1f M evals/s (incl. dealing) [%llu]\n", bad ? "FAIL" : "PASS", n, bad, 2.0 / s, (unsigned long long)(acc & 1));
  return bad ? 1 : 0;
}
