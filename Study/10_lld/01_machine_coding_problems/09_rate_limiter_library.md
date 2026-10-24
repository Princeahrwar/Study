# 09 — Rate Limiter Library

> Difficulty: advanced, specifically because the interesting version of
> this problem is **lock-free**. The algorithms themselves (Token Bucket,
> Leaky Bucket, Sliding Window) are already covered thoroughly at
> distributed-systems scale in `11_hld/01_fundamentals/15_rate_limiters.md` — read that
> first for the algorithm survey and the distributed-enforcement tradeoffs
> (central store vs. local buckets vs. consistent hashing). **This file is
> the different, LLD-scale question**: implement a single-process,
> in-memory, per-key rate limiter *library* — something a service would
> link against directly — optimized for minimal per-call overhead, which is
> where atomics and lock-free technique genuinely earn their place.
> Prerequisite: `00_foundations/10_concurrency_for_lld.md`,
> `10_lld/02_systems_lld/02_cpp_memory_model.md`.

## Step 1 — Clarify

Assumed answers:

- A library exposing `bool tryAcquire(key)` — returns whether the caller
  is allowed to proceed *right now* for that key (a user id, an API key, an
  IP address).
- Support **multiple keys** simultaneously, each with independent limits —
  not a single global limiter.
- Algorithm: **Token Bucket** — chosen explicitly over Sliding Window for
  this problem because it's the one that maps cleanly onto a single atomic
  variable per key (see below); state this choice and why, rather than
  picking arbitrarily.
