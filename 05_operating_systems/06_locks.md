# OS 06: Locks, from Peterson to the futex

*Part of the [Operating systems plan](00_plan.md) · Time: about 8 hours (5 reading and experiments, 3 problems) · You need: [OS 03](03_threads.md) (threads and the lost-update bug), [OS 05](05_process_scheduling.md) (preemption, ticks, the scheduler) and a working knowledge of C atomics; the memory-ordering rules behind the fences are taken further in OS 08*

Two threads add 1 to a shared counter five million times each, and the answer comes out near five million instead of ten. Every systems interview reaches this bug sooner or later, and the follow-up questions are always the same. What is a lock made of? Why can't a lock just disable interrupts? What does a spinlock do to the cache? When should a thread spin and when should it sleep? What is a futex, and how does `pthread_mutex_lock` use one? Why is a ticket lock fair and yet sometimes terrible? Why is a finer-grained lock not always faster? This chapter builds a lock from nothing, one layer at a time, and measures each layer on a real machine: software-only locks, hardware atomic instructions, four kinds of spinlock, the futex system call, a mutex built on it by hand, two-phase locks, and then three locked data structures.

By the end you can:

- state the three requirements of a critical-section solution (mutual exclusion, progress, bounded waiting) and the three yardsticks for a lock (correctness, fairness, performance);
- explain why `counter++` loses updates by reading its three machine instructions, and what `lock addq` changes;
- explain why disabling interrupts fails for user code and on a multiprocessor, and what the kernel does instead;
- write Peterson's lock and Lamport's bakery lock, and show (measured) that Peterson fails on x86 without a full fence;
- build test-and-set, test-and-test-and-set, ticket and MCS spinlocks, and explain each one's cache behaviour and fairness;
- explain lock-holder preemption, and show why a FIFO spinlock collapses when threads outnumber CPUs (measured: three orders of magnitude);
- use the futex system call directly, write the three-state futex mutex from first principles, and count the system calls it makes;
- choose a spin time for a two-phase lock from measurements;
- build an approximate counter, a hand-over-hand list and a per-bucket hash table, and explain from measurements why fine-grained locking sometimes loses and why false sharing can erase the gain.

**Where the numbers come from.** Every program in this chapter was compiled with GCC 13.3 (`-O2 -Wall -Wextra`, linked with `-lpthread`) and run on a Linux 6.18.44 kernel inside a Firecracker micro-VM with 2 vCPUs (Intel Xeon at 2.30 GHz) with glibc 2.39. The futex semantics are from the futex(2) and FUTEX_WAIT(2const) man pages, the lock categories from the kernel's locking documentation, and the kernel mutex design from its design document (all fetched; linked in the Sources). Drepper's "Futexes Are Tricky" could not be fetched (the site refuses automated readers), so the three-state mutex below is written from memory of that paper and checked by the tests in this chapter, not against the paper. Facts from general knowledge are marked "from memory".

**Data and timing.** Throughput figures are acquisitions per second over a fixed time (1 s unless stated), counted by every thread and checked against a counter that the lock itself protects ("count ok" in the output means no update was lost). The lock benchmarks ran twice and are shown as run 1 / run 2. Two CPUs cannot show how a lock behaves with 32 contenders: the cache-line traffic that separates a test-and-set lock from an MCS lock grows with the number of CPUs, and this machine has two. Where a claim needs more cores than that, it is marked as from memory or from the literature, not measured. The virtual CPUs are also subject to the hypervisor's own scheduling, so single figures can wander by 20 percent between runs; read ratios of 2 or more as real and smaller differences as noise.

---

## 1. The critical-section problem

A **critical section** is a stretch of code that touches shared state and must not run in two threads at once. A solution must provide three things (the standard textbook formulation, from memory):

1. **Mutual exclusion.** At most one thread is inside.
2. **Progress.** If no thread is inside and some want in, one of them gets in without waiting for threads that are not trying; the decision cannot be postponed forever.
3. **Bounded waiting.** Once a thread has asked to enter, only a bounded number of other threads can enter before it does (no starvation).

And it must hold with no assumption about how fast each thread runs or how many CPUs there are.

### 1.1 What goes wrong without it

The program below runs four counters on two threads, each adding 1 five million times, so the right answer is 10,000,000 each time.

```c
/* c1_race.c: two threads add 1 to one counter 5,000,000 times each, four ways. */
#include "lk.h"

#define N 5000000
static volatile long plain;                  /* volatile: force a real load and store each time */
static atomic_long atom;
static long guarded;
static lock_t L;
static int mode;

static void *worker(void *arg)
{
    (void)arg;
    for (int i = 0; i < N; i++) {
        switch (mode) {
        case 0: plain = plain + 1; break;
        case 1: atomic_fetch_add_explicit(&atom, 1, memory_order_relaxed); break;
        case 2: pthread_mutex_lock(&L.pm); guarded++; pthread_mutex_unlock(&L.pm); break;
        case 3: tas_lock(&L); guarded++; tas_unlock(&L); break;
        }
    }
    return NULL;
}

int main(void)
{
    static const char *name[] = { "plain load/add/store", "atomic fetch_add", "pthread mutex", "spinlock (tas)" };
    pthread_mutex_init(&L.pm, NULL);
    printf("%-22s %12s %10s %10s\n", "method", "final value", "lost", "seconds");
    for (mode = 0; mode < 4; mode++) {
        plain = 0; atom = 0; guarded = 0;
        pthread_t a, b;
        double t0 = now();
        pthread_create(&a, NULL, worker, NULL);
        pthread_create(&b, NULL, worker, NULL);
        pthread_join(a, NULL);
        pthread_join(b, NULL);
        double dt = now() - t0;
        long v = mode == 0 ? plain : mode == 1 ? atom : guarded;
        printf("%-22s %12ld %10ld %10.3f\n", name[mode], v, 2L * N - v, dt);
    }
    return 0;
}
```

Output:

```text
method                  final value       lost    seconds
plain load/add/store        4964993    5035007      0.009
atomic fetch_add           10000000          0      0.336
pthread mutex              10000000          0      0.468
spinlock (tas)             10000000          0      1.003
```

The plain counter ends near half of the right answer: about five million updates were lost. The cause is in the machine code. `counter = counter + 1` is three instructions, shown here from `objdump -d` of a two-line function (the `volatile` keeps the compiler from merging the loop into one addition):

```text
0000000000000000 <plain_inc>:
endbr64
mov    0x0(%rip),%rax        # b <plain_inc+0xb>
add    $0x1,%rax
mov    %rax,0x0(%rip)        # 16 <plain_inc+0x16>
ret
0000000000000020 <atomic_inc>:
endbr64
lock addq $0x1,0x0(%rip)        # 2d <atomic_inc+0xd>
ret
```

The plain increment loads the counter, adds, and stores. If both CPUs load the same value before either stores, one increment disappears. The atomic version is one instruction, `lock addq`, which holds the cache line for the duration of the read-modify-write, so no other CPU can slip in between. The price: the plain loop is fast (0.009 s) and wrong; the atomic one took about 0.3 s, the mutex about 0.47 s and the spinlock 0.8 to 1.0 s (the program ran twice; the output above is the second run). Per increment under two-thread contention that is about 30 ns for an atomic, 47 ns for the mutex and 80 to 100 ns for the spinlock. A lock is correct first and then judged on cost.

### 1.2 Three yardsticks for a lock

OSTEP (fetched) judges a lock on three things, and so does this chapter:

- **Correctness:** does it give mutual exclusion?
- **Fairness:** can a waiting thread starve?
- **Performance:** what does it cost with no contention, with contention among threads on one CPU, and with contention across several CPUs?

Those three performance cases are separate questions with separate answers, and later sections measure each of them.

---

## 2. Why not just disable interrupts?

On a single CPU, a thread can only lose the CPU to an interrupt (the timer tick ends its slice, a device interrupt may wake something more important). If it disables interrupts around the critical section, nothing can preempt it, and mutual exclusion follows. Early systems did exactly this. It fails in three ways (OSTEP, fetched):

1. **It needs a privileged instruction.** User code cannot execute it, and letting any program turn off interrupts means trusting every program not to loop forever with them off.
2. **It does not work on multiprocessors.** Disabling interrupts on CPU 0 does nothing about a thread on CPU 1 entering the same critical section.
3. **Interrupts can be lost** if they stay disabled too long.

