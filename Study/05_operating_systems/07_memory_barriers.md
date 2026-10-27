# OS 07 — Memory barriers and the memory model

> Prerequisite: `04_computer_organization/20_memory_consistency_and_atomics.md` (the hardware models, litmus tests, fences and atomics, measured), `04_computer_organization/19_cache_coherence.md`, `04_computer_organization/31_arm_vs_x86_and_putting_it_together.md`.
> This is the hardest tutorial in the OS track and the highest-value one for
> lock-free work. `10_lld/02_systems_lld/02_cpp_memory_model` builds directly on it.

## Two models, and why you must keep them separate

- The **hardware memory model** says what reorderings the CPU may perform.
- The **C++ memory model** says what reorderings the *compiler* may perform and
  what guarantees you can demand portably.

Your code must be correct under both. The compiler will reorder, and it will do
so aggressively — a plain `volatile` stops the compiler but says nothing to the
hardware, which is precisely why `volatile` is not a concurrency tool in C++.

Coherence (`04_computer_organization/19_cache_coherence.md`) is a third, separate thing: it makes all cores agree on
the value of *one* location. Ordering is about *several* locations. Keeping
these three straight is genuinely half the battle.

## The canonical example

```cpp
// Thread A                      // Thread B
data = 42;                       if (ready) {
ready = true;                        use(data);   // is data guaranteed to be 42?
                                 }
```

With plain variables: no guarantee, and it is undefined behaviour besides. Thread
B may see `ready == true` while `data` is still stale, because either the
compiler or the hardware may have reordered the two stores, or B's loads.

```cpp
std::atomic<bool> ready{false};
int data = 0;                    // plain int is fine — the atomic carries the ordering

// Thread A (producer)
data = 42;                                          // (1)
ready.store(true, std::memory_order_release);       // (2) nothing above sinks below

// Thread B (consumer)
if (ready.load(std::memory_order_acquire)) {        // (3) nothing below rises above
    assert(data == 42);                             // (4) guaranteed
}
```

The guarantee is precise and worth stating exactly: **if the acquire load (3)
reads the value written by the release store (2), then everything sequenced
before (2) is visible to everything sequenced after (3).** Release/acquire pairs
*synchronise-with* each other; they do not create global ordering on their own.

## The six memory orders

| Order | Use | Cost on x86 | Cost on ARM |
| --- | --- | --- | --- |
| `relaxed` | Counters, statistics; atomicity only, no ordering | free | free |
| `consume` | Intended for dependency ordering; **effectively deprecated** — every compiler promotes it to `acquire` | — | — |
| `acquire` | Loads that publish-read a handoff | free | `ldar` |
| `release` | Stores that publish a handoff | free | `stlr` |
| `acq_rel` | Read-modify-write that does both | free (RMW already locked) | `ldaxr/stlxr` |
| `seq_cst` | Default; adds a single total order across all seq_cst ops | store costs `xchg`/`mfence` | `stlr` + stronger fencing |

Practical guidance that will not get you into trouble:

1. **Start with `seq_cst`** — it is the default and it is what your intuition
   expects. Get it correct first.
2. **Relax deliberately, one site at a time, with a written justification**, only
   after profiling shows the ordering costs something.
3. `relaxed` is safe for statistics counters where you only need atomicity.
4. Do not use `consume`. It is not implemented as specified anywhere.

## The barrier that is genuinely hard: StoreLoad

Dekker's pattern. Both threads write their own flag then read the other's:

```cpp
// Thread A                                  // Thread B
x.store(1, std::memory_order_relaxed);       y.store(1, std::memory_order_relaxed);
int b = y.load(std::memory_order_relaxed);   int a = x.load(std::memory_order_relaxed);
// can a == 0 && b == 0 ?  YES, even on x86.
```

Both can read zero, **on x86**, because each store sits in its core's store
buffer while the load bypasses it. This is the one reordering x86 allows, and
this is why `mfence` exists at all. Release/acquire does **not** fix it — they
constrain LoadLoad, LoadStore and StoreStore, not StoreLoad.

Only `seq_cst` (or an explicit `thread_fence(seq_cst)` between the store and the
load) forbids the outcome:

```cpp
x.store(1, std::memory_order_seq_cst);
int b = y.load(std::memory_order_seq_cst);   // now a==0 && b==0 is impossible
```

**This example is a very common interview question**, and "can both read zero?"
with the store-buffer explanation is exactly the answer they want. It is also
the cleanest demonstration that seq_cst is not merely a stronger acquire/release.

## Fences versus ordered operations

```cpp
// Operation-level ordering — preferred. Precise, and the compiler can often
// place it more cheaply.
flag.store(true, std::memory_order_release);

// Standalone fence — orders everything before against everything after.
// Heavier. Use when you need ordering around several unrelated operations.
std::atomic_thread_fence(std::memory_order_release);
flag.store(true, std::memory_order_relaxed);
```

`atomic_signal_fence` is a different animal: compiler-only, for signal handlers
on the same thread. No hardware instruction at all.

## The kernel's vocabulary

You will read this in driver code, so learn the mapping:

| Linux kernel | Meaning | C++ analogue |
| --- | --- | --- |
| `barrier()` | Compiler only | `atomic_signal_fence` |
| `smp_mb()` | Full barrier, SMP only | `thread_fence(seq_cst)` |
| `smp_rmb()` | Loads before / loads after | roughly acquire fence |
| `smp_wmb()` | Stores before / stores after | roughly release fence |
| `smp_store_release()` | Release store | `store(release)` |
| `smp_load_acquire()` | Acquire load | `load(acquire)` |
| `mb()` / `rmb()` / `wmb()` | Full barriers including device access | — |
| `dma_wmb()` / `dma_rmb()` | Order CPU access against DMA | — |
| `READ_ONCE` / `WRITE_ONCE` | Prevent compiler tearing/duplication | `relaxed` |

