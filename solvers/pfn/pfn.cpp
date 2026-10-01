// pfn.cpp - CLI for the N-player ICM jam/fold solver (pfn.hpp).
//   pfn test                                  HU sanity check against solvers/pf.py's widths
//   pfn solve N NTOT S1 .. SN [iters]         one stack configuration (stacks in BB, action order, last = BB)
//   pfn grid N NTOT L1,L2,.. OUT [iters]      every stack configuration on the level grid, in parallel, to OUT
//   pfn time N NTOT S1 .. SN [iters]          time one solve
// Tables from data/cache/pfn (solvers/pfn/tables.cpp) and solvers/eq169.bin; run from the repo root.
#pragma GCC optimize("O3")
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <thread>
#include "pfn.hpp"
using namespace pfn;

static std::vector<double> parse_levels(const char* s) {
  std::vector<double> v; std::string t(s); size_t i = 0;
  while (i < t.size()) { size_t j = t.find(',', i); if (j == std::string::npos) j = t.size(); v.push_back(atof(t.substr(i, j - i).c_str())); i = j + 1; }
  return v;
}

int main(int argc, char** argv) {
  if (argc < 2) { fprintf(stderr, "usage: see header\n"); return 2; }
  Tables T; if (!T.load("data/cache/pfn", "solvers/eq169.bin")) return 1;
  std::string cmd = argv[1];
  if (cmd == "test") {
    // HU: ICM with payouts (1, 0) is chip EV, so the solver must reproduce pf.py (jam 58.3 / call 37.5 at 10 BB)
    struct { double S, jam, call; } want[] = {{5, .713, .620}, {10, .584, .376}, {20, .403, .217}};
    int fails = 0;
    for (auto& w : want) {
      Game g; double st[2] = {w.S, w.S}; g.set(2, 2, st);
      Solver s(T, g); s.solve(3000, 0);
      double j = s.width(Solver::node(0, 0)), c = s.width(Solver::node(1, 1));
      bool ok = std::fabs(j - w.jam) < 0.012 && std::fabs(c - w.call) < 0.012;
      printf("%s HU %g BB: jam %.3f (pf.py %.3f) call %.3f (pf.py %.3f) eps=%.5f\n", ok ? "ok  " : "FAIL", w.S, j, w.jam, c, w.call, s.eps);
      fails += !ok;
    }
    printf("%s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
  }
  if (cmd == "solve" || cmd == "time") {
    int N = atoi(argv[2]), nt = atoi(argv[3]); double st[4];
    for (int i = 0; i < N; i++) st[i] = atof(argv[4 + i]);
    int iters = argc > 4 + N ? atoi(argv[4 + N]) : 300;
    Game g; g.set(N, nt, st);
    auto t0 = std::chrono::steady_clock::now();
    Solver s(T, g); s.solve(iters, cmd == "time" ? 0 : 1e-4);
    double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    s.print();
    printf("%.2f s, %.3f s/iter\n", sec, sec / s.iters);
    return 0;
  }
  if (cmd == "grid") {
    int N = atoi(argv[2]), nt = atoi(argv[3]); auto lv = parse_levels(argv[4]); std::string out = argv[5];
    int iters = argc > 6 ? atoi(argv[6]) : 300;
    int L = lv.size(), P = 1; for (int i = 0; i < N; i++) P *= L;
    Game g0; double st0[4] = {10, 10, 10, 10}; g0.set(N, nt, st0);
    Solver s0(T, g0); int nn = s0.nodes.size();
    std::vector<uint8_t> sig((size_t)P * nn * K); std::vector<float> dev((size_t)P * nn * K), eps(P);
    std::atomic<int> next(0), done(0);
    auto t0 = std::chrono::steady_clock::now();
    int nth = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::thread> th;
    for (int t = 0; t < nth; t++) th.emplace_back([&] {
      for (;;) {
        int q = next++; if (q >= P) return;
        double st[4]; int r = q; for (int i = N - 1; i >= 0; i--) { st[i] = lv[r % L]; r /= L; }
        Game g; g.set(N, nt, st);
        Solver s(T, g); s.solve(iters);
        for (int k = 0; k < nn; k++) { int nd = s.nodes[k];
          for (int h = 0; h < K; h++) { sig[((size_t)q * nn + k) * K + h] = (uint8_t)std::lround(255 * s.sig[(size_t)nd * K + h]);
            dev[((size_t)q * nn + k) * K + h] = (float)((s.evj[(size_t)nd * K + h] - s.evf[nd]) / g.pool); } }
        eps[q] = (float)s.eps;
        int d = ++done;
        if (d % 20 == 0 || d == P) { double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
          fprintf(stderr, "%d/%d  %.0f s  (eta %.0f s)\n", d, P, sec, sec / d * (P - d)); }
      }
    });
    for (auto& x : th) x.join();
    FILE* f = fopen(out.c_str(), "wb");
    fwrite("PFN1", 1, 4, f); int hdr[4] = {N, nt, L, nn}; fwrite(hdr, 4, 4, f);
    for (double l : lv) { float x = l; fwrite(&x, 4, 1, f); }
    for (int k = 0; k < nn; k++) { int nd = s0.nodes[k]; fwrite(&nd, 4, 1, f); }
    fwrite(sig.data(), 1, sig.size(), f); fwrite(dev.data(), 4, dev.size(), f); fwrite(eps.data(), 4, eps.size(), f);
    fclose(f);
    double me = 0, mx = 0; for (float e : eps) { me += e / P; mx = std::max(mx, (double)e); }
    printf("wrote %s: %d points, %d nodes, eps mean %.5f max %.5f\n", out.c_str(), P, nn, me, mx);
    return 0;
  }
  fprintf(stderr, "unknown command\n"); return 2;
}