The kernel still disables interrupts, but for a different reason than mutual exclusion between CPUs. If an interrupt handler on this CPU takes a lock that the interrupted code on the same CPU already holds, the CPU waits for itself forever. So code that shares a lock with an interrupt handler takes the lock with interrupts off on this CPU: the `spin_lock_irqsave()` family (the suffix rules are in the kernel's locking documentation, fetched). The spinlock handles other CPUs; the disabled interrupts handle this one. Spinning locks also disable preemption while held (same document), for the reason section 7 measures.

---

## 3. Locks from loads and stores alone

Before atomic instructions existed in every processor, people asked whether mutual exclusion could be built from ordinary reads and writes. It can, and the two classic answers are worth knowing because interviewers ask for them and because they expose the memory-ordering problem sharply.

### 3.1 Peterson's lock (two threads)

Each thread has a `flag` saying "I want in", and there is one shared `turn` saying whose turn it is to wait. To enter, set your flag, then give the turn to the other thread, then wait while the other thread wants in and it is the other thread's turn:

```c
static void p_lock(int me)
{
    int other = 1 - me;
    switch (variant) {
    case 0:                                  /* relaxed: no ordering at all */
        atomic_store_explicit(&flag[me], 1, memory_order_relaxed);
        atomic_store_explicit(&turn, other, memory_order_relaxed);
        while (atomic_load_explicit(&flag[other], memory_order_relaxed) &&
               atomic_load_explicit(&turn, memory_order_relaxed) == other) relax();
        break;
    case 1:                                  /* release stores, acquire loads */
        atomic_store_explicit(&flag[me], 1, memory_order_release);
        atomic_store_explicit(&turn, other, memory_order_release);
        while (atomic_load_explicit(&flag[other], memory_order_acquire) &&
               atomic_load_explicit(&turn, memory_order_acquire) == other) relax();
        break;
    case 2:                                  /* release/acquire plus a full fence */
        atomic_store_explicit(&flag[me], 1, memory_order_release);
        atomic_store_explicit(&turn, other, memory_order_release);
        atomic_thread_fence(memory_order_seq_cst);
        while (atomic_load_explicit(&flag[other], memory_order_acquire) &&
               atomic_load_explicit(&turn, memory_order_acquire) == other) relax();
        break;
    default:                                 /* sequentially consistent (the default) */
        atomic_store(&flag[me], 1);
        atomic_store(&turn, other);
        while (atomic_load(&flag[other]) && atomic_load(&turn) == other) relax();
        break;
    }
}

static void p_unlock(int me) { atomic_store_explicit(&flag[me], 0, memory_order_release); }

static void *worker(void *arg)
{
    int me = (int)(long)arg;
    pin(me);
    while (!atomic_load(&go)) relax();
    for (long i = 0; i < rounds; i++) {
        p_lock(me);
        if (atomic_fetch_add_explicit(&inside, 1, memory_order_relaxed) != 0) violations++;
        shared++;
        atomic_fetch_sub_explicit(&inside, 1, memory_order_relaxed);
        p_unlock(me);
    }
    return NULL;
}
```

The code is in the program's `variant` switch: case 0 is relaxed ordering, 1 is release stores and acquire loads, 2 adds a full fence after the two stores, 3 is the default (sequentially consistent). The argument that it works (from memory, and standard): if both threads are inside, each saw the other's flag clear or the turn in its own favour; but the turn is written last by one of them, so it names exactly one waiter, and that one cannot get past the loop while the other's flag is still set. That argument assumes the stores become visible in program order and that a load sees the latest stores. Real CPUs do not promise that. The program runs all four variants with two pinned threads, counting entries that overlapped:

```text
variant                         entries lost updates     overlaps
relaxed                         4000000          543         2324
release/acquire                 4000000          419         2187
rel/acq + seq_cst fence         4000000            0            0
seq_cst                         4000000            0            0
```

The same program, second run:

```text
variant                         entries lost updates     overlaps
relaxed                         4000000         1849         4760
release/acquire                 4000000           15           71
rel/acq + seq_cst fence         4000000            0            0
seq_cst                         4000000            0            0
```

What the table shows: with relaxed ordering, the lock lets both threads in thousands of times. Release and acquire do not fix it (419 lost updates in the first run and 15 in the second, against 543 and 1,849 for relaxed), because the failing pattern is a **store followed by a load of a different location**: each thread's stores to its flag and the turn sit in the CPU's store buffer while its load of the other's flag reads the old value from the cache. On x86 that store-then-load reordering is the one reordering the hardware allows (from memory; OS 08 covers the model), and release and acquire do not forbid it. A full fence between the stores and the loads does, and so does the sequentially consistent version. GCC 13.3 compiles the fence into `lock orq $0x0,(%rsp)` and the `seq_cst` stores into `xchg`; both are full barriers on x86 (read from `objdump -d` of this program). Peterson's lock is correct on paper and wrong on a real CPU unless the ordering is right. OSTEP makes the same remark: it no longer works on modern hardware because of relaxed memory models (fetched).

### 3.2 Lamport's bakery lock (N threads)

Peterson handles two. Lamport's bakery algorithm handles any number: each thread takes a number larger than any it sees (like a ticket at a bakery counter), then waits for every thread holding a smaller number, breaking ties by thread id.

```c
static void bakery_lock(int me)
{
    atomic_store(&choosing[me], 1);
    int max = 0;
    for (int j = 0; j < nthr; j++) {
        int v = atomic_load(&number[j]);
        if (v > max) max = v;
    }
    atomic_store(&number[me], max + 1);          /* take a ticket one larger than any seen */
    atomic_store(&choosing[me], 0);
    for (int j = 0; j < nthr; j++) {
        if (j == me) continue;
        while (atomic_load(&choosing[j])) sched_yield();
        for (;;) {                               /* wait while j has a smaller (ticket, id) pair */
            int nj = atomic_load(&number[j]), nm = atomic_load(&number[me]);
            if (nj == 0 || nj > nm || (nj == nm && j > me)) break;
            sched_yield();
        }
    }
}

static void bakery_unlock(int me) { atomic_store(&number[me], 0); }

static void *worker(void *arg)
{
    int me = (int)(long)arg;
    while (!atomic_load(&go)) relax();
    for (long i = 0; i < rounds; i++) {
        bakery_lock(me);
        if (atomic_fetch_add(&inside, 1) != 0) violations++;
        shared++;
        atomic_fetch_sub(&inside, 1);
        bakery_unlock(me);
    }
    return NULL;
}
```

The `choosing` flags stop a thread from being compared while it is still picking its number. The program runs it with sequentially consistent atomics and `sched_yield` in the waits (more threads than CPUs would otherwise burn their slices):

```text
threads 2  entries 200000  lost updates 0  overlaps 0  0.03 s
threads 3  entries 300000  lost updates 0  overlaps 0  0.12 s
threads 5  entries 250000  lost updates 0  overlaps 0  0.18 s
```

No overlaps and no lost updates, with 5 threads on 2 CPUs. Cost: each acquisition reads every other thread's number (`N` loads), and the numbers grow without bound (in principle; from memory). The lock needs no atomic read-modify-write, which was its point in 1974 (from memory), and nobody uses it in practice because hardware provides cheaper primitives.

---

## 4. Hardware atomic instructions

All practical locks rest on instructions that read, modify and write a memory word as one indivisible step. Three matter:

| Primitive | What it does | C11 | x86-64 | Used by |
|---|---|---|---|---|
| test-and-set (exchange) | store a new value, return the old one | `atomic_exchange` | `xchg` (implicitly locked) | TAS, TTAS, futex mutex |
| compare-and-swap (CAS) | store only if the word equals an expected value; report the old value | `atomic_compare_exchange_strong` | `lock cmpxchg` | futex mutex fast path, MCS release, lock-free structures |
| fetch-and-add | add and return the old value | `atomic_fetch_add` | `lock xadd` | ticket lock, counters |

On arm64 and other load-linked/store-conditional machines the same operations are built from `ldxr`/`stxr` pairs, or from single instructions in newer cores with the "large system extensions" (from memory). The cost on this machine, from section 1.1: an atomic increment with two threads fighting over the line costs about 30 ns, against about 1 ns for a plain one. A single thread with no contention pays little: section 5.3 measures a whole lock and unlock pair at 7 to 14 ns uncontended.

Every lock below is acquired with `memory_order_acquire` and released with `memory_order_release`. Acquire says nothing after the lock can move before it; release says nothing before the unlock can move after it. Together they keep the protected data inside the critical section. (The cost of getting this wrong, and the full model, are OS 08.)

---

## 5. Spinlocks

A **spinlock** makes a thread that cannot get the lock loop until it can. It is the right tool when the wait is shorter than a context switch, and the wrong one otherwise.

### 5.1 The four designs

**Test-and-set (TAS).** One word, 0 free and 1 held. Acquire exchanges 1 into it; if the old value was 0 the lock is yours.

```c
static void tas_lock(lock_t *l)
{
    while (atomic_exchange_explicit(&l->w, 1, memory_order_acquire)) relax();
}

static void tas_unlock(lock_t *l) { atomic_store_explicit(&l->w, 0, memory_order_release); }

/* 2. test-and-test-and-set: spin on a read, write only when the lock looks free */
static void ttas_lock(lock_t *l)
{
    for (;;) {
        while (atomic_load_explicit(&l->w, memory_order_relaxed)) relax();
        if (!atomic_exchange_explicit(&l->w, 1, memory_order_acquire)) return;
    }
}
```

Every attempt is a write, so the cache line moves to the spinning CPU in exclusive state on every try, and the holder's own unlock must win it back (the standard coherence picture, from memory). Unfair: nothing orders the contenders.

**Test-and-test-and-set (TTAS).** Spin on a plain read, which can be served from a shared copy in the local cache with no traffic, and attempt the exchange only when the lock looks free.

```c
static void ttas_lock(lock_t *l)
{
    for (;;) {
        while (atomic_load_explicit(&l->w, memory_order_relaxed)) relax();
        if (!atomic_exchange_explicit(&l->w, 1, memory_order_acquire)) return;
    }
}
```

Less traffic while waiting, but when the holder releases, every waiter sees the line change and every waiter tries the exchange at once. Still unfair.

**Ticket lock.** Two counters. Arriving threads take the next ticket with fetch-and-add; the lock serves `owner`; unlock increments it.

```c
static void ticket_lock(lock_t *l)
{
    unsigned t = atomic_fetch_add_explicit(&l->next, 1, memory_order_relaxed);
    while (atomic_load_explicit(&l->owner, memory_order_acquire) != t) relax();
}

static void ticket_unlock(lock_t *l)
{
    unsigned o = atomic_load_explicit(&l->owner, memory_order_relaxed);
    atomic_store_explicit(&l->owner, o + 1, memory_order_release);
}
```

Strictly first come, first served, so waiting is bounded: the fairness TAS lacks. All waiters still spin on the same `owner` word, so a release invalidates every waiter's copy. Linux used ticket spinlocks for years (from memory) before replacing them.

**MCS lock.** Waiters form a linked queue. Each waiter spins on a flag in its own node, so a release touches exactly one waiter's cache line.

```c
static void mcs_lock(lock_t *l)
{
    atomic_store_explicit(&me.next, NULL, memory_order_relaxed);
    atomic_store_explicit(&me.locked, 1, memory_order_relaxed);
    mcs_node *pred = atomic_exchange_explicit(&l->tail, &me, memory_order_acq_rel);
    if (pred) {
        atomic_store_explicit(&pred->next, &me, memory_order_release);
        while (atomic_load_explicit(&me.locked, memory_order_acquire)) relax();
    }
}

static void mcs_unlock(lock_t *l)
{
    mcs_node *nxt = atomic_load_explicit(&me.next, memory_order_acquire);
    if (!nxt) {
        mcs_node *expect = &me;
        if (atomic_compare_exchange_strong_explicit(&l->tail, &expect, NULL,
                                                    memory_order_release, memory_order_relaxed))
            return;                                   /* nobody queued behind us */
        while (!(nxt = atomic_load_explicit(&me.next, memory_order_acquire))) relax();
    }
    atomic_store_explicit(&nxt->locked, 0, memory_order_release);
}
```

Fair, and one cache-line transfer per handoff regardless of the number of waiters (Mellor-Crummey and Scott, 1991; from memory). Costs a queue node per thread and a pointer-sized lock. The Linux kernel's `qspinlock` is an MCS lock squeezed into one 32-bit word: instead of a pointer it stores a CPU number and a node index, and a waiter that finds a busy lock with no queue can set a "pending" bit instead of using a node (LWN, fetched).

### 5.2 How to measure a lock

The benchmark `c3_bench` runs N threads that loop: lock, increment a shared counter, do some work inside, unlock, do some work outside. "Work" is a busy loop of a given length. The loop of every thread:

```c
static void *worker(void *arg)
{
    long id = (long)arg;
    if (do_pin == 1) pin((int)id % 2);
    if (do_pin == 2) pin(0);
    while (!atomic_load_explicit(&go, memory_order_acquire)) relax();
    long mine = 0;
    while (!atomic_load_explicit(&stop, memory_order_relaxed)) {
        ops->lock(&L);
        shared++;
        for (volatile int i = 0; i < cs_work; i++) { }
        ops->unlock(&L);
        mine++;
        for (volatile int i = 0; i < out_work; i++) { }
    }
    slots[id].count = mine;
    return NULL;
}
```

How long is a unit of work? Calibration with one thread and no contention (pthread mutex, nothing outside the lock, so `ns/acq` is the pair cost plus the work inside):

```text
pthread      thr=1    72.212 Macq/s        14 ns/acq
pthread      thr=1    30.381 Macq/s        33 ns/acq
pthread      thr=1    13.872 Macq/s        72 ns/acq
pthread      thr=1     0.747 Macq/s      1339 ns/acq
```

So 50 iterations take about 19 ns, 200 about 58 ns and 5,000 about 1.3 microseconds (each figure minus the 14 ns for the lock pair). The benchmark's "50 inside, 200 outside" is a critical section of about 20 ns followed by about 60 ns of other work: a short critical section and a busy lock.

The program prints throughput in millions of acquisitions per second (`Macq/s`), the mean time per acquisition (`ns/acq`, the inverse of throughput, not the latency of one acquire), the share of acquisitions of the least and most lucky thread, and the number of futex calls. The counter is checked against the total: "count ok".

### 5.3 Results on two CPUs

**One thread, nothing else running**: the price of one lock and unlock pair. Run 1 / run 2 in nanoseconds:

| Lock | ns per pair |
|---|---|
| TAS | 9 / 9 |
| TTAS | 9 / 9 |
| ticket | 7 / 7 |
| MCS | 13 / 13 |
| futex mutex (section 7) | 14 / 13 |
| pthread mutex | 14 / 14 |
| futex mutex that wakes on every unlock | 126 / 121 |

An uncontended spinlock is a handful of nanoseconds, and the futex mutex's fast path costs the same as the pthread mutex's (section 7 shows that the pthread mutex also runs on futexes). The last row is the same futex mutex with one change: unlock always makes a system call. It costs about 9 times as much, which is what a system call is.

**Two threads on two CPUs**, 50 loop iterations inside and 200 outside (`ns/acq`, run 1 / run 2; share of the least and most lucky thread):

| Lock | ns per acquisition | Macq/s | share |
|---|---|---|---|
| TAS | 309 / 303 | 3.235 / 3.296 | 50 to 50 / 45 to 55 |
| TTAS | 295 / 308 | 3.385 / 3.246 | 46 to 54 / 50 to 50 |
| ticket | 277 / 360 | 3.616 / 2.781 | 49 to 51 / 49 to 51 |
| MCS | 277 / 425 | 3.604 / 2.350 | 50 to 50 / 50 to 50 |
| futex mutex | 328 / 413 | 3.048 / 2.423 | 49 to 51 / 49 to 51 |
| pthread mutex | 317 / 299 | 3.156 / 3.339 | 44 to 56 / 50 to 50 |
| two-phase (section 8) | 259 / 278 | 3.854 / 3.596 | 48 to 52 / 49 to 51 |

Within a factor of about 1.6 they are all the same, and the run-to-run spread is as large as the gap between designs. With two CPUs there are only two waiters, so the traffic argument for MCS has nothing to show: this table cannot rank the four spinlocks. What it does show: every lock lands between about 260 and 430 ns per acquisition, which is far above the uncontended 7 to 14 ns. The extra is the cost of moving the lock's cache line (and the counter's) between the two CPUs at every handoff. Contention costs even when nobody sleeps.

