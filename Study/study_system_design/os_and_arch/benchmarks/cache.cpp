// cache_lab.cpp -- measurable cache/TLB/alignment effects
#include <atomic>
#include <chrono>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <thread>
#include <vector>

using clk = std::chrono::steady_clock;
template <class F> double ms(F&& f) {
  auto t0 = clk::now(); f();
  return std::chrono::duration<double, std::milli>(clk::now() - t0).count();
}

// ---------- 1. stride / spatial locality ----------
static void exp_stride() {
  constexpr size_t N = 64u << 20;  // 64 MiB of bytes
  std::vector<uint8_t> buf(N, 1);
  for (size_t stride : {1ul, 16ul, 64ul, 256ul, 4096ul}) {
    volatile uint64_t sum = 0;
    double t = ms([&] {
      uint64_t s = 0;
      for (size_t i = 0; i < N; i += stride) s += buf[i];
      sum = s;
    });
    printf("  stride %5zu B : %7.2f ms  (%zu touches)\n", stride, t, N / stride);
  }
}

// ---------- 2. row-major vs column-major ----------
static void exp_traversal() {
  constexpr size_t D = 4096;
  std::vector<float> m(D * D, 1.0f);
  volatile double s1 = 0, s2 = 0;
  double tr = ms([&] { double s = 0; for (size_t i = 0; i < D; ++i) for (size_t j = 0; j < D; ++j) s += m[i * D + j]; s1 = s; });
  double tc = ms([&] { double s = 0; for (size_t j = 0; j < D; ++j) for (size_t i = 0; i < D; ++i) s += m[i * D + j]; s2 = s; });
  printf("  row-major   : %7.2f ms\n  col-major   : %7.2f ms   (%.1fx slower)\n", tr, tc, tc / tr);
}

// ---------- 3. AoS vs SoA ----------
struct Particle { float x, y, z, vx, vy, vz; uint64_t id; char pad[24]; }; // 64 B
static void exp_aos_soa() {
  constexpr size_t N = 4u << 20;
  std::vector<Particle> aos(N);
  std::vector<float> xs(N, 1.0f), ys(N, 1.0f), zs(N, 1.0f);
  for (auto& p : aos) { p.x = p.y = p.z = 1.0f; }
  volatile double a = 0, b = 0;
  double ta = ms([&] { double s = 0; for (size_t i = 0; i < N; ++i) s += aos[i].x; a = s; });
  double tb = ms([&] { double s = 0; for (size_t i = 0; i < N; ++i) s += xs[i]; b = s; });
  printf("  AoS  .x sum : %7.2f ms\n  SoA  .x sum : %7.2f ms   (%.1fx faster)\n", ta, tb, ta / tb);
}

// ---------- 4. false sharing ----------
struct alignas(64) Padded { std::atomic<uint64_t> v{0}; };
static void exp_false_sharing() {
  constexpr int T = 2; constexpr uint64_t IT = 20'000'000;
  {
    std::atomic<uint64_t> shared[T];               // packed: same line
    for (int i = 0; i < T; ++i) shared[i].store(0);
    double t = ms([&] {
      std::vector<std::thread> th;
      for (int i = 0; i < T; ++i) th.emplace_back([&, i] { for (uint64_t k = 0; k < IT; ++k) shared[i].fetch_add(1, std::memory_order_relaxed); });
      for (auto& x : th) x.join();
    });
    printf("  packed  (false sharing): %7.2f ms\n", t);
  }
  {
    Padded padded[T];
    double t = ms([&] {
      std::vector<std::thread> th;
      for (int i = 0; i < T; ++i) th.emplace_back([&, i] { for (uint64_t k = 0; k < IT; ++k) padded[i].v.fetch_add(1, std::memory_order_relaxed); });
      for (auto& x : th) x.join();
    });
    printf("  64B-aligned (no sharing): %7.2f ms\n", t);
  }
}

// ---------- 5. TLB pressure: sequential vs random page touch ----------
static void exp_tlb() {
  constexpr size_t PAGES = 256 * 1024;  // 1 GiB of 4K pages
  constexpr size_t PS = 4096;
  std::vector<uint8_t> buf(PAGES * PS, 1);
  std::vector<size_t> idx(PAGES);
  for (size_t i = 0; i < PAGES; ++i) idx[i] = i;
  volatile uint64_t s1 = 0, s2 = 0;
  double tseq = ms([&] { uint64_t s = 0; for (size_t i = 0; i < PAGES; ++i) s += buf[i * PS]; s1 = s; });
  std::mt19937_64 rng(42); std::shuffle(idx.begin(), idx.end(), rng);
  double trnd = ms([&] { uint64_t s = 0; for (size_t i = 0; i < PAGES; ++i) s += buf[idx[i] * PS]; s2 = s; });
  printf("  seq  1 byte/page : %7.2f ms\n  rand 1 byte/page : %7.2f ms   (%.1fx slower)\n", tseq, trnd, trnd / tseq);
}

int main() {
  printf("[1] stride sweep (64 MiB buffer)\n");        exp_stride();
  printf("[2] 4096x4096 float matrix traversal\n");    exp_traversal();
  printf("[3] AoS vs SoA (4M particles, 64B each)\n"); exp_aos_soa();
  printf("[4] false sharing, 2 threads x 20M RMW\n");  exp_false_sharing();
  printf("[5] TLB: 1 GiB across 262144 4K pages\n");   exp_tlb();
}