- Called from **multiple threads concurrently**, extremely frequently (this
  is the whole reason lock-freedom matters here — a mutex-per-call on a
  hot request path is exactly the kind of contention this problem is
  testing whether you'd avoid).
- **Deferred, explicitly**: distributed enforcement across multiple
  processes/machines — that's the `11_hld/01_fundamentals/15_rate_limiters.md` problem,
  a different scale entirely; this file is single-process only.

## Step 2 — Entities

| Entity | Responsibility |
| --- | --- |
| `TokenBucket` | One key's state: current tokens, last refill time — the lock-free core |
| `RateLimiter` | Owns a `TokenBucket` per key, routes `tryAcquire(key)` to the right bucket |
| `RateLimitConfig` | Bucket capacity, refill rate — could vary per key (**growing set**, worth an interface if tiers are a real requirement) |

There's deliberately no Strategy interface for "which algorithm" in the
core implementation below — Token Bucket is fixed by the clarified
requirement. If a follow-up asks to support multiple algorithms, *that's*
the moment to introduce a `RateLimitAlgorithm` interface (Strategy) — doing
it up front for a single, stated algorithm would be the YAGNI violation
`00_foundations/17` warns about.

## Step 3 — Why Token Bucket is the lock-free-friendly choice

A token bucket's state is fully described by two numbers: **current token
count** and **last refill timestamp**. Refilling and consuming can both be
expressed as: compute how many tokens *should* exist right now given
elapsed time, clamp to capacity, then attempt to atomically decrement by
one if the result is `>= 1`. This is expressible as a **single 64-bit
atomic** by packing both values into one word, letting the whole operation
become one `compare_exchange` loop with no lock at all — a Sliding Window
counter, by contrast, needs a log of individual request timestamps, which
doesn't reduce to a single atomic word nearly as cleanly. Naming this
"why Token Bucket specifically, for a lock-free single-key limiter" is a
stronger answer than reciting the algorithm from memory.

## Step 4-5 — Interfaces and core implementation

```cpp
#include <atomic>
#include <chrono>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>
#include <memory>
#include <cmath>

using Clock = std::chrono::steady_clock;

// Packs (tokens, lastRefillMillis) conceptually; implemented here as two
// separate atomics updated via a CAS retry loop on the token count, which
// is simpler to reason about correctly than manual bit-packing and is the
// version worth writing live — bit-packing into one word is a valid
// "further optimization" to mention, not to default to under time pressure.
class TokenBucket {
public:
    TokenBucket(double capacity, double refillPerSecond)
        : capacity_(capacity), refillPerSecond_(refillPerSecond),
          tokens_(capacity), lastRefillMs_(nowMs()) {}

    bool tryAcquire() {
        refill();
        double current = tokens_.load(std::memory_order_relaxed);
        while (current >= 1.0) {
            // CAS loop: attempt to atomically claim one token. If another
            // thread claimed one first, `current` is updated to the actual
            // value by compare_exchange_weak's failure path, and we retry.
            if (tokens_.compare_exchange_weak(current, current - 1.0,
                                               std::memory_order_acq_rel,
                                               std::memory_order_relaxed)) {
                return true;
            }
            // loop retries with the freshly-observed `current`
        }
        return false;   // not enough tokens right now
    }

private:
    void refill() {
        int64_t now = nowMs();
        int64_t last = lastRefillMs_.load(std::memory_order_relaxed);
        int64_t elapsedMs = now - last;
        if (elapsedMs <= 0) return;

        double toAdd = (elapsedMs / 1000.0) * refillPerSecond_;
        if (toAdd <= 0) return;

        // Only one thread should actually perform a given refill window's
        // update; losing the race here just means another thread already
        // refilled for this interval, which is correct, not a bug.
        if (lastRefillMs_.compare_exchange_strong(last, now, std::memory_order_acq_rel)) {
            double current = tokens_.load(std::memory_order_relaxed);
            double updated;
            do {
                updated = std::min(capacity_, current + toAdd);
            } while (!tokens_.compare_exchange_weak(current, updated,
                                                     std::memory_order_acq_rel,
                                                     std::memory_order_relaxed));
        }
    }
    static int64_t nowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now().time_since_epoch()).count();
    }

    const double capacity_;
    const double refillPerSecond_;
    std::atomic<double> tokens_;
    std::atomic<int64_t> lastRefillMs_;
};

class RateLimiter {
public:
    RateLimiter(double capacity, double refillPerSecond)
        : capacity_(capacity), refillPerSecond_(refillPerSecond) {}

    bool tryAcquire(const std::string& key) {
        return getOrCreateBucket(key).tryAcquire();
    }

private:
    TokenBucket& getOrCreateBucket(const std::string& key) {
        {   // fast path: bucket already exists, only need a shared (read) lock
            std::shared_lock read(mu_);
            auto it = buckets_.find(key);
            if (it != buckets_.end()) return *it->second;
        }
        {   // slow path: create it, need an exclusive (write) lock
            std::unique_lock write(mu_);
            auto it = buckets_.find(key);
            if (it != buckets_.end()) return *it->second;   // re-check: someone else created it first
            auto [inserted, _] = buckets_.emplace(key, std::make_unique<TokenBucket>(capacity_, refillPerSecond_));
            return *inserted->second;
        }
    }

    double capacity_, refillPerSecond_;
    std::shared_mutex mu_;   // reader-writer lock: bucket lookup is read-heavy, per 00_foundations/10
    std::unordered_map<std::string, std::unique_ptr<TokenBucket>> buckets_;
};
```

**Why `getOrCreateBucket` uses `shared_mutex` with a double-checked
read-then-write pattern**: this is a direct, deliberate application of
`00_foundations/10`'s reader-writer lock guidance — bucket *lookup* (an
existing key, the overwhelmingly common case once the key set stabilizes)
should never block on other lookups, only on the rare event of a *new* key
appearing. The re-check inside the write lock (`it = buckets_.find(key)`
again) guards against two threads both missing the fast path for the same
new key and both trying to create it — without the re-check, the second
writer would silently overwrite the first's bucket, discarding whatever
tokens it had already consumed.

**Why `TokenBucket::tryAcquire` itself needs no mutex at all**: this is the
actual lock-free part, and it's worth being explicit that it's lock-free
specifically *because* the whole invariant (current token count) lives in
one atomic word — the CAS retry loop is the standard lock-free update
pattern from `10_lld/02_systems_lld/02_cpp_memory_model.md` and
`10_lld/02_systems_lld/04_lockfree_data_structures.md`'s CAS-loop section, applied
here to a concrete, self-contained problem rather than abstractly.

