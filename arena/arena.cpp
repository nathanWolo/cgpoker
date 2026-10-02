// arena.cpp - local arena on the C++ engine: dev bot against prev and scripted opponents, in
// process, multithreaded, with duplicate seeds and a paired dev-vs-prev comparison for SPRT.
//
//   arena [--games N] [--seed S] [--threads T] [--mix 41,34,25] [--opp station,jammer,random,prev]
//         [--mode eval|pair] [--variants dev,m1,...] [--dup] [--ms M | --trials K] [--log FILE]
//         [--sprt D1 [--alpha A --beta B]] [--clones data/clones/clones.txt] [--schedule FILE]
// Opponents: prev, station, jammer (35% shove), maniac (always shoves preflop), random, folder, bigbet, limper;
// clone:<player> (arena/clone.hpp: a live bot's fitted policy); clones (per seat, a clone drawn by how often we
// meet that player live, distinct players at a table); and the frozen versions m1, m21, m23, om1 when
// build/arena/frozen/ holds them (arena/freeze.py --commit ... --ns ...).
//
// eval: dev plays N games; each game draws its table size from --mix (4p,3p,2p) and its opponents
//       from --opp (round robin).  --dup plays every seat rotation of the same seed and lineup.
// pair: every game is also played with prev in dev's seat (same seed, same opponents, same
//       rotations); the metric is the payout difference, and --sprt D1 runs a Gaussian SPRT
//       (H0: mean 0, H1: mean D1 payout units) on the paired differences.
// --schedule FILE: game g takes its table size and opponents from line g mod L ("n name1 name2 ...", a live
//       run's tables from analysis/clone_fit.py schedule): each name plays as its clone, or as the field model
//       (clone:_field) when it has none.
// ARENA_HANDLOG=path: a per-hand ledger of the seat under test (regime, context, action, net big blinds).
// --variants a,b,c: every game is played once with each listed bot in dev's seat; each is reported, and
//       paired against the first (pair mode is --variants dev,prev).  ARENA_CLONECHECK=1 compares each
//       clone situation's realised action frequencies with its model's mean probabilities.
// Payout = TrueSkill-implied placement value: (1,0), (1,.5,0), (1,.6444,.3556,0); ties share.
// --ms gives the Monte Carlo a wall-clock budget (fidelity mode); --trials a fixed count (fast mode).
#include <algorithm>
#include <map>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "../cpp/pe7c.hpp"
#include "../bot/bot.hpp"
#include "../bot/bot_prev.hpp"
#if __has_include("../build/arena/frozen/m1.hpp")
#include "../build/arena/frozen/m1.hpp"
#define HAVE_M1 1
#endif
#if __has_include("../build/arena/frozen/m21.hpp")
#include "../build/arena/frozen/m21.hpp"
#define HAVE_M21 1
#endif
#if __has_include("../build/arena/frozen/m23.hpp")
#include "../build/arena/frozen/m23.hpp"
#define HAVE_M23 1
#endif
#if __has_include("../build/arena/frozen/om1.hpp")
#include "../build/arena/frozen/om1.hpp"
#define HAVE_OM1 1
#endif
#include "clone.hpp"

using Clock = std::chrono::steady_clock;

struct Rng {
  uint64_t x;
  uint64_t next() { uint64_t z = (x += 0x9E3779B97F4A7C15ull); z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; return z ^ (z >> 31); }
  int below(int n) { return (int)(next() % (uint64_t)n); }
  double uni() { return (next() >> 11) * (1.0 / 9007199254740992.0); }
};

// ----------------------------------------------------------------------------- agents
struct Stats { long decisions = 0; double ms_total = 0, ms_max = 0; long trials = 0; int desync = 0; long replaced = 0; std::map<std::string, long> tags;
               double clone_n[clones::NSIT][clones::NA] = {}, clone_p[clones::NSIT][clones::NA] = {}; };

