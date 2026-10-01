#pragma GCC optimize("O3")
#pragma GCC optimization("unroll-loops")
// main.cpp - CodinGame entry point: stdin -> bot::Bot -> stdout.  Bundle with tools/bundle.py.
//
// Dev builds (DEBUG below) print per-turn diagnostics to stderr; the first turn also prints the
// platform probes the plan's M0 asks for: __cplusplus, evaluator start-up time, Monte Carlo
// trials per second, and (PONDER) whether a background thread makes progress between turns.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>
#pragma GCC target("avx2,bmi,bmi2,lzcnt,popcnt")

#include "bot.hpp"

const bool DEBUG = true;      // stderr diagnostics (stderr is free on CodinGame; stdout is the action)
const bool PONDER = false;    // probe: a spinning thread; log its progress between turns

std::atomic<long> ponder_counter{0};

int main() {
  typedef std::chrono::steady_clock Clock;
  auto ms = [](Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
  std::ios::sync_with_stdio(false);
  auto t_start = Clock::now();
  pe::init();
  auto t_init = Clock::now();
  bot::Bot b;
  bot::Budget budget;
  budget.ms = 15;                                       // placeholder until CodinGame latency is measured (max observed turn 24 ms at 20)
  pk::Obs o;
  bool first = true;
  Clock::time_point t_last_flush = t_init;
  long last_counter = 0;
  if (PONDER) std::thread([] { for (;;) ponder_counter.fetch_add(1, std::memory_order_relaxed); }).detach();
  while (pk::read_obs(std::cin, first, o)) {
    auto t0 = Clock::now();
    if (first && DEBUG) {
      // probes: compile mode, evaluator start-up, Monte Carlo speed over 10 ms
      int hole[2] = {0, 5}, board[3] = {10, 23, 40};
      bot::Budget bench; bench.ms = 10; bench.max_trials = 1 << 30; bench.min_trials = 1;
      auto tb = Clock::now();
      b.equity(hole, board, 3, 1, bench, tb);
      double bms = ms(tb, Clock::now());
      fprintf(stderr, "probe __cplusplus=%ld pe_init_ms=%.1f mc_trials_per_s=%.2fM n=%d id=%d\n",
              (long)__cplusplus, ms(t_start, t_init), b.last_trials / bms / 1000.0, o.player_nb, o.player_id);
    }
    std::string out = b.act(o, budget, t0);
    std::cout << out << "\n" << std::flush;
    auto t1 = Clock::now();
    if (DEBUG) {
      long c = ponder_counter.load(std::memory_order_relaxed);
      fprintf(stderr, "r%d h%d turn_ms=%.2f since_last_flush_ms=%.0f trials=%d eq=%.3f %s%s%s\n", o.round, o.hand_nb, ms(t0, t1),
              ms(t_last_flush, t0), b.last_trials, b.last_equity, b.last_tag.c_str(),
              b.tr.desynced ? " DESYNC:" : "", b.tr.desynced ? b.tr.last_error.c_str() : "");
      if (PONDER) fprintf(stderr, "ponder_delta=%ld\n", c - last_counter);
      last_counter = c;
    }
    t_last_flush = t1;
    first = false;
  }
  return 0;
}
