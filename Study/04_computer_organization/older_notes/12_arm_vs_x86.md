# CO 12 — ARM vs x86 for systems engineers

> **Older note.** Written before the current plan. In the plan it becomes tutorial **CO 31**, and it will be rewritten in the simple tutorial style before we reach it. The number in the title above is the old one.

> Prerequisite: `07_cache_coherence_mesi.md`, `10_branch_prediction.md`.
> This is the tutorial that makes your Wi-Fi background an asset. Your APs are
> ARM; most interview folklore is x86. The gap is yours to exploit.

## The difference that actually matters

Not RISC vs CISC. That framing has been obsolete for twenty years — x86 cores
decode into RISC-like uops, and ARM has plenty of complex instructions.

The two differences that produce real engineering consequences are:

1. **Memory ordering.** x86 is strong (TSO). ARM is weak. This changes what your
   lock-free code compiles to and whether it is correct.
2. **Instruction encoding.** Fixed 4-byte on AArch64, variable 1-15 byte on x86.
   This drives decode width, the existence of the uop cache, and I-cache density.

Everything else — register count, addressing modes, condition flags — is minor
by comparison.

## Memory ordering, concretely

### x86-TSO

x86 permits exactly **one** reordering: a store followed by a load to a
*different* address may appear reordered, because stores sit in the store buffer.
Everything else is kept in order:

```
Load  -> Load    NOT reordered
Load  -> Store   NOT reordered
Store -> Store   NOT reordered
Store -> Load    MAY be reordered   <-- the only one
```

Consequence: `memory_order_acquire` on a load and `memory_order_release` on a
store are **free** on x86. They emit a plain `mov` plus a compiler-level fence.
Only `seq_cst` stores cost anything (an `xchg` or an explicit `mfence`).

(Exception noted in `../14_cache_optimisation_and_the_real_hierarchy.md`, section 8.3: non-temporal stores are weakly ordered even on
x86 and need `sfence`.)

### ARMv8

AArch64 genuinely reorders loads and stores. Ordering must be requested:

| C++ | x86-64 emits | AArch64 emits |
| --- | --- | --- |
| `load(relaxed)` | `mov` | `ldr` |
| `load(acquire)` | `mov` | `ldar` |
| `store(relaxed)` | `mov` | `str` |
| `store(release)` | `mov` | `stlr` |
| `store(seq_cst)` | `xchg` (or `mov`+`mfence`) | `stlr` |
| `fetch_add(relaxed)` | `lock xadd` | `ldxr`/`stxr` loop (or `ldadd` with LSE) |
| `thread_fence(seq_cst)` | `mfence` | `dmb ish` |

**The practical consequence, and this is your story to tell:** lock-free code
written and tested on an x86 developer laptop can be missing acquire/release
annotations entirely and still pass every test, because x86 provides that
ordering for free. Ship the same source to an ARM access point and it breaks —
rarely, under load, in the field.

That is not a hypothetical. It is one of the most common real-world concurrency
failure modes, and you work on exactly the platform where it lands. Say this in
an interview and you separate yourself immediately from candidates who have only
read about memory models.

**Always develop concurrent code with TSAN, and always test on the weakest
target you ship to.**

### LL/SC vs CAS

x86 has a true atomic compare-exchange (`lock cmpxchg`). Classic ARM has
load-linked / store-conditional:

```
retry:
    ldxr  x0, [x1]        // load exclusive, sets a monitor
    ...compute...
    stxr  w2, x3, [x1]    // store exclusive; w2 = 0 on success
    cbnz  w2, retry       // monitor lost -> retry
```

The monitor can be lost **spuriously** — an interrupt, a context switch, or
another core touching the same *reservation granule* (often a whole cache line)
clears it. So `compare_exchange_weak` may fail without the value having changed,
which is exactly why the C++ standard has both `weak` and `strong` forms.

```cpp
// weak: allowed to fail spuriously, so it must live in a loop.
// This is the CORRECT form for a retry loop and compiles to a single
// LL/SC sequence on ARM. Using strong here would add a redundant inner loop.
uint64_t expected = counter.load(std::memory_order_relaxed);
while (!counter.compare_exchange_weak(expected, expected + 1,
                                      std::memory_order_release,
                                      std::memory_order_relaxed)) {
    // expected was updated by the failed exchange; just retry.
}

// strong: use when you are NOT looping, e.g. a one-shot claim.
bool claimed = flag.compare_exchange_strong(expect_false, true,
                                            std::memory_order_acq_rel);
```

