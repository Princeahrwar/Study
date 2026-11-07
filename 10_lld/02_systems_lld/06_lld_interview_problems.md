# LLD-S 06 — The LLD interview problem set

> Prerequisite: tutorials 01–05 in this folder.
> These are the problems that actually get asked. Work each one on paper first,
> then compare.
>
> **This file predates `10_lld/01_machine_coding_problems/`.** Four of its six
> problems are now superseded by longer, more detailed versions there:
> Rate Limiter → `10_lld/01_machine_coding_problems/09_rate_limiter_library.md`, LRU Cache →
> `10_lld/01_machine_coding_problems/05_lru_cache.md`, Bounded Blocking Queue → `10_lld/01_machine_coding_problems/17_bounded_blocking_queue.md`,
> Thread-safe Logger → `10_lld/01_machine_coding_problems/12_logger_framework.md`. Parking Lot/Elevator here
> is a brief sketch versus the full treatments in `10_lld/01_machine_coding_problems/02_parking_lot.md` and
> `10_lld/01_machine_coding_problems/03_elevator_system.md`. **Connection Pool is the one problem below not
> duplicated elsewhere** — that section is still worth reading in full.

## How an LLD round is scored

The code is maybe 40% of it. The rest:

1. **Clarifying questions before code.** Single or multi threaded? Bounded?
   Expected rate? Failure behaviour? Candidates who start typing immediately lose
   points even when the code is right.
2. **Naming the invariants.** "The queue is empty iff head == tail" is worth more
   than three extra methods.
3. **Stating the complexity and the concurrency guarantee** for each operation.
4. **Handling the boundaries**: empty, full, concurrent shutdown, exhaustion.
5. **Testability.** If you cannot say how you would test it, it is not done.

Open every problem with the questions. It is the single cheapest way to look more
senior.

---

## Problem 1 — Rate limiter

**Ask first:** per-key or global? Single process or distributed? Do you need to
smooth bursts or allow them? Is it OK to be approximate?

```cpp
#include <atomic>
#include <chrono>

// Token bucket. Allows bursts up to 'capacity', sustains 'rate' per second.
// Lock-free, monotonic clock, no background thread — tokens are computed
// lazily from elapsed time, which is the trick worth knowing.
class TokenBucket {
public:
    TokenBucket(double rate_per_sec, double capacity)
        : rate_(rate_per_sec), capacity_(capacity),
          tokens_(capacity), last_(now_ns()) {}

    [[nodiscard]] bool try_acquire(double n = 1.0) noexcept {
        for (;;) {
            const uint64_t now  = now_ns();
            const uint64_t last = last_.load(std::memory_order_acquire);
            double tokens = tokens_.load(std::memory_order_acquire);

            // Refill lazily: no timer, no drift, O(1).
            const double elapsed = static_cast<double>(now - last) * 1e-9;
            tokens = std::min(capacity_, tokens + elapsed * rate_);

            if (tokens < n) return false;

            // Publish the new state. CAS on tokens; last_ is advisory.
            double desired = tokens - n;
            if (tokens_.compare_exchange_weak(tokens, desired,
                                              std::memory_order_release,
                                              std::memory_order_relaxed)) {
                last_.store(now, std::memory_order_release);
                return true;
            }
        }
    }

private:
    static uint64_t now_ns() noexcept {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    const double rate_, capacity_;
    std::atomic<double> tokens_;
    std::atomic<uint64_t> last_;
};
```

Say out loud: **lazy refill** (no timer thread, no drift), `steady_clock` not
`system_clock` (wall clock can jump backwards), and that this version is slightly
racy between the two atomics — acceptable for rate limiting, and you should name
that rather than let them find it. If exactness matters, use one 128-bit state
word or a lock.

Follow-ups to expect: **sliding window** (more accurate at boundaries, more
memory), **leaky bucket** (smooths output, no bursts), and **distributed**
(Redis with a Lua script for atomicity, or local buckets with periodic quota
redistribution — mention that the local-with-sync design keeps the hot path fast
and only approximates globally).

