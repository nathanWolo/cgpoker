#pragma once
// clone.hpp - arena opponents that play like the live CodinGame bots: per player, a stochastic policy fitted
// to that player's recorded decisions (analysis/clone_fit.py), and the decision features it is a function of.
//
// The same code computes the features for the training data (arena/clone_data.cpp replays the recorded games
// through the engine, with a Tracker per seat as the bot has) and for acting in the arena, so a clone sees
// exactly what its model was fitted on.
//
// Situations: preflop unopened and forced to act (fold / limp / raise / jam), the big blind's option (check /
// raise / jam), facing a raise (fold / call / raise / jam), facing an all-in-sized call (fold / call); postflop
// checked to (check / bet / jam), facing a bet (fold / call / raise / jam), facing an all-in-sized call.
// Action classes: 0 fold or check, 1 call, 2 bet or raise short of all-in, 3 all-in raise.
// Policy: a multinomial logit per situation over the legal classes, P(a) proportional to exp(W[a] . x).
// Bet size: drawn from the player's recorded sizes in that situation made with the nearest strength and depth
// (many bots bet bigger with stronger hands), in the unit that player sizes in (the chips the action adds over
// the pot after calling, over the big blind, or over the player's own chips; analysis/clone_fit.py picks it).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "../cpp/pe7c.hpp"
#include "../engine/tracker.hpp"
#include "../bot/pf_rank.hpp"

