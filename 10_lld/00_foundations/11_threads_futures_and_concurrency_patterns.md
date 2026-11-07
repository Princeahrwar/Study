# 11 — Threads, futures, and classic concurrency patterns

> Prerequisite: `10_concurrency_for_lld.md`. That file covered the *shared-
> state protection* toolbox (mutex, atomic, condition_variable). This file
> covers **task and thread management** on top of it — how you actually
> spin up work, get a result back asynchronously, and the two textbook
> problems (Producer-Consumer, Dining Philosophers) every concurrency
> discussion eventually circles back to. Thread Pool is deliberately not
> re-derived here — a full, tested implementation already exists in
> `10_lld/02_systems_lld/03_thread_safe_primitives.md`; this file tells you when to
> reach for one and cross-references it rather than duplicating it.

## `std::thread` and `std::jthread`

```cpp
#include <thread>

void worker(int id) { /* ... */ }

std::thread t(worker, 42);
t.join();    // must join() or detach() before the thread object is destroyed,
             // or std::terminate() is called — a very standard gotcha

// C++20: std::jthread joins automatically in its destructor (RAII for
// threads, the same principle as every other resource in this program)
std::jthread jt(worker, 42);   // no explicit join() needed
```

**Why `jthread` is the better default going forward**: `std::thread`'s
destructor calling `std::terminate()` if you forgot to `join()`/`detach()`
is a real, common bug class — exactly the kind of manual-resource-
management footgun RAII exists to eliminate (`03_oop_fundamentals.md`,
`10_lld/02_systems_lld/01_raii_and_modern_oop.md`). `jthread` also supports cooperative
cancellation via `std::stop_token`, letting a long-running worker check
`token.stop_requested()` and exit cleanly — worth mentioning if a follow-up
asks "how would you cancel a running task?"

## Futures and promises — getting a result back asynchronously

```cpp
#include <future>

// std::async: fire off work, get a std::future<T> back to collect the result
std::future<int> fut = std::async(std::launch::async, [] {
    // expensive computation
    return 42;
});
// ... do other work ...
int result = fut.get();   // blocks until ready; throws if the task threw

// std::promise/std::future pair: for when the producer isn't a callable
// you're launching directly — e.g. a worker thread in a pool that needs to
// hand a result back to whoever submitted the task
std::promise<int> prom;
std::future<int> fut2 = prom.get_future();
std::thread([&prom] { prom.set_value(42); }).detach();
int result2 = fut2.get();
```

**The distinction worth stating precisely**: `std::async` is the
higher-level, "just run this and give me a future" tool — use it for one-
off asynchronous work. `promise`/`future` is the lower-level primitive
`async` is built on, and it's what you reach for when you need to hand a
result back across a thread boundary from code that isn't structured as a
single callable — most commonly, **inside a thread pool**, where a worker
thread picks a task off a queue and needs to fulfil a promise the *caller*
is holding the future half of. This is exactly the shape of
`10_lld/02_systems_lld/03_thread_safe_primitives.md`'s Thread Pool: submitting a task
returns a `future<T>` so the caller can retrieve the result (or exception)
once a worker completes it, decoupling "when work finishes" from "when the
caller wants the answer."

**`std::launch::async` vs `std::launch::deferred`**: `async` policy runs on
a new thread immediately; `deferred` runs lazily, on the calling thread,
only when `.get()`/`.wait()` is called. Passing no policy lets the
implementation choose — often surprising in practice (it may never
parallelize at all). **Always pass `std::launch::async` explicitly** if you
actually want concurrency — a well-known gotcha worth stating unprompted.

## When to reach for a Thread Pool instead of raw threads

Spawning a `std::thread` per task has real overhead (OS thread creation,
stack allocation, scheduler bookkeeping) and no bound on concurrency — a
burst of 10,000 tasks means 10,000 OS threads, which will thrash the
scheduler and likely exhaust memory before it exhausts CPU. A **Thread
Pool** — a fixed set of worker threads pulling tasks off a shared queue —
bounds concurrency to a sane number (typically near the core count) and
amortizes thread-creation cost across many tasks.