---

## Problem 2 — LRU cache

**Ask first:** thread safe? Capacity by count or bytes? Do you need TTL? Is exact
LRU required or is approximate fine?

```cpp
#include <list>
#include <unordered_map>
#include <optional>
#include <shared_mutex>

// O(1) get and put. Intrusive list + hash map is the standard answer.
template <class K, class V>
class LruCache {
public:
    explicit LruCache(size_t capacity) : cap_(capacity) {}

    std::optional<V> get(const K& key) {
        std::lock_guard lk(m_);                 // note: get MUTATES order
        auto it = map_.find(key);
        if (it == map_.end()) return std::nullopt;
        order_.splice(order_.begin(), order_, it->second);   // O(1) move to front
        return it->second->second;
    }

    void put(const K& key, V value) {
        std::lock_guard lk(m_);
        if (auto it = map_.find(key); it != map_.end()) {
            it->second->second = std::move(value);
            order_.splice(order_.begin(), order_, it->second);
            return;
        }
        if (map_.size() >= cap_) {
            const K& victim = order_.back().first;
            map_.erase(victim);
            order_.pop_back();
        }
        order_.emplace_front(key, std::move(value));
        map_[key] = order_.begin();
    }

private:
    size_t cap_;
    std::list<std::pair<K, V>> order_;                             // front = newest
    std::unordered_map<K, typename std::list<std::pair<K,V>>::iterator> map_;
    std::mutex m_;
};
```

The insight most candidates miss: **`get` is a write**, because it reorders. So a
`shared_mutex` does not help — every reader takes the write lock. Say this, then
offer the real fixes: shard by key hash (N independent caches, N locks), or
approximate LRU with a CLOCK/second-chance bit, which makes reads genuinely
read-only. Offering sharding unprompted is the senior answer.

Follow-ups: LFU, TTL expiry (add a timer wheel from `03_thread_safe_primitives.md`), and
**cache stampede** — when a hot key expires and a thousand requests miss at once.
Fix with a per-key in-flight marker so one request refills and the rest wait.

---

## Problem 3 — Bounded blocking queue

**Ask first:** how many producers and consumers? What happens when full — block,
drop, or fail? Must shutdown drain or discard?

```cpp
template <class T>
class BoundedQueue {
public:
    explicit BoundedQueue(size_t cap) : cap_(cap) {}

    // Returns false only on shutdown.
    bool push(T item) {
        std::unique_lock lk(m_);
        not_full_.wait(lk, [this] { return closed_ || q_.size() < cap_; });
        if (closed_) return false;
        q_.push(std::move(item));
        lk.unlock();                 // unlock BEFORE notify: avoids the waiter
        not_empty_.notify_one();     // waking and immediately blocking on the mutex
        return true;
    }

    bool pop(T& out) {
        std::unique_lock lk(m_);
        not_empty_.wait(lk, [this] { return closed_ || !q_.empty(); });
        if (q_.empty()) return false;          // closed and drained
        out = std::move(q_.front());
        q_.pop();
        lk.unlock();
        not_full_.notify_one();
        return true;
    }

    void close() {
        { std::lock_guard lk(m_); closed_ = true; }
        not_empty_.notify_all();
        not_full_.notify_all();
    }

private:
    const size_t cap_;
    std::queue<T> q_;
    std::mutex m_;
    std::condition_variable not_empty_, not_full_;
    bool closed_{false};
};
```

Details that get noticed: two condition variables rather than one (so a producer
never wakes another producer), predicates that handle spurious wakeups, unlocking
before notify, and a `close()` that drains rather than discards — with the policy
stated explicitly.

---

## Problem 4 — Thread-safe logger

**Ask first:** what is the throughput? Can we lose messages on crash? Is
formatting cost on the hot path acceptable?

