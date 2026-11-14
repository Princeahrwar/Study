# OS 08: Memory barriers and the memory model, as the OS and C programmer sees them

*Part of the [Operating systems plan](00_plan.md) · Time: about 8 hours (5 reading and experiments, 3 problems) · You need: [CO 20](../04_computer_organization/20_memory_consistency_and_atomics.md) (the hardware models, litmus tests and fences, measured), [OS 06](06_locks.md) (locks and the futex) and [OS 07](07_condition_variables_and_semaphores.md) (waiting)*

CO 20 explained what hardware may do to the order of loads and stores, and measured it. This chapter is what an OS or driver engineer needs on top of that: how the compiler breaks code before the hardware gets a chance, how to publish data from one thread to another correctly in C11 (flag and data, a pointer, a lazily built object, a seqlock), how to build the queue every driver and every packet pipeline contains (the single-producer single-consumer ring), what the same C looks like on x86-64 and on arm64, which tool finds the bugs, and what the Linux kernel's barrier names mean, including the descriptor-and-doorbell pattern in a network or storage driver. Interview questions here come in a fixed set: why is `volatile` not enough? What does release/acquire guarantee, stated as a condition? Write an SPSC ring and say which orders each index needs. Where does a driver need `dma_wmb()` and where `wmb()`? Why did this code work on my x86 laptop and fail on the arm64 board? Every answer below comes with a program that was compiled and run, and the failing versions are included.

By the end you can:

- show, in a disassembly, a compiler turning `while (!flag) {}` into an infinite loop, and say which of `volatile`, a relaxed atomic and an acquire load fixes it and what each still does not give you (measured: 2 s without exit against 100 ms);
- state release/acquire as a conditional guarantee and apply it to flag and data, a pointer handoff, double-checked initialisation and a seqlock;
- explain why the broken versions of those patterns pass every test on x86 (measured: 0 failures in 2,000,000 rounds with the weakest orders) and what that does and does not prove;
- read the x86-64 and arm64 assembly for each C11 order (`stlr`, `ldar`, `dmb ish`, `ldaddal`) and name the one place x86 pays (`xchg` for a sequentially consistent store, `lock` for a fence);
- write an SPSC ring with the right order on each index, and say what each of five variants costs (measured over three runs: sequentially consistent 12 to 32 times slower than release/acquire; cached indexes 1.8 to 2.7 times faster; head and tail sharing a cache line 3.6 to 8 times slower);
- use ThreadSanitizer, and say where it is wrong (measured: it cannot see standalone fences and reports a race on code that is correct);
- map the kernel's `READ_ONCE`, `WRITE_ONCE`, `barrier()`, `smp_mb()`, `smp_wmb()`, `smp_rmb()`, `smp_load_acquire()`, `smp_store_release()`, `dma_wmb()`, `dma_rmb()` and the mandatory `mb()`, `wmb()`, `rmb()` onto what they order, and write the descriptor and doorbell sequence of a driver.

**Where the numbers come from.** Programs were compiled with GCC 13.3 (`-O2 -Wall -Wextra`, `-lpthread`) and run on a Linux 6.18.44 kernel in a Firecracker micro-VM with 2 vCPUs (Intel Xeon at 2.30 GHz), glibc 2.39. ThreadSanitizer runs used `gcc -O1 -g -fsanitize=thread`. The arm64 assembly came from `aarch64-linux-gnu-gcc` 13.2 (`-O2`, with `-march=armv8.1-a` where noted): it was only compiled, never run, because this machine has no arm64 CPU or emulator. Every statement about how arm64 behaves at run time is therefore from the architecture rules as I know them (marked "from memory"), and none is measured. The C11 rules are from the cppreference page on `memory_order`; the kernel's barrier vocabulary and rules are from the kernel's `memory-barriers` document (both fetched; see Sources). The fetch tool summarised the second half of the kernel document, so rules quoted from that half (the I/O accessors) were not read word for word.

**Data and timing.** Counts that follow from the logic (torn reads in a broken seqlock, hangs, reported races) are shown as measured. Throughput was measured in three runs and is shown as run 1 / run 2 / run 3. The VM has two CPUs and a hypervisor under it, and one early development run of the padding experiment came out the other way round, so treat differences under about 30 percent as noise. A test that passes is not a proof: section 3 is about exactly that.

---

## 1. Three reorderers, and which one this chapter adds

An operation can be reordered by the compiler, by the core (its store buffer and out-of-order engine) and by the memory system (CO 20 section 1.2). CO 20 spent its time on the second and third. In C the first is the one that breaks code in the most surprising way, because it removes accesses instead of only moving them.

### 1.1 The compiler assumes there is one thread

Under the C11 rules a program with a data race has undefined behaviour (from memory; the cppreference page I fetched does not say it), so the compiler is allowed to assume that between two of your own accesses to a plain variable nobody else writes it. It may keep the variable in a register, load it once, merge two stores, drop a store that is overwritten before it is read, or move an access across another. None of that is visible to a single-threaded observer, and all of it is visible to a second thread.

```c
/* e1_hoist.c: a spin loop on a plain flag versus volatile and atomic flags. */
#include "sy.h"

int plain_flag;
volatile int volatile_flag;
atomic_int aflag;

__attribute__((noinline)) void wait_plain(void)    { while (!plain_flag) { } }
__attribute__((noinline)) void wait_volatile(void) { while (!volatile_flag) { } }
__attribute__((noinline)) void wait_atomic(void)   { while (!atomic_load_explicit(&aflag, memory_order_relaxed)) { } }

static void *setter(void *arg)
{
    (void)arg;
    usleep(100000);                                    /* 100 ms later set all three */
    plain_flag = 1;
    volatile_flag = 1;
    atomic_store_explicit(&aflag, 1, memory_order_relaxed);
    return NULL;
}

int main(int argc, char **argv)
{
    if (argc != 2) { fprintf(stderr, "usage: e1_hoist plain|volatile|atomic\n"); return 2; }
    pthread_t t;
    pthread_create(&t, NULL, setter, NULL);
    double t0 = now();
    if (!strcmp(argv[1], "plain")) wait_plain();
    else if (!strcmp(argv[1], "volatile")) wait_volatile();
    else wait_atomic();
    printf("%s: saw the flag after %.0f ms\n", argv[1], (now() - t0) * 1e3);
    pthread_join(t, NULL);
    return 0;
}
```

The three waiting functions differ only in the kind of flag. The program sets all three flags 100 ms after it starts and waits on the one named on the command line:

```text
plain: still spinning after 2 s, killed by timeout (exit 124)
volatile: saw the flag after 100 ms
atomic: saw the flag after 100 ms
```

The plain flag never returned. This is the machine code of `wait_plain` and then of `wait_volatile`:

```text
endbr64
mov    0x2c4a(%rip),%eax        # 4034 <plain_flag>
test   %eax,%eax
jne    13f0 <wait_plain+0x10>
jmp    13ee <wait_plain+0xe>
```

```text
mov    0x2c22(%rip),%eax        # 4030 <volatile_flag>
test   %eax,%eax
je     1408 <wait_volatile+0x8>
```

`wait_plain` loads the flag once, tests it, and if it is zero jumps to an instruction that jumps to itself. The compiler proved, correctly for a single thread, that nothing inside the loop changes `plain_flag`, so it hoisted the load out. `wait_volatile` reloads the flag on every iteration, and the relaxed atomic does the same. Nothing about the hardware was involved: this hang happens on every CPU.

