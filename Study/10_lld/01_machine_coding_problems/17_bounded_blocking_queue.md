# 17 — Thread-Safe Bounded Blocking Queue

> Difficulty: intermediate, concurrency-focused. Frequently asked as its
> own standalone problem (not just as a building block) specifically to
> test whether you can write correct producer/consumer synchronization
> from scratch, live, without a library to reach for. The implementation
> is already built and explained in full in
> `00_foundations/10_concurrency_for_lld.md` — this file is the
> interview-problem framing around it: the requirements, the questions
> that get asked about it, and the extensions, kept separate from the
> foundations file so it has its own place in the problem roadmap.

## Step 1 — Clarify

Assumed answers:

- A fixed-capacity FIFO queue. `push` blocks if the queue is full;
  `pop` blocks if it's empty.
- Multiple producer and consumer threads, unbounded in number.
- **Deferred, explicitly**: a non-blocking `tryPush`/`tryPop` variant,
  timeouts on blocking calls, priority ordering (that's
  `10_task_scheduler_cron.md`'s problem, a different data structure).

## Step 2 — Entities

Deliberately one class — this is a data-structure-and-synchronization
problem, not an OOD-heavy one:

| Entity | Responsibility |
| --- | --- |
| `BoundedBlockingQueue<T>` | Owns the buffer, the mutex, and two condition variables (not-full, not-empty) |

## Step 3 — Why two condition variables, not one

The design question this problem is actually testing. A single condition
variable shared between "queue became non-full" and "queue became
non-empty" works correctness-wise (every waiter re-checks its own
predicate after waking, so a spurious wake on the wrong condition just
costs a wasted wake-and-recheck) but causes unnecessary wakeups under
load: every `notify` wakes *all* threads waiting on that one condition
variable regardless of which predicate they care about, so producers get
woken by consumer activity and vice versa, uselessly. **Two condition
variables — `notFull_` and `notEmpty_` — let `push` only wake waiting
consumers and `pop` only wake waiting producers**, which is both more
efficient and clearer to read. State this reasoning if asked "why two
condition variables and not one" — it's a real design tradeoff, not
boilerplate.

## Step 4-5 — Implementation

Full implementation, with the spurious-wakeup and predicate-wait
reasoning explained in depth, lives in
`00_foundations/10_concurrency_for_lld.md`'s `BoundedBlockingQueue`
section — reproduced here for reference:

```cpp
#include <condition_variable>
#include <mutex>
#include <queue>

template <typename T>
class BoundedBlockingQueue {
public:
    explicit BoundedBlockingQueue(size_t capacity) : capacity_(capacity) {}

    void push(T item) {
        std::unique_lock<std::mutex> lock(mu_);
        notFull_.wait(lock, [this] { return queue_.size() < capacity_; });
        queue_.push(std::move(item));
        lock.unlock();
        notEmpty_.notify_one();
    }
    T pop() {
        std::unique_lock<std::mutex> lock(mu_);
        notEmpty_.wait(lock, [this] { return !queue_.empty(); });
        T item = std::move(queue_.front());
        queue_.pop();
        lock.unlock();
        notFull_.notify_one();
        return item;
    }

private:
    std::mutex mu_;
    std::condition_variable notFull_, notEmpty_;
    std::queue<T> queue_;
    size_t capacity_;
};
```

**Why `wait` takes a predicate lambda rather than a bare `wait(lock)`**:
guards against spurious wakeups (the OS may wake a waiting thread with no
corresponding `notify`) and against another thread grabbing the resource
between a `notify` and this thread actually resuming — the predicate
re-checks the real condition every time the thread wakes, rather than
trusting the wakeup itself to mean the condition holds. This is the
single most commonly asked follow-up question on this exact problem —
have this sentence ready verbatim.

**Why `notify_one()` happens after `lock.unlock()`**: not strictly
required for correctness (most implementations tolerate notifying while
still holding the lock — the woken thread simply blocks briefly on
re-acquiring it), but unlocking first avoids an unnecessary
context-switch-and-immediately-block for the woken thread, a small but
real efficiency detail worth mentioning if asked to optimize.

## Step 6 — Edge cases handled

- Capacity of the queue is fixed at construction and never exceeded —
  `push` genuinely blocks (doesn't drop or overwrite) when full.
- Many producers and many consumers can be waiting simultaneously; each
  `push`/`pop` wakes exactly one waiter of the appropriate kind
  (`notify_one`, not `notify_all`) — correct because each successful
  operation frees exactly one slot's worth of capacity or produces
  exactly one item, matching one waiter's need, not all of them.

## Extensions (what a live follow-up usually asks)

**"Add `tryPush`/`tryPop` that return immediately (or after a timeout)
instead of blocking forever."** `wait_for` instead of `wait`, returning a
`bool`/`optional<T>` depending on whether the predicate became true
before the timeout — a direct, small change.

**"What if `notify_one` woke the wrong thread — could a `push` ever wake a
thread that's also waiting on `notFull_` for a different reason?"** No,
by construction: only `pop` ever calls `notFull_.notify_one()`, and only
threads calling `push` ever wait on `notFull_` — the split into two
condition variables from Step 3 is precisely what keeps the wake targets
correct.

**"How would you test this is actually correct under concurrency, not
just 'seems to work'?"** Stress test (many producer/consumer threads,
tight loop, assert no item lost or duplicated) plus ThreadSanitizer, per
`00_foundations/15_testing_gtest_and_gmock.md`'s testing-concurrent-code
section — "I ran it and it didn't crash" is a weak answer; naming the
sanitizer is the strong one.

**"Turn this into an unbounded queue — does anything simplify?"**
`push` no longer needs to wait or check capacity at all (only
`notEmpty_` matters), but say the tradeoff honestly: unbounded means no
backpressure — a slow consumer lets memory grow without limit, which is
exactly the failure mode bounded queues exist to prevent in a real
producer/consumer pipeline (this is the same backpressure conversation
`08_message_queue_pubsub.md`'s extensions raise).

## Interview framing to reuse

"I used two condition variables instead of one so a `push` only wakes
threads that actually care about the queue becoming non-empty, and vice
versa — with one shared condition variable, every operation would wake
every waiter regardless of which predicate they're checking, which works
but wastes wakeups under load." Precise, and it's the one sentence this
whole problem is built to elicit.

## Exercises

1. Implement `BoundedBlockingQueue` from scratch, from memory, in under 15
   minutes — this should be closer to muscle memory than most problems in
   the set by the time you reach it, since it underlies three other
   problems already built (`08_message_queue_pubsub.md`,
   `12_logger_framework.md`'s async extension,
   `10_task_scheduler_cron.md`).
2. Write a stress test with 4 producer threads and 4 consumer threads
   pushing/popping 10,000 items total through a small-capacity (e.g. 4)
   queue, and assert every pushed item is popped exactly once. Run it
   under ThreadSanitizer.
3. Implement the `tryPush`/`tryPop` timeout extension and write a test
   confirming a `tryPop` on an empty queue returns within its timeout
   rather than blocking forever.
4. Deliberately replace `notify_one()` with `notify_all()` in both methods
   and, using a thread count high enough to observe it, measure whether
   throughput changes under contention — this makes the "wasted wakeup"
   cost from Step 3 concrete rather than theoretical.
