# 12 — Low-latency design: atomics, memory models, and thread-safe containers

> Prerequisite: `10`, `11`. Those two files covered protecting shared state
> (mutex/atomic basics) and managing tasks/threads. This file is the third
> piece the syllabus asks for explicitly: **atomics and memory ordering in
> enough depth to design a thread-safe container**, plus two worked designs
> — a thread-safe **Stack** and a **fine-grained (striped-lock) HashMap** —
> that the problem set hasn't needed until now because every prior
> concurrency extension used either a single coarse-grained mutex (Monitor
> Object) or a single atomic word (the Rate Limiter's token count). For the
> full, Staff-depth memory-model treatment (all six `memory_order` values,
> the assembly underneath each, `std::atomic_ref`) —
> `lld/notes/02_cpp_memory_model.md` and `os_and_arch/10_memory_barriers.md`
> are the reference; this file gives you what you need to *design* with
> atomics correctly, not the full formal model.

## `std::atomic<T>` and compare-and-swap, restated precisely

An atomic operation is indivisible — no other thread can observe it
"half-done." `compare_exchange_weak`/`compare_exchange_strong` are the
building block nearly every lock-free structure is built from: "if the
current value is still what I last observed, replace it with my new value;
otherwise tell me what it actually is, so I can retry."

```cpp
std::atomic<int> counter{0};

// Lock-free increment via CAS loop — the pattern behind every lock-free
// structure in this file, already used concretely in the Rate Limiter
// problem's TokenBucket.
int expected = counter.load(std::memory_order_relaxed);
while (!counter.compare_exchange_weak(expected, expected + 1,
                                       std::memory_order_acq_rel,
                                       std::memory_order_relaxed)) {
    // expected was updated to the actual current value on failure — retry
}
```

**`_weak` vs `_strong`**: `compare_exchange_weak` may fail *spuriously*
(return false even when the value did match — a hardware artifact on some
architectures), so it must always be used in a retry loop, as above.
`compare_exchange_strong` never fails spuriously, at the cost of being
potentially slower on architectures where `_weak` would need to retry
internally to give that guarantee. **Default to `_weak` inside a loop**
(the common case, shown above); reach for `_strong` only for a one-shot
attempt outside a loop, where a spurious failure would be indistinguishable
from a real one and you can't just retry.

## Memory orders, the practical subset

The full six-value model is `os_and_arch/10_memory_barriers.md`'s territory
— here's the three you'll actually reach for when designing a container:

- **`memory_order_relaxed`** — atomicity only, no ordering guarantee
  relative to other memory operations. Correct for an independent counter
  nobody synchronizes *other* data through (a stats counter, the "how many
  times has this been called" case) — this is not "the unsafe one," it's
  the right choice whenever there's genuinely nothing else to order.
- **`memory_order_acquire`** (on a load) / **`memory_order_release`** (on
  a store) — the pairing that publishes data safely across threads. A
  `release` store makes every write that happened *before* it (in program
  order, on the writing thread) visible to any thread that subsequently
  does an `acquire` load of that same atomic and sees the new value. This
  is exactly the mechanism behind `10_concurrency_for_lld.md`'s `atomic<bool>`
  slot-occupancy fix in the Parking Lot problem, made explicit: the
  `compare_exchange` there needs `acq_rel` (acquire **and** release,
  combined — it's both reading and writing) precisely so that whatever the
  winning thread wrote about the slot before claiming it is visible to
  whoever reads it next.
- **`memory_order_seq_cst`** (the default if you omit the argument) — the
  strongest, simplest-to-reason-about ordering: all threads agree on a
  single global order of every `seq_cst` operation. Costs more on some
  architectures (notably ARM, where `os_and_arch/06_arm_vs_x86.md`'s weak
  ordering means `seq_cst` needs real fences x86 gets closer to "for free")
  than acquire/release. **The pragmatic default for this program**: start
  with `seq_cst` (or no explicit order at all) for correctness while
  designing; drop to `acquire`/`release`/`relaxed` only in a profiled hot
  path where you can precisely justify each choice — exactly the same
  "measure before reaching for the sophisticated tool" discipline as
  Object Pool (`07_design_patterns_creational.md`) and lock-free vs. mutex
  in the Rate Limiter problem.

## A thread-safe Stack — coarse-grained, and why that's the right call here

```cpp
template <typename T>
class ThreadSafeStack {
public:
    void push(T value) {
        std::lock_guard<std::mutex> lock(mu_);
        data_.push_back(std::move(value));
    }
    std::optional<T> pop() {
        std::lock_guard<std::mutex> lock(mu_);
        if (data_.empty()) return std::nullopt;
        T value = std::move(data_.back());
        data_.pop_back();
        return value;
    }
private:
    std::mutex mu_;
    std::vector<T> data_;
};
```

**Why this is coarse-grained (Monitor Object,
`10_concurrency_for_lld.md`) and not lock-free, and why that's the correct
default**: a stack's `push`/`pop` both touch the *same* single point (the
top) — there's no independent sub-resource to grant finer-grained locks
over the way `Level`s did in Parking Lot. A lock-free stack (a CAS loop on
a head pointer, Treiber's algorithm) exists and is a legitimate advanced
answer if explicitly asked for, but it introduces real complexity (the ABA
problem — cross-reference `lld/notes/04_lockfree_data_structures.md` for
the full treatment with hazard pointers) for a data structure where
contention is usually low enough that a plain mutex is both simpler and
just as fast in practice. Lead with the mutex version; name the lock-free
alternative and its cost (ABA, reclamation complexity) if pushed.

## A thread-safe HashMap with fine-grained locking — lock striping

This is the genuinely new technique in this file: a **single mutex around
the whole map** serializes every operation, even for unrelated keys — a
real bottleneck under high concurrent load. **Lock striping** divides the
map into N independent buckets, each with its own lock, so operations on
different buckets proceed fully in parallel.

```cpp
template <typename K, typename V, size_t StripeCount = 16>
class StripedHashMap {
public:
    void put(const K& key, V value) {
        auto& stripe = stripeFor(key);
        std::lock_guard<std::mutex> lock(stripe.mu);
        stripe.map[key] = std::move(value);
    }
    std::optional<V> get(const K& key) {
        auto& stripe = stripeFor(key);
        std::lock_guard<std::mutex> lock(stripe.mu);
        auto it = stripe.map.find(key);
        if (it == stripe.map.end()) return std::nullopt;
        return it->second;
    }
    bool erase(const K& key) {
        auto& stripe = stripeFor(key);
        std::lock_guard<std::mutex> lock(stripe.mu);
        return stripe.map.erase(key) > 0;
    }

private:
    struct Stripe {
        std::mutex mu;
        std::unordered_map<K, V> map;
    };

    Stripe& stripeFor(const K& key) {
        size_t idx = std::hash<K>{}(key) % StripeCount;   // same hash function
        return stripes_[idx];                              // picks the same stripe
    }                                                       // for a given key every time

    std::array<Stripe, StripeCount> stripes_;
};
```

**Why this scales**: two threads operating on keys that hash to different
stripes never contend at all — up to `StripeCount`-way real parallelism for
independent keys, versus a single global mutex's hard serialization.
`StripeCount` is a real, tunable tradeoff: too few stripes and you're
close to a single global lock; too many and you waste memory on mostly-idle
mutexes and hurt cache locality (each `Stripe` object, containing both a
mutex and a map, becomes a meaningfully-sized unit — `os_and_arch/03_cache_coherence_mesi.md`'s
false-sharing concern applies directly if stripes are packed tightly enough
that two threads' stripes land on the same cache line). A common real-world
choice is `StripeCount` proportional to expected thread count (a small
multiple of core count), not the expected key count.

**What this trades away, worth stating if asked**: no operation that needs
a *consistent view across the whole map* (a `size()` that's exactly right
at one instant, an iteration over every key) is possible without locking
every stripe — which then serializes just as badly as the naive version for
that one operation. This is an intentional design point: **striping
optimizes the common case (independent single-key operations) at the cost
of the rare case (whole-map operations)**, exactly mirroring the
reader-writer lock tradeoff in `10_concurrency_for_lld.md` — a design
decision justified by the actual access pattern, not applied reflexively.

**The industrial-strength version of this idea**: this is conceptually how
Java's pre-Java-8 `ConcurrentHashMap` worked internally (striped locking),
and it's the same core idea (partition to reduce contention) behind
consistent-hash sharding at a much larger scale in
`hld/notes/04_distributed_caching.md` — worth naming that connection if
the conversation turns toward "how would this look distributed."

## Interview angles

**"How would you make this hash map thread-safe without serializing every
operation behind one lock?"** Lock striping, with the tradeoff (fast
independent ops, slow whole-map ops) stated explicitly, not just the
technique.

**"What memory order would you use here, and why?"** Identify whether the
atomic is standalone (relaxed is fine) or publishing other data to another
thread (needs acquire/release) — this file's three-way breakdown is the
answer shape; naming `seq_cst` as your safe starting point before
optimizing is a legitimate, honest answer too.

**"Would you use a mutex or go lock-free for this stack/queue/map?"** Name
the actual contention pattern (single hot point vs. partitionable) before
answering — a single mutex is frequently the right answer, and saying so
with a reason is stronger than defaulting to lock-free enthusiasm.

## Exercises

1. Implement `StripedHashMap`, then benchmark it against a single-mutex
   `unordered_map` wrapper under high concurrent load (many threads doing
   random-key `put`/`get`) at a few different `StripeCount` values (1, 4,
   16, 64) — plot throughput vs. stripe count on your own machine and find
   the point of diminishing returns.
2. Implement `ThreadSafeStack`, then look up (don't necessarily implement
   unless you want the extra rep) Treiber's lock-free stack algorithm and
   write one paragraph on why it needs hazard pointers or epoch
   reclamation to be memory-safe — cross-reference
   `lld/notes/04_lockfree_data_structures.md`'s treatment of exactly this
   problem for the flow-table case.
3. Take the Parking Lot problem's `std::atomic<bool> occupied_` fix
   (`02_parking_lot.md`) and rewrite its `compare_exchange` call with each
   of `memory_order_relaxed`, `acquire`/`release`, and `seq_cst` explicitly,
   and write one sentence on which is actually correct here and why the
   others would be a latent bug even though they'd likely "work" in
   testing (a classic property of memory-ordering bugs: they're
   timing-dependent and can pass every test on your machine).
4. Add a `size()` method to `StripedHashMap` that locks every stripe in a
   fixed order (lowest index first — the same deadlock-avoidance ordering
   technique from `10_concurrency_for_lld.md`'s `transfer()` fix) to get an
   exact count, and write one sentence on why this operation, unlike
   `get`/`put`, can't avoid full serialization.
