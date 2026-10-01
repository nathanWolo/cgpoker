#pragma once
// hu.hpp - heads-up no-limit hold'em at 8-30 BB: an abstract game solved by external-sampling MCCFR.
//
// Game.  Both players start the hand with S BB; SB (button, player 0) posts 0.5, BB (player 1) posts 1.
//   Preflop, SB first:  F  C (limp)  R (raise to 2.5)  A (all-in)
//     BB after a limp:  K  R (raise to 3)  A
//     facing a raise:   F  C  R (3-bet to 3x, only the BB over the SB's open)  A
//     facing all-in:    F  C
//   Flop / turn / river, BB first:  K  B (bet half the pot)  A;  facing a bet:  F  C  A (raise all-in)
//   A bet or raise that would put a player all-in is the all-in action.  Showdown pays min(commitments).
// Cards.  Preflop the 169 classes; postflop NB buckets per street by quantiles of EHS, the hand's
//   expected showdown equity against a random hand (Monte Carlo runouts; exact on the river).
// Chance is a pool of sampled deals (both hands, the board, the buckets, the showdown result) shared
//   by every stack point; the solver samples from it, so the solution is for the pool-sampled game.
// Solver.  External-sampling MCCFR with regret clipping at 0 and linear averaging.  Exploitability is
//   the best response of each player to the other's average strategy, computed over a sub-pool.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "../../cpp/pe7c.hpp"