## Step 6 — Edge cases handled

- `refill()` uses a CAS on `lastRefillMs_` so concurrent callers within the
  same millisecond don't all separately add tokens for the same elapsed
  window — only the thread that wins the CAS performs that window's refill.
- `tokens_` never exceeds `capacity_`, clamped via `std::min` inside the
  refill CAS loop.
- A brand-new key gets its own fresh bucket at full capacity — no
  cross-contamination between keys' limits.

## Extensions (what a live follow-up usually asks)

**"What about memory growth — keys that appear once and never again?"**
The `buckets_` map grows unboundedly with the number of distinct keys ever
seen. A real library needs an eviction policy — this is a direct
cross-reference to `05_lru_cache.md`: wrap `buckets_` in an LRU structure
keyed by last-access time, evicting cold buckets. Naming this gap
unprompted, the way `02_parking_lot.md`'s pricing extension was named
unprompted, is the expected move.

**"Compare CAS-loop Token Bucket to a mutex-protected one — is the
complexity actually worth it?"** Be honest and specific: for **low**
contention (few threads, infrequent calls), a plain `std::mutex` around a
simple `TokenBucket` is simpler, easier to get right, and likely just as
fast in practice — lock-free code only pays for itself under **genuine, high
contention** on the *same key*, where CAS retries stay cheap and mutex
context-switch overhead would dominate. This is the same "measure before
reaching for the sophisticated tool" discipline as Object Pool
(`00_foundations/07`) and the `shared_mutex` caveat in `00_foundations/10`
— say it here too, it's the mark of judgment over reflexive lock-free
enthusiasm.

**"Support per-key custom limits (tiered rate limiting)."** `RateLimiter`
gains a `configure(key, capacity, refillRate)` and a lookup-or-default in
`getOrCreateBucket` — additive, doesn't touch `TokenBucket`'s internals at
all.

**"How would this look distributed across multiple servers?"** This is
exactly where `11_hld/01_fundamentals/15_rate_limiters.md` picks up — central Redis
store (`INCR` + `EXPIRE`, atomic at the Redis level), local buckets with
periodic reconciliation, or consistent-hashing the key to one owning node.
Name the three options from that file and the tradeoff between accuracy
and cross-node round-trip cost; don't re-derive them here.

## Interview framing to reuse

"The token count lives in a single atomic, so `tryAcquire` never needs a
lock — the CAS retry loop is the same pattern as any lock-free counter, and
it's worth it here specifically because this is meant to be called on a hot
request path by many threads. For a lower-contention use case I'd just use
a mutex; the lock-free version is a deliberate tradeoff for this problem's
stated scale, not a default." This sentence demonstrates both the
technique and the judgment about when it's earned — the combination is
the actual signal.

## Exercises

1. Implement `TokenBucket` and `RateLimiter` from scratch, then write a
   stress test: many threads hammering `tryAcquire` for the same key at a
   rate exceeding capacity, asserting the total number of successful
   acquisitions over a fixed window never exceeds `capacity + refillRate *
   elapsedSeconds` (within a small tolerance for timing jitter).
2. Rewrite `TokenBucket::tryAcquire` using a plain `std::mutex` instead of
   the CAS loop, and benchmark both under low contention (1-2 threads) and
   high contention (16+ threads hammering the same key) — confirm at what
   contention level the lock-free version actually wins, on your own
   machine, rather than assuming.
3. Add the LRU-eviction extension for stale keys, reusing
   `05_lru_cache.md`'s structure, and write a test confirming a key
   untouched past the eviction threshold is removed and a subsequent call
   for it starts a fresh bucket.
4. Implement a Sliding Window variant (`11_hld/01_fundamentals/15_rate_limiters.md` has
   the algorithm) and compare its memory profile per key against Token
   Bucket's two atomics — state in one sentence why Sliding Window's
   accuracy advantage costs more memory per key.
