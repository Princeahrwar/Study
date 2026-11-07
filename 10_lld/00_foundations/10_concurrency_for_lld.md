# 10 — Concurrency for LLD interviews

> Prerequisite: `03`. This is the concurrency you need *inside a
> machine-coding round* — "make this class thread-safe" is one of the most
> common live extensions across the whole problem set (it showed up as an
> extension in Parking Lot, Elevator, and Splitwise already). It is
> deliberately narrower than `10_lld/02_systems_lld/03_thread_safe_primitives.md` and
> `10_lld/02_systems_lld/04_lockfree_data_structures.md` in Phase C, which cover SPSC rings,
> seqlocks, hazard pointers, and epoch reclamation — genuinely advanced
> material you don't need mid-way through an LLD round. Read this file
> first; it's the 80% of concurrency questions that actually get asked here.

## Why this comes up constantly

Almost every LLD problem has shared mutable state (a `Slot`, an `Elevator`'s
request queue, an `Account` balance, a `BalanceSheet`), and almost every
interviewer will ask, at least once, "what if two threads call this at the
same time?" You don't need to design a lock-free structure to answer well —
you need to (1) correctly identify the race, (2) pick the right tool at the
right granularity, and (3) say why you didn't pick a bigger hammer than
necessary.

## The three tools that cover most answers

### `std::mutex` + `std::lock_guard` / `std::unique_lock`

The default answer. `lock_guard` for the common case (lock for the scope of
a block, unlock on exit via RAII — the same principle as
`10_lld/02_systems_lld/01_raii_and_modern_oop.md`, applied to a lock instead of memory).
`unique_lock` when you need to unlock early, transfer ownership, or use it
with a `condition_variable`.

```cpp
class Account {
public:
    void withdraw(double amt) {
        std::lock_guard<std::mutex> lock(mu_);
        if (amt > balance_) throw std::runtime_error("insufficient funds");
        balance_ -= amt;
    }
    void deposit(double amt) {
        std::lock_guard<std::mutex> lock(mu_);
        balance_ += amt;
    }
private:
    std::mutex mu_;
    double balance_ = 0;
};
```

**This class-where-every-public-method-locks-internally shape has a name**:
the **Monitor Object pattern**. Say the name — it converts "I put a mutex in
it" into a named, recognised design choice.

### `std::atomic<T>`

For a **single** piece of state (a counter, a boolean flag, a pointer) where
the operation you need has a lock-free hardware primitive (increment,
compare-and-swap). Cheaper than a mutex when it applies, but it only
protects that one variable — it does not give you a critical section across
multiple related fields.

```cpp
class Slot {
public:
    bool tryOccupy() {
        bool expected = false;
        return occupied_.compare_exchange_strong(expected, true);   // atomic test-and-set
    }
    void vacate() { occupied_.store(false); }
private:
    std::atomic<bool> occupied_{false};
};
```

This is the fix named in `10_lld/01_machine_coding_problems/02_parking_lot.md`'s concurrency extension —
resolving the "two cars, same free slot" race with a single atomic
compare-and-swap instead of a lock, because the invariant being protected
(`occupied_`) really is a single flag.

**When atomics are the wrong tool**: the moment you need to keep two or more
fields consistent with each other (like `Account`'s balance check-then-write
above), an atomic on one field doesn't help — you need a mutex around the
whole read-modify-write sequence, because the invariant spans multiple
operations that must appear indivisible together.

### `std::condition_variable` — for producer/consumer