namespace hu {
constexpr int NB = 10, NSTREET = 4, MAXA = 4;
enum Act { F = 0, C = 1, R = 2, A = 3, K = 4, B = 5 };
inline const char* act_name(int a) { static const char* n[] = {"F", "C", "R", "A", "K", "B"}; return n[a]; }
inline int combos_of(int c) { int r1 = c / 13, r2 = c % 13; return r1 == r2 ? 6 : r1 > r2 ? 4 : 12; }
inline std::string class_name(int c) {
  const char* R_ = "23456789TJQKA"; int r1 = c / 13, r2 = c % 13; std::string s;
  s += R_[std::max(r1, r2)]; s += R_[std::min(r1, r2)]; if (r1 != r2) s += r1 > r2 ? 's' : 'o'; return s;
}
inline int hand_class(int c0, int c1) {
  int r0 = c0 >> 2, r1 = c1 >> 2, hi = std::max(r0, r1), lo = std::min(r0, r1);
  if (r0 == r1) return r0 * 13 + r0;
  return (c0 & 3) == (c1 & 3) ? hi * 13 + lo : lo * 13 + hi;
}
struct Rng { uint64_t x; uint64_t next() { uint64_t z = (x += 0x9E3779B97F4A7C15ull); z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; return z ^ (z >> 31); }
  int below(int n) { return (int)((next() >> 32) * (uint64_t)n >> 32); } double uni() { return (next() >> 11) * (1.0 / 9007199254740992.0); } };

// ---------------------------------------------------------------- the deal pool
struct Deal { uint8_t cls[2]; uint8_t bkt[2][3]; int8_t result; };   // result: +1 SB wins, -1 BB wins, 0 tie
struct Pool {
  std::vector<Deal> d; float thr[3][NB - 1];   // EHS thresholds per postflop street
  // expected showdown equity of hand h on board bd (k cards) against a random hand, by runouts (exact on the river)
  static double ehs(const int* h, const int* bd, int k, Rng& rng, int runouts) {
    uint64_t used = 1ull << h[0] | 1ull << h[1]; for (int i = 0; i < k; i++) used |= 1ull << bd[i];
    int deck[52], nd = 0; for (int c = 0; c < 52; c++) if (!(used >> c & 1)) deck[nd++] = c;
    pe::H base = pe::add(pe::add(pe::E, h[0]), h[1]); pe::H bb = pe::E; for (int i = 0; i < k; i++) bb = pe::add(bb, bd[i]);
    double s = 0; int n = 0;
    if (k == 5) {
      uint16_t mine = pe::ev(pe::add(base, bb));
      for (int i = 0; i < nd; i++) for (int j = i + 1; j < nd; j++) { uint16_t o = pe::ev(pe::add(pe::add(bb, deck[i]), deck[j])); s += mine > o ? 1 : mine == o ? 0.5 : 0; n++; }
      return s / n;
    }
    for (int r = 0; r < runouts; r++) {
      int need = 5 - k + 2;
      for (int i = 0; i < need; i++) { int j = i + rng.below(nd - i); std::swap(deck[i], deck[j]); }
      pe::H b2 = bb; for (int i = 0; i < 5 - k; i++) b2 = pe::add(b2, deck[i]);
      uint16_t mine = pe::ev(pe::add(b2, pe::add(pe::add(pe::E, h[0]), h[1])));
      uint16_t o = pe::ev(pe::add(b2, pe::add(pe::add(pe::E, deck[5 - k]), deck[6 - k])));
      s += mine > o ? 1 : mine == o ? 0.5 : 0; n++;
    }
    return s / n;
  }
  void generate(int n, uint64_t seed, int runouts = 100) {
    d.resize(n); std::vector<float> e((size_t)n * 6);
    Rng rng{seed};
    for (int i = 0; i < n; i++) {
      int deck[52]; for (int c = 0; c < 52; c++) deck[c] = c;
      for (int k = 0; k < 9; k++) { int j = k + rng.below(52 - k); std::swap(deck[k], deck[j]); }
      int h0[2] = {deck[0], deck[1]}, h1[2] = {deck[2], deck[3]}; const int* bd = deck + 4;
      d[i].cls[0] = hand_class(h0[0], h0[1]); d[i].cls[1] = hand_class(h1[0], h1[1]);
      for (int s = 0; s < 3; s++) { e[(size_t)i * 6 + s] = ehs(h0, bd, 3 + s, rng, runouts); e[(size_t)i * 6 + 3 + s] = ehs(h1, bd, 3 + s, rng, runouts); }
      pe::H b = pe::E; for (int k = 0; k < 5; k++) b = pe::add(b, bd[k]);
      uint16_t v0 = pe::ev(pe::add(pe::add(b, h0[0]), h0[1])), v1 = pe::ev(pe::add(pe::add(b, h1[0]), h1[1]));
      d[i].result = v0 > v1 ? 1 : v0 < v1 ? -1 : 0;
    }
    for (int s = 0; s < 3; s++) {
      std::vector<float> v; v.reserve((size_t)n * 2);
      for (int i = 0; i < n; i++) { v.push_back(e[(size_t)i * 6 + s]); v.push_back(e[(size_t)i * 6 + 3 + s]); }
      std::sort(v.begin(), v.end());
      for (int b = 1; b < NB; b++) thr[s][b - 1] = v[(size_t)v.size() * b / NB];
      for (int i = 0; i < n; i++) for (int p = 0; p < 2; p++) d[i].bkt[p][s] = bucket(s, e[(size_t)i * 6 + 3 * p + s]);
    }
  }
  int bucket(int s, double v) const { int b = 0; while (b < NB - 1 && v >= thr[s][b]) b++; return b; }
  bool save(const std::string& path) const {
    FILE* f = fopen(path.c_str(), "wb"); if (!f) return false;
    int n = d.size(); fwrite("HUP1", 1, 4, f); fwrite(&n, 4, 1, f); fwrite(thr, sizeof thr, 1, f); fwrite(d.data(), sizeof(Deal), n, f); fclose(f); return true;
  }
  bool load(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb"); if (!f) return false;
    char m[4]; int n; if (fread(m, 1, 4, f) != 4 || memcmp(m, "HUP1", 4) || fread(&n, 4, 1, f) != 1) { fclose(f); return false; }
    if (fread(thr, sizeof thr, 1, f) != 1) { fclose(f); return false; }
    d.resize(n); size_t got = fread(d.data(), sizeof(Deal), n, f); fclose(f); return got == (size_t)n;
  }
};

// ---------------------------------------------------------------- the tree
struct Node {
  int player = -1, street = 0, nact = 0, act[MAXA] = {0, 0, 0, 0}, child[MAXA] = {0, 0, 0, 0};
  double c[2] = {0, 0};              // chips committed by each player so far
  int term = 0;                      // 0 decision, 1 player 0 folded, 2 player 1 folded, 3 showdown
  int base = 0, nb = 0;              // info sets [base, base + nb)
  int depth = 0;
  std::string hist;
};
struct Tree {
  double S; std::vector<Node> nodes; int ninfo = 0;
  explicit Tree(double stack) : S(stack) { build(0, 0.5, 1.0, 0, "", 0, false, 0); }
  double pot(const Node& n) const { return n.c[0] + n.c[1]; }
  // returns node index
  int build(int street, double c0, double c1, int toact, std::string hist, int raises, bool checked, int depth) {
    int id = nodes.size(); nodes.push_back(Node());
    Node n; n.street = street; n.c[0] = c0; n.c[1] = c1; n.hist = hist; n.depth = depth; n.player = toact;
    n.nb = street == 0 ? 169 : NB; n.base = ninfo; ninfo += n.nb;
    double c[2] = {c0, c1}; int other = 1 - toact;
    bool facing = c[toact] < c[other] - 1e-9;
    bool opp_allin = c[other] >= S - 1e-9;
    std::vector<std::pair<int, double>> acts;   // action, new commitment of toact
    auto add_raise_to = [&](int a, double target) { if (target >= S - 1e-9) { if (!opp_allin) acts.push_back({A, S}); } else acts.push_back({a, target}); };
    if (street == 0 && hist.empty()) {                                                  // SB first in (the blind is not a bet to it)
      acts.push_back({F, c0}); acts.push_back({C, 1.0}); add_raise_to(R, 2.5); if (acts.back().first != A) acts.push_back({A, S});
    } else if (facing) {
      acts.push_back({F, c[toact]}); acts.push_back({C, c[other]});
      if (!opp_allin) {
        if (street == 0 && hist == "R") add_raise_to(R, 3 * c[other]);                 // the BB's 3-bet
        if (acts.back().first != A) acts.push_back({A, S});
      }
    } else if (street == 0 && hist == "C") {                                            // BB after a limp
      acts.push_back({K, c1}); add_raise_to(R, 3.0); if (acts.back().first != A) acts.push_back({A, S});
    } else {                                                                            // postflop, not facing
      acts.push_back({K, c[toact]}); add_raise_to(B, c[toact] + 0.5 * (c0 + c1)); if (acts.back().first != A) acts.push_back({A, S});
    }
    // dedupe A (two paths could add it)
    for (size_t i = 1; i < acts.size(); i++) if (acts[i].first == A && acts[i - 1].first == A) { acts.erase(acts.begin() + i); break; }
    n.nact = acts.size();
    nodes[id] = n;
    for (int k = 0; k < n.nact; k++) {
      int a = acts[k].first; double nc[2] = {c0, c1}; nc[toact] = acts[k].second;
      std::string h2 = hist + act_name(a);
      int ch;
      if (a == F) ch = terminal(street, nc, toact == 0 ? 1 : 2, depth + 1, h2);
      else if (a == C && !(street == 0 && hist.empty())) {                              // a call closes the street
        bool allin = nc[0] >= S - 1e-9 && nc[1] >= S - 1e-9;
        if (allin || street == 3) ch = terminal(street, nc, 3, depth + 1, h2);
        else ch = build(street + 1, nc[0], nc[1], 1, h2 + "/", 0, false, depth + 1);
      } else if (a == K) {
        if (checked || (street == 0)) {                                                 // second check (or the BB's option) closes the street
          if (street == 3) ch = terminal(street, nc, 3, depth + 1, h2);
          else ch = build(street + 1, nc[0], nc[1], 1, h2 + "/", 0, false, depth + 1);
        } else ch = build(street, nc[0], nc[1], other, h2, raises, true, depth + 1);
      } else ch = build(street, nc[0], nc[1], other, h2, raises + (a == R || a == B || a == A), false, depth + 1);
      nodes[id].act[k] = a; nodes[id].child[k] = ch;
    }
    return id;
  }
  int terminal(int street, const double* c, int type, int depth, const std::string& hist) {
    int id = nodes.size(); Node n; n.street = street; n.c[0] = c[0]; n.c[1] = c[1]; n.term = type; n.depth = depth; n.hist = hist; n.player = -1; nodes.push_back(n); return id;
  }
  // payoff for player 0 at a terminal node
  double payoff(const Node& n, int result) const {
    if (n.term == 1) return -n.c[0];
    if (n.term == 2) return n.c[1];
    return result * std::min(n.c[0], n.c[1]);
  }
  int bucket_of(const Node& n, const Deal& d, int p) const { return n.street == 0 ? d.cls[p] : d.bkt[p][n.street - 1]; }
};

// ---------------------------------------------------------------- the solver
struct Solver {
  const Pool& pool; Tree tree; Rng rng;
  std::vector<double> regret, ssum;      // [info][MAXA]
  long iters = 0;
  Solver(const Pool& p, double S, uint64_t seed = 1) : pool(p), tree(S), rng{seed} { regret.assign((size_t)tree.ninfo * MAXA, 0); ssum.assign((size_t)tree.ninfo * MAXA, 0); }
  void strategy(int I, int nact, double* s) const {
    double tot = 0; for (int a = 0; a < nact; a++) { s[a] = std::max(0.0, regret[(size_t)I * MAXA + a]); tot += s[a]; }
    if (tot <= 0) for (int a = 0; a < nact; a++) s[a] = 1.0 / nact; else for (int a = 0; a < nact; a++) s[a] /= tot;
  }
  void avg(int I, int nact, double* s) const {
    double tot = 0; for (int a = 0; a < nact; a++) { s[a] = ssum[(size_t)I * MAXA + a]; tot += s[a]; }
    if (tot <= 0) for (int a = 0; a < nact; a++) s[a] = 1.0 / nact; else for (int a = 0; a < nact; a++) s[a] /= tot;
  }
  double walk(int id, const Deal& d, int p, double w) {
    const Node& n = tree.nodes[id];
    if (n.term) { double u = tree.payoff(n, d.result); return p == 0 ? u : -u; }
    int I = n.base + tree.bucket_of(n, d, n.player);
    double s[MAXA]; strategy(I, n.nact, s);
    if (n.player == p) {
      double v[MAXA], vs = 0;
      for (int a = 0; a < n.nact; a++) { v[a] = walk(n.child[a], d, p, w); vs += s[a] * v[a]; }
      for (int a = 0; a < n.nact; a++) { double& r = regret[(size_t)I * MAXA + a]; r = std::max(0.0, r + v[a] - vs); }
      return vs;
    }
    for (int a = 0; a < n.nact; a++) ssum[(size_t)I * MAXA + a] += w * s[a];
    double u = rng.uni(), acc = 0; int pick = n.nact - 1;
    for (int a = 0; a < n.nact; a++) { acc += s[a]; if (u < acc) { pick = a; break; } }
    return walk(n.child[pick], d, p, w);
  }
  void run(long n) {
    for (long t = 0; t < n; t++) {
      const Deal& d = pool.d[rng.below(pool.d.size())];
      double w = (double)(iters + 1);                     // linear averaging
      walk(0, d, t & 1, w);
      iters++;
    }
  }
  // ---- best response of player p to the other's average strategy, fitted on the first m deals and valued on the
  // next m (out of sample: a response fitted and valued on the same deals overstates the gain); returns p's value
  double best_response(int p, int m) const {
    m = std::min<int>(m, pool.d.size() / 2);
    std::vector<double> q((size_t)tree.ninfo * MAXA, 0);   // action values aggregated over deals
    std::vector<int> br(tree.ninfo, -1);
    // nodes of p by decreasing depth
    std::vector<int> order; for (int i = 0; i < (int)tree.nodes.size(); i++) if (tree.nodes[i].player == p) order.push_back(i);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return tree.nodes[a].depth > tree.nodes[b].depth; });
    int cur = 0;
    while (cur < (int)order.size()) {
      int depth = tree.nodes[order[cur]].depth; int end = cur; while (end < (int)order.size() && tree.nodes[order[end]].depth == depth) end++;
      for (int di = 0; di < m; di++) {
        const Deal& d = pool.d[di];
        for (int k = cur; k < end; k++) {
          int id = order[k]; const Node& n = tree.nodes[id];
          double reach = reach_opp(id, d, p); if (reach == 0) continue;
          int I = n.base + tree.bucket_of(n, d, p);
          for (int a = 0; a < n.nact; a++) q[(size_t)I * MAXA + a] += reach * value(n.child[a], d, p, br);
        }
      }
      for (int k = cur; k < end; k++) { const Node& n = tree.nodes[order[k]];
        for (int b = 0; b < n.nb; b++) { int I = n.base + b; int best = 0; for (int a = 1; a < n.nact; a++) if (q[(size_t)I * MAXA + a] > q[(size_t)I * MAXA + best]) best = a; br[I] = best; } }
      cur = end;
    }
    double v = 0, vin = 0;
    for (int di = m; di < 2 * m; di++) v += value(0, pool.d[di], p, br);
    for (int di = 0; di < m; di++) vin += value(0, pool.d[di], p, br);
    last_br_in = vin / m;
    return v / m;
  }
  mutable double last_br_in = 0;   // the same response valued on the deals it was fitted on (an upper estimate)
  // product of the opponent's average-strategy probabilities on the path to node id (p's own actions count 1)
  double reach_opp(int id, const Deal& d, int p) const {
    double r = 1; int cur = 0;
    const std::string& h = tree.nodes[id].hist;
    // follow the path by matching the history string prefix
    while (cur != id) {
      const Node& n = tree.nodes[cur];
      int next = -1, ak = -1;
      for (int a = 0; a < n.nact; a++) { const std::string& ch = tree.nodes[n.child[a]].hist; if (h.compare(0, ch.size(), ch) == 0 && ch.size() <= h.size()) { next = n.child[a]; ak = a; break; } }
      if (next < 0) return 0;
      if (n.player != p) { double s[MAXA]; avg(n.base + tree.bucket_of(n, d, n.player), n.nact, s); r *= s[ak]; if (r == 0) return 0; }
      cur = next;
    }
    return r;
  }
  // expected value for p of the subtree at id, with p playing br (fixed where set) and the other the average strategy
  double value(int id, const Deal& d, int p, const std::vector<int>& br) const {
    const Node& n = tree.nodes[id];
    if (n.term) { double u = tree.payoff(n, d.result); return p == 0 ? u : -u; }
    int I = n.base + tree.bucket_of(n, d, n.player);
    if (n.player == p) { int a = br[I] < 0 ? 0 : br[I]; return value(n.child[a], d, p, br); }
    double s[MAXA]; avg(I, n.nact, s); double v = 0;
    for (int a = 0; a < n.nact; a++) if (s[a] > 0) v += s[a] * value(n.child[a], d, p, br);
    return v;
  }
  // game value for player 0 under the average strategies, over the first m deals
  double game_value(int m) const { std::vector<int> none; m = std::min<int>(m, pool.d.size()); double v = 0; for (int i = 0; i < m; i++) v += value_avg(0, pool.d[i]); return v / m; }
  double value_avg(int id, const Deal& d) const {
    const Node& n = tree.nodes[id]; if (n.term) return tree.payoff(n, d.result);
    double s[MAXA]; avg(n.base + tree.bucket_of(n, d, n.player), n.nact, s); double v = 0;
    for (int a = 0; a < n.nact; a++) if (s[a] > 0) v += s[a] * value_avg(n.child[a], d); return v;
  }
  // ---- reports
  void print_preflop(FILE* f = stdout) const {
    for (int id = 0; id < (int)tree.nodes.size(); id++) {
      const Node& n = tree.nodes[id]; if (n.term || n.street != 0) continue;
      double freq[MAXA] = {0, 0, 0, 0};
      for (int c = 0; c < 169; c++) { double s[MAXA]; avg(n.base + c, n.nact, s); for (int a = 0; a < n.nact; a++) freq[a] += s[a] * combos_of(c) / 1326.0; }
      fprintf(f, "  %-8s %s:", n.hist.empty() ? "(root)" : n.hist.c_str(), n.player == 0 ? "SB" : "BB");
      for (int a = 0; a < n.nact; a++) fprintf(f, "  %s %5.1f%%", act_name(n.act[a]), 100 * freq[a]);
      fprintf(f, "\n");
    }
  }
};

}  // namespace hu