One benchmark artifact deserves a warning. With **no work at all** inside or outside the lock, two pinned threads gave (a separate 0.5 s run):

```text
tas          thr=2     9.582 Macq/s       104 ns/acq  share min/max  41%/ 59%  futex wait/wake 0/0  count ok
ttas         thr=2    10.974 Macq/s        91 ns/acq  share min/max  40%/ 60%  futex wait/wake 0/0  count ok
ticket       thr=2     5.298 Macq/s       189 ns/acq  share min/max  50%/ 50%  futex wait/wake 0/0  count ok
mcs          thr=2     2.720 Macq/s       368 ns/acq  share min/max  50%/ 50%  futex wait/wake 0/0  count ok
futex        thr=2    19.440 Macq/s        51 ns/acq  share min/max  46%/ 54%  futex wait/wake 244564/457266  count ok
pthread      thr=2    22.042 Macq/s        45 ns/acq  share min/max  46%/ 54%  futex wait/wake 0/0  count ok
```

The futex and pthread mutexes win by a factor of 4 to 7 over the FIFO locks and 2 over TAS. The likely reason (I did not measure burst lengths) is that a mutex lets the thread that just unlocked take the lock again immediately, since it is not first come, first served, so one thread runs many iterations without a cache-line transfer while the other sleeps. The ticket and MCS locks hand the lock over strictly, which forces a line transfer on every acquisition. That higher throughput is not a better lock; it is a less fair one, and it comes from an unrealistic loop with nothing inside the critical section. Measure with work that looks like yours.

