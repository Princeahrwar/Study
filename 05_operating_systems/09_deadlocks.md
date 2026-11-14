# OS 09: Deadlocks

*Part of the [Operating systems plan](00_plan.md) · Time: about 7 hours (4.5 reading and experiments, 2.5 problems) · You need: [OS 06](06_locks.md) (locks and the futex), [OS 07](07_condition_variables_and_semaphores.md) (waiting, and the philosophers' deadlock) and [OS 08](08_memory_barriers.md) (only for the tools section)*

A deadlock is a program that stops without crashing: every thread is waiting, none is running, and nothing will ever wake them. Interviewers ask about it in a fixed set of forms. State the four conditions and say which one each fix removes. Draw the wait-for graph and say when a cycle means deadlock and when it does not. Write a transfer function that cannot deadlock (and say what happens when the two accounts are the same one). Run the banker's algorithm on a table. Explain the difference between a deadlock, a livelock and starvation. How would you find a deadlock in a hung production process? How does the Linux kernel find deadlocks that never happened? This chapter answers each with programs that were compiled and run, including the deadlocks themselves, inspected from outside with the tools you would use on a real hang.

By the end you can:

- state the four necessary conditions (mutual exclusion, hold and wait, no preemption, circular wait), name the fix that attacks each, and say what each fix costs (measured: a single global lock was about twice as fast as two ordered locks in a short-critical-section benchmark on 2 CPUs, and ordering by lock address cost the same as ordering by account number);
- reproduce the classic two-lock deadlock, show how narrow the window can be (measured: 0 to 5 deadlocks in 1,000 to 2,000 rounds with no pause between the two locks, and a deadlock in every round with a 1 microsecond pause), and say why a test suite can pass a program that deadlocks in production;
- inspect a hung process with `/proc` and `gdb` and read the two blocked threads from the backtraces;
- build a resource-allocation or wait-for graph, find the cycle with a depth-first search, and say why a cycle proves deadlock only when every resource has one instance;
- run the safety algorithm and the resource-request algorithm of the banker's algorithm by hand and in code, on the textbook example (safe sequence P1 P3 P4 P0 P2; one request refused as unsafe even though the resources were free);
- run deadlock detection for resources with several instances, and name the recovery options;
- write a lock-order checker that reports a deadlock which never happened (a short version is in section 8.3), and explain how the kernel's lockdep does the same with lock classes;
- name the deadlocks that are not two mutexes in opposite order (self-deadlock, a lock held across `fork`, a thread joining itself, an interrupt handler taking a lock the interrupted code holds) and show the first three failing and being fixed.

**Where the numbers come from.** Programs were compiled with GCC 13.3 (`-O2 -Wall -Wextra`, `-lpthread`) and run on a Linux 6.18.44 kernel in a Firecracker micro-VM with 2 vCPUs (Intel Xeon at 2.30 GHz), glibc 2.39, with gdb, Valgrind's Helgrind and GCC's ThreadSanitizer for the tools section. The four conditions, the prevention techniques and the deadlock-bug counts are from the OSTEP chapter on concurrency bugs; the lockdep description is from the kernel's `lockdep-design` document (both fetched; see Sources). The banker's example is the textbook one (five processes, three resource types), recomputed by the program below. **This kernel is built without lockdep** (`CONFIG_PROVE_LOCKING is not set`, and `/proc/lockdep_stats` does not exist), so section 8 shows the kernel's checker from its documentation and shows its idea with a user-space checker I wrote, not with the real thing. Facts from general knowledge are marked "from memory".

**Data and timing.** Counts that follow from the logic (safe sequences, cycles, reports) are deterministic and shown once. Timings and deadlock counts were measured twice and are shown as run 1 / run 2. Deadlocks are detected by a timeout (a `pthread_mutex_timedlock` of 20 ms, or no progress for 300 ms), because a program that really hangs cannot report anything. The machine has two CPUs; differences under about 30 percent are within its noise.

---

## 1. What a deadlock is

### 1.1 Four conditions

A set of threads is deadlocked when each is waiting for something only another thread in the set can provide. Coffman and his co-authors showed in 1971 that this needs four conditions at once (as OSTEP states them):

1. **Mutual exclusion.** A resource can be held by only one thread at a time (a lock, a device, a buffer).
2. **Hold and wait.** A thread holds one resource while waiting for another.
3. **No preemption.** A resource cannot be taken away from its holder; only the holder releases it.
4. **Circular wait.** There is a cycle of threads, each waiting for a resource held by the next.

If any one is missing, deadlock cannot happen. That sentence organises everything that follows: prevention (section 4) breaks one condition for good, avoidance (section 5) refuses requests that could lead to all four, and detection (section 6) lets them happen and cleans up.

### 1.2 The smallest deadlock

Two locks, two threads, opposite order. One thread takes A then B; the other takes B then A. If each takes its first lock before the other takes its second, both wait forever. The program below runs the pattern in rounds, with a barrier so both threads start together, and with a chosen pause between the first and second lock. To count deadlocks instead of suffering them, the second lock is taken with a 20 ms timeout.

```c
static void one_round(pthread_mutex_t *first, pthread_mutex_t *second)
{
    pthread_mutex_lock(first);
    work(gap_us);
    struct timespec dl;
    abs_after(&dl, 0.02);
    if (pthread_mutex_timedlock(second, &dl) == 0) pthread_mutex_unlock(second);
    else atomic_fetch_add(&deadlocks, 1);
    pthread_mutex_unlock(first);
}
```

Four settings: no pause, 1 microsecond, 50 microseconds, and the fixed version (both threads take A first) with a 50 microsecond pause. Two runs:

```text
abba     gap   0 us  rounds 2000  timed-out second locks 2
abba     gap   1 us  rounds 200  timed-out second locks 218
abba     gap  50 us  rounds 200  timed-out second locks 219
ordered  gap  50 us  rounds 200  timed-out second locks 0
```

```text
abba     gap   0 us  rounds 2000  timed-out second locks 1
abba     gap   1 us  rounds 200  timed-out second locks 214
abba     gap  50 us  rounds 200  timed-out second locks 214
ordered  gap  50 us  rounds 200  timed-out second locks 0
```

Read it carefully, because it is the reason deadlocks survive testing. With **no pause** the window in which one thread holds its first lock and has not yet taken its second is a few nanoseconds, and the two threads almost never overlap in it: 0 to 1 timeouts in 2,000 rounds here (a first development run with 1,000 rounds saw 5). With a pause of even 1 microsecond (`usleep(1)` actually sleeps about 50 microseconds) **every round deadlocked**; the count is above the number of rounds because both threads can time out in one round. The pause stands for anything that makes the window longer in production: a page fault, a log write, a preemption, a slower machine, a loaded server. The fixed version never deadlocked at any setting. A program that is wrong can pass tens of thousands of test runs, which is why section 8 is about tools that find the cycle without needing the bad interleaving.

### 1.3 What a real hang looks like from outside

The `hang` mode runs the same two loops with plain locks and nothing to rescue it. After one second the process was inspected from outside, first with `/proc`:

```text
state, wait channel and syscall of each thread:
  tid 2178 f1_abba_g state S wchan futex_do_wait syscall 202
  tid 2180 f1_abba_g state S wchan futex_do_wait syscall 202
  tid 2181 f1_abba_g state S wchan futex_do_wait syscall 202

cpu used by the process in the last second (top-like, from /proc):
  ticks used in 1 s: 0
```

Three threads (main and the two workers), all asleep in a `futex` wait (`syscall 202` is `futex` on x86-64), and **zero CPU used in a second**. That combination, state `S`, all threads in `futex_wait`, no CPU use, is the fingerprint of a lock deadlock; a spinning livelock would use CPU instead (section 7). Then `gdb -p` on the same process, backtraces of all threads:

```text
Thread 3 (Thread 0x7fe1661ff6c0 (LWP) "f1_abba_g"):
#0  futex_wait at ../sysdeps/nptl/futex-internal.h:146
#1  __GI___lll_lock_wait at ./nptl/lowlevellock.c:49
#2  lll_mutex_lock_optimized at ./nptl/pthread_mutex_lock.c:48
#3  ___pthread_mutex_lock at ./nptl/pthread_mutex_lock.c:93
#4  hang1 at prog/f1_abba.c:45
Thread 2 (Thread 0x7fe1659fe6c0 (LWP) "f1_abba_g"):
#0  futex_wait at ../sysdeps/nptl/futex-internal.h:146
#1  __GI___lll_lock_wait at ./nptl/lowlevellock.c:49
#2  lll_mutex_lock_optimized at ./nptl/pthread_mutex_lock.c:48
#3  ___pthread_mutex_lock at ./nptl/pthread_mutex_lock.c:93
#4  hang2 at prog/f1_abba.c:46
Thread 1 (Thread 0x7fe1665dd740 (LWP) "f1_abba_g"):
#0  __futex_abstimed_wait_common64 at ./nptl/futex-internal.c:57
#1  __futex_abstimed_wait_common at ./nptl/futex-internal.c:87
#2  __GI___futex_abstimed_wait_cancelable64 at ./nptl/futex-internal.c:139
#3  __pthread_clockjoin_ex at ./nptl/pthread_join_common.c:102
#4  main at prog/f1_abba.c:64
```

Threads 2 and 3 (the workers) are both in `pthread_mutex_lock`, called from `hang2` and `hang1`; thread 1 is `main`, waiting in `pthread_join`. The backtrace gives the two source lines where each is blocked. What it does not tell you directly is which lock each holds; in gdb you would print the mutex (`print *mutex`) and read its `__owner` field, which is the thread id of the holder (from memory), and match owners to waiters to close the cycle. On a core file the same commands work with `gdb binary core`.

---

## 2. Modelling: resource-allocation and wait-for graphs

A **resource-allocation graph** has a node for each thread and each resource. An edge from a resource to a thread means the thread holds it; an edge from a thread to a resource means the thread is waiting for it. A **wait-for graph** drops the resource nodes: an edge from thread P to thread Q means P waits for something Q holds. It is the same information, simpler to search.

- If every resource has **one instance** (a mutex is the usual case), a cycle is **necessary and sufficient** for deadlock: the threads on the cycle can never proceed.
- If a resource has **several instances** (three printers, a pool of buffers), a cycle is necessary but **not sufficient**: a thread outside the cycle may hold an instance and release it, which lets the cycle break. For this case you need the matrix algorithm of section 6.

The search for a cycle in a wait-for graph is an ordinary depth-first search with three colours (unvisited, on the current path, finished); an edge to a node on the current path closes a cycle. The program below does it on a six-process graph:

```c
static int dfs(int u)                                  /* returns 1 and prints the cycle when found */
{
    color[u] = 1; stack[sp++] = u;
    for (int v = 0; v < N; v++) {
        if (!wf[u][v]) continue;
        if (color[v] == 1) {
            printf("cycle:");
            int i = 0; while (stack[i] != v) i++;
            for (; i < sp; i++) printf(" P%d ->", stack[i]);
            printf(" P%d\n", v);
            return 1;
        }
        if (color[v] == 0 && dfs(v)) return 1;
    }
    color[u] = 2; sp--;
    return 0;
}

static void check(const char *label)
{
    memset(color, 0, sizeof color); sp = 0;
    printf("%s: ", label);
    for (int u = 0; u < N; u++) if (color[u] == 0 && dfs(u)) return;
    printf("no cycle, no deadlock\n");
}
```

```text
chain P0->P1->P2->P3: no cycle, no deadlock
after P3 waits for P1: cycle: P1 -> P2 -> P3 -> P1
cycle through the start: cycle: P0 -> P1 -> P2 -> P0
textbook state, P2 asks for nothing: no deadlock
same, but P2 asks for one more C: deadlocked: P1, P2, P3, P4
```

The first three lines are the graph search: a chain `P0 -> P1 -> P2 -> P3` has no cycle (P3 waits for nothing, so it will finish and release, and the chain unwinds); when P3 starts waiting for P1 the cycle `P1 -> P2 -> P3 -> P1` appears; and the last line shows a cycle through the first node. The last two lines are the several-instances case of section 6.

---

## 3. Deadlocks that are not two mutexes in opposite order

The textbook picture is two locks in opposite order. Most real deadlocks are one of these instead, and each satisfies the four conditions in a less obvious way.

### 3.1 Self-deadlock and joining yourself

A thread that takes a non-recursive lock it already holds waits for itself. This is the commonest deadlock in practice, and it hides in call chains: `update()` takes the lock and calls `log()`, which also takes it. POSIX offers three mutex types for exactly this. The program locks a mutex twice in one thread (the second time with a 200 ms timeout) for each type, and has a thread join itself:

```c
static void self_deadlock(const char *kind, int type)
{
    pthread_mutexattr_t at;
    pthread_mutex_t m;
    pthread_mutexattr_init(&at);
    pthread_mutexattr_settype(&at, type);
    pthread_mutex_init(&m, &at);
    pthread_mutex_lock(&m);
    struct timespec dl;
    abs_after(&dl, 0.2);
    int rc = pthread_mutex_timedlock(&m, &dl);         /* second lock by the same thread */
    printf("self-lock, %-9s mutex: %s\n", kind, describe(rc));
}
```

```text
self-lock, default   mutex: timed out (would hang)
self-lock, errorcheck mutex: EDEADLK, refused
self-lock, recursive mutex: acquired
join self: EDEADLK, refused
fork child, no atfork  : lock timed out (would hang)
fork child, with atfork: lock acquired
```

The first four lines are the self-lock tests. The default mutex would hang (the timeout fired); the **error-checking** mutex returns `EDEADLK` immediately, which turns a silent hang into a bug you can see; the **recursive** mutex succeeds by counting. Recursive locks are a way to stop the hang, not a design: they let a function be called with the invariant of the data broken halfway through, which is usually a worse bug than a hang. Use the error-checking type in debug builds and fix the call chain. `pthread_join` on yourself returns `EDEADLK` as well (glibc detects that one).

### 3.2 A lock held across `fork`

After `fork()` in a multithreaded process the child has a copy of the address space with only the calling thread. A lock that another thread held at the moment of the fork is **locked in the child, with a holder that does not exist there**. The child blocks forever the first time it takes that lock (a classic one is `malloc`'s lock if the child allocates before `exec`; from memory). The program holds a mutex in a helper thread for 100 ms, forks from the main thread in that window, and has the child try to lock it for 300 ms, then repeats with `pthread_atfork` handlers that take the lock before the fork and release it in both parent and child:

```c
static void fork_test(int with_atfork)
{
    pthread_t h;
    atomic_store(&holder_ready, 0);
    if (with_atfork) pthread_atfork(prepare, parent, child);
    pthread_create(&h, NULL, holder, NULL);
    while (!atomic_load(&holder_ready)) usleep(1000);
    pid_t pid = fork();                                /* the holder thread does not exist in the child */
    if (pid == 0) {
        struct timespec dl;
        abs_after(&dl, 0.3);
        int rc = pthread_mutex_timedlock(&m_fork, &dl);
        printf("fork child, %-11s: lock %s\n", with_atfork ? "with atfork" : "no atfork", describe(rc));
        fflush(stdout);
        _exit(0);
    }
    waitpid(pid, NULL, 0);
    pthread_join(h, NULL);
}
```

The last two lines of the output above are this test: without the handlers the child's lock timed out (it would hang), with them it was acquired. The rule that follows: in a multithreaded program, the child of `fork` may call only async-signal-safe functions until it calls `exec`, or the library that owns the lock must register `atfork` handlers as the program does here.

### 3.3 Waiting on a condition nobody will signal

A thread that waits on a condition variable while holding another lock, for a condition that only a thread needing that lock can make true, is a deadlock with a condition variable in the cycle: [OS 07](07_condition_variables_and_semaphores.md) measured the single-condition-variable version hanging in 5 runs out of 5. The detection graph of section 2 does not show it, because there is no lock held by the signaller: the "resource" is an event. This is why timeouts (section 6) are the only general safety net.

### 3.4 Interrupts

In the kernel, a lock taken both in process context and in an interrupt handler can deadlock one CPU with itself: code takes the lock with interrupts enabled, an interrupt arrives, and the handler spins on the lock its own CPU's interrupted code holds. The interrupted code cannot run until the handler returns, and the handler cannot return until it gets the lock. From memory, the fix is to take such locks with interrupts disabled (`spin_lock_irqsave`). The kernel's lockdep checks exactly this class of rule (section 8.4): the documentation says a lock used in interrupt context must not be taken while holding a lock that is ever taken with interrupts enabled.

### 3.5 Priority inversion is not a deadlock, but looks like one

A high-priority thread blocked on a lock held by a preempted low-priority thread makes no progress while a medium-priority thread runs; it is a delay, not a cycle, and it ends when the medium thread finishes ([OS 07](07_condition_variables_and_semaphores.md) section 7 measured 678 ms against 78 ms with priority inheritance). A thread-state dump looks the same as a deadlock (blocked in a futex wait); the difference is that some thread is running.

---

## 4. Prevention: break one condition for good

### 4.1 Attack circular wait: a lock order

Give all locks a total order and always acquire in increasing order; then no cycle can form, because a cycle would need some thread to take a lower lock while holding a higher one. This is the technique used almost everywhere. OSTEP notes that in a large system a total order is hard and a **partial order** is used instead (the Linux memory-mapping code documents about ten lock-order groups there), and that the order can be the lock's **address**, which needs no numbering.

The benchmark moves money between four accounts, each with its own lock. Two threads (pinned to the two CPUs) transfer in a loop for one second. Two traffic patterns: `random` (each transfer picks two distinct accounts at random) and `opposite` (thread 0 always moves 0 to 1, thread 1 always moves 1 to 0, the worst case for the naive version). The locking function is the whole point:

```c
static int lock_pair(int id, int from, int to)       /* returns 0 if told to stop while retrying */
{
    switch (mode_id) {
    case NAIVE:                                        /* from first, then to: any two threads can deadlock */
        pthread_mutex_lock(&accts[from].m);
        pthread_mutex_lock(&accts[to].m);
        break;
    case ORDERED:                                      /* lower account number first: a total order */
    case ADDRESS: {                                    /* lower lock address first: same idea, no numbering needed */
        int a = from, b = to;
        if (mode_id == ORDERED ? from > to : (void *)&accts[from].m > (void *)&accts[to].m) { a = to; b = from; }
        pthread_mutex_lock(&accts[a].m);
        pthread_mutex_lock(&accts[b].m);
        break;
    }
    case TRY_NOBACKOFF:
    case TRY_BACKOFF: {                                /* take one, try the other, give up and retry */
        unsigned s = 12345 + id;
        for (;;) {
            pthread_mutex_lock(&accts[from].m);
            if (pthread_mutex_trylock(&accts[to].m) == 0) return 1;
            pthread_mutex_unlock(&accts[from].m);
            atomic_fetch_add(&retries[id], 1);
            if (mode_id == TRY_BACKOFF) { volatile int x = 0; for (unsigned k = rnd(&s) % 200; k; k--) x++; }
            if (atomic_load(&stop)) return 0;
        }
    }
    case GLOBAL:                                       /* one lock for everything: no cycle possible, no parallelism */
        pthread_mutex_lock(&global);
        break;
    }
    return 1;
}
```

The `naive` mode takes `from` then `to`. `ordered` takes the lower account number first and `address` the lower lock address first. Results, run 1:

```text
naive    random   STUCK after 1127 transfers, no progress for 300 ms (at 343 ms)
ordered  random       6.01 M transfers/s  retries per transfer   0.00  total money 4000
address  random       5.89 M transfers/s  retries per transfer   0.00  total money 4000
global   random      12.68 M transfers/s  retries per transfer   0.00  total money 4000
```

```text
naive    opposite STUCK after 92 transfers, no progress for 300 ms (at 342 ms)
try      opposite     8.26 M transfers/s  retries per transfer   0.03  total money 4000
tryrand  opposite     8.07 M transfers/s  retries per transfer   0.02  total money 4000
ordered  opposite     5.70 M transfers/s  retries per transfer   0.00  total money 4000
address  opposite     5.86 M transfers/s  retries per transfer   0.00  total money 4000
global   opposite    14.98 M transfers/s  retries per transfer   0.00  total money 4000
```

Run 2:

```text
naive    random   STUCK after 940 transfers, no progress for 300 ms (at 342 ms)
ordered  random       6.06 M transfers/s  retries per transfer   0.00  total money 4000
address  random       6.43 M transfers/s  retries per transfer   0.00  total money 4000
global   random      11.73 M transfers/s  retries per transfer   0.00  total money 4000
```

```text
naive    opposite STUCK after 497 transfers, no progress for 300 ms (at 343 ms)
try      opposite     8.28 M transfers/s  retries per transfer   0.05  total money 4000
tryrand  opposite     7.82 M transfers/s  retries per transfer   0.02  total money 4000
ordered  opposite     5.16 M transfers/s  retries per transfer   0.00  total money 4000
address  opposite     5.34 M transfers/s  retries per transfer   0.00  total money 4000
global   opposite    13.57 M transfers/s  retries per transfer   0.00  total money 4000
```

- **`naive` got stuck in both patterns, in both runs**: it completed 32 to 1,243 transfers and then no thread made progress for 300 ms, and the benchmark gave up.
- **`ordered` and `address` never deadlocked**, and cost the same within noise (about 5.2 to 6.4 million transfers per second). The address order is the one to use when the locks are not numbered, such as two objects of the same type.
- The total money was 4,000 at the end of every completed run (four accounts of 1,000): the locking did its job.

**The gotcha that interviewers use.** Ordered locking of `from` and `to` fails when `from == to`: both locks are the same lock, and the second acquisition is a self-deadlock (section 3.1). The function must test for equal accounts first, and lock once. The benchmark avoids it by choosing distinct accounts.

### 4.2 Attack hold and wait: take everything at once, or nothing

Acquire all the locks you need in one step, guarded by a global lock. The `global` mode is the extreme of that: one lock for all accounts. It cannot deadlock (one lock, no cycle). It was the **fastest** mode in this benchmark, about 11.7 to 13.6 million transfers per second against 5.2 to 6.4 for the ordered locks, because with a critical section of two additions the cost is the lock itself, and one lock pair is cheaper than two. That result does not carry over: with more CPUs, or a long critical section, one lock serialises everything (the benchmark has two threads on two CPUs and I did not test more). OSTEP's reason against it in general is the right one: callers must know all the locks they need in advance, and concurrency drops.

### 4.3 Attack no preemption: trylock and back off

Take the first lock, `trylock` the second, and if it fails release the first and start again. No thread ever waits while holding a lock, so there is no hold and wait. The two versions in the benchmark are `try` (retry at once) and `tryrand` (wait a random short time before retrying):

```text
try      random       7.07 M transfers/s  retries per transfer   0.10  total money 4000
tryrand  random       6.76 M transfers/s  retries per transfer   0.06  total money 4000
```

Both completed, with 6.4 to 8.7 million transfers per second, and retried 0.02 to 0.11 times per transfer: a failed try is rare here. Neither livelocked. That is the honest result for two threads on two free-running CPUs, and section 7 explains when the retry loop does livelock. Two problems with this technique that the benchmark hides: it only works when the code can undo what it did between the first lock and the failure (OSTEP: backing out is awkward when the locks are taken inside called routines), and it adds a retry loop whose behaviour under load needs its own testing.

### 4.4 Attack mutual exclusion: do not lock

Lock-free structures (compare-and-swap loops) have no locks to deadlock on; they bring their own problems (livelock is still possible, and the memory-ordering rules of [OS 08](08_memory_barriers.md)). Where the data allows it, partitioning (each thread owns its data) is the better version of this idea.

---

## 5. Avoidance: the banker's algorithm

Prevention restricts how locks are taken. Avoidance keeps the freedom and refuses a request when granting it could lead to deadlock. It needs information in advance: each process declares the **maximum** it will ever ask for. The system is in a **safe state** if some order exists in which every process can run to completion, each getting its remaining need from what is free plus what earlier finishers return. Unsafe does not mean deadlocked; it means deadlock is possible, so the banker will not enter it.

For `n` processes and `m` resource types the data is `Available` (free instances), `Allocation` and `Max` (n by m), and `Need = Max - Allocation`. The **safety algorithm** repeats: find an unfinished process whose need fits in `Work` (initially `Available`), pretend it finishes and add its allocation to `Work`; if all processes finish, the state is safe and the order is a safe sequence. The **resource-request algorithm** takes a request from a process: refuse it if it exceeds the process's need, make it wait if it exceeds what is free, otherwise pretend to grant it and run the safety algorithm, keeping the grant only if the result is safe.

```c
static int is_safe(int quiet)
{
    int work[NR], done[NP] = {0}, order[NP], n = 0;
    memcpy(work, avail, sizeof work);
    for (int pass = 0; pass < NP; pass++) {
        int found = 0;
        for (int p = 0; p < NP; p++) {
            if (done[p]) continue;
            int ok = 1;
            for (int r = 0; r < NR; r++) if (need(p, r) > work[r]) ok = 0;
            if (!ok) continue;
            for (int r = 0; r < NR; r++) work[r] += alloc[p][r];   /* p runs to completion and returns everything */
            done[p] = 1; order[n++] = p; found = 1;
        }
        if (!found) break;
    }
    if (!quiet) {
        if (n == NP) { printf("safe, sequence:"); for (int i = 0; i < n; i++) printf(" P%d", order[i]); printf("\n"); }
        else printf("UNSAFE: only %d of %d processes can finish\n", n, NP);
    }
    return n == NP;
}

static void request(int p, const int req[NR])
{
    printf("P%d requests (%d,%d,%d): ", p, req[0], req[1], req[2]);
    for (int r = 0; r < NR; r++) if (req[r] > need(p, r)) { printf("denied, exceeds its declared maximum\n"); return; }
    for (int r = 0; r < NR; r++) if (req[r] > avail[r]) { printf("must wait, not available\n"); return; }
    for (int r = 0; r < NR; r++) { avail[r] -= req[r]; alloc[p][r] += req[r]; }
    if (is_safe(1)) { printf("granted, "); is_safe(0); return; }
    for (int r = 0; r < NR; r++) { avail[r] += req[r]; alloc[p][r] -= req[r]; }
    printf("would be unsafe, refused (the request is available but granting it risks deadlock)\n");
}
```

The textbook state: 10, 5 and 7 instances of three resources, five processes. The program computes `Available`, the need matrix, and runs three requests:

```text
available (3,3,2); need matrix:
  P0  7 4 3
  P1  1 2 2
  P2  6 0 0
  P3  0 1 1
  P4  4 3 1
initial state: safe, sequence: P1 P3 P4 P0 P2
P1 requests (1,0,2): granted, safe, sequence: P1 P3 P4 P0 P2
P4 requests (3,3,0): must wait, not available
P0 requests (0,2,0): would be unsafe, refused (the request is available but granting it risks deadlock)
```

The initial state is safe with sequence `P1 P3 P4 P0 P2`. P1's request is granted, because the resulting state is still safe. P4's request for `(3,3,0)` must wait: it is within P4's need but not free. P0's request for `(0,2,0)` is the instructive one: the resources **are** free and the request is within P0's declared maximum, and the banker still refuses it, because granting it leaves no process able to finish. That is avoidance doing what prevention cannot: it lets a request through exactly when it is safe.

Cost and why it is rarely used: the safety algorithm is O(m n squared) per request, but the real obstacles are the assumptions: every process must declare its maximum in advance, the number of processes must be fixed, and resources must not disappear. OSTEP says this style of approach is practical only in limited settings, such as an embedded system that knows its whole workload; from memory, general-purpose operating systems rely on prevention by lock order and on detection.

A second data set, with its own answers, is Problem B.

---

## 6. Detection and recovery

Let deadlocks happen, find them, and clean up. This is the right design when deadlocks are rare and recovery is cheap, and it is what database systems do: OSTEP says they run a periodic detector that checks a resource graph for cycles.

### 6.1 Several instances of a resource

For single-instance resources the cycle search of section 2 is enough. For several instances use the matrix version of the safety algorithm, with **current requests** in place of maximum needs: a process whose request fits in `Work` can finish, so return its allocation; any process left over at the end is deadlocked. The program runs it on the textbook state (`Allocation` as in section 5, nothing available):

```c
static void detect_matrix(const char *label, int avail0[NR], int alloc[NP][NR], int request[NP][NR])
{
    int work[NR], fin[NP] = {0};
    memcpy(work, avail0, sizeof work);
    for (int p = 0; p < NP; p++) { int z = 1; for (int r = 0; r < NR; r++) if (alloc[p][r]) z = 0; if (z) fin[p] = 1; }
    for (int again = 1; again;) {
        again = 0;
        for (int p = 0; p < NP; p++) {
            if (fin[p]) continue;
            int ok = 1;
            for (int r = 0; r < NR; r++) if (request[p][r] > work[r]) ok = 0;
            if (ok) { for (int r = 0; r < NR; r++) work[r] += alloc[p][r]; fin[p] = 1; again = 1; }
        }
    }
    printf("%s: ", label);
    int any = 0;
    for (int p = 0; p < NP; p++) if (!fin[p]) { printf("%sP%d", any ? ", " : "deadlocked: ", p); any = 1; }
    printf(any ? "\n" : "no deadlock\n");
}
```

The last two lines of the output of section 2 are the two runs: when P2 asks for nothing, every process can finish in some order and there is no deadlock; when P2 asks for one more instance of resource C, **P1, P2, P3 and P4 are deadlocked** and P0, which is not waiting for anything, is not. (P0 holds resources and would finish if it asked for nothing; the algorithm reports only the processes that can never finish.)

### 6.2 When to run the detector

Running it on every request is the most prompt and the most expensive; running it periodically or when the system seems idle (CPU use falls, throughput drops) is cheaper but lets the deadlocked processes sit there longer, and the number of deadlocked processes grows as others queue behind them. The practical version in most systems is a **timeout**: a lock acquisition that waits longer than a limit is treated as a deadlock suspect. Every program in this chapter used `pthread_mutex_timedlock` for that reason. A timeout is a guess (a slow holder looks the same as a deadlock) and a safety net, not a diagnosis.

### 6.3 Recovery

- **Terminate** all deadlocked processes (simple and costly), or one at a time until the cycle breaks (needs a rerun of the detector after each).
- **Roll back** a process to a checkpoint or to the start of its transaction and restart it. This is the database answer: abort one transaction (the **victim**), release its locks, rerun it. From memory, the choice of victim weighs how much work is lost, how many resources are held and how many times it has already been the victim (to avoid starving it).
- **Preempt** a resource and give it to another process, if the resource can be saved and restored (memory pages can; a half-updated data structure cannot).
- **Escalate to a person or a restart.** OSTEP records that, for the complex cases, systems restart or involve a human.

Whichever you choose, recovery must itself be safe: the process that was rolled back must be restartable without repeating a side effect (a payment taken twice).

---

## 7. Livelock and starvation

**Livelock.** The threads are not blocked: they run, and they make no progress. The standard cause is the retry loop of section 4.3: two threads each take their first lock, fail the `trylock` on the second, release, and retry in step. OSTEP's remedy is a random delay before the retry, which breaks the symmetry. In the benchmark the plain `try` loop did not livelock: it retried 0.02 to 0.05 times per transfer with opposite traffic, and finished. The symmetry that makes the loop livelock needs two threads that take the first lock, fail and retry on exactly the same schedule, and independent CPUs with ordinary timing noise do not stay in step for long. Treat that as a result about this machine and this loop: the risk is real when retries are triggered by the same event (a broadcast wake-up, a shared timer), and the cure is cheap. The fingerprint of livelock in a process dump is the opposite of the one in section 1.3: the threads are running (high CPU use) and the same few lines of code appear in every backtrace.

**Starvation.** One thread never gets the resource, though the system as a whole progresses. It is a fairness failure, not a cycle: [OS 07](07_condition_variables_and_semaphores.md) measured a writer starved for 1,996 ms of a 2,000 ms run by a reader-preference lock. The fixes are queues (FIFO ordering, with the convoy cost seen in [OS 06](06_locks.md)), aging (raise a waiter's priority as it waits) and, in a victim-choice policy, counting how often a process has been chosen.

| | Threads blocked? | CPU use | Cycle in wait-for graph? | Typical fix |
| --- | --- | --- | --- | --- |
| Deadlock | all, forever | none | yes | lock order, avoid, detect and recover |
| Livelock | no | high | no (state keeps changing) | random back-off, a tiebreaker |
| Starvation | one or few | normal | no | queues, aging, fair locks |

---

## 8. Finding deadlocks

### 8.1 In a hung process

Section 1.3 is the method: `/proc/PID/task/*/wchan` and `syscall` show what each thread waits in; `gdb -p PID -batch -ex 'thread apply all bt'` (or `bt` on a core) gives the code location of each; matching lock owners to waiters closes the cycle. For a process you cannot stop, `strace -f -p PID` shows the threads blocked in `futex` calls (from memory; the `strace` binary is installed here but I did not use it on this hang).

### 8.2 ThreadSanitizer and Helgrind: report the cycle without the deadlock

Both tools record the order in which locks are taken and report an order that can form a cycle, **whether or not the threads ever collide**. The `serial` mode of the test program runs the two orders one thread after the other, so it cannot deadlock and prints that it finished:

```text
WARNING: ThreadSanitizer: lock-order-inversion (potential deadlock)
  Cycle in lock order graph: M (0x55e9493b30c0) => M (0x55e9493b3080) => M
  Mutex M acquired here while holding mutex M in thread T1:
  Mutex M acquired here while holding mutex M in thread T2:
SUMMARY: ThreadSanitizer: lock-order-inversion (potential deadlock) prog/f1_abba.c:48 in serial1
serial: finished, no deadlock occurred
```

ThreadSanitizer (`gcc -fsanitize=thread`) reported `lock-order-inversion (potential deadlock)`, with a cycle of the two mutexes and the two places where each was acquired while holding the other (the addresses are shown as `M`). Helgrind (`valgrind --tool=helgrind`) found the same:

```text
Thread #3: lock order "0x10C0C0 before 0x10C080" violated
 followed by a later acquisition of lock at 0x10C0C0
 followed by a later acquisition of lock at 0x10C080
 Lock at 0x10C0C0 was first observed
 Lock at 0x10C080 was first observed
serial: finished, no deadlock occurred
ERROR SUMMARY: 1 errors from 1 contexts (suppressed: 0 from 0)
```

This is the property that matters: the program above ran to completion, and both tools reported the bug anyway. Section 1.2 showed that a test could not.

### 8.3 A lock-order checker in a page

The idea of both tools, and of the kernel's lockdep, fits in a short program. Keep a graph whose nodes are lock classes (here, names). Whenever a thread takes lock B while holding lock A, add the edge A to B. Before adding an edge, check whether B can already reach A through existing edges; if it can, the new edge closes a cycle and the order is inconsistent. The program records edges across the whole run, so the second thread's order is compared with the first thread's even though they never overlapped:

```c
static int path(int from, int to, int *seen, int *via)  /* depth-first search over recorded edges */
{
    if (from == to) return 1;
    seen[from] = 1;
    for (int k = 0; k < ncls; k++)
        if (edge[from][k] && !seen[k] && path(k, to, seen, via)) { via[from] = k; return 1; }
    return 0;
}

static void lo_lock(lo_mutex *l)
{
    pthread_mutex_lock(&graph_lock);
    int b = cls(l->name);
    for (int i = 0; i < nheld; i++) {
        int a = held[i], seen[MAXC] = {0}, via[MAXC];
        if (!edge[a][b] && path(b, a, seen, via)) {    /* adding a -> b closes a cycle b -> ... -> a */
            printf("possible deadlock: %s is taken while holding %s, but the reverse order was seen: %s", l->name, cname[a], cname[b]);
            for (int x = b; x != a; x = via[x]) printf(" -> %s", cname[via[x]]);
            printf(" -> %s\n", cname[b]);
            reports++;
        }
        edge[a][b] = 1;
    }
    held[nheld++] = b;
    pthread_mutex_unlock(&graph_lock);
    pthread_mutex_lock(&l->m);
}
```

Three tests, each running its threads one at a time (nothing can block): both threads in the same order (`consistent`), the opposite order (`two`), and a three-lock cycle across three threads `A,B` then `B,C` then `C,A` (`three`):

```text
consistent: 2 thread runs, none blocked, 0 report(s)
possible deadlock: A is taken while holding B, but the reverse order was seen: A -> B -> A
two: 2 thread runs, none blocked, 1 report(s)
possible deadlock: A is taken while holding C, but the reverse order was seen: A -> B -> C -> A
three: 3 thread runs, none blocked, 1 report(s)
```

No report for consistent orders; a report for `B,A` after `A,B`; and for the three-lock case a report with the whole cycle `A -> B -> C -> A`, which no pair of the threads shows by itself. Two limits of this miniature, both shared in some form by real tools: it assumes strictly nested unlocking, and it identifies locks by name, so two instances of the same kind are one class (which is also the right choice for lockdep, and the source of its false positives, below).

### 8.4 The kernel's lockdep

The kernel's lock validator (lockdep, from the `lockdep-design` document) works on the same principle with more detail:

- A **lock class** is a group of locks that are logically the same with respect to locking rules, registered when its first instance is used; every instance of a spinlock inside a structure that has many copies maps to one class. The graph's edges (`L1 -> L2`: L2 acquired while L1 held) are recorded as the system runs, and a cycle of any length is reported. The document states the proof does not need the multi-CPU interleaving that would actually deadlock, only that each single-task chain has been seen once.
- **Usage state.** For hardirq and softirq context lockdep also tracks whether a lock has ever been taken in that context (irq-safe) or with that interrupt enabled (irq-unsafe). The rule from the documentation: a hardirq-safe lock must not be taken while a hardirq-unsafe lock is held, because an interrupt could take the safe lock while the unsafe one is held (the inversion of section 3.4). The same holds for softirq.
- **Same-class nesting** (a lock of a class taken while another of the same class is held, such as the whole disk and a partition) is declared with `_nested()` variants and subclasses (`mutex_lock_nested(lock, subclass)`), otherwise it looks like recursion. A wrong subclass assignment gives false positives or false negatives.
- **Cost.** Checking every chain against every other would be O(N squared); lockdep checks each unique lock chain once and stores a 64-bit hash of it. The class table is bounded (`MAX_LOCKDEP_KEYS`, default 8191; a typical desktop uses fewer than 1,000 classes, and `/proc/lockdep_stats` shows the count). Locks that are not initialised properly (per-bucket locks with no `spin_lock_init()`) each become a class and exhaust the table.
- **Annotations.** `lockdep_assert_held()` and relatives make a function state which lock it needs, so a caller that forgets it is caught on the first call.

The documentation says nothing I read about the build option that enables it; from memory it is `CONFIG_PROVE_LOCKING`, which this VM's kernel does not set, so I could not run it. The practical use is on a debug kernel in testing: a lockdep splat on a code path that has been run once is the kernel's equivalent of sections 8.2 and 8.3.

---

## What it costs

| Technique | What it gives up | Measured here (2 CPUs, 4 accounts) |
| --- | --- | --- |
| Lock order (number or address) | Needs a global order; callers must follow it | 5.2 to 6.4 million transfers/s, no deadlock |
| One global lock | All parallelism | 11.7 to 13.6 million transfers/s, no deadlock |
| Trylock and retry | Needs undo; retry loop to tune | 6.4 to 8.7 million transfers/s, 0.02 to 0.11 retries per transfer |
| Naive order | Nothing, until it hangs | stuck after 32 to 1,243 transfers |
| Banker's algorithm | Declared maximum, fixed process set | O(m n squared) per request (not measured) |
| Detect and recover | Work lost on rollback; detector cost | not measured |
| Timeout on lock | A guess; a slow holder looks stuck | used in every program above, 20 ms to 300 ms |
| TSan or Helgrind order check | Slower program, needs a run that reaches both orders | found the cycle in a run that finished |

## Common mistakes

1. **Locking two objects of the same type in argument order.** `transfer(a, b)` and `transfer(b, a)` deadlock. Order by address or by id.
2. **Forgetting the `a == b` case** in ordered locking, which becomes a self-deadlock.
3. **Calling out while holding a lock.** A callback, a virtual method or a log function that takes another lock creates an order you did not write down. Release the lock first, or document the order.
4. **Using a recursive mutex to silence a hang.** It hides the broken invariant instead of fixing the call chain.
5. **Calling `fork` in a multithreaded program and doing real work in the child** before `exec`. The child inherits locks whose holders are gone.
6. **Treating a passing test as evidence.** Section 1.2: 0 deadlocks in 2,000 rounds and a deadlock every round differ by 1 microsecond.
7. **Trylock without a tiebreaker.** Retrying at once under a shared trigger is the livelock recipe; add a random delay.
8. **Calling a cycle in a graph a deadlock when the resource has several instances.** It may be recoverable.
9. **Believing the banker's algorithm is used in general-purpose systems.** It needs maximum claims in advance; real systems use lock order and detection.
10. **Running a timeout as the only defence.** It cannot tell a deadlock from a slow holder and it does not say who holds what.

## Questions out loud

1. *State the four conditions and one fix for each.* Mutual exclusion (lock-free, partitioning), hold and wait (take all at once), no preemption (trylock and back off), circular wait (lock order). Remove any one and deadlock cannot occur.
2. *Write a transfer that cannot deadlock.* Lock the lower account (or lower address) first; handle `from == to` by locking once; keep the critical section to the two updates.
3. *When does a cycle in the graph prove deadlock?* When every resource in it has one instance. With several instances use the matrix detection.
4. *Safe, unsafe, deadlocked?* Safe: some order lets everyone finish. Unsafe: no such order exists, deadlock possible. Deadlocked: processes are blocked now and can never proceed. The banker's algorithm refuses a request that leads from safe to unsafe even when the resources are free.
5. *Why is the banker's algorithm not used in practice?* It needs each process's maximum claim in advance, a fixed set of processes and fixed resources.
6. *Deadlock, livelock, starvation?* Blocked forever with no CPU use and a cycle; running with no progress and high CPU use; one party never served while the system progresses.
7. *How do you find a deadlock in a running service?* Attach gdb or take a core, print all backtraces, find the threads blocked in lock calls, match owners to waiters. Prevent it with a lock-order checker (ThreadSanitizer, Helgrind, lockdep) in testing.
8. *How does lockdep find a deadlock that did not happen?* It records the order in which lock classes are taken over the whole run and reports a cycle in that graph, so two code paths run at different times are enough.
9. *What happens to a lock held by another thread when you fork?* It stays locked in the child with no one to release it; restrict the child to async-signal-safe calls until `exec`, or use `pthread_atfork`.
10. *A database detects a deadlock. What does it do?* Pick a victim transaction, abort and roll it back, release its locks, and rerun it; the choice weighs lost work and how often it has been chosen.

---

## Problems

**Where to solve them.** On any Linux machine with a C compiler and 2 or more CPUs; the programs are in the text. A and E are on paper with the program output to check against; B can be checked by running `f3_banker 1`. C needs the locking function of section 4.1.

**How to approach them.** For A, name the four conditions one by one in the scenario. For B, write the need matrix first and then run the safety loop by hand. For C, think about what happens to the call `transfer(x, x)`. For D, draw the lock graph first; the cycle will be visible. For E, simulate the order recording, adding one edge at a time.

### Problem A (E, entry level): name the conditions

A print spooler has two printers and two document threads. Each thread locks printer 1, then waits to lock printer 2, and the other does the reverse. Which of the four conditions hold? Give one change per condition that breaks it, and say which you would pick.

### Problem B (M, medium): a second banker's table

Five processes, three resource types, total instances (7,4,4). `Allocation` rows P0 to P4: (1,0,1), (2,1,0), (0,1,1), (1,1,0), (0,0,1). `Max` rows: (3,2,2), (4,2,1), (2,2,2), (3,1,1), (1,2,2). Compute `Available` and `Need`, find a safe sequence, and then decide whether P3's request for (2,0,1) can be granted, and after it, P0's request for (1,1,0).

### Problem C (M, medium): the same account twice

The function of section 4.1 locks the lower account number first. A caller passes `from == to`. What happens with the default mutex, what with an error-checking mutex, and what should the function do instead? Then add a third account type (a shared fee account that every transfer also credits) and say where it belongs in the order.

### Problem D (H, hard): a cache with a bucket lock and a list lock

A cache has one lock per hash bucket and one lock for an LRU list. `get(key)` takes the bucket lock and then the LRU lock to move the entry to the front. `evict()` takes the LRU lock, picks the tail entry, and then takes that entry's bucket lock to remove it. Show the deadlock, propose two fixes (one by lock order, one by trylock), and say which you would choose and what each costs.

### Problem E (H, hard): predict the checker

Three threads run in sequence, never overlapping: T1 takes A then B, T2 takes B then C, T3 takes C then A, each releasing both before the next starts. Predict what the lock-order checker of section 8.3 reports and at which thread, then say whether the real program with the three threads running at once can deadlock and under which interleaving.

---

## Answers

**A.** All four hold: the printers are exclusive (mutual exclusion), each thread holds one and waits for the other (hold and wait), a printer cannot be taken from a thread in the middle of a document (no preemption), and thread 1 waits for thread 2 while thread 2 waits for thread 1 (circular wait). Breaks: lock-free or sharing is not possible for a physical printer; take both printers in one step, or a pool allocator that gives out two at once (hold and wait); allow the spooler to abort a job and restart (no preemption); always lock printer 1 before printer 2 (circular wait). The lock order is the cheapest and the one to pick: it needs no support from the printers and no rollback.

**B.** `Available` is the total minus the column sums of `Allocation`, (4,3,3) summed, so (3,1,1). `Need = Max - Allocation` is P0 (2,2,1), P1 (2,1,1), P2 (2,1,1), P3 (2,0,1), P4 (1,2,1). The program (`f3_banker 1`) gives:

```text
available (3,1,1); need matrix:
  P0  2 2 1
  P1  2 1 1
  P2  2 1 1
  P3  2 0 1
  P4  1 2 1
initial state: safe, sequence: P1 P2 P3 P4 P0
P3 requests (2,0,1): granted, safe, sequence: P3 P4 P0 P1 P2
P0 requests (1,1,0): granted, safe, sequence: P3 P0 P1 P2 P4
```

The initial state is safe with sequence P1, P2, P3, P4, P0. P3's request (2,0,1) is within its need and within what is available, and after granting it the state is still safe (sequence P3, P4, P0, P1, P2), so it is granted. P0's request (1,1,0) is then granted too, and the resulting state is safe with sequence P3, P0, P1, P2, P4. By hand: after P3's grant `Available` is (1,1,0); P3 now needs (0,0,0) and finishes, returning its allocation (3,1,1), so `Work` becomes (4,2,1), and so on down the sequence. Any safe order is acceptable if every step fits in `Work`; the program prints the first one its scan order finds.

**C.** With `from == to` the function locks the same mutex twice. A default mutex hangs; an error-checking mutex returns `EDEADLK` from the second call, which turns the bug into a visible failure; a recursive mutex succeeds and the transfer moves money from an account to itself, which is harmless here and dangerous in general. The function should test `if (from == to) return;` (or lock once and do the right thing) before locking anything. For the shared fee account, give it a fixed position in the order, for example the **highest** (every transfer locks the two user accounts in order and then the fee account last): since every thread then takes the fee lock last, no cycle can include it, and it remains a bottleneck that serialises all transfers through one lock (the cost of one shared account, not a deadlock risk).

**D.** `get` holds a bucket lock and waits for the LRU lock. `evict` holds the LRU lock and waits for a bucket lock. If `get(k)` holds bucket B and `evict` holds LRU and wants B, both wait: a cycle B to LRU to B. Fix 1, by order: make the order bucket then LRU everywhere, so `evict` must not hold the LRU lock while it takes a bucket lock: it takes the LRU lock, reads the tail entry's key, releases the LRU lock, takes the bucket lock, takes the LRU lock again, and re-checks that the entry is still the tail (it may have moved meanwhile), then removes it. Cost: a second acquisition and a re-check. Fix 2, by trylock: `evict` takes the LRU lock, `trylock`s the bucket; on failure it releases the LRU lock and tries a different tail entry or retries. Cost: a retry loop that needs a random delay or a bounded number of attempts, and possible repeated failure when the tail entry is hot. I would choose fix 1 for the clarity of a documented order, and use fix 2 only where the re-check is impossible.

**E.** The checker reports one deadlock, at T3, when it takes A while holding C. By then the graph has the edges A to B (from T1) and B to C (from T2), and T3's new edge C to A would close the cycle A to B to C to A: the program printed exactly this (`A -> B -> C -> A`, 1 report, section 8.3). No pair of threads is inconsistent on its own (T1 and T2 share only B, T2 and T3 share only C, and T1 and T3 share only A, so no two threads take two common locks in opposite orders), which is why a checker that looks only at pairs would miss it. Run at the same time, the program can deadlock if each thread takes its first lock before any takes its second: T1 holds A and wants B, T2 holds B and wants C, T3 holds C and wants A. That needs all three to be inside their windows at once, rarer than the two-thread case of section 1.2 but possible.

---

## Cheat sheet

- Deadlock needs all four: mutual exclusion, hold and wait, no preemption, circular wait. Remove one and it cannot happen.
- Fix order of preference in code: lock order (by number or address), then trylock with random back-off, then a coarser lock; use timeouts as a net.
- Always handle `a == b`; use an error-checking mutex in debug builds.
- Never call unknown code while holding a lock; a callback creates an order you did not choose.
- After `fork` in a threaded program, call only async-signal-safe functions until `exec`, or use `pthread_atfork`.
- Cycle means deadlock only for single-instance resources; otherwise run the matrix detection.
- Banker's: safe state means some order lets everyone finish; refuse a request that leads to an unsafe state even if the resources are free; it needs declared maximums, so it is rare in practice.
- Hung process: state `S`, no CPU, threads in `futex_wait`: deadlock. High CPU with no progress: livelock. One thread never served: starvation.
- Find the bug without the hang: ThreadSanitizer, Helgrind, lockdep (record lock-order edges, report a cycle).
- A passing test is not evidence: a 1 microsecond change moved the deadlock rate from about 0 to 100 percent.

## Sources

Measured on the VM described above. Not fetched, and stated from memory in the text: the deadlock-handling choices of database systems (victim selection), the `malloc` lock in a forked child, the interrupt-handler deadlock and `spin_lock_irqsave`, the `__owner` field of a glibc mutex, `CONFIG_PROVE_LOCKING`, and that general-purpose systems do not use the banker's algorithm. The OSTEP chapter's study counts (31 deadlock bugs among 105 concurrency bugs) are in the source but not used in the text above.

- [OSTEP: Common concurrency problems (PDF)](https://pages.cs.wisc.edu/~remzi/OSTEP/threads-bugs.pdf), fetched: the four conditions, prevention by condition, avoidance by scheduling, detect and recover, the Lu et al. study
- [Linux kernel: Runtime locking correctness validator](https://docs.kernel.org/locking/lockdep-design.html), fetched: lock classes, usage states, the dependency graph, irq-safety rules, nesting subclasses, cost and limits
- [OS 06: Locks](06_locks.md), [OS 07: Condition variables and semaphores](07_condition_variables_and_semaphores.md) and [OS 08: Memory barriers](08_memory_barriers.md), the layers underneath
