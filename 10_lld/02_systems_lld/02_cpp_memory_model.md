# LLD-S 02 — The C++ memory model in practice

> Prerequisite: `05_operating_systems/08_memory_barriers.md`. That tutorial covers the
> hardware; this one covers what you write and how to reason about it.

## What the standard actually promises

The C++ memory model is defined in terms of **happens-before**, built from:

1. *Sequenced-before* — ordering within one thread.
2. *Synchronises-with* — a release operation and an acquire operation that reads
   its value.
3. Transitivity of the two.

If a write and a read of the same non-atomic location are **not** ordered by
happens-before, and at least one is a write, that is a **data race** and the
program has undefined behaviour. Not "a stale value" — undefined behaviour, with
the compiler free to do anything.

This is why "it's just an int, a torn read is fine" is wrong as a matter of
language semantics, before you even get to the hardware.

## Atomics as the synchronisation edge

```cpp
#include <atomic>
#include <cassert>

struct Payload { int a; double b; char c[32]; };

Payload payload;                              // plain, non-atomic
std::atomic<bool> published{false};

// Producer
void produce() {
    payload.a = 42;
    payload.b = 3.5;
    std::strcpy(payload.c, "ready");
    published.store(true, std::memory_order_release);   // the edge
}

// Consumer
void consume() {
    if (published.load(std::memory_order_acquire)) {    // the edge
        assert(payload.a == 42);                        // guaranteed
    }
}
```

The payload does not need to be atomic. **One atomic carries the ordering for an
arbitrary amount of ordinary data.** That is the central practical insight, and
it is what makes release/acquire cheap: you pay for one operation, not for every
field.

## Choosing an order: a decision procedure

```
  Is this a statistics counter you only ever aggregate?      -> relaxed
  Is this publishing data for another thread to read?        -> release (store)
  Is this receiving data published by another thread?        -> acquire (load)
  Is it a read-modify-write that both receives and publishes? -> acq_rel
  Do you need a single global order across MULTIPLE atomics?  -> seq_cst
  Not sure?                                                   -> seq_cst
```

The last line is not a cop-out; it is the correct engineering default. Start
`seq_cst`, prove correctness, then relax individual sites with a written
justification when profiling shows the cost. Relaxing first and hoping is how
subtly broken concurrent code ships.

## The patterns worth memorising

### Flag publication

```cpp
// Producer
data = compute();
ready.store(true, std::memory_order_release);

// Consumer
while (!ready.load(std::memory_order_acquire)) { /* spin or yield */ }
use(data);
```

### Reference counting

```cpp
// Increment: relaxed is sufficient. You already hold a reference, so the object
// cannot die, and no data is being published by the increment itself.
ref_.fetch_add(1, std::memory_order_relaxed);

// Decrement: release, so that everything this thread did to the object happens
// before the destructor. Then an acquire fence on the thread that actually
// destroys, so it sees all other threads' work.
if (ref_.fetch_sub(1, std::memory_order_release) == 1) {
    std::atomic_thread_fence(std::memory_order_acquire);
    delete this;
}
```

This exact pattern is in Boost and in libstdc++'s `shared_ptr`. Being able to
explain **why the decrement is release and the destroy path needs an acquire
fence** is one of the highest-signal answers in a concurrency interview.

### Sequence lock (userspace version)

```cpp
// Cheap writes, wait-free-ish reads with retry. The userspace analogue of the
// kernel seqlock in 08_kernel/05_rcu_and_kernel_sync.
template <class T>
class SeqLock {
    static_assert(std::is_trivially_copyable_v<T>);
public:
    void store(const T& v) noexcept {
        const auto s = seq_.load(std::memory_order_relaxed);
        seq_.store(s + 1, std::memory_order_relaxed);        // odd == writing
        std::atomic_thread_fence(std::memory_order_release);
        value_ = v;
        std::atomic_thread_fence(std::memory_order_release);
        seq_.store(s + 2, std::memory_order_relaxed);        // even == stable
    }

    T load() const noexcept {
        T out;
        for (;;) {
            const auto s1 = seq_.load(std::memory_order_relaxed);
            if (s1 & 1) continue;                            // write in progress
            std::atomic_thread_fence(std::memory_order_acquire);
            out = value_;
            std::atomic_thread_fence(std::memory_order_acquire);
            if (seq_.load(std::memory_order_relaxed) == s1) return out;
        }
    }

private:
    mutable std::atomic<uint64_t> seq_{0};
    T value_{};
};
```