The `smp_*` variants compile to nothing on uniprocessor builds; the plain `mb()`
family does not, because devices are still there. **In a driver, ordering
descriptor writes against a doorbell write needs `dma_wmb()` and not
`smp_wmb()`** — a distinction that matters on ARM and is a good driver-interview
detail. See `09_drivers/07_dma.md`.

## A correct SPSC ring — the pattern to internalise

```cpp
#include <atomic>
#include <cstddef>
#include <new>

// Single producer, single consumer. Capacity must be a power of two.
template <class T, size_t Capacity>
class SpscRing {
    static_assert((Capacity & (Capacity - 1)) == 0, "capacity must be a power of two");
    static constexpr size_t kMask = Capacity - 1;
    static constexpr size_t kLine = 64;

public:
    // Producer side only.
    bool push(const T& item) noexcept {
        const size_t head = head_.load(std::memory_order_relaxed);   // we own head
        const size_t next = head + 1;
        // Acquire: we must see the consumer's slot-freeing before we overwrite.
        if (next - tail_.load(std::memory_order_acquire) > Capacity) return false;
        buf_[head & kMask] = item;
        // Release: the item write above must be visible before the index moves.
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer side only.
    bool pop(T& out) noexcept {
        const size_t tail = tail_.load(std::memory_order_relaxed);   // we own tail
        // Acquire: pairs with the producer's release store of head_.
        if (tail == head_.load(std::memory_order_acquire)) return false;
        out = buf_[tail & kMask];
        // Release: the read above must complete before we declare the slot free.
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

private:
    // Each index alone on its line: otherwise producer and consumer thrash
    // the same line and you lose most of the benefit (`04_computer_organization/19_cache_coherence.md`).
    alignas(kLine) std::atomic<size_t> head_{0};
    alignas(kLine) std::atomic<size_t> tail_{0};
    alignas(kLine) T buf_[Capacity];
};
```

Four things to be able to defend line by line, because this is exactly what an
interviewer will probe:

1. Each side reads **its own** index `relaxed` — it is the only writer, so no
   synchronisation is needed with itself.
2. Each side reads the **other's** index `acquire` — that is the synchronisation
   edge.
3. Each side publishes its own index `release` — after the data access, never
   before.
4. Using monotonically increasing indices and masking only at access time makes
   full-versus-empty unambiguous without wasting a slot.

Weaken any `release` to `relaxed` and this still passes every test on x86 and
fails on ARM. That is Problem A of `04_computer_organization/31_arm_vs_x86_and_putting_it_together.md` (on x86 the two builds are the same machine code and both pass; ThreadSanitizer flags the weakened one, and the Arm listing shows `ldr`/`str` where the correct build has `ldar`/`stlr`), and it is the best single
demonstration of why the memory model is not academic.

## Tools that actually find these bugs

Review does not find memory-ordering bugs. Tools do:

- **TSAN** (`-fsanitize=thread`) — catches data races at runtime. Slow, and only
  finds what you execute, but it is the highest-value single flag in concurrent
  C++.
- **Stress on the weakest hardware you ship to.** For you, that is an ARM AP.
- **herd7 / litmus tests** — formal exploration of small litmus tests against a
  hardware model. Worth doing once for a core primitive.
- **CDSChecker / relacy** — exhaustive exploration of C++ atomics orderings for a
  small data structure.

Naming TSAN plus "test on the weak-ordering target" is the answer to "how do you
validate this?" Saying "careful code review" is not.

## Interview angles

**"Explain acquire/release."**
Give the precise conditional: *if* the acquire load reads the release store's
value, *then* prior writes are visible. Do not say "it flushes the cache" — that
is wrong and interviewers notice.

**"Can both threads read zero in Dekker's?"**
Yes, store buffering, even on x86. Only seq_cst prevents it.

**"Is `volatile` enough?"**
No. Compiler-only; no hardware ordering, no atomicity. It is for MMIO and signal
handlers, not threads.

**"Why is `relaxed` not free on ARM but free on x86?"**
Relaxed *is* free on both. It is `acquire`/`release`/`seq_cst` that cost on ARM
and are free on x86. Getting this the right way round matters.

**"Walk me through your SPSC ring's memory orders."**
The four points above.

## Exercises

1. Write Dekker's pattern with `relaxed` and run it on x86 with two pinned
   threads, counting occurrences of `a==0 && b==0`. You will see them. Switch to
   `seq_cst` and confirm they vanish.
2. Compile the SPSC ring for x86-64 and aarch64 and diff the generated
   instructions for `push`. Identify exactly which instruction the `release`
   became on each.
3. Weaken one `release` to `relaxed`, run under TSAN, and record what TSAN says.
   Then run it on ARM under load without TSAN and record how long it takes to
   produce a wrong result.
4. Read `include/asm-generic/barrier.h` and the `memory-barriers.txt` document in
   the kernel tree. Write a one-page summary of when `dma_wmb()` differs from
   `smp_wmb()`.
5. Take a small lock-free structure and run it through CDSChecker or write a
   herd7 litmus test for its core ordering claim. Doing this once changes how you
   think about the rest.
