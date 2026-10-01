// arena.cpp - local arena on the C++ engine: dev bot against prev and scripted opponents, in
// process, multithreaded, with duplicate seeds and a paired dev-vs-prev comparison for SPRT.
//
//   arena [--games N] [--seed S] [--threads T] [--mix 41,34,25] [--opp station,jammer,random,prev]
//         [--mode eval|pair] [--dup] [--ms M | --trials K] [--log FILE] [--sprt D1 [--alpha A --beta B]]
// Opponents: prev, station, jammer (35% shove), maniac (always shoves preflop), random, folder.
//
// eval: dev plays N games; each game draws its table size from --mix (4p,3p,2p) and its opponents
//       from --opp (round robin).  --dup plays every seat rotation of the same seed and lineup.
// pair: every game is also played with prev in dev's seat (same seed, same opponents, same
//       rotations); the metric is the payout difference, and --sprt D1 runs a Gaussian SPRT
//       (H0: mean 0, H1: mean D1 payout units) on the paired differences.
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
#include <string>
#include <thread>
#include <vector>

#include "../cpp/pe7c.hpp"
#include "../bot/bot.hpp"
#include "../bot/bot_prev.hpp"

using Clock = std::chrono::steady_clock;

struct Rng {
  uint64_t x;
  uint64_t next() { uint64_t z = (x += 0x9E3779B97F4A7C15ull); z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; return z ^ (z >> 31); }
  int below(int n) { return (int)(next() % (uint64_t)n); }
  double uni() { return (next() >> 11) * (1.0 / 9007199254740992.0); }
};

// ----------------------------------------------------------------------------- agents
struct Stats { long decisions = 0; double ms_total = 0, ms_max = 0; long trials = 0; int desync = 0; long replaced = 0; std::map<std::string, long> tags; };