`10_lld/02_systems_lld/03_thread_safe_primitives.md`'s Thread Pool is the reference
implementation: a bounded task queue, a fixed set of workers, `submit()`
returning a `std::future<T>` per task via `std::packaged_task`. Read it
there rather than re-deriving it — the interview-relevant takeaway for
*this* file is simply: **any machine-coding problem that says "process many
short-lived units of work concurrently"** (Task Scheduler, Message Queue
consumers, a batch of independent report generations) **is a Thread Pool
candidate**, and naming it by name, with the bounded-queue-plus-fixed-
workers shape, is the answer worth giving before writing any code.

## Producer-Consumer — the pattern underneath the Thread Pool and Message Queue problems

Already implemented in full in `10_concurrency_for_lld.md`'s
`BoundedBlockingQueue` (producers block on `push()` when full, consumers
block on `pop()` when empty, both via `condition_variable`). Worth
restating here because it's the load-bearing pattern for two upcoming
machine-coding problems (`10_lld/01_machine_coding_problems/08_message_queue_pubsub.md`,
`10_lld/01_machine_coding_problems/10_task_scheduler_cron.md`) — if `10`'s implementation isn't fully
internalized yet, that's the prerequisite to close before attempting either.

## Semaphores — counting permits, not just locked/unlocked

A `std::mutex` has exactly two states: locked or unlocked, held by at
most one thread. A **semaphore** generalizes this to a **count**: it
holds a number of "permits," and up to that many threads can hold one
simultaneously — the mutex you already know is just a semaphore with a
maximum count of 1 (this specific case even has a name, a **binary
semaphore**).

```cpp
#include <semaphore>

std::counting_semaphore<10> pool(10);   // 10 permits available, e.g. 10 DB connections

void useConnection() {
    pool.acquire();                      // blocks if all 10 are currently taken
    // ... use one of the 10 connections ...
    pool.release();                      // give the permit back
}
```

`std::counting_semaphore<N>` (C++20, `<semaphore>`) is templated on its
maximum count. `acquire()` blocks until a permit is available (identical
shape to `mutex::lock()`, generalized to "available slots" rather than
"available lock"); `release()` gives one back. `std::binary_semaphore` is
a type alias for `std::counting_semaphore<1>` — the mutex-equivalent case,
though semantically different in one important way covered next.

**The one genuinely different thing a semaphore can do that a mutex
can't**: a semaphore can be **signaled by a different thread than the one
waiting** — there's no concept of "ownership" the way a mutex requires the
same thread to lock and unlock it. This makes semaphores the right
primitive for **signaling** ("thread A, wake up, thread B just finished
its part") rather than just mutual exclusion — exactly the shape the
classic puzzles below are built around.

**Where you'd reach for this over a plain mutex**: bounding concurrent
access to a *limited pool of N interchangeable resources* (database
connections, a fixed number of worker slots) where the count itself is
the thing you're protecting, not a single shared data structure — a
`std::counting_semaphore` expresses "at most N at a time" more directly
than a mutex-guarded counter would.

## Reentrant (recursive) locks — when a thread needs to re-lock its own mutex

```cpp
std::mutex mu;
void inner() { std::lock_guard<std::mutex> lock(mu); /* ... */ }
void outer() {
    std::lock_guard<std::mutex> lock(mu);
    inner();   // DEADLOCK: this thread already holds mu, and mutex isn't reentrant --
               // it blocks waiting for itself to release a lock it's still holding
}
```

A plain `std::mutex` is **not reentrant** — if the thread that already
holds it tries to lock it again (even indirectly, through a function
call), it deadlocks against itself. `std::recursive_mutex` allows this:
the *same thread* can lock it multiple times (it tracks a count, and
must call `unlock()` the same number of times it called `lock()`).

**The honest, standard advice**: needing a recursive mutex is usually a
sign the code's lock scoping could be cleaner (a big function holding a
lock while calling into another function that also needs it, rather than
separating "the locked part" from "the part that doesn't need the lock").
Reach for `std::recursive_mutex` only when restructuring genuinely isn't
practical — know it exists and what problem it solves, but treat needing
it as a mild design smell worth a second look, not a default tool.

## Dining Philosophers — the canonical deadlock-avoidance exercise

**The setup**: N philosophers sit around a table with N forks (one between
each pair). Each needs **both** adjacent forks to eat. If every philosopher
picks up their left fork simultaneously, all forks are held and everyone
waits forever for their right fork — deadlock, and it's the textbook
illustration of the same circular-wait condition behind the `transfer()`
deadlock in `10_concurrency_for_lld.md`.

