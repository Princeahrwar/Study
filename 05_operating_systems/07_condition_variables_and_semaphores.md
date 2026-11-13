# OS 07: Condition variables, semaphores and the classic problems

*Part of the [Operating systems plan](00_plan.md) · Time: about 8 hours (5 reading and experiments, 3 problems) · You need: [OS 06](06_locks.md) (locks, the futex, lock-holder preemption), [OS 03](03_threads.md) (threads) and [OS 05](05_process_scheduling.md) (real-time priorities, for section 7)*

A lock answers "who may touch this data now". It cannot answer "wait until the queue is not empty", "wait until the child has finished" or "wait until a slot is free". Those are questions about a condition, and the tools for them are condition variables and semaphores. Interviewers ask about them in a fixed set of forms: why is `pthread_cond_wait` always in a `while` loop? What is the difference between a mutex and a binary semaphore? Write a bounded buffer. Why does a single condition variable hang a producer-consumer? What is a thundering herd? Starve a writer with a readers-writers lock, and then fix it. Solve dining philosophers without deadlock. Explain priority inversion and the fix. What are the two most common concurrency bugs that are not deadlocks? This chapter answers each one with the library's documented rules and a program that runs on a real Linux kernel, including the failing versions.

By the end you can:

- write the waiting pattern (mutex, state variable, `while` loop) from memory, and say what breaks if any of the three parts is missing (measured: a lost signal, woken threads with nothing to take, a hang);
- explain Mesa and Hoare semantics and spurious wake-ups;
- build a bounded buffer with a mutex and two condition variables, and with two semaphores and a mutex, and explain why one condition variable is not enough;
- build a counting semaphore from a mutex, a condition variable and an integer, and say what a semaphore can do that a mutex cannot;
- explain the thundering herd with numbers, and when `broadcast` is right;
- implement readers-writers with reader preference and writer preference, and show (measured) that reader preference starved a writer for the whole run, as glibc's default lock does;
- solve dining philosophers three ways, and compare them by meals eaten and fairness;
- build a reusable barrier and explain why the obvious version hangs;
- reproduce priority inversion with real-time threads (measured: 678 ms against 78 ms with priority inheritance);
- recognise atomicity violations and order violations, the bug patterns behind 97 percent of the non-deadlock bugs in a well-known study.

**Where the numbers come from.** Every program was compiled with GCC 13.3 (`-O2 -Wall -Wextra`, linked with `-lpthread`) and run on a Linux 6.18.44 kernel inside a Firecracker micro-VM with 2 vCPUs (Intel Xeon at 2.30 GHz), glibc 2.39, as root (section 7 needs real-time priorities). The rules are from the OSTEP chapters on condition variables, semaphores and concurrency bugs and from the POSIX page for `pthread_cond_wait` (all fetched; linked in the Sources). The futex layer under all of these is OS 06. Facts from general knowledge are marked "from memory".

**Data and timing.** Counts that follow from the program's logic (items consumed, lost signals, hangs) are deterministic and shown once. Timings ran twice and are shown as run 1 / run 2. The machine has two CPUs, so benchmarks with more than two runnable threads measure scheduler behaviour as much as the primitive; where that matters the text says so. Differences under about 30 percent between two timings are within this machine's run-to-run noise.

---

## 1. Waiting for a condition

Suppose a thread must wait until another thread has changed some shared state: a child has finished, a buffer has data, a counter has reached a threshold. There are two bad answers and one good one.

- **Spin on the state** under the lock (take the lock, check, release, repeat). Correct, and wasteful: OS 06 section 6 measured what spinning does when the thread you are waiting for is not on a CPU.
- **Sleep for a fixed time and check again.** Correct, and either slow (long sleeps) or wasteful (short ones).
- **Sleep until told.** The waiter goes to sleep in the kernel, and the thread that changes the state wakes it. This is a **condition variable**.

A condition variable is a queue of sleeping threads with two operations. It does not hold the condition itself; the condition is ordinary state in your data (a counter, a flag, a queue length) protected by a mutex. The condition variable only lets you sleep until someone says "the state may have changed".

### 1.1 The three parts of the pattern

```c
static void event_post(void)
{
    pthread_mutex_lock(&mu);
    event = 1;                                        /* change the state, then signal */
    pthread_cond_signal(&fill);
    pthread_mutex_unlock(&mu);
}

static void event_wait(void)
{
    pthread_mutex_lock(&mu);
    while (!event) pthread_cond_wait(&fill, &mu);     /* recheck after every wake-up */
    pthread_mutex_unlock(&mu);
}
```

This pair is the whole pattern in miniature, and each of its three parts is there for a reason (OSTEP and POSIX, fetched):

1. **A state variable (`event`).** The signal is not remembered. If the poster signals before the waiter is asleep, the signal reaches nobody. The state variable records that the event happened, so a late waiter does not sleep at all.
2. **A mutex held around the check and the wait.** `pthread_cond_wait(cv, mutex)` atomically releases the mutex and puts the thread to sleep, and re-locks the mutex before it returns. Without the mutex, the waiter could check the state, be interrupted before it sleeps, and miss the post: the same check-then-sleep gap that `FUTEX_WAIT` closes in OS 06 by comparing the word inside the kernel. The POSIX page says the caller must hold the mutex; otherwise the result is an error for error-checking and robust mutexes and undefined behaviour for the rest.
3. **A `while` loop, not an `if`.** After the wait returns, the state must be checked again (section 1.3).

The program runs the pattern with the event arriving after the wait and before it:

```text
  event came after  the wait began: wait returned after  56.5 ms
  event came before the wait began: wait returned after   0.0 ms
```

Both work: the first waits the 50 ms for the event; the second finds the state already set and returns at once.

### 1.2 What goes wrong without the state variable

`d1_cv lost` signals a condition variable when nobody is waiting, then starts a waiter that waits for the signal blindly (with a 300 ms timeout so the program ends). Then it repeats the test with a waiter that checks a flag first:

```text
  waiter waits blindly  : returned after 300.1 ms, timed out (signal was lost)
  waiter checks a flag  : returned after   0.0 ms, woken
```

The signal sent before the wait is gone. The blind waiter sleeps until the timeout. The flag version never sleeps, because the flag is already set. (OSTEP shows the same failure for a parent waiting on a child: without `done`, a child that signals first leaves the parent asleep forever.)

### 1.3 Mesa semantics, and why the loop is a `while`

When a thread is woken by `signal`, there is a gap between the signal and the moment the woken thread holds the mutex again. Another thread can run in that gap and take whatever the signal announced. This is **Mesa semantics**, used by essentially every system (OSTEP, fetched): a signal is a hint that the state may have changed, not a guarantee. Under **Hoare semantics** the woken thread runs immediately and the state is exactly as the signaller left it; it is a stronger guarantee that is harder to implement, and mostly of textbook interest (from memory: the original monitor proposals used it).

POSIX adds a second reason: **spurious wake-ups**. The page says a wait may return even though nobody signalled, and tells the caller to re-check the predicate in a loop (fetched). So the rule is: wait in a `while`, test the state, and never assume a return means the condition holds.

