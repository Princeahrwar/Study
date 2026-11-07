# LLD-S 03 — Thread-safe primitives you should be able to write cold

> Prerequisite: `02_cpp_memory_model.md`.
> These are the building blocks for Project 03 (`pfilt`) and for most LLD rounds.

## SPSC ring buffer

The single most useful concurrent structure in systems work, and the one most
likely to be asked. Full version with the optimisations that matter:

```cpp
#include <atomic>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>

template <class T, size_t Capacity>
class SpscQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
    static_assert(std::is_nothrow_move_assignable_v<T>);
    static constexpr size_t kMask = Capacity - 1;
    static constexpr size_t kLine = 64;

public:
    SpscQueue() = default;
    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;

    // Producer thread only.
    bool try_push(T item) noexcept {
        const size_t head = head_.load(std::memory_order_relaxed);

        // Cached tail: avoid touching the consumer's cache line on every push.
        // This is the optimisation that makes a real difference — without it
        // the producer reads a line the consumer writes, every single time.
        if (head - cached_tail_ >= Capacity) {
            cached_tail_ = tail_.load(std::memory_order_acquire);
            if (head - cached_tail_ >= Capacity) return false;   // genuinely full
        }

        buf_[head & kMask] = std::move(item);
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    // Consumer thread only.
    bool try_pop(T& out) noexcept {
        const size_t tail = tail_.load(std::memory_order_relaxed);

        if (tail >= cached_head_) {
            cached_head_ = head_.load(std::memory_order_acquire);
            if (tail >= cached_head_) return false;              // genuinely empty
        }

        out = std::move(buf_[tail & kMask]);
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    // Approximate — safe to call from either side for metrics only.
    size_t size_approx() const noexcept {
        return head_.load(std::memory_order_relaxed) -
               tail_.load(std::memory_order_relaxed);
    }

private:
    alignas(kLine) std::atomic<size_t> head_{0};
    size_t cached_tail_{0};                       // producer-private
    alignas(kLine) std::atomic<size_t> tail_{0};
    size_t cached_head_{0};                       // consumer-private
    alignas(kLine) T buf_[Capacity];
};
```

Points to defend, and an interviewer will ask about at least two:

1. **Monotonic indices, masked at access.** `head - tail` is unambiguous; a
   wrapped comparison needs a wasted slot or a generation bit.
2. **Cached opposite index.** Without it, every push reads the consumer's line.
   With it, the producer only re-reads when it thinks the queue is full. This
   typically doubles throughput and is the difference between a textbook ring and
   a production one.
3. **Each index on its own cache line**, with the private cache variable placed
   next to the index its own thread owns.
4. **Power-of-two capacity** so masking replaces a modulo.
5. Unsigned arithmetic makes `head - tail` correct across wraparound.

## MPSC queue (intrusive, lock-free)

Many producers, one consumer. Vyukov's intrusive design — short, and genuinely
lock-free on the producer side:

```cpp
template <class Node>
class MpscQueue {
public:
    // Any thread. Wait-free: a single exchange, no retry loop.
    void push(Node* n) noexcept {
        n->next.store(nullptr, std::memory_order_relaxed);
        Node* prev = head_.exchange(n, std::memory_order_acq_rel);
        prev->next.store(n, std::memory_order_release);
    }

    // Consumer thread only. May transiently return nullptr while a producer is
    // between its exchange and its next store — that is inherent to the design.
    Node* pop() noexcept {
        Node* tail = tail_;
        Node* next = tail->next.load(std::memory_order_acquire);

        if (tail == &stub_) {
            if (!next) return nullptr;
            tail_ = next;
            tail  = next;
            next  = next->next.load(std::memory_order_acquire);
        }
        if (next) { tail_ = next; return tail; }

        if (tail != head_.load(std::memory_order_acquire)) return nullptr;  // producer mid-push

        push(&stub_);
        next = tail->next.load(std::memory_order_acquire);
        if (next) { tail_ = next; return tail; }
        return nullptr;
    }

private:
    Node  stub_{};
    std::atomic<Node*> head_{&stub_};
    Node* tail_{&stub_};
};
```

Be honest about its property in an interview: producers are **wait-free**, but a
consumer can transiently observe an incomplete list. If you need the consumer
never to see a gap, you need a different design. Stating a structure's exact
progress guarantee — wait-free, lock-free, obstruction-free, blocking — rather
than waving at "lock-free" is a strong signal.

## Thread pool

```cpp
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class ThreadPool {
public:
    explicit ThreadPool(size_t n = std::thread::hardware_concurrency()) {
        workers_.reserve(n);
        for (size_t i = 0; i < n; ++i)
            workers_.emplace_back([this] { worker_loop(); });
    }

    ~ThreadPool() {
        {
            std::lock_guard lk(m_);
            stopping_ = true;
        }
        cv_.notify_all();
        for (auto& t : workers_) if (t.joinable()) t.join();
    }

    template <class F>
    bool submit(F&& f) {
        {
            std::lock_guard lk(m_);
            if (stopping_) return false;       // reject after shutdown starts
            tasks_.emplace(std::forward<F>(f));
        }
        cv_.notify_one();
        return true;
    }

private:
    void worker_loop() {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock lk(m_);
                cv_.wait(lk, [this] { return stopping_ || !tasks_.empty(); });
                if (stopping_ && tasks_.empty()) return;   // drain before exiting
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();                                        // never hold the lock here
        }
    }

    std::mutex m_;
    std::condition_variable cv_;
    std::queue<std::function<void()>> tasks_;
    std::vector<std::thread> workers_;
    bool stopping_{false};
};
```

