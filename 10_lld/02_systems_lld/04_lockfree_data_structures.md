# LLD-S 04 — Lock-free data structures and safe reclamation

> Prerequisite: `03_thread_safe_primitives.md`, `02_cpp_memory_model.md`.
> The hardest LLD material, and the core of Project 03 (`pfilt`).

## Progress guarantees — say the right word

| Guarantee | Definition | Example |
| --- | --- | --- |
| **Blocking** | A stalled thread can block all others | Mutex |
| **Obstruction-free** | A thread finishes if it runs alone | Rare in practice |
| **Lock-free** | *Some* thread always makes progress | CAS retry loops |
| **Wait-free** | *Every* thread finishes in bounded steps | `fetch_add`, MPSC push |

"Lock-free" does **not** mean fast, and it does not mean starvation-free — an
individual thread can retry forever while others succeed. In a hard-real-time
context that distinction is the whole point, and using the words precisely is one
of the clearest seniority signals in this area.

## The CAS loop

```cpp
// The universal shape. Everything lock-free is a variation on this.
template <class T, class F>
void update(std::atomic<T>& a, F&& transform) {
    T old = a.load(std::memory_order_relaxed);
    T desired;
    do {
        desired = transform(old);
        // weak: may fail spuriously on ARM (LL/SC), which is fine in a loop.
        // On failure, 'old' is refreshed with the current value.
    } while (!a.compare_exchange_weak(old, desired,
                                      std::memory_order_release,
                                      std::memory_order_relaxed));
}
```

Under contention this degrades badly — every failed CAS is a coherence round trip
(tutorial `04_computer_organization/19_cache_coherence.md`). Mitigations, in order of preference:

1. **Avoid the sharing.** Per-core state is always better than a clever shared
   structure. This is the answer far more often than people expect.
2. **Back off** on retry — a short pause, or exponential backoff.
3. Prefer `fetch_add` and `exchange` over CAS loops where the operation allows;
   they are wait-free and single-instruction.

## The ABA problem

```
  Thread 1 reads head -> A, and prepares to CAS(head, A, A->next == B)
  Thread 2 pops A, pops B, frees both, pushes A again (same address)
  Thread 1's CAS SUCCEEDS — the pointer is still A — and sets head to B,
  which is now freed memory.
```

The pointer matched but the *world* changed. Three standard fixes:

1. **Tagged pointers.** Pack a counter into the unused high bits of a 64-bit
   pointer, or use a 128-bit CAS (`cmpxchg16b`, or `casp` on ARM). The tag makes
   A-with-tag-1 distinguishable from A-with-tag-2.
2. **Hazard pointers.** Readers publish what they are reading; reclaimers check.
3. **Epoch-based reclamation.** Readers enter an epoch; memory is freed only when
   all readers have left the epoch it was retired in.

ABA is *the* classic lock-free interview question. You need the mechanism and at
least two fixes.

## Hazard pointers

Each reader publishes the pointer it is about to dereference into a slot the
reclaimer can see. A reclaimer scans all slots and only frees pointers nobody
published.

```cpp
#include <atomic>
#include <array>
#include <vector>

template <class T, size_t MaxThreads = 64>
class HazardPointers {
public:
    // Publish and re-validate. The loop is essential: between reading src and
    // publishing, another thread could retire the object.
    T* protect(std::atomic<T*>& src, size_t tid) noexcept {
        T* p;
        do {
            p = src.load(std::memory_order_acquire);
            slots_[tid].ptr.store(p, std::memory_order_release);
        } while (p != src.load(std::memory_order_acquire));
        return p;
    }

    void clear(size_t tid) noexcept {
        slots_[tid].ptr.store(nullptr, std::memory_order_release);
    }

    void retire(T* p, size_t tid) {
        retired_[tid].push_back(p);
        if (retired_[tid].size() >= kBatch) scan(tid);
    }

private:
    static constexpr size_t kBatch = 128;   // amortise the O(MaxThreads) scan

    void scan(size_t tid) {
        std::vector<T*> in_use;
        in_use.reserve(MaxThreads);
        for (size_t i = 0; i < MaxThreads; ++i)
            if (T* h = slots_[i].ptr.load(std::memory_order_acquire)) in_use.push_back(h);

        auto& rl = retired_[tid];
        for (size_t i = 0; i < rl.size(); ) {
            if (std::find(in_use.begin(), in_use.end(), rl[i]) == in_use.end()) {
                delete rl[i];
                rl[i] = rl.back();
                rl.pop_back();
            } else {
                ++i;
            }
        }
    }

    struct alignas(64) Slot { std::atomic<T*> ptr{nullptr}; };
    std::array<Slot, MaxThreads> slots_{};
    std::array<std::vector<T*>, MaxThreads> retired_{};
};
```

Trade-off to state: readers pay a store and a re-validating load on every access,
but reclamation is **bounded** — you can say exactly how much memory is pending.
Good when memory is tight and reader cost is acceptable.

## Epoch-based reclamation

The opposite trade, and usually the right one for a packet path.

