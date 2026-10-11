# 02 — The cache hierarchy

> Prerequisite: `01_cpu_pipelines_and_speculation.md`.
> Lab: `os_and_arch/benchmarks/cache.cpp` (already written, already measured).

## The billing model

**The machine does not charge you per byte. It charges you per line.**

Internalise this and most cache advice becomes derivable rather than memorised.
A cache line is 64 B on essentially all x86 and most ARM server cores, and 128 B
on Apple silicon (`getconf LEVEL1_DCACHE_LINESIZE` tells you). Every transfer
between levels moves whole lines. Reading one byte costs the same as reading 64.

Here is that model measured, sweeping the stride across a 64 MiB buffer:

| Stride | Wall time | Per touch | Useful bytes per line fetched |
| --- | --- | --- | --- |
| 1 B | 33.77 ms | 0.50 ns | 64 / 64 |
| 16 B | 9.98 ms | 2.38 ns | 16 / 64 |
| 64 B | 5.67 ms | 5.41 ns | 8 / 64 |
| 256 B | 2.12 ms | 8.09 ns | 8 / 64 |
| 4096 B | 0.16 ms | 9.77 ns | 8 / 64 |

Read it backwards and it clicks: **per-touch cost rises 20x while total time
falls 200x.** At stride 1 you get 64 useful bytes per fetch. At stride 4096 you
pay full price for 8 and discard the rest.

(Measured on an x86 Xeon. Your ARM machine gives a different shape — run it.)

## Anatomy: how an address finds a line

A physical address is cut into three fields. For a 32 KiB, 8-way, 64 B-line L1d:

```
   sets = 32768 / (64 * 8) = 64 sets  => 6 index bits
   line = 64 B                        => 6 offset bits

   |  ... tag ...  | index (6) | offset (6) |
    47           12 11        6 5          0
```

Three consequences fall straight out of this arithmetic:

1. **Only the index bits choose the set.** Two addresses 4096 B apart have
   identical index bits here, so they land in the *same set* and compete for the
   same 8 ways. This is the 4 KiB aliasing trap, and it is measured below.
2. **The offset bits never reach the cache.** Sub-line addressing is free; you
   cannot "read less" than a line.
3. **L1 is virtually-indexed, physically-tagged (VIPT)** on most designs, which
   is only possible when `index + offset <= page offset bits (12)`. That is
   precisely why L1 caches stalled at 32 KiB for a decade — going bigger at the
   same associativity would push index bits past the page boundary and force
   translation before indexing. Growing associativity is the escape hatch, which
   is why modern L1s are 12-way 48 KiB.

That last point is a genuinely strong interview answer to "why is L1 always 32 KB?"

## The four Cs

| Kind | Cause | Fix |
| --- | --- | --- |
| Compulsory | First ever touch of a line | Prefetch, or restructure to touch less |
| Capacity | Working set exceeds the level | Tiling/blocking, smaller structs |
| Conflict | Too many hot lines mapping to one set | Change stride, pad, offset |
| Coherence | Another core invalidated your line | Tutorial 03 — false sharing |

Conflict misses are the ones engineers systematically fail to diagnose, because
the working set "obviously fits". Here is the measurement — a pointer chase
where the only difference is whether nodes sit at offset 0 of each page
(4096 B stride) or at a scattered offset within the page:

| Footprint | Scattered within page | All at offset 0 | Penalty |
| --- | --- | --- | --- |
| 256 KiB | 1.77 ns | 5.79 ns | 3.3x |
| 4 MiB | 7.92 ns | 35.47 ns | **4.5x** |
| 256 MiB | 71.11 ns | 134.74 ns | 1.9x |

Same number of nodes, same bytes touched, same total footprint. The only change
is the low 12 bits of each address. If your ring buffers, per-core arenas or
flow-table buckets are spaced at a clean 4 KiB or 64 KiB, you are living in the
right-hand column and your profiler will point at the wrong line of code.

**The fix is embarrassingly simple:** pad power-of-two-sized structures by one
line. Allocate `4096 + 64` per slot instead of `4096`.

## Write policy — the part that surprises people

Caches are **write-back, write-allocate** by default. Two consequences:

- A write to a line you do not own triggers a **read** of that line first
  (read-for-ownership). Writing 64 B of fresh data costs you a 64 B read you do
  not need.
- Dirty lines are written back on eviction, not immediately, so store bandwidth
  is bursty and stores can be "free" right up until they are not.

When you are writing a large buffer you will never read back — a packet capture
ring, a memset of a fresh arena, a frame buffer — that RFO read is pure waste.
**Non-temporal stores** skip it:

```cpp
#include <immintrin.h>

// Streaming store: bypasses cache, no read-for-ownership, no L3 pollution.
// Must be 16/32 B aligned, and you MUST fence before anyone reads it.
void fill_stream(float* dst, size_t n, float v) {
    const __m256 vv = _mm256_set1_ps(v);
    size_t i = 0;
    for (; i + 8 <= n; i += 8)
        _mm256_stream_ps(dst + i, vv);       // NT store
    for (; i < n; ++i) dst[i] = v;
    _mm_sfence();                            // NT stores are weakly ordered
}
```

The `_mm_sfence()` is not optional. Non-temporal stores are *weakly ordered even
on x86* — they are the one exception to x86-TSO. Forgetting it is a real,
shipped-to-production class of bug, and mentioning it unprompted signals you have
actually used these rather than read about them.

Rule of thumb: NT stores win when the buffer is larger than L3 and will not be
re-read soon. Below that they lose, because you gave up the cache for nothing.