### 1.2 What `volatile` does and does not do

`volatile` in C forces the compiler to perform each access as written and not to merge or drop them. It does not make the access atomic (a 64-bit `volatile` on a 32-bit machine can tear), it adds no CPU barrier, and it does not order a volatile access against ordinary ones. On the hardware side it is neutral. That is why it fixes the hang above and nothing else, and why the Linux kernel, which wants exactly this "do the access as written" property, wraps it in two macros instead of marking variables.

The kernel document describes `READ_ONCE()` and `WRITE_ONCE()` as weak forms of `barrier()` that act only on the accesses they wrap: they prevent the compiler from merging, reloading, inventing or dropping those accesses, and from tearing them. `barrier()` is the compiler-only general barrier: it has no read or write variants and no effect on the CPU. A userspace stand-in for the two macros is in section 8; the kernel's own definitions are more careful than that and I did not read them.

### 1.3 What is and is not a barrier to the compiler

A function call the compiler cannot see into acts as a compiler barrier for any memory that call could reach, which is why a lock, an unlock or a `pthread_cond_wait` makes plain variables visible without any other help. An atomic operation with an order stronger than relaxed restricts how the compiler moves other accesses around it, in the direction its order says. A relaxed atomic restricts only itself. Keep this in mind for section 4: a relaxed store followed by a plain store gives the compiler permission to swap them, and in the programs here GCC did not, which proves nothing about the next compiler.

---

## 2. The language model in one page

CO 20 section 5 has the full treatment; this is the part used below.