template <class T> auto note_of(const T& b, int) -> decltype(b.last_note, std::string()) { return b.last_note; }   // dev has a model note
template <class T> std::string note_of(const T&, long) { return std::string(); }                                      // a frozen bot may not
template <class B, class Bud>
struct BotAgent : pk::Agent {
  B b; Bud budget; Stats* st; bool trace = false; std::string label = "dev";
  bool act(const pk::Obs& o, std::string& out) override {
    auto t0 = Clock::now();
    out = b.act(o, budget, t0);
    double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    if (trace) fprintf(stderr, "r%d h%d bb%d %s %s pot %d call %d stack %d eq %.3f %s -> %s\n", o.round, o.hand_nb, b.tr.bb, o.cards.c_str(), o.board.c_str(),
                       b.tr.pot, b.tr.call_amount(b.tr.players[b.tr.me]), o.stacks[o.player_id], b.last_equity, b.last_tag.c_str(), out.c_str());
    if (trace) { std::string nt = note_of(b, 0); if (!nt.empty()) fprintf(stderr, "    %s\n", nt.c_str()); }
    st->tags[label + ":" + b.last_tag]++;
    st->decisions++; st->ms_total += ms; st->ms_max = std::max(st->ms_max, ms); st->trials += b.last_trials; st->desync += b.tr.desynced;
    return true;
  }
};
struct Station : pk::Agent {                 // calls everything
  bool act(const pk::Obs&, std::string& out) override { out = "CALL"; return true; }
};
struct Jammer : pk::Agent {                  // preflop: all-in with probability q, else fold (checks when free); postflop: call
  Rng rng; double q;
  bool act(const pk::Obs& o, std::string& out) override {
    bool preflop = o.board[0] == 'X', can_check = false;
    for (auto& a : o.possible) can_check |= a == "CHECK";
    if (!preflop) out = "CALL";
    else if (rng.uni() < q) out = "ALL-IN";
    else out = can_check ? "CHECK" : "FOLD";
    return true;
  }
};
struct Maniac : pk::Agent {                  // shoves every hand preflop (what the lower league mostly is), calls postflop
  bool act(const pk::Obs& o, std::string& out) override {
    bool preflop = o.board[0] == 'X', can_allin = false;
    for (auto& a : o.possible) can_allin |= a == "ALL-IN";
    out = preflop && can_allin ? "ALL-IN" : "CALL"; return true;
  }
};
struct RandomLegal : pk::Agent {             // uniform over the offered actions (a BET_x is played as the minimum)
  Rng rng;
  bool act(const pk::Obs& o, std::string& out) override {
    out = o.possible[rng.below((int)o.possible.size())];
    if (out.compare(0, 4, "BET_") == 0) out[3] = ' ';
    return true;
  }
};
// blind level from the observation (the referee doubles the blinds every hand_nb_by_level hands)
static int bb_now(const pk::Obs& o) { int bb = o.big_blind; for (int h = 1; h + o.hand_nb_by_level <= o.hand_nb; h += o.hand_nb_by_level) bb *= o.level_mult; return bb; }
static int pct_of(const pk::Obs& o) {       // strength percentile of our hole cards (0 = best), -1 if unknown
  if (o.cards.size() != 5) return -1;
  int c0 = pk::card_from(o.cards.c_str()), c1 = pk::card_from(o.cards.c_str() + 3);
  return c0 < 0 || c1 < 0 ? -1 : pf::PF_PCT100[bot::hand_class(c0, c1)];
}
struct BigBet : pk::Agent {                  // raises big: opens to 8 BB with the top half, 3-bets 4x, overbets 1.5x pot postflop
  Rng rng;
  bool act(const pk::Obs& o, std::string& out) override {
    bool preflop = o.board[0] == 'X', can_check = false, can_raise = false, can_allin = false;
    for (auto& a : o.possible) { can_check |= a == "CHECK"; can_raise |= a.rfind("BET", 0) == 0; can_allin |= a == "ALL-IN"; }
    int bb = bb_now(o), me = o.player_id, my = o.chip_in_pot[me], mx = 0, pot = 0, stack = o.stacks[me];
    for (size_t i = 0; i < o.chip_in_pot.size(); i++) { mx = std::max(mx, o.chip_in_pot[i]); pot += o.chip_in_pot[i]; }
    int call = mx - my, pct = pct_of(o);
    if (preflop) {
      if (mx <= bb) {                                               // unopened
        if (pct >= 0 && pct <= 50 && can_raise) { out = "BET " + std::to_string(std::max(8 * bb - my, bb)); return true; }
        out = can_check ? "CHECK" : (pct >= 0 && pct <= 75 ? "CALL" : "FOLD"); return true;
      }
      if (pct >= 0 && pct <= 8 && can_allin) { out = "ALL-IN"; return true; }
      if (pct >= 0 && pct <= 20 && can_raise) { out = "BET " + std::to_string(4 * call); return true; }
      out = pct >= 0 && pct <= 40 ? "CALL" : (can_check ? "CHECK" : "FOLD"); return true;
    }
    if (call == 0) {
      if (rng.uni() < 0.45 && can_raise) { out = "BET " + std::to_string(std::max(3 * pot / 2, bb)); return true; }
      out = can_check ? "CHECK" : "CALL"; return true;
    }
    out = rng.uni() < 0.5 ? "CALL" : "FOLD"; return true;
  }
};
struct Limper : pk::Agent {                  // limps every hand, calls small bets, calls a shove with the top quarter
  bool act(const pk::Obs& o, std::string& out) override {
    bool can_check = false; for (auto& a : o.possible) can_check |= a == "CHECK";
    int me = o.player_id, my = o.chip_in_pot[me], mx = 0; for (int c : o.chip_in_pot) mx = std::max(mx, c);
    int call = mx - my, pct = pct_of(o);
    if (can_check) { out = "CHECK"; return true; }
    if (call >= o.stacks[me] / 2) { out = pct >= 0 && pct <= 25 ? "CALL" : "FOLD"; return true; }
    out = "CALL"; return true;
  }
};
struct Folder : pk::Agent {                  // checks when free, otherwise folds (a floor)
  bool act(const pk::Obs& o, std::string& out) override {
    bool can_check = false; for (auto& a : o.possible) can_check |= a == "CHECK";
    out = can_check ? "CHECK" : "FOLD"; return true;
  }
};

