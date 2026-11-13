# OS 03: Threads

*Part of the [Operating systems plan](00_plan.md) · Time: about 7 hours (4 reading and experiments, 3 problems) · You need: [OS 01](01_os_structure_and_system_calls.md) (system calls), [OS 02](02_processes.md) (processes, `fork`, address space), and C 01 to C 06 for the code; the C track's chapter on concurrency basics is helpful but not required*

A thread is a unit of execution inside a process. Several threads share one address space, so they can work on the same data without copying it, and they can corrupt it just as readily. Interview questions about threads come in a fixed set: what is shared and what is private, how are Linux threads built, what is the difference between a kernel thread and a user thread, why is thread creation cheaper than `fork` and by how much, what is thread-local storage, why does `fork` in a multithreaded program deadlock, and why does a server use a pool of threads instead of one thread per request. This tutorial answers each with programs run on a real Linux kernel, with the output pasted from the runs. Synchronisation (locks, semaphores, deadlock theory) is OS 06; here you meet a race and the three standard answers to it, so you can see why the rest of the track exists.

By the end you can:

- list what threads of one process share and what each has privately, and show it with a program;
- explain how Linux creates threads with `clone`, name the flags that make a thread a thread, and read them from an `strace`;
- compare kernel-level (1:1), user-level (N:1) and hybrid (M:N) threading, and measure a user-level switch;
- write correct `pthread_create` and `pthread_join` code, including passing arguments and returning results, and avoid the `&i` bug;
- use thread-local storage (`__thread`, `pthread_key_t`) and explain why `errno` is not a shared global;
- show a data race, explain why `-O2` can hide it, and fix it with an atomic or a mutex, with the measured cost of each;
- explain the memory cost of a thread stack, and the cost of creating and switching threads against processes;
- explain why `fork` in a multithreaded program can deadlock and show the fix;
- build a bounded thread pool with a clean shutdown and measure it against a thread per task.

**Where the numbers come from.** Every program and command here was run on a Linux 6.18.44 kernel inside a Firecracker micro-VM (2 vCPUs, Intel Xeon at 2.30 GHz per `/proc/cpuinfo`), Ubuntu userland, glibc 2.39 with NPTL (`getconf GNU_LIBPTHREAD_VERSION` printed `NPTL 2.39`), GCC 13.3, with ThreadSanitizer available. Each pool run checks its total against an independently computed value, and the prime counts match the known value for the range. Facts from the man pages were fetched and are linked in the Sources; facts from general knowledge are marked "from memory". `perf` was not installed.

**Data and timing.** Machine: a cloud container (a micro-VM, so the kernel is a guest), 2 vCPUs, `-O2 -pthread`. Timings are the best of 3 loops, shown as run 1 / run 2 (two complete repetitions of the whole benchmark). With only 2 vCPUs, any experiment with more than 2 busy threads shows scheduling, not extra parallelism; the text says so where it matters.

---

## 1. What a thread is

A process (OS 02) has one address space and, until now, one thread of control. A **thread** is an independent flow of execution inside a process: its own program counter, registers and stack, running the same code and sharing the same memory as the other threads of the process. You create threads because some work is concurrent: serving many clients, overlapping I/O with computation, using several CPUs on one data set.

### 1.1 What is shared and what is private