- A **data race** is two threads touching the same non-atomic location, at least one writing, with no ordering between the accesses. It is undefined behaviour, and it is what the compiler assumes away in section 1.
- Atomic operations are never data races. Each atomic has a **modification order** (a single order of all writes to it) that every thread agrees on; that and atomicity is all `memory_order_relaxed` promises.
- **Release** (on a store, or on the write half of a read-modify-write) and **acquire** (on a load, or on the read half) are the pair. The cppreference rule, stated as a condition: if an acquire load reads the value written by a release store (or a later value in that store's release sequence), then the store **synchronizes with** the load, and every write that happened before the store is visible to every read after the load.
- **Acquire/release is conditional.** If the load reads an older value, no ordering follows, which is why every handoff loop below tests the flag before touching the data.
- **Sequentially consistent** (`seq_cst`, the default) adds one total order of all seq_cst operations on top of acquire/release. It is the only order that forbids the store-buffer outcome of CO 20 (both threads reading 0), and on x86 it is the only one that costs anything for a store.
- `memory_order_consume` is lifted to acquire by every production compiler and is deprecated in C++26; do not use it. The kernel's version of the idea is `rcu_dereference()` (section 7).
- A **fence** (`atomic_thread_fence`) orders the surrounding accesses without being tied to one variable. It is the C11 name for what kernels call `smp_wmb()` (release fence) and `smp_rmb()` (acquire fence) and `smp_mb()` (seq_cst fence), approximately: the kernel's `smp_wmb()` orders only stores and the C11 release fence also orders earlier loads before later stores.

Everything in sections 3 to 7 is an application of those lines.

---

## 3. Publishing data from one thread to another

### 3.1 Flag and data

The canonical pattern: the writer fills in some data and then sets a flag; the reader waits for the flag and then reads the data. The program runs it 2,000,000 times in lock step (the writer waits for the reader's acknowledgement before the next round), with the payload a plain `int` and three versions of the flag operation: both sides relaxed (wrong by the rules), a release store with an acquire load (right), and a relaxed flag with a release fence before the store and an acquire fence after the load (right).

```c
static void *writer(void *arg)
{
    (void)arg;
    pin(0);
    for (long r = 1; r <= rounds; r++) {
        payload = (int)r;
        if (mode == RELAXED)
            atomic_store_explicit(&flag, r, memory_order_relaxed);
        else if (mode == REL_ACQ)
            atomic_store_explicit(&flag, r, memory_order_release);
        else {
            atomic_thread_fence(memory_order_release);
            atomic_store_explicit(&flag, r, memory_order_relaxed);
        }
        while (atomic_load_explicit(&ack, memory_order_acquire) != r) { }
    }
    return NULL;
}

static void *reader(void *arg)
{
    (void)arg;
    pin(1);
    for (long r = 1; r <= rounds; r++) {
        if (mode == RELAXED)
            while (atomic_load_explicit(&flag, memory_order_relaxed) != r) { }
        else if (mode == REL_ACQ)
            while (atomic_load_explicit(&flag, memory_order_acquire) != r) { }
        else {
            while (atomic_load_explicit(&flag, memory_order_relaxed) != r) { }
            atomic_thread_fence(memory_order_acquire);
        }
        if (payload != (int)r) stale++;
        atomic_store_explicit(&ack, r, memory_order_release);
    }
    return NULL;
}
```

The result, in three runs:

```text
relaxed  rounds 2000000  stale payload reads 0
relacq   rounds 2000000  stale payload reads 0
fences   rounds 2000000  stale payload reads 0
```

All three versions delivered the right payload every time, including the one that is wrong by the rules. That is not a surprise on x86, and it is the most important result in this chapter, so here is the reason. x86 does not reorder two stores or two loads (CO 20 section 3.2), the two cores are the only reorderers in play, and GCC emitted the two stores in program order (the assembly is in section 4). The relaxed version is a bug that this machine cannot show. On arm64 the hardware may make the flag visible before the payload, and nothing in the source distinguishes the two versions except the order argument.

### 3.2 A pointer, and double-checked initialisation

The next pattern builds an object once, on first use, and hands every later caller a pointer to it. Checking the pointer without the lock first (double-checked locking) avoids taking the mutex on the hot path, and is correct only with release on the store that publishes the pointer and acquire on the unlocked load. The program builds a three-field `config_t` three ways: acquire/release, everything relaxed, and `pthread_once`.

```c
/* e6_dcl.c: lazy initialisation of a shared object, three ways. */
#include "sy.h"
#include <stdint.h>

typedef struct { int a, b, c; } config_t;

static atomic_uintptr_t cfg_ptr;
static config_t cfg_storage;
static pthread_mutex_t cfg_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t cfg_once = PTHREAD_ONCE_INIT;
static int variant;

static void build(config_t *c) { c->a = 1; c->b = 2; c->c = 3; }
static void build_once(void) { build(&cfg_storage); atomic_store(&cfg_ptr, (uintptr_t)&cfg_storage); }

static config_t *get_config(void)
{
    if (variant == 0) {                                /* double-checked, acquire and release */
        config_t *p = (config_t *)atomic_load_explicit(&cfg_ptr, memory_order_acquire);
        if (!p) {
            pthread_mutex_lock(&cfg_lock);
            p = (config_t *)atomic_load_explicit(&cfg_ptr, memory_order_relaxed);
            if (!p) {
                build(&cfg_storage);
                atomic_store_explicit(&cfg_ptr, (uintptr_t)&cfg_storage, memory_order_release);
                p = &cfg_storage;
            }
            pthread_mutex_unlock(&cfg_lock);
        }
        return p;
    }
    if (variant == 1) {                                /* double-checked, everything relaxed */
        config_t *p = (config_t *)atomic_load_explicit(&cfg_ptr, memory_order_relaxed);
        if (!p) {
            pthread_mutex_lock(&cfg_lock);
            p = (config_t *)atomic_load_explicit(&cfg_ptr, memory_order_relaxed);
            if (!p) {
                build(&cfg_storage);
                atomic_store_explicit(&cfg_ptr, (uintptr_t)&cfg_storage, memory_order_relaxed);
                p = &cfg_storage;
            }
            pthread_mutex_unlock(&cfg_lock);
        }
        return p;
    }
    pthread_once(&cfg_once, build_once);               /* the library does it for you */
    return (config_t *)atomic_load_explicit(&cfg_ptr, memory_order_relaxed);
}

static void *user(void *arg)
{
    long *bad = arg;
    config_t *c = get_config();
    if (c->a != 1 || c->b != 2 || c->c != 3) (*bad)++;
    return NULL;
}

int main(int argc, char **argv)
{
    if (argc != 2) { fprintf(stderr, "usage: e6_dcl 0|1|2\n"); return 2; }
    variant = atoi(argv[1]);
    pthread_t t[4];
    long bad[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4; i++) pthread_create(&t[i], NULL, user, &bad[i]);
    for (int i = 0; i < 4; i++) pthread_join(t[i], NULL);
    printf("variant %d: torn reads %ld\n", variant, bad[0] + bad[1] + bad[2] + bad[3]);
    return 0;
}
```

```text
variant 0: torn reads 0
variant 1: torn reads 0
variant 2: torn reads 0
```

No variant tore here either, for the same reason as 3.1. Section 6 shows how ThreadSanitizer tells them apart. Two rules to take away. First, the version that is right and short is `pthread_once` (or a C++ function-local static, or `call_once`): the library does the double-checking and has been tested on every architecture it ships on. Second, the thing being published is a pointer to an object initialised before the store; the reader needs no barrier of its own beyond the acquire, because the data it reads comes after the load in program order and the load depends on the pointer. (The kernel gets this ordering from the data dependency alone, which is what `rcu_dereference()` relies on; C11 gives you nothing but acquire for it in practice because `consume` does not work.)

### 3.3 A seqlock

A seqlock gives readers a consistent snapshot of several words that a single writer updates, with no lock on the read side and no writer blocking. The writer increments a sequence counter to an odd value, writes the data, and increments it again to an even value. A reader reads the counter, reads the data, reads the counter again, and retries unless the two values are equal and even. The program keeps four words that must always be equal (the writer stores the same number in all four) and counts snapshots in which they differ.

```c
static void write_snapshot(unsigned long v)            /* single writer only */
{
    unsigned long s = atomic_load_explicit(&seq, memory_order_relaxed);
    atomic_store_explicit(&seq, s + 1, memory_order_relaxed);        /* odd: update in progress */
    atomic_thread_fence(memory_order_release);
    for (int i = 0; i < WORDS; i++)
        atomic_store_explicit(&word[i], v, memory_order_relaxed);
    atomic_store_explicit(&seq, s + 2, memory_order_release);        /* even: stable */
}

static unsigned long read_snapshot(unsigned long out[WORDS], unsigned long *retries)
{
    for (;;) {
        unsigned long s1 = atomic_load_explicit(&seq, memory_order_acquire);
        for (int i = 0; i < WORDS; i++)
            out[i] = atomic_load_explicit(&word[i], memory_order_relaxed);
        if (mode == NOCHECK) return s1;                /* bug: never looks at seq again */
        if (mode == GOOD) atomic_thread_fence(memory_order_acquire);
        unsigned long s2 = atomic_load_explicit(&seq, memory_order_relaxed);
        if (!(s1 & 1) && s1 == s2) return s1;
        (*retries)++;
    }
}
```

Three modes: `good` as above; `nocheck`, a reader that never re-reads the counter (the obvious bug); and `nofence`, a reader without the acquire fence before the second counter read. One thread writes flat out and the other reads for one second, three runs:

```text
good     reads 5127000  torn 0  retries 11001274
nocheck  reads 20132000  torn 1603065  retries 0
nofence  reads 3170000  torn 0  retries 7421747
```

```text
good     reads 4155000  torn 0  retries 7745326
nocheck  reads 16398000  torn 1442612  retries 0
nofence  reads 3608000  torn 0  retries 15014658
```

```text
good     reads 3429000  torn 0  retries 12977876
nocheck  reads 24094000  torn 3562967  retries 0
nofence  reads 4386000  torn 0  retries 12366416
```

The correct reader saw no torn snapshot in any run, and it retried constantly (the writer is running flat out), which is what a seqlock costs a reader when writes are frequent. The reader without the check saw torn data in a large fraction of its reads: the checking really is what protects it. The reader without the fence saw none, which again is x86 hiding a bug: without the fence the second counter read may be performed before the data reads, and on arm64 it can be. The seqlock is a good example of why the data words here are relaxed atomics rather than plain variables. A plain `unsigned long` read while the writer writes it is a data race, and the kernel deals with it through its own annotations (from memory); in portable C11 you use atomics.

---

## 4. The same C on x86-64 and on arm64

CO 20 section 5.3 lists the instruction for each order. This is the compiler's output for the functions of section 3, compiled for both targets. The table shows the instruction that touches the flag or the payload and nothing else:

| Function | x86-64 | arm64 (armv8.1-a) |
| --- | --- | --- |
| `publish_relaxed` | `movl $42, payload(%rip); movl $1, flag(%rip)` | `str w2, [x1, #:lo12:.LANCHOR0]; str w1, [x0]` |
| `publish_release` | `movl $42, payload(%rip); movl $1, flag(%rip)` | `str w2, [x1, #:lo12:.LANCHOR0]; stlr w1, [x0]` |
| `publish_seq_cst` | `movl $42, payload(%rip); xchgl flag(%rip), %eax` | `str w2, [x1, #:lo12:.LANCHOR0]; stlr w1, [x0]` |
| `consume_relaxed` | `movl flag(%rip), %eax` | `ldr w0, [x0]` |
| `consume_acquire` | `movl flag(%rip), %eax` | `ldar w0, [x0]` |
| `consume_seq_cst` | `movl flag(%rip), %eax` | `ldar w0, [x0]` |
| `fence_release` | `(no instruction)` | `dmb ish` |
| `fence_acquire` | `(no instruction)` | `dmb ishld` |
| `fence_seq_cst` | `lock orq $0, (%rsp)` | `dmb ish` |
| `rmw_relaxed` | `lock xaddl %eax, flag(%rip)` | `ldadd w0, w0, [x1]` |
| `rmw_acq_rel` | `lock xaddl %eax, flag(%rip)` | `ldaddal w0, w0, [x1]` |

Reading the table:

- On x86-64 a release store is an ordinary `mov`, an acquire load is an ordinary `mov`, and both are therefore also what the relaxed versions compile to. The release and acquire fences compile to nothing at all (they constrain only the compiler). This is why every test in section 3 passed.
- The only x86 instruction that pays is the sequentially consistent store, which GCC compiles to `xchg` (an implicitly locked exchange), and the sequentially consistent fence, `lock orq $0, (%rsp)` (a locked no-op that drains the store buffer). Both appear in the measurements of section 5.
- On arm64 the order becomes the instruction: `str` for relaxed, `stlr` (store-release) for release, `ldr` and `ldar` (load-acquire) likewise. A sequentially consistent store and load use the same `stlr` and `ldar` (from memory: the arm64 rules make `stlr` followed by `ldar` ordered, which is what seq_cst needs). The fences become `dmb ish` (full) and `dmb ishld` (loads before everything).
- Read-modify-write needs the armv8.1 large-system extensions to be one instruction: with `-march=armv8.1-a` GCC emitted `ldadd` for relaxed and `ldaddal` (acquire and release) for acq_rel. Without that flag it emitted a call to a library helper (`__aarch64_ldadd4_relax` and `..._acq_rel`), which I saw in the first compile and which is how one binary can run on cores with and without the extension (from memory).

The compiler-ordering point is visible too. In the `publish_relaxed` row the two stores are in source order, but nothing in the C forces that.

### 4.1 The ring's push and pop, both ways

The ring of section 5, compiled as stand-alone `ring_push` and `ring_pop` functions (the acquire/release variant), looks like this:

```text
x86-64 ring_push:
  endbr64
  movq 64(%rdi), %rax
  movq (%rdi), %rdx
  movq %rax, %rcx
  subq %rdx, %rcx
  xorl %edx, %edx
  cmpq $1024, %rcx
  je .L1
  movq %rax, %rdx
  addq $1, %rax
  andl $1023, %edx
  movq %rsi, 256(%rdi,%rdx,8)
  movl $1, %edx
  movq %rax, 64(%rdi)
  movl %edx, %eax
  ret
x86-64 ring_pop:
  endbr64
  movq (%rdi), %rax
  movq 64(%rdi), %rcx
  xorl %edx, %edx
  cmpq %rcx, %rax
  je .L6
  movq %rax, %rdx
  addq $1, %rax
  andl $1023, %edx
  movq 256(%rdi,%rdx,8), %rdx
  movq %rdx, (%rsi)
  movl $1, %edx
  movq %rax, (%rdi)
  movl %edx, %eax
  ret
arm64 ring_push:
  add x4, x0, 64
  ldr x2, [x4]
  ldar x3, [x0]
  sub x3, x2, x3
  cmp x3, 1024
  beq .L3
  and x3, x2, 1023
  add x2, x2, 1
  add x3, x3, 32
  str x1, [x0, x3, lsl 3]
  stlr x2, [x4]
  mov w0, 1
  ret
  mov w0, 0
  ret
arm64 ring_pop:
  ldr x2, [x0]
  add x3, x0, 64
  ldar x3, [x3]
  cmp x2, x3
  beq .L7
  and x3, x2, 1023
  add x2, x2, 1
  add x3, x3, 32
  ldr x3, [x0, x3, lsl 3]
  str x3, [x1]
  stlr x2, [x0]
  mov w0, 1
  ret
  mov w0, 0
  ret
```

On x86-64 push has plain `mov` loads and stores: the load of `head` (`movq (%rdi), %rdx`), the slot store (`movq %rsi, 256(%rdi,%rdx,8)`) and the tail store (`movq %rax, 64(%rdi)`) in that order, and no barrier instruction. On arm64 the same function has `ldar` for the load of `head` (acquire), `str` for the slot and `stlr` for the tail (release). The `stlr` sits after the slot store, which is the one ordering the ring depends on: the slot must be written before the tail moves. Pop mirrors it: `ldar` of the tail, `ldr` of the slot, `stlr` of the head.

---

## 5. The single-producer single-consumer ring

One thread pushes, one thread pops, and no lock is needed. This is the shape of a NIC's descriptor ring, an io_uring submission queue, a logging pipeline and the shared-memory queue of [OS 18](18_ipc_mechanisms.md). The design is small enough to get completely right, and each line of it is an application of section 2.

### 5.1 The design

- A fixed array of `RING_SIZE` slots (a power of two so the index wraps with a mask).
- `tail` counts items pushed (written only by the producer) and `head` counts items popped (written only by the consumer). They run freely and are reduced modulo the size only when indexing, so "full" is `tail - head == RING_SIZE` and "empty" is `head == tail`, with no wasted slot.
- Each index has one writer, so it needs no read-modify-write, only a store. Its writer reads it with `relaxed` (nobody else changes it). The other thread reads it with `acquire`, and its writer stores it with `release`.
- **Why those orders.** The producer writes the slot and then moves `tail` with release: any consumer that acquires the new `tail` sees the slot. The consumer reads the slot and then moves `head` with release: a producer that acquires the new `head` knows the consumer has finished reading that slot and may overwrite it. Both directions matter; the second is the one people forget, and it protects against the producer overwriting a slot that is still being read.

The code is a template so that the same logic can be compiled with different orders, with and without padding between the indexes, and with or without cached copies:

```c
/* ring_t.h: SPSC ring template.  Define NAME, LD, ST, PAD and CACHED, then include. */
#define CAT2(a, b) a##b
#define CAT(a, b) CAT2(a, b)
#define RING CAT(NAME, _ring)

typedef struct {
    alignas(PAD ? 64 : 8) atomic_ulong head;           /* next slot to pop, written by the consumer */
    alignas(PAD ? 64 : 8) atomic_ulong tail;           /* next slot to push, written by the producer */
    alignas(PAD ? 64 : 8) unsigned long cached_head;   /* producer's copy of head */
    alignas(PAD ? 64 : 8) unsigned long cached_tail;   /* consumer's copy of tail */
    alignas(64) unsigned long slot[RING_SIZE];         /* plain data, ordered by head and tail */
} RING;

static inline int CAT(NAME, _push)(RING *r, unsigned long v)
{
    unsigned long t = atomic_load_explicit(&r->tail, memory_order_relaxed);
    if (CACHED) {
        if (t - r->cached_head == RING_SIZE) {
            r->cached_head = atomic_load_explicit(&r->head, LD);
            if (t - r->cached_head == RING_SIZE) return 0;
        }
    } else if (t - atomic_load_explicit(&r->head, LD) == RING_SIZE) {
        return 0;
    }
    r->slot[t % RING_SIZE] = v;
    atomic_store_explicit(&r->tail, t + 1, ST);
    return 1;
}

static inline int CAT(NAME, _pop)(RING *r, unsigned long *v)
{
    unsigned long h = atomic_load_explicit(&r->head, memory_order_relaxed);
    if (CACHED) {
        if (h == r->cached_tail) {
            r->cached_tail = atomic_load_explicit(&r->tail, LD);
            if (h == r->cached_tail) return 0;
        }
    } else if (h == atomic_load_explicit(&r->tail, LD)) {
        return 0;
    }
    *v = r->slot[h % RING_SIZE];
    atomic_store_explicit(&r->head, h + 1, ST);
    return 1;
}

static void *CAT(NAME, _producer)(void *arg)
{
    RING *r = arg;
    pin(0);
    for (unsigned long i = 1; i <= ITEMS; i++)
        while (!CAT(NAME, _push)(r, i)) { }
    return NULL;
}

static void *CAT(NAME, _consumer)(void *arg)
{
    RING *r = arg;
    unsigned long v, expect = 1;
    pin(1);
    while (expect <= ITEMS) {
        if (CAT(NAME, _pop)(r, &v)) {
            if (v != expect) bad_items++;
            expect++;
        }
    }
    return NULL;
}

static void CAT(NAME, _run)(const char *label)
{
    static RING ring;
    pthread_t p, c;
    memset(&ring, 0, sizeof ring);
    bad_items = 0;
    double t0 = now();
    pthread_create(&p, NULL, CAT(NAME, _producer), &ring);
    pthread_create(&c, NULL, CAT(NAME, _consumer), &ring);
    pthread_join(p, NULL);
    pthread_join(c, NULL);
    double dt = now() - t0;
    printf("%-10s %6.1f Mitems/s  %6.1f ns/item  wrong items %lu\n", label, ITEMS / dt / 1e6, dt / ITEMS * 1e9, bad_items);
}

#undef NAME
#undef LD
#undef ST
#undef PAD
#undef CACHED
#undef RING
```

`CACHED` adds `cached_head` (the producer's last-seen copy of `head`) and `cached_tail` (the consumer's copy of `tail`). The producer refreshes its copy with an acquire load only when the ring looks full, and the consumer only when it looks empty. That keeps the other thread's cache line out of the common path.

### 5.2 The five variants

```c
/* e4_ring.c: an SPSC ring in C11 atomics, five variants of the same template, measured. */
#include "sy.h"
#include <stdalign.h>

#ifndef ITEMS
#define ITEMS 10000000UL
#endif
#define RING_SIZE 1024

static unsigned long bad_items;

#define NAME acqrel
#define LD memory_order_acquire
#define ST memory_order_release
#define PAD 1
#define CACHED 0
#include "ring_t.h"

#define NAME seqcst
#define LD memory_order_seq_cst
#define ST memory_order_seq_cst
#define PAD 1
#define CACHED 0
#include "ring_t.h"

#define NAME relaxed
#define LD memory_order_relaxed
#define ST memory_order_relaxed
#define PAD 1
#define CACHED 0
#include "ring_t.h"

#define NAME unpadded
#define LD memory_order_acquire
#define ST memory_order_release
#define PAD 0
#define CACHED 0
#include "ring_t.h"

#define NAME cached
#define LD memory_order_acquire
#define ST memory_order_release
#define PAD 1
#define CACHED 1
#include "ring_t.h"

int main(int argc, char **argv)
{
    const char *which = argc > 1 ? argv[1] : "all";
    int all = !strcmp(which, "all");
    if (all || !strcmp(which, "acqrel"))   acqrel_run("acqrel");
    if (all || !strcmp(which, "seqcst"))   seqcst_run("seqcst");
    if (all || !strcmp(which, "relaxed"))  relaxed_run("relaxed");
    if (all || !strcmp(which, "unpadded")) unpadded_run("unpadded");
    if (all || !strcmp(which, "cached"))   cached_run("cached");
    return 0;
}
```

- `acqrel`: the design above, indexes on separate cache lines.
- `seqcst`: every load and store seq_cst, the default if you write `atomic_load` and `atomic_store`.
- `relaxed`: every order relaxed. Wrong by the rules.
- `unpadded`: `acqrel` with `head` and `tail` adjacent in one cache line.
- `cached`: `acqrel` plus the cached copies.

The producer is pinned to CPU 0 and the consumer to CPU 1. Each variant moves 10,000,000 items, the consumer checks that they arrive in order, and "wrong items" counts those that did not. Three runs:

```text
acqrel      106.4 Mitems/s     9.4 ns/item  wrong items 0
seqcst        5.9 Mitems/s   170.1 ns/item  wrong items 0
relaxed      80.9 Mitems/s    12.4 ns/item  wrong items 0
unpadded     16.4 Mitems/s    60.8 ns/item  wrong items 0
cached      257.1 Mitems/s     3.9 ns/item  wrong items 0
```

```text
acqrel      132.3 Mitems/s     7.6 ns/item  wrong items 0
seqcst        4.1 Mitems/s   244.6 ns/item  wrong items 0
relaxed      96.1 Mitems/s    10.4 ns/item  wrong items 0
unpadded     16.1 Mitems/s    62.0 ns/item  wrong items 0
cached      243.0 Mitems/s     4.1 ns/item  wrong items 0
```

```text
acqrel       80.4 Mitems/s    12.4 ns/item  wrong items 0
seqcst        6.6 Mitems/s   151.5 ns/item  wrong items 0
relaxed      80.8 Mitems/s    12.4 ns/item  wrong items 0
unpadded     22.4 Mitems/s    44.6 ns/item  wrong items 0
cached      220.4 Mitems/s     4.5 ns/item  wrong items 0
```

Reading it:

- **No variant delivered a wrong item**, including `relaxed`. Section 3.1 explains why; section 6 shows that the tool catches it.
- **`acqrel` and `relaxed` are the same on x86** (the same machine code, section 4.1), and their throughput agrees to within noise. On arm64 the two differ by `str` against `stlr`, which I could not measure here.
- **`seqcst` was 12 to 32 times slower** than `acqrel`. Every store becomes an `xchg`, which drains the store buffer and holds the cache line, and each thread does one per item. This is the real cost of the default order, in the one place x86 charges for it.
- **`unpadded` was 3.6 to 8 times slower** than `acqrel`: `head` and `tail` share a line, so each side's store invalidates the other's copy (false sharing, [CO 19](../04_computer_organization/19_cache_coherence.md) and OS 06's hash table). It is the same code and the same orders.
- **`cached` was the fastest, by 1.8 to 2.7 times** over `acqrel`: the producer reads `head` only when the ring looks full, so the common push touches only lines the producer owns.

That ranking (cached, then acqrel, far ahead of unpadded, far ahead of seqcst) is the practical design advice: pad the indexes, cache the other side's index, and use the weakest order that is correct.

### 5.3 Many producers, or many consumers

This ring is correct for exactly one producer and one consumer. Two producers racing on `tail` would both write the same slot. A multi-producer ring needs a read-modify-write on the index (a compare-and-swap loop or `fetch_add`) and a way for the consumer to know a slot is complete (a per-slot sequence number, as in the Vyukov bounded queue, from memory). That is a different structure with different costs; the SPSC ring is fast because it is the one case that needs nothing but loads and stores.

---

## 6. Tools: finding the bug that testing cannot

Section 3 shows why testing is weak here: the wrong programs pass on x86. Two kinds of tool help.

**ThreadSanitizer** (`gcc -fsanitize=thread`) instruments every memory access and builds the happens-before relation from the atomic operations, locks and thread creation it sees. When two accesses to a plain location are not ordered by it, it reports a data race, whether or not that run corrupted anything. This is the property that matters: it finds the section 3 bugs on x86. All of the programs above were rebuilt with it at `-O1` and run for a smaller number of rounds:

```text
e2 relaxed:
WARNING: ThreadSanitizer: data race
relaxed  rounds 20000  stale payload reads 0
e2 relacq:
relacq   rounds 20000  stale payload reads 0
e2 fences:
WARNING: ThreadSanitizer: data race
fences   rounds 20000  stale payload reads 0
e4 acqrel:
acqrel wrong items 0
e4 seqcst:
seqcst wrong items 0
e4 relaxed:
WARNING: ThreadSanitizer: data race
relaxed wrong items 0
e4 unpadded:
unpadded wrong items 0
e4 cached:
cached wrong items 0
e5 good:
good     torn 0
e6 variant 0:
variant 0: torn reads 0
e6 variant 1:
WARNING: ThreadSanitizer: data race
variant 1: torn reads 0
e6 variant 2:
variant 2: torn reads 0
e7:
WARNING: ThreadSanitizer: data race
descriptors 20000  inconsistent 0  doorbell 20000
```

What this says:

- `relacq` (release store, acquire load) is clean. `relaxed` is reported as a race on the payload, which is the right verdict.
- The `fences` version of the same pattern, which the C11 rules say is correct, was **also reported as a race**. GCC printed a warning while compiling it: `atomic_thread_fence` is not supported with `-fsanitize=thread`. The tool cannot see standalone fences, so it does not see the ordering they provide. Code that uses fences needs the clang build of the tool (not tried here) or the fence rewritten as release and acquire operations on the variable for the test.
- The ring variants other than `relaxed` are clean, and `relaxed` is reported. The seqlock `good` mode (all data in atomics) is clean; the compiler warned about the fences, but the data were atomic, so there was nothing for the tool to miss.
- Double-checked initialisation with relaxed orders is reported. The acquire/release version and `pthread_once` are clean.
- The descriptor ring of section 8, which uses `READ_ONCE` (a `volatile` access) and fences, is reported even though it is correct by its design: TSan treats `volatile` as a plain access and cannot see the fences. A tool that is silent on correct code and loud on broken code is useful; one that is also loud on correct code means you have to read each report.

**Model checkers and litmus-test tools** (from memory; none installed on this VM): `herd7` runs a small litmus test against a formal model of x86, arm64 or the Linux kernel and lists every allowed outcome; CDSChecker and CBMC explore interleavings of small C11 programs. They answer "is this outcome allowed?" exhaustively for a small test, which no number of runs can.

**The compiler's own output.** For the question "did my fence do anything", read the assembly (`objdump -d`, or `-S`), as sections 1 and 4 do. A barrier that compiled to nothing was either free on this architecture or removed.

---

## 7. The Linux kernel's vocabulary

The kernel does not use the C11 names. It predates them, runs on every architecture, and has a bigger menu. These are the rules from the kernel's `memory-barriers` document that matter here (the document is long; the first 100,000 of its 129,000 characters were read directly, and the rest came through a summary).

| Name | What it does | Closest C11 | Use it for |
| --- | --- | --- | --- |
| `barrier()` | Compiler barrier only; no CPU effect | `atomic_signal_fence` | Stop the compiler moving or merging accesses; a general barrier with no read or write variants |
| `READ_ONCE(x)`, `WRITE_ONCE(x, v)` | One access, as written, not torn, merged, reloaded or invented | relaxed atomic load and store | Any shared variable the compiler might otherwise cache or split |
| `smp_mb()` | Full barrier on SMP builds (compiler barrier on UP) | seq_cst fence | Store then load must stay in order |
| `smp_wmb()` | Orders stores against stores | release fence (approx.) | Write the data, then the flag |
| `smp_rmb()` | Orders loads against loads | acquire fence (approx.) | Read the flag, then the data |
| `smp_store_release(p, v)`, `smp_load_acquire(p)` | One-way barriers on one variable | release store, acquire load | Publishing, preferred over a bare barrier |
| `smp_mb__before_atomic()`, `smp_mb__after_atomic()` | Upgrade an unordered atomic RMW to a full barrier | none | Around `atomic_inc()` and similar that return nothing |
| `smp_store_mb()` | Store followed by a full barrier | seq_cst store | The `set_current_state()` pattern |
| `rcu_assign_pointer()`, `rcu_dereference()` | Publish and read a pointer | release store, (consume) | RCU-protected data |
| `mb()`, `rmb()`, `wmb()` | **Mandatory** barriers, ordering against devices as well as other CPUs | none | MMIO and device memory; not for CPU to CPU ordering |
| `dma_wmb()`, `dma_rmb()`, `dma_mb()` | Cheaper barriers for coherent DMA memory | none | Descriptors the device reads and writes |

The C11 column and the use given for `smp_store_mb()` are my mapping, from memory, not the document's.

Rules worth knowing, from the same document:

- **The `smp_` barriers order CPUs; the mandatory ones also order devices.** On a uniprocessor build `smp_mb()` degrades to a compiler barrier; `mb()` does not. Using `smp_wmb()` to order a write to device memory against a write to RAM is wrong, even if it happens to work on x86.
- **Release followed by acquire is not a full barrier.** A release store and a later acquire load of different variables may still be seen out of order (the StoreLoad case of CO 20). Where that matters, `smp_mb()`.
- **Address dependencies.** A load whose address came from an earlier load is ordered after it. Since kernel v4.15 `READ_ONCE()` supplies an implicit address-dependency barrier, and the old explicit APIs were removed in v5.9; the portable spelling is `rcu_dereference()`.
- **Control dependencies are weaker.** A branch on a loaded value orders that load before a later store, and nothing else: a later load needs `smp_rmb()`. The document warns that "compilers do not understand control dependencies", so the compiler may remove the branch and with it the ordering.
- **Minimal guarantees.** Dependent accesses issue in order; overlapping accesses from one CPU appear ordered to that CPU; independent accesses may be reordered; bitfields and misaligned variables carry no guarantee; barriers do not guarantee completion or multi-copy atomicity, and only general barriers restore agreement across CPUs.
- **Locks imply acquire and release** (from memory; the part of the document I could check shows lock-ordering examples but not the rule itself). Taking a lock is an acquire and releasing it a release, so everything inside the critical section stays inside; an unlock followed by a lock of a different lock is the release-then-acquire case above.
- **Interrupts.** The document calls disabling local interrupts "a form of locking" and says that where a CPU and an interrupt routine interact it recommends interrupt-disabling locks over barriers.

The sleep and wake-up barrier rules (`set_current_state()` and `wake_up()`) were in the part of the document I did not read, and the `mmiowb` rules were not covered in what I saw; do not take either from this chapter.

---

## 8. A driver's descriptor ring and doorbell

A NIC or storage controller shares a ring of descriptors with the driver in coherent DMA memory. Each descriptor has a buffer address, a length and a status or OWN field that says whose turn it is. The driver fills a descriptor and flips ownership to the device; then it writes to a device register (the doorbell) so the device looks. The device returns the descriptor by flipping ownership back. This is the pattern the kernel document's DMA example describes, and it needs three kinds of ordering:

1. Before reading a descriptor's fields, check the OWN field, then `dma_rmb()`, so the fields are not read before the check.
2. After writing a descriptor's fields, `dma_wmb()`, then set OWN, so the device never sees OWN with stale fields.
3. After setting OWN and before the doorbell, a barrier strong enough to order RAM against the device register. In the kernel that ordering comes with `writel()` (the default I/O accessors keep a prior write to memory ordered before the device write, per the kernel document's I/O section; `writel_relaxed()` drops that guarantee), or a `wmb()` in front of `writel_relaxed()` (the second from memory). A `dma_wmb()` is not enough for this step, because the rules for `dma_*` barriers give no ordering for MMIO.

The userspace model below keeps the logic and replaces the pieces that cannot be reproduced in userspace: the "device" is a second thread, the doorbell is an atomic variable, and `dma_wmb()` and `dma_rmb()` are C11 release and acquire fences. The macros are stand-ins for the kernel's, not copies:

```c
/* e7_desc.h: kernel-style barrier names written with C11 primitives (userspace model only). */
#define READ_ONCE(x)      (*(const volatile __typeof__(x) *)&(x))
#define WRITE_ONCE(x, v)  (*(volatile __typeof__(x) *)&(x) = (v))
#define smp_mb()          atomic_thread_fence(memory_order_seq_cst)
#define smp_wmb()         atomic_thread_fence(memory_order_release)
#define smp_rmb()         atomic_thread_fence(memory_order_acquire)
#define dma_wmb()         atomic_thread_fence(memory_order_release)
#define dma_rmb()         atomic_thread_fence(memory_order_acquire)
```

```c
/* e7_desc.c: a descriptor ring with an OWN bit and a doorbell, the device simulated by a thread. */
#include "sy.h"
#include "e7_desc.h"

#define NDESC 64
#define DRIVER_OWN 0
#define DEVICE_OWN 1

struct desc {
    unsigned long addr;                                /* buffer address, written by the driver */
    unsigned int len;                                  /* length, written by the driver */
    unsigned int status;                               /* ownership, written by both sides */
};

static struct desc ring[NDESC];
static atomic_ulong doorbell;                          /* stands in for a device register */
static unsigned long total, bad;

static void *driver(void *arg)
{
    (void)arg;
    pin(0);
    for (unsigned long i = 0; i < total; i++) {
        struct desc *d = &ring[i % NDESC];
        while (READ_ONCE(d->status) != DRIVER_OWN) { } /* wait until the device handed it back */
        dma_rmb();                                     /* status check before touching the rest */
        d->addr = 0x1000 + i;
        d->len = (unsigned int)(i & 0xffff);
        dma_wmb();                                     /* data before the ownership flip */
        WRITE_ONCE(d->status, DEVICE_OWN);
        smp_wmb();                                     /* a real driver would use writel() here */
        atomic_store_explicit(&doorbell, i + 1, memory_order_relaxed);
    }
    return NULL;
}

static void *device(void *arg)
{
    (void)arg;
    pin(1);
    for (unsigned long i = 0; i < total; i++) {
        struct desc *d = &ring[i % NDESC];
        while (READ_ONCE(d->status) != DEVICE_OWN) { }
        dma_rmb();                                     /* status before data */
        if (d->addr != 0x1000 + i || d->len != (unsigned int)(i & 0xffff)) bad++;
        dma_wmb();
        WRITE_ONCE(d->status, DRIVER_OWN);
    }
    return NULL;
}

int main(int argc, char **argv)
{
    total = argc > 1 ? strtoul(argv[1], NULL, 10) : 1000000;
    pthread_t a, b;
    pthread_create(&a, NULL, driver, NULL);
    pthread_create(&b, NULL, device, NULL);
    pthread_join(a, NULL);
    pthread_join(b, NULL);
    printf("descriptors %lu  inconsistent %lu  doorbell %lu\n", total, bad, atomic_load(&doorbell));
    return 0;
}
```

```text
descriptors 1000000  inconsistent 0  doorbell 1000000
```

A million descriptors went round a 64-entry ring with no inconsistent one. As with every program here, that proves little on x86; what it shows is the shape: **status check, `dma_rmb()`, read fields**, and **write fields, `dma_wmb()`, set OWN**. Remove the first `dma_wmb()` on arm64 and the device can see OWN with the old address (from memory: this is the classic driver bug that appears on arm64 boards and not on x86 development machines, and it is the reason `dma_wmb()` exists as a separate, cheap operation).

---

## What it costs

| Operation | x86-64 | arm64 (from memory, not measured) | Measured here |
| --- | --- | --- | --- |
| relaxed load or store | `mov` | `ldr`, `str` | ring `relaxed`: same speed as `acqrel` |
| acquire load, release store | `mov` | `ldar`, `stlr` | ring `acqrel`: 7.6 to 12.4 ns per item with two threads |
| seq_cst store | `xchg` | `stlr` | ring `seqcst`: 12 to 32 times slower |
| release or acquire fence | nothing | `dmb ish`, `dmb ishld` | not measured |
| seq_cst fence | `lock orq $0, (%rsp)` | `dmb ish` | not measured here (see CO 20 section 2.4) |
| read-modify-write | `lock xadd` | `ldadd` family with LSE | not measured here |
| padding two indexes apart | none | none | 3.6 to 8 times faster |
| cached index | none | none | 1.8 to 2.7 times faster |

The lesson for x86 development: nothing cheap on x86 is a bug-finder, because release, acquire and relaxed are all the same instruction there. If the code is meant to run on arm64 (or POWER or RISC-V) test it on that, or under a tool that models that.

## Common mistakes

1. **Using `volatile` as the synchronisation.** It stops the compiler hoisting a load (section 1) and orders nothing else; a flag-and-data handoff with `volatile` flag and plain data is still a data race and still wrong on arm64.
2. **Concluding "it works" from an x86 test.** The weakest orders passed every test in sections 3 and 5. Passing on x86 shows the compiler did not break it, not that the orders are right.
3. **Release without a matching acquire** (or the reverse). A release store has no effect on a reader that loads the flag with a relaxed load.
4. **Reading the data before checking the flag.** Acquire/release is conditional on the load having read the stored value; the data may be read only after the test passes.
5. **Forgetting the second direction in a ring.** The consumer's `head` update needs release and the producer's read of `head` needs acquire, or the producer can overwrite a slot being read.
6. **Using `smp_wmb()` for a doorbell.** It orders CPUs, not devices. The doorbell needs `wmb()` or `writel()`.
7. **Making everything `seq_cst` "to be safe".** It is correct and measured at 12 to 32 times slower in the ring; it also hides which ordering you actually need.
8. **Writing double-checked locking with a plain pointer.** It is the original sin of this topic. Use `pthread_once` or an atomic pointer with acquire and release.
9. **Trusting a sanitizer on fences.** ThreadSanitizer cannot see standalone fences; read the report before concluding anything.
10. **Plain variables in a seqlock.** The data words are read while the writer writes them; make them atomic (portable) or `READ_ONCE` (kernel), and do not drop the re-check of the sequence.

## Questions out loud

1. *Why is `volatile` not enough for a flag?* It makes the compiler reload the flag every time, which fixes the hoisted-loop hang. It does not make the access atomic on every type, adds no CPU barrier, and does not order the flag against the data it announces.
2. *What does release/acquire guarantee?* Conditionally: if the acquire load reads the value written by the release store, every write before the store is visible after the load. Nothing is guaranteed if the load reads an older value.
3. *Why did it work on x86 and fail on arm64?* x86 keeps stores in order and loads in order, so the missing barrier was never needed by the hardware; arm64 lets the flag overtake the data. The source was wrong on both.
4. *Write an SPSC ring and say the orders.* Section 5.1: slot write then release store of `tail`; acquire load of `tail` by the consumer; slot read then release store of `head`; acquire load of `head` by the producer; each side reads its own index relaxed.
5. *What does a seq_cst store cost on x86?* An `xchg`, a locked instruction that drains the store buffer: 12 to 32 times slower in the ring.
6. *`smp_wmb()` or `dma_wmb()` or `wmb()`?* `smp_wmb()` for CPU against CPU, `dma_wmb()` for descriptors in coherent DMA memory, `wmb()` (or `writel()`) when a device register is involved.
7. *Is release followed by acquire a full barrier?* No; different variables can still be seen out of order, so use `smp_mb()` or a seq_cst operation.
8. *How would you find an ordering bug that never shows on your machine?* ThreadSanitizer for the data race, a model checker (herd7) for small patterns, and a run on a weakly ordered CPU.
9. *How does a seqlock work and when does it fail?* Odd counter means a write is in progress; readers retry unless the counter is even and unchanged. It is right for small data and rare writes, and it starves readers if the writer is always busy (the retries in section 3.3).
10. *What is a control dependency good for?* Ordering an earlier load before a later store only; not load to load, and the compiler may remove the branch.

---

## Problems

**Where to solve them.** Any Linux machine with GCC and at least 2 CPUs; the programs are in the text and the answers. `aarch64-linux-gnu-gcc` (package `gcc-aarch64-linux-gnu` on Debian and Ubuntu) lets you read the arm64 output of C code. Godbolt's Compiler Explorer does the same in a browser. ThreadSanitizer is part of GCC and clang (`-fsanitize=thread`).

**How to approach them.** Predict before you run. For A, write the happens-before chain edge by edge. For B, find which edge is missing and whether the test you run could ever show it. For C, read the instruction for the store and ask what it orders. For D, decide what the re-check protects. For E, draw the two indexes and say who reads and writes each.

### Problem A (E, entry level): choose the orders

A thread writes `msg` (a plain struct) and then sets `ready`; another thread waits for `ready` and reads `msg`. Write both sides in C11 with the weakest orders that are correct. Then say what changes if there are two reader threads, and what changes if the writer later sets `ready` back to 0 and reuses `msg`.

### Problem B (M, medium): the test that passes

You are given a flag-and-data program with every access relaxed. It runs 100 million rounds on your x86 laptop with no failure. Give the three reasons that is not evidence of correctness, name two things you would run to find the bug, and say what output each should give.

### Problem C (M, medium): read the arm64

Compile `publish_release` and `consume_acquire` for arm64 (as in section 4). Find the instruction that carries the ordering on each side. Then remove the release from the store, recompile, and say what changed in the output and what changed in what the program promises.

### Problem D (H, hard): what does the re-check protect

In the seqlock of section 3.3, the `nocheck` reader returned torn snapshots in over 5 percent of its reads. Explain the exact interleaving of one torn read. Then explain why a reader that checks the counter only before the data (not after) is also wrong, and what the `nofence` reader needs on arm64.

### Problem E (H, hard): a ring with batching

The consumer processes items in batches of 32. Modify the ring so the consumer publishes `head` once per batch, and the producer publishes `tail` once per batch of 16 pushes. State the new invariants, the orders, and what happens to latency when traffic is light. Say how you would test the change with the tools of section 6.

---

## Answers

**A.** The writer stores `msg` with ordinary writes and then `atomic_store_explicit(&ready, 1, memory_order_release)`. The reader loops on `atomic_load_explicit(&ready, memory_order_acquire)` until it is 1 and then reads `msg`. With two readers nothing changes: each reader's acquire pairs with the one release store. If the writer later resets `ready` and reuses `msg`, the readers must be finished with `msg` first; that needs a second handshake back to the writer (an acknowledgement counter with release on the reader side and acquire on the writer side, as `ack` in section 3.1), or the writer overwrites data that is being read. Release/acquire says nothing about readers that are still reading.

**B.** (1) x86 keeps stores in order and loads in order, so the hardware never reorders the pattern; (2) the compiler happened to keep the source order, which no rule requires; (3) the race window may be too narrow or the timing too regular to hit on this machine. To find the bug, build with `-fsanitize=thread`, which should report a data race on the payload (section 6 shows this), and compile for arm64 and read the output: the store of the flag and the store of the data are both plain `str`, and nothing orders them. A model checker such as `herd7` with an arm64 model would list the stale outcome as allowed (from memory; not run here).

**C.** On the store side, the release version compiles to `stlr w1, [x0]`; with `memory_order_relaxed` it becomes `str w1, [x0]`. On the load side, the acquire load is `ldar` and the relaxed one is `ldr`. Removing the release changed one instruction and removed the promise: with `str` the hardware is free to make the flag visible before the payload store, so a reader's acquire load can see the flag set and read a stale payload. The assembly shown in section 4 gives all four instructions.

**D.** One torn read: the reader reads `seq` (even, say 10) and the first two words (value 5), the writer then bumps `seq` to 11 and writes the last two words (value 6), and the reader reads those and returns `5, 5, 6, 6` without looking at `seq` again; the `good` reader would have seen 12 or 11 on the second read and retried. A reader that checks only before the data is wrong for the same reason: the check says the writer was idle when the reader started, and not that it stayed idle. On arm64 the `nofence` reader needs an acquire fence (or `ldar` on the data loads) between the data loads and the second counter read, so that the second read cannot happen first.

**E.** Invariant: the producer's published `tail` is at most its private count and every slot below it is complete; the consumer's published `head` is at most its private count and every slot below it has been read. Orders are unchanged: release when publishing, acquire when reading the other index, relaxed for a thread's private counters. Latency under light traffic grows because up to 15 items can sit unpublished at the producer and the consumer sees nothing until the batch fills or a timer flushes it; add a flush when the producer goes idle. Test it with the item-order check of section 5, ThreadSanitizer on the version that uses no standalone fences, and a stress test with the consumer deliberately slowed so the ring runs full.

---

## Cheat sheet

- The compiler reorders and removes accesses first; `volatile` or an atomic stops that, nothing more.
- Release/acquire is conditional: reading the stored value is what makes earlier writes visible.
- x86: release, acquire and relaxed are plain `mov`; only seq_cst stores (`xchg`) and seq_cst fences (`lock`) cost. arm64: `stlr`, `ldar`, `dmb`.
- A test on x86 cannot find a missing release or acquire; ThreadSanitizer and a run on arm64 can.
- SPSC ring: slot write then release `tail`; slot read then release `head`; each side acquires the other's index; pad the indexes; cache the other side's index.
- Seqlock: odd counter while writing; readers retry on odd or changed; data words atomic or `READ_ONCE`.
- Kernel: `READ_ONCE`/`WRITE_ONCE` single accesses; `smp_*` for CPUs; `dma_*` for DMA descriptors; `mb`/`wmb`/`rmb` and `writel` for devices.
- Descriptors: check OWN, `dma_rmb()`, read; write fields, `dma_wmb()`, set OWN, then a device-ordered doorbell.
- Release followed by acquire is not a full barrier.
- ThreadSanitizer cannot see standalone fences and `volatile`.

## Sources

Measured on the VM described above; the arm64 assembly was compiled, not run. Not fetched, and stated from memory in the text: the Vyukov bounded queue, the arm64 run-time behaviour, `herd7`, CDSChecker and CBMC, the kernel's definitions of `READ_ONCE` and the sleep and `mmiowb` rules, and the GCC and ThreadSanitizer documentation (the `-Wtsan` warning was read from the compiler's own output).

- [cppreference: `memory_order` (C)](https://en.cppreference.com/w/c/atomic/memory_order), fetched: the six orders, acquire/release synchronisation and release sequences
- [Linux kernel: Linux kernel memory barriers](https://docs.kernel.org/core-api/wrappers/memory-barriers.html), fetched: the barrier vocabulary, minimal guarantees, dependencies and the DMA example (first 100,000 characters directly; the I/O accessor rules from a summary of the rest)
- [CO 20: Memory consistency and atomics](../04_computer_organization/20_memory_consistency_and_atomics.md), the hardware side, litmus tests and the instruction table
- [OS 06: Locks](06_locks.md) and [OS 07: Condition variables and semaphores](07_condition_variables_and_semaphores.md), the locks and waiting that all of this underlies