```cpp
class Table {
public:
    explicit Table(int n) : forks_(n) {}

    void philosopherEats(int id) {
        int left = id;
        int right = (id + 1) % forks_.size();

        // Fix: consistent global ordering — always lock the lower-numbered
        // fork first. Breaks the circular-wait condition that causes
        // deadlock, exactly the ordering fix from 10_concurrency_for_lld.md.
        int first = std::min(left, right);
        int second = std::max(left, right);

        std::scoped_lock lock(forks_[first], forks_[second]);   // C++17, deadlock-free
        // eat
    }

private:
    std::vector<std::mutex> forks_;
};
```

**Four classic fixes, worth knowing all four by name even though the
ordering fix above is the one to actually implement**:

1. **Resource ordering** (shown above): always acquire in a fixed global
   order. Simple, general, the same technique as the bank-transfer fix.
2. **Arbitrator/waiter**: a central lock that only allows N-1 philosophers
   to attempt picking up forks at once — guarantees at least one succeeds
   in getting both forks, breaking the all-hold-one-fork deadlock state.
3. **Chandy/Misra algorithm**: a distributed, token-passing solution with no
   central arbitrator — the advanced, "if pushed" answer, worth naming even
   if you wouldn't implement it live.
4. **Asymmetric solution**: one philosopher (arbitrarily) picks up their
   *right* fork first while all others pick up left first — breaks the
   symmetry that causes the circular wait, without needing an arbitrator.