The program makes a producer fill a one-item slot 300,000 times for two consumers. One version waits with `if`, the other with `while`. Each time a consumer is woken and finds the slot empty it counts the event (a real program would take from an empty buffer here):

```c
static void *consumer(void *arg)
{
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&mu);
        if (use_while) { while (count == 0 && !done) pthread_cond_wait(&fill, &mu); }
        else if (count == 0 && !done) pthread_cond_wait(&fill, &mu);
        if (count == 0) {
            if (done) { pthread_mutex_unlock(&mu); return NULL; }
            underflow++;                              /* woken, but there is nothing to take */
        } else {
            count = 0;
            taken++;
        }
        pthread_cond_signal(&empty);
        pthread_mutex_unlock(&mu);
    }
}
```

```text
if    produced 300000  consumed 300000  woken with nothing to take 3563
while produced 300000  consumed 300000  woken with nothing to take 0
```

Second run:

```text
if    produced 300000  consumed 300000  woken with nothing to take 22
while produced 300000  consumed 300000  woken with nothing to take 0
```

With `if`, consumers were woken with nothing to take 3,563 / 22 times in the two runs (earlier runs of the same program printed 235, 507 and 1,242, so the count varies by two orders of magnitude from run to run): the other consumer barged in after the signal and took the item. With `while`, never. Nothing was lost here only because the program checks `count` before taking; in OSTEP's version the buffer's `get()` asserts and the program crashes.

### 1.4 The rule in one line

Lock, then `while (!condition) wait`, then act, then unlock. The thread that changes the state does so under the same lock and then signals. Both sides use the same state variable.

---

## 2. The bounded buffer

A producer puts items into a buffer of `SLOTS` entries and a consumer takes them out. The producer must wait while the buffer is full, the consumer while it is empty. With a mutex and two condition variables:

```c
static void put_cv(long v)                            /* mutex and two condition variables */
{
    pthread_mutex_lock(&bm);
    while (nbuf == SLOTS) pthread_cond_wait(&not_full, &bm);
    buf[fill_ptr] = v; fill_ptr = (fill_ptr + 1) % SLOTS; nbuf++;
    pthread_cond_signal(&not_empty);
    pthread_mutex_unlock(&bm);
}

static long get_cv(void)
{
    pthread_mutex_lock(&bm);
    while (nbuf == 0) pthread_cond_wait(&not_empty, &bm);
    long v = buf[use_ptr]; use_ptr = (use_ptr + 1) % SLOTS; nbuf--;
    pthread_cond_signal(&not_full);
    pthread_mutex_unlock(&bm);
    return v;
}
```

The producer waits on `not_full` and wakes `not_empty`; the consumer does the reverse. Using **two** condition variables is not decoration. OSTEP (fetched) shows what happens with one: with several consumers, a consumer finishing its `get` signals the shared condition variable and may wake another consumer rather than the producer; that consumer finds the buffer empty and sleeps again, and now the producer, the thing that could make progress, is asleep with everyone else. `pb` reproduces it with a one-slot buffer, one producer and two consumers, giving up after 3 seconds:

```text
onecv_signal     HUNG after 3 of 20000 items: no progress for 3 s, 3 of 4 threads asleep, count = 0
onecv_signal     HUNG after 4 of 20000 items: no progress for 3 s, 3 of 4 threads asleep, count = 0
onecv_signal     HUNG after 3 of 20000 items: no progress for 3 s, 3 of 4 threads asleep, count = 0
onecv_signal     HUNG after 4 of 20000 items: no progress for 3 s, 3 of 4 threads asleep, count = 0
onecv_signal     HUNG after 2 of 20000 items: no progress for 3 s, 3 of 4 threads asleep, count = 0
onecv_broadcast  finished 20000 of 20000 items in 0.55 s
twocv            finished 20000 of 20000 items in 0.44 s
```

Five runs out of five hung after only a handful of items, with three of the four threads asleep (the fourth is `main`, which reports). Replacing `signal` with `broadcast` on the single condition variable finished (every waiter wakes, rechecks and the right one proceeds), and two condition variables finished faster. The broadcast fix is a **covering condition**: correct because it wakes everybody, at a cost measured in section 4. OSTEP's advice is that if your program only works when `signal` is turned into `broadcast`, there is probably a bug to find instead.

---

## 3. Semaphores

A **semaphore** is an integer with two operations. `sem_wait` waits until the value is positive and then decrements it; `sem_post` increments it and wakes a waiter if there is one. Dijkstra's original description lets the value go negative, its magnitude counting the waiters; the Linux implementation does not, and OSTEP's hand-built version behaves like that (fetched).

The initial value decides what the semaphore means. Ask how many of the resource can be handed out immediately after initialisation (OSTEP, fetched):

| Use | Initial value | Pattern |
|---|---|---|
| lock (binary semaphore) | 1 | `sem_wait` before the critical section, `sem_post` after |
| ordering ("B after A") | 0 | B calls `sem_wait`; A calls `sem_post` when done |
| counting a resource (empty slots) | the number of slots | producer waits, consumer posts |
| throttling (at most N at once) | N | wrap the heavy region in wait and post |

The ordering case works whichever side runs first, because the count remembers a post that happened early. That is the semaphore's version of the state variable in section 1.

### 3.1 A semaphore from a mutex and a condition variable

The two are interchangeable in power: a semaphore is a condition variable whose state variable has been packaged with it. OSTEP's `Zemaphore` (fetched) is just this:

```c
static void zem_wait(zem_t *z)
{
    pthread_mutex_lock(&z->m);
    while (z->value <= 0) pthread_cond_wait(&z->c, &z->m);
    z->value--;
    pthread_mutex_unlock(&z->m);
}

static void zem_post(zem_t *z)
{
    pthread_mutex_lock(&z->m);
    z->value++;
    pthread_cond_signal(&z->c);
    pthread_mutex_unlock(&z->m);
}
```

The state is the integer `value`, the condition is `value > 0`, and the `while` is the same rule as before. The converse (building condition variables from semaphores) is considerably harder and error-prone (OSTEP). POSIX semaphores (`sem_t`) are provided by the C library; on Linux their slow path is a futex (from memory).

### 3.2 A mutex and a binary semaphore are not the same

A mutex has an **owner**: the thread that locked it must unlock it. A semaphore has none: any thread may post. The program shows both:

```text
mutex locked by main, unlocked by another thread: returns 1 (Operation not permitted)
semaphore posted by another thread: value is now 1, and main can sem_wait it
```

An error-checking mutex refuses an unlock from another thread (`EPERM`, "Operation not permitted"); the semaphore accepts a post from another thread, which is exactly what ordering needs ("the child posts, the parent waits"). The ownership also matters for the kernel's help: priority inheritance (section 7) needs to know whom to boost, and a semaphore has no owner to boost, so it can suffer priority inversion without a cure (from memory: that is why real-time code uses mutexes for mutual exclusion).

### 3.3 The bounded buffer with semaphores

Two semaphores count the slots: `empty` starts at `SLOTS`, `full` at 0. The mutex protects only the buffer indices:

```c
static void put_sem(long v)                           /* two semaphores for the slots, a mutex for the indices */
{
    sem_wait(&sempty);                                /* wait for an empty slot */
    pthread_mutex_lock(&bm);
    buf[fill_ptr] = v; fill_ptr = (fill_ptr + 1) % SLOTS;
    pthread_mutex_unlock(&bm);
    sem_post(&sfull);                                 /* announce a full slot */
}

static long get_sem(void)
{
    sem_wait(&sfull);
    pthread_mutex_lock(&bm);
    long v = buf[use_ptr]; use_ptr = (use_ptr + 1) % SLOTS;
    pthread_mutex_unlock(&bm);
    sem_post(&sempty);
    return v;
}
```

The order matters. If the mutex is taken before the `sem_wait`, a consumer holding the mutex while waiting for data blocks the producer that would supply the data, and the program deadlocks (OSTEP, fetched). So: wait on the counting semaphore first, take the mutex only around the buffer update.

Throughput of the three versions (cv, the hand-built semaphore, the POSIX semaphore), one producer and one consumer pinned to the two CPUs, then two pairs unpinned (4 threads on 2 CPUs), `ns per item`, run 1 / run 2. Every run checks that the sum of the items consumed equals the sum produced:

| Version | 1 pair, pinned | 2 pairs, free |
|---|---|---|
| mutex and condition variables | 1,349 / 1,554 | 1,773 / 1,735 |
| hand-built semaphores | 1,424 / 1,552 | 1,651 / 1,941 |
| POSIX semaphores | 1,541 / 1,550 | 1,047 / 1,202 |

