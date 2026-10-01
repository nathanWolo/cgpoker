#pragma once
// pfn.hpp - N-player (2-4) preflop jam/fold equilibrium with ICM payouts, by fictitious play.
//
// The game: one hand.  Every non-BB player posts the small blind (0.5 BB), the BB posts 1 BB (this
// referee's rule).  Players act in order (index N-1 is the BB, index 0 acts first) and either fold or
// go all-in; a player facing an all-in calls (all-in) or folds.  Terminal values are Malmuth-Harville
// ICM (cpp/icm.hpp) of the resulting stacks with the game's TrueSkill payouts, so a hand's value is in
// "share of the prize pool".  Hands are the 169 preflop classes (index as in solvers/eq.c).
//
// Approximations (all standard for push/fold solvers):
//   - card removal: exact for the two hands of a 3-way showdown given our own (W3 counts), pairwise
//     for everyone else (W2); a folded hand tells nothing about the folder's cards;
//   - 3-way all-in: main pot by the Monte Carlo 3-way shares (eq3), the side pot by the pairwise equity
//     of its two contenders; 4-way all-ins at the level of 20 strength buckets (eq4b);
//   - the value of folding does not depend on the folded hand;
//   - ties are folded into the win shares.
// Fictitious play: each iteration every node best-responds to the others' average strategy; the
// average is updated with weight 1/t.  `eps` is the reach-weighted gain a best response would make
// over the average strategy, summed over all nodes, in prize-pool units.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "../../cpp/icm.hpp"