```cpp
#include <atomic>
#include <array>
#include <vector>

// Readers are nearly free: one relaxed store to enter, one to leave.
// Memory is freed two epochs after retirement, when no reader can hold it.
template <size_t MaxThreads = 64>
class EpochManager {
public:
    void enter(size_t tid) noexcept {
        const uint64_t e = global_.load(std::memory_order_acquire);
        local_[tid].epoch.store(e | kActive, std::memory_order_release);
    }

    void exit(size_t tid) noexcept {
        local_[tid].epoch.store(0, std::memory_order_release);
    }

    void retire(size_t tid, void* p, void (*deleter)(void*)) {
        const uint64_t e = global_.load(std::memory_order_relaxed);
        limbo_[e % 3].push_back({p, deleter});
        try_advance();
    }

private:
    static constexpr uint64_t kActive = 1ull << 63;

    void try_advance() {
        uint64_t e = global_.load(std::memory_order_relaxed);   // NOT const: the CAS
                                                                // writes to it on failure
        for (size_t i = 0; i < MaxThreads; ++i) {
            const uint64_t l = local_[i].epoch.load(std::memory_order_acquire);
            if ((l & kActive) && (l & ~kActive) != e) return;   // someone is behind
        }
        if (global_.compare_exchange_strong(e, e + 1,
                                            std::memory_order_acq_rel)) {
            // Everything retired two epochs ago is now unreachable.
            auto& bucket = limbo_[(e + 1) % 3];   // two epochs back
            for (auto& r : bucket) r.deleter(r.ptr);
            bucket.clear();
        }
    }

    struct Retired { void* ptr; void (*deleter)(void*); };
    struct alignas(64) Local { std::atomic<uint64_t> epoch{0}; };

    std::atomic<uint64_t> global_{0};
    std::array<Local, MaxThreads> local_{};
    std::array<std::vector<Retired>, 3> limbo_{};
};
```

**Hazard pointers vs epochs, stated as a trade:** hazard pointers cost the reader
more but bound memory; epochs give nearly-free readers but let memory grow
unboundedly if any reader stalls inside an epoch. For a packet path where readers
are short and bounded, epochs win — which is why this is what Project 03 (`pfilt`) uses,
and why RCU (the kernel's epoch scheme) is structured the same way.

## A practical lock-free flow table

This is the structure Project 03 (`pfilt`) is built around, and it embodies most of this
tree's lessons at once:

```cpp
#include <atomic>
#include <cstdint>

// Open addressing, bucket of four entries per 64 B cache line.
// One cache miss per lookup instead of three: the single most important
// property, and the reason for the layout (see 04_computer_organization/14_cache_optimisation_and_the_real_hierarchy, section 7.2).
struct alignas(64) Bucket {
    std::atomic<uint32_t> tags[4];      // 0 == empty; high bits of the hash
    uint32_t              keys[4][5];   // 5-tuple, 20 bytes each... too big.
    // In practice: store a 64-bit key digest here and keep the full 5-tuple
    // in a side array, so the bucket stays exactly one line.
};

// The realistic shape:
struct alignas(64) FlowBucket {
    std::atomic<uint64_t> keys[4];      // 32 B — SipHash digest of the 5-tuple
    std::atomic<uint32_t> vals[4];      // 16 B — index into a flow-state array
    std::atomic<uint32_t> epochs[4];    // 16 B — last-seen tick, for aging
};                                       // = 64 B exactly
static_assert(sizeof(FlowBucket) == 64);
```

Design decisions and the reasoning, which is what an interviewer wants:

- **Keyed hash (SipHash-class), not a fast non-cryptographic hash.** An attacker
  who controls the 5-tuple can otherwise generate collisions and turn your O(1)
  table into O(n). This is a real DoS and Palo Alto will ask about it.
- **Four entries per line** so a lookup is one miss. Beyond four you need a
  second line and lose the property.
- **2 MiB pages** for the table. At 400 MiB with 4 KiB pages it can never be
  TLB-resident (the arithmetic in `05_operating_systems/10_virtual_memory_and_tlb`), and on ARM we measured that
  as up to 4.4x.
- **Aging amortised into the packet path** — check a few buckets per packet
  rather than sweeping periodically, which is exactly the 400 us tail-latency bug
  in the mock interview.
- **Insert by CAS on an empty tag**; on a full bucket, evict the oldest epoch.
  Bounded work, no unbounded probe chain.

## When lock-free is the wrong answer

Say this unprompted; it is a maturity signal:

- **A mutex is often faster.** Under low contention, an uncontended mutex is ~20
  ns and involves no retry storms.
- **Sharding beats cleverness.** Per-core structures with no sharing outperform
  any shared lock-free structure, and they are far easier to reason about.
- **Lock-free code is hard to verify.** If you cannot run TSAN, stress on the weak
  target, and ideally a model checker, you probably should not ship it.
- **Lock-free is not starvation-free.** For hard real time you need wait-free.

## Interview angles

**"What is ABA and how do you fix it?"**
The mechanism, then tagged pointers / hazard pointers / epochs.

**"Hazard pointers vs epoch reclamation?"**
Reader cost vs memory bound. Give the packet-path conclusion.

**"Is your queue lock-free or wait-free?"**
Classify it precisely. Most candidates cannot.

**"Design a flow table for 1M flows at 3 Mpps."**
Bucket-per-cache-line layout, keyed hashing, hugepages with the TLB arithmetic,
amortised aging, bounded insert. This is a complete Staff answer.

**"When would you not use lock-free?"**
Low contention, verification cost, starvation concerns, and "shard it instead".

## Exercises

1. Implement a lock-free Treiber stack, then construct an ABA failure with a
   deliberately aggressive allocator that reuses addresses. This is hard to hit
   by accident and very instructive to hit on purpose.
2. Fix it three ways — tagged pointer, hazard pointers, epochs — and benchmark
   reader cost for each at 1, 4 and 8 threads.
3. Build the `FlowBucket` table. Verify `sizeof` is 64, then measure lookups per
   second with 4 KiB and 2 MiB pages on your ARM machine. Compare the ratio to
   the `05_operating_systems/10_virtual_memory_and_tlb` measurement.
4. Attack your own table: generate 5-tuples that collide under a non-keyed hash
   and measure the lookup degradation. Then switch to SipHash and show it is
   gone. This experiment is a portfolio piece by itself.
5. Take one structure through CDSChecker or relacy. Write up what it proved and
   what it could not.