## Prefetchers, and exactly when they fail

Hardware prefetchers are why sequential access is so much faster than the
latency numbers suggest. Typical units:

- **Next-line** in L1 and L2.
- **Stride detector** (IP-based): recognises a constant stride per instruction
  pointer, positive or negative.
- **Streamer** in L2/L3: detects ascending/descending line runs, prefetches
  ahead within a 4 KiB page.

They fail in four specific situations, and knowing these by name is the useful part:

1. **Pointer chasing.** The next address is not computable until the current
   load returns. Nothing can predict it.
2. **Crossing a page boundary.** Hardware prefetchers generally do not prefetch
   across a 4 KiB page, because the next page's translation may not exist.
   *This is a hidden argument for hugepages:* a 2 MiB page lets the streamer run
   512x further before it stops.
3. **Random or hash-derived access.** By construction unpredictable.
4. **Very short loops.** The stride detector needs several iterations to train.

Software prefetch is a last resort, not a first move:

```cpp
// Only helps when you can compute the address well ahead of use AND the
// hardware prefetcher genuinely cannot see the pattern.
for (size_t i = 0; i < n; ++i) {
    __builtin_prefetch(&table[hashes[i + 8]], 0 /*read*/, 1 /*low temporal locality*/);
    process(table[hashes[i]]);
}
```

Get the distance wrong and you either prefetch too late (no benefit) or too early
(evicted before use, plus wasted bandwidth). Always measure; software prefetch
makes things worse at least as often as it helps.

## Blocking: the one algorithmic technique

When the working set exceeds a level, restructure so each *tile* fits. Matrix
transpose is the canonical demonstration because the naive version is pathological:

```cpp
// Naive: one of the two accesses always strides by the row length.
void transpose_naive(const float* src, float* dst, size_t n) {
    for (size_t i = 0; i < n; ++i)
        for (size_t j = 0; j < n; ++j)
            dst[j * n + i] = src[i * n + j];
}

// Blocked: work on a B x B tile that fits comfortably in L1/L2.
// Both src and dst tiles stay resident for the whole inner pair of loops.
void transpose_blocked(const float* src, float* dst, size_t n, size_t B = 64) {
    for (size_t ii = 0; ii < n; ii += B)
        for (size_t jj = 0; jj < n; jj += B) {
            const size_t i_end = std::min(ii + B, n);
            const size_t j_end = std::min(jj + B, n);
            for (size_t i = ii; i < i_end; ++i)
                for (size_t j = jj; j < j_end; ++j)
                    dst[j * n + i] = src[i * n + j];
        }
}
```

Choose `B` so that `2 * B * B * sizeof(float)` comfortably fits L1 or L2. For
64 KiB of L1 and 4 B floats, `B = 64` gives `2 * 64 * 64 * 4 = 32 KiB`. This is
the same reasoning behind BLAS tiling and behind convolution tiling on edge NPUs
(tutorial `embedded_ai/02`).

## The counters that settle arguments

```bash
# Is it a data-cache problem, and at which level?
perf stat -e L1-dcache-loads,L1-dcache-load-misses,\
             l2_rqsts.all_demand_miss,LLC-loads,LLC-load-misses ./app

# Where are the stalls coming from at all?
perf stat --topdown ./app

# Which source lines miss?
perf record -e L1-dcache-load-misses:pp ./app && perf report

# Which cache lines are shared between which cores? (tutorial 03)
perf c2c record ./app && perf c2c report
```

A miss *rate* alone means nothing. 50% misses on a loop with high MLP can be
faster than 5% misses on a dependent chain. Always pair a rate with a top-down
bucket.

## Interview angles

**"Why is this loop slow?"** Name the four Cs and say which you would test first.
Then name the counter that discriminates them.

**"Why is L1 cache always about 32 KB?"** The VIPT constraint above. This is a
genuinely differentiating answer.

**"How would you make this struct faster?"** Hot/cold splitting, line alignment,
and the arithmetic of how many records fit a line. Tutorial 06 of the LLD track
has the full treatment.

**"When would you use non-temporal stores?"** Buffer larger than L3, no re-read
soon, and *mention the fence*.

**"Your working set is 2 MB and L2 is 2 MB, but it's slow. Why?"**
Conflict misses or coherence traffic. Capacity is not the only failure mode, and
recognising that instantly is a strong signal.

## Exercises

1. Compute the set count, index bits and aliasing stride for each cache level on
   your own machine. Get the geometry from `lscpu -C` or
   `/sys/devices/system/cpu/cpu0/cache/index*/`. Write it down.
2. Reproduce the aliasing measurement: allocate N buffers spaced exactly 4096 B
   apart, then respace them at `4096 + 64`, and measure a pass over one hot word
   in each. Sweep N from 1 to 32 and find the exact N where the cliff appears.
   That N should equal your L1 associativity. Confirm it does.
3. Implement `transpose_naive` and `transpose_blocked` for a 4096x4096 matrix.
   Sweep `B` over {8, 16, 32, 64, 128, 256} and plot. Explain the optimum from
   your cache sizes.
4. Take `fill_stream` and benchmark it against a plain loop at 1 MiB, 16 MiB,
   256 MiB and 2 GiB. Find the crossover point where NT stores start winning, and
   check it against your L3 size.
5. Run `cache.cpp` from the benchmarks folder on your ARM machine and on an AP.
   Write up every place the ARM numbers disagree with the x86 table in this file.
   That write-up is interview gold — guard it.