namespace pfn {
constexpr int K = 169, B = 20;

inline int combos_of(int c) { int r1 = c / 13, r2 = c % 13; return r1 == r2 ? 6 : r1 > r2 ? 4 : 12; }
inline std::string class_name(int c) {
  const char* R = "23456789TJQKA"; int r1 = c / 13, r2 = c % 13;
  std::string s; s += R[std::max(r1, r2)]; s += R[std::min(r1, r2)];
  if (r1 != r2) s += r1 > r2 ? 's' : 'o';
  return s;
}

struct Tables {
  std::vector<uint16_t> w2, w3;
  std::vector<float> eq3, eq4b;
  std::vector<double> e2;            // K*K: P(a beats b), ties 1/2
  int bucket[K], nc[K];
  double p1[K];                      // P(class)
  std::vector<double> pc;            // K*K:   P(x | h)
  std::vector<double> pxy;           // K*K*K: P(x, y | h)
  std::vector<double> pu2;           // K*K:   P(x, y)
  std::vector<double> eq3b, eq2b;    // B^3*3, B*B: bucket-level shares
  static bool rd(const std::string& path, void* p, size_t bytes) {
    FILE* f = fopen(path.c_str(), "rb"); if (!f) { fprintf(stderr, "cannot open %s\n", path.c_str()); return false; }
    size_t n = fread(p, 1, bytes, f); fclose(f);
    if (n != bytes) { fprintf(stderr, "%s: short read %zu of %zu\n", path.c_str(), n, bytes); return false; }
    return true;
  }
  bool load(const std::string& dir, const std::string& eq169) {
    w2.resize(K * K); w3.resize((size_t)K * K * K); eq3.resize((size_t)K * K * K * 3); eq4b.resize((size_t)B * B * B * B * 4); e2.resize(K * K);
    uint8_t bk[K];
    if (!rd(dir + "/w2.bin", w2.data(), w2.size() * 2) || !rd(dir + "/w3.bin", w3.data(), w3.size() * 2) ||
        !rd(dir + "/eq3.bin", eq3.data(), eq3.size() * 4) || !rd(dir + "/eq4b.bin", eq4b.data(), eq4b.size() * 4) ||
        !rd(dir + "/bucket.bin", bk, K) || !rd(eq169, e2.data(), e2.size() * 8)) return false;
    for (int c = 0; c < K; c++) bucket[c] = bk[c], nc[c] = combos_of(c), p1[c] = nc[c] / 1326.0;
    pc.resize(K * K); pxy.resize((size_t)K * K * K); pu2.resize(K * K);
    double tot2 = 0;
    for (int h = 0; h < K; h++) {
      double z2 = 0, z3 = 0;
      for (int x = 0; x < K; x++) { z2 += w2[h * K + x]; tot2 += w2[h * K + x]; for (int y = 0; y < K; y++) z3 += w3[((size_t)h * K + x) * K + y]; }
      for (int x = 0; x < K; x++) { pc[h * K + x] = w2[h * K + x] / z2; for (int y = 0; y < K; y++) pxy[((size_t)h * K + x) * K + y] = w3[((size_t)h * K + x) * K + y] / z3; }
    }
    for (int x = 0; x < K; x++) for (int y = 0; y < K; y++) pu2[x * K + y] = w2[x * K + y] / tot2;
    // bucket-level shares: combo-weighted averages over the member classes
    eq3b.assign((size_t)B * B * B * 3, 0); eq2b.assign(B * B, 0);
    std::vector<std::vector<int>> mem(B); for (int c = 0; c < K; c++) mem[bucket[c]].push_back(c);
    for (int a = 0; a < B; a++) for (int b = 0; b < B; b++) {
      double s = 0, w = 0;
      for (int x : mem[a]) for (int y : mem[b]) { double ww = (double)nc[x] * nc[y]; s += ww * e2[x * K + y]; w += ww; }
      eq2b[a * B + b] = s / w;
      for (int c = 0; c < B; c++) {
        double sh[3] = {0, 0, 0}; w = 0;
        for (int x : mem[a]) for (int y : mem[b]) for (int z : mem[c]) {
          double ww = (double)nc[x] * nc[y] * nc[z]; const float* q = &eq3[(((size_t)x * K + y) * K + z) * 3];
          sh[0] += ww * q[0]; sh[1] += ww * q[1]; sh[2] += ww * q[2]; w += ww;
        }
        for (int k = 0; k < 3; k++) eq3b[(((size_t)a * B + b) * B + c) * 3 + k] = sh[k] / w;
      }
    }
    return true;
  }
};

struct Game {
  int N = 3, ntot = 3; double S[4] = {10, 10, 10, 10}; const double* pay = icm::PAY3; double pool = 1.5;
  void set(int n, int nt, const double* st) {
    N = n; ntot = nt; for (int i = 0; i < n; i++) S[i] = st[i];
    pay = icm::payouts(nt); pool = 0; for (int i = 0; i < nt; i++) pool += pay[i];
  }
  double blind(int j) const { return j == N - 1 ? 1.0 : 0.5; }
};

// final stacks after history H when the all-in players finish in order rank[0..nr) (best first)
inline void settle(const Game& g, uint32_t H, const int* rank, int nr, double* fin) {
  double c[4], lv[4]; int nl = 0;
  for (int j = 0; j < g.N; j++) { c[j] = (H >> j & 1) ? g.S[j] : std::min(g.blind(j), g.S[j]); fin[j] = g.S[j] - c[j]; lv[nl++] = c[j]; }
  std::sort(lv, lv + nl); nl = std::unique(lv, lv + nl) - lv;
  double prev = 0;
  for (int l = 0; l < nl; l++) {
    double L = lv[l], pot = 0;
    for (int j = 0; j < g.N; j++) pot += std::min(c[j], L) - std::min(c[j], prev);
    int w = -1;
    for (int r = 0; r < nr; r++) if (c[rank[r]] >= L - 1e-9) { w = rank[r]; break; }
    if (w >= 0) fin[w] += pot;
    else for (int j = 0; j < g.N; j++) fin[j] += std::min(c[j], L) - std::min(c[j], prev);   // nobody eligible: returned
    prev = L;
  }
}

struct Hist {                       // one complete action history
  int na = 0, A[4] = {0, 0, 0, 0};  // all-in players, ascending id
  int pos[4] = {0, 0, 0, 0};        // the same players, ascending stack
  int no = 1;                       // showdown outcomes (orderings that matter for the pots)
  double V[8][4];                   // V[outcome][player] = ICM value of that player
};

struct Solver {
  const Tables& T; Game g; int NS = 0;
  std::vector<Hist> HS;
  std::vector<double> sig, evj, evf, F, M, Fu, Mu, reach;
  std::vector<int> nodes;           // decision nodes in order
  int iters = 0; double eps = 1;