What a reviewer checks, in order:

- The `cv_.wait` predicate handles **spurious wakeups**. A bare `wait()` is a bug.
- `stopping_` is checked **inside** the predicate, and the drain policy is
  explicit — this pool drains before exiting; the alternative is to discard, and
  you should say which you chose and why.
- The task runs with the lock **released**.
- The destructor joins. A pool whose destructor does not join is a use-after-free
  waiting to happen.

The obvious next question is "how would you make this scale?" — per-worker deques
with work stealing, which removes the single shared mutex. Offering that
unprompted is the senior move.

## Timer wheel

O(1) insert and expiry, which a `priority_queue` cannot give you. This is what
TCP retransmit timers and connection-tracking timeouts use.

```cpp
#include <array>
#include <cstdint>
#include <vector>

// Single-level wheel. For very long timeouts, use a hierarchy of these
// (hierarchical timing wheels), which is what the kernel does.
template <class Timer, size_t Slots = 512>
class TimerWheel {
    static_assert((Slots & (Slots - 1)) == 0);
public:
    explicit TimerWheel(uint64_t tick_ns) : tick_ns_(tick_ns) {}

    // O(1). delay_ns must be < Slots * tick_ns for a single-level wheel.
    void add(Timer* t, uint64_t delay_ns) {
        const uint64_t ticks = delay_ns / tick_ns_;
        const size_t slot = (cursor_ + ticks) & (Slots - 1);
        t->rounds = ticks / Slots;            // long timeouts wait extra laps
        slots_[slot].push_back(t);
    }

    // O(expired) per tick, not O(total timers).
    template <class Fn>
    void tick(Fn&& on_expire) {
        auto& bucket = slots_[cursor_];
        for (size_t i = 0; i < bucket.size(); ) {
            if (bucket[i]->rounds > 0) {
                bucket[i]->rounds--;
                ++i;
            } else {
                Timer* t = bucket[i];
                bucket[i] = bucket.back();
                bucket.pop_back();            // swap-and-pop: no shifting
                on_expire(t);
            }
        }
        cursor_ = (cursor_ + 1) & (Slots - 1);
    }

private:
    uint64_t tick_ns_;
    size_t cursor_{0};
    std::array<std::vector<Timer*>, Slots> slots_;
};
```

The comparison to state: a binary heap gives O(log n) insert and O(log n) removal
of the minimum; a timer wheel gives O(1) insert and O(1) amortised expiry, at the
cost of bounded resolution and range. For a million connection timeouts, the
wheel wins decisively. This is a classic LLD question.

## Object pool

```cpp
// Fixed-capacity, no allocation after construction. Essential in a data plane,
// where malloc in the hot path is both slow and unbounded in latency.
template <class T, size_t N>
class ObjectPool {
public:
    ObjectPool() {
        for (size_t i = 0; i < N; ++i) free_.push_back(&storage_[i]);
    }

    T* acquire() noexcept {
        if (free_.empty()) return nullptr;    // caller MUST handle exhaustion
        T* p = free_.back();
        free_.pop_back();
        return p;
    }

    void release(T* p) noexcept { free_.push_back(p); }

private:
    std::array<T, N> storage_;
    std::vector<T*> free_;
};
```

The design point worth making: `acquire` returning `nullptr` is not a flaw, it is
the *contract*. A pool that grows on exhaustion has unbounded latency and has
given up the property you built it for. In a data plane, exhaustion means drop
and count, not allocate.

## Interview angles

**"Implement a thread-safe queue."**
Ask the clarifying questions first: how many producers and consumers, bounded or
unbounded, blocking or not. Then pick. The clarification is half the score.

**"Why is your ring buffer's capacity a power of two?"**
Masking instead of modulo, and unambiguous full/empty with monotonic indices.

**"What is the difference between lock-free and wait-free?"**
Lock-free: *some* thread makes progress. Wait-free: *every* thread makes progress
in bounded steps. Then classify your own structure honestly.

**"How do you expire a million timers?"**
Timer wheel, O(1). Contrast with a heap.

**"Your thread pool's destructor — what could go wrong?"**
Not joining, not waking waiters, discarding queued work silently, tasks touching
destroyed members.

## Exercises

1. Implement `SpscQueue` with and without the cached-index optimisation.
   Benchmark both at 100M items between two pinned threads. Then use `perf c2c`
   to show the coherence traffic difference.
2. Write a randomised stress harness for it: random push/pop timing, random
   payloads, checksum verification. Run under TSAN for an hour.
3. Implement the timer wheel and benchmark it against a `priority_queue` at
   10k, 100k and 1M timers. Plot insert and tick cost for both.
4. Extend the thread pool to per-worker deques with work stealing. Measure the
   improvement at 16 threads with short tasks, where the shared mutex hurts most.
5. Take each structure and write down its exact progress guarantee and its
   failure mode under exhaustion. This document is more useful than the code.
