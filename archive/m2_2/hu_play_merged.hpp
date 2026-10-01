#pragma once
// hu_play.hpp - playing the solved heads-up 8-30 BB strategy (bot/hu_tables.hpp, from solvers/hu).
//
// The hand so far is mapped onto the abstract game's action history (F fold, C call/limp, K check, R raise,
// B half-pot bet, A all-in; '/' between streets; a raise of 60% of the stack or more counts as all-in, any
// other raise as the tree's raise, any non-all-in bet as the half-pot bet, a postflop raise as all-in).  The
// node for that history at the two nearest stack points gives action probabilities for our bucket (the
// preflop class, or the EHS bucket postflop); they are interpolated in stack and sampled.  A history the
// tree does not have (a 4-bet, a timeout) is off-tree and the caller falls back to its rules.
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "hu_tables.hpp"

namespace bot {

struct HuNode { int player = 0, street = 0, nact = 0; char acts[4] = {0, 0, 0, 0}; double c0 = 0, c1 = 0; int off = 0; };
struct HuTables {
  std::vector<unsigned char> data;
  std::vector<std::map<std::string, HuNode>> nodes;    // preflop nodes per stack point
  std::map<std::string, HuNode> post;                  // postflop nodes by key (hu_key)
  bool ok = false;
};
inline std::string hu_key(int street, int pot_idx, int behind_idx, const std::string& acts, const std::string& hist) {
  return std::to_string(street) + "|" + std::to_string(pot_idx) + "|" + std::to_string(behind_idx) + "|" + acts + "|" + hist;
}
inline int hu_log_idx(double x) { return (int)std::lround(std::log(std::max(x, 0.5)) / std::log(hu_t::LOGBASE)) + hu_t::IDX_OFF; }
int cjk14_decode(const char* s, unsigned char* out, int out_max);   // bot.hpp

// The abstract tree, rebuilt from the solver's rules (solvers/hu/hu.hpp Tree::build, same depth-first order)
struct HuTree {
  double S; std::map<std::string, HuNode>& nodes; int& off; int ndec = 0; unsigned long long hash = 1469598103934665603ull;
  HuTree(double s, std::map<std::string, HuNode>& m, int& o) : S(s), nodes(m), off(o) { build(0, 0.5, 1.0, 0, "", false); }
  void build(int street, double c0, double c1, int toact, const std::string& hist, bool checked) {
    double c[2] = {c0, c1}; int other = 1 - toact;
    bool facing = c[toact] < c[other] - 1e-9, opp_allin = c[other] >= S - 1e-9;
    std::vector<std::pair<char, double>> acts;
    auto add_raise_to = [&](char a, double target) { if (target >= S - 1e-9) { if (!opp_allin) acts.push_back({'A', S}); } else acts.push_back({a, target}); };
    if (street == 0 && hist.empty()) { acts = {{'F', c0}, {'C', 1.0}}; add_raise_to('R', 2.5); if (acts.back().first != 'A') acts.push_back({'A', S}); }
    else if (facing) {
      acts = {{'F', c[toact]}, {'C', c[other]}};
      if (!opp_allin) { if (street == 0 && hist == "R") add_raise_to('R', 3 * c[other]); if (acts.back().first != 'A') acts.push_back({'A', S}); }
    } else if (street == 0 && hist == "C") { acts = {{'K', c1}}; add_raise_to('R', 3.0); if (acts.back().first != 'A') acts.push_back({'A', S}); }
    else { acts = {{'K', c[toact]}}; add_raise_to('B', c[toact] + 0.5 * (c0 + c1)); add_raise_to('b', c[toact] + 1.0 * (c0 + c1)); if (acts.back().first != 'A') acts.push_back({'A', S}); }
    for (size_t i = 1; i < acts.size(); ) if (acts[i].first == 'A' && acts[i - 1].first == 'A') acts.erase(acts.begin() + i); else i++;
    HuNode nd; nd.player = toact; nd.street = street; nd.nact = acts.size(); for (int k = 0; k < nd.nact; k++) nd.acts[k] = acts[k].first;
    nd.c0 = c0; nd.c1 = c1; nd.off = -1;
    if (street == 0) {                                   // preflop nodes are stored per stack point; postflop ones are keyed
      nd.off = off; off += (169 * nd.nact + 1) / 2; ndec++;
      for (unsigned char ch : hist) hash = (hash ^ ch) * 1099511628211ull;
      hash = (hash ^ 0x2F) * 1099511628211ull;
      nodes[hist] = nd;
    }
    for (int k = 0; k < nd.nact; k++) {
      char a = acts[k].first; double nc[2] = {c0, c1}; nc[toact] = acts[k].second; std::string h2 = hist + a;
      if (a == 'F') continue;
      if (a == 'C' && !(street == 0 && hist.empty())) {
        bool allin = nc[0] >= S - 1e-9 && nc[1] >= S - 1e-9;
        if (!(allin || street == 3)) build(street + 1, nc[0], nc[1], 1, h2 + "/", false);
      } else if (a == 'K') {
        if (checked || street == 0) { if (street != 3) build(street + 1, nc[0], nc[1], 1, h2 + "/", false); }
        else build(street, nc[0], nc[1], other, h2, true);
      } else build(street, nc[0], nc[1], other, h2, false);
    }
  }
};
inline const HuTables& hu_tables() {
  static const HuTables T = [] {
    HuTables t;
    t.data.resize(strlen(hu_t::DATA) / 3 * 14 / 8 + 8);
    int n = cjk14_decode(hu_t::DATA, t.data.data(), (int)t.data.size());
    if (n <= 0) return t;
    int off = 0; bool ok = true;
    for (int s = 0; s < hu_t::NS; s++) {
      t.nodes.push_back({});
      HuTree tr(hu_t::STACKS[s], t.nodes.back(), off);
      ok = ok && tr.ndec == hu_t::NDEC[s] && tr.hash == hu_t::HASH[s];
    }
    ok = ok && off == hu_t::NPRE;
    for (int k = 0; ok && k < hu_t::NKEYS; k++) {         // postflop records
      if (off + 9 > n) { ok = false; break; }
      const unsigned char* r = &t.data[off];
      HuNode nd; nd.street = r[0]; nd.nact = r[3]; for (int i = 0; i < 4; i++) nd.acts[i] = i < nd.nact ? (char)r[4 + i] : 0;
      int hl = r[8]; std::string hist((const char*)&t.data[off + 9], hl);
      nd.off = off + 9 + hl;
      t.post[hu_key(nd.street, r[1], r[2], std::string(nd.acts, nd.nact), hist)] = nd;
      off = nd.off + (10 * nd.nact + 1) / 2;
    }
    t.ok = ok && off <= n;
    return t;
  }();
  return T;
}
inline void hu_probs(const HuTables& t, const HuNode& n, int bucket, double* p) {
  double tot = 0;
  for (int a = 0; a < n.nact; a++) {
    int idx = bucket * n.nact + a; unsigned char b = t.data[n.off + idx / 2];
    p[a] = (idx & 1 ? b & 15 : b >> 4) / 15.0; tot += p[a];
  }
  if (tot <= 0) for (int a = 0; a < n.nact; a++) p[a] = 1.0 / n.nact; else for (int a = 0; a < n.nact; a++) p[a] /= tot;
}
inline int hu_bucket(int street, double ehs) { int b = 0; while (b < 9 && ehs >= hu_t::THR[street - 1][b]) b++; return b; }

inline char hu_sample(const HuTables& t, const HuNode& n, int bucket, double u) {
  double p[4]; hu_probs(t, n, bucket, p);
  double acc = 0; int pick = n.nact - 1;
  for (int a = 0; a < n.nact; a++) { acc += p[a]; if (u < acc) { pick = a; break; } }
  return n.acts[pick];
}
// postflop: the sampled action for this street's history, given the pot and the stack behind (BB, the real ones);
// 0 if no record fits.  The actions available follow from facing/not facing and which bet sizes are below the stack.
inline char hu_decide_post(int street, const std::string& street_hist, double pot_bb, double behind_bb, int bucket, double u) {
  const HuTables& t = hu_tables(); if (!t.ok) return 0;
  bool facing = !street_hist.empty() && (street_hist.back() == 'B' || street_hist.back() == 'b' || street_hist.back() == 'A');
  bool opp_allin = !street_hist.empty() && street_hist.back() == 'A';
  std::vector<std::string> cands;
  if (facing) { if (opp_allin) cands = {"FC"}; else cands = {"FCA", "FC"}; }
  else {
    bool half = 0.5 * pot_bb < behind_bb - 1e-9, full = 1.0 * pot_bb < behind_bb - 1e-9;
    std::string first = std::string("K") + (half ? "B" : "") + (full ? "b" : "") + "A";
    cands = {first, "KBbA", "KBA", "KA"};
  }
  int pi = hu_log_idx(pot_bb), bi = hu_log_idx(behind_bb);
  for (const std::string& acts : cands)
    for (int dp = 0; dp <= 1; dp++) for (int db = 0; db <= 1; db++)              // nearest buckets first
      for (int sp = -1; sp <= 1; sp += 2) for (int sb = -1; sb <= 1; sb += 2) {
        if ((dp == 0 && sp == 1) || (db == 0 && sb == 1)) continue;
        auto it = t.post.find(hu_key(street, pi + sp * dp, bi + sb * db, acts, street_hist));
        if (it != t.post.end()) return hu_sample(t, it->second, bucket, u);
      }
  return 0;
}
// preflop: the sampled abstract action at history `hist` for effective stack `eff` (BB) and our class; 0 if off-tree
inline char hu_decide(double eff, const std::string& hist, int bucket, double u, const HuNode** used = nullptr) {
  const HuTables& t = hu_tables(); if (!t.ok) return 0;
  int s0 = 0; while (s0 + 1 < hu_t::NS && hu_t::STACKS[s0 + 1] <= eff) s0++;
  int s1 = std::min(s0 + 1, hu_t::NS - 1);
  double w = s1 > s0 ? (eff - hu_t::STACKS[s0]) / (hu_t::STACKS[s1] - hu_t::STACKS[s0]) : 0; w = std::min(1.0, std::max(0.0, w));
  auto i0 = t.nodes[s0].find(hist), i1 = t.nodes[s1].find(hist);
  const HuNode* n0 = i0 == t.nodes[s0].end() ? nullptr : &i0->second;
  const HuNode* n1 = i1 == t.nodes[s1].end() ? nullptr : &i1->second;
  if (!n0 && !n1) return 0;
  double p[4] = {0, 0, 0, 0};
  const HuNode* n = n0 ? n0 : n1;
  bool same = n0 && n1 && n0->nact == n1->nact; for (int k = 0; same && k < 4; k++) same = n0->acts[k] == n1->acts[k];
  if (same) {
    double q0[4], q1[4]; hu_probs(t, *n0, bucket, q0); hu_probs(t, *n1, bucket, q1);
    for (int a = 0; a < n->nact; a++) p[a] = (1 - w) * q0[a] + w * q1[a];
  } else { n = (n0 && (!n1 || w < 0.5)) ? n0 : n1; hu_probs(t, *n, bucket, p); }
  if (used) *used = n;
  double acc = 0; int pick = n->nact - 1;
  for (int a = 0; a < n->nact; a++) { acc += p[a]; if (u < acc) { pick = a; break; } }
  return n->acts[pick];
}

}  // namespace bot