Note the honest caveat: strictly, `value_` being non-atomic means a concurrent
read during a write is a data race by the letter of the standard, even though the
retry discards the result. Real implementations either accept this (it works on
every real compiler and hardware) or use `atomic_ref` / per-word atomics. Knowing
that the textbook seqlock has this wart is a genuinely sophisticated point.

## `std::atomic_ref` (C++20)

Apply atomic operations to an object that is not itself atomic:

```cpp
#include <atomic>

// Useful when you have a large array of plain values and only sometimes need
// atomic access — e.g. a statistics array shared with a non-atomic fast path.
void bump(uint64_t* counters, size_t i) {
    std::atomic_ref<uint64_t> ref(counters[i]);
    ref.fetch_add(1, std::memory_order_relaxed);
}
```

This solves a real problem: previously you had to make the whole array
`std::atomic<uint64_t>`, which changes its layout and its interoperability with
C APIs and DMA buffers.

## What `volatile` is and is not

```cpp
volatile int x;     // NOT thread synchronisation.
```

`volatile` means "do not optimise away or reorder *this* access relative to other
volatile accesses." It provides:

- No atomicity. `x++` on a volatile is still read-modify-write.
- No ordering against non-volatile accesses.
- Nothing whatsoever to the hardware.

Its legitimate uses: memory-mapped I/O registers, and variables touched by a
signal handler (`volatile sig_atomic_t`). For threads, always `std::atomic`.

This question comes up constantly, and getting it crisply right is a quick win.

## Testing concurrent code

```bash
# ThreadSanitizer — the single highest-value flag in concurrent C++.
g++ -fsanitize=thread -O1 -g -std=c++20 test.cpp -o test && ./test

# Combine with a stress harness. TSAN only finds what you actually execute,
# so randomised interleaving matters:
for i in $(seq 100); do ./test --threads=8 --seed=$i || break; done
```

Structural advice that matters more than the flags:

- **Make the data structure testable in isolation.** A lock-free queue with a
  randomised multi-threaded harness plus TSAN is verifiable. The same queue
  embedded in a packet pipeline is not.
- **Deterministic seeds** so failures reproduce.
- **Run on the weak-ordering target.** TSAN checks the C++ happens-before relation, so on x86 it does flag a missing
  `release` or `acquire` when the flag guards plain data, even though x86 supplies the ordering for free
  (`04_computer_organization/31_arm_vs_x86_and_putting_it_together.md`, Problem A). It cannot flag a weak order that involves only
  atomic accesses, and it says nothing about the hardware, so the weak-ordering target is still the test.
  This is the single most common way lock-free code ships broken.
- For a core primitive, run it once through a model checker (CDSChecker, relacy)
  or write a herd7 litmus test.

## Interview angles

**"Explain the C++ memory model."**
happens-before, built from sequenced-before plus synchronises-with. Then a data
race is UB, not a stale read.

**"Why is the refcount decrement release?"**
So all work on the object happens-before destruction; plus the acquire fence on
the destroying thread.

**"Is `volatile` enough for a flag between threads?"**
No. No atomicity, no ordering, nothing to the hardware.

**"When is relaxed safe?"**
When you need atomicity but no ordering of surrounding data — counters, and the
non-final refcount increment.

**"How do you test this?"**
TSAN plus randomised stress with fixed seeds, run on the weakest target you ship
to. Mention a model checker for the core primitive.

## Exercises

1. Implement the `SeqLock` above and stress it: one writer, seven readers,
   verifying every read is a consistent snapshot. Run under TSAN and note what it
   says about the non-atomic `value_`.
2. Implement an intrusive refcounted base with the release/acquire-fence pattern.
   Then weaken the decrement to relaxed and construct a test that fails on ARM.
3. Take the SPSC ring from `05_operating_systems/08_memory_barriers`, compile it for x86 and aarch64, and
   annotate each generated instruction with which memory order produced it.
4. Write a data race deliberately, run it under TSAN, and read the report end to
   end — both stacks, and the "location" section. Reading TSAN output fluently is
   a skill worth 20 minutes.
5. Implement a `std::atomic_ref`-based statistics array and compare its layout
   and performance against `std::atomic<uint64_t>[]`. Note how it interacts with
   a plain C API expecting `uint64_t*`.
