// check.cpp - driver for engine/check.py: proves poker_engine.hpp against sim/poker_sim.py.
//
//   check gen <games> <seed0> <out.log>   random-action games (legal, illegal and odd outputs, rare
//                                         timeouts); writes every raw output and the engine's results
//   check run <in.log> <out.log>          replays recorded outputs (from check.py's replay dump or a
//                                         gen log) and writes the engine's results
//
// Log format, one game per block:
//   G <id> <n> <seed>
//   A <pid> <raw output, escaped: \\ \t \n \r>      or    A <pid> \0   (timeout)
//   S <turn> <hand> <pid> <shown action>            (engine output: post-replacement action)
//   R <hands> <rounds> <cancelled> <score0> ... <scoreN-1>
//   X <message>                                     (engine raised: NONE round at 601)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "poker_engine.hpp"

static std::string esc(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '\\') o += "\\\\"; else if (c == '\t') o += "\\t"; else if (c == '\n') o += "\\n";
    else if (c == '\r') o += "\\r"; else o += c;
  }
  return o;
}
static std::string unesc(const std::string& s) {
  std::string o;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      char n = s[++i];
      o += n == 't' ? '\t' : n == 'n' ? '\n' : n == 'r' ? '\r' : n;
    } else o += s[i];
  }
  return o;
}

struct Rng {                                  // splitmix64
  uint64_t x;
  uint64_t next() { uint64_t z = (x += 0x9E3779B97F4A7C15ull); z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; return z ^ (z >> 31); }
  int below(int n) { return (int)(next() % (uint64_t)n); }
  double uni() { return (next() >> 11) * (1.0 / 9007199254740992.0); }
};

// Random bot: a style per game (0 wild, 1 passive, 2 aggressive, 3 tight, 4 calling station, 5 limper)
// so that short games, long games, the 600-round cap, all-in runouts and the raise cap are all reached.
struct RandomAgent : pk::Agent {
  Rng rng; int style; std::ofstream* log; int pid;
  bool act(const pk::Obs& o, std::string& out) override {
    double r = rng.uni();
    int stack = o.stacks[o.player_id];
    int min_bet = 0;
    for (auto& a : o.possible) if (a.rfind("BET_", 0) == 0) min_bet = atoi(a.c_str() + 4);
    bool timeout = false;
    if (style >= 4) r = 1;                                   // stations and limpers: no odd outputs or timeouts
    if (r < 0.002) timeout = true;
    else if (r < 0.03) {                                   // odd / illegal formats
      static const char* odd[] = {"bet 50", " call ", "BET_30", "BET  30", "RAISE 20", "CALL 10", "ALL_IN", "FOLD;msg", "CHECK;x;y",
                                  "BET 0", "BET -5", "BET 99999999999", "BET +25", "BET", "", "TOTO", "all-in", "BET 2x", "BET 007", "check "};
      out = odd[rng.below(sizeof odd / sizeof *odd)];
    } else {
      double pc, pk_, pf, pa;                              // CALL, CHECK, FOLD, ALL-IN; rest BET
      if (style == 0) pc = .30, pk_ = .15, pf = .12, pa = .10;
      else if (style == 1) pc = .55, pk_ = .30, pf = .05, pa = .02;
      else if (style == 2) pc = .15, pk_ = .05, pf = .10, pa = .25;
      else if (style == 3) pc = .20, pk_ = .25, pf = .40, pa = .03;
      else if (style == 4) pc = .70, pk_ = .30, pf = 0, pa = 0;         // calling station: never folds or bets
      else pc = .60, pk_ = .36, pf = .02, pa = 0;                        // limper: tiny bets, rare folds
      double u = rng.uni();
      if (u < pc) out = "CALL";
      else if (u < pc + pk_) out = "CHECK";
      else if (u < pc + pk_ + pf) out = "FOLD";
      else if (u < pc + pk_ + pf + pa) out = "ALL-IN";
      else {
        int v = rng.below(5);
        int amt = v == 0 ? min_bet : v == 1 ? rng.below(std::max(1, stack) + 10) : v == 2 ? min_bet + rng.below(50)
                : v == 3 ? std::max(1, stack * (1 + rng.below(3)) / 3) : rng.below(30);
        out = "BET " + std::to_string(amt);
        if (rng.below(10) == 0) out += ";hello";
      }
    }
    if (timeout) *log << "A " << pid << " \\0\n";
    else *log << "A " << pid << " " << esc(out) << "\n";
    return !timeout;
  }
};