struct Opts {
  int games = 200, threads = 4; uint64_t seed = 1; int mix[3] = {41, 34, 25};
  std::vector<std::string> opp = {"station", "jammer", "random"};
  std::vector<std::string> variants = {"dev"};
  bool dup = false; double ms = 0; int trials = 20000; std::string log, clones = "data/clones/clones.txt", schedule;
  double sprt_d1 = 0, alpha = 0.05, beta = 0.05;
};
static std::map<std::string, clones::Policy> CLONES;      // loaded once, read-only while the games run

template <class T> auto set_seed0(T& b, uint64_t s, int) -> decltype(b.seed0, void()) { b.seed0 = s; }   // bots since M2.3 reseed per turn
template <class T> void set_seed0(T&, uint64_t, long) {}
template <class B, class Bud>
static pk::Agent* frozen_agent(const char* label, uint64_t seed, const Opts& o, Stats* st) {
  auto* a = new BotAgent<B, Bud>();
  a->budget.ms = o.ms; a->budget.max_trials = o.trials; a->budget.min_trials = o.ms > 0 ? 2000 : o.trials; a->st = st; a->b.rng.x = seed; a->label = label;
  set_seed0(a->b, seed, 0);
  return a;
}

// hyb:<player>:<regime>[+<regime>...]: the dev bot, except in the named regimes, where that player's clone decides
// (a causal test of where a policy gains: both see every decision, so both trackers stay in sync).  Regimes:
// pre3s / pre3d (preflop, 3-4 alive, effective stack <= / > 20 BB), post3 (postflop, 3-4 alive), and the same
// with 2 for heads-up; a regime with a clone situation appended (pre2s.open, pre2s.vsraise, ...) narrows it.
struct Hybrid : pk::Agent {
  pk::Agent* dev = nullptr; clones::CloneAgent* cl = nullptr; std::vector<std::string> regimes; long used = 0, total = 0;
  bot::Bot* b = nullptr;
  ~Hybrid() { delete dev; delete cl; }
  bool act(const pk::Obs& o, std::string& out) override {
    std::string a, c;
    dev->act(o, a); cl->act(o, c);
    const pk::Tracker& t = b->tr;
    int alive = 0, opp = 0; for (auto& p : t.players) if (p.stack + p.total > 0) { alive++; if (p.id != t.me && !p.folded) opp = std::max(opp, p.stack + p.total); }
    double eff = std::min(t.players[t.me].stack + t.players[t.me].total, opp) / (double)t.bb;
    std::string r = std::string(t.board.empty() ? "pre" : "post") + (alive >= 3 ? "3" : "2") + (t.board.empty() ? (eff <= 20 ? "s" : "d") : "");
    std::string r2 = r + "." + clones::sit_name(clones::features(cl->tr).sit);      // e.g. pre2s.open, pre2s.vsraise
    bool use = std::find(regimes.begin(), regimes.end(), r) != regimes.end() || std::find(regimes.begin(), regimes.end(), r2) != regimes.end();
    total++; used += use;
    out = use ? c : a;
    return true;
  }
};

