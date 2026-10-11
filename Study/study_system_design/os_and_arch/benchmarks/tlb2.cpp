// tlb_lab_v2.cpp -- isolate dTLB reach from BOTH cache capacity and 4K set-aliasing.
// v1 put every node at offset 0 within its page: a 4096 B stride, which maps every
// node onto the same L1 set. That measured conflict misses, not the TLB.
// v2 scatters the node to a pseudo-random 64 B slot inside its page.
#include <sys/mman.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

using clk = std::chrono::steady_clock;
constexpr size_t PS = 4096, LINES = PS / 64;

static double probe(size_t pages, bool huge, bool scatter) {
  size_t bytes = pages * PS + (2u << 20);
  void* raw = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  madvise(raw, bytes, huge ? MADV_HUGEPAGE : MADV_NOHUGEPAGE);
  // 2 MiB-align so THP can actually back the region
  auto* base = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(raw) + (2u << 20) - 1) & ~((uintptr_t)(2u << 20) - 1));
  std::mt19937_64 rng(1337);
  std::vector<size_t> order(pages);
  for (size_t i = 0; i < pages; ++i) order[i] = i;
  std::shuffle(order.begin(), order.end(), rng);
  auto slot_of = [&](size_t page) {
    size_t off = scatter ? (std::mt19937_64(page * 0x9E3779B97F4A7C15ull)() % LINES) * 64 : 0;
    return reinterpret_cast<void**>(base + page * PS + off);
  };
  for (size_t i = 0; i < pages; ++i) *slot_of(order[i]) = slot_of(order[(i + 1) % pages]);
  void* p = slot_of(order[0]);
  const size_t iters = std::max<size_t>(pages * 20, 3'000'000);
  auto t0 = clk::now();
  for (size_t i = 0; i < iters; ++i) p = *reinterpret_cast<void**>(p);
  double ns = std::chrono::duration<double, std::nano>(clk::now() - t0).count() / iters;
  asm volatile("" :: "r"(p));
  munmap(raw, bytes);
  return ns;
}

int main() {
  printf("  pages  footprint  lines touched | 4K scattered | 4K aliased | THP scattered\n");
  for (size_t pages : {16ul, 64ul, 256ul, 1024ul, 4096ul, 16384ul, 65536ul}) {
    printf("%7zu %7zu KiB %9zu KiB   | %9.2f ns | %7.2f ns | %9.2f ns\n",
           pages, pages * PS / 1024, pages * 64 / 1024,
           probe(pages, false, true), probe(pages, false, false), probe(pages, true, true));
  }
}