---

## 6. Spinning when the holder cannot run

Everything so far assumed the thread holding the lock is running on a CPU. In user space that is not guaranteed, because the scheduler can preempt the holder in the middle of the critical section (OS 05). Then every thread that spins is burning its time slice waiting for a thread that is not running. This is **lock-holder preemption**. The kernel avoids it for its own spinlocks by disabling preemption while one is held (the locking document, fetched). User code cannot.

Here are four and eight threads on two CPUs, same work, no pinning (`ns/acq`, run 1 / run 2):

| Lock | 4 threads | 8 threads | 2 threads forced onto one CPU |
|---|---|---|---|
| TAS | 532 / 515 | 899 / 654 | 143 / 143 |
| TTAS | 424 / 369 | 691 / 693 | 163 / 139 |
| TAS that yields | 219 / 204 | 184 / 186 | 105 / 98 |
| ticket | 164,082 / 160,288 | 150,949 / 149,383 | 10,030 / 25,212 |
| MCS | 373,046 / 372,977 | 399,199 / 196,329 | 182,684 / 21,978 |
| futex mutex | 323 / 322 | 297 / 258 | 111 / 107 |
| two-phase | 216 / 283 | 245 / 255 | 106 / 106 |
| pthread mutex | 173 / 166 | 248 / 265 | 99 / 98 |

Three results stand out:

- **The ticket and MCS locks collapse** by a factor of about 500 to 1,200 against the sleeping locks: roughly 150,000 to 400,000 ns per acquisition with 4 and 8 threads, against 170 to 330. Two threads on one CPU already cost the ticket lock 10,000 to 25,000 ns and MCS 22,000 to 183,000 ns. The cause is their fairness. The lock is handed to a specific waiter, in queue order. If that waiter is not on a CPU, nobody gets the lock until the scheduler runs it, and every other spinner burns its slice (a tick is 4 ms here, OS 05) in the meantime. A lock that is merely unfair (TAS, TTAS) hands the lock to whoever is running.
- **TAS and TTAS run 1.2 to 3 times slower** than at two threads, because spinners still take CPU time away from the holder.
- **The locks that give up the CPU stay flat**: the TAS that calls `sched_yield()` on each failed try (shown below), the futex mutex, the two-phase lock and the pthread mutex. On the last column, two threads on one CPU, a spinner can never see the lock freed while it holds the only CPU; here the critical section is short, so the damage is small for TAS (about 40 percent) and large only for the FIFO locks.

The yielding TAS is the test-and-set lock with the spin hint replaced by a call that gives up the CPU:

```c
static void yield_lock(lock_t *l)
{
    while (atomic_exchange_explicit(&l->w, 1, memory_order_acquire)) sched_yield();
}
```

The fix is to stop spinning when the wait may be long: yield, or sleep in the kernel. The next section builds the sleeping part.

---

## 7. The futex: sleeping in the kernel, locking in user space

A **futex** ("fast userspace mutex") is a kernel facility, not a lock. It gives user space two operations on a 32-bit word in the process's memory, and everything else is built on them (futex(2) and FUTEX_WAIT(2const), fetched):

- **`FUTEX_WAIT(addr, expected)`**: if the word at `addr` still equals `expected`, put the thread to sleep on that address. The comparison and the decision to sleep are atomic with respect to other futex operations on the word. If the word differs, return at once with `EAGAIN`.
- **`FUTEX_WAKE(addr, n)`**: wake up to `n` threads sleeping on that address and return how many were woken.

