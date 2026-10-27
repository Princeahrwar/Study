# CO 20: Memory Consistency and Atomics

*Part of the [Computer Organization plan](00_plan.md) · Core tutorial · Time: about 5 hours (2.5 reading, 2.5 problems) · You need: [tutorial 19](19_cache_coherence.md) (coherence, the cost of an atomic) and [tutorial 11](11_superscalar_and_out_of_order_execution.md), section 7 (the store buffer and store-to-load forwarding)*

By the end of this tutorial you can:

- explain why a program that is correct on one core can fail on two, and name the three places where operations get reordered (compiler, core, memory system);
- define sequential consistency, enumerate the outcomes of a small program under it, and show which outcomes real machines add;
- describe x86-TSO (a store buffer and one allowed reordering) and the weaker Arm, POWER and RISC-V models, and say which of the four load and store reorderings each allows;
- predict the outcome of the four classic litmus tests (store buffering, message passing, load buffering, IRIW) on each model;
- use fences and acquire/release operations correctly, and say what instruction each memory order becomes on x86, Arm, RISC-V and POWER;
- explain why `volatile` is not atomic, and show what goes wrong;
- explain how atomic read-modify-write works (the `lock` prefix, LL/SC, Arm LSE), what `compare_exchange_weak` is for, and the ABA problem with its fixes;
- describe hardware transactional memory (Intel TSX, Arm TME, POWER), lock elision and why it stays niche.

Tutorial 19 made all cores agree on the value of *each address*. It said nothing about the **order** in which a core's stores to *different* addresses become visible to others, and that order decides whether a flag really means "the data is ready". A cache-coherent machine can still show you `flag == 1` and the old `data`. This tutorial is about the contract that says when that may happen, and the instructions you use to forbid it.

**Where the numbers come from.** The litmus-test counts were measured on the cloud virtual machine of tutorials 14 to 19 (an Intel Xeon, two separate cores): two threads run a tiny test millions of times and count the outcomes. The assembly in section 5 is real compiler output (GCC 13 and clang 18, cross-compiling for Arm, RISC-V and POWER). The Intel TSX numbers in section 7 were measured on the same VM, whose CPU supports it. I have only x86 hardware, so what Arm, POWER and RISC-V *allow* is taken from their architecture rules and not measured. Cloud numbers drift, so measured figures are ranges over three runs.

> **The one-sentence model.** Every core runs ahead of the others and buffers its stores, so other cores can see its loads and stores in a different order from the program's; a memory model says which orders are allowed, fences and acquire/release operations forbid the ones you cannot afford, and every ISA charges a different price for them: on x86 only "store then load" has to be paid for, on Arm, POWER and RISC-V everything does.

---

## 1. Why order matters

### 1.1 The example everything comes back to

```cpp
// shared, initially 0:  int data;  int flag;
// Thread A (producer)            // Thread B (consumer)
data = 42;                        while (flag == 0) { }
flag = 1;                         use(data);          // is it 42?
```

On one core, the answer is always yes. With two cores the answer can be no, for three independent reasons.

### 1.2 Who reorders

| Who | What it does | Why |
|---|---|---|
| **The compiler** | moves, merges or deletes loads and stores; keeps a variable in a register; hoists a load out of a loop | speed. It may do anything that a *single* thread cannot notice (the "as-if" rule) |
| **The core** | executes out of order, retires in order, but a retired store waits in the **store buffer** before it reaches the cache; a weak core may also let stores leave the buffer in a different order, and execute loads early | hide the 100 ns it takes to get a line (tutorial 19) |
| **The memory system** | two stores to different lines travel through different cache slices, links and queues, and can become visible to another core in a different order | parallel hardware has no single queue |

A single thread never notices any of this: a load sees the newest earlier store to its own address (store-to-load forwarding, tutorial 11), and every core gives its own program the illusion of running in order. The illusion breaks only when *another* core looks.

### 1.3 What a memory model is

A **memory model** (or memory consistency model) is the contract between the hardware (or the compiler) and the programmer: for any program it defines which outcomes are **allowed**. There are two levels, and your code must satisfy both:

- the **hardware model** of the ISA: x86-TSO, Armv8, RISC-V's RVWMO, POWER;
- the **language model**: C and C++ (since 2011), Java, Rust, Go. The compiler translates the language's guarantees into the right instructions for the target ISA (section 5).

**Coherence is not consistency.** Coherence ([tutorial 19](19_cache_coherence.md)): all cores agree on the sequence of values of *one location*. Consistency: the order in which operations on *different* locations may be seen. The producer example above breaks consistency, not coherence: each variable has one agreed value at all times, but `flag` can change before `data`.

---

## 2. Sequential consistency and the store buffer

### 2.1 The simple model

Lamport (1979) defined **sequential consistency (SC)**: the result of any execution is the same as if all operations of all threads were executed in *some single sequence*, with each thread's operations appearing in program order. It is what most people assume: take all the threads' steps and interleave them.

The classic test, **store buffering (SB)**. All variables start at 0, `r1` and `r2` are registers:

```text
Thread 0:  x = 1;   r1 = y;        Thread 1:  y = 1;   r2 = x;
```

Under SC the four operations can be interleaved in 4! / (2! · 2!) = 6 ways that keep each thread's order (A = `x=1`, B = `r1=y` in thread 0; C = `y=1`, D = `r2=x` in thread 1):

| Order | What happens | (r1, r2) |
|---|---|---|
| A B C D | B reads y before it is set; D reads x = 1 | (0, 1) |
| A C B D | both stores first | (1, 1) |
| A C D B | both stores first | (1, 1) |
| C A B D | both stores first | (1, 1) |
| C A D B | both stores first | (1, 1) |
| C D A B | D reads x before it is set; B reads y = 1 | (1, 0) |

**(0, 0) is not in the table.** Take the later of the two stores in any sequence. The load that follows it in the same thread runs after *both* stores, so it reads a 1. Under SC the outcome (0, 0) is impossible.

### 2.2 Why hardware is not SC: the store buffer

A store must obtain its cache line in an owned state (E or M, tutorial 19) before it can write it. If the line is in another core's cache that takes about 75 to 100 ns on this machine, and a core that stalled on every store would be 100 times slower. So a retired store is put into a **store buffer** (a FIFO of tens of entries: 56 on Intel Skylake, more on newer cores) and the core carries on. The buffer drains into the L1 cache in order, as lines arrive.

```text
   core ──store──▶ [ store buffer: x=1 ... ] ──drain, in order──▶ L1 cache ──▶ coherence ──▶ other cores
     ▲                      │
     └──── load checks the buffer first (forwarding), then L1
```

A later **load to a different address** does not wait for the buffer: it reads the L1 cache at once. So in store buffering, thread 0's `x = 1` can still be sitting in its buffer when thread 1 loads `x` (and sees 0), and the same happens the other way round: **both loads return 0**. The store has been *executed* but is not yet *visible*. This is the **store-to-load reordering** and it is present in every mainstream processor, x86 included.

### 2.3 Measured on the VM

Two threads, one on each of the two cores, run the SB test 1 million times per round, 5 rounds, three runs. Both threads are released at the same instant (a shared cycle counter), and every iteration uses fresh cache lines that another core last touched, so each store has to fetch its line and waits in the buffer for a time of the order of 100 ns (I did not measure the window itself). Count of iterations with (r1, r2) = (0, 0):

| Variant | Instructions per thread | (0, 0) outcomes out of 5 million, 3 runs |
|---|---|---|
| relaxed | `mov` store, `mov` load | **3.79 to 4.01 million** (76 to 80%) |
| release store, acquire load | the same two `mov`s | **3.75 to 4.18 million** (75 to 84%) |
| `seq_cst` store, `seq_cst` load | `xchg` store, `mov` load | **0** in 15 million |
| relaxed + `mfence` between | | **0** in 15 million |
| relaxed + `lock or` between | | **0** in 15 million |

Three conclusions. **The outcome SC forbids is not rare**; with a window like this it is the common case. **Release and acquire do not prevent it**: they compile to the same instructions as relaxed on x86. **Only a full barrier between the store and the load does**, and it can be a fence, a locked instruction, or a `seq_cst` store (an `xchg`, which is locked). How often it happens depends on how closely the two threads start together. The portable program in Problem D synchronises them with a shared counter, which is less exact than the cycle counter used here, and it saw the outcome in 1.6 to 10.4% of the iterations (relaxed and release/acquire, three runs each; `seq_cst` never).

The other two classic tests never failed on x86 (section 3.2 explains why): **message passing** (the producer example) produced the forbidden result 0 times in 15 million, although the consumer saw the flag already set about 1.1 million times in total, so the test was able to catch it; **load buffering** produced 0 of 15 million.

### 2.4 What the fix costs on x86

*Measured*, one thread, 100 million operations, three runs, nanoseconds per store:

| Store | ns |
|---|---|
| plain store | 0.18 |
| release store (a plain `mov`) | 0.27 to 0.41 |
| `seq_cst` store (`xchg`) | 5.3 to 5.7 |
| store followed by `lock or $0,(%rsp)` (a full fence) | 7.8 to 8.2 |
| store followed by `mfence` | 14.3 to 15.0 |

Draining the store buffer is the price. Note that GCC implements a `seq_cst` fence as `lock or` on the stack, which is cheaper on this CPU than `mfence`, while clang emits `mfence`; and that a `seq_cst` *store* is cheaper than store plus fence, because `xchg` does both in one instruction.

---

## 3. The models of real machines

### 3.1 Four reorderings

A load or a store can be reordered with a later load or store in four ways. A model is mostly the list of which of them the hardware may perform (to *different* addresses):