template <class B, class Bud>
struct BotAgent : pk::Agent {
  B b; Bud budget; Stats* st; bool trace = false; const char* label = "dev";
  bool act(const pk::Obs& o, std::string& out) override {
    auto t0 = Clock::now();
    out = b.act(o, budget, t0);
    double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    if (trace) fprintf(stderr, "r%d h%d bb%d %s %s pot %d call %d stack %d eq %.3f %s -> %s\n", o.round, o.hand_nb, b.tr.bb, o.cards.c_str(), o.board.c_str(),
                       b.tr.pot, b.tr.call_amount(b.tr.players[b.tr.me]), o.stacks[o.player_id], b.last_equity, b.last_tag.c_str(), out.c_str());
    st->tags[std::string(label) + ":" + b.last_tag]++;
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
  bool pair = false, dup = false; double ms = 0; int trials = 20000; std::string log;
  double sprt_d1 = 0, alpha = 0.05, beta = 0.05;
};

static pk::Agent* make_agent(const std::string& name, uint64_t seed, const Opts& o, Stats* st) {
  if (name == "dev") {
    auto* a = new BotAgent<bot::Bot, bot::Budget>();
    a->budget.ms = o.ms; a->budget.max_trials = o.trials; a->budget.min_trials = o.ms > 0 ? 2000 : o.trials; a->st = st; a->b.rng.x = seed; a->b.seed0 = seed;
    a->trace = getenv("ARENA_TRACE") != nullptr;
    if (getenv("BOT_JF")) a->b.jamfold_max_bb = atof(getenv("BOT_JF"));
    if (getenv("BOT_SLOW")) a->b.use_fast = false;        // test the table-free evaluator path
    if (getenv("BOT_PFN")) a->b.pfn_max_bb = atof(getenv("BOT_PFN"));
    if (getenv("BOT_HU")) a->b.hu_max_bb = atof(getenv("BOT_HU"));
    if (getenv("BOT_HUPOST")) a->b.hu_postflop = atoi(getenv("BOT_HUPOST")) != 0;
    if (getenv("BOT_HUSHOVE")) a->b.hu_shove_bb = atof(getenv("BOT_HUSHOVE"));
    return a;
  }
  if (name == "prev") {
    auto* a = new BotAgent<prev::Bot, prev::Budget>();
    a->budget.ms = o.ms; a->budget.max_trials = o.trials; a->budget.min_trials = o.ms > 0 ? 2000 : o.trials; a->st = st; a->b.rng.x = seed; a->label = "prev"; return a;
  }
  if (name == "station") return new Station();
  if (name == "jammer") { auto* a = new Jammer(); a->rng = Rng{seed}; a->q = 0.35; return a; }
  if (name == "random") { auto* a = new RandomLegal(); a->rng = Rng{seed}; return a; }
  if (name == "maniac") return new Maniac();
  if (name == "folder") return new Folder();
  if (name == "bigbet") { auto* a = new BigBet(); a->rng = Rng{seed}; return a; }
  if (name == "limper") return new Limper();
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
struct GameResult { GameSpec spec; std::vector<int> scores; std::vector<double> pay; int hands, rounds; bool cancelled; std::string variant; };

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
    else if (a == "--mode") o.pair = next() == "pair";
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
  // base games
  Rng meta{o.seed * 0x9E3779B97F4A7C15ull + 17};
  std::vector<GameSpec> specs;
  int opp_rr = 0;
  for (int g = 0; g < o.games; g++) {
    int r = meta.below(o.mix[0] + o.mix[1] + o.mix[2]);
    int n = r < o.mix[0] ? 4 : r < o.mix[0] + o.mix[1] ? 3 : 2;
    int64_t seed = (int64_t)meta.next();
    int dev_seat = meta.below(n);
    std::vector<std::string> lineup(n);
    for (int s = 0; s < n; s++) lineup[s] = s == dev_seat ? "dev" : o.opp[opp_rr++ % o.opp.size()];
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
  for (int i = 0; i < (int)specs.size(); i++) { items.push_back({i, "dev"}); if (o.pair) items.push_back({i, "prev"}); }
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
        if (name == "dev" && items[k].variant == "prev") name = "prev";
        agents.push_back(make_agent(name, (uint64_t)sp.seed * 31 + s * 7 + 1, o, &stats[tid]));
      }
      pk::Engine eng(sp.n, sp.seed);
      GameResult gr; gr.spec = sp; gr.variant = items[k].variant;
      try {
        auto r = eng.run(agents);
        gr.scores = r.scores; gr.hands = r.hands; gr.rounds = r.rounds; gr.cancelled = r.cancelled;
        stats[tid].replaced += eng.replaced[sp.dev_seat];
        if (eng.replaced[sp.dev_seat] && getenv("ARENA_DEBUG"))
          for (auto& l : eng.replaced_log) if (atoi(l.c_str()) == sp.dev_seat) fprintf(stderr, "replaced %s\n", l.c_str());
      } catch (std::exception& e) {                      // NONE round at 601: count as cancelled with current stacks
        gr.scores.assign(sp.n, 0); for (int s = 0; s < sp.n; s++) gr.scores[s] = eng.players[s].stack + eng.players[s].total;
        gr.hands = eng.hand_nb; gr.rounds = eng.turn; gr.cancelled = true;
      }
      gr.pay = payouts(gr.scores);
      results[k] = gr;
      for (auto* a : agents) delete a;
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
  double dev_pay = 0; int dev_games = 0, dev_first = 0, cancelled = 0;
  std::map<std::string, std::pair<long, long>> ahead;      // opponent name -> (dev ahead, comparisons)
  std::map<int, std::pair<double, int>> by_n;
  std::map<int, double> pair_dev, pair_prev;               // spec index -> payout
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
    if (g.variant == "dev") {
      dev_pay += g.pay[ds]; dev_games++; dev_first += g.pay[ds] == 1.0; cancelled += g.cancelled;
      by_n[g.spec.n].first += g.pay[ds]; by_n[g.spec.n].second++;
      for (int s = 0; s < g.spec.n; s++) if (s != ds) {
        auto& a = ahead[g.spec.lineup[s]];
        a.second++; a.first += g.scores[ds] > g.scores[s] ? 2 : g.scores[ds] == g.scores[s] ? 1 : 0;
      }
      pair_dev[(int)(items[k].spec)] = g.pay[ds];
    } else pair_prev[(int)(items[k].spec)] = g.pay[ds];
  }
  Stats st; for (auto& s : stats) { st.decisions += s.decisions; st.ms_total += s.ms_total; st.ms_max = std::max(st.ms_max, s.ms_max); st.trials += s.trials; st.desync += s.desync; st.replaced += s.replaced; for (auto& kv : s.tags) st.tags[kv.first] += kv.second; }
  printf("arena: %d games (%zu played incl. rotations%s) in %.1fs on %d threads; %.1f games/s; bot decisions %ld, mean %.2f ms, max %.2f ms, mean trials %.0f, desyncs %d, replaced actions %ld, cancelled %d\n",
         o.games, results.size(), o.pair ? " x2 variants" : "", secs, o.threads, results.size() / secs, st.decisions,
         st.decisions ? st.ms_total / st.decisions : 0, st.ms_max, st.decisions ? (double)st.trials / st.decisions : 0, st.desync, st.replaced, cancelled);
  if (getenv("ARENA_TAGS")) {                      // decision-tag histogram per bot (dev / prev)
    std::map<std::string, long> tot; for (auto& kv : st.tags) tot[kv.first.substr(0, kv.first.find(':'))] += kv.second;
    for (auto& kv : st.tags) printf("  tag %-16s %8ld  %5.1f%%\n", kv.first.c_str(), kv.second, 100.0 * kv.second / tot[kv.first.substr(0, kv.first.find(':'))]);
  }
  printf("dev: mean payout %.4f over %d games (first place %.1f%%)", dev_pay / dev_games, dev_games, 100.0 * dev_first / dev_games);
  for (auto& kv : by_n) printf("; %dp %.4f (n=%d)", kv.first, kv.second.first / kv.second.second, kv.second.second);
  printf("\n");
  for (auto& kv : ahead) {
    double p = 0.5 * kv.second.first / kv.second.second, se = std::sqrt(p * (1 - p) / kv.second.second);
    printf("dev finishes ahead of %-8s %.3f +- %.3f (n=%ld)\n", kv.first.c_str(), p, se, kv.second.second);
  }
  if (o.pair) {
    std::vector<double> d;
    for (auto& kv : pair_dev) if (pair_prev.count(kv.first)) d.push_back(kv.second - pair_prev[kv.first]);
    double n = d.size(), mean = 0, var = 0;
    for (double x : d) mean += x; mean /= n;
    for (double x : d) var += (x - mean) * (x - mean); var /= std::max(1.0, n - 1);
    double se = std::sqrt(var / n);
    printf("paired dev-prev: mean payout diff %+.4f +- %.4f (n=%.0f, sd %.3f)\n", mean, se, n, std::sqrt(var));
    if (o.sprt_d1 > 0) {
      double d1 = o.sprt_d1, llr = var > 0 ? n * (mean * d1 - d1 * d1 / 2) / var : 0;   // Gaussian SPRT, H0 mean 0 vs H1 mean d1
      double la = std::log(o.beta / (1 - o.alpha)), lb = std::log((1 - o.beta) / o.alpha);
      printf("SPRT H1=%+.3f: LLR %.2f (bounds %.2f, %.2f) -> %s\n", d1, llr, la, lb, llr >= lb ? "PASS" : llr <= la ? "FAIL" : "continue");
    }
  }
  return 0;
}
