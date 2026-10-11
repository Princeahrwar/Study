# 03 — Cache coherence, MESI, and the true cost of an atomic

> Prerequisite: `02_cache_hierarchy.md`.
> Diagram: `diagrams/02_mesi.mmd`. Lab: `benchmarks/cache.cpp` experiment 4.

## The problem coherence solves

Every core has its own L1. If core 0 and core 1 both cache address `X` and core 0
writes it, core 1 must not keep serving the stale value. Coherence is the
hardware protocol that makes a multicore machine behave like it has one memory.

Two things people routinely get wrong, so fix them now:

1. **Coherence is per cache line, never per variable.** The hardware has no idea
   what a variable is. Two unrelated counters in the same 64 B line contend
   exactly as hard as one shared counter.
2. **Coherence is not ordering.** Coherence guarantees all cores eventually agree
   on the value of each *single* location. It says nothing about the order of
   operations across *different* locations — that is the memory model, tutorial 10.
   Candidates conflate these constantly; not conflating them is a signal.

## MESI

Four states per line, per cache:

| State | Meaning | Dirty? | Others have it? |
| --- | --- | --- | --- |
| **M**odified | I own it and I changed it | Yes | No |
| **E**xclusive | I own it, unchanged | No | No |
| **S**hared | Clean copy, others may have it too | No | Maybe |
| **I**nvalid | My copy is stale; refetch before use | — | — |

The transitions that matter:

```
read, nobody else has it          I -> E
read, someone else has it         I -> S   (and they go E -> S)
write, I have it exclusive        E -> M   (silent! no bus traffic)
write, I have it shared           S -> M   (invalidate every other sharer)
write, I don't have it            I -> M   (read-for-ownership)
another core reads my M line      M -> S   (writeback or cache-to-cache forward)
another core writes my line       M/E/S -> I
```

**The one to remember is `E -> M`.** A write to a line you already hold
exclusively is free — no messages, no latency. A write to a line held Shared
requires invalidating every other holder and waiting for acknowledgement. That
gap *is* the cost of contention.

Real designs extend this. **MOESI** (AMD, many ARM) adds **O**wned, letting a
dirty line be shared without writing back to memory. **MESIF** (Intel) adds
**F**orward, designating exactly one sharer to respond to requests so that five
sharers do not all answer at once. The four-state model is enough to reason
correctly; know the extensions exist and what problem each solves.

## Why an atomic costs what it costs

```cpp
std::atomic<uint64_t> counter{0};
counter.fetch_add(1, std::memory_order_relaxed);
```

Uncontended, this is a `lock xadd` on x86 or an LL/SC loop on ARM, against a line
already in M state: roughly **15-25 cycles**. The cost is the instruction's
internal serialisation, not communication.

Contended across N cores, every increment must acquire the line exclusively.
Each acquisition is a coherence round trip — **100-300+ cycles**, and it scales
*worse* than linearly because the requests queue. Throughput on a single
contended atomic actually goes **down** as you add cores.

That asymmetry is the whole reason for per-core counters:

```cpp
// The pattern to reach for. Each core owns its line: every increment is E -> M,
// which is free. Aggregation is a rare, cheap read.
struct alignas(std::hardware_destructive_interference_size) PerCoreCounter {
    std::atomic<uint64_t> v{0};
};

class Stats {
public:
    explicit Stats(unsigned ncores) : slots_(ncores) {}

    // Hot path: called from the packet loop. No contention, ever.
    void bump(unsigned core) noexcept {
        slots_[core].v.fetch_add(1, std::memory_order_relaxed);
    }

    // Cold path: called by a stats thread every second or so.
    // Note: this is a *fuzzy* snapshot — slots are read at slightly different
    // times, so the total may never have been the true instantaneous value.
    // For monotonic counters that is fine, and saying so out loud is the
    // senior part of the answer.
    uint64_t total() const noexcept {
        uint64_t t = 0;
        for (const auto& s : slots_) t += s.v.load(std::memory_order_relaxed);
        return t;
    }

private:
    std::vector<PerCoreCounter> slots_;
};
```

## False sharing, measured

Two threads, each incrementing *its own* counter 20 million times. They never
touch each other's data. The only variable is whether the two counters share a
cache line:

| Placement | x86 Xeon (2 threads) | ARM64 (2 threads) |
| --- | --- | --- |
| Adjacent, same line | 720.09 ms | 225.26 ms |
| `alignas(64)`, separate lines | 145.05 ms | 32.46 ms |
| Penalty | **5.0x** | **6.9x** |

One keyword. Note the ARM penalty is *worse* — and ARM is what your access points
run.

