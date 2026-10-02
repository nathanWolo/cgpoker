// clone_data.cpp - training data for the arena clones (arena/clone.hpp): replays the recorded games through the
// engine, each seat with a Tracker fed its own stdin snapshots as the bot has, and writes one row per decision
// with the clone features and the action the player took (after the referee's replacement).
//
//   clone_data <games.log> <out.tsv>      games.log from analysis/clone_fit.py dump:
//       G <gameId> <n> <seed> / N <seat> <pseudo> / A <seat> <raw output, escaped> or A <seat> \0 / R <scores>
// A game whose turn order or final scores differ from the record is reported and skipped.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "../engine/poker_engine.hpp"
#include "../engine/tracker.hpp"
#include "clone.hpp"

static std::string unesc(const std::string& s) {
  std::string o;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\\' && i + 1 < s.size()) { char n = s[++i]; o += n == 't' ? '\t' : n == 'n' ? '\n' : n == 'r' ? '\r' : n; }
    else o += s[i];
  }
  return o;
}

struct Game { std::string id; int n = 0; int64_t seed = 0; std::vector<std::string> names; std::vector<std::pair<int, std::string>> acts; std::vector<int> scores; };

struct Rec { int seat, hand; clones::Decision d; };

struct DataAgent : pk::Agent {
  int seat; std::vector<std::pair<int, std::string>>* q; size_t* pos; bool* order_ok; std::vector<Rec>* recs;
  pk::Tracker tr;
  bool act(const pk::Obs& o, std::string& out) override {
    tr.apply(o);
    recs->push_back({seat, o.hand_nb, clones::features(tr)});
    if (*pos >= q->size()) { *order_ok = false; out = "FOLD"; return true; }
    auto& e = (*q)[(*pos)++];
    if (e.first != seat) *order_ok = false;
    if (e.second == "\\0") return false;
    out = unesc(e.second);
    return true;
  }
};

int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: clone_data <games.log> <out.tsv>\n"); return 2; }
  pe::init();
  std::ifstream in(argv[1]);
  std::vector<Game> games;
  std::string line;
  while (std::getline(in, line)) {
    if (line.size() < 2) continue;
    char tag = line[0]; std::string rest = line.substr(2);
    if (tag == 'G') { games.emplace_back(); Game& g = games.back(); char id[64]; long long seed; sscanf(rest.c_str(), "%63s %d %lld", id, &g.n, &seed); g.id = id; g.seed = seed; g.names.assign(g.n, "?"); }
    else if (tag == 'N') { int s = atoi(rest.c_str()); games.back().names[s] = rest.substr(rest.find(' ') + 1); }
    else if (tag == 'A') { int s = atoi(rest.c_str()); games.back().acts.push_back({s, rest.substr(rest.find(' ') + 1)}); }
    else if (tag == 'R') { std::istringstream is(rest); int v; while (is >> v) games.back().scores.push_back(v); }
  }
  FILE* out = fopen(argv[2], "w");
  fprintf(out, "game\thand\tseat\tpseudo\tn_total\tstreet\tsit\tmask");
  for (int f = 0; f < clones::NF; f++) fprintf(out, "\tx%d", f);
  fprintf(out, "\tcls\tsize\teff_bb\ts\tcall\tpot\tstack\town_bb\talive\tbb\n");
  int ok_games = 0, bad = 0; long rows = 0;
  for (auto& g : games) {
    pk::Engine eng(g.n, g.seed);
    size_t pos = 0; bool order_ok = true;
    std::vector<Rec> recs;
    std::vector<DataAgent> agents(g.n);
    std::vector<pk::Agent*> ap;
    for (int s = 0; s < g.n; s++) { agents[s].seat = s; agents[s].q = &g.acts; agents[s].pos = &pos; agents[s].order_ok = &order_ok; agents[s].recs = &recs; ap.push_back(&agents[s]); }
    pk::Engine::Result r;
    try { r = eng.run(ap); } catch (std::exception& e) { order_ok = false; }
    if (!order_ok || pos != g.acts.size() || r.scores != g.scores || eng.log.size() != recs.size()) {
      fprintf(stderr, "game %s: replay mismatch (order %d, %zu/%zu actions), skipped\n", g.id.c_str(), order_ok, pos, g.acts.size());
      bad++; continue;
    }
    ok_games++;
    for (size_t k = 0; k < recs.size(); k++) {
      const Rec& rc = recs[k]; const clones::Decision& d = rc.d;
      double size; int cls = clones::action_class(d, eng.log[k].shown, &size);
      if (cls < 0) continue;
      fprintf(out, "%s\t%d\t%d\t%s\t%d\t%d\t%d\t%u", g.id.c_str(), rc.hand, rc.seat, g.names[rc.seat].c_str(), g.n, d.street, d.sit, d.mask);
      for (int f = 0; f < clones::NF; f++) fprintf(out, "\t%.4f", d.x[f]);
      fprintf(out, "\t%d\t%.4f\t%.2f\t%.4f\t%d\t%d\t%d\t%.2f\t%d\t%d\n", cls, size, d.eff_bb, d.s, d.call, d.pot, d.stack, d.own_bb, d.alive, d.bb);
      rows++;
    }
  }
  fclose(out);
  printf("%d games replayed exactly, %d skipped; %ld decisions written to %s\n", ok_games, bad, rows, argv[2]);
  return bad ? 1 : 0;
}