glibc does not export a `futex()` function (from memory; the man page's own example calls the system call directly), so the programs wrap `syscall(SYS_futex, ...)` in a small function. `FUTEX_PRIVATE_FLAG` tells the kernel the word is not shared with other processes, which lets it skip some work (fetched). A return of 0 from wait does not prove a real wake-up: the man page says callers must treat a 0 return as possibly spurious and re-check the word (fetched).

The program below calls the system call directly and prints what comes back:

```c
/* c4_futex.c: the futex system call by itself: what wait and wake return. */
#include "lk.h"

static atomic_int word;
static atomic_int woke;

static void *sleeper(void *arg)
{
    (void)arg;
    long r = fx_wait(&word, 0, NULL);
    if (r == 0) atomic_fetch_add(&woke, 1);
    return NULL;
}

static void show(const char *what, long r)
{
    printf("%-44s -> %ld%s%s\n", what, r, r < 0 ? " " : "", r < 0 ? strerror(errno) : "");
}

int main(void)
{
    atomic_store(&word, 1);
    long r = fx_wait(&word, 0, NULL);                      /* word is 1, caller expected 0 */
    show("wait(expect 0) while the word is 1", r);

    atomic_store(&word, 0);
    struct timespec to = { 0, 50 * 1000 * 1000 };          /* 50 ms, relative */
    double t0 = now();
    r = fx_wait(&word, 0, &to);
    char buf[80];
    snprintf(buf, sizeof buf, "wait(expect 0), 50 ms timeout (took %.0f ms)", (now() - t0) * 1e3);
    show(buf, r);

    show("wake(1) with nobody asleep", fx_wake(&word, 1));

    pthread_t t[3];
    for (int i = 0; i < 3; i++) pthread_create(&t[i], NULL, sleeper, NULL);
    usleep(100000);                                        /* give all three time to go to sleep */
    show("wake(1) with three asleep", fx_wake(&word, 1));
    usleep(50000);
    printf("%-44s    %d thread(s) returned so far\n", "", atomic_load(&woke));
    show("wake(INT_MAX) for the rest", fx_wake(&word, INT_MAX));
    for (int i = 0; i < 3; i++) pthread_join(t[i], NULL);
    printf("%-44s    %d thread(s) returned in total\n", "", atomic_load(&woke));

    /* The wake-before-wait race: the waker changes the word, then wakes. A waiter that is
       late sees the new value and does not sleep, so the wake-up cannot be lost. */
    atomic_store(&word, 1);
    fx_wake(&word, 1);
    show("wait(expect 0) after the word became 1", fx_wait(&word, 0, NULL));
    return 0;
}
```

Output:

```text
wait(expect 0) while the word is 1           -> -1 Resource temporarily unavailable
wait(expect 0), 50 ms timeout (took 50 ms)   -> -1 Connection timed out
wake(1) with nobody asleep                   -> 0
wake(1) with three asleep                    -> 1
                                                1 thread(s) returned so far
wake(INT_MAX) for the rest                   -> 2
                                                3 thread(s) returned in total
wait(expect 0) after the word became 1       -> -1 Resource temporarily unavailable
```

What each line shows:

- A wait whose expected value is stale returns `EAGAIN` immediately and does not sleep. This is why a wake-up cannot be lost: the last line is the wake-before-wait race. The waker changed the word and woke nobody (nothing was asleep yet); the late waiter finds the word changed and refuses to sleep.
- A wait with a timeout returns `ETIMEDOUT` (the text "Connection timed out" is `strerror` for that code) after the time given: 50 ms asked, 50 ms measured.
- Wake returns the number of threads actually woken: 0, then 1 of the 3 sleepers, then the remaining 2.

The kernel keeps a queue of sleepers per address (from memory: it hashes the address into a table of queues), so a futex costs nothing until someone sleeps on it. There is no kernel object to create or destroy, which is why a mutex can be a bare integer in user memory.

### 7.1 A mutex on a futex

**Attempt 1: two states.** The word is 0 (free) or 1 (held). To lock, exchange in a 1; if the old value was nonzero, sleep until it changes and try again. To unlock, set 0 and wake one sleeper.

```c
static void fa_lock(lock_t *l)
{
    while (atomic_exchange_explicit(&l->w, 1, memory_order_acquire) != 0) fx_wait(&l->w, 1, NULL);
}

static void fa_unlock(lock_t *l)
{
    atomic_store_explicit(&l->w, 0, memory_order_release);
    fx_wake(&l->w, 1);
}
```

This is correct. Its flaw is in unlock: it makes a system call every time, even when nobody is sleeping, which is nearly always. In the table of section 5.3 it costs 121 to 126 ns per pair uncontended, against 13 to 14 for the next version.

**Attempt 2: three states** (Drepper's design, from memory; checked here only by the tests). The word is 0 free, 1 held with nobody asleep, 2 held and somebody may be asleep.

```c
static void fm_lock(lock_t *l)
{
    int c = 0;
    if (atomic_compare_exchange_strong_explicit(&l->w, &c, 1,
                                                memory_order_acquire, memory_order_relaxed))
        return;                                       /* fast path: no system call */
    if (c != 2) c = atomic_exchange_explicit(&l->w, 2, memory_order_acquire);
    while (c != 0) {
        fx_wait(&l->w, 2, NULL);                          /* sleep only while the word is still 2 */
        c = atomic_exchange_explicit(&l->w, 2, memory_order_acquire);
    }
}

static void fm_unlock(lock_t *l)
{
    if (atomic_fetch_sub_explicit(&l->w, 1, memory_order_release) != 1) {
        atomic_store_explicit(&l->w, 0, memory_order_release);
        fx_wake(&l->w, 1);                            /* only when someone may be asleep */
    }
}
```

Read it as follows. The fast path in lock is one compare-and-swap from 0 to 1: no system call. If that fails, the thread marks the lock "someone is waiting" by exchanging in a 2, and sleeps only while the word is still 2; after every wake-up it exchanges in a 2 again, because it cannot know whether other sleepers remain. Unlock subtracts 1: if the old value was 1 (held, no sleepers), the unlock is done, with no system call; otherwise it sets 0 and wakes one sleeper. So a lock that is never contended never enters the kernel.

`pb` counts the system calls made by this mutex with 1,000,000 lock and unlock pairs in total:

```text
threads 1  cs_work 0     pairs 1000000  futex wait calls 0  wake calls 0
threads 2  cs_work 0     pairs 1000000  futex wait calls 46063  wake calls 86951
threads 2  cs_work 200   pairs 1000000  futex wait calls 65730  wake calls 81668
threads 2  cs_work 2000  pairs 1000000  futex wait calls 194884  wake calls 195487
```

One thread: no system calls at all. Two threads contending: tens of thousands of waits (hundreds of thousands with a longer critical section), and with a short one **more wakes than waits** (86,951 against 46,063 in the second line). The second look at the code explains it. A thread woken from sleep exchanges a 2 into the word. When it unlocks it sees 2, and wakes someone, even if nobody is asleep any more. The three-state mutex cannot tell "one sleeper left" from "none", so it errs towards a useless wake. A wake with nobody to wake is cheap compared with a wait, but it is still a system call. With the long critical section (the last line, 2,000 loop iterations) the two counts are almost equal, because nearly every unlock does find a sleeper.

**Does the pthread mutex use futexes?** Yes. Running the benchmark under `strace -f -c -e trace=futex`:

```text
$ strace -f -c -e trace=futex bin/c3_bench pthread 2 1 50 200 1
pthread      thr=2     4.295 Macq/s       233 ns/acq  share min/max  20%/ 80%  futex wait/wake 0/0  count ok
% time     seconds  usecs/call     calls    errors syscall
------ ----------- ----------- --------- --------- ----------------
100.00    0.183765           5     32410     12329 futex
------ ----------- ----------- --------- --------- ----------------
100.00    0.183765           5     32410     12329 total
```

about 32,000 futex calls in one second, about 12,000 of them returning an error (`EAGAIN`, the "value already changed" case). The benchmark's own counters read 0/0 because they count only calls made through its wrapper, not glibc's. So glibc's `pthread_mutex_lock` on this machine is a futex lock: contended waits and wake-ups go through `FUTEX_WAIT` and `FUTEX_WAKE`, and the zero-futex-call benchmark rows in section 5.3 show that an uncontended lock stays in user space. The same three states as above are likely (from memory). (`strace` slows the process a lot, so the count says that futex calls happen, not how many there are without it.) glibc's source was not read for this chapter; the claim rests on the trace. Some other mutex types also exist: recursive, error-checking, and an adaptive type that spins briefly before sleeping (the last from memory).

---

## 8. Two-phase locks: spin first, then sleep

The futex mutex sleeps at the first sign of contention, which costs two system calls and two context switches even when the holder would have released in 100 ns. A **two-phase lock** spins for a bounded time first, then sleeps. OSTEP (fetched) describes the Linux futex lock as the first step in that direction: it spins once. The version here spins `tp_spin` times and then falls back to the three-state mutex above:

```c
static void tp_lock(lock_t *l)
{
    for (int i = 0; i < tp_spin; i++) {
        int c = 0;
        if (atomic_load_explicit(&l->w, memory_order_relaxed) == 0 &&
            atomic_compare_exchange_strong_explicit(&l->w, &c, 1,
                                                    memory_order_acquire, memory_order_relaxed))
            return;
        relax();
    }
    fm_lock(l);
}
```

How long should the spin be? Measured on two CPUs with two threads; with a short critical section (50 loop iterations, about 20 ns) and then a longer one (5,000, about 1.3 microseconds), time per acquisition and futex waits, run 1 / run 2:

| Spin tries | Short CS: ns per acq | futex waits | Long CS: ns per acq | futex waits |
|---|---|---|---|---|
| 0 | 339 / 328 | 369,246 / 362,494 | 3,679 / 3,636 | 84,026 / 85,538 |
| 10 | 311 / 222 | 256,056 / 26,420 | not run | not run |
| 100 | 285 / 229 | 368 / 381 | 3,395 / 3,298 | 92,317 / 102,099 |
| 1,000 | 284 / 271 | 254 / 248 | 1,651 / 1,707 | 3,134 / 2,690 |
| 10,000 | 283 / 280 | 0 / 6 | 1,651 / 1,608 | 128 / 44 |
| 100,000 | not run | not run | 1,692 / 1,652 | 39 / 3 |

Reading it:

- With the **short critical section**, even 100 spin tries remove almost every sleep (hundreds of thousands of waits become a few hundred). Time per acquisition improves only modestly, 15 to 30 percent, because the sleep path was a minority of the cost.
- With the **long critical section** (about 1.3 microseconds), 100 tries are not enough: the spinner gives up and sleeps almost as often as with no spin, and the time per acquisition stays at about 3,300 to 3,700 ns. A spin of 1,000 or more halves it to about 1,650 ns, because the spinner now outlasts the holder. Beyond that nothing improves; a longer spin only adds risk if the holder is preempted.
- The right spin time depends on how long the lock is held. A rule of thumb with a theoretical backing (from memory: Karlin and others showed that spinning for about the cost of a block and wake-up never costs more than twice the best choice in hindsight): spin for roughly the time a sleep and wake would cost. The kernel mutex does a version of this, called optimistic spinning: it spins while the owner is running on a CPU and goes to sleep as soon as the owner is not (the mutex design document, fetched).

Four threads on two CPUs (short critical section) gave a similar picture: 100 or more spin tries cut futex waits from about 340,000 to about 1,000, and the throughput rose 10 to 30 percent.

---

## 9. Priority inversion, other lock kinds, and the kernel's lock families

### 9.1 Priority inversion and priority-inheritance futexes

A low-priority thread holds a lock; a high-priority thread blocks on it; a medium-priority thread that does not need the lock preempts the low one. The high-priority thread now waits for the medium one: priority inverted (a Mars probe famously hit this in 1997; from memory). The cure is **priority inheritance**: the holder runs at the priority of its highest waiter until it unlocks. Linux implements this in the futex layer with PI futexes: the word holds 0 when free, the owner's thread id when held, and a waiters bit under contention, and the kernel updates the word itself (futex(2), fetched). The operation is `FUTEX_LOCK_PI`; a program selects it by creating the mutex with `PTHREAD_PRIO_INHERIT` (from memory). It is the "priority-inheritance mutex" OS 05 recommended for `SCHED_FIFO` code, and OS 07 treats the problem in full.

### 9.2 Reader-writer locks

Many threads may read at once; a writer needs exclusive access. The design question is who waits for whom. If readers always get in while others read, a writer can starve. The kernel's `rwlock_t` is a fair spinning lock on ordinary kernels, so writers do not starve; on PREEMPT_RT it is built on an `rt_mutex`, and a low-priority reader holding it can keep out a high-priority writer (kernel locking document, fetched). For read-mostly data the kernel prefers sequence locks and RCU, which let readers run with no atomic operation at all (from memory; the later kernel chapters take them up).

### 9.3 The kernel's lock families

The kernel's locking document (fetched) sorts locks into three families, and the rule that follows from them is the best summary of this chapter:

| Family | Members | Behaviour |
|---|---|---|
| sleeping | `mutex`, `rt_mutex`, `semaphore`, `rw_semaphore`, `ww_mutex` | block the task; only in preemptible task context |
| CPU-local | `local_lock` | disables preemption or interrupts on this CPU |
| spinning | `raw_spinlock_t`, bit spinlocks; `spinlock_t` and `rwlock_t` on non-RT kernels | busy-wait, with preemption disabled while held |

Nesting is allowed from outside to inside in this order: sleeping locks, then `spinlock_t`/`rwlock_t`/`local_lock`, then `raw_spinlock_t` and bit spinlocks. A sleeping lock can never be taken while holding a spinning one, because sleeping with preemption off would stall the CPU. On a PREEMPT_RT kernel `spinlock_t` is rebuilt on `rt_mutex`: it sleeps, stays preemptible, and does not disable interrupts, while `raw_spinlock_t` stays a true spinlock for the few places that need one (same document). The tool `lockdep` reports violations of these rules; the deadlocks that lock ordering prevents are OS 09.

---

## 10. Locked data structures

A lock protects data; how finely you divide the data decides how much waiting you get and how much locking you pay for. Three structures, each with a coarse version and a fine one. Everything runs with threads pinned to CPUs 0 and 1.

### 10.1 A counter, exact and approximate

The exact counter takes the lock on every increment. The **approximate counter** keeps a count per thread and adds it to the shared total only every `S` increments:

```c
static void counter_add(void)
{
    if (variant[0] == 'e') {                      /* exact: lock on every increment */
        pthread_mutex_lock(&cmu);
        global_count++;
        pthread_mutex_unlock(&cmu);
    } else if (++local_count >= threshold) {      /* approximate: lock once per THRESHOLD */
        pthread_mutex_lock(&cmu);
        global_count += local_count;
        pthread_mutex_unlock(&cmu);
        local_count = 0;
    }
}
```

`S` is the threshold. A read of the global value can be low by at most the unflushed counts: `threads * (S - 1)`. The program counted 1,000,003 increments per thread on two threads (a prime-like count so some are left unflushed at the end), and reports the throughput, the value of the shared total and the shortfall:

```text
counter  approx  threads 2     17.62 Mops/s  global read   2000006 of   2000006  (off by 0, bound 0)
counter  approx  threads 2     67.67 Mops/s  global read   2000000 of   2000006  (off by 6, bound 6)
counter  approx  threads 2    111.44 Mops/s  global read   2000000 of   2000006  (off by 6, bound 30)
counter  approx  threads 2    238.44 Mops/s  global read   2000000 of   2000006  (off by 6, bound 126)
counter  approx  threads 2   1024.77 Mops/s  global read   1998848 of   2000006  (off by 1158, bound 2046)
counter  exact   threads 1     76.69 Mops/s  global read   1000003 of   1000003  (off by 0)
counter  exact   threads 2     21.08 Mops/s  global read   2000006 of   2000006  (off by 0)
```

Reading the table:

- **The exact counter does not scale.** One thread: 76.7 Mops/s, about 13 ns per increment. Two threads: 21.1 Mops/s in total, so two threads got less than 30 percent of one thread's throughput. This is contention: the lock word and the counter bounce between two CPUs' caches.
- **The threshold buys throughput:** about 18 Mops/s at `S` = 1 (about the same as exact), 68 at 4, 111 at 16, 238 at 64, and over 1,000 at 1,024 (each thread doing about 500 M increments per second, 2 ns each, with one lock operation per 1,024).
- **The error stays within the bound:** shortfall 6 for `S` = 4, 16 and 64 (each thread had 3 increments unflushed), 1,158 for `S` = 1,024 against a bound of 2,046.

Accuracy against speed is the whole trade, and a program picks `S` from the error it can tolerate (Problem D). The Linux kernel's per-CPU counters work this way (from memory: `percpu_counter` batches per-CPU counts into a shared total).

### 10.2 A list: one lock or one lock per node

A sorted list of 64 nodes; each operation finds a key and increments its value. The coarse version takes one lock for the whole walk. The **hand-over-hand** version (lock coupling) locks the next node before releasing the current one, so at most two node locks are held and different threads can be in different parts of the list:

```c
static void list_bump(long key)
{
    if (variant[0] == 'c') {
        pthread_mutex_lock(&list_mu);
        node *n = head;
        while (n->key != key) n = n->next;
        n->val++;
        pthread_mutex_unlock(&list_mu);
    } else {                                      /* hand over hand: hold at most two node locks */
        node *cur = head;
        pthread_mutex_lock(&cur->mu);
        while (cur->key != key) {
            node *nx = cur->next;
            pthread_mutex_lock(&nx->mu);
            pthread_mutex_unlock(&cur->mu);
            cur = nx;
        }
        cur->val++;
        pthread_mutex_unlock(&cur->mu);
    }
}
```

Measured (Mops/s; every run checks that no update was lost):

```text
list     coarse  threads 1     17.36 Mops/s  updates 200000 of 200000 ok
list     hoh     threads 1      2.39 Mops/s  updates 200000 of 200000 ok
list     coarse  threads 2      3.80 Mops/s  updates 400000 of 400000 ok
list     hoh     threads 2      0.42 Mops/s  updates 400000 of 400000 ok
hash     coarse  threads 1     22.08 Mops/s  updates 1000000 of 1000000 ok
hash     bucket  threads 1     17.17 Mops/s  updates 1000000 of 1000000 ok
hash     coarse  threads 2      3.14 Mops/s  updates 2000000 of 2000000 ok
hash     bucket  threads 2     15.06 Mops/s  updates 2000000 of 2000000 ok
hash     coarse  threads 1     21.96 Mops/s  updates 1000000 of 1000000 ok
hash     bucket  threads 1     21.76 Mops/s  updates 1000000 of 1000000 ok
hash     coarse  threads 2      9.46 Mops/s  updates 2000000 of 2000000 ok
hash     bucket  threads 2     42.02 Mops/s  updates 2000000 of 2000000 ok
```

The first four lines are the list: with one thread the coarse list runs about 17 Mops/s and hand-over-hand about 2.4, so **the fine-grained version is 7 times slower** with nobody to compete with. With two threads, coarse falls to 3.8 and hand-over-hand to 0.3 to 0.4 (earlier runs of the same program gave 0.24 and 0.28; the final run 0.42): still 9 times slower than coarse. A lookup walks 32 nodes on average, so hand-over-hand does about 32 lock and unlock pairs at 13 ns each, against about 2 ns of real work per node; the lock costs more than the thing it protects. Hand-over-hand pays off when the work per node is large, or when threads work in distant parts of a long list (OSTEP makes the same point; from memory). A finer lock is not a faster lock.

### 10.3 A hash table: one lock per bucket

The last four lines of the output above are the hash table (256 buckets, 4,096 keys). **Coarse** uses one mutex; **bucket** one mutex per bucket:

```c
static void hash_bump(long key)
{
    int b = (int)(key % B);
    pthread_mutex_t *m = variant[0] == 'c' ? &hash_mu : &bucket_mu[b];
    pthread_mutex_lock(m);
    hnode *n = bucket[b];
    while (n->key != key) n = n->next;
    n->val++;
    pthread_mutex_unlock(m);
}
```

The last argument of the program is `PARTITION`. At 0 every thread uses random keys from the whole table. At 1 thread 0 only touches buckets 0 to 127 and thread 1 only buckets 128 to 255, so the two threads never share a lock, a bucket or a node. The lines in the output come in the order: partition 0 (coarse and bucket for 1 thread, then for 2), then partition 1 (same order). Summary in Mops/s:

| Case | 1 thread | 2 threads |
|---|---|---|
| coarse lock, shared keys | 22.1 | 3.1 |
| per-bucket locks, shared keys | 17.2 | 15.1 |
| coarse lock, partitioned keys | 22.0 | 9.5 |
| per-bucket locks, partitioned keys | 21.8 | 42.0 |

The first row is the cost of contention: two threads run 7 times slower than one. Per-bucket locks help (5 times better than coarse) but two threads are still slower than one thread, because both threads still write the same nodes and the same lock lines, and every write pulls the line from the other CPU. The last row is the only one that scales: 42.0 against 21.8, a factor of 1.9. The coarse lock with partitioned keys (9.5) shows that the lock alone serialises the threads even when their data is separate.

**False sharing.** The delivered program allocates every node on its own 64-byte cache line (`aligned_alloc(64, 64)`) and the bucket head array is aligned. An earlier version of the program used 32-byte nodes from `calloc` and split keys by even and odd instead of by bucket range, and two threads reached only about 13 Mops/s even with per-bucket locks padded to a cache line each. Two nodes per line meant each thread's writes invalidated the other thread's line even though they never touched the same node. That is **false sharing**: logically separate data on one cache line. It was fixed by changing the layout, which is how it was diagnosed; this machine has no `perf`, so no cache-miss counter confirmed it. The rule for scalable locked structures is two parts, and both matter: split the lock, and keep each lock's data off other locks' cache lines.

---

## What it costs

| Mechanism | Measured effect here | Section |
|---|---|---|
| Plain `counter++` on 2 threads | lost about half of 10 million updates | 1.1 |
| Atomic increment under 2-thread contention | about 30 ns, against about 1 ns for a plain one | 1.1 |
| Peterson without a full fence, 4 million entries | hundreds to thousands of overlaps; zero with a fence | 3.1 |
| Uncontended lock and unlock (spinlock, futex mutex) | 7 to 14 ns | 5.3 |
| Futex mutex that wakes on every unlock, uncontended | about 125 ns (9 times the three-state mutex) | 5.3, 7.1 |
| Two threads contending on two CPUs, any lock | about 260 to 430 ns per acquisition | 5.3 |
| FIFO spinlock (ticket, MCS) with 4 or 8 threads on 2 CPUs | 150,000 to 400,000 ns per acquisition | 6 |
| Futex mutex or pthread mutex in the same case | 170 to 330 ns per acquisition | 6 |
| Two-phase lock, spin 1,000 or more, long critical section | 1,650 ns against 3,300 to 3,700 ns with 0 or 100 tries | 8 |
| Exact shared counter, 2 threads against 1 | 21 against 77 Mops/s | 10.1 |
| Approximate counter, `S` = 64 and 1,024 | 238 and over 1,000 Mops/s | 10.1 |
| Hand-over-hand list against one lock, 64 nodes | 7 to 9 times slower | 10.2 |
| Per-bucket hash table, partitioned keys, aligned nodes | 42.0 Mops/s, 1.9 times one thread | 10.3 |

---

## Common mistakes

1. **Using a pure spinlock in user space without thinking about preemption.** The holder can be descheduled while you spin; a FIFO spinlock then collapses (section 6).
2. **Using `volatile` as synchronisation.** It keeps the compiler from caching a variable; it provides no atomicity and no ordering between CPUs. Section 1.1's plain counter lost half its updates with `volatile`.
3. **Writing Peterson's lock with ordinary stores and loads, or with only release and acquire.** It needs a full fence between the stores and the loads (section 3.1).
4. **Calling `FUTEX_WAIT` without a loop that rechecks the condition.** A return of 0 may be spurious, and another thread may have taken the lock first.
5. **Unlocking by always calling `FUTEX_WAKE`.** Correct, 9 times slower uncontended (section 7.1). Keep a state that says whether anyone sleeps.
6. **Forgetting that the three-state mutex needs the "2" after a wake-up.** A woken thread cannot tell whether other sleepers remain, so it must set the state to 2 before it takes the lock.
7. **Assuming test-and-test-and-set is fair.** It is not; waiters can starve. The ticket lock is fair, and brittle when threads outnumber CPUs.
8. **Taking a finer lock for a faster one.** Hand-over-hand was 7 to 9 times slower than one lock for 64 nodes (section 10.2).
9. **Splitting a lock and leaving the data on shared cache lines.** The per-bucket table scaled only after nodes got their own lines (section 10.3).
10. **Benchmarking with an empty critical section.** The mutexes beat the ticket lock by 4 to 7 times for the wrong reason: they let one thread run on (section 5.3).
11. **Holding a lock across a blocking call or I/O.** Every other thread then waits for the slowest operation; with a spinlock they also burn CPU.
12. **Taking locks in different orders in different places.** Deadlock; pick one global order (OS 09).
13. **Spinning on a single CPU.** If the holder cannot run until you stop, spinning cannot help (section 6, last column).
14. **Putting two hot locks (or a lock and its data you modify rarely from another thread) on one cache line.** Align contended locks to 64 bytes.

---

## Questions out loud

1. **What are the three requirements of a critical-section solution?** Mutual exclusion, progress, bounded waiting.
2. **Why does `counter++` lose updates?** Load, add, store are separate instructions; two CPUs can both load the old value. An atomic read-modify-write (`lock addq`) is one indivisible step. Measured: about half of 10 million updates lost.
3. **Why can't a lock just disable interrupts?** User code cannot; it does nothing about other CPUs; interrupts can be lost. The kernel disables local interrupts to protect against its own handlers, and uses spinlocks against other CPUs.
4. **Explain Peterson's algorithm and why it fails on real hardware.** Flags plus a turn variable, correct under sequential consistency. On x86 a store can sit in the store buffer past a later load, so both threads can read the other's flag as clear; a full fence after the stores fixes it (measured: overlaps without, none with).
5. **What are test-and-set, compare-and-swap and fetch-and-add?** Atomic read-modify-write instructions; TAS stores and returns the old value, CAS stores only if the word matches, fetch-and-add adds.
6. **TAS against TTAS?** TAS writes on every try, so the line ping-pongs; TTAS spins on a read and writes only when the lock looks free. Neither is fair.
7. **Why is the ticket lock fair, and what is its weakness?** Strict first come, first served. All waiters spin on one word (a release invalidates all copies), and a descheduled next-in-line stalls everyone.
8. **What does MCS add?** Each waiter spins on its own node, so a handoff touches one cache line however many wait. Linux's `qspinlock` is MCS packed into 32 bits.
9. **What is lock-holder preemption?** A thread is descheduled while holding a lock (or, for FIFO locks, while next in line); others spin uselessly. Measured: ticket and MCS were 500 to 1,200 times slower than the sleeping locks with 4 threads on 2 CPUs.
10. **When spin and when sleep?** Spin when the wait is shorter than a sleep and wake and the holder is running; sleep otherwise. A two-phase lock spins for about the cost of a sleep and then sleeps.
11. **What is a futex?** A kernel facility with two operations on a user word: wait (sleep if the word still has the expected value, atomically) and wake (wake up to n sleepers). A lock's fast path stays in user space.
12. **Build a mutex from a futex.** The three-state mutex: 0 free, 1 locked, 2 locked with possible sleepers; compare-and-swap 0 to 1 to lock; unlock subtracts 1 and wakes only if the old value was not 1.
13. **Why can't a wake-up be lost in the futex design?** The wait compares the word under the kernel's lock; if a waker changed it first, the wait returns `EAGAIN` and the thread retries.
14. **What is a PI futex?** A futex whose word holds the owner's thread id; the kernel boosts the owner to the highest waiter's priority (priority inheritance).
15. **How do you make a shared counter scale?** Per-thread counts flushed every `S` increments (approximate counter); the read error is bounded by `threads * (S - 1)`.
16. **Is a finer-grained lock always faster?** No: hand-over-hand cost 7 to 9 times a single lock for a 64-node list, and a per-bucket table did not scale until false sharing was removed.
17. **What is false sharing?** Logically separate data on one cache line, so writes by different CPUs invalidate each other's copy.
18. **What are the kernel's lock families, and what changes on PREEMPT_RT?** Sleeping, CPU-local, spinning; `spinlock_t` becomes a sleeping `rt_mutex`-based lock, `raw_spinlock_t` stays spinning.

---

## Problems

**Where to solve them.** On any Linux machine with a C compiler and at least 2 CPUs. A and D are mostly paper; B and C use programs shown in the answers (`pb`, `pc`, built from the code in this chapter, with `-lpthread`); E needs the benchmark `c3_bench` from section 5.2 and its lock table. Numbers will differ on your machine; the ratios should not. The programs need an x86-64 or arm64 Linux with GCC or Clang.

**How to approach them.** Predict first. For A, write the counters after every step. For B, count the paths through the lock code that reach the kernel. For C, think about which accesses the CPU may reorder. For D, bound the error, then pick `S`. For E, write down what each lock does when the holder is descheduled before you look at the table.

### Problem A (E, entry level): trace a ticket lock

A ticket lock starts with `next = 0` and `owner = 0`. Threads T1, T2, T3 and T4 call `lock` in that order, one at a time with no overlap in the calls, and then each in turn calls `unlock` after it gets in. What ticket does each thread get, what are `next` and `owner` at the end, and in what order do they enter? Then say whether a TAS lock under the same arrival order would give the same order.

### Problem B (M, medium): count the system calls

How many `futex` system calls does the three-state mutex make for 1,000,000 uncontended lock and unlock pairs in one thread? How many does the two-state "wake on every unlock" mutex make? Then run `pb` with two threads and critical-section lengths 0, 200 and 2,000 iterations, and explain why the number of wakes is larger than the number of waits for the short ones and nearly equal for the long one.

### Problem C (M, medium): where does the fence go?

Peterson's lock needs a full fence somewhere in its entry code. Try it before both stores, between them and after both. Which placements work in 4 million entries, and why?

### Problem D (H, hard): size an approximate counter

Two threads count events with an approximate counter. A monitoring thread reads the shared total and needs it within 0.1 percent of the true count whenever the true count is at least 1,000,000. Pick `S`. How many times fewer lock acquisitions does it make than the exact counter? Which row of the measured table does it correspond to?

### Problem E (H, hard): choose the lock

A service has 4 worker threads on a 2-CPU machine. Each worker holds a lock for about 20 ns, then works about 60 ns without it (the benchmark's 50 and 200 loop iterations, calibrated in section 5.2). Choose among TAS, TTAS, ticket, MCS, the futex mutex, the pthread mutex and a two-phase lock using the measured tables, and say what changes if the critical section sometimes does a disk read.

---

## Answers

**A.** Tickets come from `fetch_add`: T1 gets 0, T2 gets 1, T3 gets 2 and T4 gets 3, and `next` ends at 4. T1 enters at once because `owner` is 0. Each unlock increments `owner`, so `owner` becomes 1 and T2 enters, then 2 (T3), then 3 (T4), and after T4's unlock `owner` is 4, equal to `next`: the lock is free. Entry order is exactly arrival order. A TAS lock gives no such guarantee: whichever thread's exchange runs first after an unlock wins, regardless of arrival order, so the order can be anything and a thread can lose repeatedly.

**B.** The three-state mutex's fast path is one compare-and-swap from 0 to 1 in lock and one subtraction in unlock; neither calls the kernel when nobody contends, so the count is **0**. The two-state mutex calls `FUTEX_WAKE` on every unlock, so its count is **1,000,000 wakes** (and 0 waits). The program (the worker is shown) confirms the first and the section 5.3 run shows the second (3.2 and 3.3 million wake calls in 0.4 s runs):

```c
static void *worker(void *arg)
{
    pin((int)(long)arg % 2);
    for (long i = 0; i < per_thread; i++) {
        fm_lock(&L);
        shared++;
        for (volatile int k = 0; k < cs_work; k++) { }
        fm_unlock(&L);
    }
    return NULL;
}
```

```text
threads 1  cs_work 0     pairs 1000000  futex wait calls 0  wake calls 0
threads 2  cs_work 0     pairs 1000000  futex wait calls 46063  wake calls 86951
threads 2  cs_work 200   pairs 1000000  futex wait calls 65730  wake calls 81668
threads 2  cs_work 2000  pairs 1000000  futex wait calls 194884  wake calls 195487
```

With a short critical section there are about twice as many wakes as waits: after a woken thread sets the state to 2, its own unlock sees 2 and wakes someone although nobody may be asleep. With 2,000 iterations inside, the other thread really is asleep almost every time the lock is released, so the two counts nearly agree (194,884 waits, 195,487 wakes).

**C.** The program (the lock entry is shown):

```c
static void p_lock(int me)
{
    int other = 1 - me;
    if (where == 0) fence();                                     /* before both stores */
    atomic_store_explicit(&flag[me], 1, memory_order_release);
    if (where == 1) fence();                                     /* between the two stores */
    atomic_store_explicit(&turn, other, memory_order_release);
    if (where == 2) fence();                                     /* after both stores */
    while (atomic_load_explicit(&flag[other], memory_order_acquire) &&
           atomic_load_explicit(&turn, memory_order_acquire) == other) relax();
}
```

Run 1:

```text
fence before both stores   lost updates      2  overlaps     10
fence between the stores   lost updates      0  overlaps      1
fence after both stores    lost updates      0  overlaps      0
```

Run 2:

```text
fence before both stores   lost updates      2  overlaps     13
fence between the stores   lost updates      0  overlaps      0
fence after both stores    lost updates      0  overlaps      0
```

A fence **before** both stores does nothing useful: a fence orders accesses that come before it against accesses that come after, and the stores that matter come after it. They can still sit in the store buffer while the loads run, which is the failing pattern of section 3.1. A fence **between** the two stores forces the flag to become visible before the turn store, but the turn store itself can still be buffered while this thread's load of the other flag proceeds; so a thread can read the other's flag as clear while its own turn store has not yet drained, and both enter. That is rare (1 overlap in 4 million in run 1, none in run 2) and the rarity is the danger: a test can pass. A fence **after both stores** forces both to become visible before either load, and 8 million entries over the two runs showed no overlap. This is evidence, not proof; the argument is the proof.

**D.** The error bound is `threads * (S - 1)`. The true count is at least 1,000,000, so 0.1 percent is 1,000: `2 * (S - 1) <= 1000`, so `S <= 501`. Take a power of two below it, `S` = 256: the shortfall is at most 510, or 0.051 percent at the smallest count. Each thread then takes the lock once per 256 increments, **256 times fewer** acquisitions than the exact counter (3,906 instead of 1,000,000 per million increments per thread). The measured rows nearest to it are `S` = 64 (238 Mops/s) and `S` = 1,024 (over 1,000 Mops/s, bound 2,046, which would break the requirement), so expect a few hundred Mops/s. If readers need an exact answer, they must take every thread's local count under a lock, which puts the cost back on the readers.

**E.** The second table of section 6 (4 threads on 2 CPUs, run 1 / run 2, ns per acquisition) is the relevant one:

| Lock | ns per acquisition |
|---|---|
| ticket | 164,082 / 160,288 |
| MCS | 373,046 / 372,977 |
| TAS | 532 / 515 |
| TTAS | 424 / 369 |
| futex mutex | 323 / 322 |
| two-phase | 216 / 283 |
| pthread mutex | 173 / 166 |

Reject ticket and MCS: with more threads than CPUs their fairness makes them wait for a descheduled thread (150,000 to 370,000 ns). Reject TAS and TTAS: they work, but burn CPU that the holder needs and are 2 to 3 times slower than the pthread mutex. The TAS that yields on each failed try does well here too (219 / 204 ns) but depends on how `sched_yield` behaves under the scheduler, which OS 05 shows is not something to lean on. Of the sleeping locks the pthread mutex was fastest in both runs (173 and 166 ns), the two-phase lock next and the hand-built futex mutex last; take the **pthread mutex**: it is the one that is already written, tested and tuned (adaptive variants exist), and it needs no spin-time guess. A tuned two-phase lock is the alternative if profiling shows many short waits. If the critical section sometimes reads from disk, **never spin**: a waiter would burn its whole slice during a millisecond-scale wait, so use a sleeping lock (the futex or pthread mutex) and consider keeping I/O outside the lock altogether.

---

## Cheat sheet

| Item | Rule |
|---|---|
| Critical section requirements | mutual exclusion, progress, bounded waiting |
| Lock yardsticks | correctness, fairness, performance (uncontended, one CPU, many CPUs) |
| Disable interrupts | privileged, useless across CPUs; kernel uses it only against local handlers |
| Peterson | flag per thread plus turn; needs a full fence after the stores on x86 |
| Bakery | ticket numbers for N threads, `N` loads per entry, no atomic read-modify-write |
| TAS | exchange in a loop; every try writes; unfair |
| TTAS | spin on a read, exchange when free; unfair |
| Ticket | `fetch_add` for a ticket, serve `owner`; fair; all spin on one word |
| MCS | queue, each waiter spins on its own node; fair; one transfer per handoff |
| Lock-holder preemption | FIFO spinlocks collapse when threads outnumber CPUs (500 to 1,200 times slower, measured) |
| Futex | `WAIT(addr, expected)` sleeps if the word still equals `expected`; `WAKE(addr, n)` wakes up to `n` |
| Futex mutex | states 0, 1, 2; CAS 0 to 1; unlock subtracts 1, wakes only if the old value was not 1 |
| System calls | none uncontended; more wakes than waits is normal for the three-state mutex |
| Two-phase lock | spin about the cost of a sleep and wake, then sleep (1,000 tries beat 100 for a microsecond-scale critical section) |
| PI futex | word holds owner id; `PTHREAD_PRIO_INHERIT`; fixes priority inversion |
| Approximate counter | per-thread counts, flush every `S`; error at most `threads * (S - 1)` |
| Fine-grained locks | not automatically faster: hand-over-hand cost 7 to 9 times a single lock |
| False sharing | separate data on one cache line; align contended data to 64 bytes |
| Kernel locks | sleeping, CPU-local, spinning; nesting from sleeping to spinning only; RT turns `spinlock_t` into a sleeping lock |

---

## Sources

- [futex(2), Linux man pages](https://man7.org/linux/man-pages/man2/futex.2.html): overview, atomic check-and-sleep, `FUTEX_PRIVATE_FLAG`, PI futexes (fetched).
- [FUTEX_WAIT(2const)](https://man7.org/linux/man-pages/man2/FUTEX_WAIT.2const.html): return values, `EAGAIN`, `EINTR`, `ETIMEDOUT`, spurious wake-ups, atomicity (fetched).
- [OSTEP, Locks](https://pages.cs.wisc.edu/~remzi/OSTEP/threads-locks.pdf): the three criteria, interrupts, test-and-set, compare-and-swap, ticket lock, futex lock, two-phase lock, the Peterson note (fetched).
- [Lock types and their rules, kernel documentation](https://www.kernel.org/doc/html/latest/locking/locktypes.html): sleeping, CPU-local and spinning locks, PREEMPT_RT behaviour, nesting order (fetched).
- [Mutex design, kernel documentation](https://www.kernel.org/doc/html/latest/locking/mutex-design.html): fast path, optimistic spinning, slow path, owner field (fetched).
- [LWN: queued spinlocks](https://lwn.net/Articles/590243/): why ticket locks gave way to a queued design, the 32-bit lock word (fetched).
- [Drepper, "Futexes Are Tricky"](https://akkadia.org/drepper/futex.pdf): the source of the three-state mutex; not fetched (the site refuses automated readers), so that design is from memory and verified by the tests in this chapter.
