// hu.cpp - CLI for the heads-up solver (hu.hpp).  Run from the repo root.
//   hu pool N SEED OUT [runouts]           sample the deal pool (data/cache/hu/pool.bin)
//   hu solve S ITERS [POOL] [BRDEALS]      solve one stack (BB); prints preflop frequencies, value, exploitability
//   hu grid S1,S2,.. ITERS OUT [POOL]      solve every stack in parallel and write the average strategies
#pragma GCC optimize("O3")
#include <chrono>
#include <cstdlib>
#include <thread>
#include "hu.hpp"
using namespace hu;

static void write_strategy(FILE* f, const Solver& s) {
  const Tree& t = s.tree; int nn = t.nodes.size(); double S = t.S;
  fwrite(&S, 8, 1, f); fwrite(&nn, 4, 1, f);
  for (int id = 0; id < nn; id++) {
    const Node& n = t.nodes[id];
    int hl = n.hist.size(); fwrite(&hl, 4, 1, f); fwrite(n.hist.data(), 1, hl, f);
    fwrite(&n.player, 4, 1, f); fwrite(&n.street, 4, 1, f); fwrite(&n.term, 4, 1, f); fwrite(n.c, 8, 2, f);
    fwrite(&n.nact, 4, 1, f); fwrite(n.act, 4, MAXA, f); fwrite(n.child, 4, MAXA, f); fwrite(&n.nb, 4, 1, f);
    for (int b = 0; b < n.nb; b++) { double p[MAXA] = {0, 0, 0, 0}; if (!n.term) s.avg(n.base + b, n.nact, p); fwrite(p, 8, MAXA, f); }
  }
}

int main(int argc, char** argv) {
  if (argc < 2) { fprintf(stderr, "usage: see header\n"); return 2; }
  std::string cmd = argv[1];
  pe::init();
  if (cmd == "pool") {
    int n = atoi(argv[2]); uint64_t seed = atoll(argv[3]); std::string out = argv[4]; int runouts = argc > 5 ? atoi(argv[5]) : 100;
    auto t0 = std::chrono::steady_clock::now();
    Pool p; p.generate(n, seed, runouts);
    printf("pool: %d deals in %.1f s; EHS thresholds:\n", n, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    for (int s = 0; s < 3; s++) { printf("  %s:", s == 0 ? "flop " : s == 1 ? "turn " : "river"); for (int b = 0; b < NB - 1; b++) printf(" %.3f", p.thr[s][b]); printf("\n"); }
    return p.save(out) ? 0 : 1;
  }
  Pool pool; std::string pool_path = "data/cache/hu/pool.bin";
  if (cmd == "solve") {
    double S = atof(argv[2]); long iters = atol(argv[3]); if (argc > 4) pool_path = argv[4]; int brd = argc > 5 ? atoi(argv[5]) : 50000;
    if (!pool.load(pool_path)) { fprintf(stderr, "no pool at %s\n", pool_path.c_str()); return 1; }
    Solver s(pool, S);
    printf("S=%g BB: %zu nodes, %d info sets, pool %zu deals\n", S, s.tree.nodes.size(), s.tree.ninfo, pool.d.size());
    auto t0 = std::chrono::steady_clock::now();
    long done = 0; int rounds = 5;
    for (int r = 1; r <= rounds; r++) {
      s.run(iters / rounds); done += iters / rounds;
      double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
      double v = s.game_value(brd), b0 = s.best_response(0, brd), b0i = s.last_br_in, b1 = s.best_response(1, brd), b1i = s.last_br_in;
      printf("  iters %ld (%.0f s): value SB %+.4f BB/hand; BR SB %+.4f (in-sample %+.4f), BR BB %+.4f (%+.4f); exploitability %.4f (in-sample %.4f) BB/hand\n",
             done, sec, v, b0, b0i, b1, b1i, (b0 + b1) / 2, (b0i + b1i) / 2);
    }
    s.print_preflop();
    return 0;
  }
  if (cmd == "grid") {
    std::vector<double> st; { std::string a = argv[2]; size_t i = 0; while (i < a.size()) { size_t j = a.find(',', i); if (j == std::string::npos) j = a.size(); st.push_back(atof(a.substr(i, j - i).c_str())); i = j + 1; } }
    long iters = atol(argv[3]); std::string out = argv[4]; if (argc > 5) pool_path = argv[5];
    if (!pool.load(pool_path)) { fprintf(stderr, "no pool at %s\n", pool_path.c_str()); return 1; }
    std::vector<Solver*> sol; for (double S : st) sol.push_back(new Solver(pool, S, (uint64_t)(S * 1000) + 7));
    std::vector<std::thread> th; std::vector<double> ex(st.size()), val(st.size());
    auto t0 = std::chrono::steady_clock::now();
    for (size_t i = 0; i < st.size(); i++) th.emplace_back([&, i] { sol[i]->run(iters); val[i] = sol[i]->game_value(50000); ex[i] = (sol[i]->best_response(0, 50000) + sol[i]->best_response(1, 50000)) / 2; });
    for (auto& x : th) x.join();
    FILE* f = fopen(out.c_str(), "wb"); fwrite("HUS1", 1, 4, f); int ns = st.size(); fwrite(&ns, 4, 1, f); fwrite(&pool.thr, sizeof pool.thr, 1, f);
    for (size_t i = 0; i < st.size(); i++) { write_strategy(f, *sol[i]); printf("S=%g: value SB %+.4f, exploitability %.4f BB/hand\n", st[i], val[i], ex[i]); }
    fclose(f);
    printf("wrote %s in %.0f s\n", out.c_str(), std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    return 0;
  }
  fprintf(stderr, "unknown command\n"); return 2;
}