The properties that make this so dangerous in production:

- It does not show up in a two-thread unit test at a meaningful magnitude.
- It gets worse with core count, so it appears when you scale up, not when you
  develop.
- It produces no wrong answers. The code is correct and simply slow.
- Profilers point at the increment instruction, which looks innocent.

**The classic offenders**, worth memorising as a checklist:

1. A ring buffer's `head` and `tail` declared adjacently. Producer writes head,
   consumer writes tail, and they thrash. This is *the* most common instance in
   networking code, and it is in the SPSC ring tutorial (`lld/03`).
2. An array of per-thread state without padding: `struct { int count; } stats[N];`
3. A lock adjacent to the data it protects — acquiring the lock invalidates the
   data line for everyone.
4. `std::vector<bool>` and other bit-packed containers written by several threads.
5. Two hot atomics in the same struct because they were logically related.

## Finding it: `perf c2c`

This is the tool almost no candidate has used, and saying its name correctly is
a genuine differentiator.

```bash
perf c2c record -F 60000 -a -- sleep 10
perf c2c report --stdio
```

What to look for in the report:

- **HITM** (hit-modified): a load that had to take the line from *another core's*
  modified state. This is the smoking gun for true or false sharing.
- The report groups by cache line and shows the offsets within the line that each
  core touched. If two different offsets in one line are hot on two different
  cores, that is false sharing. If the *same* offset is hot on both, that is true
  sharing and needs an algorithmic fix, not padding.

That distinction — same offset versus different offsets in the line — is the
whole diagnostic, and it is a crisp thing to be able to state.

## Padding correctly

```cpp
#include <new>      // std::hardware_destructive_interference_size (C++17)

// Prefer the standard constant where your toolchain implements it sanely.
// Caveat worth knowing: libstdc++ warns about ABI stability with it, and Apple
// silicon uses 128 B lines while the constant often reports 64. Measure.
#ifdef __cpp_lib_hardware_interference_size
    constexpr size_t kLine = std::hardware_destructive_interference_size;
#else
    constexpr size_t kLine = 64;
#endif

// Producer and consumer indices, each alone on its line.
class SpscIndices {
    alignas(kLine) std::atomic<uint64_t> head_{0};   // written by producer
    alignas(kLine) std::atomic<uint64_t> tail_{0};   // written by consumer
    alignas(kLine) char pad_{};                      // stop the next object sharing tail_'s line
};
```

The third padding member is the one people forget: without it, whatever the
allocator places after your object shares `tail_`'s line.

There is a real cost to this. Padding a 16 B struct to 64 B means a quarter of
the density and four times the cache footprint. On a *read-mostly* structure that
is a net loss. **Pad what is written by multiple threads; pack what is read by
many.** The inverse of false sharing is a genuine anti-pattern.

## Interview angles

**"Why doesn't this scale past four cores?"**
Coherence traffic on a shared line is the first hypothesis. `perf c2c` confirms.

**"What's the difference between false sharing and true sharing?"**
False: different variables, same line — fix with padding. True: same variable —
padding does nothing, you need per-core state or a different algorithm.

**"How expensive is an atomic increment?"**
Give the range and the reason: ~20 cycles uncontended because the line is already
in M, hundreds when contended because each write needs an exclusive round trip.
Candidates who quote one number without the contention caveat are marked down.

**"Is `relaxed` cheaper than `seq_cst`?"**
On x86, for RMW: no, both compile to a `lock` prefix. For *loads and stores*:
yes, `seq_cst` stores need an `xchg` or `mfence`. On ARM: yes, meaningfully, in
both cases. This ARM/x86 split is a great thing to volunteer.

**"Does coherence give me ordering?"**
No. Separate mechanisms. This is the highest-value distinction on this page.

## Exercises

1. Reproduce experiment 4 from `cache.cpp` and extend it: sweep thread count from
   1 to your core count, padded and unpadded. Plot both. The unpadded curve
   should get *worse* with more threads while the padded one stays flat.
2. Install `perf c2c` and catch your own false sharing red-handed. Confirm the
   HITM count drops to near zero after padding.
3. Build the `Stats` class above and compare it against a single shared atomic at
   1, 2, 4 and 8 threads. Plot increments per second. Explain the shape.
4. Measure `fetch_add` with `relaxed` vs `seq_cst` on x86 and on your ARM
   machine. Disassemble both (`objdump -d`). Write down the instruction
   difference for each architecture. This exercise directly prepares tutorial 10.
5. Find a padded struct in real code — the Linux kernel's
   `____cacheline_aligned_in_smp` is everywhere — and write a paragraph on why
   *that specific* field needed it.