  Solver(const Tables& t, const Game& game) : T(t), g(game) {
    NS = (1 << g.N) - 1;
    sig.assign((size_t)NS * K, 0); evj.assign((size_t)NS * K, 0); evf.assign(NS, 0);
    F.assign((size_t)NS * K, 0); M.assign((size_t)NS * K * B, 0); Fu.assign(NS, 0); Mu.assign((size_t)NS * B, 0); reach.assign(NS, 0);
    for (int i = 0; i < g.N; i++) for (int p = 0; p < (1 << i); p++) if (has_node(i, p)) nodes.push_back(node(i, p));
    build_hists();
  }
  static int node(int i, int p) { return (1 << i) - 1 + p; }
  int node_of(int j, uint32_t H) const { return node(j, H & ((1u << j) - 1)); }
  bool has_node(int i, int p) const { return !(i == g.N - 1 && p == 0); }
  static void node_ip(int nd, int& i, int& p) { i = 0; while ((2 << i) - 1 <= nd) i++; p = nd - ((1 << i) - 1); }
  std::string node_name(int nd) const {
    static const char* L2[] = {"SB", "BB"}; static const char* L3[] = {"D", "SB", "BB"}; static const char* L4[] = {"U", "D", "SB", "BB"};
    const char** L = g.N == 2 ? L2 : g.N == 3 ? L3 : L4;
    int i, p; node_ip(nd, i, p);
    std::string s = L[i]; s += p ? " after " : " first in";
    for (int j = 0; j < i; j++) s += (p >> j & 1) ? 'j' : 'f';
    s += p ? " (call)" : " (jam)";
    return s;
  }
  void build_hists() {
    HS.resize(1u << g.N);
    for (uint32_t H = 0; H < (1u << g.N); H++) {
      Hist& hs = HS[H];
      for (int j = 0; j < g.N; j++) if (H >> j & 1) hs.A[hs.na++] = j;
      for (int k = 0; k < hs.na; k++) hs.pos[k] = hs.A[k];
      std::stable_sort(hs.pos, hs.pos + hs.na, [&](int a, int b) { return g.S[a] < g.S[b]; });
      std::vector<std::array<int, 4>> rk;
      const int* P = hs.pos;
      if (hs.na == 0) rk = {{g.N - 1, 0, 0, 0}};
      else if (hs.na == 1) rk = {{hs.A[0], 0, 0, 0}};
      else if (hs.na == 2) rk = {{hs.A[0], hs.A[1], 0, 0}, {hs.A[1], hs.A[0], 0, 0}};
      else if (hs.na == 3) rk = {{P[0], P[1], P[2], 0}, {P[0], P[2], P[1], 0}, {P[1], P[2], P[0], 0}, {P[2], P[1], P[0], 0}};
      else rk = {{P[0], P[1], P[2], P[3]}, {P[0], P[1], P[3], P[2]}, {P[0], P[2], P[3], P[1]}, {P[0], P[3], P[2], P[1]},
                 {P[1], P[2], P[3], P[0]}, {P[1], P[3], P[2], P[0]}, {P[2], P[3], P[1], P[0]}, {P[3], P[2], P[1], P[0]}};
      hs.no = rk.size();
      for (int o = 0; o < hs.no; o++) {
        double fin[4];
        settle(g, H, rk[o].data(), std::max(1, hs.na), fin);
        for (int i = 0; i < g.N; i++) hs.V[o][i] = icm::icm(fin, g.N, i, g.pay);
      }
    }
  }
  // per-iteration aggregates of the current strategy
  void prep() {
    for (int nd : nodes) {
      const double* s = &sig[(size_t)nd * K];
      double fu = 0; double mu[B] = {0};
      for (int x = 0; x < K; x++) { fu += T.p1[x] * (1 - s[x]); mu[T.bucket[x]] += T.p1[x] * s[x]; }
      Fu[nd] = fu; for (int b = 0; b < B; b++) Mu[(size_t)nd * B + b] = mu[b];
      for (int h = 0; h < K; h++) {
        const double* pc = &T.pc[h * K]; double f = 0; double m[B] = {0};
        for (int x = 0; x < K; x++) { f += pc[x] * (1 - s[x]); m[T.bucket[x]] += pc[x] * s[x]; }
        F[(size_t)nd * K + h] = f; for (int b = 0; b < B; b++) M[((size_t)nd * K + h) * B + b] = m[b];
      }
    }
    for (int nd : nodes) {
      int i, p; node_ip(nd, i, p); double r = 1;
      for (int j = 0; j < i; j++) { int nj = node_of(j, p); r *= (p >> j & 1) ? 1 - Fu[nj] : Fu[nj]; }
      reach[nd] = r;
    }
  }
  // 3-way payoff from shares s[] (in A order) for the player whose outcome values are V
  static inline double pay3(const double* s, const int* idx, double q, const double* V) {
    return s[idx[0]] * (q * V[0] + (1 - q) * V[1]) + s[idx[1]] * V[2] + s[idx[2]] * V[3];
  }
  void compute() {
    const int N = g.N;
    for (int nd : nodes) {
      int i, p; node_ip(nd, i, p);
      const int nlater = N - 1 - i;
      // ---- fold: continuation among the others, independent of our hand
      double vf = 0;
      for (int comp = 0; comp < (1 << nlater); comp++) {
        uint32_t H = p | ((uint32_t)comp << (i + 1));
        if (H == (1u << (N - 1))) continue;              // "everyone folds, the BB jams": the BB has no decision there
        const Hist& hs = HS[H];
        double fac = 1, norm = 1;
        for (int j = i + 1; j < N; j++) if (!(H >> j & 1) && !(j == N - 1 && (H & ((1u << j) - 1)) == 0)) fac *= Fu[node_of(j, H)];
        for (int k = 0; k < hs.na; k++) { int j = hs.A[k]; if (j > i) fac *= 1 - Fu[node_of(j, H)]; else norm *= 1 - Fu[node_of(j, H)]; }
        if (norm < 1e-12) continue;
        const double* V = nullptr; double term = 0;
        if (hs.na <= 1) term = hs.V[0][i];
        else if (hs.na == 2) {
          int a = hs.A[0], b = hs.A[1]; const double* sa = &sig[(size_t)node_of(a, H) * K]; const double* sb = &sig[(size_t)node_of(b, H) * K];
          double V0 = hs.V[0][i], V1 = hs.V[1][i], s = 0, w = 0;
          for (int x = 0; x < K; x++) { if (sa[x] == 0) continue; const double* pu = &T.pu2[x * K]; const double* e = &T.e2[x * K];
            for (int y = 0; y < K; y++) { double ww = pu[y] * sa[x] * sb[y]; s += ww * (e[y] * V0 + (1 - e[y]) * V1); w += ww; } }
          term = w > 0 ? s / w : 0;                      // conditional on both being all-in
          // probability that they are: later players' weights are in fac already (1-Fu); earlier players' in norm
        } else {                                         // three others all-in (N = 4): bucket level
          int a = hs.A[0], b = hs.A[1], c = hs.A[2];
          const double* ma = &Mu[(size_t)node_of(a, H) * B]; const double* mb = &Mu[(size_t)node_of(b, H) * B]; const double* mc = &Mu[(size_t)node_of(c, H) * B];
          int idx[3], hb[3]; for (int k = 0; k < 3; k++) idx[k] = hs.pos[k] == a ? 0 : hs.pos[k] == b ? 1 : 2;
          double Vi[4] = {hs.V[0][i], hs.V[1][i], hs.V[2][i], hs.V[3][i]}, s = 0, w = 0;
          for (int ba = 0; ba < B; ba++) for (int bb = 0; bb < B; bb++) for (int bc = 0; bc < B; bc++) {
            double ww = ma[ba] * mb[bb] * mc[bc]; if (ww == 0) continue;
            hb[0] = ba; hb[1] = bb; hb[2] = bc;
            const double* sh = &T.eq3b[(((size_t)ba * B + bb) * B + bc) * 3];
            double q = T.eq2b[hb[idx[1]] * B + hb[idx[2]]];
            s += ww * pay3(sh, idx, q, Vi); w += ww;
          }
          term = w > 0 ? s / w : 0;
        }
        (void)V;
        vf += fac * term;
      }
      evf[nd] = vf;
      // ---- jam / call, per hand
      double* ej = &evj[(size_t)nd * K];
      for (int h = 0; h < K; h++) ej[h] = 0;
      for (int comp = 0; comp < (1 << nlater); comp++) {
        uint32_t H = p | (1u << i) | ((uint32_t)comp << (i + 1));
        const Hist& hs = HS[H];
        int C[3], nC = 0; for (int k = 0; k < hs.na; k++) if (hs.A[k] != i) C[nC++] = hs.A[k];
        for (int h = 0; h < K; h++) {
          double fac = 1, norm = 1;
          for (int j = i + 1; j < N; j++) if (!(H >> j & 1)) fac *= F[(size_t)node_of(j, H) * K + h];
          for (int k = 0; k < nC; k++) if (C[k] < i) norm *= 1 - F[(size_t)node_of(C[k], H) * K + h];
          if (norm < 1e-12) continue;
          double term = 0;
          if (nC == 0) term = hs.V[0][i];
          else if (nC == 1) {
            int c = C[0]; const double* sc = &sig[(size_t)node_of(c, H) * K];
            double Vw = hs.V[i < c ? 0 : 1][i], Vl = hs.V[i < c ? 1 : 0][i];
            const double* pc = &T.pc[h * K]; const double* e = &T.e2[h * K];
            for (int x = 0; x < K; x++) term += pc[x] * sc[x] * (e[x] * Vw + (1 - e[x]) * Vl);
          } else if (nC == 2) {
            int a = C[0], b = C[1]; const double* sa = &sig[(size_t)node_of(a, H) * K]; const double* sb = &sig[(size_t)node_of(b, H) * K];
            int idx[3]; for (int k = 0; k < 3; k++) idx[k] = hs.pos[k] == i ? 0 : hs.pos[k] == a ? 1 : 2;
            double Vi[4] = {hs.V[0][i], hs.V[1][i], hs.V[2][i], hs.V[3][i]};
            const double* pxy = &T.pxy[(size_t)h * K * K]; const float* q3 = &T.eq3[(size_t)h * K * K * 3];
            // q = P(pos[1] beats pos[2]); hands: i -> h, a -> x, b -> y
            int q1 = idx[1], q2 = idx[2];
            for (int x = 0; x < K; x++) {
              if (sa[x] == 0) continue;
              const double* pr = &pxy[x * K]; const float* sh = &q3[(size_t)x * K * 3];
              for (int y = 0; y < K; y++) {
                double ww = pr[y] * sa[x] * sb[y]; if (ww == 0) continue;
                int hd[3] = {h, x, y};
                double q = T.e2[hd[q1] * K + hd[q2]];
                double s[3] = {sh[y * 3], sh[y * 3 + 1], sh[y * 3 + 2]};
                term += ww * pay3(s, idx, q, Vi);
              }
            }
          } else {                                       // three callers: bucket level
            int a = C[0], b = C[1], c = C[2]; int bh = T.bucket[h];
            const double* ma = &M[((size_t)node_of(a, H) * K + h) * B]; const double* mb = &M[((size_t)node_of(b, H) * K + h) * B]; const double* mc = &M[((size_t)node_of(c, H) * K + h) * B];
            int idx[4]; for (int k = 0; k < 4; k++) idx[k] = hs.pos[k] == i ? 0 : hs.pos[k] == a ? 1 : hs.pos[k] == b ? 2 : 3;
            const double* Vi = hs.V[0]; (void)Vi;
            double Vo[8]; for (int o = 0; o < 8; o++) Vo[o] = hs.V[o][i];
            int hb[4]; hb[0] = bh;
            for (int ba = 0; ba < B; ba++) for (int bb = 0; bb < B; bb++) for (int bc = 0; bc < B; bc++) {
              double ww = ma[ba] * mb[bb] * mc[bc]; if (ww == 0) continue;
              hb[1] = ba; hb[2] = bb; hb[3] = bc;
              const float* s4 = &T.eq4b[((((size_t)bh * B + ba) * B + bb) * B + bc) * 4];
              double s1 = s4[idx[0]], s2 = s4[idx[1]], s3 = s4[idx[2]], s4v = s4[idx[3]];
              const double* r3 = &T.eq3b[(((size_t)hb[idx[1]] * B + hb[idx[2]]) * B + hb[idx[3]]) * 3];   // among pos[1..3]
              double t = T.eq2b[hb[idx[2]] * B + hb[idx[3]]];
              term += ww * (s1 * (r3[0] * (t * Vo[0] + (1 - t) * Vo[1]) + r3[1] * Vo[2] + r3[2] * Vo[3]) +
                            s2 * (t * Vo[4] + (1 - t) * Vo[5]) + s3 * Vo[6] + s4v * Vo[7]);
            }
          }
          ej[h] += fac * term / norm;
        }
      }
    }
  }
  // one fictitious-play step; returns eps (reach-weighted best-response gain, pool units)
  double step(int t) {
    prep(); compute();
    double gain = 0;
    for (int nd : nodes) {
      double vf = evf[nd]; double* s = &sig[(size_t)nd * K]; const double* ej = &evj[(size_t)nd * K];
      for (int h = 0; h < K; h++) {
        double cur = s[h] * ej[h] + (1 - s[h]) * vf, best = std::max(ej[h], vf);
        gain += reach[nd] * T.p1[h] * (best - cur);
        double br = ej[h] > vf ? 1 : 0;
        s[h] += (br - s[h]) / t;
      }
    }
    iters = t;
    return eps = gain / g.pool;
  }
  void solve(int max_iters, double tol = 1e-4) {
    for (int t = 1; t <= max_iters; t++) { double e = step(t); if (t >= 50 && e < tol) break; }
  }
  double width(int nd) const { double w = 0; for (int h = 0; h < K; h++) w += T.p1[h] * (sig[(size_t)nd * K + h] > 0.5); return w; }
  void print(FILE* f = stdout) const {
    fprintf(f, "N=%d ntot=%d stacks", g.N, g.ntot); for (int i = 0; i < g.N; i++) fprintf(f, " %g", g.S[i]);
    fprintf(f, " BB  iters=%d eps=%.5f\n", iters, eps);
    for (int nd : nodes) {
      const double* s = &sig[(size_t)nd * K];
      std::vector<int> in, mix; for (int h = 0; h < K; h++) { if (s[h] > 0.5) in.push_back(h); if (s[h] > 0.05 && s[h] < 0.95) mix.push_back(h); }
      std::sort(in.begin(), in.end(), [&](int a, int b) { return evj[(size_t)nd * K + a] - evj[(size_t)nd * K + b] > 0; });
      fprintf(f, "  %-24s reach %.3f  %5.1f%%  fold=%.4f", node_name(nd).c_str(), reach[nd], 100 * width(nd), evf[nd] / g.pool);
      fprintf(f, "  weakest:"); for (size_t k = in.size() > 6 ? in.size() - 6 : 0; k < in.size(); k++) fprintf(f, " %s", class_name(in[k]).c_str());
      if (!mix.empty()) { fprintf(f, "  mixed:"); for (int h : mix) fprintf(f, " %s=%.2f", class_name(h).c_str(), s[h]); }
      fprintf(f, "\n");
    }
  }
};

}  // namespace pfn