Knowing *why* `weak` exists — spurious LL/SC failure on ARM — rather than just
"use weak in loops" is the level of answer that scores.

ARMv8.1 added **LSE** atomics (`ldadd`, `swp`, `cas`), which are single
instructions and scale far better under contention than LL/SC retry loops.
Build with `-march=armv8.1-a` or later to get them. On a contended counter the
difference is large, and it is a good concrete thing to have measured.

## Instruction encoding and its knock-on effects

| | x86-64 | AArch64 |
| --- | --- | --- |
| Encoding | 1-15 bytes, variable | 4 bytes, fixed |
| Decode | Hard; needs length-finding, hence uop cache | Trivial; wide decode is cheap |
| I-cache density | Better for simple ops (some are 2-3 B) | Worse per instruction, but fewer instructions needed |
| GP registers | 16 | 31 |

More registers is a real advantage: less spilling, which means fewer stack
accesses in hot loops. When you look at compiler output for the same function on
both, AArch64 versions typically have noticeably less stack traffic.

## Cache and TLB differences that bit us in the lab

Do not assume x86 geometry. From the measurements in `benchmarks/`:

| | x86 Xeon | ARM64 (your machine) |
| --- | --- | --- |
| Cache line | 64 B | 64 B (128 B on Apple silicon) |
| L1 hit, pointer chase | 1.77 ns | 0.70 ns |
| False sharing penalty | 5.0x | **6.9x** |
| Hugepage benefit at 16 MiB | ~1.2x | **4.4x** |
| 4K aliasing penalty at 4 MiB | 4.5x | 3.2x |

Two of those deserve emphasis because they invert common advice:

- **Hugepages mattered far more on ARM** in our measurement — 4.4x versus
  roughly 1.2x. If you carry "hugepages are a minor optimisation" from x86
  folklore onto an ARM data plane, you leave a large win on the table.
- **False sharing was worse on ARM.** Padding discipline matters more, not less,
  on your target platform.

Both of these are findings *from your own hardware*. That is the kind of detail
that makes an interview answer sound like experience rather than reading.

## Other practical differences

- **Unaligned access.** Both handle it in normal memory, but ARM faults on
  unaligned access to *device* memory, which matters in drivers. In the kernel,
  use the `get_unaligned`/`put_unaligned` helpers rather than casting.
- **Endianness.** Both are little-endian in practice (AArch64 can be big-endian
  but effectively never is). Network byte order conversions are still required.
- **Weak memory + DMA.** Device drivers on ARM need the right barriers between
  descriptor writes and doorbell writes. `dma_wmb()` and `writel()` semantics are
  not decoration — see `09_drivers/07_dma.md`.
- **Cycle counter access.** `rdtsc` on x86 versus `CNTVCT_EL0` on ARM, and on ARM
  the PMU counters usually need kernel permission to read from userspace.

## Interview angles

**"What's the difference between ARM and x86?"**
Do not say RISC vs CISC. Say memory ordering and instruction encoding, then give
one concrete consequence of each. This alone puts you in a small minority.

**"Why does `compare_exchange_weak` exist?"**
Spurious LL/SC failure on ARM-class hardware. Strong forms have to add a loop to
hide it, which is wasted work if you were looping anyway.

**"Your lock-free queue works on your laptop and fails on the target. Why?"**
The answer of this whole tutorial. It is also, not coincidentally, an excellent
war story if you have ever actually hit it.

**"How would you validate concurrent code for ARM?"**
TSAN, stress on the real target, `-march` matching the deployment target, and
ideally a model checker (CDSChecker, herd7) for the core primitive. Mentioning
tooling rather than "careful review" is the senior answer.

## Exercises

1. Write a small function using `std::atomic` with each memory order, compile for
   both `x86-64` and `aarch64` (godbolt is fine), and build the instruction table
   above from your own observation. Do not copy mine; produce it.
2. Benchmark a contended `fetch_add` on your ARM machine with
   `-march=armv8-a` versus `-march=armv8.1-a` (LL/SC versus LSE). Report the
   difference at 2 and 4 threads.
3. Take the SPSC ring from `10_lld/02_systems_lld/03_thread_safe_primitives`, deliberately weaken the release store to
   relaxed, and run it under TSAN on ARM under load. Document what happens and
   how long it took to fail. Keep this as an interview story.
4. Rerun cache.cpp (removed) and tlb2.cpp (removed) on an actual access point. Write
   `12_arm_findings.md` beside this file with every disagreement you find against
   the x86 numbers. This file is one of the most valuable artifacts in the whole
   study system — it is yours and nobody else has it.