All three are in the same range, about 1.0 to 1.9 microseconds per item: with a 16-slot buffer and threads that do nothing but hand items over, almost every item involves a sleep and a wake-up (OS 06's futex costs), so the cost is the kernel round trip. With one pair pinned to the two CPUs the three tie. With four threads on two CPUs the POSIX semaphore was 30 to 40 percent faster in both runs, a gap larger than this machine's noise; I did not investigate why. Choose on clarity first: a condition variable when the condition is richer than a count, a semaphore when it is a count.

---

## 4. Signal or broadcast: the thundering herd

`signal` wakes one waiter, `broadcast` wakes all of them. Waking all is always correct (every waiter rechecks its predicate) and is sometimes the only correct choice, as in OSTEP's memory allocator: threads wait for different amounts of memory, and a `free` that signals one thread may wake one that still cannot proceed while one that could stays asleep. But when only one waiter can make use of the event, broadcast wakes everybody for nothing. That is the **thundering herd**: all of them wake, contend for the mutex, and all but one go back to sleep.

The program holds 4, 32 or 128 waiters on one condition variable and hands them 3,000 tokens one at a time, each token waking either one waiter (`signal`) or all of them (`broadcast`). It prints the time per token and the voluntary context switches per token (blocking in the kernel; counted for the whole process with `getrusage`). Run 1 / run 2:

| Waiters | `signal`: µs per token | `broadcast`: µs per token | `signal`: switches | `broadcast`: switches |
|---|---|---|---|---|
| 4 | 22.8 / 25.6 | 27.7 / 37.2 | 2.0 / 2.0 | 5.4 / 5.2 |
| 32 | 21.0 / 23.2 | 96.5 / 104.7 | 2.0 / 2.0 | 29.9 / 31.4 |
| 128 | 26.0 / 25.8 | 188.6 / 193.5 | 2.0 / 2.0 | 73.5 / 69.1 |

`signal` costs about 2 switches per token however many waiters there are (one for the waiter that wakes and takes the token, one for the producer waiting for it), flat at 21 to 26 µs. `broadcast` grows with the herd: about 5, 30 and 70 switches per token for 4, 32 and 128 waiters, and 1.2 to 1.5, 4.5 and 7 times the time. Each woken thread that finds the token gone goes back to sleep, which is one more switch and one more trip through the mutex. An earlier pair of runs of the same program gave smaller herd costs (24 to 29 switches and 3 to 4 times the time at 32 and 128 waiters), so read the table as "grows steeply with the number of waiters", not as exact factors. Kernel code has the same problem and the same cure: wake one exclusive waiter instead of all (from memory; the old `accept()` herd is the standard example).

---

## 5. Monitors

A **monitor** packages the pattern: a data structure, the mutex that guards it and the condition variables that go with it, so that callers cannot reach the data without the lock. The idea is from the 1970s (Hoare, Brinch Hansen; from memory). Java's `synchronized` methods with `wait` and `notify` are a Mesa-style monitor with one condition variable per object (from memory), and C++'s `std::condition_variable` is the pthreads pattern with a nicer type (the CPP track). In C you build the monitor by convention: keep the mutex, the condition variables and the data in one struct, expose only functions that take the lock, and never touch the fields from outside. Every bounded buffer in this chapter is a monitor in that sense.

---

## 6. The classic problems

Each of the classics isolates one thing that goes wrong with synchronisation. All four programs below check their own invariants.

### 6.1 Readers and writers

Many threads may read at once; a writer needs exclusive access. The design question is **who waits for whom**. Two designs, both built from the pieces above:

```c
static void read_lock(void)
{
    switch (mode) {
    case 0:
        pthread_mutex_lock(&rmu);
        if (++readers_active == 1) sem_wait(&writelock);      /* the first reader locks writers out */
        pthread_mutex_unlock(&rmu);
        break;
    case 1:
        pthread_mutex_lock(&rmu);
        while (writing || writers_waiting > 0) pthread_cond_wait(&wcv, &rmu);   /* yield to writers */
        active_readers++;
        pthread_mutex_unlock(&rmu);
        break;
    default: pthread_rwlock_rdlock(&rw);
    }
}

static void read_unlock(void)
{
    switch (mode) {
    case 0:
        pthread_mutex_lock(&rmu);
        if (--readers_active == 0) sem_post(&writelock);      /* the last reader lets writers in */
        pthread_mutex_unlock(&rmu);
        break;
    case 1:
        pthread_mutex_lock(&rmu);
        if (--active_readers == 0) pthread_cond_broadcast(&wcv);
        pthread_mutex_unlock(&rmu);
        break;
    default: pthread_rwlock_unlock(&rw);
    }
}

static void write_lock(void)
{
    switch (mode) {
    case 0: sem_wait(&writelock); break;
    case 1:
        pthread_mutex_lock(&rmu);
        writers_waiting++;
        while (writing || active_readers > 0) pthread_cond_wait(&wcv, &rmu);
        writers_waiting--; writing = 1;
        pthread_mutex_unlock(&rmu);
        break;
    default: pthread_rwlock_wrlock(&rw);
    }
}

static void write_unlock(void)
{
    switch (mode) {
    case 0: sem_post(&writelock); break;
    case 1:
        pthread_mutex_lock(&rmu);
        writing = 0;
        pthread_cond_broadcast(&wcv);
        pthread_mutex_unlock(&rmu);
        break;
    default: pthread_rwlock_unlock(&rw);
    }
}
```

Mode 0 is **reader preference** (the OSTEP design): a count of active readers under a mutex, and one semaphore `writelock` that the first reader takes and the last reader releases. Mode 1 is **writer preference**: readers wait while any writer is waiting or writing. Modes 2 and 3 are glibc's `pthread_rwlock_t`, with its default settings and with the kind set to `PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP`.

The experiment: three readers each take the lock, hold it for 1 ms and take it again immediately, staggered so that they overlap and at least one reader is always inside; one writer asks for the lock every 20 ms. The run lasts 2 seconds. Results (run 1 / run 2):

| Lock | Reads | Writes | Writer wait, mean (ms) | Writer wait, max (ms) |
|---|---|---|---|---|
| reader preference (OSTEP design) | 5,524 / 5,541 | 1 / 1 | 1996.0 / 1995.3 | 1996.0 / 1995.3 |
| writer preference (mutex and cv) | 5,344 / 5,332 | 94 / 94 | 0.6 / 0.7 | 1.1 / 1.1 |
| glibc rwlock, default | 5,554 / 5,535 | 1 / 1 | 1996.0 / 1995.9 | 1996.0 / 1995.9 |
| glibc rwlock, prefer writer | 5,319 / 5,316 | 94 / 94 | 0.7 / 0.7 | 1.1 / 2.3 |

With reader preference **the writer got in once in the whole 2 seconds**, and only after the readers stopped: it waited about 1,996 ms, nearly the entire run. That is **writer starvation**. The glibc default behaves the same way, so on this system `pthread_rwlock_t` prefers readers unless you set the kind. With writer preference the writer waited under 2 ms every time (94 writes in each run), and the readers lost 3 to 4 percent of their reads (about 5,330 against 5,540): the price of making each reader wait for a pending writer. The reverse starvation, readers locked out by a stream of writers, is possible with writer preference; a **fair** lock (queue everybody in arrival order, with consecutive readers admitted together) avoids both at the price of more bookkeeping (from memory).

### 6.2 Dining philosophers

Five philosophers sit at a round table with five forks, one between each pair. A philosopher needs both adjacent forks to eat. Each fork is a mutex. The naive rule is "take the left fork, then the right". If every philosopher takes the left fork before any takes the right one, all five hold one fork and wait for the next: the four deadlock conditions (OS 09) all hold, including **circular wait**. The program inserts a 10 ms pause between the two pick-ups to make that outcome likely, and uses a timed lock (0.5 s) as the deadlock detector:

```c
static int pick_up(int id)
{
    int left = id, right = (id + 1) % N;
    switch (mode) {
    case 0:                                           /* naive: left, a pause, then right */
        if (timed_lock(&fork_mu[left])) return -1;
        usleep(10000);
        if (timed_lock(&fork_mu[right])) { pthread_mutex_unlock(&fork_mu[left]); return -1; }
        return 0;
    case 1: {                                         /* ordered: always the lower-numbered fork first */
        int a = left < right ? left : right, b = left < right ? right : left;
        pthread_mutex_lock(&fork_mu[a]);
        usleep(10000);
        pthread_mutex_lock(&fork_mu[b]);
        return 0;
    }
    case 2:                                           /* waiter: at most four may try at once */
        sem_wait(&waiter);
        pthread_mutex_lock(&fork_mu[left]);
        usleep(10000);
        pthread_mutex_lock(&fork_mu[right]);
        return 0;
    default:                                          /* trylock: put the left fork back if the right is taken */
        for (;;) {
            if (atomic_load(&stop)) return 1;             /* the run is over, not a failure */
            pthread_mutex_lock(&fork_mu[left]);
            usleep(10000);
            if (pthread_mutex_trylock(&fork_mu[right]) == 0) return 0;
            pthread_mutex_unlock(&fork_mu[left]);
            usleep(1000 + 997 * id);                  /* back off a different time for each philosopher */
        }
    }
}
```

The four rules are `naive`, `ordered` (always take the lower-numbered fork first, which breaks the cycle: this is Dijkstra's resource ordering, and OSTEP's variant has one philosopher reverse his order), `waiter` (a semaphore initialised to 4 so at most four philosophers compete; by the pigeonhole principle one of them can always get both forks) and `trylock` (take the left, try the right, and put the left back with a different delay per philosopher if the right is taken). Each run is 3 seconds; thinking takes 1 to 4 ms and eating 2 ms. The program checks that no two neighbours eat at the same time. Run 1 / run 2:

| Rule | Meals, total | Fewest | Most | Neighbours ate together | Timed out (deadlocked) |
|---|---|---|---|---|---|
| naive | 374 / 375 | 0 / 0 | 188 / 188 | 0 / 0 | 3 / 3 |
| ordered | 832 / 833 | 103 / 104 | 209 / 209 | 0 / 0 | 0 / 0 |
| waiter | 981 / 982 | 196 / 196 | 197 / 197 | 0 / 0 | 0 / 0 |
| trylock | 297 / 288 | 12 / 12 | 172 / 164 | 0 / 0 | 0 / 0 |

What to read from it:

- **No rule broke mutual exclusion** (the fourth column is 0 everywhere); the problem is not safety but **progress** and **fairness**.
- **`naive` deadlocked**: three philosophers gave up after waiting 0.5 s for the second fork (the detector breaking the cycle; the forks they dropped let the others finish a few meals). One philosopher ate no meal at all.
- **`waiter` was the best**: the most meals and the most even (196 or 197 each). It costs a shared semaphore that every philosopher touches.
- **`ordered`** never deadlocks but is uneven (about 103 to 209 meals): the philosopher who must take his right fork first spends more time waiting.
- **`trylock` made about a third of the progress** and left one philosopher with 12 meals against 164 to 172 for the best-fed. Dropping a fork and retrying avoids deadlock but invites **livelock**: philosophers keep acting without getting anywhere. OSTEP (fetched) lists this trade-off for the "no preemption" condition and notes it can livelock.

### 6.3 The sleeping barber

A barber cuts hair in a shop with three waiting chairs. When no customer is waiting the barber sleeps; an arriving customer wakes him, or sits down if the barber is busy, or leaves if the chairs are full. It is a bounded buffer with a twist: customers that find the buffer full are turned away instead of waiting.

```c
static void *barber(void *arg)
{
    (void)arg;
    for (;;) {
        int v;
        sem_getvalue(&customers, &v);
        if (v == 0) atomic_fetch_add(&barber_slept, 1);       /* nobody waiting: the barber sleeps */
        sem_wait(&customers);                                  /* sleep until a customer arrives */
        sem_wait(&mu);
        waiting--;
        sem_post(&barber_ready);                               /* call the customer to the chair */
        sem_post(&mu);
        if (atomic_load(&served) + atomic_load(&turned_away) >= CUSTOMERS) return NULL;
        usleep(3000);                                          /* cut hair */
    }
}

static void *customer(void *arg)
{
    (void)arg;
    sem_wait(&mu);
    if (waiting < CHAIRS) {
        waiting++;
        sem_post(&customers);                                  /* wake the barber */
        sem_post(&mu);
        sem_wait(&barber_ready);                               /* wait for the chair */
        atomic_fetch_add(&served, 1);
    } else {
        sem_post(&mu);
        atomic_fetch_add(&turned_away, 1);                     /* shop full: leave */
    }
    return NULL;
}
```

`customers` counts the people waiting (the barber sleeps on it), `barber_ready` is how the barber hands a customer the chair, and `mu` protects the count of waiting customers. 60 customers arrived 1 to 5 ms apart; a cut takes 3 ms:

```text
customers 60  served 55  turned away 5  total 60  barber found nobody waiting 29 times (counted with sem_getvalue, so approximate)
```

Served plus turned away equals the number of customers. The "sleep" count comes from reading the semaphore value just before the barber waits, so it is approximate. A common mistake in this problem is deciding whether to sit down outside the lock that protects the waiting count, which lets more customers than chairs sit down.

### 6.4 Barriers

A barrier makes N threads wait until all of them have arrived, so that no thread starts phase `r + 1` before every thread has finished phase `r`. The obvious implementation counts arrivals and lets the last one reset the count and wake the others. The obvious waiters' condition is `arrived != 0`. It looks right, and it hangs:

```c
static int barrier_wait(void)
{
    if (mode == 2) { pthread_barrier_wait(&pb); return 0; }
    struct timespec ts;
    int rc = 0;
    pthread_mutex_lock(&bm);
    if (mode == 0) {                                  /* naive: count up, the last resets and wakes */
        arrived++;
        if (arrived == nthr) {
            arrived = 0;
            pthread_cond_broadcast(&bc);
        } else {
            abs_after(&ts, 1.0);
            while (arrived != 0 && rc == 0) rc = pthread_cond_timedwait(&bc, &bm, &ts);
        }
    } else {                                          /* generation: wait for the generation to change */
        int my = generation;
        if (++arrived == nthr) {
            arrived = 0;
            generation++;
            pthread_cond_broadcast(&bc);
        } else {
            abs_after(&ts, 1.0);
            while (my == generation && rc == 0) rc = pthread_cond_timedwait(&bc, &bm, &ts);
        }
    }
    pthread_mutex_unlock(&bm);
    return rc;
}
```

Mode 0 is the obvious one; mode 1 is the fix. In mode 0 a woken waiter must wait for the mutex and then re-evaluate `arrived != 0`. A thread that has already left the barrier can come round again and arrive for the next phase before a slow waiter has run; `arrived` is now 1, and the slow waiter, still in the loop, sees a nonzero count and goes back to sleep for a barrier that has already opened. In mode 1 each waiter remembers the **generation** number it arrived in and waits only for that number to change, which no later arrival can undo. Three threads, naive version:

```text
naive      threads 3  HUNG: a thread waited 1 s for a barrier that never opened (1.0 s)
```

(Each waiter used a 1-second timed wait so the program could report a hang instead of hanging.) The generation version and glibc's `pthread_barrier_t`, with a check after every barrier that every thread did arrive (`wrong` counts violations), time per barrier, run 1 / run 2:

| Barrier | 2 threads, pinned (µs) | 4 threads on 2 CPUs (µs) |
|---|---|---|
| generation (mutex and cv) | 12.21 / 13.35 | 16.37 / 16.08 |
| `pthread_barrier_t` | 11.37 / 11.94 | 13.11 / 15.06 |

Both are correct (`wrong 0` in every run) and take 11 to 17 microseconds per barrier, which is sleeping and waking threads; the library barrier is 5 to 20 percent faster. The hand-built one is the pattern to know, the library one is what to use.

---

## 7. Priority inversion and priority inheritance

A high-priority thread H waits for a lock held by a low-priority thread L. A medium-priority thread M, which does not need the lock, becomes runnable and preempts L. Now H waits for L, and L cannot run because M is running: H is delayed by a thread of lower priority than itself, for as long as M wants the CPU. This is **priority inversion**. (A Mars probe famously reset itself because of it in 1997; from memory.)

The program reproduces it with three `SCHED_FIFO` threads, all pinned to CPU 0 (OS 05: a runnable real-time thread of higher priority always preempts a lower one). L has priority 10, takes the lock and needs 100 ms of CPU while holding it. H has priority 30, wakes at about 20 ms and wants the lock. M has priority 20, wakes at about 30 ms, needs 600 ms of CPU and never touches the lock. The only difference between the two runs is the mutex protocol:

```c
int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    if (strcmp(argv[1], "inherit") == 0) pthread_mutexattr_setprotocol(&a, PTHREAD_PRIO_INHERIT);
    pthread_mutex_init(&lock, &a);
    struct sched_param sp = { .sched_priority = 90 };    /* main runs above everything and mostly sleeps */
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
    pin(1);
    pthread_t l, h, m;
    t_start = now();
    pthread_create(&l, NULL, low, NULL);
    usleep(2000);                                         /* give L time to take the lock */
    pthread_create(&h, NULL, high, NULL);
    pthread_create(&m, NULL, medium, NULL);
    pthread_join(h, NULL); pthread_join(m, NULL); pthread_join(l, NULL);
    printf("%-8s H waited %6.1f ms for a lock whose holder needs 100 ms of CPU in total\n", argv[1], h_wait * 1e3);
    return 0;
}
```

Results (how long H waited for the lock), run 1 / run 2:

| Mutex | H's wait (ms) |
|---|---|
| plain (`PTHREAD_PRIO_NONE`) | 678.5 / 679.8 |
| priority-inheriting (`PTHREAD_PRIO_INHERIT`) | 77.9 / 80.0 |

H waited about 678 ms with the plain mutex, longer than M's whole 600 ms burst, and about 78 ms with priority inheritance, which is exactly the remainder of L's critical section. The arithmetic is in Problem E. With **priority inheritance** the kernel raises the holder to the priority of its highest waiter for as long as it holds the lock; L then runs at priority 30, M cannot preempt it, and L finishes its 100 ms and releases. glibc implements this with the PI futexes of OS 06 (word holding the owner's thread id, `FUTEX_LOCK_PI`; from memory for the glibc part). The alternative is **priority ceiling** (`PTHREAD_PRIO_PROTECT`: the lock has a fixed priority and whoever holds it runs at that priority; from memory), or avoiding locks shared between real-time and ordinary threads at all.

Semaphores have no owner and so cannot be boosted: use a mutex when the protected resource might be contended by threads of different priority (section 3.2).

---

## 8. Concurrency bugs that are not deadlocks

Lu and colleagues studied 105 concurrency bugs in MySQL, Apache, Mozilla and OpenOffice (as reported in OSTEP, fetched): 74 were not deadlocks and 31 were. Of the non-deadlock bugs **97 percent were atomicity violations or order violations**. These two patterns are therefore what you will most often find, and most often be asked about.

### 8.1 Atomicity violation

A region of code was meant to execute as a unit but nothing enforces it. The classic form is check-then-act: one thread checks a condition and then acts on it, and another thread changes the condition in between. OSTEP's example is a thread that tests `thd->proc_info != NULL` and then uses it while another thread sets it to NULL. The program does a bank version: 2,000,000 accounts each hold 100, and two threads each try to withdraw 60 from every account when the balance allows it. At most one withdrawal should succeed per account:

```c
static void *withdraw(void *arg)
{
    pin((int)(long)arg);
    for (int i = 0; i < ACCOUNTS; i++) {
        if (use_lock) pthread_mutex_lock(&mu);
        if (balance[i] >= 60) {                           /* check ... */
            for (volatile int k = 0; k < 20; k++) { }     /* (a window, as real code has) */
            balance[i] = balance[i] - 60;                 /* ... then act */
        }
        if (use_lock) pthread_mutex_unlock(&mu);
    }
    return NULL;
}
```

```text
atomicity        accounts 2000000  each started with 100 and two threads each tried to withdraw 60: overdrawn 1152990
atomicity_fixed  accounts 2000000  each started with 100 and two threads each tried to withdraw 60: overdrawn 0
order            the user thread read config = 0 (the initialiser sets 42)
order_fixed      the user thread read config = 42 (the initialiser sets 42)
```

Without the lock, about 550,000 accounts ended up **overdrawn** (negative): both threads passed the check before either subtracted. With one mutex around the check and the update, none. The fix for an atomicity violation is a lock around the whole check-and-act region, not around each statement.

### 8.2 Order violation

Two operations must happen in a particular order and nothing enforces it: a thread uses a value before another thread has initialised it. The second two lines of the output above are the program's order violation: the user thread read `config` before the initialiser, which takes 50 ms, had set it, so it saw 0. The fix is to make the order explicit:

```c
static void *initialiser(void *arg)
{
    (void)arg;
    usleep(50000);                                        /* slow start-up work */
    config = 42;
    sem_post(&ready);
    return NULL;
}

static void *user(void *arg)
{
    (void)arg;
    if (wait_for_ready) sem_wait(&ready);
    seen = config;
    return NULL;
}
```

A semaphore initialised to 0 (or a condition variable with a state variable, as OSTEP suggests) makes the user wait; with it the user thread read 42. This is the same pattern as section 1: the semaphore is the state variable that remembers the event.

### 8.3 Deadlock

The remaining 31 of the 105 bugs. Deadlock needs four conditions at once (OSTEP, fetched): mutual exclusion, hold-and-wait, no preemption, circular wait. Removing one prevents it: take locks in a global order (circular wait), take all locks at once (hold-and-wait), use `trylock` and back off (no preemption, with the livelock risk seen in section 6.2), or avoid locks with atomic operations (mutual exclusion). OS 09 is the full treatment.

---

## What it costs

| Mechanism | Measured effect here | Section |
|---|---|---|
| Signal sent before anyone waits, waiter has no state variable | slept the full 300 ms timeout; with a flag, returned in 0.0 ms | 1.2 |
| `if` instead of `while` around the wait, 300,000 items, 2 consumers | woken with nothing to take 22 to 3,563 times depending on the run; `while`: 0 | 1.3 |
| One condition variable with `signal`, 1 producer and 2 consumers | hung in 5 runs out of 5, after at most 10 items | 2 |
| Bounded buffer, cv or semaphores | about 1.0 to 1.9 µs per item; POSIX semaphores 30 to 40 percent faster only with 4 threads on 2 CPUs | 3.3 |
| `broadcast` against `signal`, 128 waiters | 4.5 to 7 times the time per token, 30 to 70 against 2 context switches | 4 |
| Reader-preference lock with overlapping readers | the writer waited about 1,996 ms of a 2,000 ms run | 6.1 |
| Writer preference, same load | writer waited under 2 ms; readers lost 3 to 4 percent of their reads | 6.1 |
| Dining philosophers, `waiter` against `ordered` against `trylock` | about 980, 830 and 290 meals in 3 s | 6.2 |
| Barrier crossing, 2 to 4 threads | 11 to 17 µs | 6.4 |
| Priority inversion, plain against inheriting mutex | 678 ms against 78 ms of waiting | 7 |
| Unlocked check-then-act on 2,000,000 accounts | about 550,000 overdrawn | 8.1 |

---

## Common mistakes

1. **Waiting with `if` instead of `while`.** A woken thread may find the state already taken (Mesa semantics) or may have been woken for no reason (spurious wake-up). Section 1.3 counted from dozens to thousands of such wake-ups.
2. **Calling `pthread_cond_wait` without holding the mutex.** An error for error-checking mutexes, undefined behaviour for the others.
3. **Signalling without changing any state.** A signal is not remembered; the state variable is what a late waiter finds.
4. **Changing the state without the mutex.** The waiter can check, then miss your change and your signal, and sleep forever.
5. **One condition variable for two different conditions, with `signal`.** The wake-up goes to the wrong kind of thread and everyone ends up asleep (Problem B).
6. **Turning `signal` into `broadcast` to make a hang disappear.** It may hide a bug; at best it is a covering condition with a thundering-herd bill.
7. **Taking the mutex before `sem_wait` in a bounded buffer.** A consumer sleeping on the semaphore holds the mutex the producer needs.
8. **Treating a binary semaphore as a mutex.** It has no owner: any thread can release it, and it cannot be priority-inherited.
9. **Passing a relative time to `pthread_cond_timedwait`.** It takes an absolute deadline, by default on `CLOCK_REALTIME`; this chapter's programs compute one from the current time.
10. **Assuming waiters wake in FIFO order.** POSIX says waiters are selected by the scheduling policy and that several threads may wake on a multiprocessor (fetched).
11. **Using a reader-preference lock for data with a steady stream of readers and a writer you care about.** The writer starved for the whole run (section 6.1).
12. **Believing `trylock` and retry solve deadlock.** They avoid it and invite livelock; one philosopher ate almost nothing (section 6.2).
13. **A barrier that counts arrivals but not generations.** A fast thread can start the next phase before a slow one has left the last (section 6.4).
14. **Checking a condition outside the lock that protects it, then acting.** The atomicity violation of section 8.1.
15. **Sharing a lock between real-time and ordinary threads without priority inheritance.** The inversion of section 7.

---

## Questions out loud

1. **What is a condition variable?** A queue of sleeping threads with wait, signal and broadcast. It holds no condition itself; the condition is your own state, protected by the mutex that the wait releases atomically.
2. **Why must `pthread_cond_wait` be called with the mutex held?** The check of the state and the going to sleep must be atomic with respect to the thread that changes the state, or a wake-up can fall in the gap. The wait releases the mutex and sleeps as one step and re-locks before returning.
3. **Why a `while` loop around the wait?** Mesa semantics (another thread may take the state first) and spurious wake-ups (POSIX allows them). Measured: with `if`, consumers woke with nothing to take hundreds of times.
4. **Mesa against Hoare semantics?** Mesa: a signal is a hint, the woken thread rechecks. Hoare: the woken thread runs at once and the state is as the signaller left it. Systems use Mesa.
5. **What does the state variable do?** Remembers the event. Signals are not stored; a waiter that arrives late checks the variable and does not sleep.
6. **Write a bounded buffer.** Mutex, `count`, two condition variables (`not_full`, `not_empty`), each side waits in a `while` on its own condition and signals the other's.
7. **Why two condition variables?** With one, a `signal` can wake a thread of the same kind, which sleeps again, and the thread that could make progress stays asleep. Measured: hung 5 of 5 times.
8. **What is a semaphore?** An integer with wait (block until positive, then decrement) and post (increment, wake a waiter).
9. **Mutex against binary semaphore?** A mutex has an owner and the owner must unlock it; a semaphore can be posted by anyone, which suits signalling and ordering and rules out priority inheritance.
10. **How would you build a semaphore?** A mutex, a condition variable and an integer: wait loops while the value is not positive and decrements; post increments and signals. Replace the `while` by `if` and it breaks (Problem C).
11. **Bounded buffer with semaphores?** `empty` initialised to the number of slots, `full` to 0, a mutex only around the buffer update; wait on the counting semaphore before taking the mutex.
12. **Signal or broadcast?** `signal` when any single waiter can use the event; `broadcast` when waiters wait for different things (a covering condition) or the state change enables all of them. Broadcast to a herd of 32 to 128 waiters cost 4.5 to 7 times as much per event here.
13. **Readers-writers: who should wait?** With reader preference writers can starve (the writer got in once in 2 seconds, and glibc's default behaves so); with writer preference readers wait for a pending writer (3 to 4 percent fewer reads here) and could starve under a stream of writers; a fair lock avoids both.
14. **Dining philosophers: three solutions?** Break the circular wait with a resource order (lowest fork first); limit contenders to N-1 with a semaphore; or use trylock and back off (livelock risk). The semaphore version was fairest here.
15. **Why does the obvious reusable barrier hang?** A fast thread re-enters before a slow waiter re-checks the count; use a generation number.
16. **What is priority inversion and the fix?** A high-priority thread waits for a lock held by a low-priority one that a medium-priority thread keeps preempting. Priority inheritance boosts the holder. Measured: 678 ms against 78 ms.
17. **What are an atomicity violation and an order violation?** A region that was meant to be indivisible is interleaved (check-then-act); two operations meant to happen in a fixed order do not (use before initialisation). Together they were 97 percent of the non-deadlock bugs in the study OSTEP reports.
18. **What are the four conditions of deadlock?** Mutual exclusion, hold and wait, no preemption, circular wait; remove one to prevent it (OS 09).

---

## Problems

**Where to solve them.** On any Linux machine with a C compiler and at least 2 CPUs. A is on paper with a table to check against. B and C use the programs `pb` and `pc` shown in the answers (build with `-lpthread`; they need `sy.h`'s helpers or equivalents). D needs the program `d3_rw`. E needs root (real-time priorities) and the program `d7_pi`; on a machine with more than two CPUs pin all three threads to one CPU as the program does. Numbers differ on your machine; the ratios and the hangs should not.

**How to approach them.** Predict before you run. For A, count what one philosopher does in one cycle. For B, simulate three threads by hand until one runs out of work. For C, ask which assumption of the `while` loop is gone. For D, decide what each side of the trade costs and which side your service can afford. For E, draw the timeline in milliseconds.

### Problem A (E, entry level): the philosophers' arithmetic

With 5 forks and every philosopher needing two, how many can eat at the same time? Show in three lines that the `waiter` rule (a semaphore initialised to 4) cannot deadlock. Each philosopher in the program thinks about 1.1 ms, pauses 10 ms between picking up the two forks and eats 2 ms. What is the largest number of meals one philosopher could eat in 3 seconds, and how does that compare with the measured totals?

### Problem B (M, medium): the hanging buffer

A one-slot buffer has one producer and two consumers that all use one condition variable `one` and the `while` rule. Trace, thread by thread, how all three end up asleep with `count == 0`. Then run `pb` and fix it two ways.

### Problem C (M, medium): break the semaphore

The hand-built semaphore of section 3.1 is used in a bounded buffer with 2 producers and 2 consumers. Predict which of these three changes breaks it: (a) `if` instead of `while` in `zem_wait`; (b) `signal` after unlocking the mutex in `zem_post`; (c) `broadcast` instead of `signal`. Run `pc` to check, and explain the one that breaks.

### Problem D (H, hard): choose the lock for a configuration cache

Eight request threads read a shared configuration on every request; one thread updates it every 20 ms. You must pick a readers-writers policy. Using the measurements of section 6.1, say what each policy costs, which you would choose, and what the benchmark does not show.

### Problem E (H, hard): predict a priority inversion

With the timings of section 7 (L takes the lock at time 0 and needs 100 ms of CPU; H wants the lock after about 22 ms; M becomes runnable after about 32 ms and needs 600 ms), predict H's wait with a plain mutex and with priority inheritance, then run `d7_pi`.

---

## Answers

**A.** Two philosophers can eat at once: each uses two of the five forks, and three would need six. The `waiter` rule: at most four philosophers hold or wait for forks at once; if all four held one fork each, the fifth fork (there are five) is held by nobody, and the philosopher next to that fork can take it and eat, so a deadlock needs all five to hold exactly one fork each, which four contenders cannot do (pigeonhole). One cycle takes at least 1.1 + 10 + 2 = 13.1 ms, so in 3 seconds at most about 3,000 / 13.1 = 229 meals per philosopher and 1,145 in total (less in practice: each sleep rounds up a little). The measured table of section 6.2 has `waiter` at about 196 to 197 per philosopher, 86 percent of the bound: almost nobody waits. `ordered` reached about 830 in total with a wide spread (103 to 209 per philosopher), which means some philosophers spend much of their time waiting for a fork, and `trylock` about 290.

**B.** Trace (P is the producer, C1 and C2 the consumers): P fills the slot (`count = 1`) and signals `one`, which wakes C1; P loops, finds `count == 1` and sleeps on `one`. C1 takes the item (`count = 0`) and signals `one`: the choice of waiter is up to the system, and it picks C2 (asleep since the start), not P. C2 wakes, finds `count == 0`, goes back to sleep. Now P, C1 (about to wait) and C2 are all asleep on `one`, `count` is 0, and nobody is left to signal. The code that does it:

```c
static void wake(pthread_cond_t *c)
{
    if (mode == 1) pthread_cond_broadcast(c); else pthread_cond_signal(c);
}

static void *consumer(void *arg)
{
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&mu);
        while (count == 0 && !done) pthread_cond_wait(consumer_waits_on(), &mu);
        if (count == 0) { pthread_mutex_unlock(&mu); return NULL; }
        count = 0;
        taken++;
        wake(mode == 2 ? &empty : &one);
        pthread_mutex_unlock(&mu);
    }
}
```

Output of the five runs of `onecv_signal` and the two fixes (`onecv_broadcast` wakes everybody so the producer is among them; `twocv` gives producers and consumers their own queues):

```text
onecv_signal     HUNG after 3 of 20000 items: no progress for 3 s, 3 of 4 threads asleep, count = 0
onecv_signal     HUNG after 4 of 20000 items: no progress for 3 s, 3 of 4 threads asleep, count = 0
onecv_signal     HUNG after 3 of 20000 items: no progress for 3 s, 3 of 4 threads asleep, count = 0
onecv_signal     HUNG after 4 of 20000 items: no progress for 3 s, 3 of 4 threads asleep, count = 0
onecv_signal     HUNG after 2 of 20000 items: no progress for 3 s, 3 of 4 threads asleep, count = 0
onecv_broadcast  finished 20000 of 20000 items in 0.55 s
twocv            finished 20000 of 20000 items in 0.44 s
```

In `twocv` mode `wake` is called with the other side's condition variable, so a consumer can only wake a producer and the reverse. Prefer that fix; broadcast is the covering-condition patch.

**C.** (a) breaks it, (b) and (c) do not. The `while` is what keeps `zem_wait` correct when another thread arrives between the post and the woken thread's return from the wait. With `if`: the value is 0 and two threads wait; a post makes it 1 and wakes one; before that thread re-locks the mutex, a third thread's `zem_wait` sees 1 and decrements to 0; the woken thread then decrements again without re-checking, to -1. Two threads have used the same permit, which in a bounded buffer means one slot used twice or overwritten:

```c
static void zem_wait(zem_t *z)
{
    pthread_mutex_lock(&z->m);
    if (variant == 1) { if (z->value <= 0) pthread_cond_wait(&z->c, &z->m); }   /* if, not while */
    else { while (z->value <= 0) pthread_cond_wait(&z->c, &z->m); }
    z->value--;
    if (z->value < 0) atomic_fetch_add(&below_zero, 1);
    pthread_mutex_unlock(&z->m);
}
```

Run 1:

```text
while           finished in 0.47 s  value went below zero 0 times  checksum ok
if              finished in 0.40 s  value went below zero 9900 times  checksum WRONG
broadcast       finished in 0.34 s  value went below zero 0 times  checksum ok
signal_outside  finished in 0.45 s  value went below zero 0 times  checksum ok
```

Run 2:

```text
while           finished in 0.39 s  value went below zero 0 times  checksum ok
if              finished in 0.45 s  value went below zero 4379 times  checksum WRONG
broadcast       finished in 0.32 s  value went below zero 0 times  checksum ok
signal_outside  finished in 0.45 s  value went below zero 0 times  checksum ok
```

The `if` version drove the value below zero thousands of times in 400,000 waits (9,900 in run 1, 4,379 in run 2) and the checksum of consumed items was wrong. Signalling after unlocking (b) is allowed (the state was changed under the lock; the POSIX page does not require holding the mutex for a signal, and OSTEP advises holding it as a rule of thumb) and `broadcast` (c) is correct but wakes all waiters for each post.

**D.** The measured costs (section 6.1): **reader preference** gave the writer one write in the whole 2-second run (about 1,996 ms of waiting): the configuration would stay stale for as long as the readers keep overlapping. glibc's default behaves the same, so "just use `pthread_rwlock_t`" has this property on this system. **Writer preference** bounded the writer's wait to about 1 ms and cost the readers 3 to 4 percent of their reads. A **fair** lock gives both bounded waits at more bookkeeping (from memory). For a configuration that must be fresh, choose writer preference (either the glibc kind `PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP` or the hand-built one), since the writer here is a single thread with a long gap between updates, so it cannot starve the readers. What the benchmark does not show: its readers hold the lock for 1 ms, which makes overlap certain; real readers holding a lock for 100 ns overlap far less, so starvation would be milder (this was not measured). The reader count was 3, not 8. And for read-mostly data the better design may be no lock for the readers at all (publish a new immutable copy and swap a pointer; from memory, the RCU idea).

**E.** Timeline in milliseconds with the plain mutex: L runs from 0 and holds the lock; H wakes at about 22 and blocks on the lock; M wakes at about 32 and preempts L (priority 20 against 10), so L has done about 32 ms of its 100 ms. M runs for 600 ms, until about 632. L resumes, does its remaining 68 ms, and releases at about 700. H gets the lock: it waited 700 - 22 = **678 ms**. With priority inheritance: when H blocks at about 22 ms, L is raised to priority 30; M (20) cannot preempt it; L has done about 22 ms and needs 78 ms more; it releases at about 100 ms, and H waited **78 ms**. M then runs. The measured values:

```text
plain    H waited  678.5 ms for a lock whose holder needs 100 ms of CPU in total
inherit  H waited   77.9 ms for a lock whose holder needs 100 ms of CPU in total
```

and a second run:

```text
plain    H waited  679.8 ms for a lock whose holder needs 100 ms of CPU in total
inherit  H waited   80.0 ms for a lock whose holder needs 100 ms of CPU in total
```

The machine reproduced both predictions to within 2 ms in both runs. The lesson of the numbers: the inverted wait grew with M's 600 ms of work, something H has no control over, while the inherited wait was bounded by the length of L's critical section.

---

## Cheat sheet

| Item | Rule |
|---|---|
| Waiting pattern | `lock; while (!cond) wait(cv, m); act; unlock` |
| Changing the state | under the same mutex, then `signal` |
| Why `while` | Mesa semantics and spurious wake-ups |
| Why a state variable | a signal is not remembered |
| Condition variable | queue of sleepers; `wait` releases the mutex and sleeps atomically |
| `signal` against `broadcast` | one waiter can use it, against waiters waiting for different things (covering condition) |
| One cv, two conditions | `signal` may wake the wrong kind; hangs (5 of 5 here); use two |
| Semaphore | integer; `wait` blocks while 0 then decrements, `post` increments and wakes |
| Initial value | 1 for a lock, 0 for ordering, N for N resources |
| Mutex against semaphore | the mutex has an owner (and priority inheritance); the semaphore does not |
| Bounded buffer with semaphores | `empty` = slots, `full` = 0; `sem_wait` first, mutex only around the buffer |
| Semaphore from cv | mutex, cv, integer; `while (value <= 0) wait` (with `if` it breaks) |
| Readers-writers | reader preference can starve writers (glibc default does); writer preference costs readers a little |
| Dining philosophers | resource order, a semaphore of N-1, or trylock with back-off (livelock risk) |
| Reusable barrier | count arrivals and a generation; waiters wait for the generation to change |
| Priority inversion | H waits for L while M runs; fix: priority inheritance (678 ms against 78 ms) |
| Atomicity violation | check-then-act without a lock; lock the whole region |
| Order violation | use before initialisation; make the order explicit (semaphore at 0) |
| 97 percent | of the non-deadlock bugs in the study OSTEP reports are one of these two |

---

## Sources

- [OSTEP, Condition Variables](https://pages.cs.wisc.edu/~remzi/OSTEP/threads-cv.pdf): the join problem, state variable and lock, `if` against `while`, one cv against two, Mesa and Hoare, covering conditions (fetched).
- [OSTEP, Semaphores](https://pages.cs.wisc.edu/~remzi/OSTEP/threads-sema.pdf): definition, binary semaphores, ordering, bounded buffer and its deadlock, reader-writer lock, dining philosophers, throttling, `Zemaphore` (fetched).
- [OSTEP, Common Concurrency Problems](https://pages.cs.wisc.edu/~remzi/OSTEP/threads-bugs.pdf): the Lu et al. study (105 bugs, 74 non-deadlock, 97 percent atomicity or order violations), deadlock conditions and prevention (fetched).
- [pthread_cond_wait(3p)](https://man7.org/linux/man-pages/man3/pthread_cond_wait.3p.html): spurious wake-ups, the predicate loop, the mutex requirement, which waiter is chosen (fetched).
- [futex(2)](https://man7.org/linux/man-pages/man2/futex.2.html) and [FUTEX_WAIT(2const)](https://man7.org/linux/man-pages/man2/FUTEX_WAIT.2const.html): the PI futex and the atomic check-and-sleep that condition variables rest on (fetched for OS 06).