// Replays recorded outputs; checks that the engine asks the recorded player each time.
struct ReplayAgent : pk::Agent {
  std::vector<std::pair<int, std::string>>* q; size_t* pos; int pid; bool* order_ok; bool* has_timeout;
  bool act(const pk::Obs& o, std::string& out) override {
    if (*pos >= q->size()) { *order_ok = false; out = "FOLD"; return true; }
    auto& e = (*q)[(*pos)++];
    if (e.first != pid || o.player_id != pid) *order_ok = false;
    if (e.second == "\\0") return false;
    out = unesc(e.second);
    return true;
  }
};

static void write_result(std::ofstream& out, pk::Engine& eng, const pk::Engine::Result& r) {
  for (auto& l : eng.log) out << "S " << l.turn << " " << l.hand << " " << l.pid << " " << l.shown << "\n";
  out << "R " << r.hands << " " << r.rounds << " " << (r.cancelled ? 1 : 0);
  for (int s : r.scores) out << " " << s;
  out << "\n";
}

int main(int argc, char** argv) {
  if (argc < 2) { fprintf(stderr, "usage: check gen <games> <seed0> <out> | check run <in> <out>\n"); return 2; }
  std::string mode = argv[1];
  if (mode == "gen") {
    int games = atoi(argv[2]);
    uint64_t seed0 = strtoull(argv[3], nullptr, 10);
    std::ofstream out(argv[4]);
    Rng meta{seed0};
    for (int g = 0; g < games; g++) {
      int n = 2 + meta.below(3);
      int64_t seed = (int64_t)meta.next();
      int style = meta.below(6);
      out << "G " << g << " " << n << " " << seed << "\n";
      std::vector<RandomAgent> bots(n);
      std::vector<pk::Agent*> ag;
      for (int i = 0; i < n; i++) {
        bots[i].rng = Rng{meta.next()}; bots[i].style = style == 0 ? meta.below(6) : style; bots[i].log = &out; bots[i].pid = i;
        ag.push_back(&bots[i]);
      }
      pk::Engine eng(n, seed);
      try {
        auto r = eng.run(ag);
        write_result(out, eng, r);
      } catch (std::exception& e) {
        for (auto& l : eng.log) out << "S " << l.turn << " " << l.hand << " " << l.pid << " " << l.shown << "\n";
        out << "X " << e.what() << "\n";
      }
    }
    return 0;
  }
  if (mode == "run") {
    std::ifstream in(argv[2]);
    std::ofstream out(argv[3]);
    std::string line;
    struct Game { std::string id; int n; int64_t seed; std::vector<std::pair<int, std::string>> acts; };
    std::vector<Game> games;
    while (std::getline(in, line)) {
      if (line.rfind("G ", 0) == 0) {
        std::istringstream is(line.substr(2));
        Game g; is >> g.id >> g.n >> g.seed; games.push_back(g);
      } else if (line.rfind("A ", 0) == 0) {
        size_t sp = line.find(' ', 2);
        int pid = atoi(line.substr(2, sp - 2).c_str());
        games.back().acts.push_back({pid, sp == std::string::npos ? "" : line.substr(sp + 1)});
      }
    }
    for (auto& g : games) {
      out << "G " << g.id << " " << g.n << " " << g.seed << "\n";
      size_t pos = 0; bool order_ok = true, ht = false;
      std::vector<ReplayAgent> bots(g.n);
      std::vector<pk::Agent*> ag;
      for (int i = 0; i < g.n; i++) {
        bots[i].q = &g.acts; bots[i].pos = &pos; bots[i].pid = i; bots[i].order_ok = &order_ok; bots[i].has_timeout = &ht;
        ag.push_back(&bots[i]);
      }
      pk::Engine eng(g.n, g.seed);
      try {
        auto r = eng.run(ag);
        write_result(out, eng, r);
      } catch (std::exception& e) {
        for (auto& l : eng.log) out << "S " << l.turn << " " << l.hand << " " << l.pid << " " << l.shown << "\n";
        out << "X " << e.what() << "\n";
      }
      out << "O " << (order_ok && pos == g.acts.size() ? 1 : 0) << " " << pos << " " << g.acts.size() << "\n";
    }
    return 0;
  }
  fprintf(stderr, "unknown mode\n");
  return 2;
}