// dev/key=value/...: the dev bot with parameters changed (the M3 preflop thresholds and switches)
static void set_param(bot::Bot& b, const std::string& k, double v) {
  if (k == "open") b.p_open = v; else if (k == "open_step") b.p_open_step = v; else if (k == "iso") b.p_iso = v;
  else if (k == "iso_bb") b.p_iso_bb = v; else if (k == "iso_limper") b.p_iso_limper = v; else if (k == "3bet") b.p_3bet = v;
  else if (k == "mwfold") b.mw_fold_all = v != 0; else if (k == "om") b.use_om = v != 0;
  else if (k == "hu_call") b.hu_call_prior = v; else if (k == "hu_callw") b.hu_callw_prior = v; else if (k == "hu_gate") b.hu_jam_gate = (int)v;
  else if (k == "jf") b.jamfold_max_bb = v; else if (k == "evjam") b.hu_evjam_bb = v; else if (k == "evjam_m") b.hu_evjam_margin = v;
  else { fprintf(stderr, "unknown dev parameter %s\n", k.c_str()); exit(2); }
}
static pk::Agent* make_agent(const std::string& name, uint64_t seed, const Opts& o, Stats* st) {
  if (name.rfind("hyb:", 0) == 0) {
    size_t c2 = name.find(':', 4);
    auto* h = new Hybrid();
    h->dev = make_agent("dev", seed, o, st);
    h->b = &static_cast<BotAgent<bot::Bot, bot::Budget>*>(h->dev)->b;
    h->cl = static_cast<clones::CloneAgent*>(make_agent("clone:" + name.substr(4, c2 - 4), seed ^ 0x5bd1e995, o, st));
    std::string rs = name.substr(c2 + 1); size_t p = 0;
    while (p <= rs.size()) { size_t q = rs.find('+', p); h->regimes.push_back(rs.substr(p, q == std::string::npos ? std::string::npos : q - p)); if (q == std::string::npos) break; p = q + 1; }
    return h;
  }
  if (name.rfind("dev/", 0) == 0) {
    auto* a = static_cast<BotAgent<bot::Bot, bot::Budget>*>(make_agent("dev", seed, o, st));
    a->label = name;
    size_t p = 4;
    while (p < name.size()) {
      size_t q = name.find('/', p); std::string kv = name.substr(p, q == std::string::npos ? std::string::npos : q - p);
      size_t eq = kv.find('='); if (eq == std::string::npos) { fprintf(stderr, "bad parameter %s\n", kv.c_str()); exit(2); }
      set_param(a->b, kv.substr(0, eq), atof(kv.c_str() + eq + 1));
      if (q == std::string::npos) break; p = q + 1;
    }
    return a;
  }
  if (name == "dev") {
    auto* a = new BotAgent<bot::Bot, bot::Budget>();
    a->budget.ms = o.ms; a->budget.max_trials = o.trials; a->budget.min_trials = o.ms > 0 ? 2000 : o.trials; a->st = st; a->b.rng.x = seed; a->b.seed0 = seed;
    a->trace = getenv("ARENA_TRACE") != nullptr;
    if (getenv("BOT_JF")) a->b.jamfold_max_bb = atof(getenv("BOT_JF"));
    if (getenv("BOT_SLOW")) a->b.use_fast = false;        // test the table-free evaluator path
    if (getenv("BOT_PFN")) a->b.pfn_max_bb = atof(getenv("BOT_PFN"));
    if (getenv("BOT_HU")) a->b.hu_max_bb = atof(getenv("BOT_HU"));
    if (getenv("BOT_HUPOST")) a->b.hu_postflop = atoi(getenv("BOT_HUPOST")) != 0;
    if (getenv("BOT_OM")) a->b.use_om = atoi(getenv("BOT_OM")) != 0;
    if (getenv("BOT_HUSHOVE")) a->b.hu_shove_bb = atof(getenv("BOT_HUSHOVE"));
    return a;
  }
  if (name == "prev") {
    return frozen_agent<prev::Bot, prev::Budget>("prev", seed, o, st);
  }
  if (name == "station") return new Station();
  if (name == "jammer") { auto* a = new Jammer(); a->rng = Rng{seed}; a->q = 0.35; return a; }
  if (name == "random") { auto* a = new RandomLegal(); a->rng = Rng{seed}; return a; }
  if (name == "maniac") return new Maniac();
  if (name == "folder") return new Folder();
  if (name == "bigbet") { auto* a = new BigBet(); a->rng = Rng{seed}; return a; }
  if (name == "limper") return new Limper();
  if (name.rfind("clone:", 0) == 0) {
    auto it = CLONES.find(name.substr(6));
    if (it == CLONES.end()) { fprintf(stderr, "no clone %s in %s\n", name.c_str(), o.clones.c_str()); exit(2); }
    auto* a = new clones::CloneAgent(); a->pol = &it->second; a->field = &CLONES["_field"]; a->rng.x = seed; return a;
  }
#ifdef HAVE_M1
  if (name == "m1") return frozen_agent<m1::Bot, m1::Budget>("m1", seed, o, st);
#endif
#ifdef HAVE_M21
  if (name == "m21") return frozen_agent<m21::Bot, m21::Budget>("m21", seed, o, st);
#endif
#ifdef HAVE_M23
  if (name == "m23") return frozen_agent<m23::Bot, m23::Budget>("m23", seed, o, st);
#endif
#ifdef HAVE_OM1
  if (name == "om1") return frozen_agent<om1::Bot, om1::Budget>("om1", seed, o, st);
#endif
  fprintf(stderr, "unknown agent %s\n", name.c_str()); exit(2);
}