The key design insight, and it is what they are testing: **do the formatting off
the hot path.** A logger that formats a string, takes a mutex and writes to a
file inside a packet loop is a latency disaster.

```
  hot path:   copy raw arguments into a per-thread SPSC ring   (~20 ns)
  background: dequeue, format, batch, write                     (off the path)
```

Structure it as: per-thread SPSC ring (`03_thread_safe_primitives.md`) of small POD records holding
a format-string pointer and raw arguments; a background thread drains every ring,
formats, batches, and writes with `writev`. Mention that this is how modern
low-latency loggers work, and that the trade is losing the tail of the buffer on
a hard crash — which you mitigate with a signal handler that drains.

Follow-ups: log levels checked before the copy; a monotonic sequence number for
ordering across threads; bounded ring with a dropped-message counter rather than
blocking the producer.

---

## Problem 5 — Connection pool

**Ask first:** fixed or elastic? What happens on exhaustion — block, fail, or
grow? How do you detect a dead connection?

The parts candidates forget, and each is a follow-up waiting to happen:

- **Health checking.** A pooled connection can be dead. Validate on checkout, or
  keep a background prober. Validate-on-checkout costs latency; probing costs
  complexity. State the trade.
- **Idle timeout and max lifetime.** Servers and load balancers close idle
  connections; a pool that does not expire them hands out corpses.
- **Exhaustion policy.** Blocking with a timeout is usually right. Unbounded
  growth is usually wrong.
- **RAII handle** that returns the connection on destruction, so an early return
  or an exception cannot leak it.
- **Thundering herd on reconnect** after an outage — add jitter.

```cpp
// The handle is the interesting part: ownership returns automatically.
class PooledConnection {
public:
    PooledConnection(Pool* p, Conn* c) : pool_(p), conn_(c) {}
    ~PooledConnection() { if (pool_ && conn_) pool_->release(conn_); }
    PooledConnection(PooledConnection&& o) noexcept
        : pool_(std::exchange(o.pool_, nullptr)), conn_(std::exchange(o.conn_, nullptr)) {}
    Conn* operator->() const noexcept { return conn_; }
private:
    Pool* pool_; Conn* conn_;
};
```

---

## Problem 6 — Parking lot / elevator / etc.

The classic OOP design problems. They are testing modelling, not algorithms.
What scores:

- **Entities, value objects and services separated.** A `Ticket` is a value; a
  `PricingStrategy` is a service.
- **Strategy pattern for policy** — pricing, allocation — so a new rule does not
  touch the core.
- **State machine made explicit**, with an enum and a transition table, not a
  tangle of booleans.
- **Concurrency named**: two cars arriving at the last space is the interesting
  case, and you should raise it before they do.
- **No god class.** A `ParkingLot` with twenty methods is the failure mode.

Draw the state machine. Interviewers remember candidates who draw.

---

## A checklist for any LLD round

```
  [ ] Asked about threading, bounds, failure policy BEFORE coding
  [ ] Stated the invariants in one line each
  [ ] Stated complexity per operation
  [ ] Stated the concurrency guarantee (blocking / lock-free / wait-free)
  [ ] Handled empty, full, shutdown, exhaustion
  [ ] Used RAII for every resource
  [ ] Said how I would test it, including the concurrent case
  [ ] Named one thing I would change if the rate were 100x
```

That last line is the one that turns a Senior answer into a Staff answer.

## Exercises

1. Implement all six, from memory, on a 45-minute timer each. Then diff against
   these versions and write down what you missed.
2. For each, write the clarifying questions you would ask, before looking at the
   solution. Keep them in `15_interviews/04_flashcards/`.
3. Take the LRU cache and shard it. Benchmark 8 threads against the single-mutex
   version and produce the scaling curve.
4. Build the two-stage logger and measure hot-path cost per log call. Target under
   50 ns; report what you actually get.
5. Write the test you would use for the bounded queue's shutdown path — including
   the case where a producer is blocked in `push` when `close()` is called.