For **waiting** for a condition to become true, rather than spinning. The
classic pairing: a bounded queue where producers block when full and
consumers block when empty (Tier 3 problem #16 in the roadmap).

```cpp
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

**Why `wait` takes a predicate lambda**: it guards against *spurious
wakeups* (the OS is allowed to wake a waiting thread with no corresponding
`notify`) and against the case where another thread grabs the resource
between `notify` and this thread actually running — the predicate re-checks
the real condition every time the thread wakes, rather than trusting the
wakeup itself. This is a specific, commonly-asked gotcha: "why not just
`wait()` with no predicate?" — say this answer.

## Reader-writer locks — for read-heavy shared state

When reads vastly outnumber writes (a `ConfigStore`, a routing table), a
plain mutex serializes readers against each other unnecessarily.
`std::shared_mutex` lets multiple readers hold a shared lock concurrently,
while a writer needs exclusive access.

```cpp
class ConfigStore {
public:
    std::string get(const std::string& key) const {
        std::shared_lock lock(mu_);          // shared: many readers concurrently
        return map_.at(key);
    }
    void set(std::string key, std::string value) {
        std::unique_lock lock(mu_);          // exclusive: blocks all readers and writers
        map_[std::move(key)] = std::move(value);
    }
private:
    mutable std::shared_mutex mu_;
    std::unordered_map<std::string, std::string> map_;
};
```

**When to reach for it over a plain mutex**: only when reads genuinely
dominate and contention is real — `shared_mutex` has higher per-operation
overhead than `mutex`, so it's a net loss under light contention or a
roughly even read/write mix. Naming this tradeoff unprompted is a strong
signal; reaching for `shared_mutex` reflexively is not.

## Lock scope and granularity — the design decision, not just the syntax

This is the part of the answer that actually separates Senior from Staff,
and it's the same question across every problem in the set:

- **Coarse-grained**: one lock over the whole object (`ParkingLot`,
  `Dispatcher`). Simple, always correct, but serializes unrelated work — two
  cars parking on different levels shouldn't have to wait on each other.
- **Fine-grained**: a lock per sub-resource (`Level`, `Elevator`, `Account`).
  More throughput, more complexity, and a new risk — **deadlock** if a
  thread needs to hold two fine-grained locks at once in inconsistent order.

**The classic deadlock example, worth having ready**: transferring money
between two accounts.

```cpp
// BROKEN: can deadlock if thread 1 does transfer(A, B) while thread 2
// does transfer(B, A) at the same time — each holds one lock and waits
// for the other.
void transfer(Account& from, Account& to, double amt) {
    std::lock_guard<std::mutex> l1(from.mu_);
    std::lock_guard<std::mutex> l2(to.mu_);
    from.balance_ -= amt;
    to.balance_ += amt;
}
```

**Fix 1 — consistent lock ordering**: always lock the account with the
lower ID (or address) first, regardless of which is "from" and which is
"to." Removes the cycle that causes deadlock.

**Fix 2 — `std::scoped_lock`** (C++17), which locks multiple mutexes
together using a deadlock-avoidance algorithm internally:

```cpp
void transfer(Account& from, Account& to, double amt) {
    std::scoped_lock lock(from.mu_, to.mu_);   // deadlock-free multi-lock
    from.balance_ -= amt;
    to.balance_ += amt;
}
```

Know both, but lead with `scoped_lock` as the modern, correct-by-default
answer — mentioning the manual-ordering fix afterward shows you understand
*why* it works, not just that a standard-library function exists.

## Livelock — deadlock's more embarrassing cousin

**Deadlock**: two threads are stuck, each waiting forever for a resource
the other holds — nothing moves, ever, but at least it's obviously stuck
(a hung process is easy to *notice*). **Livelock**: two threads are
**not** stuck — they're both actively doing work, repeatedly — but
neither makes real progress, because they keep reacting to each other in
a way that undoes what the other just did. The canonical example: two
people in a hallway, both step left to let the other pass, then both step
right, then both step left again — forever, neither blocked, neither
getting anywhere.

```cpp
// Contrived but real: two threads each trying to be "polite" by backing
// off when they detect contention, forever re-colliding.
std::atomic<bool> resourceAHeld{false}, resourceBHeld{false};

void threadOneWork() {
    while (true) {
        if (!resourceAHeld.exchange(true)) {          // grabbed A
            if (resourceBHeld.load()) {                // B is taken, "politely" back off
                resourceAHeld.store(false);
                continue;                               // retry from the top -- forever, if timing aligns
            }
            break;   // got both, proceed
        }
    }
}
```

**Why this matters as a distinct thing to name**: a livelocked system
shows CPU activity (threads spinning, doing work) but zero throughput —
which looks completely different in monitoring/debugging than a deadlock
(threads sitting idle, blocked). If you only know the word "deadlock,"
you'll misdiagnose a livelock as "it's not stuck, so it must be fine, just
slow" — worth being able to name the distinct failure mode precisely.
**The general fix**: the same tools that fix deadlock (consistent
ordering, avoiding retry-and-back-off loops that both sides do
identically) apply here too, plus adding **randomized backoff** (each
thread waits a different, random amount of time before retrying, per
`00_foundations/11`'s Dining Philosophers discussion of asymmetric fixes)
so both sides don't keep re-colliding in lockstep.

## Interview angles

**"Make this class thread-safe."** Identify the actual invariant being
protected (not just "add a lock somewhere") — is it one field (atomic
candidate) or several fields that must stay consistent together (mutex
around the whole operation)? State the granularity choice and why.

**"Two threads call `transfer(A, B)` and `transfer(B, A)` at the same time —
what happens?"** Walk through the deadlock directly, then give
`scoped_lock` or ordered locking as the fix. This is asked constantly and
almost verbatim.

**"Would you use a lock or an atomic here?"** Single independent field →
atomic. Multiple fields, or a check-then-act sequence that must be
indivisible → mutex. Say which and why for the specific case in front of
you, not as a general rule.

## Exercises

1. Take `ParkingLot` from `01_machine_coding_problems/02_parking_lot.md` and
   implement the per-`Level` mutex extension in full, then write a test
   spawning many threads all trying to park at once, asserting no slot is
   double-assigned.
2. Implement the `BoundedBlockingQueue` above from scratch, then write a
   producer/consumer test with multiple producers and consumers and confirm
   no item is lost or duplicated.
3. Reproduce the `transfer()` deadlock on purpose (two threads, opposite
   transfer order, no fix) and confirm it hangs. Then fix it with
   `scoped_lock` and confirm it no longer does.
4. Convert `ConfigStore` above to use a plain `std::mutex` instead of
   `shared_mutex`, benchmark both under a read-heavy workload (many reader
   threads, few writers), and note at what read:write ratio the
   `shared_mutex` version actually wins — don't assume, measure it, the
   same discipline as the benchmarks in the removed CO benchmarks.
5. Implement the `threadOneWork`-style livelock above with two threads
   contending for two atomic flags, and confirm — by adding an iteration
   counter — that both threads run forever without either completing.
   Then fix it with randomized backoff (`std::this_thread::sleep_for` a
   random short duration before retrying) and confirm one side eventually
   wins.