static std::vector<double> payouts(const std::vector<int>& scores) {
  int n = (int)scores.size();
  static const double pay2[] = {1, 0}, pay3[] = {1, .5, 0}, pay4[] = {1, .6444, .3556, 0};
  const double* pay = n == 2 ? pay2 : n == 3 ? pay3 : pay4;
  std::vector<double> out(n);
  for (int i = 0; i < n; i++) {
    int better = 0, equal = 0;
    for (int j = 0; j < n; j++) better += scores[j] > scores[i], equal += scores[j] == scores[i];
    double s = 0; for (int k = better; k < better + equal; k++) s += pay[k];
    out[i] = s / equal;
  }
  return out;
}

struct GameSpec { int idx; int n; int64_t seed; std::vector<std::string> lineup; int dev_seat; int rotation; };
struct GameResult { GameSpec spec; std::vector<int> scores; std::vector<double> pay; int hands, rounds; bool cancelled; std::string variant; std::string ledger; };

// Per-hand ledger of one seat (ARENA_HANDLOG): the regime at hand start, what happened before the seat's first
// preflop action, that action, how the hand went for it, and its net chips in big blinds.  One TSV line per hand:
// variant game rot n hand alive eff_bb pos context first outcome n_flop net_bb
static int street_of(const std::string& board) { int k = 0; size_t i = 0; while (i < board.size()) { size_t j = board.find('_', i); if (board.compare(i, (j == std::string::npos ? board.size() : j) - i, "X") != 0) k++; if (j == std::string::npos) break; i = j + 1; } return k; }
static std::string hand_ledger(const pk::Engine& eng, int seat, const std::vector<int>& final_stacks, const std::string& variant, int game, int rot) {
  std::string out;
  std::map<int, std::vector<const pk::LogEntry*>> by_hand;
  for (auto& e : eng.log) by_hand[e.hand].push_back(&e);
  const auto& hs = eng.hand_starts;
  for (size_t i = 0; i < hs.size(); i++) {
    const auto& h = hs[i];
    int n = (int)h.chips.size(), mine = h.chips[seat];
    if (mine <= 0) continue;
    int next = i + 1 < hs.size() ? hs[i + 1].chips[seat] : final_stacks[seat];
    int alive = 0, opp_max = 0;
    for (int s = 0; s < n; s++) if (h.chips[s] > 0) { alive++; if (s != seat) opp_max = std::max(opp_max, h.chips[s]); }
    double eff = std::min(mine, opp_max) / (double)h.bb;
    const char* pos = seat == h.bb_id ? "BB" : seat == h.sb_id ? "SB" : seat == h.dealer_id ? "BTN" : "UTG";
    std::string ctx = "unopened", first = "-";
    int folded_pre = 0; bool i_folded_pre = false, acted = false;
    for (auto* e : by_hand[h.hand]) {
      int street = street_of(eng.round_infos[e->turn].board);
      if (street != 0) continue;
      std::string a = e->shown.substr(0, e->shown.find('_'));
      if (a == "FOLD") { folded_pre++; if (e->pid == seat) i_folded_pre = true; }
      if (e->pid == seat) { if (!acted) first = a, acted = true; continue; }
      if (!acted) { if (a == "BET" || a == "ALL-IN") ctx = "raised"; else if (a == "CALL" && ctx == "unopened") ctx = "limped"; }
    }
    const char* outcome = i_folded_pre ? "fold-pre" : folded_pre >= alive - 1 ? "won-pre" : "flop";
    char buf[256];
    snprintf(buf, sizeof buf, "%s\t%d\t%d\t%d\t%d\t%d\t%.1f\t%s\t%s\t%s\t%s\t%d\t%.2f\n", variant.c_str(), game, rot, n, h.hand, alive, eff, pos,
             ctx.c_str(), first.c_str(), outcome, alive - folded_pre, (next - mine) / (double)h.bb);
    out += buf;
  }
  return out;
}