| Model | Load → Load | Load → Store | Store → Store | Store → Load | Other stores seen in one order by all cores? |
|---|---|---|---|---|---|
| **SC** | no | no | no | no | yes |
| **x86-TSO** (SPARC's TSO is the same; IBM Z is similar) | no | no | no | **yes** | yes |
| **Armv8** | yes | yes | yes | yes | yes (since Armv8's "other-multi-copy-atomic" model) |
| **RISC-V RVWMO** | yes | yes | yes | yes | yes |
| **POWER** | yes | yes | yes | yes | **no** |

(Alpha was weaker still: it even reordered two loads where the second depended on the first. Nothing alive today does.) "Yes" means *allowed*: a weak core need not do it, and many do not do it most of the time. The point is that you may not rely on it not happening.

All of these models still guarantee that a thread sees **its own** operations in order, and that **dependencies** are respected (section 3.3).

### 3.2 x86-TSO in detail

x86's model is **Total Store Order**, a store buffer and nothing else:

1. Loads are not reordered with other loads. Stores are not reordered with other stores. A store is not reordered with an *earlier* load.
2. **A load may be reordered with an earlier store to a different address** (the store sits in the buffer). This is the only one.
3. A core sees its own buffered stores (forwarding).
4. All other cores see the stores of any core in that core's program order, and agree on the order in which different cores' stores happened.
5. Locked instructions (`lock`-prefixed instructions and `xchg` with memory) and `mfence` drain the buffer: they are full barriers.

This is why message passing works without any fence on x86: the producer's two stores leave the buffer in order, and the consumer's two loads execute in order. It is also why the same code **fails on Arm**: it is the commonest porting bug in lock-free code, and x86 testing can never find it.

Exceptions (you meet them in drivers and memcpy code): **non-temporal stores** (`movnti`, `movntdq`), write-combining memory and the fast `rep movs` string operations are *weakly ordered* even on x86; they need an `sfence` before the flag store that publishes them.

### 3.3 The weak models: Arm, POWER, RISC-V

These allow all four reorderings. They get there in several ways: stores leave the store buffer out of order (a store whose line is already in the cache may overtake an older store that is waiting for its line), the core executes loads early and speculatively, and the memory system delivers lines in different orders. The gain is simpler, cheaper hardware and more freedom to overlap misses; the price is that **you must say what order you need**.

What a weak core does **not** break are **dependencies**, because it cannot even compute the next operation without the earlier result:

- **address dependency:** `p = load(ptr); v = load(*p)`: the second load cannot start before the first one returns;
- **data dependency:** `v = load(a); store(b, v)`: the stored value comes from the load;
- **control dependency:** `if (load(a)) store(b, 1)`: the store is not performed until the branch resolves (a *load* after a branch may still run early, speculatively).

C++ tried to expose this for free (`memory_order_consume`); no compiler implements it as specified, and all promote it to `acquire`.

**Multi-copy atomicity** (the last column of the table): in an MCA machine, once a store becomes visible to one other core it is visible to all of them. POWER is not MCA, so two readers can disagree about the order of two independent writes (the IRIW test below).

### 3.4 Litmus tests and who allows what

A **litmus test** is a tiny program with an interesting outcome. All variables start at 0; plain (relaxed) accesses. "Allowed" means the machine may produce that outcome.

| Test | Program | The outcome | SC | x86-TSO | Armv8 | RISC-V | POWER |
|---|---|---|---|---|---|---|---|
| **SB** (store buffering) | T0: `x=1; r1=y` · T1: `y=1; r2=x` | r1 = 0 and r2 = 0 | no | **yes** | yes | yes | yes |
| **MP** (message passing) | T0: `data=1; flag=1` · T1: `r1=flag; r2=data` | r1 = 1 and r2 = 0 | no | no | **yes** | yes | yes |
| **LB** (load buffering) | T0: `r1=y; x=1` · T1: `r2=x; y=1` | r1 = 1 and r2 = 1 | no | no | yes (rare in hardware) | yes | yes |
| **IRIW** | T0: `x=1` · T1: `y=1` · T2: `r1=x; r2=y` · T3: `r3=y; r4=x` (readers' loads kept in order) | r1=1, r2=0, r3=1, r4=0 | no | no | no | no | **yes** |

Which reordering causes which: SB is store→load; MP is store→store in the producer or load→load in the consumer; LB is load→store; IRIW is a failure of multi-copy atomicity. On this VM I could run SB, MP and LB; the Arm, RISC-V and POWER columns come from the architecture rules.


---

## 4. Fences, acquire and release

### 4.1 Four barriers, named by what they separate

Name a barrier by the kind of operation before it and the kind after it:

| Barrier | Stops this from moving across | x86 already guarantees it? |
|---|---|---|
| LoadLoad | an earlier load and a later load | yes |
| LoadStore | an earlier load and a later store | yes |
| StoreStore | an earlier store and a later store | yes |
| **StoreLoad** | an earlier store and a later load | **no** (the store buffer) |

Real programs rarely need these one at a time. They need two combinations, and a third that is stronger than both:

- **Acquire**, attached to a *load*: nothing after the load (in program order) may move before it. That is LoadLoad plus LoadStore, *after that load*. Use it on the load that reads a flag.
- **Release**, attached to a *store*: nothing before the store may move after it. That is LoadStore plus StoreStore, *before that store*. Use it on the store that sets a flag.
- **Full** (`seq_cst`, or a full fence): all four, including StoreLoad.

The contract, in one sentence:

> If an acquire load returns the value written by a release store, then everything the writer did **before** the release is visible to everything the reader does **after** the acquire.

```text
 Producer                                   Consumer
 data = 42;                  ─┐
 flag.store(1, release);      │   reads-from   while (flag.load(acquire) == 0) { }
        └── nothing above ────┴──────────────▶        └── nothing below moves above this load
            moves below this store                    use(data);          // sees 42
```

Neither one orders an earlier release *store* against a later acquire *load* of a different address. That pair is StoreLoad, which is why release and acquire did not fix store buffering in section 2.3.

**Why acquire and release cost nothing on x86.** TSO already gives LoadLoad, LoadStore and StoreStore, so the compiler only has to avoid moving the operations itself (a *compiler barrier*, which emits no instruction). The same C++ becomes `ldar`/`stlr` on Arm and a `fence` or a `lwsync` on RISC-V and POWER (section 5.3).

### 4.2 The barrier instructions

| You want | x86-64 | Arm64 | RISC-V | POWER |
|---|---|---|---|---|
| **Full barrier** (includes StoreLoad) | `mfence`, or any `lock`-prefixed instruction, or `xchg` with memory | `dmb ish` | `fence rw,rw` | `sync` |
| **Acquire** (a load, then everything after it) | nothing | `ldar` instead of `ldr`; or `dmb ishld` after | `fence r,rw` after the load | `lwsync` after the load; or branch on the loaded value, then `isync` |
| **Release** (everything before it, then a store) | nothing | `stlr` instead of `str`; or `dmb ish` before | `fence rw,w` before the store | `lwsync` before the store |

Notes you will be asked about:

- **x86** has two other fences. `sfence` orders stores and matters only for the weakly ordered stores of section 3.2 (non-temporal and write-combining). `lfence` waits until all earlier instructions have completed locally; it is not needed to order ordinary loads, and in practice it appears in Spectre mitigations.
- **Arm** has `dmb` (a memory barrier, with a domain `ish` for the inner-shareable domain, which is all the cores of an ordinary system, and a type `ld`, `st` or full), `dsb` (waits until earlier memory accesses have *completed*, needed around device and cache-maintenance operations) and `isb` (flushes the pipeline). `ldar` and `stlr` are *sequentially consistent with each other*: a `stlr` followed by an `ldar` to another address is not reordered. That is why a C++ `seq_cst` store and load need no extra instruction on Arm (section 5.3). Armv8.3 adds `ldapr`, a cheaper acquire that is not ordered after an earlier `stlr`.
- **RISC-V** `fence` takes a predecessor set and a successor set from `r` (reads), `w` (writes), `i` and `o` (device input and output). `fence.tso` is the x86 guarantee: everything except store before load.
- **POWER** has three: `sync` (full, expensive), `lwsync` (everything except StoreLoad, so it is the acquire and the release fence) and `isync` (it discards speculatively executed instructions; after a branch that depends on a load it makes the load an acquire).

Barriers for **devices and DMA** are a different, stronger family (an Arm `dsb` or `dmb oshst`, an x86 `sfence`, a read-back of the register). They appear in the tutorial on interrupts and DMA (26).

### 4.3 Fences as separate statements

C++ also lets you put the barrier in its own statement. The message-passing example in fence form:

```cpp
// Producer
data = 42;
std::atomic_thread_fence(std::memory_order_release);   // earlier loads and stores stay before later stores
flag.store(1, std::memory_order_relaxed);

// Consumer
while (flag.load(std::memory_order_relaxed) == 0) { }
std::atomic_thread_fence(std::memory_order_acquire);   // earlier loads stay before later loads and stores
use(data);
```

It means the same. The difference is cost and reach: the release fence orders **all** earlier accesses against **all** later stores, so one fence can cover several stores (a good fit for the seqlock in Problem B), but on Arm a release fence is a full `dmb ish` while a release *store* is the cheaper `stlr`. Prefer the ordered operation when one operation is the whole story, and the fence when one barrier replaces several.

### 4.4 When release and acquire are not enough: StoreLoad

Release/acquire cannot stop "I wrote my flag, then read yours, and so did the other thread." This **store, then load of another location** shape, repeated on both sides, is the store-buffering litmus test in disguise. It turns up in real code more often than you might expect:

- **Dekker's and Peterson's algorithms:** set my flag, then read the other's.
- **Sleep and wake-up handshakes (futex-like code, event counts):** the waiter sets `waiting = 1` and then re-checks the queue; the producer adds to the queue and then checks `waiting`. If both loads can see the old values, the waiter sleeps forever (a *lost wake-up*).
- **Hazard pointers:** publish the pointer you are about to use, then re-read the source to check it did not change.
- **Biased and asymmetric locks, and some RCU-like schemes.**

The rule: **between the store and the load there must be a full barrier.** In C++ that means `seq_cst` operations on both sides, or `std::atomic_thread_fence(std::memory_order_seq_cst)` between them. It costs the store-buffer drain of section 2.4 (5 to 15 ns on x86), so it is the one barrier worth keeping off the fast path when you can.

### 4.5 A method for placing barriers

1. **Find every hand-off:** a place where one thread writes data and then writes something (a flag, an index, a pointer) that tells another thread "it is ready". Mark the data writes, the "ready" write and the reader's read of it.
2. Make the "ready" store a **release** and the reader's load an **acquire**. Everything the writer did before is now visible after the read.
3. Look for the **store, then load another location** shape on both sides (section 4.4). Those need `seq_cst` or a full fence.
4. Everything else, for example statistics counters whose value nobody uses to decide anything about other data, can stay **relaxed**.
5. **Do not trust a test on x86.** x86 hides every mistake except StoreLoad. Use ThreadSanitizer for the data races, run the code on a weak machine (an Arm server or an Apple-silicon Mac), or check the core primitive with a model checker (`herd7`/`litmus7` for the hardware models, CppMem or CDSChecker for C++). The OS track's [memory barriers note](../05_operating_systems/07_memory_barriers.md) has the single-producer single-consumer ring worked through line by line.

---

## 5. The C++ memory model

### 5.1 Data races and happens-before

C and C++ define their own memory model so that the same source works on x86, Arm and the rest:

- A **data race** is two threads accessing the same non-atomic location at the same time, at least one of them writing, with nothing ordering them. A data race is **undefined behaviour**, not just "an unpredictable value". The compiler is allowed to assume it never happens, and section 5.4 shows what it does with that assumption.
- Operations on a `std::atomic` never race; they are defined for every ordering.
- **Happens-before** is the ordering the rules give you: operations in one thread are ordered by program order; an acquire load that reads the value of a release store *synchronises with* it; and the relation is transitive. If A happens-before B, then B sees A's writes.

If your program has no data races and you use only the default `seq_cst` operations, it behaves as the SC model of section 2.1 says. This is the "sequential consistency for data-race-free programs" promise. The weaker orders are an optimisation for code that needs it.

### 5.2 The memory orders

| Order | Guarantee | Use it for |
|---|---|---|
| `relaxed` | atomicity only; no ordering against other variables | statistics counters; a flag whose meaning does not guard other data; anything ordered by something else |
| `acquire` | on a load: later operations stay after it | reading a "ready" flag, an index or a lock |
| `release` | on a store: earlier operations stay before it | publishing data, releasing a lock |
| `acq_rel` | both, on a read-modify-write | `fetch_add` or `compare_exchange` that both takes and passes on something |
| `seq_cst` | acquire and release, plus one total order of all `seq_cst` operations | the default; needed for the StoreLoad shape |
| `consume` | meant to give only dependent loads an acquire | **do not use**: every compiler promotes it to `acquire` |

Start with the default `seq_cst`, get the algorithm right, and weaken one operation at a time with a written reason. An unjustified `relaxed` is a bug that no x86 test will find.

### 5.3 What the compiler emits

The same C++ operations compiled for four ISAs (32-bit `int` operations; GCC 13.3 for x86-64 and Arm64, clang 18 for RISC-V `rv64gc` and POWER `powerpc64le -mcpu=pwr8`, all `-O2`; the real output, with labels removed):

| C++ operation | x86-64 | Arm64 (Armv8.0) | RISC-V | POWER |
|---|---|---|---|---|
| load, relaxed | `mov` | `ldr` | `lw` | `lwz` |
| load, acquire | `mov` | `ldar` | `lw` ; `fence r,rw` | `lwz` ; `cmpd` ; `bne-` ; `isync` |
| load, seq_cst | `mov` | `ldar` | `fence rw,rw` ; `lw` ; `fence r,rw` | `sync` ; `lwz` ; `cmpd` ; `bne-` ; `isync` |
| store, relaxed | `mov` | `str` | `sw` | `stw` |
| store, release | `mov` | `stlr` | `fence rw,w` ; `sw` | `lwsync` ; `stw` |
| store, seq_cst | **`xchg`** | `stlr` | `fence rw,w` ; `sw` | `sync` ; `stw` |
| `fetch_add`, relaxed | `lock xadd` | `ldxr`/`add`/`stxr` loop | `amoadd.w` | `lwarx`/`addi`/`stwcx.` loop |
| `fetch_add`, seq_cst | `lock xadd` | `ldaxr`/`add`/`stlxr` loop | `amoadd.w.aqrl` | `sync` ; loop ; `lwsync` |
| `compare_exchange`, seq_cst | `lock cmpxchg` | `ldaxr`/`cmp`/`stlxr` loop | `lr.w.aqrl`/`sc.w.rl` loop | `sync` ; `lwarx`/`stwcx.` loop ; `lwsync` |
| `atomic_thread_fence(acquire)` | nothing | `dmb ishld` | `fence r,rw` | `lwsync` |
| `atomic_thread_fence(release)` | nothing | `dmb ish` | `fence rw,w` | `lwsync` |
| `atomic_thread_fence(seq_cst)` | `lock or $0,(%rsp)` (GCC); `mfence` (clang) | `dmb ish` | `fence rw,rw` | `sync` |

Reading the table:

- **x86 charges only for the StoreLoad cases:** the `seq_cst` store (`xchg`) and the read-modify-writes. Everything else is a plain `mov`, which is why x86 testing hides ordering bugs.
- **Arm** uses `ldar` and `stlr` (what they cost depends on the core; I could not measure it here) and runs the read-modify-writes as a loop. With the Armv8.1 large-system extension (LSE, section 6.3) GCC emits one instruction instead: `ldadd` for the relaxed `fetch_add`, `ldaddal` for the `seq_cst` one and `casal` for the `compare_exchange`.
- **RISC-V** with clang 18 puts explicit `fence` instructions around plain loads and stores (the base mapping); the atomic read-modify-write instructions carry acquire and release bits (`.aq`, `.rl`) themselves.
- **POWER** uses `sync` where x86 would use a locked instruction, and builds the acquire load from a never-taken branch on the loaded value followed by `isync`, the control-dependency trick of section 3.3.

### 5.4 `volatile` is not atomic

In C and C++, `volatile` tells the compiler "every access in the source must happen, in this order *relative to other volatile accesses*". It was designed for memory-mapped device registers. It gives **neither atomicity nor ordering between threads**. (Java's and C#'s `volatile` mean something else: they are sequentially consistent atomics.) Three experiments on the VM.

**1. It does not make read-modify-write atomic.** Two threads, one per core, each add 1 to a shared `long` ten million times; the right answer is 20,000,000:

| Counter | Result, three runs |
|---|---|
| `volatile long`, `x++` | **10.2 to 12.9 million** (7.1 to 9.8 million updates lost) |
| `add $1, mem` written in assembly, no `lock` | 10.4 to 11.9 million |
| `lock add $1, mem` | 20,000,000 every time |
| `atomic_fetch_add` (`lock xadd`) | 20,000,000 every time |
| plain `long`, `x++` | 20,000,000 every time, **by accident** (below) |

`x++` on a volatile is a load, an add and a store (or a single `add` to memory without `lock`); another core's update can land between them and be overwritten. The plain `long` row looks like a pass but is the worst result of all: at `-O2` GCC turned the whole ten-million-iteration loop into one instruction, `add $10000000, plain_c`, so the race window was a single instruction and nothing was lost. It is still a data race and still undefined behaviour; change the compiler, the flags or the loop body and it breaks.

**2. It does not stop the compiler from assuming there is no other thread.**

```cpp
int flag;                                // plain
void wait_flag(void) { while (!flag); }
```

GCC 13 at `-O2` compiles that to a load followed by `.L3: jmp .L3`: it read `flag` once and loops forever. In a test, a second thread set the flag after 100 ms and the program never returned (killed by a timeout). With `volatile int flag` or a `std::atomic<int>` loaded with `memory_order_relaxed` the loop re-reads the flag and the program finished as soon as the 100 ms were over. The relaxed atomic costs the same as the volatile (a `mov`) and is the correct replacement.

**3. It gives no ordering to anything else.** A volatile flag guarding plain data publishes nothing: there is no release, so on Arm the reader can see the flag and stale data, and even on x86 the compiler is allowed to move the plain data accesses across the volatile ones.

Use `volatile` for device registers and `sig_atomic_t`-style signal flags. For threads, use `std::atomic`.

### 5.5 Atomicity of plain accesses

A naturally aligned load or store of 1, 2, 4 or 8 bytes is a single indivisible access on x86 and Arm; a value that straddles a cache line can be read **torn**, half old and half new (tutorial 19's *split lock* is the same crossing). Wider objects are not atomic with ordinary instructions. `std::atomic<T>` hides all of this, and `is_lock_free()` tells you whether the compiler found a real instruction or fell back to a hidden lock, which it often does for objects wider than 8 bytes (a 16-byte atomic needs `cmpxchg16b` on x86-64 and depends on the compiler, its flags and the library).

---

## 6. Atomic read-modify-write

### 6.1 The problem

`counter++` is a load, an add and a store. If two cores run it at the same time on the same value, both load 5, both store 6, and one increment is lost (the volatile counter above lost 35 to 49% of them). An atomic read-modify-write must make the three steps indivisible. Each ISA family has its own way.

### 6.2 x86: the `lock` prefix

`lock add`, `lock xadd`, `lock cmpxchg`, `lock or`, `lock and`, `lock xor`, plus `xchg` with a memory operand (always locked) and the double-width `lock cmpxchg16b`. The core obtains the cache line in the Modified state (tutorial 19), **does not answer snoops for that line** until the instruction finishes. It also drains the store buffer before it executes, and later loads wait for it, so a locked instruction is a full barrier. Cost on this VM, uncontended: 5.6 to 6.0 ns (about 18 to 20 cycles), and with a second core fighting for the line 40 to 49 ns (tutorial 19).

Only some operations have a locked instruction that **returns the old value**: add and subtract (`xadd`), exchange and compare-and-swap. For `and`, `or` and `xor` the old value is not available from `lock or`, so when the program uses the result the compiler emits a `cmpxchg` loop (the GCC 13 output for `fetch_or` is exactly that). Tutorial 19 advised using the dedicated atomic when one exists; on x86 that is true for add and exchange, but a `fetch_or` or `fetch_and` whose result you use is a CAS loop anyway.

### 6.3 Arm, POWER, RISC-V: load-linked and store-conditional

These ISAs have no instruction that holds a line through a read, an ALU operation and a write. They offer a pair instead:

```asm
retry:  ldxr  w0, [x1]          // load-exclusive: read, and mark this address as "mine" (the exclusive monitor)
        add   w2, w0, 1
        stxr  w3, w2, [x1]      // store-exclusive: store only if the mark is still there; w3 = 0 on success, 1 on failure
        cbnz  w3, retry         // lost the race (or the mark was cleared): try again
```

The mark is cleared when another core writes the line, and also, depending on the implementation, when this core takes an interrupt or an exception, or evicts the line. The store-conditional can therefore **fail even if nobody else touched the value** (a *spurious* failure). The hardware guarantees progress only for short loops: keep the code between the two instructions to a handful of simple ALU operations, with no other loads or stores and no barriers.

Equivalent pairs: POWER `lwarx` and `stwcx.` (`ldarx`/`stdcx.` for 64 bits); RISC-V `lr.w` and `sc.w`.

**Why `compare_exchange_weak` exists.** On an LL/SC machine, `compare_exchange_strong` has to be a loop of its own, to hide spurious failures: it retries until it knows the value really differed. `compare_exchange_weak` is allowed to fail spuriously and skips that inner loop. So: in a retry loop of your own (the usual case) use `weak`; when you do not loop, use `strong`. On x86 both are the same `lock cmpxchg`.

**Single-instruction atomics.** Under contention an LL/SC loop is poor: every core pulls the line in exclusive state, and a core that loses it fails its store-conditional and starts again. Armv8.1 added the **large-system extension (LSE)**: `ldadd`, `ldset` (or), `ldclr` (and-not), `ldeor`, `swp`, `cas` and `casp` (a pair, for double-width), each with suffixes for acquire (`a`), release (`l`) or both (`al`). They are single instructions, and an implementation may execute them in the shared cache or the interconnect and not in the core, so the line need not bounce (what a given chip does is up to its designer). Recent GCC and clang can emit calls to small helper functions that choose LSE or an LL/SC loop at run time ("outline atomics"), so one binary runs on old and new cores. RISC-V's A extension has the `amo` instructions (`amoadd`, `amoswap`, `amoand`, `amoor`, `amoxor`, `amomax`, `amomin`) but no compare-and-swap; a newer extension, Zacas, adds `amocas`.

### 6.4 Compare-and-swap and CAS loops

`compare_exchange(expected, desired)`: if the location equals `expected`, store `desired` and return true; otherwise copy the current value into `expected` and return false. It is the universal building block: anything can be built from it as a *read, compute, try to publish, retry* loop.

```cpp
// An atomic maximum: there is no x86 instruction for it, so build it from CAS.
void atomic_max(std::atomic<int>& a, int v) {
    int cur = a.load(std::memory_order_relaxed);
    while (cur < v &&                                     // nothing to do if the stored value is already big enough
           !a.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {
        // failed: 'cur' now holds the value someone else wrote; re-test and retry
    }
}
```

A CAS loop costs about 10 ns per success on this VM when uncontended (tutorial 19) and, unlike `fetch_add`, can fail under contention and redo its work, so it scales worse.

### 6.5 The ABA problem

CAS asks "is the value still what I read?" It cannot tell "unchanged" from "changed and changed back". A lock-free stack shows the trouble:

```cpp
struct Node { int value; Node* next; };
std::atomic<Node*> head{nullptr};

void push(Node* n) {
    n->next = head.load(std::memory_order_relaxed);
    while (!head.compare_exchange_weak(n->next, n, std::memory_order_release, std::memory_order_relaxed)) { }
}

Node* pop() {
    Node* h = head.load(std::memory_order_acquire);
    while (h && !head.compare_exchange_weak(h, h->next,          // h->next is read before the CAS runs
                                            std::memory_order_acquire, std::memory_order_acquire)) { }
    return h;
}
```

Start with the stack `A → B → C` (head points to A). Thread 1 starts a `pop`:

| Step | Thread | Action | Stack afterwards |
|---|---|---|---|
| 1 | T1 | reads `h = A` and `h->next = B`, then is descheduled just before its CAS | A → B → C |
| 2 | T2 | `pop()` returns A | B → C |
| 3 | T2 | `pop()` returns B; the program frees B | C |
| 4 | T2 | `push(A)` puts the same node back (it went back to a pool and was handed out again) | A → C |
| 5 | T1 | resumes: CAS(`head`, expected A, desired B) **succeeds**, because `head` is A again | **head = B, a freed node** |

The CAS was right that `head` still holds A, and wrong about everything else: A's `next` is now C, not B. The stack is corrupt and `head` points to freed memory. (A second, related bug: in step 1 `h->next` is a read of a node another thread may already have freed. Safe memory reclamation, below, fixes both.)

The fixes:

- **Tag the pointer with a counter** and CAS the pair, so a reused address no longer matches. Either use a double-width CAS, or squeeze a counter into the unused high bits of a 48-bit pointer (fragile: 57-bit addressing, and Arm pointer-authentication and tagging use those bits). The double-width CAS is `lock cmpxchg16b` on x86-64 (it needs `-mcx16` and a 16-byte aligned object) and `casp` on Arm. Check what your toolchain really generates: in my test, GCC 13 and clang 18 with libstdc++ did **not** turn a `std::atomic` of a 16-byte struct into that instruction. They called libatomic and `is_lock_free()` returned false, even with `-mcx16`. `__sync_bool_compare_and_swap` on an `unsigned __int128` gave one inline `lock cmpxchg16b` in GCC 13, and clang 18 inlined the `__atomic` form too.
- **Safe memory reclamation:** never free or reuse a node while any thread might still hold a pointer to it. Hazard pointers, epoch-based reclamation and RCU are the three families. The lock-free data structures note in the LLD track works the stack through in detail ([lock-free data structures](../10_lld/02_systems_lld/04_lockfree_data_structures.md)).
- **Do not recycle nodes** (use a pool that only grows), or **avoid the structure:** a single-producer single-consumer ring has no ABA at all.
- **LL/SC does not make a C++ CAS loop immune.** Real LL/SC fails on *any* write to the location, so an assembly loop that holds the reservation across the whole operation would not see ABA. But a C++ `compare_exchange` reads the value, returns, lets you compute, and only then runs the store-conditional; the reservation covers a few instructions, not the window between steps 1 and 5. The stack above is broken on every ISA.

---

## 7. Hardware transactional memory

### 7.1 The idea

Instead of making each operation atomic, let the hardware run a whole critical section **speculatively** and decide at the end whether it was atomic. A transaction runs like this:

1. `xbegin` takes a checkpoint of the registers and starts tracking. Every line the code **reads** becomes part of the *read set*; every line it **writes** becomes part of the *write set*, and the new data stays in the L1 cache, invisible to other cores. (The write set lives in the L1; the read set is tracked there and, as 7.4 shows, a little beyond.)
2. Tutorial 19's coherence messages do the conflict detection: if another core **writes** a line in my read set, or **reads or writes** a line in my write set, the transaction has a *conflict* and **aborts**: all of its writes are discarded, the registers are restored, and execution continues at the address `xbegin` named, with a status code.
3. `xend` with no conflict **commits**: all the writes become visible at once.

Compare with a lock: the lock says "only one thread at a time", whether or not the threads touch the same data. A transaction says "go ahead together, and I will undo it if you actually collide". Where the data really is disjoint, nothing waits.

### 7.2 What exists

| Machine | Instructions | Status (as far as I know) |
|---|---|---|
| Intel TSX | **RTM**: `xbegin`, `xend`, `xabort`, `xtest` (also the older prefix form, HLE, which this tutorial leaves out) | present on some server parts, absent from many client parts; disabled by microcode on some models after bugs (see 7.5) |
| IBM POWER | `tbegin.`, `tend.`, `tabort.` | POWER8 and POWER9 had it; I believe POWER10 dropped it |
| IBM Z | `TBEGIN`, `TBEGINC` (a constrained form with a progress guarantee), `TEND` | in use in IBM's mainframes since the zEC12 generation |
| Arm | TME: `tstart`, `tcommit`, `ttest`, `tcancel` | specified for Armv9-A; I know of little or no shipping silicon |

The CPU on this VM supports Intel RTM, so section 7.4 is measured. Everything below is about RTM.

### 7.3 Lock elision

The main use is **lock elision**: keep the lock for correctness but, most of the time, never write it. The code runs inside a transaction that merely **reads** the lock variable and aborts if it is held. If two threads use the same lock on different data, both transactions commit and neither waits. A conflict, a capacity problem or any other abort sends the thread to the **fallback path**, which takes the real lock; that write to the lock aborts every transaction that had read it, so the fallback is safe.

```cpp
void lock_elided(Lock& L) {
    for (int tries = 0; tries < 3; ++tries) {
        unsigned s = _xbegin();
        if (s == _XBEGIN_STARTED) {
            if (!L.is_locked()) return;            // runs the critical section inside the transaction; the lock is in my read set
            _xabort(0xff);                          // somebody holds the real lock: give up this attempt
        }
        if (!(s & _XABORT_RETRY)) break;            // the hardware says retrying is pointless
        while (L.is_locked()) _mm_pause();          // do not retry while the real lock is held
    }
    L.lock();                                       // fallback: take the real lock the usual way
}
void unlock_elided(Lock& L) {
    if (_xtest()) _xend();                          // we are inside a transaction: commit
    else          L.unlock();                       // we took the real lock
}
```

The fallback path is **mandatory**: the hardware gives no promise that any transaction ever commits.

### 7.4 What I measured

Intel RTM on this VM (Xeon, L1 data cache 48 KB, 12-way, 64-byte lines, 64 sets). All memory was touched first; a page fault inside a transaction aborts it, and my first test failed 100% for that reason. Three runs each.

**Cost.** An empty `xbegin`/`xend` pair takes **13.8 to 14.0 ns** (about 29 cycles), more than twice a `lock xadd` (5.6 to 6.0 ns). Of a million empty transactions, 31 to 62 aborted for no reason of mine, most likely timer interrupts.

**Write capacity.** The transaction writes one byte in each of K different lines:

| Lines written | Size | Commit rate |
|---|---|---|
| 1 to 640 | up to 40 KB | 98.5 to 100% |
| 768 | 48 KB (the whole L1 data cache) | 73.9 to 99.5% |
| 1024 or more | 64 KB or more | **0%** (every attempt a capacity abort) |

**Associativity.** The limit is not only the L1 size. When the K lines are 4096 bytes apart, all of them map to the *same* L1 set, and the cache is 12-way:

| Lines in one set | Commit rate |
|---|---|
| 10 | 99.7 to 100% |
| 11 | 98.4 to 100% |
| 12 | 95.1 to 100% |
| **13** | **20.3 to 31.8%** |
| 14 | 0.4 to 1.4% |
| 15 | 0.0% |

A 13th written line to one set has to evict a line of the write set, and the transaction cannot survive that. This is why a transaction that "should" fit sometimes aborts: it depends on the addresses, and on whatever else (the stack, the lock, code data) shares those sets.

**Read capacity.** Reading up to 32 KB always committed; 64 KB committed 97 to 98%; 256 KB or more committed **0%**. The read set is tracked more loosely than the write set, but only up to a limit.

**System calls.** A `getpid()` inside a transaction aborted **every time** (10,000 of 10,000, in all three runs). So do other instructions the hardware cannot undo, and page faults, interrupts and exceptions.

**Two threads, 4 million increments in total** (each thread adds 2 million; time in seconds):

| Scenario | Time | Aborts |
|---|---|---|
| counters on **separate lines**, one lock, **plain spin lock** | 0.26 to 0.31 | none |
| the same, **lock elided** | **0.034 to 0.042** | 112 to 466 |
| **one shared counter**, plain spin lock | 0.43 to 0.52 | none |
| one shared counter, lock elided | 0.20 to 0.28 | 2.0 to 2.4 million |
| one shared counter, `fetch_add` | **0.10 to 0.12** | none |
| two counters 8 bytes apart (**one line**), lock elided | 0.14 to 0.24 | 1.1 to 3.0 million |

What it shows:

- **Elision does what it promises when the data is disjoint:** the same lock, about 7 to 8 times faster than taking it, and almost no aborts.
- **It cannot beat real sharing.** On one shared counter roughly a third of the attempts conflict and are retried (2.0 to 2.4 million aborts for 4 million increments; the lock fallback ran 0 to 377 times). It was still 1.5 to 2.3 times faster than the spin lock, but 1.7 to 2.5 times **slower than a single atomic add**. For a counter, use the atomic.
- **False sharing is a conflict.** Conflicts are detected per cache line, so two counters 8 bytes apart abort each other about as often as one shared counter does. The effect of tutorial 19 returns, now as aborts.

A first version of this test showed a flood of conflict aborts even on separate counters, because my statistics counters and the fallback lock shared a line. Padded to a line each, the aborts disappeared. Elided code is as sensitive to layout as anything else.

### 7.5 Why it stayed niche

- **No guarantee of progress:** capacity, associativity, interrupts, system calls, page faults and many instructions all abort, so every transaction needs a correct fallback path, and the fallback must be tested.
- **The lemming effect:** once one thread takes the real lock, every in-flight transaction aborts, retries, finds the lock held and also falls back; the whole group serialises until the lock is free.
- **Hard to debug and to tune:** the state of an aborted transaction is gone, you cannot step inside one, and the abort rate depends on addresses and timing.
- **Security and errata:** Intel disabled TSX by microcode on early Haswell and Broadwell parts after a bug; some Skylake parts have a mode (`TSX_FORCE_ABORT`) that makes every transaction abort; the **TSX asynchronous abort** leak (TAA, CVE-2019-11135, in the family of microarchitectural data-sampling attacks) led Linux to offer a `tsx=` boot option, and many systems leave TSX off; researchers also used transactions to probe kernel addresses without taking a fault.
- **Software:** glibc has opt-in elision for `pthread` mutexes (a tunable, off by default).
- **It is rarely the best answer:** fine-grained locks, atomics, per-core data and RCU (tutorial 19 and the OS track) usually do as well without the aborts. Elision pays when you have a coarse lock you cannot restructure and the work under it is mostly disjoint, which is the top two rows of the table above.

---

## 8. What this means for your code

| Symptom | Likely cause | Try |
|---|---|---|
| Works on x86, returns stale data on Arm, POWER or RISC-V (flag seen, data old) | the code relies on TSO: a plain or relaxed flag guards other data | release store of the flag, acquire load of it; check with ThreadSanitizer and on an Arm machine |
| Two threads both enter a critical section, or both miss each other's flag, or a waiter sleeps forever (Dekker, Peterson, lost wake-up) | StoreLoad: each side stores its own flag, then loads the other's, with only release/acquire between | `seq_cst` on both sides, or a full fence between the store and the load |
| An optimised build hangs in a `while (!flag)` loop; the debug build works | the compiler hoisted the load out of the loop (a data race, so undefined behaviour) | `std::atomic` with `relaxed` (or `acquire` if the flag guards data), not `volatile` |
| A shared counter ends below the expected total | non-atomic read-modify-write (plain, or `volatile`) | `fetch_add`, or per-thread counters summed at the end |
| A lock-free stack or queue corrupts rarely, usually after a `free` | ABA, or reading a node another thread has freed | tagged pointer or safe memory reclamation (hazard pointers, epochs, RCU) |
| A `seq_cst` store appears in the profile (5 ns or more on x86) | `seq_cst` where release is enough: it is an `xchg` | release for publishing; keep `seq_cst` for the StoreLoad shape only |
| A CAS loop gets slower as threads are added | failed attempts redo work and fight for the line | `fetch_add` or `exchange` if one fits; per-core data; fewer shared writes |
| Transactions abort all the time | capacity or associativity, a conflict (false sharing counts), a system call inside | shrink the section, pad the data, cap the retries, ask whether a plain atomic does the job |
| A bug appears only under load, or only on a new CPU | the window is a few nanoseconds wide and the old CPU never hit it | stress on the weakest hardware you ship to, TSAN, a model checker on the core primitive |
| An x86 test of a lock-free ring buffer passes with every order relaxed | x86 compiles `relaxed` and `release`/`acquire` to the same `mov`s; QEMU on an x86 host does not reorder either | [tutorial 31](31_arm_vs_x86_and_putting_it_together.md) section 3 and Problem A: ThreadSanitizer flags it on x86, and the Arm listing shows `ldar`/`stlr` against `ldr`/`str` |

---

## 9. Mistakes that cost points

1. **"x86 is strongly ordered, so I need no barriers."** x86 allows store-to-load reordering (section 2.3 measured it in 75 to 84% of the iterations), and the compiler reorders whatever the CPU would not.
2. **Believing release and acquire fix Dekker.** They give LoadLoad, LoadStore and StoreStore. The StoreLoad case needs `seq_cst` or a full fence.
3. **Using `volatile` between threads.** No atomicity (10 to 13 million of 20 million), no ordering, no happens-before.
4. **Saying an acquire "flushes the cache" or "makes the write visible now".** Caches are coherent already. Acquire and release restrict *reordering*, and the guarantee is conditional: *if* the acquire load reads the release store's value, *then* the earlier writes are visible.
5. **Passing `acquire` to a store or `release` to a load.** Not meaningful. Acquire belongs to loads, release to stores, `acq_rel` to read-modify-writes.
6. **Getting the costs backwards.** Relaxed is free everywhere. Acquire and release are free on x86 (plain `mov`) and one instruction (`ldar`, `stlr`) on Arm. On x86 the expensive operation is the `seq_cst` store (`xchg`, about 5.5 ns), not the release store (0.3 ns).
7. **Confusing coherence with consistency.** Coherence: one location, one agreed sequence of values. Consistency: the order in which stores to different locations may be seen.
8. **Using `compare_exchange_weak` outside a loop, or `strong` inside one on an LL/SC machine** (the nested retry wastes work).
9. **Forgetting ABA and memory reclamation** in a lock-free structure, and testing with a pool that never reuses a node.
10. **Treating a passing x86 stress test as proof.** It cannot show a missing release or acquire; run on an Arm machine, under TSAN, or through a model checker.
11. **Using `memory_order_consume`.** Every compiler treats it as `acquire`.
12. **Using `seq_cst` for a mutex.** Lock needs acquire, unlock needs release; nothing more.
13. **Believing "it is a plain `int`, it is aligned, so it is atomic enough".** The compiler may keep it in a register (the infinite loop of 5.4), merge the accesses, or turn the whole loop into one instruction. A data race is undefined behaviour.
14. **Expecting transactional memory to make locking free.** Capacity and associativity limits, aborts on system calls, the need for a correct fallback path, and measured costs of 14 ns for an empty transaction (section 7.4) are part of the deal.

---

## 10. Questions you will be asked

**What is a memory model, and why is the hardware's not enough?** The contract that says which outcomes of a concurrent program are allowed. There are two: the hardware's (x86-TSO, Armv8, RISC-V RVWMO, POWER) and the language's (C++11 and later). Your code must be correct under the language model, because the compiler reorders too, and the compiler then emits the right instructions for the target's hardware model.

**What is sequential consistency, and how do real machines differ?** All operations appear in one global order that respects each thread's program order. x86-TSO relaxes one thing: a load may complete before an earlier store to a different address, because the store waits in the store buffer. Arm, RISC-V and POWER also reorder loads with loads, loads with later stores and stores with stores. POWER also lets two readers disagree about the order of two independent writes.

**Why can both threads read zero in the store-buffering test, even on x86?** Each thread's store sits in its store buffer while its load reads the cache. Neither store has reached the other core yet, so both loads return the old value. On the VM it happened in 75 to 84% of the iterations. Only a full barrier between the store and the load (`mfence`, a `lock`-prefixed instruction, an `xchg`, or `seq_cst` operations) removes it: 0 in 15 million.

**Explain acquire and release.** A release store orders everything before it (in program order) ahead of the store; an acquire load orders everything after it behind the load. If the acquire load reads the value the release store wrote, the two *synchronise*: everything the writer did before the release is visible to everything the reader does after the acquire. It is a constraint on ordering, conditional on the read, and not a cache flush.

**Why does message passing work with plain stores on x86 but break on Arm?** x86 never reorders store with store or load with load, so the data store is visible before the flag store and the consumer's two loads happen in order. Arm may reorder either side, so the consumer can see the flag set and still read old data. The fix is a release on the flag store and an acquire on the flag load, which cost nothing on x86 and an `stlr` and an `ldar` on Arm.

**What do the memory orders compile to?** On x86 only the `seq_cst` store (`xchg`) and the read-modify-writes (`lock` prefix) cost anything; relaxed, acquire and release are plain `mov`. Arm: `ldr`/`str` for relaxed, `ldar` for acquire and `seq_cst` loads, `stlr` for release and `seq_cst` stores, `dmb` for fences. RISC-V puts `fence` instructions around plain accesses, and POWER uses `lwsync` for release and `sync` for `seq_cst`.

**When do you need `seq_cst`?** When a thread stores to one location and then loads another, and another thread does the reverse, and the program is wrong if both loads see the old values: Dekker and Peterson, lost wake-ups, hazard pointers. Otherwise acquire and release are enough.

**Is `volatile` enough for a flag between threads?** No. It is not atomic for read-modify-write, it gives no ordering between threads, and it does not create happens-before. It is for device registers and signal flags. Use `std::atomic`, with `relaxed` if the flag guards nothing else; it compiles to the same `mov`.

**How are atomic read-modify-writes implemented?** x86: the `lock` prefix (or `xchg`), which holds the cache line until the instruction ends and is a full barrier. Arm, POWER, RISC-V: load-linked/store-conditional loops (the store fails if another core touched the line), and Armv8.1 LSE adds single-instruction atomics (`ldadd`, `cas`, `swp`) that can run near the data and scale better under contention. RISC-V also has `amo` instructions.

**What are `compare_exchange_weak` and `_strong`?** Both try to swap in a new value if the location still holds the expected one. `weak` may fail even when the value matches, because an LL/SC store-conditional can fail spuriously; use it in a retry loop, where it is cheaper. `strong` hides spurious failures; use it when you do not loop. On x86 they are identical.

**What is the ABA problem, and how do you fix it?** A CAS cannot tell "unchanged" from "changed and changed back", so a stale pointer that has been freed and reused can pass the check and corrupt the structure. Fixes: tag the pointer with a counter and CAS the pair (double-width CAS), reclaim memory safely (hazard pointers, epochs, RCU) so a node in use is never reused, or avoid the structure. LL/SC by itself would catch it, but a C++ CAS loop does not hold the reservation across the whole operation.

**What is lock elision, and why is it not everywhere?** The hardware runs the critical section as a transaction that only reads the lock, and commits if no other core touched the same cache lines. Threads on disjoint data run in parallel (7 to 8 times faster than the lock on the VM). But there is no progress guarantee (capacity, associativity, interrupts, system calls all abort), a correct fallback is mandatory, aborts are hard to debug, and Intel's TSX had errata and a security issue (TAA), so many systems keep it off.

**How would you validate a lock-free queue?** ThreadSanitizer for data races, stress tests on the weakest hardware you ship to (an Arm server), a model checker or litmus tool on the core primitive (`herd7`, CppMem, CDSChecker), a review of every `relaxed`, and a test that deliberately reuses nodes to expose ABA. "Careful code review" alone is not an answer, and neither is "it passed on my laptop".

---

## 11. Problems

**How to use this section.** Try each problem before you read the answers. A to C are paper problems (25 to 30 minutes each); D is a program on your own machine. Section 3 and the table in 3.4 give A, section 4 gives B, section 6.5 gives C, and D is a copy of the experiment of section 2.3.

### Problem A (paper): which outcomes are allowed?

All variables start at 0. All accesses are plain loads and stores (no atomics, no fences). `r1` and `r2` are registers.

| Test | Thread 0 | Thread 1 | The outcome to judge |
|---|---|---|---|
| 1 | `x = 1; r1 = y;` | `y = 1; r2 = x;` | `r1 == 0` and `r2 == 0` |
| 2 | `data = 1; flag = 1;` | `r1 = flag; r2 = data;` | `r1 == 1` and `r2 == 0` |
| 3 | `r1 = y; x = 1;` | `r2 = x; y = 1;` | `r1 == 1` and `r2 == 1` |
| 4 | `x = 1; y = 2;` | `y = 1; x = 2;` | after both threads finish: `x == 1` and `y == 1` |

(a) For each test say whether the outcome is allowed under SC, under x86-TSO and under Armv8. Name the reordering that would produce it.
(b) Now make the accesses atomics: in test 2 the flag store is `release` and the flag load `acquire`; in test 1 every store is `release` and every load `acquire`; in test 3 the `x` and `y` stores are `release`; in test 4 the second store of each thread (`y = 2`, `x = 2`) is `release`. Which outcomes become forbidden on Arm?
(c) Which of the four outcomes can you provoke on the x86 VM, and what does that say about testing?

*Where:* paper; the table in 3.4 and the four reorderings of 3.1. *How:* for each outcome, try to build one global order of the four operations that respects both program orders (SC). If you cannot, ask which program-order pair the hardware would have to break, and whether the model breaks it.

### Problem B (paper): put the barriers in

**(1) A seqlock.** One writer updates two values; readers copy them without taking a lock and retry if the writer was active. The data are atomics so that reading them while they change is not a data race. Every access below is `relaxed`.

```cpp
std::atomic<unsigned> seq{0};
std::atomic<int> a{0}, b{0};

void write(int x, int y) {                        // one writer thread
    unsigned s = seq.load(std::memory_order_relaxed);
    seq.store(s + 1, std::memory_order_relaxed);  // (1) odd: update in progress
    a.store(x, std::memory_order_relaxed);        // (2)
    b.store(y, std::memory_order_relaxed);        // (3)
    seq.store(s + 2, std::memory_order_relaxed);  // (4) even: update finished
}

bool read(int& x, int& y) {                       // any number of readers
    unsigned s1 = seq.load(std::memory_order_relaxed);   // (5)
    if (s1 & 1) return false;                     // writer active
    x = a.load(std::memory_order_relaxed);        // (6)
    y = b.load(std::memory_order_relaxed);        // (7)
    unsigned s2 = seq.load(std::memory_order_relaxed);   // (8)
    return s1 == s2;                              // true: x and y are a consistent pair
}
```

(a) Say which pairs of operations must not be reordered, and what each reordering would let a reader accept.
(b) Add the minimum ordering (memory orders on operations, or fences) and say what it compiles to on x86 and on Arm.

**(2) A lost wake-up.** A consumer sleeps when there is nothing to do; a producer wakes it.

```cpp
std::atomic<int>  items{0};
std::atomic<bool> waiting{false};

// consumer                                           // producer
waiting.store(true, std::memory_order_relaxed);       items.store(1, std::memory_order_relaxed);
if (items.load(std::memory_order_relaxed) == 0)       if (waiting.load(std::memory_order_relaxed))
    sleep_until_woken();                                  wake_consumer();
```

(c) Describe the execution in which the consumer sleeps and is never woken. Is it possible on x86? (d) Fix it, and say what the fix costs on x86.

*Where:* paper. *How:* section 4, in particular the method of 4.5 and the StoreLoad rule of 4.4. For the seqlock, think of the reader as "check the sequence number, read the data, check it again": what must the hardware not do to the order of those three steps, and what must the writer guarantee about the order of its four stores?

### Problem C (paper plus a compile): the ABA problem

A lock-free stack (the `push` and `pop` of section 6.5). It currently holds, top first, `X → Y → Z`.

Thread 1 starts `pop()`: it reads `h = X` and `h->next = Y`, and is descheduled before its CAS. Thread 2 then runs: `pop()` (returns `X`), `push(W)` (a new node `W`), and `push(X)` (the same node `X` again).

(a) Draw the stack just before Thread 1 resumes. Does Thread 1's CAS succeed? What does the stack hold afterwards, and what has been lost?
(b) Repair it with a tagged pointer: the head becomes a pair (pointer, counter) and every successful `push` and `pop` increments the counter. What pair did Thread 1 read, what pair is the head when it resumes, and what happens to its CAS?
(c) Thread 2's updates run at 2 million per second. How long before a 16-bit counter wraps completely round? A 32-bit counter? What does that mean for a descheduled thread?
(d) On your machine: compile the program below with `g++ -O2 -std=c++17 tp.cpp -latomic -o tp`, then again with `-mcx16`, and run it. Then look at the assembly (`g++ -O2 -std=c++17 -mcx16 -S tp.cpp`) and search for `cmpxchg16b`. What do you see?

```cpp
// tp.cpp - is a 16-byte (pointer, counter) pair a real atomic?
#include <atomic>
#include <cstdint>
#include <cstdio>
struct Node;
struct Tagged { Node* p; std::uintptr_t tag; };
std::atomic<Tagged> top;
bool cas(Tagged& e, Tagged d) { return top.compare_exchange_weak(e, d, std::memory_order_acquire, std::memory_order_acquire); }
int main() { std::printf("lock free: %d\n", (int)top.is_lock_free()); }
```

*Where:* paper for (a) to (c); a terminal for (d). *How:* section 6.5. For (c) divide the size of the counter by the update rate.

### Problem D (your machine): measure the store-buffer reordering

Compile and run this program (Linux: `g++ -O2 -std=c++17 -pthread litmus.cpp -o litmus`; macOS: `clang++ -O2 -std=c++17 litmus.cpp -o litmus`). It runs two tests a million times each and counts the interesting outcomes. It needs about 270 MB of memory and a few seconds per line.

```cpp
// litmus.cpp - two classic tests of the memory model, run many times.
#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>
using std::memory_order;
constexpr int N = 1'000'000;
struct alignas(128) Cell { std::atomic<int> v{0}; };      // each cell on its own line (128 covers Apple)

static std::vector<Cell> X(N), Y(N);
static std::vector<int>  R1(N), R2(N);
static std::atomic<int>  arrived{0};

static void sync_threads(int i) {                          // both threads leave when both have arrived at round i
    arrived.fetch_add(1);
    while (arrived.load() < 2 * (i + 1)) { }
}

// Store buffering:  T0: x=1; r1=y     T1: y=1; r2=x      "r1==0 && r2==0" is the interesting outcome
template <memory_order St, memory_order Ld>
long store_buffering() {
    for (int i = 0; i < N; ++i) { X[i].v = 0; Y[i].v = 0; }
    arrived = 0;
    std::thread t0([] { for (int i = 0; i < N; ++i) { sync_threads(i); X[i].v.store(1, St); R1[i] = Y[i].v.load(Ld); } });
    std::thread t1([] { for (int i = 0; i < N; ++i) { sync_threads(i); Y[i].v.store(1, St); R2[i] = X[i].v.load(Ld); } });
    t0.join(); t1.join();
    long c = 0; for (int i = 0; i < N; ++i) c += (R1[i] == 0 && R2[i] == 0);
    return c;
}

// Message passing:  T0: data=1; flag=1     T1: r1=flag; r2=data     "r1==1 && r2==0" is the interesting outcome
template <memory_order St, memory_order Ld>
long message_passing() {
    for (int i = 0; i < N; ++i) { X[i].v = 0; Y[i].v = 0; }
    arrived = 0;
    std::thread t0([] { for (int i = 0; i < N; ++i) { sync_threads(i); X[i].v.store(1, std::memory_order_relaxed); Y[i].v.store(1, St); } });
    std::thread t1([] { for (int i = 0; i < N; ++i) { sync_threads(i); R1[i] = Y[i].v.load(Ld); R2[i] = X[i].v.load(std::memory_order_relaxed); } });
    t0.join(); t1.join();
    long c = 0; for (int i = 0; i < N; ++i) c += (R1[i] == 1 && R2[i] == 0);
    return c;
}

int main() {
    std::printf("store buffering, relaxed        : %ld of %d\n", store_buffering<std::memory_order_relaxed, std::memory_order_relaxed>(), N);
    std::printf("store buffering, release/acquire: %ld of %d\n", store_buffering<std::memory_order_release, std::memory_order_acquire>(), N);
    std::printf("store buffering, seq_cst        : %ld of %d\n", store_buffering<std::memory_order_seq_cst, std::memory_order_seq_cst>(), N);
    std::printf("message passing, relaxed        : %ld of %d\n", message_passing<std::memory_order_relaxed, std::memory_order_relaxed>(), N);
    std::printf("message passing, release/acquire: %ld of %d\n", message_passing<std::memory_order_release, std::memory_order_acquire>(), N);
}
```

(a) Before you run it, predict each of the five lines on your machine (zero, rare, or common).
(b) Run it three times and record the counts. Why do they vary from run to run?
(c) Compile with `-S` and find the instruction that implements the `seq_cst` store. How does it differ from the release store?
(d) Add a fourth store-buffering variant: relaxed store, then `std::atomic_thread_fence(F)`, then relaxed load, for `F` equal to `seq_cst`, `release` and `acq_rel`. Which fences remove the outcome?
(e) If you have an Arm machine (an Apple-silicon Mac, an AWS Graviton instance), run the program there and compare the message-passing lines.

*Where:* a terminal. *How:* section 2. Use a quiet machine and look at all three runs. A zero proves nothing about a relaxed variant: it says only that you did not see the outcome.

---

## Answers

### Problem A

(a)

| Test | Outcome | SC | x86-TSO | Armv8 | Reordering needed |
|---|---|---|---|---|---|
| 1 (store buffering) | `r1 == 0`, `r2 == 0` | no | **yes** | yes | each thread's load before its own earlier store (store→load) |
| 2 (message passing) | `r1 == 1`, `r2 == 0` | no | no | **yes** | the two stores out of order (store→store), or the two loads (load→load) |
| 3 (load buffering) | `r1 == 1`, `r2 == 1` | no | no | **yes** | each thread's store before its own earlier load (load→store) |
| 4 (2+2W) | `x == 1`, `y == 1` | no | no | **yes** | a thread's two stores out of order (store→store) |

SC check. Test 1: take the later of the two stores; the load that follows it in its own thread runs after both stores and reads a 1, so (0, 0) cannot happen. For tests 2 to 4 the outcome would need a cycle in the "must come before" relation. Test 2: the flag store comes before the flag load (it supplied the 1), which comes before the data load (program order), which comes before the data store (it read 0), which comes before the flag store (program order). Test 3: T1's `y = 1` comes before T0's load of `y`, which comes before T0's `x = 1`, which comes before T1's load of `x`, which comes before T1's `y = 1`. Test 4: `x = 2` (T1) must come before `x = 1` (T0) for `x` to end as 1; `x = 1` comes before `y = 2` in T0; `y = 2` must come before `y = 1` (T1) for `y` to end as 1; and `y = 1` comes before `x = 2` in T1. Each is a cycle, so no single order exists. TSO keeps all of load→load, load→store and store→store, so it forbids 2, 3 and 4 and allows only 1. Arm allows all four. (Test 3 is allowed by the Arm architecture but is hard to observe on real cores.)

(b) Test 2 with release on the flag store and acquire on the flag load: **forbidden** (that is the contract). Test 1 with release stores and acquire loads: **still allowed**, on Arm and on x86; the store→load pair is not ordered by release/acquire (this is the measurement of 2.3). Test 3 with release stores: **forbidden**; a release store cannot move before the earlier load, so the thread that sees `1` cannot have done its store first. (Test 4 with release stores is also forbidden for the same reason: each `y = 2` or `x = 2` store is release and keeps the earlier store before it.)

(c) Only test 1. On the VM it appeared in 75 to 84% of the iterations with relaxed and with release/acquire; tests 2 and 3 gave 0 of 15 million (I did not run test 4, which TSO forbids), even though the consumer in test 2 saw the flag already set about 1.1 million times. The test tells you x86 will not show the other three, and a program that depends on them being impossible passes every x86 test and still breaks on Arm.

### Problem B

**(1) Seqlock.**

(a) Writer: (1) must become visible before (2) and (3), so that a reader which sees any new data also sees an odd or changed sequence number (a store→store order); (4) must become visible after (2) and (3), or a reader could see the even number while `b` is still old. Reader: (5) must come before (6) and (7), or the reader could read the data and only then the "before" sequence number; and (8) must come after (6) and (7), or the check would be done before the data are read. Any of these reorderings lets a reader see `s1 == s2` (even) and still hold a half-updated pair.

(b)

```cpp
void write(int x, int y) {
    unsigned s = seq.load(std::memory_order_relaxed);
    seq.store(s + 1, std::memory_order_relaxed);                   // (1)
    std::atomic_thread_fence(std::memory_order_release);           // keeps (1) before (2) and (3)
    a.store(x, std::memory_order_relaxed);                         // (2)
    b.store(y, std::memory_order_relaxed);                         // (3)
    seq.store(s + 2, std::memory_order_release);                   // (4) keeps (2) and (3) before it
}

bool read(int& x, int& y) {
    unsigned s1 = seq.load(std::memory_order_acquire);             // (5) keeps (6) and (7) after it
    if (s1 & 1) return false;
    x = a.load(std::memory_order_relaxed);                         // (6)
    y = b.load(std::memory_order_relaxed);                         // (7)
    std::atomic_thread_fence(std::memory_order_acquire);           // keeps (6) and (7) before (8)
    unsigned s2 = seq.load(std::memory_order_relaxed);             // (8)
    return s1 == s2;
}
```

On x86 none of the four orderings produces an instruction (only a compiler barrier). On Arm (section 5.3): the writer's release fence is `dmb ish`, (4) is `stlr`; the reader's (5) is `ldar` and the acquire fence is `dmb ishld`. GCC 13 for Arm64 produces exactly that: in the writer `str`, `dmb ish`, `str`, `str`, `stlr`; in the reader `ldar`, two `ldr`, `dmb ishld`, `ldr`. The release *fence* is needed (and not a release store at (1)), because a release store orders the operations *before* it, while here the store must stay before the operations *after* it. The data are atomics only to avoid a data race; the kernel's own seqlocks use plain data and its own barriers.

**(2) Lost wake-up.**

(c) The consumer stores `waiting = true` and loads `items`; the producer stores `items = 1` and loads `waiting`. This is store buffering. Each store can sit in its store buffer while the other thread's load reads the old value: the consumer reads `items == 0` and goes to sleep, the producer reads `waiting == false` and does not wake it. It is possible on x86, because it is the one reordering x86 allows.

(d) A full barrier between the store and the load on each side:

```cpp
// consumer                                              // producer
waiting.store(true, std::memory_order_relaxed);          items.store(1, std::memory_order_relaxed);
std::atomic_thread_fence(std::memory_order_seq_cst);     std::atomic_thread_fence(std::memory_order_seq_cst);
if (items.load(std::memory_order_relaxed) == 0)          if (waiting.load(std::memory_order_relaxed))
    sleep_until_woken();                                     wake_consumer();
```

(or `seq_cst` on all four operations). Now at least one of the two loads sees the other thread's store, so either the consumer does not sleep or the producer wakes it. On x86 each fence costs about 8 ns (GCC's `lock or`) to 15 ns (`mfence`) on this VM; in real code the producer's fence is the one on the fast path. Futex-based locks avoid a separate fence by keeping the "someone is waiting" state in the same word as the lock, changing it with one atomic read-modify-write (a full barrier on x86), and letting the kernel re-check that word before it puts the thread to sleep.

### Problem C

(a) Before Thread 1 resumes the stack is `X → W → Y → Z` (`X->next` is now `W`). Thread 1's CAS asks "is `head` still `X`?" and the answer is yes, so it **succeeds** and stores `Y` into `head`. The stack is now `Y → Z`, and `X` is returned. **Node `W` is lost**: nothing points to it any more, so whatever work it held is never done (a leak, and a lost item). With a freed `Y` instead the result would be a corrupt stack.

(b) Thread 1 read the pair (`X`, *t*). Thread 2 made three successful updates (`pop`, `push(W)`, `push(X)`), so the head is (`X`, *t* + 3). The pointers match but the counters do not, so Thread 1's CAS **fails**; it reloads the head (`X`, *t* + 3) and retries with `X->next == W`.

(c) 16 bits: 65,536 / 2,000,000 per second = **32.8 ms**. 32 bits: 4,294,967,296 / 2,000,000 = **2,147 s**, about 36 minutes. A stale (pointer, counter) pair can fool a CAS only if the counter wrapped completely round while the thread was descheduled and the pointer happens to be the same again. With 16 bits a thread descheduled for more than about 33 ms at this rate has that exposure; with 32 bits it needs a delay of half an hour, and a 64-bit counter never wraps in practice. That is why the counter should be as wide as the double-width CAS allows.

(d) On the test machine (GCC 13.3 and clang 18, libstdc++): the program does not link without `-latomic`, because the compare-and-swap on the 16-byte struct is a call to `__atomic_compare_exchange_16` (GCC) or `__atomic_compare_exchange` (clang); with `-latomic` it prints `lock free: 0`, **with and without `-mcx16`**; the assembly with `-mcx16` contains the call and no `cmpxchg16b`. So a `std::atomic` of a 16-byte struct is not a hardware atomic with these toolchains. What does work is the builtin on a 128-bit integer: `__sync_bool_compare_and_swap` on an `unsigned __int128` compiled to one `lock cmpxchg16b` with `-mcx16` in GCC 13, and clang 18 inlined both the `__sync` and the `__atomic` forms. On Arm64 GCC 13 with `-march=armv8.1-a` the `__sync` form became `caspal`. Other compilers and library versions may differ, so check the assembly, as you did here.

### Problem D

(a) Prediction: store buffering with relaxed and with release/acquire: **common** on x86; with `seq_cst`: **zero**. Message passing, relaxed or release/acquire: **zero** on x86. On an Arm machine, relaxed message passing can be non-zero.

(b) On the test VM (two separate cores), three runs:

| Line | Count per million | Share |
|---|---|---|
| store buffering, relaxed | 15,859 to 89,223 | 1.6 to 8.9% |
| store buffering, release/acquire | 20,469 to 103,595 | 2.0 to 10.4% |
| store buffering, `seq_cst` | 0 | 0% |
| message passing, relaxed | 0 | 0% |
| message passing, release/acquire | 0 | 0% |

The counts vary by about a factor of five between runs because the outcome needs both stores to be still in their buffers while both loads execute, a window of the order of a hundred nanoseconds. How often the two threads happen to reach it depends on how closely they start together (this program uses a shared counter and a spin loop, less exact than the cycle-counter start of section 2.3, which gave 75 to 84%), on the scheduler, and on what else the (virtual) machine is doing.

(c) The `seq_cst` store compiles to `xchgl` (it appears in the instance for the two `seq_cst` lambdas), where the release store is a plain `movl`. `xchg` with a memory operand is implicitly locked, so it drains the store buffer: this is why the outcome disappears, and why the store costs about 5.5 ns instead of 0.3 ns.

(d) Measured: `fence(seq_cst)` gave **0** in all three runs; `fence(release)` gave 46,206 to 126,806 and `fence(acq_rel)` gave 53,173 to 111,415 per million, as common as with no fence at all. A release or acquire fence does not order a store before a later load. Only the full fence does.

(e) I could not run it on Arm here, so what follows is what the architecture allows, not a measurement. Store buffering is allowed on every model, so expect it there too. Relaxed message passing is *allowed* to show `flag == 1` with `data == 0`; the rate on real cores varies and can be low, so run it several times and look for a non-zero count on the "message passing, relaxed" line. The release/acquire message passing must stay at zero on any machine; a non-zero count there would be a compiler or hardware bug.

---

## Cheat sheet

- **Coherence vs consistency:** coherence = one agreed order of values per location (tutorial 19); consistency = the order in which accesses to different locations may be seen. A memory model defines which outcomes are allowed, at two levels (hardware and language); the compiler reorders too.
- **SC:** one global order consistent with every thread's program order. **x86-TSO:** SC plus a store buffer, so only **store→load** may be reordered (measured: store buffering gave `(0, 0)` in 75 to 84% of the iterations with relaxed or release/acquire, 0 in 15 million with `mfence`, `lock or` or `seq_cst`). **Armv8, RISC-V RVWMO, POWER:** all four reorderings; POWER is also not multi-copy atomic. Dependencies (address, data, control to a store) are respected.
- **Litmus tests:** SB (store→load): allowed on TSO and weaker. MP (store→store or load→load), LB (load→store), 2+2W: allowed on Arm, RISC-V and POWER only. IRIW (readers disagree on write order): POWER only.
- **Acquire** (on a load): later accesses stay after it. **Release** (on a store): earlier accesses stay before it. If the acquire reads the release's value, the writer's earlier writes are visible to the reader's later accesses. They give LoadLoad, LoadStore, StoreStore but **not StoreLoad**; that needs `seq_cst` or a full fence between the store and the load (Dekker, Peterson, lost wake-ups, hazard pointers).
- **Costs, x86:** relaxed, acquire, release: a plain `mov` (store 0.2 to 0.4 ns). `seq_cst` store = `xchg` 5.3 to 5.7 ns; store plus `lock or` 7.8 to 8.2 ns; store plus `mfence` 14.3 to 15.0 ns. **Arm:** `ldar`, `stlr`, `dmb ish` (full), `dmb ishld`; RISC-V `fence`; POWER `lwsync` (all but StoreLoad) and `sync` (full).
- **C++ orders:** relaxed (atomicity only: counters), acquire, release, acq_rel (RMW), seq_cst (default; one total order), consume (do not use). Start with `seq_cst`, weaken one site at a time with a reason. A mutex needs only acquire and release. Data race = undefined behaviour.
- **`volatile`:** not atomic (two threads, 20 million increments: 10.2 to 12.9 million survived) and no ordering between threads. A plain `while (!flag)` compiled to an infinite loop (the load was hoisted); a `volatile` or relaxed atomic flag is re-read. Use `std::atomic`; relaxed is the same `mov`. A plain `long` counter gave the right total only because GCC folded the loop into one add, which is still a data race.
- **Atomic RMW:** x86 `lock` prefix (5.6 to 6.0 ns uncontended, full barrier); `lock xadd`, `xchg`, `cmpxchg` only; `fetch_or` with the result used is a CAS loop. Arm, POWER, RISC-V: LL/SC loops (`ldxr`/`stxr`, `lwarx`/`stwcx.`, `lr`/`sc`), spurious failures, keep loops short. Arm LSE: `ldadd`, `ldset`, `swp`, `cas`, `casp`, suffix `a`/`l`/`al`; outline atomics pick at run time. RISC-V `amo*`, `amocas` only with Zacas.
- **CAS:** `compare_exchange_weak` in a loop (can fail spuriously on LL/SC), `strong` when not looping; identical on x86. **ABA:** tag the pointer with a counter (double-width CAS: `cmpxchg16b`, `casp`; `std::atomic` of a 16-byte struct was not lock-free with GCC 13 or clang 18, but `__sync_bool_compare_and_swap` on `__int128` was), or reclaim safely (hazard pointers, epochs, RCU), or avoid the structure. A 16-bit tag wraps in 33 ms at 2 million updates per second.
- **Transactional memory (Intel RTM):** `xbegin`/`xend`/`xabort`/`xtest`; read and write sets tracked per cache line; conflict, capacity (write set limited by L1 size and associativity: 12 lines in one set on this CPU), system calls and interrupts abort; empty transaction 13.8 to 14.0 ns. **Lock elision:** read the lock inside the transaction, fall back to the real lock; measured 7 to 8 times faster than the lock on disjoint data, but 1.7 to 2.5 times slower than an atomic add on a shared counter. Errata and TAA (CVE-2019-11135); POWER10 dropped HTM (I believe); Arm TME has little or no silicon; glibc elision is opt-in.
- **Validation:** ThreadSanitizer, stress on the weakest hardware you ship to, `herd7`/litmus7, CppMem, CDSChecker; never "it passed on x86".

**Next:** tutorial 21, SIMD and vector processing (SSE, AVX and AVX-512, Arm NEON and SVE, RISC-V vectors; lane widths, alignment, masks and gathers; auto-vectorisation and intrinsics; why vector code is often limited by memory and by clock reductions). Say "next" when you are ready.
