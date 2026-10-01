#pragma once
// pfd.hpp - 3-4 player preflop equilibrium at 20-100 BB (chip EV) by vector CFR+ with equity-valued flop leaves.
//
// The game: N players with equal stacks S (BB); every non-BB posts 0.5, the BB 1 (this referee); index 0 acts
// first, index N-1 is the BB.  Actions: fold, call (a limp or a call), raise (open to 2.5 BB, re-raise to 3x the
// bet, at most two raises; a raise of 60% of the stack or more is all-in), all-in; the BB may check its option.
// Leaves: a fold-out pays the pot to the last player; an all-in showdown pays by equity (exact pairwise, bucketed
// 3- and 4-way, the M1 solver's tables); a flop with k players pays pot x share_i x realisation, share_i the
// hand's showdown equity against the others' ranges (exact pairwise, bucketed multiway) pulled toward 1/k by a
// position factor (in position 1.05, out of position 0.93): the standard "equity realisation" leaf model.
// Card removal: pairwise with our own hand.  Solver: CFR+ (regret matching+, alternating, linear averaging) over
// 169-class reach vectors, one traversal per player per iteration.  No convergence guarantee with 3+ players;
// the root frequencies and the average positive regret are reported.
#include "../pfn/pfn.hpp"

namespace pfd {
using pfn::K; using pfn::B;
constexpr int MAXA = 4;

struct Node {
  int player = -1, type = 0;        // type: 0 decision, 1 fold-out, 2 all-in showdown, 3 flop leaf
  double c[4] = {0, 0, 0, 0};
  bool in[4] = {false, false, false, false}, allin[4] = {false, false, false, false};
  int nact = 0; char acts[MAXA] = {0, 0, 0, 0}; int child[MAXA] = {0, 0, 0, 0};
  int base = 0, depth = 0, winner = -1;
  std::string hist;
};
// realisation: a hand realises R(h) x its equity, R rising with the hand's strength percentile (junk folds to bets,
// strong hands get paid): R = r_lo + (r_hi - r_lo) x pct, times r_ip in position / r_oop out of it, times r_multi
// per extra opponent beyond one.  Set from preflop-solver practice, not measured (docs/plan.md M3).
struct Game { int N = 3; double S = 30; double r_ip = 1.05, r_oop = 0.93, r_lo = 0.55, r_hi = 1.15, r_multi = 0.92; };

struct Tree {
  Game g; std::vector<Node> nodes; int ninfo = 0, ndec = 0;
  explicit Tree(const Game& game) : g(game) {
    double c[4] = {0, 0, 0, 0}; bool in[4] = {false, false, false, false}, al[4] = {false, false, false, false};
    for (int i = 0; i < g.N; i++) { c[i] = i == g.N - 1 ? 1.0 : 0.5; in[i] = true; }
    build(c, in, al, 0, 1.0, 0, 0, "", 0);
  }
  int next_actor(const bool* in, const bool* al, int from) const {
    for (int k = 1; k <= g.N; k++) { int j = (from + k) % g.N; if (in[j] && !al[j]) return j; }
    return -1;
  }
  // acted: bitmask of players who have acted since the last raise (the BB's post is not an action)
  int build(const double* c, const bool* in, const bool* al, int toact, double tomatch, int raises, int acted, std::string hist, int depth) {
    int id = nodes.size(); nodes.push_back(Node());
    Node n; n.depth = depth; n.hist = hist; for (int i = 0; i < 4; i++) { n.c[i] = c[i]; n.in[i] = in[i]; n.allin[i] = al[i]; }
    int nin = 0, last = -1; for (int i = 0; i < g.N; i++) if (in[i]) { nin++; last = i; }
    // terminal checks
    if (nin == 1) { n.type = 1; n.winner = last; nodes[id] = n; return id; }
    bool round_over = true;
    for (int i = 0; i < g.N; i++) if (in[i] && !al[i] && (!(acted >> i & 1) || c[i] < tomatch - 1e-9)) round_over = false;
    if (round_over) {                 // equal stacks: a called all-in means everyone left is all-in, so a showdown; else the flop
      int nal = 0; for (int i = 0; i < g.N; i++) nal += in[i] && al[i];
      n.type = nal >= 1 ? 2 : 3;
      nodes[id] = n; return id;
    }
    n.player = toact; n.type = 0; n.base = ninfo; ninfo += K; ndec++;
    bool facing = c[toact] < tomatch - 1e-9;
    std::vector<std::pair<char, double>> acts;
    if (facing) {
      acts.push_back({'F', c[toact]});
      acts.push_back({'C', std::min(tomatch, g.S)});
      if (tomatch < g.S - 1e-9) {
        if (raises < 2) { double t = raises == 0 ? 2.5 : 3 * tomatch; if (t >= 0.6 * g.S) acts.push_back({'A', g.S}); else acts.push_back({'R', t}); }
        if (acts.back().first != 'A') acts.push_back({'A', g.S});
      }
    } else {                                                           // the BB's option (or a limped pot coming back)
      acts.push_back({'K', c[toact]});
      double t = raises == 0 ? 3.0 : 3 * tomatch;
      if (t >= 0.6 * g.S) acts.push_back({'A', g.S}); else { acts.push_back({'R', t}); acts.push_back({'A', g.S}); }
    }
    n.nact = acts.size(); for (int k = 0; k < n.nact; k++) n.acts[k] = acts[k].first;
    nodes[id] = n;
    for (int k = 0; k < n.nact; k++) {
      char a = acts[k].first; double nc[4], nt = tomatch; bool nin_[4], nal[4]; int nr = raises, nacted = acted | (1 << toact);
      for (int i = 0; i < 4; i++) { nc[i] = c[i]; nin_[i] = in[i]; nal[i] = al[i]; }
      if (a == 'F') nin_[toact] = false;
      else if (a == 'C' || a == 'K') { nc[toact] = acts[k].second; if (nc[toact] >= g.S - 1e-9) nal[toact] = true; }
      else { nc[toact] = acts[k].second; nt = nc[toact]; nr++; nacted = 1 << toact; if (nc[toact] >= g.S - 1e-9) nal[toact] = true; }
      int nxt = next_actor(nin_, nal, toact);
      if (nxt < 0) nxt = toact;
      int ch = build(nc, nin_, nal, nxt, nt, nr, nacted, hist + a, depth + 1);
      nodes[id].child[k] = ch;
    }
    return id;
  }
};

struct Solver {
  const pfn::Tables& T; Tree tree; Game g;
  std::vector<double> regret, ssum;    // [info][K? no: info = node.base + class] x MAXA
  int iters = 0;
  double pct[K];                       // strength percentile of each class (equity against a random hand)
  Solver(const pfn::Tables& t, const Game& game) : T(t), tree(game), g(game) {
    regret.assign((size_t)tree.ninfo * MAXA, 0); ssum.assign((size_t)tree.ninfo * MAXA, 0);
    double eq[K]; for (int h = 0; h < K; h++) { eq[h] = 0; for (int x = 0; x < K; x++) eq[h] += T.pc[h * K + x] * T.e2[h * K + x]; }
    for (int h = 0; h < K; h++) { double below = 0; for (int x = 0; x < K; x++) if (eq[x] < eq[h]) below += T.p1[x]; pct[h] = below; }
  }
  void strategy(const Node& n, int h, double* s) const {
    double tot = 0; for (int a = 0; a < n.nact; a++) { s[a] = std::max(0.0, regret[((size_t)n.base + h) * MAXA + a]); tot += s[a]; }
    if (tot <= 0) for (int a = 0; a < n.nact; a++) s[a] = 1.0 / n.nact; else for (int a = 0; a < n.nact; a++) s[a] /= tot;
  }
  void avg(const Node& n, int h, double* s) const {
    double tot = 0; for (int a = 0; a < n.nact; a++) { s[a] = ssum[((size_t)n.base + h) * MAXA + a]; tot += s[a]; }
    if (tot <= 0) for (int a = 0; a < n.nact; a++) s[a] = 1.0 / n.nact; else for (int a = 0; a < n.nact; a++) s[a] /= tot;
  }
  // realisation factor for player i among the in-hand set (the dealer, index N-3 (3p: 0), is in position; the
  // last in-hand player in postflop order acts last)
  double realisation(const Node& n, int i, int h, int k) const {
    int dealer = g.N - 3 < 0 ? 0 : g.N - 3;                 // 3p: index 0 is the dealer, 4p: index 1
    int lastpos = -1;                                      // postflop order: dealer + 1 ... dealer; the last in-hand one is in position
    for (int q = 1; q <= g.N; q++) { int j = (dealer + q) % g.N; if (n.in[j]) lastpos = j; }
    double r = (g.r_lo + (g.r_hi - g.r_lo) * pct[h]) * (i == lastpos ? g.r_ip : g.r_oop);
    for (int q = 2; q < k; q++) r *= g.r_multi;
    return r;
  }
  // terminal value for player p's classes, given the other players' reach vectors
  void terminal(const Node& n, int p, const std::vector<std::vector<double>>& reach, double* v) const {
    double pot = 0; for (int i = 0; i < g.N; i++) pot += n.c[i];
    // folded opponents only scale the value (their hand is not conditioned on ours): f = prod_j sum_x p1[x] reach[j][x];
    // opponents still in are weighted by pc[h][x] reach[j][x] (pairwise card removal with our hand)
    std::vector<int> opp; for (int j = 0; j < g.N; j++) if (j != p && n.in[j]) opp.push_back(j);
    double f = 1; for (int j = 0; j < g.N; j++) if (j != p && !n.in[j]) { double s = 0; for (int x = 0; x < K; x++) s += T.p1[x] * reach[j][x]; f *= s; }
    if (!n.in[p]) {                                        // we folded earlier: -c[p], weighted by the others' reach
      for (int h = 0; h < K; h++) {
        double w = f; for (int j : opp) { const double* pc = &T.pc[h * K]; double s = 0; for (int x = 0; x < K; x++) s += pc[x] * reach[j][x]; w *= s; }
        v[h] = -w * n.c[p];
      }
      return;
    }
    if (n.type == 1) { for (int h = 0; h < K; h++) v[h] = f * (pot - n.c[p]); return; }
    int k = opp.size() + 1;
    if (k == 2) {
      int j = opp[0]; const double* rj = reach[j].data();
      for (int h = 0; h < K; h++) {
        const double* pc = &T.pc[h * K]; const double* e = &T.e2[h * K]; double s = 0, w = 0;
        for (int x = 0; x < K; x++) { double ww = pc[x] * rj[x]; w += ww; s += ww * e[x]; }
        if (w <= 0) { v[h] = 0; continue; }
        double share = s / w; if (n.type == 3) share = realisation(n, p, h, 2) * share;
        v[h] = f * w * (share * pot - n.c[p]);
      }
      return;
    }
    // multiway: bucket-level shares; the opponents' bucket masses given h
    std::vector<double> m((size_t)opp.size() * B);
    for (int h = 0; h < K; h++) {
      const double* pc = &T.pc[h * K]; double wtot = 1; int bh = T.bucket[h];
      for (size_t q = 0; q < opp.size(); q++) {
        double* mq = &m[q * B]; for (int b = 0; b < B; b++) mq[b] = 0; double w = 0;
        const double* rj = reach[opp[q]].data();
        for (int x = 0; x < K; x++) { double ww = pc[x] * rj[x]; mq[T.bucket[x]] += ww; w += ww; }
        wtot *= w; if (w > 0) for (int b = 0; b < B; b++) mq[b] /= w;
      }
      if (wtot <= 0) { v[h] = 0; continue; }
      double s = 0;
      if (k == 3) {
        for (int bx = 0; bx < B; bx++) for (int by = 0; by < B; by++) { double ww = m[bx] * m[B + by]; if (ww > 0) s += ww * T.eq3b[(((size_t)bh * B + bx) * B + by) * 3]; }
      } else {
        for (int bx = 0; bx < B; bx++) for (int by = 0; by < B; by++) for (int bz = 0; bz < B; bz++) {
          double ww = m[bx] * m[B + by] * m[2 * B + bz]; if (ww > 0) s += ww * T.eq4b[((((size_t)bh * B + bx) * B + by) * B + bz) * 4];
        }
      }
      double share = n.type == 3 ? realisation(n, p, h, k) * s : s;
      v[h] = f * wtot * (share * pot - n.c[p]);
    }
  }
  void iterate() {
    iters++;
    double w = iters;
    for (int p = 0; p < g.N; p++) {
      std::vector<std::vector<double>> reach(g.N, std::vector<double>(K, 1.0));
      std::vector<double> v(K);
      walk_p(0, p, reach, std::vector<double>(K, 1.0), v.data(), w);
    }
  }
  // walk with the traverser's reach carried along (for the average strategy)
  void walk_p(int id, int p, std::vector<std::vector<double>>& reach, std::vector<double> reach_p, double* v, double w) {
    const Node& n = tree.nodes[id];
    if (n.type != 0) { terminal(n, p, reach, v); return; }
    std::vector<double> va((size_t)n.nact * K);
    if (n.player == p) {
      std::vector<double> rp(K);
      for (int a = 0; a < n.nact; a++) {
        for (int h = 0; h < K; h++) { double s[MAXA]; strategy(n, h, s); rp[h] = reach_p[h] * s[a]; }
        walk_p(n.child[a], p, reach, rp, &va[(size_t)a * K], w);
      }
      for (int h = 0; h < K; h++) {
        double s[MAXA]; strategy(n, h, s); double vs = 0;
        for (int a = 0; a < n.nact; a++) vs += s[a] * va[(size_t)a * K + h];
        v[h] = vs;
        for (int a = 0; a < n.nact; a++) { double& r = regret[((size_t)n.base + h) * MAXA + a]; r = std::max(0.0, r + va[(size_t)a * K + h] - vs); }
        for (int a = 0; a < n.nact; a++) ssum[((size_t)n.base + h) * MAXA + a] += w * reach_p[h] * s[a];
      }
      return;
    }
    int j = n.player; std::vector<double> saved = reach[j];
    for (int h = 0; h < K; h++) v[h] = 0;
    for (int a = 0; a < n.nact; a++) {
      for (int h = 0; h < K; h++) { double s[MAXA]; strategy(n, h, s); reach[j][h] = saved[h] * s[a]; }
      walk_p(n.child[a], p, reach, reach_p, &va[(size_t)a * K], w);
      for (int h = 0; h < K; h++) v[h] += va[(size_t)a * K + h];
    }
    reach[j] = saved;
  }
  void print(FILE* f = stdout) const {
    fprintf(f, "N=%d S=%g: %zu nodes, %d decision nodes, iters %d\n", g.N, g.S, tree.nodes.size(), tree.ndec, iters);
    for (int id = 0; id < (int)tree.nodes.size(); id++) {
      const Node& n = tree.nodes[id]; if (n.type || n.depth > 3) continue;
      double freq[MAXA] = {0, 0, 0, 0};
      for (int c = 0; c < K; c++) { double s[MAXA]; avg(n, c, s); for (int a = 0; a < n.nact; a++) freq[a] += s[a] * T.p1[c]; }
      fprintf(f, "  %-8s p%d:", n.hist.empty() ? "(root)" : n.hist.c_str(), n.player);
      for (int a = 0; a < n.nact; a++) fprintf(f, "  %c %5.1f%%", n.acts[a], 100 * freq[a]);
      fprintf(f, "\n");
    }
  }
};

}  // namespace pfd