namespace clones {

enum Sit { P_OPEN, P_BBOPT, P_VSRAISE, P_VSJAM, Q_CK, Q_VSBET, Q_VSJAM, NSIT };
inline const char* sit_name(int s) {
  static const char* n[NSIT] = {"open", "bbopt", "vsraise", "vsjam", "ck", "vsbet", "vsjam_post"};
  return n[s];
}
constexpr int NF = 18, NA = 4;

struct Rng {
  uint64_t x;
  uint64_t next() { uint64_t z = (x += 0x9E3779B97F4A7C15ull); z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; return z ^ (z >> 31); }
  int below(int n) { return (int)((next() >> 32) * (uint64_t)n >> 32); }
  double uni() { return (next() >> 11) * (1.0 / 9007199254740992.0); }
};

inline int hand_class(int c0, int c1) {
  int r0 = c0 >> 2, r1 = c1 >> 2, hi = r0 > r1 ? r0 : r1, lo = r0 > r1 ? r1 : r0;
  if (r0 == r1) return r0 * 13 + r0;
  return (c0 & 3) == (c1 & 3) ? hi * 13 + lo : lo * 13 + hi;
}

// Strength of the hole cards on this board, 1 = best: preflop 1 - the class's percentile (bot/pf_rank.hpp);
// postflop the equity against one random hand to the river (exact on the river, 600 samples before it), seeded
// from the cards so the training data and the arena get the same number for the same cards.
inline double strength(const int* hole, const std::vector<int>& board) {
  if (board.empty()) return 1.0 - pf::PF_PCT100[hand_class(hole[0], hole[1])] / 100.0;
  uint64_t used = 1ull << hole[0] | 1ull << hole[1];
  pe::H b = pe::E;
  for (int c : board) used |= 1ull << c, b = pe::add(b, c);
  int deck[52], nd = 0;
  for (int c = 0; c < 52; c++) if (!(used >> c & 1)) deck[nd++] = c;
  pe::H mine = pe::add(pe::add(b, hole[0]), hole[1]);
  double win = 0; int n = 0;
  if (board.size() == 5) {
    int v = pe::ev(mine);
    for (int i = 0; i < nd; i++) for (int j = i + 1; j < nd; j++) {
      int w = pe::ev(pe::add(pe::add(b, deck[i]), deck[j]));
      win += v > w ? 1 : v == w ? 0.5 : 0; n++;
    }
    return win / n;
  }
  Rng rng{used * 0x9E3779B97F4A7C15ull + board.size()};
  int need = 5 - (int)board.size();
  for (int t = 0; t < 600; t++) {
    for (int k = 0; k < 2 + need; k++) { int j = k + rng.below(nd - k); std::swap(deck[k], deck[j]); }
    pe::H rest = pe::E;
    for (int k = 0; k < need; k++) rest = pe::add(rest, deck[2 + k]);
    int v = pe::ev(pe::add(mine, rest)), w = pe::ev(pe::add(pe::add(pe::add(b, rest), deck[0]), deck[1]));
    win += v > w ? 1 : v == w ? 0.5 : 0; n++;
  }
  return win / n;
}

struct Decision {
  int sit = 0; unsigned mask = 0; double x[NF] = {};
  int street = 0, call = 0, pot = 0, stack = 0, min_bet = 0, n_opp = 0, alive = 0, bb = 0;
  double eff_bb = 0, own_bb = 0, s = 0;
};

// Features of the decision the tracker is at (after apply(), next_player == me).
inline Decision features(const pk::Tracker& t) {
  Decision d;
  const int me = t.me;
  const pk::Player& p = t.players[me];
  d.street = (int)t.board.size(); d.call = t.call_amount(p); d.pot = t.pot; d.stack = p.stack;
  bool preflop = d.street == 0, can_call = false, can_allin = false, can_bet = false;
  for (auto& a : t.possible_actions()) {
    if (a == "CALL") can_call = true;
    else if (a == "ALL-IN") can_allin = true;
    else if (a.rfind("BET_", 0) == 0) can_bet = true, d.min_bet = atoi(a.c_str() + 4);
  }
  int opp_max = 0;
  for (auto& q : t.players) if (q.id != me && !q.folded && !q.eliminated) d.n_opp++, opp_max = std::max(opp_max, q.stack + q.total);
  for (auto& q : t.players) d.alive += !q.eliminated;
  d.eff_bb = std::min(p.stack + p.total, opp_max) / (double)t.bb;
  d.own_bb = (p.stack + p.total) / (double)t.bb; d.bb = t.bb;
  d.s = strength(p.hand, t.board);
  bool vsjam = d.call > 0 && d.call >= d.stack;
  if (preflop) d.sit = vsjam ? P_VSJAM : t.last_raiser == -1 ? (d.call > 0 ? P_OPEN : P_BBOPT) : P_VSRAISE;
  else d.sit = d.call == 0 ? Q_CK : vsjam ? Q_VSJAM : Q_VSBET;
  d.mask = 1;
  if (can_call || (vsjam && can_allin)) d.mask |= 2;
  if (can_bet && !vsjam) d.mask |= 4;
  if (can_allin && !vsjam) d.mask |= 8;
  // this hand's actions so far (the log may still hold the previous hand's until this hand's first action)
  bool pf_aggr = false; int limpers = 0;
  for (auto& a : t.hand_log) {
    if (a.hand != t.hand_nb || a.street != 0) continue;
    if (a.type == pk::A_BET || a.type == pk::A_ALL_IN) pf_aggr = a.pid == me;
    if (a.type == pk::A_CALL && !a.raised && a.pid != me) limpers++;
  }
  bool ip = true;                                       // postflop: nobody who can still act acts after us
  if (!preflop && t.dealer_id >= 0) {
    int pos_me = 0;
    for (int i = 1; i <= t.n; i++) if ((t.dealer_id + i) % t.n == me) pos_me = i;
    for (int i = pos_me + 1; i <= t.n; i++) { const pk::Player& q = t.players[(t.dealer_id + i) % t.n]; if (!q.folded && !q.allin && !q.eliminated) ip = false; }
  }
  double s = d.s, ld = std::log(std::max(1.0, d.eff_bb)) / std::log(100.0), rc = std::min(3.0, d.call / (double)std::max(1, d.pot - d.call));
  double* x = d.x;
  x[0] = 1; x[1] = s; x[2] = s * s; x[3] = ld; x[4] = s * ld; x[5] = d.n_opp >= 2; x[8] = rc; x[11] = s * rc;
  if (preflop) { x[6] = me == t.sb_id && me != t.bb_id; x[7] = me == t.bb_id; x[9] = t.raise_nb >= 3; x[10] = limpers > 0; }
  else { x[6] = pf_aggr; x[7] = ip; x[9] = d.street == 4; x[10] = d.street == 5; }
  // hand shape, what rule-based bots key on: preflop pair / suited / ace / both broadway; postflop a pair made
  // with a hole card, two pair or better, a flush or straight draw, top pair or an overpair
  int h0 = p.hand[0], h1 = p.hand[1], r0 = h0 >> 2, r1 = h1 >> 2;
  if (preflop) { x[12] = r0 == r1; x[13] = (h0 & 3) == (h1 & 3); x[14] = r0 == 12 || r1 == 12; x[15] = r0 >= 8 && r1 >= 8; }
  else {
    unsigned bm = 0; int top = 0, suit_n[4] = {}, hole_suit_n[4] = {};
    for (int c : t.board) bm |= 1u << (c >> 2), top = std::max(top, c >> 2), suit_n[c & 3]++;
    suit_n[h0 & 3]++; suit_n[h1 & 3]++; hole_suit_n[h0 & 3]++; hole_suit_n[h1 & 3]++;
    x[12] = r0 == r1 || (bm >> r0 & 1) || (bm >> r1 & 1);
    pe::H all = pe::add(pe::add(pe::E, h0), h1); for (int c : t.board) all = pe::add(all, c);
    int v = pe::ev(all);
    x[13] = v >= 4138;
    bool draw = false;
    if (d.street < 5 && v < 5854) {
      for (int su = 0; su < 4; su++) draw |= suit_n[su] == 4 && hole_suit_n[su] > 0;
      unsigned m = bm | 1u << r0 | 1u << r1, hm = 1u << r0 | 1u << r1;
      m = m << 1 | (m >> 12 & 1); hm = hm << 1 | (hm >> 12 & 1);   // ace low too
      for (int lo = 0; lo + 4 <= 13; lo++) { unsigned w = 31u << lo; draw |= __builtin_popcount(m & w) == 4 && (hm & w); }
    }
    x[14] = draw;
    x[15] = (r0 == r1 && r0 > top) || r0 == top || r1 == top;
  }
  x[16] = d.alive == 2;                                 // the heads-up phase (many bots switch strategy there)
  x[17] = ld * ld;
  return d;
}

// Class of a shown (post-replacement) action at decision d; -1 for a timeout.  BET_x adds x chips.
inline int action_class(const Decision& d, const std::string& shown, double* size) {
  *size = -1;
  if (shown == "FOLD" || shown == "CHECK") return 0;
  if (shown == "CALL") return 1;
  if (shown == "ALL-IN") return d.call > 0 && d.call >= d.stack ? 1 : 3;
  if (shown.rfind("BET_", 0) == 0) {
    int x = atoi(shown.c_str() + 4);
    if (x >= d.stack) return 3;
    *size = (x - d.call) / (double)std::max(1, d.pot + d.call);
    return 2;
  }
  return -1;
}

struct Policy {
  std::string name; double weight = 0;
  double W[NSIT][NA][NF] = {};
  struct Size { double s, ld, v; };
  std::vector<Size> sizes[NSIT];
  int unit[NSIT] = {};                                  // 0 pot after calling, 1 big blind, 2 own chips
  // a recorded size made with strength and depth near (s, ld): uniform among the nearest tenth (at least 5)
  double draw_size(int sit, double s, double ld, Rng& rng) const {
    const std::vector<Size>& z = sizes[sit];
    std::vector<std::pair<double, int>> dist(z.size());
    for (size_t i = 0; i < z.size(); i++) dist[i] = {(z[i].s - s) * (z[i].s - s) + (z[i].ld - ld) * (z[i].ld - ld), (int)i};
    int k = std::min((int)z.size(), std::max(5, (int)z.size() / 10));
    std::nth_element(dist.begin(), dist.begin() + (k - 1), dist.end());
    return z[dist[rng.below(k)].second].v;
  }
  void probs(const Decision& d, double* pr) const {
    double z[NA], mx = -1e300;
    for (int a = 0; a < NA; a++) {
      if (!(d.mask >> a & 1)) continue;
      z[a] = 0; for (int f = 0; f < NF; f++) z[a] += W[d.sit][a][f] * d.x[f];
      mx = std::max(mx, z[a]);
    }
    double tot = 0;
    for (int a = 0; a < NA; a++) { pr[a] = d.mask >> a & 1 ? std::exp(z[a] - mx) : 0; tot += pr[a]; }
    for (int a = 0; a < NA; a++) pr[a] /= tot;
  }
};

// clones.txt (analysis/clone_fit.py): "P name weight", then "W sit a w0..w11" and "S sit v1 v2 ..." lines
inline std::map<std::string, Policy> load(const std::string& path) {
  std::map<std::string, Policy> out;
  std::ifstream in(path);
  std::string line; Policy* cur = nullptr;
  while (std::getline(in, line)) {
    std::istringstream is(line); std::string tag; is >> tag;
    if (tag == "P") { std::string name; double w; is >> name >> w; cur = &out[name]; cur->name = name; cur->weight = w; }
    else if (tag == "W" && cur) { int s, a; is >> s >> a; for (int f = 0; f < NF; f++) is >> cur->W[s][a][f]; }
    else if (tag == "S" && cur) { int s; is >> s >> cur->unit[s]; Policy::Size z; while (is >> z.s >> z.ld >> z.v) cur->sizes[s].push_back(z); }
  }
  return out;
}

// Plays a Policy.  Like the bot, it keeps a Tracker on the stdin snapshots.
struct CloneAgent : pk::Agent {
  const Policy* pol = nullptr; const Policy* field = nullptr;
  pk::Tracker tr; Rng rng{1};
  long counts[NSIT][NA] = {}; double psum[NSIT][NA] = {};   // implementation check: realised vs model probabilities
  bool act(const pk::Obs& o, std::string& out) override {
    tr.apply(o);
    Decision d = features(tr);
    double pr[NA]; pol->probs(d, pr);
    double u = rng.uni(); int a = 0;
    while (a < NA - 1 && (u >= pr[a] || !(d.mask >> a & 1))) { u -= pr[a]; a++; }
    while (!(d.mask >> a & 1)) a--;                     // rounding at the top end
    counts[d.sit][a]++; for (int k = 0; k < NA; k++) psum[d.sit][k] += pr[k];
    if (a == 0) out = d.call > 0 ? "FOLD" : "CHECK";
    else if (a == 1) out = d.call < d.stack ? "CALL" : "ALL-IN";
    else if (a == 3) out = "ALL-IN";
    else {
      const Policy* src = !pol->sizes[d.sit].empty() ? pol : field;
      double v = src->sizes[d.sit].empty() ? 0.6 : src->draw_size(d.sit, d.x[1], d.x[3], rng);
      int u = src->sizes[d.sit].empty() ? 0 : src->unit[d.sit];
      double unit = u == 1 ? d.bb : u == 2 ? d.own_bb * d.bb : d.pot + d.call;
      long amt = std::lround(v * unit);
      if (amt < d.min_bet) amt = d.min_bet;
      out = amt >= d.stack ? "ALL-IN" : "BET " + std::to_string(amt);
    }
    return true;
  }
};

}  // namespace clones