int main(int argc, char** argv) {
  Opts o;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto next = [&]() { return std::string(argv[++i]); };
    if (a == "--games") o.games = atoi(next().c_str());
    else if (a == "--seed") o.seed = strtoull(next().c_str(), nullptr, 10);
    else if (a == "--threads") o.threads = atoi(next().c_str());
    else if (a == "--mix") sscanf(next().c_str(), "%d,%d,%d", &o.mix[0], &o.mix[1], &o.mix[2]);
    else if (a == "--opp") { o.opp.clear(); std::string s = next(); size_t p = 0; while (p <= s.size()) { size_t q = s.find(',', p); o.opp.push_back(s.substr(p, q == std::string::npos ? std::string::npos : q - p)); if (q == std::string::npos) break; p = q + 1; } }
    else if (a == "--mode") { if (next() == "pair") o.variants = {"dev", "prev"}; }
    else if (a == "--variants") { o.variants.clear(); std::string s = next(); size_t p = 0; while (p <= s.size()) { size_t q = s.find(',', p); o.variants.push_back(s.substr(p, q == std::string::npos ? std::string::npos : q - p)); if (q == std::string::npos) break; p = q + 1; } }
    else if (a == "--clones") o.clones = next();
    else if (a == "--schedule") o.schedule = next();
    else if (a == "--dup") o.dup = true;
    else if (a == "--ms") o.ms = atof(next().c_str());
    else if (a == "--trials") o.trials = atoi(next().c_str());
    else if (a == "--log") o.log = next();
    else if (a == "--sprt") o.sprt_d1 = atof(next().c_str());
    else if (a == "--alpha") o.alpha = atof(next().c_str());
    else if (a == "--beta") o.beta = atof(next().c_str());
    else { fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
  }
  pe::init();
  std::vector<std::vector<std::string>> sched;                // --schedule: per line, the opponents
  if (!o.schedule.empty()) {
    std::ifstream in(o.schedule); std::string line;
    while (std::getline(in, line)) { std::istringstream is(line); int n; std::vector<std::string> v; std::string x; if (!(is >> n)) continue; while (is >> x) v.push_back(x); if ((int)v.size() == n - 1) sched.push_back(v); }
    if (sched.empty()) { fprintf(stderr, "empty schedule %s\n", o.schedule.c_str()); return 2; }
  }
  bool want_clones = !sched.empty();
  for (auto& x : o.opp) want_clones |= x == "clones" || x.rfind("clone:", 0) == 0;
  for (auto& x : o.variants) want_clones |= x.rfind("clone:", 0) == 0 || x.rfind("hyb:", 0) == 0;
  std::vector<std::pair<std::string, double>> pool;          // clones drawn for "clones", by live-encounter weight
  if (want_clones) {
    CLONES = clones::load(o.clones);
    if (!CLONES.count("_field")) { fprintf(stderr, "no clones in %s (analysis/clone_fit.py fit)\n", o.clones.c_str()); return 2; }
    for (auto& kv : CLONES) if (kv.first != "_field" && kv.second.weight > 0) pool.push_back({kv.first, kv.second.weight});
  }
  // base games
  // the game generator's stream: seed times a constant that is not the generator's own increment (with the increment,
  // seed S + 1 was seed S's stream shifted by one draw, and nearby seeds replayed mostly the same games)
  Rng meta{o.seed * 0xD1B54A32D192ED03ull + 0x632BE59BD9B4E019ull};
  meta.next(); meta.next();
  std::vector<GameSpec> specs;
  int opp_rr = 0;
  for (int g = 0; g < o.games; g++) {
    int r = meta.below(o.mix[0] + o.mix[1] + o.mix[2]);
    int n = r < o.mix[0] ? 4 : r < o.mix[0] + o.mix[1] ? 3 : 2;
    const std::vector<std::string>* row = sched.empty() ? nullptr : &sched[g % sched.size()];
    if (row) n = (int)row->size() + 1;
    int64_t seed = (int64_t)meta.next();
    int dev_seat = meta.below(n);
    std::vector<std::string> lineup(n);
    std::vector<std::string> drawn;                            // distinct clones at one table
    for (int s = 0; s < n; s++) {
      if (s == dev_seat) { lineup[s] = "dev"; continue; }
      if (row) { const std::string& nm = (*row)[s < dev_seat ? s : s - 1]; lineup[s] = "clone:" + (CLONES.count(nm) ? nm : std::string("_field")); continue; }
      lineup[s] = o.opp[opp_rr++ % o.opp.size()];
      if (lineup[s] == "clones") {
        double tot = 0; for (auto& c : pool) if (std::find(drawn.begin(), drawn.end(), c.first) == drawn.end()) tot += c.second;
        double u = meta.uni() * tot; std::string pick;
        for (auto& c : pool) if (std::find(drawn.begin(), drawn.end(), c.first) == drawn.end()) { pick = c.first; if ((u -= c.second) < 0) break; }
        drawn.push_back(pick); lineup[s] = "clone:" + pick;
      }
    }
    int rots = o.dup ? n : 1;
    for (int rot = 0; rot < rots; rot++) {
      GameSpec sp{g, n, seed, {}, 0, rot};
      sp.lineup.resize(n);
      for (int s = 0; s < n; s++) sp.lineup[(s + rot) % n] = lineup[s];
      sp.dev_seat = (dev_seat + rot) % n;
      specs.push_back(sp);
    }
  }
  // work items: (spec index, variant)
  struct Item { int spec; std::string variant; };
  std::vector<Item> items;
  for (int i = 0; i < (int)specs.size(); i++) for (auto& v : o.variants) items.push_back({i, v});
  std::vector<GameResult> results(items.size());
  std::vector<Stats> stats(o.threads);
  std::atomic<int> next_item{0};
  auto worker = [&](int tid) {
    for (;;) {
      int k = next_item.fetch_add(1);
      if (k >= (int)items.size()) break;
      const GameSpec& sp = specs[items[k].spec];
      std::vector<pk::Agent*> agents;
      for (int s = 0; s < sp.n; s++) {
        std::string name = sp.lineup[s];
        if (name == "dev") name = items[k].variant;
        agents.push_back(make_agent(name, (uint64_t)sp.seed * 31 + s * 7 + 1, o, &stats[tid]));
      }
      pk::Engine eng(sp.n, sp.seed);
      GameResult gr; gr.spec = sp; gr.variant = items[k].variant;
      try {
        auto r = eng.run(agents);
        gr.scores = r.scores; gr.hands = r.hands; gr.rounds = r.rounds; gr.cancelled = r.cancelled;
        if (getenv("ARENA_HANDLOG")) gr.ledger = hand_ledger(eng, sp.dev_seat, r.stacks, items[k].variant, sp.idx, sp.rotation);
        stats[tid].replaced += eng.replaced[sp.dev_seat];
        if (eng.replaced[sp.dev_seat] && getenv("ARENA_DEBUG"))
          for (auto& l : eng.replaced_log) if (atoi(l.c_str()) == sp.dev_seat) fprintf(stderr, "replaced %s\n", l.c_str());
      } catch (std::exception& e) {                      // NONE round at 601: count as cancelled with current stacks
        gr.scores.assign(sp.n, 0); for (int s = 0; s < sp.n; s++) gr.scores[s] = eng.players[s].stack + eng.players[s].total;
        gr.hands = eng.hand_nb; gr.rounds = eng.turn; gr.cancelled = true;
      }
      gr.pay = payouts(gr.scores);
      results[k] = gr;
      for (auto* a : agents) {
        if (auto* c = dynamic_cast<clones::CloneAgent*>(a))
          for (int si = 0; si < clones::NSIT; si++) for (int ai = 0; ai < clones::NA; ai++) stats[tid].clone_n[si][ai] += c->counts[si][ai], stats[tid].clone_p[si][ai] += c->psum[si][ai];
        delete a;
      }
    }
  };
  auto t0 = Clock::now();
  std::vector<std::thread> th;
  for (int t = 0; t < o.threads; t++) th.emplace_back(worker, t);
  for (auto& t : th) t.join();
  double secs = std::chrono::duration<double>(Clock::now() - t0).count();

  // ---- report
  std::ofstream log;
  if (!o.log.empty()) log.open(o.log);
  const std::string& v0 = o.variants[0];
  std::map<std::string, std::map<int, double>> pay_by;         // variant -> spec index -> payout
  std::map<std::string, std::map<int, std::pair<double, int>>> by_n;
  std::map<std::string, std::pair<double, int>> first;
  std::map<std::string, std::pair<long, long>> ahead;          // opponent name -> (variants[0] ahead, comparisons)
  int cancelled = 0;
  for (size_t k = 0; k < results.size(); k++) {
    const GameResult& g = results[k];
    int ds = g.spec.dev_seat;
    if (log.is_open()) {
      log << g.variant << " g" << g.spec.idx << " rot" << g.spec.rotation << " n" << g.spec.n << " seed " << g.spec.seed << " lineup";
      for (auto& l : g.spec.lineup) log << " " << l;
      log << " scores"; for (int s : g.scores) log << " " << s;
      log << " pay"; for (double p : g.pay) log << " " << p;
      log << " hands " << g.hands << " rounds " << g.rounds << (g.cancelled ? " CANCELLED" : "") << "\n";
    }
    pay_by[g.variant][items[k].spec] = g.pay[ds];
    by_n[g.variant][g.spec.n].first += g.pay[ds]; by_n[g.variant][g.spec.n].second++;
    first[g.variant].first += g.pay[ds] == 1.0; first[g.variant].second++;
    if (g.variant == v0) {
      cancelled += g.cancelled;
      for (int s = 0; s < g.spec.n; s++) if (s != ds) {
        auto& a = ahead[g.spec.lineup[s]];
        a.second++; a.first += g.scores[ds] > g.scores[s] ? 2 : g.scores[ds] == g.scores[s] ? 1 : 0;
      }
    }
  }
  if (const char* hl = getenv("ARENA_HANDLOG")) {
    std::ofstream f(hl);
    f << "variant\tgame\trot\tn\thand\talive\teff_bb\tpos\tcontext\tfirst\toutcome\tn_flop\tnet_bb\n";
    for (auto& g : results) f << g.ledger;
  }
  Stats st; for (auto& s : stats) { st.decisions += s.decisions; st.ms_total += s.ms_total; st.ms_max = std::max(st.ms_max, s.ms_max); st.trials += s.trials; st.desync += s.desync; st.replaced += s.replaced; for (auto& kv : s.tags) st.tags[kv.first] += kv.second;
                                    for (int si = 0; si < clones::NSIT; si++) for (int ai = 0; ai < clones::NA; ai++) st.clone_n[si][ai] += s.clone_n[si][ai], st.clone_p[si][ai] += s.clone_p[si][ai]; }
  printf("arena: %d games (%zu played incl. rotations x %zu variants) in %.1fs on %d threads; %.1f games/s; bot decisions %ld, mean %.2f ms, max %.2f ms, mean trials %.0f, desyncs %d, replaced actions %ld, cancelled %d\n",
         o.games, results.size() / o.variants.size(), o.variants.size(), secs, o.threads, results.size() / secs, st.decisions,
         st.decisions ? st.ms_total / st.decisions : 0, st.ms_max, st.decisions ? (double)st.trials / st.decisions : 0, st.desync, st.replaced, cancelled);
  if (getenv("ARENA_TAGS")) {                      // decision-tag histogram per bot
    std::map<std::string, long> tot; for (auto& kv : st.tags) tot[kv.first.substr(0, kv.first.find(':'))] += kv.second;
    for (auto& kv : st.tags) printf("  tag %-16s %8ld  %5.1f%%\n", kv.first.c_str(), kv.second, 100.0 * kv.second / tot[kv.first.substr(0, kv.first.find(':'))]);
  }
  if (getenv("ARENA_CLONECHECK")) {                // implementation check: realised clone actions vs the model's probabilities
    for (int si = 0; si < clones::NSIT; si++) {
      double n = 0; for (int ai = 0; ai < clones::NA; ai++) n += st.clone_n[si][ai];
      if (!n) continue;
      printf("  clone %-11s n=%7.0f  realised/model:", clones::sit_name(si), n);
      for (int ai = 0; ai < clones::NA; ai++) printf("  %.3f/%.3f", st.clone_n[si][ai] / n, st.clone_p[si][ai] / n);
      printf("\n");
    }
  }
  for (auto& v : o.variants) {
    double sum = 0; int cnt = 0; for (auto& kv : pay_by[v]) sum += kv.second, cnt++;
    printf("%s: mean payout %.4f over %d games (first place %.1f%%)", v.c_str(), sum / cnt, cnt, 100.0 * first[v].first / first[v].second);
    for (auto& kv : by_n[v]) printf("; %dp %.4f (n=%d)", kv.first, kv.second.first / kv.second.second, kv.second.second);
    printf("\n");
  }
  for (auto& kv : ahead) {
    double p = 0.5 * kv.second.first / kv.second.second, se = std::sqrt(p * (1 - p) / kv.second.second);
    printf("%s finishes ahead of %-8s %.3f +- %.3f (n=%ld)\n", v0.c_str(), kv.first.c_str(), p, se, kv.second.second);
  }
  for (size_t vi = 1; vi < o.variants.size(); vi++) {
    const std::string& v1 = o.variants[vi];
    for (int nn : {0, 2, 3, 4}) {                  // 0 = all table sizes
      std::vector<double> d;
      for (auto& kv : pay_by[v0]) if (pay_by[v1].count(kv.first) && (nn == 0 || specs[kv.first].n == nn)) d.push_back(kv.second - pay_by[v1][kv.first]);
      if (d.size() < 2) continue;
      double n = d.size(), mean = 0, var = 0;
      for (double x : d) mean += x; mean /= n;
      for (double x : d) var += (x - mean) * (x - mean); var /= std::max(1.0, n - 1);
      double se = std::sqrt(var / n);
      printf("paired %s-%s%s: mean payout diff %+.4f +- %.4f (n=%.0f, sd %.3f)\n", v0.c_str(), v1.c_str(), nn ? (" " + std::to_string(nn) + "p").c_str() : "", mean, se, n, std::sqrt(var));
      if (nn == 0 && o.sprt_d1 > 0 && o.variants.size() == 2) {
        double d1 = o.sprt_d1, llr = var > 0 ? n * (mean * d1 - d1 * d1 / 2) / var : 0;   // Gaussian SPRT, H0 mean 0 vs H1 mean d1
        double la = std::log(o.beta / (1 - o.alpha)), lb = std::log((1 - o.beta) / o.alpha);
        printf("SPRT H1=%+.3f: LLR %.2f (bounds %.2f, %.2f) -> %s\n", d1, llr, la, lb, llr >= lb ? "PASS" : llr <= la ? "FAIL" : "continue");
      }
    }
  }
  return 0;
}
