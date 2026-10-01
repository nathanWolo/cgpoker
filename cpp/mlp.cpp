// MLP forward-pass cost under CodinGame's flags (pragma O3 + AVX2/FMA target).
// float32 weights (dequantised from an int8/int4 payload at init), ReLU, row-major W[out][in].
#pragma GCC optimize("O3")
#include <cstdio>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <immintrin.h>
#include <initializer_list>
#pragma GCC target("avx2,fma,bmi,bmi2,popcnt,lzcnt")
static double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
#define AI __attribute__((always_inline)) inline
AI float hsum(__m256 v) { __m128 a = _mm_add_ps(_mm256_castps256_ps128(v), _mm256_extractf128_ps(v, 1)); a = _mm_hadd_ps(a, a); a = _mm_hadd_ps(a, a); return _mm_cvtss_f32(a); }
// y[B][o] = act(W[o][i] . x[B][i] + b[o]); processes 4 samples per W-row pass to reuse the row
AI void dense(const float* W, const float* b, const float* x, float* y, int in, int out, int B, bool relu) {
  int s = 0;
  for (; s + 4 <= B; s += 4) {
    const float *x0 = x + s * in, *x1 = x0 + in, *x2 = x1 + in, *x3 = x2 + in;
    for (int o = 0; o < out; o++) {
      const float* w = W + o * in; __m256 a0 = _mm256_setzero_ps(), a1 = a0, a2 = a0, a3 = a0;
      for (int i = 0; i < in; i += 8) { __m256 wv = _mm256_loadu_ps(w + i);
        a0 = _mm256_fmadd_ps(wv, _mm256_loadu_ps(x0 + i), a0); a1 = _mm256_fmadd_ps(wv, _mm256_loadu_ps(x1 + i), a1);
        a2 = _mm256_fmadd_ps(wv, _mm256_loadu_ps(x2 + i), a2); a3 = _mm256_fmadd_ps(wv, _mm256_loadu_ps(x3 + i), a3); }
      float r[4] = {hsum(a0) + b[o], hsum(a1) + b[o], hsum(a2) + b[o], hsum(a3) + b[o]};
      for (int k = 0; k < 4; k++) y[(s + k) * out + o] = relu && r[k] < 0 ? 0 : r[k];
    }
  }
  for (; s < B; s++) {
    const float* x0 = x + s * in;
    for (int o = 0; o < out; o++) {
      const float* w = W + o * in; __m256 a0 = _mm256_setzero_ps(), a1 = a0;
      int i = 0; for (; i + 16 <= in; i += 16) { a0 = _mm256_fmadd_ps(_mm256_loadu_ps(w + i), _mm256_loadu_ps(x0 + i), a0); a1 = _mm256_fmadd_ps(_mm256_loadu_ps(w + i + 8), _mm256_loadu_ps(x0 + i + 8), a1); }
      for (; i < in; i += 8) a0 = _mm256_fmadd_ps(_mm256_loadu_ps(w + i), _mm256_loadu_ps(x0 + i), a0);
      float r = hsum(_mm256_add_ps(a0, a1)) + b[o]; y[s * out + o] = relu && r < 0 ? 0 : r;
    }
  }
}
struct Net { int L, dims[8]; float *W[8], *b[8]; };
static float* rnd(int n) { float* p = (float*)aligned_alloc(32, ((n * 4 + 31) / 32) * 32); for (int i = 0; i < n; i++) p[i] = (rand() / (float)RAND_MAX - 0.5f) * 0.1f; return p; }
static float bufA[1 << 20], bufB[1 << 20];
static const float* fwd(Net& n, const float* x, int B) {
  const float* in = x; float* out = bufA;
  for (int l = 0; l < n.L; l++) { dense(n.W[l], n.b[l], in, out, n.dims[l], n.dims[l + 1], B, l + 1 < n.L); in = out; out = out == bufA ? bufB : bufA; }
  return in;
}
int main() {
  int cfg[][6] = {{4, 64, 64, 64, 8, 0}, {4, 128, 128, 128, 8, 0}, {4, 128, 256, 256, 8, 0}, {4, 256, 256, 256, 8, 0}, {4, 256, 512, 256, 8, 0}};
  static float X[256 * 256]; for (int i = 0; i < 256 * 256; i++) X[i] = rand() / (float)RAND_MAX;
  for (auto& c : cfg) {
    Net n; n.L = c[0] - 1; long params = 0;
    for (int l = 0; l <= n.L; l++) n.dims[l] = c[l + 1];
    for (int l = 0; l < n.L; l++) { n.W[l] = rnd(n.dims[l] * n.dims[l + 1]); n.b[l] = rnd(n.dims[l + 1]); params += n.dims[l] * n.dims[l + 1] + n.dims[l + 1]; }
    printf("MLP %d-%d-%d-%d (%ld params, %.0f KB fp32):", c[1], c[2], c[3], c[4], params, params * 4 / 1024.0);
    for (int B : {1, 16, 64}) {
      volatile float sink = 0; int reps = 0; double t0 = now(), t1;
      do { for (int k = 0; k < 64; k++) { const float* y = fwd(n, X, B); sink += y[0]; } reps += 64; t1 = now(); } while (t1 - t0 < 0.3);
      double us = (t1 - t0) / reps / B * 1e6;
      printf("  B=%d %.2f us/sample (%.0f per 40 ms)", B, us, 40000 / us);
    }
    printf("\n");
  }
}
