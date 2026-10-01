// pfd.cpp - CLI for the 3-4 player preflop solver (pfd.hpp).  Run from the repo root (tables in data/cache/pfn).
//   pfd solve N S [iters]                 one game; prints the root-area frequencies
//   pfd grid N S1,S2,.. ITERS OUT         every stack (in parallel) to OUT
#pragma GCC optimize("O3")
#include <chrono>
#include <cstdlib>
#include <thread>
#include "pfd.hpp"
using namespace pfd;

static void write_strategy(FILE* f, const Solver& s) {
  int N = s.g.N; double S = s.g.S; int nn = s.tree.nodes.size();
  fwrite(&N, 4, 1, f); fwrite(&S, 8, 1, f); fwrite(&nn, 4, 1, f);
  for (const Node& n : s.tree.nodes) {
    int hl = n.hist.size(); fwrite(&hl, 4, 1, f); fwrite(n.hist.data(), 1, hl, f);
    fwrite(&n.player, 4, 1, f); fwrite(&n.type, 4, 1, f); fwrite(&n.nact, 4, 1, f); fwrite(n.acts, 1, MAXA, f); fwrite(n.child, 4, MAXA, f); fwrite(n.c, 8, 4, f);
    if (!n.type) for (int h = 0; h < K; h++) { double p[MAXA] = {0, 0, 0, 0}; s.avg(n, h, p); fwrite(p, 8, MAXA, f); }
  }
}

int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: see header\n"); return 2; }
  pfn::Tables T; if (!T.load("data/cache/pfn", "solvers/eq169.bin")) return 1;
  std::string cmd = argv[1];
  if (cmd == "solve") {
    Game g; g.N = atoi(argv[2]); g.S = atof(argv[3]); int iters = argc > 4 ? atoi(argv[4]) : 200;
    auto t0 = std::chrono::steady_clock::now();
    Solver s(T, g);
    printf("N=%d S=%g: %zu nodes, %d decision nodes\n", g.N, g.S, s.tree.nodes.size(), s.tree.ndec);
    for (int t = 0; t < iters; t++) s.iterate();
    s.print();
    double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    printf("%.1f s (%.3f s/iter)\n", sec, sec / iters);
    return 0;
  }
  if (cmd == "grid") {
    int N = atoi(argv[2]); std::vector<double> st; { std::string a = argv[3]; size_t i = 0; while (i < a.size()) { size_t j = a.find(',', i); if (j == std::string::npos) j = a.size(); st.push_back(atof(a.substr(i, j - i).c_str())); i = j + 1; } }
    int iters = atoi(argv[4]); std::string out = argv[5];
    std::vector<Solver*> sol; for (double S : st) { Game g; g.N = N; g.S = S; sol.push_back(new Solver(T, g)); }
    std::vector<std::thread> th;
    for (size_t i = 0; i < st.size(); i++) th.emplace_back([&, i] { for (int t = 0; t < iters; t++) sol[i]->iterate(); });
    for (auto& x : th) x.join();
    FILE* f = fopen(out.c_str(), "wb"); fwrite("PFD1", 1, 4, f); int ns = st.size(); fwrite(&ns, 4, 1, f);
    for (size_t i = 0; i < st.size(); i++) { write_strategy(f, *sol[i]); sol[i]->print(); }
    fclose(f); printf("wrote %s\n", out.c_str());
    return 0;
  }
  return 2;
}