From [pthreads(7)](https://man7.org/linux/man-pages/man7/pthreads.7.html) (fetched), threads of one process share the process ID, parent PID, process group and session, user and group IDs, the open file descriptors, signal dispositions (the handlers), the current and root directories, `umask`, resource limits and CPU time accounting. Each thread has its own thread ID, **signal mask**, **`errno`**, alternate signal stack, scheduling policy and priority, and its **stack**. The address space, meaning the code, globals, the heap and the memory maps, is shared; the page says Linux also makes capabilities and CPU affinity per-thread.

| Shared by all threads | Private to each thread |
|---|---|
| Code, globals, heap, memory maps | Stack (locals, return addresses) |
| Open file descriptors and file offsets | Registers and program counter |
| Signal handlers | Signal mask |
| PID, user ID, working directory | Thread ID, `errno`, thread-local variables |
| Resource limits, CPU accounting | Scheduling priority |

The consequence in one sentence: sharing memory makes communication free and makes **every unsynchronised write a potential bug**.

### 1.2 A thread is not a process, and not very different

Run a program that creates a thread, then forks. It shows what changes in each case:

```c
#define _GNU_SOURCE
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

static int global = 0;

static int tasks_in_process(void)
{
    int n = 0;
    DIR *d = opendir("/proc/self/task");
    struct dirent *e;
    while ((e = readdir(d)))
        if (e->d_name[0] != '.')
            ++n;
    closedir(d);
    return n;
}

static void *thread_fn(void *arg)
{
    (void)arg;
    global += 100;                                   /* same address space: the main thread sees it */
    printf("thread : getpid() == main's pid: %s, gettid() == getpid(): %s\n",
           getpid() == (pid_t)(long)arg ? "yes" : "no", syscall(SYS_gettid) == getpid() ? "yes" : "no");
    fflush(stdout);
    return NULL;
}

int main(void)
{
    pid_t mypid = getpid();
    printf("main   : tasks in /proc/self/task = %d\n", tasks_in_process());
    pthread_t t;
    pthread_create(&t, NULL, thread_fn, (void *)(long)mypid);
    pthread_join(t, NULL);
    printf("after the thread : global = %d (the thread's write is visible)\n", global);

    pid_t pid = fork();
    if (pid == 0) {
        global += 1000;                              /* a copy: the parent does not see it */
        _exit(0);
    }
    waitpid(pid, NULL, 0);
    printf("after the process: global = %d (the child's write is not visible)\n", global);
    return 0;
}
```

```text
main   : tasks in /proc/self/task = 1
thread : getpid() == main's pid: yes, gettid() == getpid(): no
after the thread : global = 100 (the thread's write is visible)
after the process: global = 100 (the child's write is not visible)
```

- The thread's `getpid()` equals the main thread's: **one process ID**, because the thread belongs to the same process. Its `gettid()` differs: each thread has its own ID (the kernel's `pid`; `getpid()` returns the thread-group ID, `Tgid` in `/proc`).
- The thread's write to `global` is visible to main (100); the forked child's write is not (still 100): the child has a copy, the thread has the original.
- `/proc/self/task` has one entry per thread; at the start of this program it listed one.

---

## 2. How Linux builds threads: `clone`

Linux has no separate "thread" object. The kernel schedules **tasks**, each a `task_struct` (OS 02); a process is a group of tasks that share resources. The system call that creates one is `clone`. Its flags say what the new task shares with its creator. From [clone(2)](https://man7.org/linux/man-pages/man2/clone.2.html) (fetched):

| Flag | Effect |
|---|---|
| `CLONE_VM` | Caller and child share the same memory space |
| `CLONE_FS` | Share root directory, working directory and umask |
| `CLONE_FILES` | Share one file descriptor table |
| `CLONE_SIGHAND` | Share the signal handler table (masks stay separate) |
| `CLONE_THREAD` | The child joins the caller's thread group: same TGID and parent |
| `CLONE_SETTLS` | Set the child's thread-local-storage descriptor |
| `CLONE_PARENT_SETTID` | Write the child's thread ID into the parent's memory before `clone` returns |
| `CLONE_CHILD_CLEARTID` | Zero the child's TID in memory on exit and wake any futex waiting on it (this is how `pthread_join` is woken) |

**`fork` and `pthread_create` are the same system call with different flags.** `fork` shares nothing; a thread shares nearly everything. The program below calls `clone` directly with two flag sets. The only difference between them is `CLONE_VM`:

```c
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

static int shared_var = 0;

static int child_fn(void *arg)
{
    (void)arg;
    shared_var = 42;
    return 0;
}

/* clone() with a chosen set of flags: the primitive under both fork and pthread_create */
static void try(const char *name, int flags)
{
    shared_var = 0;
    size_t stk = 1 << 16;
    char *stack = mmap(NULL, stk, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    pid_t pid = clone(child_fn, stack + stk, flags | SIGCHLD, NULL);
    if (pid < 0) { perror("clone"); return; }
    waitpid(pid, NULL, 0);
    printf("%-36s child's write visible to parent: %s\n", name, shared_var == 42 ? "yes" : "no");
    munmap(stack, stk);
}

int main(void)
{
    try("clone(0)            like fork:", 0);
    try("clone(CLONE_VM)      shared memory:", CLONE_VM);
    return 0;
}
```

```text
clone(0)            like fork:       child's write visible to parent: no
clone(CLONE_VM)      shared memory:  child's write visible to parent: yes
```

With no sharing flags the child's write is invisible (a fork-like copy); with `CLONE_VM` it is visible (shared memory). Add the other flags and you have a thread. Look at what glibc actually asks the kernel for. This is `strace -f` of the earlier program, showing the system call behind `pthread_create` and then behind `fork` (long argument values replaced by names):

```text
clone3: CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD|CLONE_SYSVSEM|CLONE_SETTLS|CLONE_PARENT_SETTID|CLONE_CHILD_CLEARTID
clone: CLONE_CHILD_CLEARTID|CLONE_CHILD_SETTID|SIGCHLD
```

- `pthread_create` made a **`clone3`** call (glibc 2.39 uses the newer `clone3`, the superset described on the man page) with `CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD | CLONE_SYSVSEM | CLONE_SETTLS | CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID`: share memory, files, directory and handlers, join the thread group, set up TLS, and report the thread ID. That list is the definition of a Linux thread.
- `fork` made a `clone` with `SIGCHLD` and the tid flags and no sharing flags (glibc implements `fork` with `clone`).

### 2.1 Kernel threads versus user threads

Who does the scheduling? Three models:

| Model | Mapping | Switching | Blocking system call | Uses several CPUs? | Examples |
|---|---|---|---|---|---|
| Kernel threads (1:1) | Each user thread is one kernel task | By the kernel | Blocks only that thread | Yes | Linux NPTL, Windows, macOS |
| User threads (N:1) | Many user threads in one kernel task | By a library, no kernel entry | Blocks all of them | No | Old green-thread libraries |
| Hybrid (M:N) | M user threads over N kernel tasks | Library plus kernel | Runtime must hand off | Yes | Go goroutines, Erlang processes (from memory) |

[pthreads(7)](https://man7.org/linux/man-pages/man7/pthreads.7.html) says the modern implementation is **NPTL**, which uses `clone` and implements synchronisation with `futex`, and that each thread maps to one kernel scheduling entity: the **1:1** model. It replaced LinuxThreads (unsupported since glibc 2.4). The 1:1 model is simple and uses all CPUs; its costs are that each thread is a kernel object with a kernel stack and a creation system call (section 6), and that a switch enters the kernel.

User-level threads avoid the kernel. `swapcontext` from glibc switches between two contexts inside one kernel thread. Measure it:

```c
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <ucontext.h>

/* User-level threads: two contexts switch with swapcontext in one kernel thread. */
static ucontext_t main_ctx, co_ctx;
static long switches;
static long limit;

static void co_main(void)
{
    for (;;) {
        ++switches;
        swapcontext(&co_ctx, &main_ctx);
    }
}

int main(int argc, char **argv)
{
    static char stack[64 * 1024];
    getcontext(&co_ctx);
    co_ctx.uc_stack.ss_sp = stack;
    co_ctx.uc_stack.ss_size = sizeof stack;
    co_ctx.uc_link = &main_ctx;
    makecontext(&co_ctx, co_main, 0);
    limit = argc > 1 ? atol(argv[1]) : 1000000;
    double best = 1e18;
    for (int r = 0; r < 3; ++r) {
        struct timespec a, b;
        clock_gettime(CLOCK_MONOTONIC, &a);
        for (long i = 0; i < limit; ++i)
            swapcontext(&main_ctx, &co_ctx);     /* a round trip is two switches */
        clock_gettime(CLOCK_MONOTONIC, &b);
        double d = ((b.tv_sec - a.tv_sec) * 1e9 + (b.tv_nsec - a.tv_nsec)) / limit / 2;
        if (d < best)
            best = d;
    }
    printf("user-level switch with swapcontext: %.0f ns per switch (coroutine ran %ld times)\n", best, switches);
    return 0;
}
```

```text
user-level switch with swapcontext: 175 ns per switch (coroutine ran 3000000 times)
user-level switch with swapcontext: 175 ns per switch (coroutine ran 3000000 times)
```

That is 175 ns per switch, over a hundred times a plain function call but far below the microsecond a kernel thread switch costs (section 6). It is not as cheap as it could be, though: running `strace -c` on 1000 round trips counted

```text
6001 rt_sigprocmask calls for 6000 switches (3 repetitions of 1000 round trips)
```

so glibc's `swapcontext` makes **one `rt_sigprocmask` system call per switch**, to save and restore the signal mask as POSIX requires. A user-level scheduler that wants the full gain writes its own switch in a few lines of assembly that saves registers only (from memory; Go's runtime does this). The lesson for an interview: "user-level switching avoids the kernel" is true in design, and the library must also avoid the kernel in implementation.

---

## 3. The pthreads API

The basics (POSIX threads, from the C library, link with `-pthread`):

| Call | Does |
|---|---|
| `pthread_create(&t, attr, fn, arg)` | Start a thread running `fn(arg)`; returns 0 or an **error number** (not `errno`) |
| `pthread_join(t, &ret)` | Wait for `t` to end; collect its return value; free its resources |
| `pthread_detach(t)` | Say nobody will join; resources are freed at exit |
| `pthread_exit(v)` | End this thread |
| `pthread_self()` | This thread's ID |

[pthread_create(3)](https://man7.org/linux/man-pages/man3/pthread_create.3.html) (fetched) says: on error it returns an error number, not `-1`; the single `arg` is passed to the start routine; and when the main thread returns from `main` (or any thread calls `exit`), **all threads are terminated**. A common bug is a `main` that returns while workers are still running, so they never finish.

### 3.1 Passing arguments: the `&i` bug

`pthread_create` takes one `void *`. The classic mistake is to pass the address of the loop variable, which the loop then changes. The program starts four threads, holds them until all are created, then lets them read their argument:

```c
/* Problem A: what does each thread print? All threads start after the loop has finished. */
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t go = PTHREAD_COND_INITIALIZER;
static int released;
static int seen_buggy[4], seen_fixed[4];

static void wait_for_release(void)
{
    pthread_mutex_lock(&gate);
    while (!released)
        pthread_cond_wait(&go, &gate);
    pthread_mutex_unlock(&gate);
}

static void *buggy(void *arg)
{
    wait_for_release();
    int slot = *(int *)arg;                  /* reads the loop variable of main, whenever the thread gets to run */
    (void)slot;
    return arg;
}

static void *fixed(void *arg)
{
    wait_for_release();
    return arg;                              /* the value itself was passed, nothing is read later */
}

int main(void)
{
    pthread_t t[4];
    int i;
    for (i = 0; i < 4; ++i)
        pthread_create(&t[i], NULL, buggy, &i);          /* BUG: every thread gets the address of the same variable */
    pthread_mutex_lock(&gate);
    released = 1;
    pthread_cond_broadcast(&go);
    pthread_mutex_unlock(&gate);
    for (int k = 0; k < 4; ++k) {
        void *r;
        pthread_join(t[k], &r);
        seen_buggy[k] = *(int *)r;                       /* i is 4 now: the loop variable after the last increment */
    }
    printf("passing &i:       thread 0..3 read %d %d %d %d\n", seen_buggy[0], seen_buggy[1], seen_buggy[2], seen_buggy[3]);

    released = 0;
    int ids[4] = {0, 1, 2, 3};
    for (int k = 0; k < 4; ++k)
        pthread_create(&t[k], NULL, fixed, &ids[k]);     /* one slot per thread */
    pthread_mutex_lock(&gate);
    released = 1;
    pthread_cond_broadcast(&go);
    pthread_mutex_unlock(&gate);
    for (int k = 0; k < 4; ++k) {
        void *r;
        pthread_join(t[k], &r);
        seen_fixed[k] = *(int *)r;
    }
    printf("passing &ids[k]:  thread 0..3 read %d %d %d %d\n", seen_fixed[0], seen_fixed[1], seen_fixed[2], seen_fixed[3]);
    return 0;
}
```

```text
passing &i:       thread 0..3 read 4 4 4 4
passing &ids[k]:  thread 0..3 read 0 1 2 3
```

All four threads read **4**: `&i` pointed at the same variable, and by the time they ran the loop had finished with `i == 4`. (Without the gate the values depend on timing, which is why this bug is intermittent in real code.) The fixes: give each thread its own slot (`&ids[k]`, shown), or pass the value itself as the pointer, or allocate a struct per thread.

### 3.2 Returning results

A thread can return a `void *`, or write into a struct that it was handed. The second is clearer for several values. `pthread_join` also **orders memory**: everything the thread wrote before it returned is visible to the joiner afterwards (the POSIX rule; from memory). Two threads sum halves of an array:

```c
/* Problem B: sum an array with two threads; each returns its partial sum through pthread_join. */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    const long *a;
    long lo, hi;            /* range [lo, hi) */
    long sum;               /* result, written by the thread, read after the join */
} Job;

static void *partial_sum(void *p)
{
    Job *j = p;
    long s = 0;
    for (long i = j->lo; i < j->hi; ++i)
        s += j->a[i];
    j->sum = s;
    return NULL;
}

int main(void)
{
    enum { N = 1000000 };
    long *a = malloc(sizeof *a * N);
    long want = 0;
    for (long i = 0; i < N; ++i) {
        a[i] = i % 1000;
        want += a[i];
    }
    Job jobs[2] = {{a, 0, N / 2, 0}, {a, N / 2, N, 0}};
    pthread_t t[2];
    for (int k = 0; k < 2; ++k)
        pthread_create(&t[k], NULL, partial_sum, &jobs[k]);
    for (int k = 0; k < 2; ++k)
        pthread_join(t[k], NULL);                  /* the join also makes the results visible to main */
    long got = jobs[0].sum + jobs[1].sum;
    printf("partial sums %ld and %ld, total %ld, expected %ld: %s\n", jobs[0].sum, jobs[1].sum, got, want, got == want ? "match" : "MISMATCH");
    free(a);
    return 0;
}
```

```text
partial sums 249750000 and 249750000, total 499500000, expected 499500000: match
```

---

## 4. Thread-local storage

A global variable is shared. Sometimes you want a global-looking name with one copy per thread. **Thread-local storage** (TLS) gives each thread its own copy. In C: the `__thread` storage class (GNU) or `_Thread_local` (C11). At the library level: `pthread_key_create`, `pthread_setspecific`, `pthread_getspecific`. The best-known example is **`errno`**: it looks like a global but must be per-thread, or one thread's failing call would overwrite another's error code.

```c
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static __thread int counter = 0;                     /* one copy per thread */
static int shared = 0;                               /* one copy for all (updated under a lock) */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_key_t key;                            /* the library-level equivalent */

static void *worker(void *arg)
{
    long id = (long)arg;
    for (int i = 0; i < 1000; ++i) {
        ++counter;
        pthread_mutex_lock(&lock);
        ++shared;
        pthread_mutex_unlock(&lock);
    }
    errno = (int)id;                                 /* errno is thread-local: no interference */
    pthread_setspecific(key, (void *)(id * 10));
    printf("thread %ld: its counter = %d, its errno = %d, its key value = %ld\n",
           id, counter, errno, (long)pthread_getspecific(key));
    fflush(stdout);
    return NULL;
}

int main(void)
{
    pthread_key_create(&key, NULL);
    pthread_t t[3];
    for (long i = 0; i < 3; ++i) {
        pthread_create(&t[i], NULL, worker, (void *)(i + 1));
        pthread_join(t[i], NULL);                    /* one at a time so the printed lines come out in order */
    }
    printf("main    : its counter = %d, shared = %d\n", counter, shared);
    return 0;
}
```

```text
thread 1: its counter = 1000, its errno = 1, its key value = 10
thread 2: its counter = 1000, its errno = 2, its key value = 20
thread 3: its counter = 1000, its errno = 3, its key value = 30
main    : its counter = 0, shared = 3000
```

Each thread counted to 1000 in its own `counter` (main's own copy stayed 0); `errno` kept each thread's own value; the key mechanism does the same dynamically. The `shared` counter reached 3000 because it was updated under a mutex.

How it works (from memory): the kernel sets a segment register (`FS` on x86-64) to the base of a per-thread block, which is why the `clone3` call above includes `CLONE_SETTLS`. A `__thread` variable is an offset from that base, so access costs about one extra instruction. Switching threads switches the `FS` base like any other register. Consequence: TLS makes the thread's data reachable without a lock, because no other thread has the same address.

---

## 5. A data race, and three ways to answer it

Two threads incrementing one counter 2,000,000 times each should reach 4,000,000. A `counter++` is a read, an add and a write; if two threads interleave them, one update is lost. The program tries four versions:

```c
#define _GNU_SOURCE
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>

enum { THREADS = 2, PER = 2000000 };
static long plain = 0;
static volatile long vplain = 0;
static atomic_long atom = 0;
static long locked = 0;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static void *racy(void *a) { (void)a; for (long i = 0; i < PER; ++i) plain++; return NULL; }
static void *racy_volatile(void *a) { (void)a; for (long i = 0; i < PER; ++i) vplain++; return NULL; }
static void *atomic_(void *a) { (void)a; for (long i = 0; i < PER; ++i) atomic_fetch_add(&atom, 1); return NULL; }
static void *mutexed(void *a)
{
    (void)a;
    for (long i = 0; i < PER; ++i) {
        pthread_mutex_lock(&lock);
        locked++;
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}

static double run(void *(*fn)(void *))
{
    pthread_t t[THREADS];
    double t0 = now_ms();
    for (int i = 0; i < THREADS; ++i) pthread_create(&t[i], NULL, fn, NULL);
    for (int i = 0; i < THREADS; ++i) pthread_join(t[i], NULL);
    return now_ms() - t0;
}

int main(void)
{
    double a = run(racy), v = run(racy_volatile), b = run(atomic_), c = run(mutexed);
    long want = (long)THREADS * PER;
    printf("expected total each: %ld\n", want);
    printf("plain  counter++ (-O2):  %ld (lost %ld)   time %6.1f ms\n", plain, want - plain, a);
    printf("volatile counter++   : %ld (lost %ld)   time %6.1f ms\n", vplain, want - vplain, v);
    printf("atomic fetch_add     : %ld (lost %ld)   time %6.1f ms\n", (long)atom, want - (long)atom, b);
    printf("mutex lock/unlock    : %ld (lost %ld)   time %6.1f ms\n", locked, want - locked, c);
    return 0;
}
```

Run 1:

```text
expected total each: 4000000
plain  counter++ (-O2):  4000000 (lost 0)   time    0.1 ms
volatile counter++   : 2201315 (lost 1798685)   time    4.5 ms
atomic fetch_add     : 4000000 (lost 0)   time   81.5 ms
mutex lock/unlock    : 4000000 (lost 0)   time  349.0 ms
```

Run 2:

```text
expected total each: 4000000
plain  counter++ (-O2):  4000000 (lost 0)   time    0.2 ms
volatile counter++   : 2008528 (lost 1991472)   time    6.3 ms
atomic fetch_add     : 4000000 (lost 0)   time   96.7 ms
mutex lock/unlock    : 4000000 (lost 0)   time  374.1 ms
```

How to read it:

1. **The plain `long` counter lost nothing, in 0.1 to 0.2 ms.** Not because it is safe: at `-O2` the compiler kept the counter in a register for the whole loop and wrote it back once, so the threads never raced on it in this build. A data race is **undefined behaviour**: a program can look correct on one compiler and one set of flags and be wrong on another. Absence of a failure proves nothing.
2. **The `volatile` counter lost about half the updates** (1.8 to 2.0 million of 4 million), every run. `volatile` forces each access to memory but does not make the read-add-write atomic. (`volatile` is for device registers and signal flags, not for threads.)
3. **The atomic increment** (`atomic_fetch_add`, one `lock xadd` instruction on x86-64, from memory) lost nothing and took about 80 to 100 ms.
4. **The mutex** lost nothing and took about 350 to 375 ms, about four times the atomic. Each pair `lock` and `unlock` was a function call; under contention a failed `lock` may enter the kernel (a `futex` call, from memory).

Choose in this order: **no shared writes** (per-thread data, combined at the end), then an **atomic** for one variable, then a **mutex** for anything larger. The mutex is the general tool; the atomic is faster and works only for single operations.

A tool finds this class of bug without waiting for a bad run. Compiling the same program with `-fsanitize=thread` and running it gave:

```text
1 SUMMARY: ThreadSanitizer: data race   in racy
2 SUMMARY: ThreadSanitizer: data race   in racy_volatile
3 WARNING: ThreadSanitizer: data race (pid=25847)
```

ThreadSanitizer reported a data race in `racy` (the plain counter, which showed no wrong result) and two in `racy_volatile`, and **nothing** for the atomic and mutex versions. So it caught the bug the output hid. (TSan reports the first occurrence of each race and slows the program by a large factor; use it in tests.)

The full theory (critical sections, locks, semaphores, deadlock, memory ordering) is OS 06, OS 07 and OS 08.

---

## 6. What a thread costs

### 6.1 Time: creating and switching

```c
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static double now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e9 + t.tv_nsec;
}

static void *nop(void *a) { return a; }

/* ping-pong between two threads with a mutex and two condition variables */
static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t c_ping = PTHREAD_COND_INITIALIZER, c_pong = PTHREAD_COND_INITIALIZER;
static int turn = 0;                                 /* 0: main's turn, 1: partner's turn */
static long rounds;

static void *partner(void *a)
{
    (void)a;
    cpu_set_t s;
    CPU_ZERO(&s);
    CPU_SET((long)a, &s);
    sched_setaffinity(0, sizeof s, &s);
    pthread_mutex_lock(&m);
    for (long i = 0; i < rounds; ++i) {
        while (turn != 1)
            pthread_cond_wait(&c_pong, &m);
        turn = 0;
        pthread_cond_signal(&c_ping);
    }
    pthread_mutex_unlock(&m);
    return NULL;
}

static double thread_pingpong(int partner_cpu, long n)
{
    rounds = n;
    turn = 0;
    cpu_set_t s;
    CPU_ZERO(&s);
    CPU_SET(0, &s);
    sched_setaffinity(0, sizeof s, &s);
    pthread_t t;
    pthread_create(&t, NULL, partner, (void *)(long)partner_cpu);
    double t0 = now_ns();
    pthread_mutex_lock(&m);
    for (long i = 0; i < n; ++i) {
        turn = 1;
        pthread_cond_signal(&c_pong);
        while (turn != 0)
            pthread_cond_wait(&c_ping, &m);
    }
    pthread_mutex_unlock(&m);
    double d = (now_ns() - t0) / n;
    pthread_join(t, NULL);
    return d;
}

static double process_pingpong(int partner_cpu, long n)
{
    int a[2], b[2];
    pipe(a);
    pipe(b);
    pid_t pid = fork();
    if (pid == 0) {
        cpu_set_t s;
        CPU_ZERO(&s);
        CPU_SET(partner_cpu, &s);
        sched_setaffinity(0, sizeof s, &s);
        close(a[1]);
        char c;
        while (read(a[0], &c, 1) == 1)
            write(b[1], &c, 1);
        _exit(0);
    }
    cpu_set_t s;
    CPU_ZERO(&s);
    CPU_SET(0, &s);
    sched_setaffinity(0, sizeof s, &s);
    char c = 'x';
    double t0 = now_ns();
    for (long i = 0; i < n; ++i) {
        write(a[1], &c, 1);
        read(b[0], &c, 1);
    }
    double d = (now_ns() - t0) / n;
    close(a[1]);
    waitpid(pid, NULL, 0);
    return d;
}

#define BEST(var, N, stmt) do { var = 1e18; for (int rep = 0; rep < 3; ++rep) { double t0 = now_ns(); \
    for (int i = 0; i < (N); ++i) { stmt; } double d = (now_ns() - t0) / (N); if (d < var) var = d; } } while (0)

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    double t;
    BEST(t, 2000, { pthread_t th; pthread_create(&th, NULL, nop, NULL); pthread_join(th, NULL); });
    printf("pthread_create + join                       %9.0f ns\n", t);
    BEST(t, 1000, { pid_t p = fork(); if (p == 0) _exit(0); waitpid(p, NULL, 0); });
    printf("fork + wait (small parent)                  %9.0f ns\n", t);
    double best = 1e18;
    for (int r = 0; r < 3; ++r) { double d = thread_pingpong(0, 100000); if (d < best) best = d; }
    printf("thread round trip, same CPU (mutex+condvar) %9.0f ns\n", best);
    best = 1e18;
    for (int r = 0; r < 3; ++r) { double d = process_pingpong(0, 100000); if (d < best) best = d; }
    printf("process round trip, same CPU (pipes)        %9.0f ns\n", best);
    best = 1e18;
    for (int r = 0; r < 3; ++r) { double d = thread_pingpong(1, 50000); if (d < best) best = d; }
    printf("thread round trip, two CPUs                 %9.0f ns\n", best);
    best = 1e18;
    for (int r = 0; r < 3; ++r) { double d = process_pingpong(1, 50000); if (d < best) best = d; }
    printf("process round trip, two CPUs                %9.0f ns\n", best);
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    BEST(t, 20000000, { pthread_mutex_lock(&mu); pthread_mutex_unlock(&mu); });
    printf("uncontended mutex lock + unlock             %9.1f ns\n", t);
    return 0;
}
```

| Operation | Time (run 1 / run 2) |
|---|---|
| `pthread_create` + `pthread_join` | 31.0 / 29.8 µs |
| `fork` + wait, small parent | 133.7 / 127.3 µs |
| Thread round trip, same CPU (mutex and condition variables) | 2.6 / 2.7 µs |
| Process round trip, same CPU (two pipes) | 1.8 / 1.8 µs |
| Thread round trip, two CPUs | 21.5 / 23.0 µs |
| Process round trip, two CPUs | 22.7 / 23.4 µs |
| Uncontended mutex lock + unlock | 13.3 / 13.1 ns |

- **Creating a thread costs about 30 µs, four times less than a `fork` (about 130 µs here).** A thread needs no page-table copy (OS 02 showed `fork` growing from 80 µs to 14 ms with the parent's size; thread creation does not depend on it). What remains is the `clone3` call, setting up a stack and the TLS block, and the join. A pool (section 8) removes even that.
- **The same-CPU thread round trip was slower than the process one (2.6 µs against 1.8 µs).** This is the opposite of the textbook "thread switches are cheaper than process switches", and worth stating plainly. A thread switch does skip changing the address space, but the two programs do different work: the pipe version is four `read` and `write` system calls and two switches, while the condition-variable version is a mutex handoff, a `futex` wait and a `futex` wake on each side (from memory about what the library does; I did not count its system calls). The difference is in the synchronisation primitive, not in the switch itself. **A measured "thread switch" is the switch plus whatever primitive caused it**; to isolate the switch itself you need a lower-level test than this one.
- **Across two CPUs both cost about 21 to 23 µs**, about eight times the same-CPU figure: waking a task on another CPU needs an inter-processor interrupt, and in a VM that can need a host exit. The address-space difference vanishes in that noise.
- An **uncontended mutex lock and unlock cost 13 ns**: an atomic operation in user space with no system call. A mutex only costs a context switch when it is contended.

### 6.2 Memory: stacks

Each thread has its own stack. Under NPTL its default size is the `RLIMIT_STACK` soft limit when that is not "unlimited" (here `ulimit -s` is 8192 KiB, so 8 MiB; if it is unlimited the default is 2 MB on most architectures, from the `pthread_create` man page, fetched). The memory is **reserved** (virtual) but **committed only as touched** (OS 02, copy-on-write and demand paging). The program creates N idle threads and reads `VmSize` and `VmRSS` from `/proc/self/status`:

```c
#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t go = PTHREAD_COND_INITIALIZER;
static int released;

static void *idle(void *a)
{
    (void)a;
    pthread_mutex_lock(&gate);
    while (!released)
        pthread_cond_wait(&go, &gate);
    pthread_mutex_unlock(&gate);
    return NULL;
}

static long status_kb(const char *key)
{
    FILE *f = fopen("/proc/self/status", "r");
    char line[256];
    long v = -1;
    size_t n = strlen(key);
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, key, n))
            v = atol(line + n);
    fclose(f);
    return v;
}

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 1000;
    size_t stack = (argc > 2 ? (size_t)atol(argv[2]) : 8192) * 1024;
    long vm0 = status_kb("VmSize:"), rss0 = status_kb("VmRSS:");
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, stack);
    pthread_t *t = malloc(sizeof *t * (size_t)n);
    int made = 0;
    for (int i = 0; i < n; ++i) {
        int rc = pthread_create(&t[i], &at, idle, NULL);
        if (rc) { printf("pthread_create failed at thread %d: %s\n", i, strerror(rc)); break; }
        ++made;
    }
    usleep(200000);
    printf("threads created: %d, stack size %zu KiB\n", made, stack >> 10);
    printf("VmSize grew by %ld MiB, VmRSS grew by %ld MiB\n", (status_kb("VmSize:") - vm0) >> 10, (status_kb("VmRSS:") - rss0) >> 10);
    pthread_mutex_lock(&gate);
    released = 1;
    pthread_cond_broadcast(&go);
    pthread_mutex_unlock(&gate);
    for (int i = 0; i < made; ++i)
        pthread_join(t[i], NULL);
    return 0;
}
```

```text
1000 threads, default 8 MiB stack:
threads created: 1000, stack size 8192 KiB
VmSize grew by 8004 MiB, VmRSS grew by 8 MiB
1000 threads, 64 KiB stack:
threads created: 1000, stack size 64 KiB
VmSize grew by 66 MiB, VmRSS grew by 8 MiB
10000 threads, 64 KiB stack:
threads created: 10000, stack size 64 KiB
VmSize grew by 666 MiB, VmRSS grew by 81 MiB
```

- 1000 threads at 8 MiB reserved **8 GiB of address space** (`VmSize` +8004 MiB) but used only **8 MiB of RAM** (`VmRSS`): about 8 KiB per thread, the few pages each had touched.
- Shrinking the stack to 64 KiB cut the reservation to 66 MiB without changing RSS.
- 10,000 threads with 64 KiB stacks used 81 MiB RSS, about 8 KiB each, and `pthread_create` did not fail. The kernel also holds a kernel stack and a `task_struct` per thread (not in `VmRSS`, from memory).
- Limits: `/proc/sys/kernel/threads-max` is 63992 here, and `pid_max` is 32768; every thread takes an ID from the same pool as processes, so those bound the number of threads (from memory; I did not create 30,000 to test it). On a 32-bit machine, the 8 MiB virtual stacks are what ran out of address space.

A thread per connection therefore costs about 8 KiB of RAM plus kernel structures, which is fine for thousands of connections and poor for hundreds of thousands, which is why event-driven servers (OS 15, `epoll` and `io_uring`) exist.

---

## 7. `fork` in a multithreaded program

`fork` copies the address space but, as [fork(2)](https://man7.org/linux/man-pages/man2/fork.2.html) says (fetched in OS 02), **only the calling thread** exists in the child. The memory, including every mutex, is copied in whatever state it was in at that moment. If another thread held a lock when the fork happened, the child's copy of the mutex is locked, and **the thread that would unlock it does not exist in the child**. [pthread_atfork(3)](https://man7.org/linux/man-pages/man3/pthread_atfork.3.html) (fetched) states this: mutexes held by other threads may never be unlocked in the child, and the child should call only async-signal-safe functions until it calls `exec`.

The program starts a thread that holds a mutex almost all the time, then forks ten times; each child tries to take the mutex and is killed by a timer after 0.2 s if it blocks:

```c
#define _GNU_SOURCE
#include <dirent.h>
#include <pthread.h>
#include <signal.h>
#include <sys/time.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static int stop;

static int tasks(void)
{
    int n = 0;
    DIR *d = opendir("/proc/self/task");
    struct dirent *e;
    while ((e = readdir(d)))
        if (e->d_name[0] != '.')
            ++n;
    closedir(d);
    return n;
}

static void *holder(void *a)
{
    (void)a;
    pthread_mutex_lock(&lock);                  /* this thread holds the lock while main forks */
    while (!stop)
        pthread_cond_wait(&cv, &lock);          /* (wait releases it, so main signals instead of waiting here) */
    pthread_mutex_unlock(&lock);
    return NULL;
}

static void *hog(void *a)
{
    (void)a;
    for (;;) {
        pthread_mutex_lock(&lock);
        usleep(1000);                           /* holds the lock almost all the time */
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}

int main(void)
{
    (void)holder;
    pthread_t t;
    pthread_create(&t, NULL, hog, NULL);
    usleep(50000);
    printf("parent has %d threads before the fork\n", tasks());
    fflush(stdout);
    int hung = 0, ok = 0;
    for (int trial = 0; trial < 10; ++trial) {
        pid_t pid = fork();
        if (pid == 0) {
            setitimer(ITIMER_REAL, &(struct itimerval){{0, 0}, {0, 200000}}, NULL);   /* kill the child after 0.2 s if it deadlocks */
            if (trial == 0)
                printf("child  has %d thread(s) after the fork\n", tasks());
            fflush(stdout);
            pthread_mutex_lock(&lock);          /* the lock may be copied in the locked state */
            pthread_mutex_unlock(&lock);
            _exit(0);
        }
        int st;
        waitpid(pid, &st, 0);
        if (WIFSIGNALED(st) && WTERMSIG(st) == SIGALRM)
            ++hung;
        else
            ++ok;
    }
    printf("10 forks while another thread held a mutex: %d children locked up (killed by alarm), %d finished\n", hung, ok);
    return 0;
}
```

```text
parent has 2 threads before the fork
child  has 1 thread(s) after the fork
10 forks while another thread held a mutex: 10 children locked up (killed by alarm), 0 finished
```

The child has one thread, and **all 10 children locked up**: the lock was held at the moment of the fork, in every trial. (In real programs the hog is a logging or `malloc` lock, and the hang is rare and unrepeatable, which is what makes it expensive to debug.)

The standard fixes: call only `exec` (or `_exit`) in the child; or use `pthread_atfork` to take every lock in the `prepare` handler (so no other thread holds it at the instant of the fork) and release it in both the parent and the child. The second version of the program with that fix:

```c
/* Problem D fix: the same experiment as d7, with pthread_atfork making the lock safe across fork. */
#define _GNU_SOURCE
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static void *hog(void *a)
{
    (void)a;
    for (;;) {
        pthread_mutex_lock(&lock);
        usleep(1000);
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}

static void before_fork(void) { pthread_mutex_lock(&lock); }       /* wait until no other thread holds it */
static void after_fork(void) { pthread_mutex_unlock(&lock); }      /* the parent and the child each release their copy */

int main(void)
{
    pthread_atfork(before_fork, after_fork, after_fork);
    pthread_t t;
    pthread_create(&t, NULL, hog, NULL);
    usleep(50000);
    int hung = 0, ok = 0;
    for (int trial = 0; trial < 10; ++trial) {
        pid_t pid = fork();
        if (pid == 0) {
            setitimer(ITIMER_REAL, &(struct itimerval){{0, 0}, {0, 200000}}, NULL);
            pthread_mutex_lock(&lock);
            pthread_mutex_unlock(&lock);
            _exit(0);
        }
        int st;
        waitpid(pid, &st, 0);
        if (WIFSIGNALED(st) && WTERMSIG(st) == SIGALRM)
            ++hung;
        else
            ++ok;
    }
    printf("with pthread_atfork: 10 forks while another thread held a mutex: %d locked up, %d finished\n", hung, ok);
    return 0;
}
```

```text
with pthread_atfork: 10 forks while another thread held a mutex: 0 locked up, 10 finished
```

Zero children locked up. The man page calls the general goal of `pthread_atfork` "too difficult to be practicable": it works for a lock you own, and no one can take every lock of every library. Prefer `fork` then `exec`, or `posix_spawn`; and do not `fork` after threads exist unless the child will `exec`.

Two more rules from the same man pages: when any thread calls `exec`, all other threads in the process are terminated; and a signal sent to the process (not to a particular thread) is delivered to **one arbitrary thread** that has it unblocked, so a program that cares which thread handles `SIGTERM` blocks it in all the others (`pthread_sigmask`, from memory).

---

## 8. Thread pools

Creating a thread costs about 30 µs. For a task that takes 1 µs that is a 30-times overhead, and an unlimited number of threads exhausts memory. A **thread pool** starts a fixed set of workers once and feeds them tasks through a queue. The design questions are the same every time:

1. **The queue**: bounded (so a fast producer waits instead of exhausting memory) or unbounded.
2. **Waiting**: workers sleep on a condition variable when the queue is empty, the producer sleeps when it is full.
3. **Shutdown**: a flag that says no more tasks; a broadcast to wake idle workers so they see it; workers exit only when the queue is empty, so every submitted task runs; `join` each worker.
4. **Running the task outside the lock**: otherwise the pool serialises the work.

The implementation below does all four, and the program runs 20,000 small tasks through pools of 1, 2 and 4 workers and through a thread per task, checking every total:

```c
/* Problem E: a thread pool with a bounded queue, graceful shutdown, and a timing comparison. */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef void (*TaskFn)(void *);
typedef struct { TaskFn fn; void *arg; } Task;

typedef struct {
    pthread_mutex_t m;
    pthread_cond_t not_empty, not_full;
    Task q[64];
    int head, count;
    int closing;                    /* no more submissions; workers exit when the queue is empty */
    pthread_t *workers;
    int nworkers;
} Pool;

static void *worker(void *p)
{
    Pool *pool = p;
    for (;;) {
        pthread_mutex_lock(&pool->m);
        while (pool->count == 0 && !pool->closing)
            pthread_cond_wait(&pool->not_empty, &pool->m);
        if (pool->count == 0) {                       /* closing and drained */
            pthread_mutex_unlock(&pool->m);
            return NULL;
        }
        Task t = pool->q[pool->head];
        pool->head = (pool->head + 1) % 64;
        --pool->count;
        pthread_cond_signal(&pool->not_full);
        pthread_mutex_unlock(&pool->m);
        t.fn(t.arg);                                  /* run the task outside the lock */
    }
}

static void pool_start(Pool *p, int n)
{
    pthread_mutex_init(&p->m, NULL);
    pthread_cond_init(&p->not_empty, NULL);
    pthread_cond_init(&p->not_full, NULL);
    p->head = p->count = p->closing = 0;
    p->nworkers = n;
    p->workers = malloc(sizeof(pthread_t) * (size_t)n);
    for (int i = 0; i < n; ++i)
        pthread_create(&p->workers[i], NULL, worker, p);
}

static void pool_submit(Pool *p, TaskFn fn, void *arg)
{
    pthread_mutex_lock(&p->m);
    while (p->count == 64)                            /* bounded: the producer waits when the queue is full */
        pthread_cond_wait(&p->not_full, &p->m);
    p->q[(p->head + p->count) % 64] = (Task){fn, arg};
    ++p->count;
    pthread_cond_signal(&p->not_empty);
    pthread_mutex_unlock(&p->m);
}

static void pool_shutdown(Pool *p)
{
    pthread_mutex_lock(&p->m);
    p->closing = 1;
    pthread_cond_broadcast(&p->not_empty);            /* wake every idle worker so it sees the flag */
    pthread_mutex_unlock(&p->m);
    for (int i = 0; i < p->nworkers; ++i)
        pthread_join(p->workers[i], NULL);            /* all queued tasks have run when the joins return */
    free(p->workers);
}

static long total;                                    /* the tasks add into this */
static pthread_mutex_t total_lock = PTHREAD_MUTEX_INITIALIZER;

static void task_square(void *arg)
{
    long v = (long)arg, s = 0;
    for (int i = 0; i < 200; ++i)                     /* a little work so a task is not empty */
        s += (v * v + i) % 7;
    pthread_mutex_lock(&total_lock);
    total += s;
    pthread_mutex_unlock(&total_lock);
}

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static void *thread_per_task(void *arg) { task_square(arg); return NULL; }

int main(void)
{
    enum { TASKS = 20000 };
    long want = 0;
    for (long v = 0; v < TASKS; ++v)
        for (int i = 0; i < 200; ++i)
            want += (v * v + i) % 7;
    for (int workers = 1; workers <= 4; workers *= 2) {
        total = 0;
        double t0 = now_ms();
        Pool p;
        pool_start(&p, workers);
        for (long v = 0; v < TASKS; ++v)
            pool_submit(&p, task_square, (void *)v);
        pool_shutdown(&p);
        printf("pool of %d worker(s): %d tasks, total %s, %7.1f ms\n", workers, TASKS, total == want ? "correct" : "WRONG", now_ms() - t0);
    }
    total = 0;
    double t0 = now_ms();
    for (long v = 0; v < TASKS; ++v) {
        pthread_t t;
        pthread_create(&t, NULL, thread_per_task, (void *)v);
        pthread_join(t, NULL);
    }
    printf("thread per task:     %d tasks, total %s, %7.1f ms\n", TASKS, total == want ? "correct" : "WRONG", now_ms() - t0);
    return 0;
}
```

Run 1:

```text
pool of 1 worker(s): 20000 tasks, total correct,    13.7 ms
pool of 2 worker(s): 20000 tasks, total correct,    13.3 ms
pool of 4 worker(s): 20000 tasks, total correct,    13.9 ms
thread per task:     20000 tasks, total correct,   598.8 ms
```

Run 2:

```text
pool of 1 worker(s): 20000 tasks, total correct,    14.5 ms
pool of 2 worker(s): 20000 tasks, total correct,    12.8 ms
pool of 4 worker(s): 20000 tasks, total correct,    13.6 ms
thread per task:     20000 tasks, total correct,   575.9 ms
```

- Every total is correct.
- A **thread per task took 576 to 702 ms, against 13 to 16 ms for a pool**: about 40 to 45 times more. It is the 30 µs per thread of section 6.1 multiplied by 20,000, the whole cost of creating and joining.
- The pool with 2 workers was **no faster than with 1** (run 1: 15.7 and 15.1 ms; run 2: 14.5 and 12.8 ms), and 4 workers were no faster either (22.5 and 13.6 ms; the run-to-run spread is itself a sign of the queue contention). A task here is about a microsecond of work, so the queue's mutex and condition variable are the bottleneck: workers wait for each other at the lock and wake each other with `futex` calls. That is a result worth remembering: **a pool helps when tasks are large enough to dominate the queue cost**. For tiny tasks, give each worker a batch, or give each worker its own queue.
- With only 2 vCPUs, 4 workers cannot run more in parallel than 2; the extra ones only add switching.

---

## 9. Parallel speedup is not free

Counting primes below 6,000,000 by trial division splits cleanly across threads:

```c
/* Problem C: count primes below N with 1, 2 and 4 threads and report the speedup (this machine has 2 vCPUs). */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <time.h>

enum { N = 6000000 };

typedef struct { long lo, hi, count; } Range;

static int is_prime(long n)
{
    if (n < 2) return 0;
    for (long d = 2; d * d <= n; ++d)
        if (n % d == 0) return 0;
    return 1;
}

static void *count_range(void *p)
{
    Range *r = p;
    long c = 0;
    for (long n = r->lo; n < r->hi; ++n)
        c += is_prime(n);
    r->count = c;
    return NULL;
}

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

int main(void)
{
    double base = 0;
    for (int threads = 1; threads <= 4; threads *= 2) {
        Range r[4];
        pthread_t t[4];
        double t0 = now_ms();
        /* contiguous ranges: the upper range holds larger numbers, so its thread has more work (imbalance) */
        for (int k = 0; k < threads; ++k) {
            r[k].lo = (long)N * k / threads;
            r[k].hi = (long)N * (k + 1) / threads;
        }
        for (int k = 0; k < threads; ++k) pthread_create(&t[k], NULL, count_range, &r[k]);
        long total = 0;
        for (int k = 0; k < threads; ++k) { pthread_join(t[k], NULL); total += r[k].count; }
        double ms = now_ms() - t0;
        if (threads == 1) base = ms;
        printf("%d thread(s): primes below %d = %ld, %7.0f ms, speedup %.2f\n", threads, N, total, ms, base / ms);
    }
    return 0;
}
```

```text
1 thread(s): primes below 6000000 = 412849,    2241 ms, speedup 1.00
2 thread(s): primes below 6000000 = 412849,    1425 ms, speedup 1.57
4 thread(s): primes below 6000000 = 412849,    1134 ms, speedup 1.98
```

The count (412,849) is the same every time, and the table shows what limited the speed:

- **Two threads gave a speedup of 1.6 on 2 CPUs, not 2.0.** The work is split into two contiguous ranges, and the upper range contains larger numbers, which take longer to test, so one thread finishes late (**load imbalance**).
- **Four threads gave 1.87 on 2 CPUs**, better than two threads, because more smaller ranges let the scheduler balance the load, not because there are more CPUs.
- The usual bound is **Amdahl's law**: if a fraction `s` of the work is serial, the speedup with `n` processors is at most `1 / (s + (1 - s) / n)` (from memory). Anything that serialises the threads (a lock, a shared queue, thread creation, the join) counts toward `s`.

---

## What it costs

| Operation | Cost on this machine | Note |
|---|---|---|
| `pthread_create` + `join` | about 30 µs | No page-table copy |
| `fork` + `wait`, small parent | about 130 µs | Grows with the parent's memory |
| Uncontended mutex | 13 ns | Atomic in user space |
| Atomic increment under 2-thread contention | about 20 to 25 ns each | 80 to 100 ms for 4 million |
| Mutex under 2-thread contention | about 90 ns each | 350 to 375 ms for 4 million |
| User-level switch (`swapcontext`) | 175 ns | Includes one `rt_sigprocmask` system call |
| Thread round trip, same CPU | about 2.6 µs | Condition variable and mutex |
| Thread round trip, two CPUs | about 21 to 23 µs | Cross-CPU wake-up |
| Stack, committed | about 8 KiB per idle thread | 8 MiB reserved by default |
| Thread per task vs pool, 20,000 tiny tasks | 576 to 702 ms vs 13 to 16 ms | Creation cost per task |

---

## Common mistakes

1. **Passing `&i` of a loop variable to a thread.** All threads see the final value (Problem A).
2. **Returning from `main` while threads run.** All threads are terminated.
3. **Forgetting to join or detach.** A joinable thread that is never joined leaks its resources.
4. **Treating `pthread_create`'s return as `-1` and `errno`.** It returns an error number directly.
5. **Believing "it printed the right total" proves there is no race.** At `-O2` the racy loop here gave the right answer; only ThreadSanitizer reported it.
6. **Using `volatile` for thread safety.** It stops the compiler caching a value; it does not make an update atomic. 1.8 to 2.0 million updates were lost here.
7. **Sharing a pointer to a stack variable that goes out of scope.** The thread keeps a pointer into a stack frame that no longer exists.
8. **Using a `static` or global buffer in a function that several threads call** (the classic `strtok`, `localtime`). Use the `_r` versions or thread-local storage.
9. **Calling `fork` in a multithreaded program and then doing real work in the child.** The child inherits locks held by threads that do not exist; 10 of 10 children locked up here.
10. **Holding a lock while running the task in a pool.** It serialises the workers.
11. **Believing more threads than CPUs go faster for CPU-bound work.** Four threads on 2 CPUs reached 1.87, not 4.
12. **Creating a thread per request in a server.** 30 µs and 8 KiB of RAM each, plus unbounded growth.
13. **Believing thread switches are always cheaper than process switches.** They skip the address-space change, but the measured round trip here was slower because of the primitive used.
14. **Assuming user-level threads never enter the kernel.** glibc `swapcontext` made a system call on every switch.
15. **Starting a pool with no shutdown protocol.** Workers wait forever, or tasks in the queue are dropped.

---

## Questions out loud

1. **What is a thread? How does it differ from a process?** A flow of execution with its own registers and stack inside a process; threads share the address space, descriptors and handlers. Processes have separate address spaces.
2. **What do threads share and what is private?** Section 1.1.
3. **How does Linux implement threads?** As tasks created by `clone` with `CLONE_VM`, `CLONE_FILES`, `CLONE_FS`, `CLONE_SIGHAND` and `CLONE_THREAD` (plus TLS and tid flags). `fork` is the same call without the sharing flags. NPTL, 1:1.
4. **Kernel threads versus user threads?** Kernel threads are scheduled by the kernel, so one can block while others run and all CPUs are used; user threads are scheduled by a library and are cheap to switch but one blocking call can stall them all. M:N mixes both (Go).
5. **Why is creating a thread cheaper than a process?** No page-table copy, no new address space, just a task, a stack and a TLS block: about 30 µs against 130 µs here, and the gap grows with the parent's size.
6. **Is a thread switch cheaper than a process switch?** It avoids changing the address space and so preserves the TLB and caches; but measure it: the primitive used for the handoff can dominate.
7. **What is thread-local storage?** One copy of a variable per thread, accessed through a per-thread base register (`FS` on x86-64). `errno` is the canonical case.
8. **What is a race condition?** A bug where the result depends on the interleaving of unsynchronised accesses to shared data, at least one a write. It is undefined behaviour in C.
9. **How do you fix one?** Avoid the sharing; or use an atomic; or a lock. Measured: atomic about 4 times cheaper than a mutex under contention.
10. **Why is `volatile` not enough?** It does not make a read-modify-write atomic or order accesses between threads.
11. **What happens to the other threads when one calls `fork`?** They do not exist in the child, but their locks are copied as they were; the child can deadlock.
12. **What is a thread pool and when do you use one?** A fixed set of workers fed from a queue: bounded concurrency, no per-task creation cost. about 600 to 700 ms against about 15 ms for 20,000 tasks here.
13. **How do you shut a pool down safely?** Set a closing flag, wake all workers, let them drain the queue, join them.
14. **Why do more threads not give a linear speedup?** Fewer CPUs than threads, load imbalance, serial sections (Amdahl), lock contention, and memory bandwidth.
15. **Which thread gets a signal sent to the process?** One arbitrary thread with the signal unblocked; to control it, block the signal in every thread but one.

---

## Problems

Solve each on paper or in a file before reading the answers.

**Where to solve them.** All five need a Linux machine with GCC and `-pthread` (a VM, WSL or a cloud shell). Online compilers often limit threads and timing, so use a real shell. For C, a machine with more than 2 CPUs shows larger speedups; the answers quote this 2-vCPU machine.

**How to approach them.** Write the expected output or the expected timing shape first, then run. For A, ask when each thread reads its argument. For B, decide who owns the result before the join. For C, work out the speedup you expect and what limits it. For D, think about which thread holds the lock at the instant of the fork. For E, write the shutdown protocol before the code.

### Problem A (E, entry level): what do the threads print?

Four threads are created in a loop with `pthread_create(&t[i], NULL, fn, &i)`, then released after the loop. What does each read? Then fix it.

### Problem B (E, entry level): sum with two threads

Sum an array of one million values with two threads, each handling one half, and check the total against a single-threaded sum. Return each partial sum through a struct passed to the thread.

### Problem C (M, medium): measure the speedup

Count primes below 6,000,000 with 1, 2 and 4 threads and report the speedup. Explain why 2 threads do not give 2.0 on 2 CPUs.

### Problem D (M, medium): fork with a lock held

A thread holds a mutex almost all the time. The main thread forks ten times and each child takes the mutex. Show the hang, then fix it with `pthread_atfork`.

### Problem E (H, hard): a bounded thread pool

Write a thread pool with a bounded queue (capacity 64), `submit`, and a `shutdown` that runs every queued task and joins the workers. Verify the results of 20,000 tasks against an independent computation, and compare with a thread per task.

---

## Answers

**A.** The four threads read the same variable, which holds the value it had when they ran, and the loop had ended with `i` equal to 4:

```text
passing &i:       thread 0..3 read 4 4 4 4
passing &ids[k]:  thread 0..3 read 0 1 2 3
```

The fix is one slot per thread (`&ids[k]`), which gives 0 1 2 3, as in the second line. The bug depends on timing: without the release gate a thread that starts early may read an earlier value, which is why the failure is intermittent in real programs.

**B.** The solution is the program of section 3.2; the output:

```text
partial sums 249750000 and 249750000, total 499500000, expected 499500000: match
```

Each thread writes only its own `Job`, so there is no sharing to protect; `pthread_join` makes the results visible.

**C.** The program is the one of section 9:

```text
1 thread(s): primes below 6000000 = 412849,    2241 ms, speedup 1.00
2 thread(s): primes below 6000000 = 412849,    1425 ms, speedup 1.57
4 thread(s): primes below 6000000 = 412849,    1134 ms, speedup 1.98
```

Speedup 1.6 with 2 threads: the two contiguous ranges are unequal in work (larger numbers need more divisions), so the CPU running the lower range idles at the end. 1.87 with 4 threads: smaller pieces balance better. Neither can pass 2.0 on 2 vCPUs. A cyclic split (thread `k` takes every `n`-th number) or a shared counter handing out small blocks would balance better; I did not run those versions.

**D.** The hang is the program of section 7 (`d7_forkthread.c`), the fix is `pdfix.c`:

```text
parent has 2 threads before the fork
child  has 1 thread(s) after the fork
10 forks while another thread held a mutex: 10 children locked up (killed by alarm), 0 finished
with pthread_atfork: 10 forks while another thread held a mutex: 0 locked up, 10 finished
```

The `prepare` handler takes the lock, which waits until the hog is not holding it, so at the instant of the fork **no other thread holds it**; the parent and child handlers then release their own copies. The cost is that every `fork` now waits for the lock, and it works only for locks you know about; a library's internal lock still needs `exec`.

**E.** The pool is the program of section 8. Its design follows the four points listed there: bounded queue with `not_full`, workers sleeping on `not_empty`, a `closing` flag with a broadcast, and workers that exit only when the queue is empty. The result (see section 8): correct totals in every configuration; 13 to 16 ms for 1 and 2 workers, 14 to 22 ms for 4, 576 to 702 ms for a thread per task.

---

## Cheat sheet

| Topic | Fact |
|---|---|
| Thread | Own registers, stack, TID, signal mask, `errno`; shares address space, descriptors, handlers |
| Linux | Task created by `clone` with `CLONE_VM`, `CLONE_FILES`, `CLONE_FS`, `CLONE_SIGHAND`, `CLONE_THREAD` (NPTL, 1:1) |
| `fork` vs thread | Same system call; `fork` shares nothing |
| IDs | `getpid()` is the thread-group ID, `gettid()` the thread's own |
| `pthread_create` | Returns 0 or an error number; single `void *` argument |
| `main` returns | All threads end |
| TLS | `__thread`, `_Thread_local`, `pthread_key_*`; base in `FS` on x86-64 |
| Race | Undefined behaviour; `-O2` may hide it; TSan finds it |
| Fixes | No sharing, atomic, mutex |
| Costs here | Create + join about 30 µs; fork 130 µs; mutex 13 ns; `swapcontext` 175 ns |
| Stack | 8 MiB reserved, about 8 KiB committed |
| `fork` + threads | Child has one thread; held locks stay locked; call `exec` or use `pthread_atfork` |
| Pool | Bounded queue, condition variables, closing flag, drain and join |
| Tools | `/proc/PID/task`, `ps -L`, `strace -f`, `-fsanitize=thread` |

---

## Sources

- [clone(2), Linux man pages](https://man7.org/linux/man-pages/man2/clone.2.html): the sharing flags and `clone3` (fetched).
- [pthreads(7), Linux man pages](https://man7.org/linux/man-pages/man7/pthreads.7.html): shared and per-thread attributes, NPTL and the 1:1 model, signals (fetched).
- [pthread_create(3), Linux man pages](https://man7.org/linux/man-pages/man3/pthread_create.3.html): default stack size, error return, what happens when `main` returns (fetched).
- [pthread_atfork(3), Linux man pages](https://man7.org/linux/man-pages/man3/pthread_atfork.3.html): handlers, and `fork` in a multithreaded program (fetched).
- [OS 02](02_processes.md): `fork`, copy-on-write, the process address space, and the cost of `fork`.
- Marked "from memory" in the text and not fetched or measured: the `FS` mechanism for TLS, the `lock xadd` instruction, the internals of `futex` use in mutexes and condition variables, Go's switching code, the `join` memory-ordering rule, the exact limit on thread count, the load-balanced alternatives in Problem C, and Amdahl's law.
