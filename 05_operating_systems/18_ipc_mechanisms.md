# OS 18: IPC mechanisms and their real costs

*Part of the [Operating systems plan](00_plan.md) · Time: about 10 hours (7 reading and experiments, 3 problems) · You need: [OS 17](17_ipc_fundamentals.md) (the IPC mechanisms and their rules), [OS 06](06_locks.md) and [OS 07](07_condition_variables_and_semaphores.md) (mutexes, condition variables), [OS 08](08_memory_barriers.md) (acquire and release) and [OS 15](15_file_systems.md) (descriptors and open file descriptions)*

This is a rewrite. The earlier version was a sketch of C++ wrappers with round-number costs from memory; this one measures everything it states. [OS 17](17_ipc_fundamentals.md) answered "which mechanism, and what are its rules". This tutorial answers the questions that come next in a senior systems interview. If two processes must exchange a million messages a second, what is the design, and where does the time go? What is `eventfd`, and how does it differ from a pipe? What is a futex, and why is an uncontended mutex free? How do you let a consumer sleep without losing a wake-up, and when should it spin instead? How does a process hand an open file, a socket or a block of memory to another process it did not fork? How do you learn that a peer died without racing with pid reuse? What is netlink, why does Wi-Fi configuration use it, and how does it compare with `/proc` and `sysfs`? The tutorial ends with a complete data plane that uses all of them, and a decision table built from measured numbers.

**Two honest limits, stated first.** (1) The machine is a Firecracker micro-VM with 2 virtual CPUs. Waking a thread on **another** CPU costs an inter-processor interrupt, which a VM makes expensive: the cross-CPU round trips below (about 25 µs) are much slower than on bare metal (single-digit microseconds, from memory), while the same-CPU numbers (about 2 µs) are closer to a real machine. I report both and never mix them. (2) Two things the tutorial describes were not available to run: the generic netlink family `nl80211` (the Wi-Fi configuration interface) is not in this kernel, so that lookup is shown failing, and the `dummy` network driver is not built, so the interface-event demonstration uses a `bridge` instead. Anything I did not run or fetch is marked "from memory".

By the end you can:

- do the arithmetic that decides a design: one million messages a second leaves 1 µs per message, a wake-up across CPUs costs about 25 µs here, so the wake-up cannot be on the per-message path (measured: a round trip through two `eventfd`s took 25,893 ns across CPUs and 1,824 ns on one CPU);
- explain `eventfd`, in counter and semaphore mode, and why a count of writes collapses into one read (measured: three writes of 1 and one read returned 3);
- explain the futex contract, "sleep only if the word still holds the value I saw", and build a mutex from it (measured: a hand-built three-state mutex cost 14.1 ns uncontended with zero system calls, and under contention 2.97 percent of acquisitions entered the kernel);
- write a shared-memory ring whose consumer sleeps without ever missing a wake-up, using a "sleeping" flag and a re-check, and choose its spin budget from the arrival gap (measured: spinning gave a median latency of 0.7 µs and a consumer that used 99.6 percent of a CPU; sleeping gave 13.7 to 16.6 µs at 2 percent of a CPU);
- pass a descriptor to an unrelated process with `SCM_RIGHTS` and say what the receiver actually gets (measured: the parent's descriptor was 3, the child's was 6, and the read offset was shared);
- create nameless shared memory with `memfd_create` and hand it over by descriptor, and know the limits (measured: 253 descriptors in one message worked, 254 failed with `EINVAL`);
- detect a peer's death with `pidfd_open` and `poll`, without a pid-reuse race (measured: the descriptor became readable 151 ms after a child that slept 150 ms);
- read the kernel's link table through netlink, subscribe to its change events, and see where generic netlink and `nl80211` fit (measured: a full link dump took 5.2 µs for 4 links, and the controller listed 13 generic families);
- assemble and run the complete data plane (measured: 5.68 million messages a second through a ring in a `memfd` passed over a socket, and the server noticed the producer's `SIGKILL` through a `pidfd` 190 µs later).

**Where the numbers come from.** Programs were compiled with GCC 13.3 (`-O2 -Wall -Wextra`) and run as root on a Linux 6.18.44 kernel in a Firecracker micro-VM with 2 vCPUs and 8 GiB of memory (glibc 2.39, x86_64). Six programs were written for this tutorial; their listings or the relevant functions are printed in the text and their output is pasted unedited. Kernel behaviour was checked against the manual pages `eventfd(2)`, `futex(2)`, `futex(7)`, `pidfd_open(2)`, `memfd_create(2)`, `netlink(7)` and `rtnetlink(7)` (fetched for this tutorial) and `unix(7)` (fetched for OS 17), listed under Sources with what each page did and did not confirm.

**Data and timing.** Latencies are medians of five to nine batches of 20,000 to 200,000 operations, with the minimum and maximum printed where they matter. Run-to-run differences of 10 to 20 percent are normal in a VM, and the throughput of the spinning ring variants varied by more than that between two runs (shown in section 4.6). The cost of one spin iteration (a `pause` instruction plus an atomic load) was measured at about 11 ns, which turns a "spin budget" into time: 2,000 iterations is about 22 µs and 20,000 is about 220 µs.

---

## 1. The problem: what a message costs and where it goes

[OS 17](17_ipc_fundamentals.md) measured a one-byte round trip through a pipe at 24,505 ns across CPUs and 2,112 ns on one CPU, a POSIX semaphore at 20.5 µs across CPUs, and a shared flag that one side spins on at 193 ns. Those numbers say that **the copy is not the expensive part of a small message; the wake-up is.** A pipe write copies a few bytes. What takes 20 µs is: the writer calls `write` (a system call), the kernel finds the reader asleep on the pipe, marks it runnable, sends an inter-processor interrupt to its CPU if it is idle, the CPU leaves its idle state, the scheduler runs the reader, and the reader returns from its own `read` system call. Every step is a cost you pay once per wake-up, and a pipe pays it once per message when the reader keeps falling asleep.

Now the arithmetic that fixes the design. Suppose a packet-processing daemon must pass 1,000,000 events a second to a worker process.

| Budget | Value |
| --- | --- |
| time per event | 1 / 1,000,000 s = **1 µs** |
| a cross-CPU wake-up, measured in this VM | about **25 µs** |
| a system call that does no work (from [OS 19](19_user_kernel_boundary.md)) | a few hundred ns on bare metal (from memory) |
| a cache-line transfer between two cores | tens of ns |

A design that wakes the consumer for every event spends 25 times the whole budget on the wake-up alone. A design that never makes a system call per event, and lets the consumer find the data by looking at memory, fits. The three rules that follow from the arithmetic organise this tutorial:

1. **Keep the data path out of the kernel.** Put the messages in shared memory and move them with loads and stores ([OS 17](17_ipc_fundamentals.md) section 5, and section 4 below).
2. **Use the kernel only to sleep and wake**, and only when the consumer is actually idle. The kernel interfaces for that are `eventfd` (section 2) and the futex (section 3).
3. **Use the kernel, with its security and naming, for setup and for events that are rare**: handing over a descriptor (section 5), watching a peer (section 6), reading and changing system state (section 7).

Everything in the next sections is one of those three tools, and section 8 puts them together.

---

## 2. `eventfd`: a counter you can wait on

### 2.1 What it is

`eventfd` creates a descriptor that holds one unsigned 64-bit counter in the kernel. `write` of an 8-byte integer **adds** it to the counter. `read` of 8 bytes returns the counter **and resets it to zero** (counter mode), or returns 1 and subtracts 1 (semaphore mode, `EFD_SEMAPHORE`). A read of an empty counter blocks, or fails with `EAGAIN` if the descriptor is non-blocking. It is not a pipe: no bytes are queued, only a number, so there is no capacity to fill and no message boundary to keep. It is a descriptor, so it works with `poll`, `select` and `epoll`, which a futex cannot.

The program `k1_eventfd.c` checks each rule. The semantics section:

```c
static void sect_semantics(void)
{
    uint64_t v = 0;
    int e = eventfd(0, EFD_NONBLOCK);
    wr(e, 1); wr(e, 1); wr(e, 1);
    const char *s = rd(e, &v);
    printf("counter mode: three writes of 1, one read returns %s, value %llu (the counts are added, one read drains them)\n", s, (unsigned long long)v);
    s = rd(e, &v);
    printf("  a second read on the empty eventfd: %s\n", s);
    wr(e, 5);
    s = rd(e, &v);
    printf("  write 5, read: value %llu\n", (unsigned long long)v);
    close(e);

    e = eventfd(0, EFD_NONBLOCK | EFD_SEMAPHORE);
    wr(e, 3);
    printf("semaphore mode: write 3, then reads return:");
    for (int i = 0; i < 4; i++) { s = rd(e, &v); printf(" %s", strcmp(s, "ok") ? s : (v == 1 ? "1" : "other")); }
    printf("\n");
    close(e);

    e = eventfd(0, EFD_NONBLOCK);
    wr(e, 0xfffffffffffffffeULL);
    errno = 0;
    ssize_t w = write(e, &(uint64_t){ 1 }, sizeof(uint64_t));
    printf("overflow: after writing 0xfffffffffffffffe, writing 1 returned %zd, errno %s (a blocking eventfd would sleep until a read)\n", w, errno == EAGAIN ? "EAGAIN" : strerror(errno));
    errno = 0;
    w = write(e, "abc", 3);
    printf("  a write of 3 bytes (not 8): %zd, errno %s\n", w, errno == EINVAL ? "EINVAL" : strerror(errno));
    close(e);
}
```

and its output:

```text
counter mode: three writes of 1, one read returns ok, value 3 (the counts are added, one read drains them)
  a second read on the empty eventfd: EAGAIN
  write 5, read: value 5
semaphore mode: write 3, then reads return: 1 1 1 EAGAIN
overflow: after writing 0xfffffffffffffffe, writing 1 returned -1, errno EAGAIN (a blocking eventfd would sleep until a read)
  a write of 3 bytes (not 8): -1, errno EINVAL
```

The reading of that output:

- **Counts collapse.** Three writes of 1, one read: the read returned 3. The consumer learns "three things happened" and not "which three". That is the right behaviour for a doorbell ("there is work in the ring") and the wrong one for carrying data. It is also why the pattern in section 4 puts the data in a ring and uses the `eventfd` only to wake the reader.
- **Semaphore mode** hands out one unit per read: write 3, then reads returned 1, 1, 1 and then `EAGAIN`. Use it when each wake-up must correspond to one item and several waiters share the descriptor, which is what a counting semaphore does.
- **The counter has a ceiling** (the largest value is 0xfffffffffffffffe). A write that would pass it blocks, or returns `EAGAIN` on a non-blocking descriptor, as measured. Hitting it means the reader has been absent for 2^64 events, so it is a limit you meet only by a bug.
- **The size is exactly 8.** A 3-byte write failed with `EINVAL`.

### 2.2 It is a descriptor, so it composes with `epoll`

A process that waits for several things (a socket, a timer, a peer's death, "work is ready") needs one wait call, and that call is `epoll_wait`. A futex cannot be put in an `epoll` set; an `eventfd` can. The epoll section:

```c
static void sect_epoll(void)
{
    int e = eventfd(0, EFD_NONBLOCK), ep = epoll_create1(0);
    struct epoll_event ev = { .events = EPOLLIN, .data.fd = e }, out[4];
    epoll_ctl(ep, EPOLL_CTL_ADD, e, &ev);
    printf("epoll on an eventfd: empty -> %d ready; ", epoll_wait(ep, out, 4, 0));
    wr(e, 1);
    printf("after a write -> %d ready; ", epoll_wait(ep, out, 4, 0));
    uint64_t v;
    IGN(read(e, &v, sizeof v));
    printf("after the read -> %d ready\n", epoll_wait(ep, out, 4, 0));
    double t0 = now();
    if (fork() == 0) { usleep(150000); wr(e, 1); _exit(0); }
    int n = epoll_wait(ep, out, 4, 2000);
    printf("  a child writes after 150 ms; the parent's epoll_wait woke after %.0f ms with %d ready (the eventfd is shared through fork)\n", (now() - t0) * 1000, n);
    wait(NULL);
    close(ep); close(e);
}
```

```text
epoll on an eventfd: empty -> 0 ready; after a write -> 1 ready; after the read -> 0 ready
  a child writes after 150 ms; the parent's epoll_wait woke after 150 ms with 1 ready (the eventfd is shared through fork)
```

The `eventfd` shows as ready after a write and not ready after the read drained it (level-triggered, as a pipe would), and the child that wrote after 150 ms woke the parent's `epoll_wait` after 150 ms, with the descriptor shared through `fork`. For a descriptor shared with a process that is **not** a descendant, use descriptor passing (section 5). Event loops in network daemons use exactly this: a worker thread or process writes to an `eventfd`, and the loop's `epoll_wait` returns.

### 2.3 What it costs

One write plus one read of 8 bytes in one thread, nothing waiting, measures the kernel work with no scheduling in it:

```text
one write plus one read of 8 bytes in the same thread, nothing waiting (median of 9 batches of 200000):
  eventfd          307 ns
  pipe             406 ns
  unix socket      552 ns
```

An `eventfd` pair costs 307 ns, a pipe 406 ns and a Unix socket 552 ns. The ordering is the one you would expect (an `eventfd` has no buffer to copy into and no socket state), but the spread is small next to the wake-up. Two `eventfd`s used for a ping-pong between two processes (each side blocks in `read` on its own, writes the other's) give:

```text
  eventfd ping-pong, CPU 0 and CPU 1: round trip 25893 ns (min 22788, max 28545)
  eventfd ping-pong, both on CPU 0: round trip 1824 ns (min 1800, max 3127)
```

That is **25,893 ns across CPUs and 1,824 ns on one CPU**, the same as the pipe and the semaphore of [OS 17](17_ipc_fundamentals.md): the time belongs to the sleep and wake, and not to the kind of descriptor that carries the signal. Choosing `eventfd` over a pipe for a doorbell saves about 100 ns of kernel work and a pipe buffer, and, more importantly, it expresses the intent (a count, not a byte stream) and cannot fill up.

**When to use it:** to wake a thread or process that sleeps in `epoll`; to signal "something happened" from a signal handler (an 8-byte `write` is async-signal-safe); and as the sleep half of the ring in section 4 when the consumer also waits on other descriptors. **When not to:** to carry data, and as a lock.

---
## 3. The futex: sleep only if nothing changed

### 3.1 The problem a futex solves

A lock has two jobs. Uncontended, it must cost almost nothing: a program takes locks billions of times, and a system call each time (`eventfd`'s pair costs 307 ns above) would be a disaster. Contended, it must put the loser to sleep and wake it when the lock is released, because spinning forever wastes a CPU ([OS 06](06_locks.md)). One mechanism serves both: keep the lock **state in user-space memory**, change it with atomic instructions, and call the kernel only to sleep and to wake. That kernel part is the **futex** ("fast user-space mutex"). The manual page states the split directly: the futex operation "occurs entirely in user space for the noncontended case", and the system call is for the case where a thread must block for longer (futex(7) and futex(2), fetched).

### 3.2 The contract

A futex is a 32-bit word, four-byte aligned, in memory. There is one system call, `futex(2)`, with an operation code. Two operations carry the idea:

| Operation | What it does |
| --- | --- |
| `FUTEX_WAIT(addr, expected)` | **Atomically**: if `*addr == expected`, put the caller to sleep on `addr`; otherwise return `EAGAIN` at once. |
| `FUTEX_WAKE(addr, n)` | Wake up to `n` threads sleeping on `addr`; return how many woke. |

The kernel keeps **no value**. The word is yours, and the kernel only keeps a queue of sleepers per address. The atomic check is what makes the design work. Consider the race you would otherwise have: a thread sees the lock held, decides to sleep, and the owner releases and calls "wake" **before** the thread is asleep, so the wake finds nobody and the thread then sleeps forever. With `FUTEX_WAIT(addr, expected)` the thread passes the value it saw. If the owner changed the word in the gap, the kernel's check (done under the kernel's own lock, against the same queue the waker uses) fails and the thread does not sleep. "Sleep only if the word still holds the value I saw" removes the lost wake-up without any lock of its own. The manual page confirms it: "the comparison of that value with the expected value, and the actual blocking will happen atomically".

Glibc does not export a `futex` function, so the program calls the system call directly, with one small wrapper:

```c
static long futex(_Atomic uint32_t *u, int op, uint32_t val, const struct timespec *to)
{
    return syscall(SYS_futex, u, op, val, to, NULL, 0);
}
```

The raw behaviours, in the function `sect_raw`:

```c
static void sect_raw(void)
{
    _Atomic uint32_t w = 7;
    errno = 0;
    long r = futex(&w, FUTEX_WAIT_PRIVATE, 5, NULL);
    printf("FUTEX_WAIT with expected 5 while the word holds 7: returned %ld, errno %s (no sleep: the value check and the sleep are one atomic step in the kernel)\n", r, errno == EAGAIN ? "EAGAIN" : strerror(errno));
    struct timespec to = { 0, 100 * 1000 * 1000 };
    double t0 = now();
    errno = 0;
    r = futex(&w, FUTEX_WAIT_PRIVATE, 7, &to);
    printf("FUTEX_WAIT with expected 7 and a 100 ms timeout: returned %ld, errno %s after %.0f ms\n", r, errno == ETIMEDOUT ? "ETIMEDOUT" : strerror(errno), (now() - t0) * 1000);
    printf("FUTEX_WAKE with nobody waiting: woke %ld\n", futex(&w, FUTEX_WAKE_PRIVATE, 1, NULL));

    _Atomic uint32_t *sh = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    *sh = 0;
    t0 = now();
    pid_t c = fork();
    if (c == 0) {
        while (*sh == 0) futex(sh, FUTEX_WAIT, 0, NULL);
        _exit(0);
    }
    usleep(100000);
    *sh = 1;
    long n = futex(sh, FUTEX_WAKE, 1, NULL);
    waitpid(c, NULL, 0);
    printf("shared futex across fork (no PRIVATE flag): child slept on a MAP_SHARED word, parent set it to 1 and woke %ld waiter; child exited after %.0f ms\n", n, (now() - t0) * 1000);

    _Atomic uint32_t *pw = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    *pw = 0;
    t0 = now();
    c = fork();
    if (c == 0) { futex(pw, FUTEX_WAIT_PRIVATE, 0, NULL); _exit(0); }
    usleep(100000);
    *pw = 1;
    n = futex(pw, FUTEX_WAKE_PRIVATE, 1, NULL);
    usleep(100000);
    int alive = waitpid(c, NULL, WNOHANG) == 0;
    printf("same test with the PRIVATE flag: the wake reported %ld waiters woken, and the child is %s (a private futex is keyed by address in one address space, so the parent's wake missed it)\n", n, alive ? "still asleep" : "gone");
    if (alive) { kill(c, SIGKILL); waitpid(c, NULL, 0); }
}
```

```text
FUTEX_WAIT with expected 5 while the word holds 7: returned -1, errno EAGAIN (no sleep: the value check and the sleep are one atomic step in the kernel)
FUTEX_WAIT with expected 7 and a 100 ms timeout: returned -1, errno ETIMEDOUT after 100 ms
FUTEX_WAKE with nobody waiting: woke 0
shared futex across fork (no PRIVATE flag): child slept on a MAP_SHARED word, parent set it to 1 and woke 1 waiter; child exited after 101 ms
same test with the PRIVATE flag: the wake reported 0 waiters woken, and the child is still asleep (a private futex is keyed by address in one address space, so the parent's wake missed it)
```

The four lines show the whole contract:

1. A wait with the **wrong expected value** (5 while the word holds 7) returned `EAGAIN` and did not sleep.
2. A wait with the right value and a 100 ms timeout slept and returned `ETIMEDOUT` after 100 ms. The timeout is **relative** for `FUTEX_WAIT` (measured: it expired after 100 ms from now), and it is measured on `CLOCK_MONOTONIC` unless a real-time clock flag is given (futex(2), fetched).
3. A wake with nobody asleep returned 0 and is harmless, which is why a release path can call it speculatively.
4. A **shared** futex (no `PRIVATE` flag) in a `MAP_SHARED` page woke a child in another process, and the child left after 101 ms (the 100 ms of the parent's `usleep` plus the wake).
5. A **private** futex (the `FUTEX_PRIVATE_FLAG` operations) did **not**: the wake reported 0 woken and the child stayed asleep, because a private futex is keyed by the address inside one address space, and the child, after `fork`, is a different address space with the same address. The manual page says a private futex is "process-private and not shared with another process" and allows kernel optimisations. Rule: use the `PRIVATE` forms for threads of one process (the common case, and faster, from the page's wording), and the shared forms for a word in memory mapped by several processes.

### 3.3 Building a mutex from it

The standard teaching design (from Ulrich Drepper's paper "Futexes Are Tricky", from memory) uses three states in the word: **0** unlocked, **1** locked with no waiters, **2** locked and someone may be asleep. The listing:

```c
typedef struct { _Atomic uint32_t s; } fmutex;
static _Atomic long wait_calls, wake_calls;

static void fm_lock(fmutex *m, int shared)
{
    uint32_t c = 0;
    if (atomic_compare_exchange_strong(&m->s, &c, 1)) return;
    if (c != 2) c = atomic_exchange(&m->s, 2);
    while (c != 0) {
        atomic_fetch_add(&wait_calls, 1);
        futex(&m->s, shared ? FUTEX_WAIT : FUTEX_WAIT_PRIVATE, 2, NULL);
        c = atomic_exchange(&m->s, 2);
    }
}

static void fm_unlock(fmutex *m, int shared)
{
    if (atomic_exchange(&m->s, 0) == 2) {
        atomic_fetch_add(&wake_calls, 1);
        futex(&m->s, shared ? FUTEX_WAKE : FUTEX_WAKE_PRIVATE, 1, NULL);
    }
}
```

Read it with the states in mind.

- **Lock, fast path:** a compare-and-swap from 0 to 1. If it succeeds the lock was free and no system call happened. This is the whole cost of an uncontended lock.
- **Lock, slow path:** the lock is held. Move the state to 2 (an atomic exchange), which announces "there is a waiter". If the exchange returned 0 the lock became free in the meantime and the thread now owns it (with the state 2, which is safe, only slightly pessimistic). Otherwise call `FUTEX_WAIT(&state, 2)`. It sleeps only if the state is still 2, so a release between the exchange and the wait is not lost. After waking, exchange to 2 again and test: the woken thread cannot know whether others still sleep, so it must leave the state at 2.
- **Unlock:** exchange the state to 0. If the old value was 2, someone may be asleep, so call `FUTEX_WAKE(&state, 1)`. If it was 1, nobody can be asleep and no system call happens.

The measurements:

```text
  uncontended lock+unlock, hand-built:     14.1 ns   (futex calls: 0)
  uncontended lock+unlock, pthread mutex:   5.3 ns   (futex calls: 0)
```

An uncontended lock and unlock of the hand-built mutex took 14.1 ns with **zero** futex calls, against 5.3 ns for the glibc mutex. The roughly 9 ns gap I did not decompose: glibc's fast path is tuned and mine uses sequentially consistent atomics, so I treat the comparison as "both are tens of nanoseconds, with no kernel entry", which is the point, and not as a verdict on either. Compare **14.1 ns against 307 ns** for just writing and reading an `eventfd` once: the futex design makes the common case an order of magnitude cheaper than any system call.

Now two threads hammering one lock:

```c
static void *worker(void *arg)
{
    long n = (long)arg;
    for (long i = 0; i < n; i++) {
        fm_lock(&G->m, 1);
        G->counter++;
        fm_unlock(&G->m, 1);
    }
    return NULL;
}

static void sect_contended(void)
{
    enum { N = 1000000 };
    G = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    atomic_store(&wait_calls, 0); atomic_store(&wake_calls, 0);
    /* Two threads, one on each CPU. The counters are process-local atomics; with threads they are shared. */
    pthread_t t[2];
    double t0 = now();
    for (int i = 0; i < 2; i++) pthread_create(&t[i], NULL, worker, (void *)(long)N);
    for (int i = 0; i < 2; i++) pthread_join(t[i], NULL);
    double dt = now() - t0;
    printf("  two threads, %d lock+unlock each on a hand-built mutex: counter %ld (expected %d), %.1f ns per critical section, FUTEX_WAIT calls %ld, FUTEX_WAKE calls %ld\n",
           N, G->counter, 2 * N, dt / (2 * N) * 1e9, (long)wait_calls, (long)wake_calls);
    printf("  fraction of acquisitions that entered the kernel to wait: %.3f%%\n", 100.0 * wait_calls / (2.0 * N));
}
```

```text
  two threads, 1000000 lock+unlock each on a hand-built mutex: counter 2000000 (expected 2000000), 56.5 ns per critical section, FUTEX_WAIT calls 59296, FUTEX_WAKE calls 113029
  fraction of acquisitions that entered the kernel to wait: 2.965%
```

The counter ended at 2,000,000 (correct), a critical section took 56.5 ns, and only **2.965 percent** of acquisitions ever called `FUTEX_WAIT` (59,296 calls in 2,000,000 acquisitions). That figure is the second reason futexes work: with critical sections this short, the lock is usually free again by the time a contender looks a second time, so the kernel is entered rarely (the explanation is the standard one; I did not instrument how often the second look succeeded).

Notice one more number: **113,029 wake calls, nearly twice the waits.** The cause is the third state. After any thread has waited, it sets the state to 2 and leaves it there, so the **next** unlocks see 2 and call `FUTEX_WAKE` even when nothing sleeps. Each of those is a system call that finds an empty queue. This is the price of a design with only three states and no waiter count, and glibc's mutex has the same shape. (A design that stores the number of waiters pays for it with more atomic work on every operation.) Two threads and a tight loop are the worst case for this effect; with longer critical sections or less contention it shrinks.

### 3.4 What else is built on it

The facts below are from memory unless marked, because the pages I fetched do not describe them:

- glibc builds `pthread_mutex_t`, `pthread_cond_t`, `sem_t`, `pthread_rwlock_t` and `pthread_barrier_t` on futexes. [OS 17](17_ipc_fundamentals.md) measured a process-shared mutex under contention at 55 ns per increment and a semaphore at 65 ns, which match the 56.5 ns here: they are all this design.
- A mutex with `PTHREAD_PROCESS_SHARED` uses the shared (non-`PRIVATE`) futex operations on a word in shared memory, which is why it works between processes.
- **Robust mutexes** (the `EOWNERDEAD` recovery of OS 17) use a per-thread "robust list" registered with `set_robust_list(2)` that the kernel walks at thread exit, marking the word and waking a waiter. **Priority-inheritance mutexes** use separate operations (`FUTEX_LOCK_PI`) so the kernel can boost the owner. futex(7) mentions neither, so treat both as from memory.
- A futex cannot be put in `epoll`. If the sleeper must also wait for a socket or a timer, use an `eventfd` (section 2) or let the loop block in `epoll_wait` and use the futex only in threads that wait for nothing else.

**When to use a raw futex yourself:** almost never for a lock (use the pthread one) and sometimes for a small custom primitive, such as the "consumer sleeps on the ring's tail word" of section 4. **The rule to remember:** the word and the check belong to you; the kernel only makes "check, then sleep" atomic.

---
## 4. A shared-memory data plane: how the consumer waits

### 4.1 The shape

[OS 17](17_ipc_fundamentals.md) built a single-producer single-consumer ring in shared memory and spun on it: no system call, 193 ns round trip, 18.4 million 64-byte messages a second, and two CPUs at 100 percent. That is the right design only if the CPUs are free to burn. A real service must also **idle**: when no messages arrive, the consumer should sleep and use no CPU, and when one arrives, it should wake in the fewest microseconds. The question this section answers is the one every low-latency system faces: **what does the consumer do when the ring is empty?** There are five answers, and the program `k3_sleep.c` runs them all on the same ring.

The ring has 1,024 slots of one cache line each, and three indices on three separate cache lines (separate so that the producer's writes to `tail` do not invalidate the line the consumer reads `head` from; [OS 17](17_ipc_fundamentals.md) section 5.5):

```c
enum { RN = 1024 };                                /* slots, a power of two */
enum kind { SPIN, EFD_ALWAYS, EFD_FLAG, FUTEX };

struct msg { double ts; uint64_t seq; char pad[48]; };   /* one cache line */

struct ring {
    _Alignas(64) _Atomic uint32_t head;            /* next slot to read, written by the consumer */
    _Alignas(64) _Atomic uint32_t tail;            /* next slot to write, written by the producer; also the futex word */
    _Alignas(64) _Atomic uint32_t sleeping;        /* the consumer is about to block, or is blocked */
    _Atomic uint64_t notes;                        /* wake-up system calls the producer made */
    int efd;
    struct msg slot[RN];
};
```

`tail` is a 32-bit counter that only grows, and it doubles as the **futex word**: a consumer that sleeps on the futex sleeps on `tail`, expecting it to change. `sleeping` is the flag that tells the producer whether a wake-up is needed. `efd` is an `eventfd` for the eventfd variants.

The five strategies, as the program names them:

| Strategy | What the consumer does when the ring is empty | What the producer does after pushing |
| --- | --- | --- |
| spin only | loops on `tail`, never sleeps | nothing |
| eventfd on every message | blocks in `read` on the `eventfd` | **writes** the `eventfd` every time |
| eventfd, only if sleeping | sets `sleeping`, re-checks, then blocks in `read` | writes the `eventfd` only if it sees `sleeping` set |
| futex, only if sleeping | sets `sleeping`, re-checks, then `FUTEX_WAIT` on `tail` | `FUTEX_WAKE` only if it sees `sleeping` set |
| spin N, then futex | spins up to N iterations, then as the row above | as the row above |

### 4.2 The protocol that never loses a wake-up

The two functions that implement every strategy:

```c
static void push(struct ring *r, const struct strat *s, const struct msg *m)
{
    uint32_t t = atomic_load_explicit(&r->tail, memory_order_relaxed);
    while (t - atomic_load_explicit(&r->head, memory_order_acquire) == RN) relax();
    r->slot[t % RN] = *m;
    atomic_store(&r->tail, t + 1);                 /* seq_cst: must be ordered before the load of sleeping */
    switch (s->kind) {
    case SPIN: break;
    case EFD_ALWAYS: {
        uint64_t one = 1;
        IGN(write(r->efd, &one, 8));
        r->notes++;
        break;
    }
    case EFD_FLAG:
        if (atomic_load(&r->sleeping)) {
            uint64_t one = 1;
            IGN(write(r->efd, &one, 8));
            r->notes++;
        }
        break;
    case FUTEX:
        if (atomic_load(&r->sleeping)) {
            futex(&r->tail, FUTEX_WAKE, 1);
            r->notes++;
        }
        break;
    }
}

static void pop(struct ring *r, const struct strat *s, struct msg *m)
{
    for (;;) {
        uint32_t h = atomic_load_explicit(&r->head, memory_order_relaxed);
        if (atomic_load_explicit(&r->tail, memory_order_acquire) != h) {
            *m = r->slot[h % RN];
            atomic_store_explicit(&r->head, h + 1, memory_order_release);
            return;
        }
        if (s->kind == SPIN) { relax(); continue; }
        if (s->kind == EFD_ALWAYS) {               /* no flag: every push wrote the eventfd */
            uint64_t v;
            IGN(read(r->efd, &v, 8));
            continue;
        }
        for (long i = 0; i < s->budget; i++) {     /* spin phase; budget 0 skips it */
            if (atomic_load_explicit(&r->tail, memory_order_acquire) != h) goto again;
            relax();
        }
        atomic_store(&r->sleeping, 1);             /* seq_cst: publish the intent, then look again */
        if (atomic_load(&r->tail) != h) { atomic_store(&r->sleeping, 0); continue; }
        if (s->kind == FUTEX) {
            futex(&r->tail, FUTEX_WAIT, h);        /* sleeps only if tail still equals h */
        } else {
            uint64_t v;
            IGN(read(r->efd, &v, 8));
        }
        atomic_store(&r->sleeping, 0);
    again:;
    }
}
```

The interesting part is the **flag handshake**. The producer must not make a system call per message (that is what rule 1 of section 1 forbids), so it calls the kernel only if the consumer announced that it is about to sleep. The danger is the gap between the consumer's "the ring is empty" and its "I am asleep", during which the producer can push and see no sleeper:

```text
consumer                               producer
--------                               --------
sees tail == head (empty)
                                       writes the slot, publishes tail
                                       reads sleeping: 0, so no wake-up
sets sleeping = 1
sleeps, forever              <- the message is in the ring, nobody is told
```

The fix is the order in the code, and it is the same idea as the futex's atomic check, applied one level up:

1. The consumer **publishes its intent first**: `sleeping = 1`.
2. It then **looks again**: if `tail` moved, it clears the flag and takes the message.
3. Only if the ring is still empty does it block.
4. The producer does the mirror image: **publish the data first** (`tail`), then **read the flag**.

Whichever happens first, one side sees the other. If the producer's flag read comes before the consumer's flag write, then the producer's `tail` store is earlier still, so the consumer's re-check in step 2 sees the new `tail`. If the flag write comes first, the producer sees it and wakes. This is the store-then-load pattern (each side stores one variable, then loads the other's), and it is exactly the case where **acquire and release are not enough**: they order a store before a *later store* or a load after an *earlier load*, not a store before a later load. The code uses sequentially consistent operations (the default for `atomic_store` and `atomic_load` without an `_explicit` order) on `tail`, on the flag stores and on the flag loads, which puts a full barrier between each store and the next load ([OS 08](08_memory_barriers.md)). Without it the CPU may let the consumer's re-check read a stale `tail` while the producer reads a stale flag, and the message is lost. I did not try to provoke that on this machine; it is the reasoning of the memory model, not a measurement.

Two details:

- The **futex variant has a second check**, which narrows the window but does not replace the barriers. `FUTEX_WAIT(&tail, h)` sleeps only if `tail` still equals `h`, so a push whose `tail` store is already visible makes the wait return `EAGAIN`. But a producer whose store is still in its store buffer can read `sleeping` as 0 and skip the wake, while the kernel's check still reads the old `tail`; the window is short (the store drains in nanoseconds, the system call takes far longer) but the memory model does not close it, so the barriers stay. This is reasoning, not something I measured.
- The **eventfd variants rely on the counter instead**. A write to an `eventfd` that happens before the consumer's `read` is not lost: the counter holds it and the `read` returns at once. A stale count can wake the consumer when the ring is already empty, which is why `pop` loops and tests `tail` again after every wake-up. Spurious wake-ups are normal and must be harmless.

### 4.3 Latency of a round trip

Two rings, one each way, one message in flight: the parent pushes to the child, the child pushes back, and the parent measures the round trip (median of five batches of 20,000).

```text
ring round trip, CPU 0 and CPU 1
  spin only                      round trip     856 ns  (min 783, max 869)
  eventfd on every message       round trip   25362 ns  (min 24788, max 25778)
  eventfd, only if sleeping      round trip   25630 ns  (min 25344, max 26533)
  futex, only if sleeping        round trip   25288 ns  (min 24812, max 28644)
  spin 200, then futex           round trip    1254 ns  (min 992, max 2808)
  spin 2000, then futex          round trip    1207 ns  (min 1116, max 1240)
  spin 20000, then futex         round trip    1146 ns  (min 962, max 7743)
  spin 200000, then futex        round trip    1010 ns  (min 868, max 1034)
```

The shape is the one the arithmetic of section 1 predicted. Every strategy that **sleeps** costs about **25 µs** across CPUs (25,362, 25,630 and 25,288 ns): they are the same wake-up as the pipe and the semaphore of OS 17, and the flag saves nothing here because the consumer is asleep every time, so the producer calls the kernel every time. The strategies that **spin first** are about 20 times faster, **1.0 to 1.25 µs** (the hybrids), against 856 ns for pure spinning. The hybrids are slower than pure spinning by 150 to 400 ns. The likely cause is the extra sequentially consistent loads and stores of the flag protocol on each message, but I did not take that apart, so it is a guess.

The same test with both processes on **one CPU** (spinning variants are skipped: a spinner on the CPU of the thread it waits for burns its whole time slice):

```text
ring round trip, both processes on CPU 0
  spin only                      skipped (a spinner on the CPU of the thread it waits for)
  eventfd on every message       round trip    1844 ns  (min 1747, max 2015)
  eventfd, only if sleeping      round trip    1623 ns  (min 1518, max 1816)
  futex, only if sleeping        round trip    1793 ns  (min 1749, max 1878)
  spin 200, then futex           skipped (a spinner on the CPU of the thread it waits for)
  spin 2000, then futex          skipped (a spinner on the CPU of the thread it waits for)
  spin 20000, then futex         skipped (a spinner on the CPU of the thread it waits for)
  spin 200000, then futex        skipped (a spinner on the CPU of the thread it waits for)
```

On one CPU every sleeping strategy lands at **1.6 to 1.8 µs**, within noise of the plain `eventfd` ping-pong of section 2 (1,824 ns). The ring adds nothing there, which is the right result: when the consumer has to sleep anyway, the cost is the context switch, and the shared memory is for the throughput.

### 4.4 Latency and CPU of a sparse stream

A round trip hides the real tradeoff, which is between **latency** and **CPU time used while waiting**. The second test sends one message every 200 µs (the producer busy-waits to pace) and records, for each message, the time from the producer's timestamp to the moment the consumer pops it (`CLOCK_MONOTONIC` is the same clock in both processes), plus the consumer's CPU time as a fraction of the wall time. It then repeats with a message every 20 µs.

```text
one message every 200 us (the producer busy-waits to pace), 3000 messages, timestamp to pop
  spin only                      median     0.7 us  p99     5.3 us  max   1319.0 us  consumer CPU  99.6% of wall
  eventfd on every message       median    15.5 us  p99    91.4 us  max    476.5 us  consumer CPU   2.1% of wall
  eventfd, only if sleeping      median    16.6 us  p99    92.9 us  max    543.5 us  consumer CPU   2.2% of wall
  futex, only if sleeping        median    13.7 us  p99   104.6 us  max   1421.6 us  consumer CPU   1.9% of wall
  spin 200, then futex           median    14.4 us  p99    70.5 us  max    775.6 us  consumer CPU   3.7% of wall
  spin 2000, then futex          median    13.7 us  p99   120.3 us  max   1361.3 us  consumer CPU  16.9% of wall
  spin 20000, then futex         median     0.7 us  p99    87.0 us  max    174.2 us  consumer CPU  96.7% of wall
  spin 200000, then futex        median     0.7 us  p99    22.2 us  max    399.7 us  consumer CPU  99.8% of wall
one message every 20 us (the producer busy-waits to pace), 3000 messages, timestamp to pop
  spin only                      median     0.5 us  p99    10.4 us  max     67.9 us  consumer CPU  99.7% of wall
  eventfd on every message       median    11.2 us  p99   163.3 us  max    427.1 us  consumer CPU  12.4% of wall
  eventfd, only if sleeping      median    12.0 us  p99    85.8 us  max   2306.1 us  consumer CPU  16.5% of wall
  futex, only if sleeping        median    12.5 us  p99   105.5 us  max    424.5 us  consumer CPU  12.8% of wall
  spin 200, then futex           median     9.6 us  p99    19.3 us  max     55.8 us  consumer CPU  23.4% of wall
  spin 2000, then futex          median     0.6 us  p99    42.0 us  max    501.3 us  consumer CPU  97.7% of wall
  spin 20000, then futex         median     0.6 us  p99   113.3 us  max    672.0 us  consumer CPU  97.4% of wall
  spin 200000, then futex        median     0.7 us  p99   381.0 us  max    979.2 us  consumer CPU  97.2% of wall
```

Read the 200 µs block first.

- **Spin only:** median **0.7 µs**, and the consumer used **99.6 percent** of a CPU to do it.
- **The sleepers** (eventfd every message, eventfd if sleeping, futex if sleeping) have a median of **13.7 to 16.6 µs** and a p99 around 90 to 105 µs, and use **about 2 percent** of a CPU. The 13.7 to 16.6 µs is the wake-up from an idle CPU; the long tail is the occasional slow wake (the largest values, 0.5 to 1.4 ms, are single messages and I did not chase them).
- **Spin 200 and spin 2,000 then futex** behave like the sleepers (median 14.4 and 13.7 µs). Their budgets are 200 iterations (about 2 µs) and 2,000 (about 22 µs), and the next message arrives after 200 µs, so the spin **never catches it**. It only adds CPU: 16.9 percent for a budget of 2,000, against 1.9 percent for no spin at all, for no improvement in latency.
- **Spin 20,000 then futex** (about 220 µs) is longer than the gap, so every message is caught while spinning: median **0.7 µs**, like pure spinning, and CPU **96.7 percent**. The consumer is, in effect, spinning all the time.

The 20 µs block shows the same cliff one level down. A budget of 200 (2 µs) catches some messages (median 9.6 µs, 23.4 percent CPU, a median between the sleep and the spin latencies; I did not establish why, and one plausible reason, from memory, is that a CPU woken every 20 µs stays in a shallower idle state than one woken every 200 µs, so its wake-up is faster). A budget of 2,000 (22 µs) is longer than the gap, and the median drops to 0.6 µs at 97.7 percent CPU.

**The lesson in one line: a spin budget is all or nothing.** If it is longer than the gap between messages you pay (nearly) a full CPU and get spin latency. If it is shorter you pay a fraction of a CPU for the spin, and then the sleep latency anyway. The useful budgets are therefore the ones that match a measured gap distribution, not an arbitrary 1,000.

### 4.5 Choosing the budget

There is a principled way to pick it. Spinning costs CPU time at a known rate; sleeping costs a fixed price (two system calls and a wake-up). If you knew the next arrival time you would spin when it is closer than the sleep price and sleep when it is further. You do not know it, and the standard answer (the "ski rental" argument, from memory) is to **spin for about as long as the sleep-and-wake costs**, then sleep: whichever way the future turns out, you have paid at most about twice the best fixed choice. Here the sleep-and-wake price is about 14 µs of latency, so a budget of roughly 1,300 iterations (14 µs at 11 ns each) is the break-even, and a budget of that order is exactly what **did not help** in the 200 µs test (2,000 iterations), because that test's gaps are all far longer than the break-even. Real traffic is bursty: messages come in trains with short gaps inside the train and long gaps between trains, and the same budget then spins through the short gaps and sleeps through the long ones. That is the case hybrids are built for. Practical rules:

1. **Measure the gap distribution** of the real workload and set the budget to a high percentile of the *intra-train* gaps, not to the mean.
2. **Make the budget adaptive** (grow it when spinning succeeded, shrink it when it failed) if the load varies; glibc has an "adaptive" mutex type that spins briefly before it sleeps, and many runtimes tune the spin the same way (from memory).
3. **Never spin without a time bound**, and never on a CPU you share with the thread you wait for (the same-CPU test above skips spinners for that reason).
4. **Count the CPU you spend.** A spinning consumer on a shared or virtualised host also steals from its neighbours, and if the host deschedules the producer's virtual CPU mid-spin, the consumer spins for nothing (from memory; a VM effect I did not measure).

### 4.6 Throughput of a burst

When the producer sends 2,000,000 messages as fast as it can, the question changes to **how many system calls does the notification cost per message**. The first run:

```text
burst of 2,000,000 messages, producer as fast as it can
  spin only                        3.28 M msgs/s   wake-up calls         0   consumer CPU 100.2% of wall
  eventfd on every message         1.46 M msgs/s   wake-up calls   2000000   consumer CPU  28.4% of wall
  eventfd, only if sleeping        1.99 M msgs/s   wake-up calls   1147013   consumer CPU  57.9% of wall
  futex, only if sleeping          2.25 M msgs/s   wake-up calls    895418   consumer CPU  89.6% of wall
  spin 200, then futex             4.27 M msgs/s   wake-up calls     26880   consumer CPU  91.7% of wall
  spin 2000, then futex            4.29 M msgs/s   wake-up calls      3248   consumer CPU  98.9% of wall
  spin 20000, then futex           4.86 M msgs/s   wake-up calls       750   consumer CPU  99.5% of wall
  spin 200000, then futex          3.74 M msgs/s   wake-up calls       343   consumer CPU  99.7% of wall
```

The three runs, side by side, millions of messages per second:

| Strategy | Throughput, three runs (M msgs/s) | Wake-up calls in the three runs |
| --- | --- | --- |
| spin only | 3.28 / 3.86 / 4.16 | none |
| eventfd on every message | 1.46 / 1.44 / 1.41 | 2,000,000 / 2,000,000 / 2,000,000 |
| eventfd, only if sleeping | 1.99 / 1.84 / 2.02 | 1,147,013 / 1,228,510 / 1,163,065 |
| futex, only if sleeping | 2.25 / 2.04 / 2.24 | 895,418 / 988,068 / 911,717 |
| spin 200, then futex | 4.27 / 44.55 / 4.00 | 26,880 / 904 / 12,515 |
| spin 2000, then futex | 4.29 / 42.71 / 4.43 | 3,248 / 189 / 574 |
| spin 20000, then futex | 4.86 / 3.75 / 4.10 | 750 / 1,128 / 452 |
| spin 200000, then futex | 3.74 / 3.93 / 4.64 | 343 / 65 / 1,844 |

What the three runs support:

- **Waking on every message is producer-bound.** The producer makes one `eventfd` write per message, 2,000,000 system calls, and throughput is 1.4 to 1.5 million a second, about 700 ns per message. The consumer is idle most of the time (28 to 33 percent CPU).
- **Waking only when the consumer sleeps helps by about 30 to 60 percent** (1.8 to 2.3 million a second) and still makes **about 0.9 to 1.2 million** wake-up calls, because a fast producer and a consumer that drains the ring in microseconds and then sleeps again repeat the cycle: wake, drain, sleep, wake. The flag removes the wake-ups for messages pushed while the consumer is awake, which is only some of them.
- **A spin phase collapses the calls to hundreds or thousands** (from 65 to 26,880 over the three runs, and below 1,900 in all but three of the 12 hybrid measurements) and moves throughput to about 4 million a second, the same as pure spinning. A burst has no long gaps, so the spin phase catches every message.
- **The spinning variants are not stable.** Most spinning measurements sit between 3.3 and 4.9 million a second, but two of the 15 spinning measurements in the three runs (spin 200 and spin 2,000 in the second run) came out at **44.6 and 42.7 million a second**, ten times higher, with the same code. A reasonable reading is that the ring settled into a regime where the producer runs ahead and the consumer reads many already-cached slots per cache-line transfer, against a regime where each message moves the lines back and forth in lockstep. I did not isolate the cause, so treat it as a hypothesis, and treat "about 4 million a second" as the number to design with. The standard remedy, from memory, is for each side to cache the other side's index and publish its own in batches, which is what production rings do; OS 17's 64-byte ring reached 18.4 million a second without it.

### 4.7 A summary of the five

| You want | Use | Cost to know |
| --- | --- | --- |
| lowest latency, CPU is free | spin only | one CPU at 100 percent per consumer |
| lowest CPU when idle, latency of 14 to 17 µs is fine | futex or eventfd, only if sleeping | about 2 percent CPU; one wake-up per sleep |
| waiting on several things at once | `eventfd` in `epoll` | the counter coalesces; one syscall to read it |
| low latency for bursts or steady load, idle when quiet | spin N, then futex | budget must match the intra-burst gap |

One caution about the code above: the struct stores the `eventfd` **number** in the shared ring, which works in this program only because the child inherited the descriptor and kept its number. Two unrelated processes get different numbers for the same descriptor (section 5), so a real design keeps descriptor numbers out of shared memory and passes them separately, as section 8 does.

---
## 5. Passing descriptors between processes

### 5.1 What is passed

A descriptor is a small integer, an index into the calling process's table. The entry points at an **open file description** in the kernel, which holds the file offset, the flags and the pointer to the file, socket or pipe ([OS 15](15_file_systems.md)). Two processes cannot share the integer, because it means nothing in the other's table. What a Unix domain socket can do is carry an **ancillary message** of type `SCM_RIGHTS` alongside the data. The kernel takes the descriptors named in the message, and, when the receiver reads it, **installs new table entries in the receiver that point at the same open file descriptions**. The receiver gets a new integer, usually different, for the same underlying object.

The two helpers (a sender and a receiver, both of which must also move one byte of ordinary data, which `sendmsg` carries in `iov`):

```c
static ssize_t send_fds(int sock, const int *fds, int count)
{
    char byte = 'F';
    struct iovec iov = { &byte, 1 };
    char ctl[CMSG_SPACE(sizeof(int) * 256)];
    memset(ctl, 0, sizeof ctl);
    struct msghdr mh = { .msg_iov = &iov, .msg_iovlen = 1, .msg_control = ctl,
                         .msg_controllen = CMSG_SPACE(sizeof(int) * count) };
    struct cmsghdr *c = CMSG_FIRSTHDR(&mh);
    c->cmsg_level = SOL_SOCKET;
    c->cmsg_type = SCM_RIGHTS;
    c->cmsg_len = CMSG_LEN(sizeof(int) * count);
    memcpy(CMSG_DATA(c), fds, sizeof(int) * count);
    return sendmsg(sock, &mh, 0);
}

static int recv_fds(int sock, int *fds, int cap, int *flags)
{
    char byte;
    struct iovec iov = { &byte, 1 };
    char ctl[CMSG_SPACE(sizeof(int) * 256)];
    struct msghdr mh = { .msg_iov = &iov, .msg_iovlen = 1, .msg_control = ctl,
                         .msg_controllen = CMSG_SPACE(sizeof(int) * cap) };
    ssize_t n = recvmsg(sock, &mh, MSG_CMSG_CLOEXEC);
    if (n <= 0) return -1;
    if (flags) *flags = mh.msg_flags;
    for (struct cmsghdr *c = CMSG_FIRSTHDR(&mh); c; c = CMSG_NXTHDR(&mh, c))
        if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS) {
            int k = (c->cmsg_len - CMSG_LEN(0)) / sizeof(int);
            memcpy(fds, CMSG_DATA(c), sizeof(int) * k);
            return k;
        }
    return 0;
}
```

The first program opens a file, reads four bytes, passes the descriptor to a child, and the child reads four bytes through its copy:

```c
static void sect_shared_offset(void)
{
    char path[] = "/tmp/k4_XXXXXX";
    int fd = mkstemp(path);
    IGN(write(fd, "0123456789abcdef", 16));
    lseek(fd, 0, SEEK_SET);
    int sv[2];
    IGN(socketpair(AF_UNIX, SOCK_STREAM, 0, sv));
    char buf[5] = { 0 };
    IGN(read(fd, buf, 4));
    printf("parent: opened a file as fd %d and read \"%s\"\n", fd, buf);
    pid_t c = fork();
    if (c == 0) {
        int got[1];
        int n = recv_fds(sv[1], got, 1, NULL);
        char b2[5] = { 0 };
        IGN(read(got[0], b2, 4));
        printf("child:  received %d descriptor as fd %d (the parent's was %d), read \"%s\"\n", n, got[0], fd, b2);
        close(got[0]);
        _exit(0);
    }
    send_fds(sv[0], &fd, 1);
    waitpid(c, NULL, 0);
    IGN(read(fd, buf, 4));
    printf("parent: read again, got \"%s\" (the two descriptors share one open file description, so one offset)\n", buf);
    unlink(path);
    close(fd); close(sv[0]); close(sv[1]);
}
```

```text
parent: opened a file as fd 3 and read "0123"
child:  received 1 descriptor as fd 6 (the parent's was 3), read "4567"
parent: read again, got "89ab" (the two descriptors share one open file description, so one offset)
```

The parent's descriptor was **3**; the child got **6**. The parent had read `0123`, the child, reading through its own number, got `4567`, and the parent's next read gave `89ab`. **One open file description means one offset**, shared by both. That is the same rule as `fork` and `dup` ([OS 15](15_file_systems.md)), now across unrelated processes. The consequences:

- A descriptor passed as read-only stays read-only. The receiver gets exactly the access that was checked **when the sender opened it**, which is why passing is the basis of **privilege separation**: a privileged process opens a file, a device or a listening port, and hands the descriptor to a worker that has no right to open it by name (the pattern used by privilege-separated daemons and by socket activation; from memory).
- Closing the descriptor in one process does not close it in the other. The object lives until the last descriptor to it goes.
- Pass only what the receiver should be able to use, and have the receiver check what it got (`fstat` for the type) before trusting it.

### 5.2 Limits, measured

```c
static void sect_limits(void)
{
    int sv[2];
    IGN(socketpair(AF_UNIX, SOCK_STREAM, 0, sv));
    int fds[256];
    for (int i = 0; i < 256; i++) fds[i] = eventfd(0, 0);
    ssize_t r = send_fds(sv[0], fds, 253);
    printf("sendmsg with 253 descriptors: %zd\n", r);
    int got[256], fl = 0;
    int k = recv_fds(sv[1], got, 256, &fl);
    printf("  receiver got %d descriptors, MSG_CTRUNC %s\n", k, (fl & MSG_CTRUNC) ? "set" : "clear");
    for (int i = 0; i < k; i++) close(got[i]);
    errno = 0;
    r = send_fds(sv[0], fds, 254);
    printf("sendmsg with 254 descriptors: %zd, errno %s\n", r, r < 0 ? (errno == EINVAL ? "EINVAL" : strerror(errno)) : "none");

    r = send_fds(sv[0], fds, 3);
    k = recv_fds(sv[1], got, 1, &fl);
    printf("3 sent, receiver's buffer holds 1: got %d, MSG_CTRUNC %s (room for one int is padded to room for two; any descriptor that does not fit is closed by the kernel, not queued)\n", k, (fl & MSG_CTRUNC) ? "set" : "clear");
    for (int i = 0; i < k; i++) close(got[i]);
    for (int i = 0; i < 256; i++) close(fds[i]);

    enum { N = 100000 };
    int e = eventfd(0, 0);
    double t0 = now();
    for (int i = 0; i < N; i++) {
        int g[1];
        send_fds(sv[0], &e, 1);
        recv_fds(sv[1], g, 1, NULL);
        close(g[0]);
    }
    printf("send one descriptor, receive it, close it, in one process: %.0f ns per cycle\n", (now() - t0) / N * 1e9);
    close(e); close(sv[0]); close(sv[1]);
}
```

```text
sendmsg with 253 descriptors: 1
  receiver got 253 descriptors, MSG_CTRUNC clear
sendmsg with 254 descriptors: -1, errno EINVAL
3 sent, receiver's buffer holds 1: got 2, MSG_CTRUNC set (room for one int is padded to room for two; any descriptor that does not fit is closed by the kernel, not queued)
send one descriptor, receive it, close it, in one process: 1494 ns per cycle
```

- **253 descriptors fit in one message; 254 fails with `EINVAL`.** The cap of 253 is the `SCM_MAX_FD` limit of `unix(7)` (from memory; I measured the effect, not read the constant).
- **The receiver's control buffer decides how many it accepts.** Three descriptors were sent and the buffer was sized with `CMSG_SPACE` for one `int`, and the receiver got **two** descriptors with `MSG_CTRUNC` set. The reason is alignment: `CMSG_SPACE(sizeof(int))` rounds up, and the padding is room for a second `int`. The descriptor that did not fit is closed by the kernel, not queued. **Always test `MSG_CTRUNC`**, and size the buffer for the maximum you will accept.
- On a **stream** socket the kernel needs at least one byte of ordinary data to attach the ancillary message to (from memory; the helpers always send one byte, `F`).
- `MSG_CMSG_CLOEXEC` on the receive makes the new descriptors close-on-exec at once, which avoids leaking them into a child between `recvmsg` and `fcntl`.
- A passed descriptor counts against the **receiver's** `RLIMIT_NOFILE`; a flood of them can make the receiver's own `open` fail with `EMFILE` (from memory). A server that accepts descriptors from untrusted clients must cap them.
- The cycle (a socket in flight that carries the only descriptor of itself) is handled by a garbage collector in the kernel (from memory). It is not your problem, but it is why in-flight descriptors are accounted separately.

One send, one receive and one close in a single process cost **1,494 ns** here (three runs: 1,265, 1,272 and 1,494 ns), about the cost of a small socket message plus the table insertion. Descriptor passing is a **setup** tool, not a per-message one.

### 5.3 `memfd_create`: nameless shared memory

[OS 17](17_ipc_fundamentals.md) listed five ways to share memory. `memfd_create` is the one made for passing. It creates an anonymous file in memory and returns a descriptor; there is **no name** in `/dev/shm` or anywhere. The `name` argument "serves only for debugging purposes" and shows in `/proc/self/fd` (memfd_create(2), fetched). Two processes that have nothing in common but a socket can then share memory: one creates, sizes (`ftruncate`) and maps it, passes the descriptor, and the other maps the same descriptor.

```c
static void sect_memfd(void)
{
    int sv[2];
    IGN(socketpair(AF_UNIX, SOCK_STREAM, 0, sv));
    pid_t c = fork();                              /* forked before the memfd exists */
    if (c == 0) {
        int got[1];
        recv_fds(sv[1], got, 1, NULL);
        struct stat st;
        fstat(got[0], &st);
        char *p = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, got[0], 0);
        char link[64], target[128] = { 0 };
        snprintf(link, sizeof link, "/proc/self/fd/%d", got[0]);
        ssize_t n = readlink(link, target, sizeof target - 1);
        printf("child:  got fd %d, size %lld, text \"%s\", /proc link: %s\n", got[0], (long long)st.st_size, p, n > 0 ? target : "?");
        _exit(0);
    }
    int m = memfd_create("plane", MFD_CLOEXEC);
    IGN(ftruncate(m, 4096));
    char *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, m, 0);
    strcpy(p, "written before the child had the descriptor");
    send_fds(sv[0], &m, 1);
    waitpid(c, NULL, 0);
    close(m); close(sv[0]); close(sv[1]);
}
```

```text
child:  got fd 5, size 4096, text "written before the child had the descriptor", /proc link: /memfd:plane (deleted)
```

The child was forked **before** the memfd existed, so it could not have inherited it; it got descriptor 5 over the socket, mapped it, and read the text the parent had written before sending. Its size is 4,096, and the `/proc` link reads `/memfd:plane (deleted)`: the object has a label and no path, which is why "(deleted)" appears. What `memfd` gives that `shm_open` does not:

- **No name to clean up and none to squat on.** A `shm_open` object lives in `/dev/shm` until someone unlinks it, and any process with permission can open it by name. A memfd is released "once all references to the file are dropped" (fetched), so a crashed owner leaks nothing, and only processes holding the descriptor can map it.
- **Sealing.** With `MFD_ALLOW_SEALING`, `fcntl` can add seals `F_SEAL_SHRINK`, `F_SEAL_GROW`, `F_SEAL_WRITE` and `F_SEAL_SEAL`, so a receiver can rely on the size or the contents not changing under it (fetched). The most important use: a receiver that must **not** be crashed by the sender truncating the file (a `SIGBUS` on access, as in [OS 17](17_ipc_fundamentals.md) section 5.2) asks for the shrink seal first.
- Added in Linux 3.17 and glibc 2.27 (fetched).

### 5.4 Credentials in flight

`SO_PEERCRED` ([OS 17](17_ipc_fundamentals.md)) tells you who **connected**. For datagram sockets, or when each message must carry its sender, the ancillary message `SCM_CREDENTIALS` carries a `struct ucred` (pid, uid, gid) that the **kernel checks** at send time. The measurement:

```c
static void sect_creds(void)
{
    int sv[2];
    IGN(socketpair(AF_UNIX, SOCK_DGRAM, 0, sv));
    int on = 1;
    setsockopt(sv[1], SOL_SOCKET, SO_PASSCRED, &on, sizeof on);
    for (int pass = 0; pass < 2; pass++) {
        char byte = 'c';
        struct iovec iov = { &byte, 1 };
        char ctl[CMSG_SPACE(sizeof(struct ucred))];
        memset(ctl, 0, sizeof ctl);
        struct msghdr mh = { .msg_iov = &iov, .msg_iovlen = 1, .msg_control = ctl, .msg_controllen = sizeof ctl };
        struct cmsghdr *c = CMSG_FIRSTHDR(&mh);
        c->cmsg_level = SOL_SOCKET; c->cmsg_type = SCM_CREDENTIALS; c->cmsg_len = CMSG_LEN(sizeof(struct ucred));
        struct ucred u = { .pid = pass ? 1 : getpid(), .uid = getuid(), .gid = getgid() };
        memcpy(CMSG_DATA(c), &u, sizeof u);
        ssize_t s = sendmsg(sv[0], &mh, 0);
        char rb; struct iovec riov = { &rb, 1 };
        char rctl[CMSG_SPACE(sizeof(struct ucred))];
        struct msghdr rm = { .msg_iov = &riov, .msg_iovlen = 1, .msg_control = rctl, .msg_controllen = sizeof rctl };
        if (s > 0 && recvmsg(sv[1], &rm, 0) > 0) {
            struct cmsghdr *rc = CMSG_FIRSTHDR(&rm);
            struct ucred g;
            memcpy(&g, CMSG_DATA(rc), sizeof g);
            printf("sender claims pid %d: receiver sees pid %d uid %d (this process is pid %d)\n", u.pid, g.pid, g.uid, getpid());
        } else {
            printf("sender claims pid %d: sendmsg failed, errno %s\n", u.pid, strerror(errno));
        }
    }
    close(sv[0]); close(sv[1]);
}
```

```text
sender claims pid 514: receiver sees pid 514 uid 0 (this process is pid 514)
sender claims pid 1: receiver sees pid 1 uid 0 (this process is pid 514)
```

Both messages were delivered, including the one where the sender **claimed to be pid 1**. That is not a bug in the check: this process runs as root, and the kernel lets a privileged sender (from memory: one with `CAP_SYS_ADMIN` for the pid) name another process. An unprivileged sender may only claim its own pid, uid and gid, and the kernel refuses a false claim with `EPERM` (from memory; I did not run it as an unprivileged user here). The lesson for design: the credentials are trustworthy against **unprivileged** senders only. If a root process can forge them, a service that trusts "the pid in the message" has the same trust boundary as a service that trusts root.

---

## 6. `pidfd`: a handle on a process

### 6.1 The race it removes

A process id is a number that the kernel **reuses**. The classic bug: a monitor stores the pid of a worker, the worker exits and is reaped, a new unrelated process gets the same number, and the monitor's `kill(pid, SIGTERM)` hits a stranger. The other classic: waiting for a process that is **not** your child has no kernel interface at all, so tools poll `/proc/<pid>` or `kill(pid, 0)` in a loop.

`pidfd_open(pid, 0)` (Linux 5.3, from the manual page, fetched) returns a descriptor that **refers to that process**, not to its number. The number can be reused, the descriptor still names the original. It is close-on-exec by default (fetched). It is a descriptor, so it goes into `poll` and `epoll`, and it becomes readable "only when the last thread in the thread group exits" (fetched), which is exactly "the process is gone". Related calls: `pidfd_send_signal` sends a signal to the process the descriptor names (the page confirms it), and `waitid` on a pidfd reaps a child; the page I fetched does not describe `P_PIDFD`, so that detail rests on the measurement below and on memory (`waitid(2)`, Linux 5.4).

```c
static void sect_pidfd(void)
{
    pid_t c = fork();
    if (c == 0) { usleep(150000); _exit(7); }
    long pfd = syscall(SYS_pidfd_open, c, 0);
    struct pollfd p = { .fd = (int)pfd, .events = POLLIN };
    int n = poll(&p, 1, 0);
    printf("pidfd_open(child) -> fd %ld; poll right away: %d ready (the child is alive)\n", pfd, n);
    double t0 = now();
    n = poll(&p, 1, 2000);
    printf("  poll again: %d ready after %.0f ms (revents POLLIN %s)\n", n, (now() - t0) * 1000, (p.revents & POLLIN) ? "set" : "clear");
    siginfo_t si;
    memset(&si, 0, sizeof si);
    int r = waitid(P_PIDFD, (id_t)pfd, &si, WEXITED);
    printf("  waitid(P_PIDFD) -> %d, si_pid %d, exit status %d (reaped through the descriptor)\n", r, si.si_pid, si.si_status);
    close((int)pfd);
    errno = 0;
    long bad = syscall(SYS_pidfd_open, 999999, 0);
    printf("pidfd_open on a pid that does not exist: %ld, errno %s\n", bad, errno == ESRCH ? "ESRCH" : strerror(errno));
}
```

```text
pidfd_open(child) -> fd 3; poll right away: 0 ready (the child is alive)
  poll again: 1 ready after 151 ms (revents POLLIN set)
  waitid(P_PIDFD) -> 0, si_pid 517, exit status 7 (reaped through the descriptor)
pidfd_open on a pid that does not exist: -1, errno ESRCH
```

- Right after `pidfd_open`, `poll` showed **0 ready**: the child was alive.
- The child slept 150 ms and exited with status 7; `poll` returned **1 ready after 151 ms** with `POLLIN` set.
- `waitid(P_PIDFD, ...)` returned 0 with the child's pid and **exit status 7**: the descriptor both announced the exit and reaped the child.
- `pidfd_open` on a pid that does not exist failed with **`ESRCH`**.

For a child that has already exited but is not yet reaped, the descriptor still refers to the zombie, so the guarantee holds as long as nothing else reaped it (the page lists the conditions: `SIGCHLD` not ignored, no `SA_NOCLDWAIT`, no other reaper; fetched).

### 6.2 What it is for

- **Supervisors**: wait for several workers in one `epoll` set together with sockets and timers, instead of a `SIGCHLD` handler plus `waitpid` ([OS 17](17_ipc_fundamentals.md)).
- **A peer's death** when the peer is a different process and nothing else tells you: a ring in shared memory does not (OS 17's table of what dies with the peer). Section 8 uses it.
- **Safe signalling** by a descriptor that cannot go stale.

Getting the pid in the first place needs a trustworthy source, such as `SO_PEERCRED` on the connection; newer kernels can also return a pidfd for the peer of a socket (from memory, kernel 6.5 or later; not checked here). The descriptor can also be passed over a Unix socket like any other (the page I fetched does not cover that, so it is from memory).

---

## 7. Netlink: the kernel's structured conversation

### 7.1 What it is and what it replaces

Netlink is a socket family, `AF_NETLINK`, for "transferring information between the kernel and user-space processes" (netlink(7), fetched). You open a socket, send binary request messages, and read binary replies and notifications. It is how `ip`, `ss` and most network daemons talk to the kernel. The families (protocols) include (fetched): `NETLINK_ROUTE` (links, addresses, routes and other network state), `NETLINK_GENERIC` ("generic netlink", a framework in which each kernel subsystem registers a named family), `NETLINK_SOCK_DIAG` (socket queries), `NETLINK_KOBJECT_UEVENT` (kernel device events delivered to user space) and others such as audit and crypto.

Why it exists, against the alternatives:

| Interface | Shape | Good at | Weak at |
| --- | --- | --- | --- |
| `ioctl` | one command per call, fixed structs | one device, one query | extending a struct breaks users; no notifications |
| `/proc` and `sysfs` | text files, one value per file in sysfs | humans, scripts, one value | parsing text; many files; no atomic multi-field update; polling for changes (sysfs has a `poll`-able change notification for some attributes, from memory) |
| **netlink** | typed binary messages with extensible attributes | bulk reads (dumps), atomic multi-field changes, **multicast notifications**, acknowledgement and an error code | writing the code; it is not reliable under load (below) |

### 7.2 The message format

Every message starts with a `struct nlmsghdr`: total length, a **type**, **flags**, a sequence number and the sender's port id. After it comes a family-specific header (for links, a `struct ifinfomsg`: family, type, interface index, flags and a change mask; rtnetlink(7), fetched) and then a list of **attributes**, each an `rtattr` with a length, a type and a payload, aligned to four bytes (also fetched). The flags that matter: `NLM_F_REQUEST` (set on every request), `NLM_F_DUMP` ("give me the whole table", defined as `NLM_F_ROOT | NLM_F_MATCH`), `NLM_F_ACK` (answer even on success) and `NLM_F_MULTI` (this message is one of several and the series ends with `NLMSG_DONE`). A failure arrives as an `NLMSG_ERROR` message carrying a negated errno, and an acknowledgement is the same message with error 0 (fetched).

The helpers of the program, which open a socket, append an attribute, and collect replies until `DONE` or an error:

```c
static struct rtattr *addattr(struct nlmsghdr *n, int type, const void *data, int len)
{
    struct rtattr *a = (struct rtattr *)((char *)n + NLMSG_ALIGN(n->nlmsg_len));
    a->rta_type = type;
    a->rta_len = RTA_LENGTH(len);
    if (len) memcpy(RTA_DATA(a), data, len);
    n->nlmsg_len = NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(a->rta_len);
    return a;
}

static int nl_open(int proto, unsigned groups)
{
    int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, proto);
    struct sockaddr_nl sa = { .nl_family = AF_NETLINK, .nl_groups = groups };
    if (fd < 0 || bind(fd, (struct sockaddr *)&sa, sizeof sa) < 0) { perror("netlink"); exit(1); }
    return fd;
}

static int nl_collect(int fd, handler fn, void *arg, int *msgs, int *recvs)
{
    for (;;) {
        ssize_t n = recv(fd, rbuf, sizeof rbuf, 0);
        if (n < 0) return -errno;
        if (recvs) (*recvs)++;
        for (struct nlmsghdr *h = (struct nlmsghdr *)rbuf; NLMSG_OK(h, (unsigned)n); h = NLMSG_NEXT(h, n)) {
            if (h->nlmsg_type == NLMSG_DONE) return -1000;
            if (h->nlmsg_type == NLMSG_ERROR) return -((struct nlmsgerr *)NLMSG_DATA(h))->error;
            if (msgs) (*msgs)++;
            if (fn) fn(h, arg);
        }
    }
}
```

### 7.3 A dump of the link table

```c
static void on_link(const struct nlmsghdr *h, void *arg)
{
    struct linkinfo *li = arg;
    if (h->nlmsg_type != RTM_NEWLINK) return;
    struct ifinfomsg *ifm = NLMSG_DATA(h);
    int len = IFLA_PAYLOAD(h);
    const char *name = "?";
    unsigned mtu = 0;
    for (struct rtattr *a = IFLA_RTA(ifm); RTA_OK(a, len); a = RTA_NEXT(a, len)) {
        if (a->rta_type == IFLA_IFNAME) name = RTA_DATA(a);
        if (a->rta_type == IFLA_MTU) memcpy(&mtu, RTA_DATA(a), 4);
    }
    li->count++;
    if (li->verbose) printf("    index %d  %-10s mtu %5u  %s\n", ifm->ifi_index, name, mtu, (ifm->ifi_flags & IFF_UP) ? "UP" : "down");
}

static int dump_links(int fd, int verbose, int *msgs, int *recvs)
{
    struct req r;
    memset(&r, 0, sizeof r);
    r.nh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
    r.nh.nlmsg_type = RTM_GETLINK;
    r.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    r.nh.nlmsg_seq = 1;
    r.ifm.ifi_family = AF_UNSPEC;
    if (send(fd, &r, r.nh.nlmsg_len, 0) < 0) return -errno;
    struct linkinfo li = { 0, verbose };
    nl_collect(fd, on_link, &li, msgs, recvs);
    return li.count;
}
```

```text
RTM_GETLINK dump on a NETLINK_ROUTE socket:
    index 1  lo         mtu 65536  UP
    index 2  ifb0       mtu  1500  down
    index 3  ifb1       mtu  1500  down
    index 4  eth0       mtu  1400  UP
  4 links, 4 messages, 2 recv calls
```

The request is `RTM_GETLINK` with `NLM_F_DUMP`. The kernel answers with one `RTM_NEWLINK` message per interface (4 here: `lo`, `ifb0`, `ifb1`, `eth0` with MTUs 65,536, 1,500, 1,500 and 1,400), in **2 `recv` calls** (the four messages and the `NLMSG_DONE` marker arrived in two reads; I did not log which message fell in which), and the program reads each interface's name and MTU out of its attributes.

What it costs, against the same information from the files:

```text
  full RTM_GETLINK dump (send plus recv)              5.2 us
  open, read, close of /proc/net/dev (text, all links)    5.5 us
  open, read, close of one sysfs file (lo mtu)        2.5 us
```

A full dump is **5.2 µs**, about the same as opening and reading `/proc/net/dev` (5.5 µs), and a single `sysfs` attribute is about half of that (2.5 µs). With 4 links the choice does not matter for speed. It matters with hundreds of interfaces or routes, where a dump returns typed records in a few `recv` calls and a text file needs parsing, and, above all, it matters for **change notification**, which sysfs and `/proc` cannot do as a stream.

### 7.4 Events on a multicast group

A netlink socket can join a **multicast group** by setting a bit mask in `nl_groups` when it binds ("each family has 32 groups", fetched). The kernel then pushes a message to the socket whenever the state changes. The program below creates, brings up and deletes an interface through one socket and listens for the notifications on another:

```c
static int link_op(int fd, int type, int flags, const char *name, int index, unsigned up, const char *kind)
{
    struct req r;
    memset(&r, 0, sizeof r);
    r.nh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
    r.nh.nlmsg_type = type;
    r.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | flags;
    r.nh.nlmsg_seq = 2;
    r.ifm.ifi_family = AF_UNSPEC;
    r.ifm.ifi_index = index;
    if (up) { r.ifm.ifi_flags = IFF_UP; r.ifm.ifi_change = IFF_UP; }
    if (name) addattr(&r.nh, IFLA_IFNAME, name, strlen(name) + 1);
    if (kind) {
        struct rtattr *nest = addattr(&r.nh, IFLA_LINKINFO, NULL, 0);
        addattr(&r.nh, IFLA_INFO_KIND, kind, strlen(kind) + 1);
        nest->rta_len = (char *)&r.nh + r.nh.nlmsg_len - (char *)nest;
    }
    if (send(fd, &r, r.nh.nlmsg_len, 0) < 0) return -errno;
    return nl_collect(fd, NULL, NULL, NULL, NULL);
}

static void drain_events(int ev)
{
    struct pollfd p = { .fd = ev, .events = POLLIN };
    while (poll(&p, 1, 200) > 0) {
        ssize_t n = recv(ev, rbuf, sizeof rbuf, 0);
        for (struct nlmsghdr *h = (struct nlmsghdr *)rbuf; NLMSG_OK(h, (unsigned)n); h = NLMSG_NEXT(h, n)) on_event(h, NULL);
    }
}

static void sect_events(void)
{
    int cmd = nl_open(NETLINK_ROUTE, 0), ev = nl_open(NETLINK_ROUTE, RTMGRP_LINK);
    const char *nm = "k5dummy0";
    printf("link events on the RTMGRP_LINK multicast group, while this process changes an interface:\n");
    static const char *kinds[] = { "dummy", "bridge" };
    int r = -1;
    for (int k = 0; k < 2 && r != 0; k++) {
        r = link_op(cmd, RTM_NEWLINK, NLM_F_CREATE | NLM_F_EXCL, nm, 0, 0, kinds[k]);
        printf("  create a \"%s\" interface named %s: %s\n", kinds[k], nm, r == 0 ? "ok" : strerror(r));
    }
    drain_events(ev);
    if (r == 0) {
        int idx = index_of(nm);
        r = link_op(cmd, RTM_NEWLINK, 0, NULL, idx, 1, NULL);
        printf("  set %s up: %s\n", nm, r == 0 ? "ok" : strerror(r));
        drain_events(ev);
        r = link_op(cmd, RTM_DELLINK, 0, NULL, idx, 0, NULL);
        printf("  delete %s: %s\n", nm, r == 0 ? "ok" : strerror(r));
        drain_events(ev);
    }
    r = link_op(cmd, RTM_DELLINK, 0, "no_such_if", 0, 0, NULL);
    printf("  delete an interface that does not exist: errno %s (the error comes back as an NLMSG_ERROR reply, not as a failed send)\n", r > 0 ? strerror(r) : "none");
    close(cmd); close(ev);
}
```

```text
link events on the RTMGRP_LINK multicast group, while this process changes an interface:
  create a "dummy" interface named k5dummy0: Operation not supported
  create a "bridge" interface named k5dummy0: ok
    event: RTM_NEWLINK k5dummy0  flags UP=0 RUNNING=0
  set k5dummy0 up: ok
    event: RTM_NEWLINK k5dummy0  flags UP=1 RUNNING=1
  delete k5dummy0: ok
    event: RTM_NEWLINK k5dummy0  flags UP=0 RUNNING=0
    event: RTM_DELLINK k5dummy0  flags UP=0 RUNNING=0
  delete an interface that does not exist: errno No such device (the error comes back as an NLMSG_ERROR reply, not as a failed send)
```

- **The `dummy` driver is not in this kernel**: the create request came back `Operation not supported`. The `bridge` driver is, so the demonstration uses that. (Which kinds exist is a property of the kernel build; the failure is returned as an ordinary error.)
- **One change can produce more than one event.** Creating produced one `RTM_NEWLINK` (state down); setting it up produced another (flags `UP` and `RUNNING`); deleting produced a `RTM_NEWLINK` (going down) **and** an `RTM_DELLINK`. A consumer must therefore treat each message as "this interface changed, look at the flags", never as "exactly one thing happened".
- **The error for a bad request comes back as a message**, not as a failed `send`: deleting `no_such_if` made `send` succeed and the reply was an `NLMSG_ERROR` with `ENODEV` ("No such device").
- **Netlink is not reliable** (fetched). If the socket buffer fills because the program is slow, the kernel drops messages and the next `recv` fails with `ENOBUFS`. The correct reaction is to **re-dump** the table and rebuild your view, which is why long-running daemons handle both an event stream and a dump. A related rule, from memory: a dump that races with a change can be flagged `NLM_F_DUMP_INTR` and should be repeated.

### 7.5 Generic netlink and the Wi-Fi interface

`NETLINK_ROUTE` has a fixed set of message types. For everything else the kernel offers **generic netlink**: a subsystem registers a **family** by name, and the numeric family id is **assigned at run time**. A program first asks the built-in controller family `nlctrl` (id 16 in this kernel) "what is the id of the family named X", then sends that family's commands.

```c
static int genl_query(int fd, const char *name, struct fam *f)
{
    struct { struct nlmsghdr nh; struct genlmsghdr g; char buf[128]; } r;
    memset(&r, 0, sizeof r);
    r.nh.nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
    r.nh.nlmsg_type = GENL_ID_CTRL;
    r.nh.nlmsg_flags = NLM_F_REQUEST | (name ? NLM_F_ACK : NLM_F_DUMP);
    r.nh.nlmsg_seq = 3;
    r.g.cmd = CTRL_CMD_GETFAMILY;
    r.g.version = 1;
    if (name) {
        struct nlattr *a = (struct nlattr *)((char *)&r + NLMSG_ALIGN(r.nh.nlmsg_len));
        a->nla_type = CTRL_ATTR_FAMILY_NAME;
        a->nla_len = NLA_HDRLEN + strlen(name) + 1;
        memcpy((char *)a + NLA_HDRLEN, name, strlen(name) + 1);
        r.nh.nlmsg_len = NLMSG_ALIGN(r.nh.nlmsg_len) + NLA_ALIGN(a->nla_len);
    }
    if (send(fd, &r, r.nh.nlmsg_len, 0) < 0) return -errno;
    return nl_collect(fd, on_family, f, NULL, NULL);
}

static void sect_generic(void)
{
    int fd = nl_open(NETLINK_GENERIC, 0);
    struct fam f;
    memset(&f, 0, sizeof f);
    int r = genl_query(fd, NULL, &f);
    printf("generic netlink: the controller (family nlctrl, id %d) lists %d families in this kernel:\n  %s\n", f.nlctrl_id, f.count, f.names);
    for (int k = 0; k < 2; k++) {
        const char *want = k ? "nl80211" : "nlctrl";
        struct fam g;
        memset(&g, 0, sizeof g);
        r = genl_query(fd, want, &g);
        printf("  lookup by name \"%s\": %s", want, g.count ? "found" : strerror(r > 0 ? r : -r));
        if (g.count) printf(", family id %d", k ? g.nl80211 : g.nlctrl_id);
        printf("\n");
    }
    close(fd);
}
```

```text
generic netlink: the controller (family nlctrl, id 16) lists 13 families in this kernel:
  nlctrl VFS_DQUOT thermal netdev ethtool NLBL_MGMT NLBL_CIPSOv4 NLBL_CALIPSO NLBL_UNLBL acpi_event tcp_metrics mptcp_pm TASKSTATS
  lookup by name "nlctrl": found, family id 16
  lookup by name "nl80211": No such file or directory

```

This kernel registers **13 families** (among them `nlctrl`, `ethtool`, `netdev`, `thermal`, `tcp_metrics`, `mptcp_pm`, `TASKSTATS` and `VFS_DQUOT`). The lookup of `nlctrl` found id 16, and the lookup of **`nl80211` failed with `ENOENT`**, which is the answer for "this kernel has no wireless configuration stack": this VM has no Wi-Fi hardware and no `cfg80211`.

Why that name matters for a Wi-Fi engineer (all from memory, none of it runnable here): `nl80211` is the family through which user space configures wireless devices: scanning, associating, creating access-point interfaces, adding stations, reading station statistics, regulatory domain, and receiving events for scan results, connection and disconnection, through its own multicast groups. The tools `iw`, `wpa_supplicant` and `hostapd` are `nl80211` clients, and a driver exposes its functions to them through `cfg80211`. If an interview asks "how does `hostapd` tell the driver to start a BSS?", the answer shape is: a generic netlink request on the `nl80211` family, resolved by name through `nlctrl`, with attributes for the interface, the SSID, the channel and so on. Vendor-specific commands ride the same transport.

### 7.6 Mistakes the measurements exposed

One mistake is mine, and it is instructive. The first version of the family lookup sent a **non-dump request for one family** and waited for `NLMSG_DONE` or an error. A request that is not a dump and does not ask for an acknowledgement gets **one reply and nothing after it**: no `DONE` and no ACK. The program printed everything up to the dump of families, then blocked in `recv`; the command had not returned after 120 seconds, and I stopped it. The fix is `NLM_F_ACK` on the lookup (the final line of the output shows the corrected version). **Know how each request ends**: a dump ends with `NLMSG_DONE`; a modify ends with an ACK or error if you set `NLM_F_ACK` (and with an error only if it fails otherwise); a single get ends with its one reply unless you request an ACK. Use a timeout on `recv` in anything that is not a throwaway.

---
## 8. Putting it together: one complete data plane

The tools of sections 2 to 6 solve one problem each. A real service uses them together, in this order, and the program `k6_plane.c` runs the whole sequence between two processes that share nothing but a socket.

1. **Setup, through the kernel, once.** The server creates a `memfd`, sizes it, maps it, creates an `eventfd` and a `pidfd` for the client, and sends the `memfd` and the `eventfd` to the client in **one** `SCM_RIGHTS` message (section 5). The client maps the `memfd` (section 5.3).
2. **Data path, no kernel.** The client writes 64-byte messages into the ring in the shared memory; the server reads them (section 4).
3. **Wake-ups, only when needed.** The server spins for 2,000 iterations (about 22 µs), then sets its `sleeping` flag, re-checks, and blocks in `epoll_wait` on the `eventfd`. The client writes the `eventfd` only if it sees the flag (section 4.2).
4. **Peer death, through the kernel, once.** The same `epoll` set holds the `pidfd` of the client (section 6). When the client dies, the server learns of it without any cooperation from the client.

The shared state and the sender and receiver of the two descriptors:

```c
struct plane {
    _Alignas(64) _Atomic uint32_t head;
    _Alignas(64) _Atomic uint32_t tail;
    _Alignas(64) _Atomic uint32_t sleeping;
    _Alignas(64) double death_ts;                  /* written by the producer just before it dies */
    uint64_t wakeups;
    struct msg slot[RN];
};

static ssize_t send_two(int sock, int a, int b)
{
    char byte = 'P';
    struct iovec iov = { &byte, 1 };
    char ctl[CMSG_SPACE(2 * sizeof(int))];
    memset(ctl, 0, sizeof ctl);
    struct msghdr mh = { .msg_iov = &iov, .msg_iovlen = 1, .msg_control = ctl, .msg_controllen = sizeof ctl };
    struct cmsghdr *c = CMSG_FIRSTHDR(&mh);
    int fds[2] = { a, b };
    c->cmsg_level = SOL_SOCKET; c->cmsg_type = SCM_RIGHTS; c->cmsg_len = CMSG_LEN(sizeof fds);
    memcpy(CMSG_DATA(c), fds, sizeof fds);
    return sendmsg(sock, &mh, 0);
}

static int recv_two(int sock, int *a, int *b)
{
    char byte;
    struct iovec iov = { &byte, 1 };
    char ctl[CMSG_SPACE(2 * sizeof(int))];
    struct msghdr mh = { .msg_iov = &iov, .msg_iovlen = 1, .msg_control = ctl, .msg_controllen = sizeof ctl };
    if (recvmsg(sock, &mh, MSG_CMSG_CLOEXEC) <= 0) return -1;
    struct cmsghdr *c = CMSG_FIRSTHDR(&mh);
    if (!c || c->cmsg_type != SCM_RIGHTS) return -1;
    int fds[2];
    memcpy(fds, CMSG_DATA(c), sizeof fds);
    *a = fds[0]; *b = fds[1];
    return 0;
}
```

The producer and the client's life (a burst of 1,000,000 messages, then 500 messages 100 µs apart, then it dies by `SIGKILL` without closing anything):

```c
static void produce(struct plane *p, int efd, uint64_t seq)
{
    uint32_t t = atomic_load_explicit(&p->tail, memory_order_relaxed);
    while (t - atomic_load_explicit(&p->head, memory_order_acquire) == RN) __builtin_ia32_pause();
    p->slot[t % RN].seq = seq;
    atomic_store(&p->tail, t + 1);
    if (atomic_load(&p->sleeping)) { uint64_t one = 1; IGN(write(efd, &one, 8)); p->wakeups++; }
}

static void client(int sock)
{
    pin(0);
    int mfd, efd;
    if (recv_two(sock, &mfd, &efd) < 0) _exit(2);
    struct plane *p = mmap(NULL, sizeof *p, PROT_READ | PROT_WRITE, MAP_SHARED, mfd, 0);
    printf("client: received descriptors %d (memfd) and %d (eventfd), mapped the plane at %p\n", mfd, efd, (void *)p);
    uint64_t seq = 0;
    for (int i = 0; i < BURST; i++) produce(p, efd, seq++);
    for (int i = 0; i < PACED; i++) {
        double next = now() + GAP_US * 1e-6;
        while (now() < next) __builtin_ia32_pause();
        produce(p, efd, seq++);
    }
    p->death_ts = now();
    kill(getpid(), SIGKILL);                       /* die without closing anything */
    _exit(3);
}
```

The server, which does the receiving, the sleeping and the death detection:

```c
int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    int sv[2];
    IGN(socketpair(AF_UNIX, SOCK_STREAM, 0, sv));
    pid_t c = fork();
    if (c == 0) { close(sv[0]); client(sv[1]); }
    close(sv[1]);
    pin(1);

    int mfd = memfd_create("plane", MFD_CLOEXEC);
    IGN(ftruncate(mfd, sizeof(struct plane)));
    struct plane *p = mmap(NULL, sizeof *p, PROT_READ | PROT_WRITE, MAP_SHARED, mfd, 0);
    int efd = eventfd(0, EFD_NONBLOCK);
    int pfd = (int)syscall(SYS_pidfd_open, c, 0);
    int ep = epoll_create1(0);
    struct epoll_event e1 = { .events = EPOLLIN, .data.fd = efd }, e2 = { .events = EPOLLIN, .data.fd = pfd }, out[2];
    epoll_ctl(ep, EPOLL_CTL_ADD, efd, &e1);
    epoll_ctl(ep, EPOLL_CTL_ADD, pfd, &e2);
    printf("server: created the memfd as fd %d and the eventfd as fd %d, mapped the plane at %p\n", mfd, efd, (void *)p);
    send_two(sv[0], mfd, efd);

    uint64_t got = 0, expect = 0, bad = 0, sleeps = 0, spurious = 0;
    double tfirst = 0, tburst = 0;
    int dead = 0;
    while (!dead || atomic_load(&p->head) != atomic_load(&p->tail)) {
        uint32_t h = atomic_load_explicit(&p->head, memory_order_relaxed);
        if (atomic_load_explicit(&p->tail, memory_order_acquire) != h) {
            if (p->slot[h % RN].seq != expect++) bad++;
            atomic_store_explicit(&p->head, h + 1, memory_order_release);
            if (got++ == 0) tfirst = now();
            if (got == BURST) tburst = now();
            continue;
        }
        if (dead) break;
        int again = 0;
        for (int i = 0; i < SPIN; i++) {
            if (atomic_load_explicit(&p->tail, memory_order_acquire) != h) { again = 1; break; }
            __builtin_ia32_pause();
        }
        if (again) continue;
        atomic_store(&p->sleeping, 1);
        if (atomic_load(&p->tail) != h) { atomic_store(&p->sleeping, 0); continue; }
        sleeps++;
        int n = epoll_wait(ep, out, 2, -1);
        atomic_store(&p->sleeping, 0);
        int woke_for_data = 0;
        for (int i = 0; i < n; i++) {
            if (out[i].data.fd == efd) { uint64_t v; IGN(read(efd, &v, 8)); woke_for_data = 1; }
            if (out[i].data.fd == pfd) dead = 1;
        }
        if (!woke_for_data && !dead) spurious++;
    }
    double tdetect = now();
    int st;
    waitpid(c, &st, 0);
    printf("server: %llu messages received, %llu out of order\n", (unsigned long long)got, (unsigned long long)bad);
    printf("server: burst of %d messages in %.1f ms = %.2f M msgs/s, %llu producer wake-ups, %llu consumer sleeps\n", BURST,
           (tburst - tfirst) * 1000, BURST / (tburst - tfirst) / 1e6, (unsigned long long)p->wakeups, (unsigned long long)sleeps);
    printf("server: producer %s; the pidfd became readable and the server saw it %.0f us after the producer's last timestamp\n",
           WIFSIGNALED(st) ? "killed by SIGKILL" : "exited", (tdetect - p->death_ts) * 1e6);
    printf("server: the ring still held every message the producer wrote before dying (the plane belongs to the memfd, not to the process)\n");
    (void)spurious;
    return 0;
}
```

The output:

```text
server: created the memfd as fd 4 and the eventfd as fd 5, mapped the plane at 0x7f22c37dd000
client: received descriptors 3 (memfd) and 5 (eventfd), mapped the plane at 0x7f22c37dd000
server: 1000500 messages received, 0 out of order
server: burst of 1000000 messages in 176.2 ms = 5.68 M msgs/s, 1042 producer wake-ups, 503 consumer sleeps
server: producer killed by SIGKILL; the pidfd became readable and the server saw it 190 us after the producer's last timestamp
server: the ring still held every message the producer wrote before dying (the plane belongs to the memfd, not to the process)
```

What it shows:

- **Descriptor numbers differ.** The server made the `memfd` as descriptor 4 and the `eventfd` as 5; the client received them as 3 and 5. The ring holds no descriptor numbers, which is why the producer takes `efd` as an argument and not from the shared struct (the point made at the end of section 4).
- **The mapping address is the same in both here** (`0x7f22c37dd000`), because the client was created by `fork` and the kernel placed the new mapping at the same address in an identical layout. That is an accident of this test. Two unrelated processes would usually get different addresses, so the ring must hold **indices and offsets, never pointers**.
- **Nothing was lost or reordered**: 1,000,500 messages (1,000,000 in the burst and 500 paced) arrived with **0 out of order**.
- **Throughput**: the burst ran at 5.68 million messages a second, with only 1,042 wake-up system calls and 503 consumer sleeps for 1,000,500 messages. One run; the spin-based numbers of section 4.6 vary, so read this as "several million a second", not as a benchmark.
- **The producer's death was seen in 190 µs** after its last timestamp. The producer wrote the timestamp and then killed itself with `SIGKILL`, so it never closed a descriptor or ran an exit handler; the `pidfd` still became readable, because the kernel, not the process, announces the exit. (The time includes the server finishing the messages already in the ring, so it is an upper bound on the detection delay.)
- **The ring outlived the producer.** Every message the producer wrote before dying was still in the memory the `memfd` holds, and the server read all of them. A plane in a `memfd` belongs to its descriptors, not to the process that filled it.

What the program leaves out, and a production version needs:

- **A handshake and a version**: the first message on the socket should carry a version number and the ring geometry, and the receiver should check it before mapping.
- **Untrusted contents.** The peer can write anything into shared memory, so the consumer validates every index and length it reads from it, and a receiver that maps a file from an untrusted sender asks for the shrink and grow seals (section 5.3).
- **A full ring**: here the producer spins when the ring is full; a real one chooses between blocking, dropping and overwriting, and counts the drops.
- **Recovery** after the producer dies mid-message: the slot is written before `tail` publishes it, so a half-written slot is never visible, but the consumer must decide whether to reset the ring for the next producer.
- **Batching and cache effects**: caching the other side's index and publishing in batches (section 4.6), and placing the ring on the right NUMA node ([OS 13](13_numa.md)).

---

## 9. Choosing, and what to avoid

The table combines the measurements of this tutorial and of [OS 17](17_ipc_fundamentals.md). The cross-CPU figures include the VM's inter-processor-interrupt cost.

| Need | Use | Measured here |
| --- | --- | --- |
| a few control messages, naming, access control | Unix `SOCK_SEQPACKET` or stream socket | round trip 28,873 ns across CPUs (OS 17); a descriptor send, receive and close about 1,494 ns |
| a stream of small events, 1 M a second or more | shared ring with a sleeping strategy (section 4) | about 4 M msgs/s spinning; 1.4 to 2.3 M msgs/s when the producer pays a wake-up system call |
| lowest latency, CPU is free | spin on shared memory | 0.7 µs median, a CPU at 100 percent |
| lowest idle CPU, latency of 14 to 17 µs is fine | futex or `eventfd`, wake only if sleeping | about 2 percent CPU at one message per 200 µs |
| low latency for bursts, idle when quiet | spin N, then sleep, with N from the gap | the cliff of section 4.4 |
| wait for several things together | `epoll` over `eventfd`, `pidfd`, sockets, timers | one descriptor each |
| a lock between processes | process-shared, robust `pthread` mutex (a futex) | about 55 ns per contended acquisition (OS 17); 56.5 ns for the hand-built one |
| hand an open file, a socket or memory to another process | `SCM_RIGHTS`, `memfd` | setup cost only |
| know that a peer process died | `pidfd` in `epoll`; `EOWNERDEAD` for a lock | 190 µs after a `SIGKILL` here |
| read or change network state, subscribe to changes | `NETLINK_ROUTE` and generic netlink | a link dump 5.2 µs |
| a single numeric setting or status | a `sysfs` file | 2.5 µs per read |

What to avoid, with the reason:

- **Signals to carry data.** They coalesce (100 sent, 1 delivered), cost 26.5 µs across CPUs (OS 17), and a handler may do almost nothing.
- **System V IPC in new code.** Keys instead of descriptors, no `epoll`, objects that outlive the program, and nothing a `memfd`, an `eventfd` and a socket cannot do better.
- **Loopback TCP between processes on one machine.** It is the same wake-up as a Unix socket (28.9 against 32.7 µs round trip in OS 17) plus protocol work, and it has no credentials, no descriptor passing and no filesystem permissions.
- **Polling in a loop** (`sleep(1)` and read `/proc`, `kill(pid, 0)`): use `epoll` on a `pidfd`, an `eventfd`, or a netlink subscription.
- **Pipes or FIFOs on a hot path** once the rate passes about a million messages a second: the per-message wake-up is the whole cost.
- **Putting a futex in an event loop.** If the thread also waits for descriptors, use an `eventfd`.

---

## What it costs

| Operation | Cost (measured here, unless marked) | Why |
| --- | --- | --- |
| `eventfd` write plus read, one thread | 307 ns (pipe 406, Unix socket 552) | two system calls, a counter and no buffer |
| `eventfd` ping-pong, across CPUs / one CPU | 25,893 / 1,824 ns | two sleeps and wake-ups |
| uncontended futex mutex, lock plus unlock | 14.1 ns (glibc 5.3) | two atomic operations, no system call |
| contended futex mutex, two threads | 56.5 ns; 2.965 percent of acquisitions waited | atomics plus an occasional sleep |
| ring round trip, spinning / spin then futex / futex only | 856 / 1,010 to 1,254 / about 25,300 ns across CPUs | no system call; or a wake-up each way |
| ring round trip on one CPU, sleeping | 1,623 to 1,844 ns | one context switch each way |
| sparse stream, spin / sleep, median latency | 0.7 / 13.7 to 16.6 µs | cache line against wake from idle |
| sparse stream, consumer CPU, spin / sleep | 99.6 / about 2 percent of a CPU | busy loop against idle |
| burst, wake on every message / only if sleeping / spin then sleep | 1.4 to 1.5 / 1.8 to 2.3 / about 4 M msgs/s | one system call per message; per sleep; almost none |
| one descriptor sent, received and closed | 1,265 to 1,494 ns | two socket calls and a table insert |
| `pidfd` readable after the process exits | 151 ms for a 150 ms sleeper; 190 to 215 µs after a `SIGKILL` in the data plane | the kernel announces the exit |
| netlink link dump / `/proc/net/dev` / one `sysfs` file | 5.2 / 5.5 / 2.5 µs | typed records against text |
| one spin iteration | about 11 ns | `pause` and an atomic load |

---

## Common mistakes

1. **Using an `eventfd` to carry data.** Three writes of 1 became one read of 3. It is a doorbell; the data goes in the ring.
2. **Not using `EFD_NONBLOCK` in an event loop.** A blocking `read` on an empty `eventfd` stops the whole loop. Read it only after `epoll` says it is ready, with a non-blocking descriptor.
3. **Not re-testing the condition after `FUTEX_WAIT` returns.** It returns on a wake, on `EAGAIN`, on a signal and on a timeout. Loop and look at the word again.
4. **Using a `PRIVATE` futex operation on a word shared between processes.** The wake reported 0 woken and the sleeper stayed asleep. Use the shared operations for shared memory.
5. **Leaving out the re-check, or weakening the ordering, in a flag handshake.** A consumer that sleeps after the producer skipped the wake sleeps with a message in the ring. Publish the intent, look again, then block; use sequentially consistent operations for the store and the load of the two flags.
6. **Choosing a spin budget without measuring the gap.** The result is all or nothing: a budget of 2,000 iterations on a 200 µs stream cost 16.9 percent of a CPU and gained nothing.
7. **Spinning on the CPU of the thread you wait for.** The spinner burns its whole time slice.
8. **Storing descriptor numbers, or pointers, in shared memory.** The same descriptor has different numbers in different processes, and the same mapping may have different addresses. Store indices and offsets, and pass descriptors over the socket.
9. **Not checking `MSG_CTRUNC` or sizing the control buffer too small.** Descriptors that do not fit are closed by the kernel without an error. Also check the type of a received descriptor (`fstat`) before using it.
10. **Trusting `SCM_CREDENTIALS` from a privileged sender.** A root sender named pid 1 and the receiver saw pid 1. The credentials are only as strong as the sender's privilege.
11. **Storing a pid and calling `kill(pid)` later.** The number can be reused. Hold a `pidfd`.
12. **Assuming netlink never loses messages.** A slow reader gets `ENOBUFS` and must re-dump. Also assume one change can produce more than one event, as the delete of an interface did.
13. **Sending a netlink request and waiting for the wrong terminator.** A single get without `NLM_F_ACK` has no `DONE` and no ACK; the lookup in the first version of the program never returned. Put a timeout on `recv`.
14. **Assuming `nl80211` (or any generic family) is present.** Look the family up by name and handle `ENOENT`, as the controller returned for it here.
15. **Treating the peer's shared memory as trusted.** Validate every index and length read from it, and seal a `memfd` received from an untrusted process.
16. **Quoting VM numbers as hardware numbers.** The 25 µs cross-CPU wake-up here is an artefact of virtual inter-processor interrupts; on bare metal it is much smaller (from memory). Compare strategies on the same machine, and state the machine.

---

## Questions out loud

Answer each in two minutes, with a number or a diagram, before looking back (the section in brackets).

1. A service must handle one million messages a second between two processes. Do the arithmetic and say where the wake-up can and cannot be. [1]
2. What is an `eventfd`? What do `write` and `read` do in counter mode and in semaphore mode, and why is it not a good data channel? [2.1]
3. Why is an `eventfd` better than a futex for a thread that also waits on a socket? [2.2, 3.4]
4. State the futex contract in one sentence. Why does the atomic "check, then sleep" prevent a lost wake-up? [3.2]
5. Walk through a three-state futex mutex: the lock fast path, the slow path and the unlock. Why does the unlock sometimes wake a thread that is not there? [3.3]
6. Why does a futex word shared between two processes need the non-`PRIVATE` operations? [3.2]
7. A ring consumer must sleep when empty. Write the protocol that cannot lose a wake-up, and say why it needs sequentially consistent operations. [4.2]
8. At one message per 200 µs, why did a spin budget of 2,000 iterations not help while 20,000 did? How do you choose a budget? [4.4, 4.5]
9. What does `SCM_RIGHTS` give the receiver? What is shared between the sender's descriptor and the receiver's, and what is not? [5.1]
10. Why use a `memfd` instead of `shm_open` for sharing memory between two processes? What are seals for? [5.3]
11. How do you reliably learn that another process died, without a pid-reuse race? What does `pidfd` add over `SIGCHLD`? [6]
12. What is netlink, and how does it differ from `ioctl`, `sysfs` and `/proc`? What happens when a netlink reader is too slow? [7.1, 7.4]
13. How does an application like `hostapd` or `wpa_supplicant` talk to a Wi-Fi driver? What would the lookup of `nl80211` return on a kernel without wireless? [7.5]
14. Describe a complete data plane between two unrelated processes: setup, data path, wake-ups, death detection. What must the receiver not trust? [8]

---

## Problems

**Where to solve them.** On your own machine: compile the programs above (`gcc -O2 -pthread`), watch the system calls with `strace -f -c` or `strace -f -e trace=futex,eventfd2,sendmsg,recvmsg` (the futex and `eventfd` calls of a program are visible there), look at descriptors with `ls -l /proc/<pid>/fd`, at the netlink sockets with `ss -f netlink` or `cat /proc/net/netlink`, and at links with `ip monitor link` (that command listens to the same multicast group as section 7.4). For practice problems of this kind: the `man7.org` pages of `futex(2)`, `eventfd(2)`, `unix(7)` and `netlink(7)`, the "Futexes Are Tricky" paper by Ulrich Drepper, the lock-free queue chapters of books on the C++ memory model, and the source of a small ring such as the LMAX Disruptor or the Linux `kfifo` (from memory), then rebuild the ring and measure it.

**How to approach them.** For a protocol, write the two threads' operations as numbered lines and look for the interleaving in which each reads before the other writes. For a cost, write the formula first (`time = calls * cost per call`) and then insert the measured numbers. For a descriptor question, ask which **open file description** each descriptor points to.

### Problem A (E, entry level): `eventfd` by hand

A non-blocking `eventfd` in counter mode starts at 0. The program runs `write(2)`, `write(3)`, `read`, `read`, `write(7)`, `read`. A second non-blocking `eventfd` is created with `EFD_SEMAPHORE`, and the program runs `write(2)`, `read`, `read`, `read`. (a) What does each `read` return? (b) After which operations does `epoll` report each descriptor readable? (c) What does a 4-byte `write` return?

### Problem B (E, entry level): trace the three-state mutex

Two threads, T1 and T2, use the hand-built mutex of section 3.3, starting at state 0. T1 locks. T2 tries to lock. T1 unlocks. T2 wakes up, then unlocks. Give the state after each step, the number of `FUTEX_WAIT` and `FUTEX_WAKE` calls, and say whether any wake was wasted. Then repeat with one change: T1's unlock happens **after** T2 has set the state to 2 but **before** T2 calls `FUTEX_WAIT`.

### Problem C (M, medium): a spin budget for a bursty stream

Messages arrive in trains of 50, 30 µs apart, followed by 2,000 µs of silence. One spin iteration takes 11 ns; waking from a sleep adds 14 µs of latency; a message found while spinning has about 0.7 µs of latency. (a) What is the smallest budget, in iterations, that catches every message inside a train? (b) What is the total latency added to a train with sleep-always, and with a spin-then-sleep consumer using that budget rounded up to 3,000? (c) What fraction of a CPU does the spin-then-sleep consumer spend spinning, counting the final spin before it sleeps? (d) What happens with a budget of 1,000 iterations?

### Problem D (M, medium): who shares which offset

A file has 100 bytes. A parent opens it and reads 10 bytes, then passes the descriptor to a child with `SCM_RIGHTS`. (a) The child reads 25 bytes, then the parent reads 5. What are the offset and the bytes (by position) each read returned? (b) The child then calls `lseek(fd, 0, SEEK_SET)`, and the parent reads 10 bytes: which bytes does it get, and what is the offset after? (c) Redo (a) if the child had instead opened the path itself. (d) A sender has 600 descriptors to pass: how many messages, if it uses the largest single message? (e) The receiver has descriptors 0, 1, 2, 3 and 5 open and receives three: which numbers does it get?

### Problem E (H, hard): break the sleeping protocol

A consumer and a producer share `tail` (starts 0) and `sleeping` (starts 0). Producer: (P1) store `tail = 1`; (P2) load `sleeping`, and call the wake if it is 1. Consumer: (C1) load `tail` and see 0 (empty); (C2) store `sleeping = 1`; (C3) load `tail`; (C4) if it is still 0, block. (a) Show the interleaving, using only the order of those operations, in which the consumer blocks with a message in the ring when the re-check at C3 is **missing** (go from C1 straight to C4). (b) With the re-check present, show why no interleaving of sequentially consistent operations loses the wake. (c) Using only release stores and acquire loads, the CPU may still reorder P1 and P2 (a store with a later load to a different address). Show the outcome in which P2 reads 0 and C3 reads 0. (d) Does `FUTEX_WAIT(&tail, 0)` in place of C4 rescue the outcome of (c)? (e) In the burst test the futex strategy made 895,418 wake calls for 2,000,000 messages at 2.25 million messages a second: what fraction of the messages made a call, and how many calls per second is that?

---

## Answers

**A.** (a) Counter mode: the first `read` returns **5** (2 plus 3) and resets the counter, the second fails with `EAGAIN`, then `write(7)` and the `read` returns **7**. Semaphore mode: after `write(2)` the three reads return **1**, **1** and fail with `EAGAIN`. (b) Counter mode: readable after `write(2)` and after `write(3)`, not after the first `read`, readable after `write(7)`, not after its `read`. Semaphore mode: readable after `write(2)` and after the first `read` (the counter is 1), not after the second. (c) The 4-byte `write` returns -1 with `EINVAL`; the transfer size must be exactly 8 bytes (section 2.1, measured with 3 bytes).

**B.** First scenario. Start 0. T1 locks: CAS 0 to 1, state **1**, no system call. T2: CAS fails, sees 1 (not 2), exchanges to 2 (returns 1), state **2**, calls `FUTEX_WAIT(&s, 2)`, which sleeps (the word equals 2). T1 unlocks: exchange to 0 returns 2, so it calls `FUTEX_WAKE`; state **0**. T2 wakes, exchanges to 2 (returns 0), so it owns the lock, state **2**. T2 unlocks: exchange to 0 returns 2, so it calls `FUTEX_WAKE`; state **0**. **Calls: one `FUTEX_WAIT` (which slept) and two `FUTEX_WAKE`; the second wake is wasted** (nobody sleeps), the effect of section 3.3. Second scenario: T2's exchange makes the state 2; T1's unlock returns 2 and calls `FUTEX_WAKE`, which finds nobody (wasted); T2's `FUTEX_WAIT(&s, 2)` finds the word is 0, returns `EAGAIN` without sleeping, T2 exchanges to 2 (returns 0) and owns the lock; T2's unlock returns 2 and wakes nobody (wasted). **Calls: one `FUTEX_WAIT` (returned at once) and two `FUTEX_WAKE`, both wasted**, and no thread ever slept: the atomic check prevented the lost wake-up.

**C.** (a) 30 µs / 11 ns = 2,727.3, so at least **2,728 iterations** (3,000 rounds it up to 33 µs). (b) Sleep-always: 50 messages times 14 µs = **700 µs** per train. Spin-then-sleep: the first message finds the consumer asleep (14 µs) and the other 49 are caught while spinning: 14 + 49 times 0.7 = **48.3 µs**. (c) A train lasts 49 times 30 = 1,470 µs between the first and last message; the consumer spins through it, then spins its budget of 33 µs once more before it sleeps, about 1,503 µs of spinning in a cycle of 1,470 + 2,000 = 3,470 µs: **about 43 percent** of a CPU (pure spinning is 100 percent). (d) A budget of 1,000 iterations is 11 µs, shorter than the 30 µs gap, so **no message is ever caught**: each pays the 14 µs wake-up as in sleep-always (700 µs per train), and the consumer also burns 50 times 11 = 550 µs per train, 550 / 3,470 = **15.9 percent** of a CPU for no gain (the cliff of section 4.4).

**D.** (a) The descriptors share one open file description, so one offset. Parent read bytes 0 to 9 (offset 10). The child reads 25: bytes **10 to 34**, offset 35. The parent reads 5: bytes **35 to 39**, offset **40**. (b) After the child's `lseek` the shared offset is 0, so the parent's 10-byte read returns bytes **0 to 9** and the offset is **10**. (c) If the child had opened the path itself it would have its own description: after the parent's 10-byte read (offset 10) the child reads bytes 0 to 24 (offset 25), and the parent's 5-byte read returns bytes 10 to 14 (offset 15); the offsets are independent. (d) At most 253 per message: 253 + 253 + 94 = 600, so **three** messages (253 is the measured limit, 254 failed with `EINVAL`). (e) The kernel installs each at the lowest free number (from memory, as for any descriptor allocation): **4, 6 and 7**.

**E.** (a) Without C3: C1 sees `tail` 0. The producer runs P1 (`tail` = 1) and P2 (reads `sleeping`, which is still 0, so it does not wake). Then the consumer runs C2 (`sleeping` = 1) and C4 (blocks). The message is in the ring and no wake will come. (b) With C3 present, and sequentially consistent operations, the four operations (P1, P2, C2, C3) have some total order consistent with each thread's order, so P1 is before P2 and C2 before C3. If P2 reads 0, then P2 is before C2, so P1 is before C3 and C3 reads 1: the consumer does not block. If P2 reads 1, the producer wakes. One of the two always holds. (c) With release and acquire only, the CPU may complete the load P2 before the store P1 becomes visible, and the load C3 before the store C2 becomes visible (each thread reorders its store with its later load; the store-buffer case on x86). Then P2 reads 0 (C2 not yet visible) and C3 reads 0 (P1 not yet visible), the producer does not wake, the consumer blocks. A full barrier (a sequentially consistent store and load, or `atomic_thread_fence(memory_order_seq_cst)`) between the store and the load in each thread forbids it. (d) It narrows the window but is not a proof: the kernel's check reads `tail` at some later time, and by then the producer's store has very likely drained and the wait returns `EAGAIN`, but the memory model gives no guarantee, so the barriers stay (section 4.2). (e) 895,418 / 2,000,000 = **44.8 percent** of the messages made a call, and 0.448 times 2.25 million = about **1.0 million calls a second**, which is why the flag alone does not make a fast producer cheap and the spin phase does.

---

## Cheat sheet

- **Budget**: 1 M msgs/s is 1 µs each; a cross-CPU wake-up is 25 µs here. Keep data off the kernel, use the kernel to sleep and to set up.
- **`eventfd`**: 64-bit counter; `write` adds, `read` returns and resets (or returns 1 and decrements with `EFD_SEMAPHORE`); exactly 8 bytes; readable when above 0; a descriptor, so `epoll` works; counts coalesce; round trip 25.9 µs across CPUs, 1.8 µs on one.
- **Futex**: 32-bit word in your memory; `FUTEX_WAIT(addr, expected)` sleeps only if `*addr == expected` (else `EAGAIN`); `FUTEX_WAKE(addr, n)`; `PRIVATE` forms for one process, shared forms for shared memory; uncontended lock is a CAS; states 0, 1, 2; 14 ns uncontended, 56.5 ns and 3 percent kernel entries contended.
- **Sleeping ring**: publish data, then read the flag; publish the flag, then look again, then block; sequentially consistent store and load; spurious wake-ups are normal; the futex word is `tail`.
- **Spin budget**: all or nothing; match the gap; 11 ns per iteration; sleep latency 14 to 17 µs against 0.7 µs spinning; spin costs a CPU, sleep costs about 2 percent.
- **Burst**: wake each message 1.4 M/s; wake only if sleeping 1.8 to 2.3 M/s with about 1 M calls; spin then sleep about 4 M/s with hundreds of calls (and an unexplained jump to 43 M/s twice).
- **`SCM_RIGHTS`**: receiver gets a new descriptor number for the same open file description (shared offset); 253 per message; check `MSG_CTRUNC`; one data byte on a stream socket; `MSG_CMSG_CLOEXEC`; a setup tool (about 1.3 to 1.5 µs a cycle).
- **`memfd_create`**: anonymous, nameless, freed when the last reference goes; seals (`F_SEAL_SHRINK`, `F_SEAL_GROW`, `F_SEAL_WRITE`, `F_SEAL_SEAL`); pass it over a socket.
- **`SCM_CREDENTIALS`**: kernel-checked pid, uid, gid; root may name any pid.
- **`pidfd`**: `pidfd_open` (5.3) refers to the process, not the number; readable when it exits; `waitid` with `P_PIDFD` reaps; `ESRCH` if gone; no pid-reuse race.
- **Netlink**: `AF_NETLINK`; `NETLINK_ROUTE` and generic; `nlmsghdr` plus family header plus attributes; flags `REQUEST`, `DUMP`, `ACK`, `MULTI`; `DONE` ends a dump; `ERROR` carries a negated errno; multicast groups for events; not reliable (`ENOBUFS`, then re-dump); generic families by name through `nlctrl`; `nl80211` is the Wi-Fi family (absent here, `ENOENT`).
- **Data plane**: socket for setup (a memfd and an eventfd), ring for data, eventfd or futex only if sleeping, pidfd for death; indices not pointers; validate everything from the peer.

---

## Sources

- [eventfd(2)](https://man7.org/linux/man-pages/man2/eventfd.2.html) (fetched): counter semantics, `EFD_SEMAPHORE`, the maximum value and `EAGAIN`, readiness for `poll` and `select`, inheritance across `fork` and `execve`, the `EINVAL` conditions. The page does not discuss use from a signal handler; that the `write` system call is async-signal-safe comes from `signal-safety(7)`, fetched for OS 17.
- [futex(2)](https://man7.org/linux/man-pages/man2/futex.2.html) and [futex(7)](https://man7.org/linux/man-pages/man7/futex.7.html) (fetched): the atomic compare and block, four-byte alignment, `FUTEX_PRIVATE_FLAG`, the clock for timeouts, and the statement that the noncontended case is entirely in user space. Neither page gave a mutex example, a description of the kernel queue, robust futexes or priority inheritance; those, the three-state design (Drepper's "Futexes Are Tricky") and the glibc facts are from memory. Per-operation pages such as `FUTEX_WAIT(2const)` were not fetched, so the `EAGAIN`, `ETIMEDOUT` and relative-timeout behaviour is from the measurements.
- [pidfd_open(2)](https://man7.org/linux/man-pages/man2/pidfd_open.2.html) (fetched): the descriptor refers to the process, readable when the last thread exits, close-on-exec by default, zombie guarantee and its conditions, `ESRCH`, Linux 5.3. It does not describe `P_PIDFD` for `waitid`, passing over a socket, or `SO_PEERPIDFD`; those are from the measurement or from memory.
- [memfd_create(2)](https://man7.org/linux/man-pages/man2/memfd_create.2.html) (fetched): anonymous file, name for debugging, `MFD_CLOEXEC`, `MFD_ALLOW_SEALING`, the four seals, release on the last reference, transfer over a Unix socket, Linux 3.17 and glibc 2.27.
- [netlink(7)](https://man7.org/linux/man-pages/man7/netlink.7.html) and [rtnetlink(7)](https://man7.org/linux/man-pages/man7/rtnetlink.7.html) (fetched): the protocol families, message flags and types, multicast groups through `nl_groups`, the unreliability and `ENOBUFS`, the 8192-byte buffer note, `ifinfomsg` and `rtattr`, `RTM_NEWLINK`, `RTM_DELLINK`, `RTM_GETLINK`. `RTMGRP_LINK`, the dump semantics, `NLM_F_DUMP_INTR` and the generic netlink framework are not on those pages; they come from the header files I compiled against and from memory.
- [unix(7)](https://man7.org/linux/man-pages/man7/unix.7.html) (fetched for OS 17): `SCM_RIGHTS` and `SCM_CREDENTIALS` exist and `SO_PEERCRED`. The 253-descriptor limit, the one-byte rule on stream sockets, the receiver's `RLIMIT_NOFILE` and the privilege rule for forged credentials were measured where stated and otherwise are from memory.
- Not fetched, so all claims about them are from memory: `epoll(7)` (the program shows level-triggered behaviour), `waitid(2)`, `cmsg(3)`, the ski-rental argument for spin budgets, `nl80211` and `cfg80211` and their user-space clients, privilege-separated daemons and socket activation, kernel garbage collection of in-flight descriptors, and the LMAX Disruptor and `kfifo` examples.
- Measurements on this machine: Linux 6.18.44 in a Firecracker VM, 2 vCPUs, GCC 13.3, glibc 2.39, six programs, as root. Cross-CPU numbers include the VM's cost of inter-processor interrupts and are not those of bare metal. The spinning throughput varied by ten times between runs, and the explanation offered for it is a hypothesis.