**Interview framing**: Dining Philosophers is rarely asked as a full
machine-coding problem on its own — it's asked as a **concept check**
("what's the classic solution to deadlock in a cyclic-resource-acquisition
scenario, and why does it work?"). Having the resource-ordering fix ready,
implemented, and explicable in one sentence ("break the cycle by imposing a
total order on lock acquisition") is what's actually being tested.

## Classic concurrency interview puzzles

A distinct, smaller category from Dining Philosophers and the machine-
coding problems: short, tightly-scoped **thread-coordination puzzles**,
often asked as quick warm-ups or filters before a bigger concurrency
question. All four below are really the same skill in different clothes —
**making one thread wait for a signal from another, using the right
primitive** — and having the shape memorized cold is worth more than
re-deriving it live.

### Print in Order / FooBar alternately

**Problem**: two (or three) threads must print in a fixed relative order,
repeatedly — e.g. thread A always prints "Foo" before thread B prints
"Bar," alternating, forever.

```cpp
class FooBar {
public:
    explicit FooBar(int n) : n_(n) {}

    void foo() {
        for (int i = 0; i < n_; ++i) {
            fooSem_.acquire();     // wait for permission to print "Foo"
            std::cout << "Foo";
            barSem_.release();     // signal: bar() may now run
        }
    }
    void bar() {
        for (int i = 0; i < n_; ++i) {
            barSem_.acquire();
            std::cout << "Bar";
            fooSem_.release();     // signal: foo() may run again
        }
    }

private:
    int n_;
    std::binary_semaphore fooSem_{1};   // starts WITH a permit: foo() goes first
    std::binary_semaphore barSem_{0};   // starts empty: bar() must wait for foo()'s signal
};
```

**The one detail worth internalizing**: the *initial* semaphore counts
encode "who goes first" — `fooSem_` starts at 1 (immediately acquirable),
`barSem_` starts at 0 (must be signaled before anyone can proceed). This
initial-count trick is the core idea behind every puzzle in this section.

### Print Zero, Even, Odd

**Problem**: three threads — one prints only `0`, one prints only even
numbers, one prints only odd numbers — must interleave to produce
`0 1 0 2 0 3 0 4 ...` in order.

The shape is identical to FooBar, extended to three semaphores forming a
cycle: `zero → odd/even → zero → odd/even → ...`, where the "zero" thread
signals whichever of odd/even should run next based on the current
number's parity. Worth sketching the semaphore cycle on paper before
coding — three participants instead of two is where people start making
mistakes under time pressure.

### The H2O molecule problem

**Problem**: hydrogen and oxygen threads arrive at a barrier and must
combine in groups of exactly 2 hydrogen + 1 oxygen (forming "H2O"), with
threads from different molecules never interleaving within one group.

```cpp
class H2O {
public:
    void hydrogen() {
        hydrogenSem_.acquire();     // counting semaphore, max 2 permits: only 2 H at a time
        std::cout << "H";
        barrier_.arrive_and_wait();  // wait for the full trio (C++20 std::barrier)
        hydrogenSem_.release();
    }
    void oxygen() {
        oxygenSem_.acquire();       // counting semaphore, max 1 permit: only 1 O at a time
        std::cout << "O";
        barrier_.arrive_and_wait();
        oxygenSem_.release();
    }

private:
    std::counting_semaphore<2> hydrogenSem_{2};
    std::counting_semaphore<1> oxygenSem_{1};
    std::barrier<> barrier_{3};   // releases all three once 3 threads have arrived
};
```

**The new primitive here**: `std::barrier` (C++20, `<barrier>`) — unlike
a semaphore (permits) or a condition variable (a single signal), a
barrier makes every participant wait until *all N* have arrived, then
releases them together. This is the right tool whenever "wait for exactly
N cooperating threads to reach this point" is the actual requirement —
name it specifically rather than trying to build the same effect out of
condition variables and a manual counter.

### Interview framing for this whole category

These puzzles are graded almost entirely on **whether you reach for the
right primitive without hesitation** — semaphores for counted
permits/signaling, a barrier for "wait for exactly N," a plain condition
variable for a single wait/notify. Getting the *shape* right fast matters
more here than in the bigger design problems, precisely because these are
meant to be quick — treat the exercises below as drills to run until
they're close to automatic, not one-time solves.

## Interview angles

**"How would you run N independent tasks concurrently and collect all their
results?"** `std::async` per task (each returning a `future<T>`) for a
small, known N; a Thread Pool with `submit()` returning futures for a large
or unbounded N — name the crossover point (roughly: more tasks than you'd
be comfortable with as raw OS threads) as the deciding factor.

**"What's Dining Philosophers, and how do you fix it?"** State the deadlock
condition (circular wait, each holds one resource, waits for the next) and
the resource-ordering fix in under thirty seconds — this is a fluency
check, not a design challenge.

**"Why does `std::thread`'s destructor call `terminate()` if you forgot to
join?"** Because a still-running, unjoined thread accessing stack-local
state whose scope has already ended (or is about to end) is a use-after-
scope bug waiting to happen — the standard library refuses to guess whether
you meant to `join()` or `detach()` and terminates rather than silently
picking one. `jthread` sidesteps this by joining automatically.

## Exercises

1. Implement `Table`/Dining Philosophers with the resource-ordering fix,
   spin up N=5 philosopher threads each eating in a loop, and let it run
   long enough (thousands of iterations) under no deadlock — then
   deliberately remove the ordering (lock `left` then `right` unconditionally)
   and confirm you can reproduce a hang.
2. Implement the asymmetric fix (one philosopher reversed) as an
   alternative, and write one sentence on which fix you'd actually propose
   live and why (hint: which one generalises better to N philosophers added
   at runtime).
3. Take `10_lld/02_systems_lld/03_thread_safe_primitives.md`'s Thread Pool
   implementation, submit 100 tasks that each return a different computed
   value, collect all 100 futures, and confirm every result is correct and
   arrives in the pool's completion order (not necessarily submission
   order) — state why those two orders can differ.
4. Rewrite one `std::async(std::launch::async, ...)` call without the
   explicit policy (pass none) and, if your standard library implementation
   allows observing it, check whether it actually ran concurrently or was
   deferred — this is the "always pass the policy explicitly" lesson made
   concrete on your own toolchain.
5. Implement `FooBar` exactly as shown, spawn two threads calling `foo()`
   and `bar()` with `n = 5`, and confirm the output is exactly
   `FooBarFooBarFooBarFooBarFooBar` every run (run it 20 times in a loop
   to build confidence it's not a lucky interleaving).
6. Implement Print Zero-Even-Odd with three semaphores forming the cycle
   described above, for numbers 1 through 10, and confirm the output is
   always `0102030405060708090 10` in that exact interleaved order.
7. Implement `H2O` with `std::barrier`, spawn a mix of hydrogen and
   oxygen threads (e.g. 4 oxygen, 8 hydrogen), and confirm every printed
   group of 3 characters contains exactly two `H`s and one `O` — never a
   partial or malformed group.
8. Reproduce the recursive-mutex deadlock from this file's `outer()`/
   `inner()` example with a plain `std::mutex`, confirm it hangs, then fix
   it by switching to `std::recursive_mutex` — and separately, write one
   sentence on how you'd restructure the code to avoid needing it at all
   (hint: which function should actually hold the lock).
