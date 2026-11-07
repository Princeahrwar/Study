# CO 19: Cache Coherence

*Part of the [Computer Organization plan](00_plan.md) · Core tutorial · Time: about 5 hours (2.5 reading, 2.5 problems) · You need: [tutorial 13](13_cache_basics.md) and [tutorial 14](14_cache_optimisation_and_the_real_hierarchy.md) (write-back caches, the four Cs) and [tutorial 18](18_multicore_and_multithreading.md) (shared memory, threads, Amdahl's law)*

By the end of this tutorial you can:

- explain the coherence problem, state what coherence guarantees (and what it does not: that is consistency, [tutorial 20](20_memory_consistency_and_atomics.md)), and say why the unit is the cache line;
- write down the MSI and MESI states and transitions, trace a sequence of reads and writes by three cores by hand, and count the bus transactions;
- explain what MOESI and MESIF add, why write-invalidate beats write-update, and how snoop filters and directories replace broadcast;
- compute the storage overhead of a directory and describe a three-hop miss;
- separate true sharing from false sharing, recognise the usual offenders, and fix them with padding, alignment and per-thread data;
- say what an atomic operation costs alone and under contention, and why a single contended counter stops scaling;
- compare test-and-set, test-and-test-and-set, backoff, ticket and MCS locks, and sense-reversing and tree barriers, by the cache-line traffic each one causes;
- use `perf c2c` and its HITM counts to find a sharing problem, and know what changes on Arm.

Tutorial 18 ended with a warning: two threads on one shared counter ran 2.2 to 2.5 times *slower* than one. This tutorial explains why. Each core has private L1 and L2 caches (tutorials 13 and 14), so the same memory can sit in several caches at once, and the hardware must keep those copies from disagreeing. It does, and a correct program never notices. A fast program notices all the time: the hardware does it by **moving cache lines between cores**, each move costs about as much as a DRAM access, and almost every multi-threaded slowdown in practice is a count of those moves.

**Where the numbers come from.** The state traces, directory sizes and lock-traffic counts are exact; I checked the traces with a small private model. Timings marked *measured* were taken on the cloud virtual machine of tutorials 14 to 18 (an Intel Xeon under KVM, two virtual CPUs; here a core-to-core hand-off measured 75 to 102 ns, which shows the two CPUs are separate cores). Cloud numbers drift between runs, so measured figures are ranges over three runs. The VM has no performance-counter access, so `perf c2c` (section 8) is described and not shown.

> **The one-sentence model.** A cache line may be written by only one cache at a time; when another core wants it, the line moves (tens to a hundred nanoseconds) and the old owner loses it; so sharing cost is the number of hand-offs, and the way to scale is to make the cores stop passing lines around.

---

## 1. The coherence problem

### 1.1 A stale copy

Two cores share a variable `X`, initially 0 in memory. The caches are write-back (tutorial 13), so a store changes only the writer's cache:

| Step | Core 0's cache | Core 1's cache | Memory |
|---|---|---|---|
| start | (empty) | (empty) | X = 0 |
| core 0 reads X | X = 0 | (empty) | X = 0 |
| core 1 reads X | X = 0 | X = 0 | X = 0 |
| core 0 writes X = 1 | **X = 1** | X = 0 (stale) | X = 0 (stale) |
| core 1 reads X | X = 1 | **returns 0** | X = 0 |

Core 1 reads a value that was overwritten before it asked. A program that waits for a flag, reads a counter or takes a lock would be wrong. The hardware **coherence protocol** exists so that this never happens.

### 1.2 What coherence guarantees

For each memory location (in practice, each line):

1. **Single writer, multiple readers (SWMR).** At any moment a line is either writable by exactly one cache, or readable by any number of caches, never both.
2. **Data-value invariant.** A read returns the value of the most recent write to that location, and all cores see the writes to one location in the same order.

The hardware does not know what a variable is, so the unit is the **cache line**: **64 bytes** on x86 and most Arm server cores (`getconf LEVEL1_DCACHE_LINESIZE` prints 64 on this VM), and **128 bytes** as reported by Apple's M-series cores. Two unrelated variables in one line are, to the protocol, the same object (section 4).

### 1.3 Coherence is not consistency

Coherence concerns *one location*. It says nothing about the **order in which writes to different locations** become visible. Take a producer that writes `data = 42` and then `flag = 1`, and a consumer that waits for `flag == 1` and then reads `data`. The machine can be perfectly coherent and still, on a weakly ordered core, let another core see `flag = 1` while `data` still reads the old value, because the two stores left the store buffer or travelled through the interconnect in a different order. That is the subject of [tutorial 20](20_memory_consistency_and_atomics.md) (memory consistency, fences, acquire and release). Coherence gives each variable a single agreed value; consistency gives the program an agreed order between variables. Candidates mix them up constantly.

### 1.4 Two ways to get coherence

- **Software coherence:** nothing is automatic; software flushes and invalidates caches by hand. It survives for DMA buffers on non-coherent paths (tutorial 14) and for some accelerator memory.
- **Hardware coherence:** the caches talk to each other. Two designs: **snooping** (broadcast every request to the others, section 2) and **directories** (ask a record where the line is, section 3). Real chips use both ideas together.

---

## 2. Snooping protocols

### 2.1 Invalidate or update

When core 0 writes a shared line the other copies must not stay stale. Two options:

- **Write-update:** broadcast the new value to every sharer. Simple, but a core that writes a variable 1,000 times in a loop sends 1,000 broadcasts, and the other cores may never read it.
- **Write-invalidate:** send one message that makes every other copy invalid. The writer now holds the only copy and further writes are free. The cost is one message per *hand-off*, not per write.

Writes come in bursts to the same line, so invalidation is far cheaper, and every current CPU uses it.

### 2.2 State and messages

Each line in each cache carries a **state**, and each cache controller **snoops**: it watches the requests of the other caches on the shared interconnect and reacts. The messages used below:

| Message | Meaning |
|---|---|
| `BusRd` | "I miss on a read; give me a copy" |
| `BusRdX` | "I miss on a write; give me the line and invalidate all other copies" (read for ownership) |
| `BusUpgr` | "I hold the line in Shared and want to write; invalidate the others" (no data moves) |
| flush | the owner of a dirty line supplies it (to the requester, and in MSI and MESI also to memory) |

### 2.3 MSI

Three states: **M**odified (the only copy, dirty), **S**hared (clean; others may have it), **I**nvalid.

| State | Local read | Local write | Another core reads (`BusRd` seen) | Another core writes (`BusRdX` or `BusUpgr` seen) |
|---|---|---|---|---|
| **I** | `BusRd`, go to S | `BusRdX`, go to M | nothing | nothing |
| **S** | hit | `BusUpgr`, go to M | stay S | go to I |
| **M** | hit | hit | flush, go to S | flush, go to I |

### 2.4 MESI: the Exclusive state

In MSI a core that reads a line nobody else has and then writes it pays twice: `BusRd` to load, `BusUpgr` to write, although no other core was involved. **MESI** adds the **E**xclusive state: *clean, and the only copy*. On a read miss, if no other cache holds the line, it is loaded as E rather than S. A write to an E line becomes M with **no message at all**.

| State | Meaning | Dirty? | Other copies? |
|---|---|---|---|
| **M** modified | I own it and changed it | yes | none |
| **E** exclusive | I own it, unchanged | no | none |
| **S** shared | a clean copy | no | maybe |
| **I** invalid | stale or absent | | |

| State | Local read | Local write | Another core reads | Another core writes |
|---|---|---|---|---|
| **I** | `BusRd`; go to E if no other cache has the line, else S | `BusRdX`, go to M | nothing | nothing |
| **E** | hit | **silent**, go to M | go to S | go to I |
| **S** | hit | `BusUpgr`, go to M | stay S | go to I |
| **M** | hit | hit | flush, go to S | flush, go to I |

The line to remember is **E to M is free**. A write to a line you already own exclusively costs nothing; a write to a line others hold costs a transaction and waits for the invalidations; and a read of a line that is Modified in another core's cache needs a cache-to-cache transfer. That gap is the cost of sharing.

### 2.5 Worked trace

Three cores access one line `X`, initially in no cache. The state letters are the states of core 0, core 1 and core 2, in that order (MESI):

| Step | Access | States after | Bus action |
|---|---|---|---|
| 1 | C0 reads | E I I | `BusRd` (no sharer, so E) |
| 2 | C0 writes | M I I | none (E to M is silent) |
| 3 | C1 reads | S S I | `BusRd`; C0 flushes (writes back) and drops to S |
| 4 | C1 writes | I M I | `BusUpgr`; C0 invalidated |
| 5 | C2 reads | I S S | `BusRd`; C1 flushes and drops to S |
| 6 | C0 reads | S S S | `BusRd`; the line is clean, so a sharer or memory supplies it |
| 7 | C0 writes | M I I | `BusUpgr`; C1 and C2 invalidated |
| 8 | C0 writes | M I I | none (already M) |

**Six bus transactions** (steps 1 and 3 to 7). In MSI step 1 loads the line as S (`BusRd`) and step 2 needs a `BusUpgr`, so MSI needs **seven**; the Exclusive state saved one transaction. Memory was written twice (the flushes in steps 3 and 5).

### 2.6 MOESI and MESIF

| Protocol | New state | What it fixes | Used by |
|---|---|---|---|
| **MOESI** | **O**wned: dirty *and* shared | when a core reads a line that is Modified elsewhere, the owner keeps the dirty line, supplies it directly and does **not** write it back to memory; one cache stays responsible for the dirty data | AMD; Arm's AMBA protocols have equivalent states (UniqueClean, UniqueDirty, SharedClean, SharedDirty, Invalid) |
| **MESIF** | **F**orward: the one sharer designated to answer | when several caches hold a line in S and another core misses, exactly one sharer replies instead of all of them | Intel (introduced with the QPI-era designs) |

In the trace above, MOESI makes steps 3 and 5 cache-to-cache transfers (C0 and then C1 become Owned) and the line is not written to memory at all during the eight steps, against two writes in MESI. The transaction count stays 6.

### 2.7 Snoop filters and the shared L3

Broadcasting every miss to every core does not scale: with *N* cores each miss is seen by *N* − 1 others, so the snoop traffic grows about as *N*². A **snoop filter** at the shared L3 (or at the home agent of a mesh stop) remembers which cores' private caches may hold each line, so a miss is forwarded only to those. With an **inclusive** L3 (tutorial 14) the L3 tags themselves are the filter; non-inclusive L3s keep a separate filter structure. The consequence is that inside a modern chip coherence already looks like a directory (section 3), not like a bus.

---

## 3. Directory protocols

### 3.1 The idea

Each line has a **home**: the node (a memory controller and L3 slice) that owns its address. The home keeps a **directory entry** for the line: its state and who has it. A miss goes point-to-point to the home; the home sends invalidations or forwards the request **only to the actual sharers or owner**, and they answer. No broadcast, so the traffic stays roughly proportional to the amount of real sharing. The price is the directory's storage and an extra step in the path.

### 3.2 Entry formats and their cost

| Format | Entry holds | Overhead per 64-byte (512-bit) line |
|---|---|---|
| **Full bit vector** | one presence bit per core, plus state | *N* / 512: 16 cores 3.1%, 64 cores **12.5%**, 256 cores **50%** |
| **Limited pointers** | *k* pointers of log₂ *N* bits; on overflow, broadcast or fall back | 4 pointers on 256 cores: 4 × 8 = 32 bits = 6.3% |
| **Coarse vector** | one bit per group of cores | shrinks the vector by the group size; some needless invalidations |
| **Sparse directory** | entries only for lines that are cached somewhere, sized to the total cache capacity, not to memory | small, but an eviction from the directory must invalidate the line |

### 3.3 A three-hop miss

A read miss to a line that is dirty in another core's cache:

```text
snooping:   requester ──broadcast──▶ owner ──data──▶ requester          2 steps
directory:  requester ──▶ home ──forward──▶ owner ──data──▶ requester   3 steps (the home also learns the new sharer)
```

The extra hop is the price of not broadcasting. The three kinds of message (request to the home, forward to the owner, response) travel in separate virtual networks so that the protocol cannot deadlock ([tutorial 29](29_interconnection_networks_and_warehouse_scale_computing.md) section 5.4). On this VM a dirty line moving from one core to the other costs one hand-off of **75 to 102 ns** (section 4.2), about the same as a DRAM access (120 to 135 ns, tutorial 16): **reading data that another core just wrote is as slow as reading main memory**.

### 3.4 Snooping or directory?

| | Snooping | Directory |
|---|---|---|
| Latency of a miss to a remote dirty line | lowest (2 steps) | one more step |
| Traffic | every miss reaches every cache (or a filter) | only the sharers |
| Needs | a broadcast medium or ordered interconnect | per-line storage, a home for every address |
| Scales to | tens of cores, with filters | many cores and many sockets |

Real machines are hybrids: snooping or filters inside a cluster of cores that share an L3, directories between the clusters and between sockets. That is why a single socket can show cheap core-to-core transfers inside one group and slower ones across groups (chiplets, tutorial 18 section 4.4).

---

## 4. True sharing and false sharing

### 4.1 Two kinds

Every miss caused by another core's invalidation is a **coherence miss** (the fourth C of [tutorial 14](14_cache_optimisation_and_the_real_hierarchy.md): compulsory, capacity, conflict, coherence). There are two reasons it happens:

- **True sharing:** the cores really use the same data: a counter, a flag, a lock, a queue index. The communication is part of the algorithm; the only fix is to communicate less (batch, combine per thread, restructure).
- **False sharing:** the cores use *different* variables that happen to sit in the **same cache line**. Nothing needs to be communicated, yet each write by one core invalidates the other's copy. The fix is to move the data apart.

Typical places where false sharing hides:

| Where | Why it happens |
|---|---|
| `long partial[NTHREADS]`, `stats[tid].count` | per-thread slots, 8 bytes each: eight threads share one line |
| two fields of one struct written by different threads | the producer's `tail` and the consumer's `head` side by side |
| a lock next to the data it protects | taking the lock invalidates the neighbours' copies, and a spinner reads the line the holder is writing |
| neighbouring globals or small heap objects | the linker and `malloc` pack them (`malloc` aligns to 16 bytes, not 64) |
| a read-mostly field next to a hot written field | the readers miss every time the writer writes |
| arrays of small elements split among threads at unaligned boundaries | only the boundary line is shared, but it is shared |

### 4.2 What a hand-off costs

*Measured:* two threads, one on each of the two CPUs, take turns on one flag: each waits until the flag says "my turn", then flips it. Each turn moves one cache line from one core to the other. The one-way time was **75 to 102 ns** (round trip 150 to 205 ns, three runs). The DRAM latency measured on the same machine was 120 to 135 ns ([tutorial 16](16_dram_memory_controllers_and_numa.md)). **Getting a line that another core just wrote costs about as much as going to main memory.** An L1 hit is about 1 to 1.5 ns.

That one number explains most of this tutorial: a coherence miss costs roughly 50 to 100 times an L1 hit, so a loop that takes a coherence miss on every iteration can run that many times slower than one that does not, unless something else (the store buffer, a long instruction chain) hides part of it.

### 4.3 Measuring false sharing, and a trap

Each of two threads updates *its own* 8-byte variable, 20 to 500 million times. In one run the two variables are 8 bytes apart (same line), in the other 128 bytes apart (different lines, no sharing). The table gives how many times slower the same-line run was. *Measured*, three runs each:

| Loop body (per thread) | Same line vs 128 B apart |
|---|---|
| `*c = i;` (stores only) | 0.9 to 1.0 times (no slowdown) |
| `(*c)++;` on a `volatile` | 1.0 to 1.2 times |
| unlocked `add [mem], 1` | 1.2 to 1.3 times |
| `(*c)++;` and a store to a private line | **6.2 to 6.3 times** |
| `(*c)++;` and `pv[i & 1023]++` on a private array | **4.4 to 6.9 times** |
| `(*c)++;` then `mfence` | **4.9 to 5.8 times** |
| `lock add` (an atomic increment) | **7.3 to 7.8 times** |
| private work, shared `++` once every 8 iterations | 1.6 to 1.8 times |

My first test was the first row, and it showed **nothing**; I almost concluded that the two CPUs shared a core. They do not (the hand-off above, and the last rows, show two separate cores). What the first rows show is that on this CPU a loop of *back-to-back stores to the same line* hides false sharing: the core holds the line while it drains a long run of stores to it, so the other core's request wins only now and then. I believe the reason is the **store buffer**, which merges and commits consecutive stores to one line together, but I could not confirm it (the VM gives no access to performance counters). What is certain is that **as soon as the loop mixes the shared line with other work that needs its own line to be written in order (a store to another line, a fence, an atomic), the full penalty appears.**

The practical rule: **do not conclude from a microbenchmark that two variables can share a line.** Real code has other stores, atomics and fences between the updates. Use the atomic or interleaved-store version of the test, and use `perf c2c` (section 8) on the real program.

Also *measured*: padding to 64 bytes (an adjacent line) was as good as 128 bytes (`lock add`, two threads: 0.118 to 0.128 s against 0.120 to 0.141 s; the same line took 0.81 to 0.97 s).

### 4.4 Fixing it

In order of preference:

1. **Keep the data per thread, and merge at the end.** Accumulate in a local variable or in thread-local storage and write the shared slot once. No line is shared while the work runs and no padding is needed.

   ```cpp
   // slow: every iteration writes a slot that shares its line with the other threads' slots
   void worker(int tid) { for (long x : my_chunk) partial[tid] += f(x); }

   // fast: the running sum lives in a register; the shared slot is written once
   void worker(int tid) { long local = 0; for (long x : my_chunk) local += f(x); partial[tid] = local; }
   ```

   (A compiler can sometimes do this itself, but it cannot when `f` may touch `partial` or when the slot is atomic or volatile; do not depend on it.)

2. **Pad and align** what must stay shared and written: give each hot object its own line.

   ```cpp
   struct alignas(64) PerThread { std::atomic<long> n{0}; };   // sizeof == 64
   PerThread slots[8];                                          // slot i never shares a line with slot i+1
   ```

   `alignas(64)` makes the *size* a multiple of 64 as well, so the array elements stay aligned. For heap objects use `std::aligned_alloc(64, size)` or, from C++17, `new` of an over-aligned type. In C++17 the constant `std::hardware_destructive_interference_size` (in `<new>`) names the right distance, but it is fixed when you compile, for the compile target, and compilers warn when a header uses it; for a structure that crosses a library boundary, choose 64 (or 128) yourself.
3. **Split hot from cold, written from read-only.** Put the fields one thread writes on one line, the fields everyone only reads on another. The Linux kernel does this by hand with `____cacheline_aligned_in_smp`, and Java has `@Contended`. A queue shows the idea: keep the consumer's `head` and the producer's `tail` on separate lines, the constant fields (capacity, mask, buffer pointer) on a third, and let each side keep a private copy of the other's index, refreshed only when the queue looks full or empty, so the index lines move rarely instead of once per item (Problem B).
4. **Reduce the write rate.** Batch updates, or write the shared flag only when its value changes.

**Do not pad everything.** A padded counter uses 64 bytes for 8 bytes of data. A thousand padded counters are 64 KB, which no longer fits in a 32 to 48 KB L1 ([tutorial 13](13_cache_basics.md)); pad the few objects that threads really write concurrently.

**Which size?** 64 bytes is the line on x86 and most Arm server cores. Apple's M-series report 128 bytes (`sysctl hw.cachelinesize`). Some Intel cores also fetch the neighbouring line along with a miss (the adjacent-line prefetcher, [tutorial 14](14_cache_optimisation_and_the_real_hierarchy.md)), which is why some widely used libraries (for example Rust's `crossbeam-utils`, with its `CachePadded` type) pad to 128 bytes on x86. On this VM 64 was enough; 128 costs only memory.

---

## 5. The cost of an atomic

### 5.1 Alone: a fixed price

An atomic read-modify-write must read the line, modify it and write it back with no other core able to intervene. On x86 the `lock` prefix does that: the core takes the line in the M state, holds it until the operation completes, and also **drains the store buffer**, so every locked instruction is a full memory barrier (tutorial 20). That is a fixed cost even with no other core in sight. *Measured*, one thread, 100 million operations on one variable:

| Operation | Time per operation |
|---|---|
| plain `++` (load, add, store) | 0.27 to 0.38 ns (about one cycle) |
| `fetch_add`, relaxed (`lock xadd`) | **5.6 to 5.9 ns** (about 18 to 20 cycles) |
| `fetch_add`, seq_cst | 5.8 to 6.0 ns (the same instruction) |
| `compare_exchange` loop (load + `lock cmpxchg`) | 9.8 to 10.1 ns |
| `store`, release (a plain `mov` on x86) | 0.23 to 0.28 ns |
| `store`, seq_cst (an `xchg` on x86) | 5.4 to 5.8 ns |
| `store` followed by `mfence` | 14.9 to 15.3 ns |

Three lessons: an uncontended atomic is **about 20 times a plain increment** (cheap in absolute terms, expensive in a hot loop of one nanosecond per item); on x86 the memory-order argument changes nothing for read-modify-write (always locked) but a **seq_cst store costs about 20 times a release store**, which is the first place tutorial 20 matters; and a `compare_exchange` loop costs nearly twice a `fetch_add` even when it never fails.

### 5.2 With company: the line moves

Now two threads each do 20 million `lock add`. If each has its own line, the run takes 0.12 s, the same as alone (6 ns per operation). If both use the same line, each thread takes **0.81 to 0.97 s**, which is 40 to 49 ns per operation, **7 to 8 times slower**, because the line has to travel between the cores (hand-off 75 to 102 ns, section 4.2) and the atomic cannot start until the line has arrived. The pair together does 40 million operations in 0.81 to 0.97 s, which is 41 to 50 million per second, **about 4 times less than one thread alone** (160 to 175 million per second): adding a second thread made the counter slower in total, which is the result of tutorial 18. (That tutorial measured a factor of 2.2 to 2.5 for the same total work in another session, so the exact factor drifts with the state and placement of the VM; the shape does not.)

### 5.3 CAS loops and LL/SC

A `compare_exchange` loop reads the old value, computes the new one, and tries to swap; if another core changed the value in between, the swap fails and the loop retries. Under contention failed attempts waste line transfers, so a CAS loop scales worse than a `fetch_add` that always succeeds. Use the dedicated atomic (`fetch_add`, `fetch_or`, `exchange`) when one exists.

Arm cores originally had no read-modify-write instruction. They used **load-linked / store-conditional** (`ldxr` / `stxr`): the load marks the line, and the store succeeds only if no other core touched the line since. Under contention the store-conditional fails often and the loop spins. Armv8.1 added single-instruction atomics (the **LSE** set: `ldadd`, `cas`, `swp`), which some implementations can execute where the data lives (in the shared cache or the interconnect), so the line need not move to the requesting core; recent compilers on AArch64 Linux choose between the two at run time (outline atomics). The effect is large on many-core Arm servers, and a reason to use `fetch_add` and not hand-written loops.

**Misaligned atomics are a disaster:** a locked operation that spans two cache lines (a *split lock*) forces the processor to lock the whole memory system, costs thousands of cycles and can slow other cores and tenants; Linux can detect it (`split_lock_detect`). Keep atomic variables naturally aligned.

### 5.4 Counters: one shared, or one per core

| Design | Increment | Read | Scales? |
|---|---|---|---|
| one shared atomic | `fetch_add` on the shared line (6 ns alone, 40 to 49 ns with company) | one load | no: the total stays at tens of millions per second (41 to 50 million with 2 threads here) however many threads are added |
| one atomic per thread, each on its own line | `fetch_add` on a private line, 6 ns | sum all slots | yes |
| one plain per-thread counter, single writer | `slot.store(slot.load(relaxed) + 1, relaxed)` compiles to a plain add: 0.3 ns | sum all slots | yes, and cheapest |

The sum of the slots is a **fuzzy snapshot**: each slot is exact at some moment but not all at the same moment, which is fine for statistics and wrong for "stop when the total reaches a limit". When you need an exact bound, give each thread a **budget** (a batch of 1,000 grabbed from the shared total) so the shared line is touched once per thousand operations. This is how the Linux kernel's per-CPU counters work, and how a sharded request counter in a service works.

---

## 6. Locks and the lines they move

A lock is a shared variable that every contender reads and writes. How a lock behaves under contention is mostly a question of **how many cache lines move per hand-over**.

### 6.1 Test-and-set, test-and-test-and-set, backoff

**Test-and-set (TAS):** spin on `exchange(1)` until it returns 0. Every attempt is a write, so every waiter keeps stealing the line in M state from the holder and from each other; the holder's own release store has to win the line back. With *N* waiters the lock's line is under continuous attack even while the holder works.

**Test-and-test-and-set (TTAS):** spin on an ordinary **load** (the line sits in S state in every waiter's cache, so the spinning is local and silent) and try the `exchange` only when the lock looks free:

```cpp
void lock()   { for (;;) { while (flag.load(std::memory_order_relaxed)) cpu_relax();          // spin on a read
                           if (!flag.exchange(1, std::memory_order_acquire)) return; } }       // then try once
void unlock() { flag.store(0, std::memory_order_release); }
```

While the lock is held there is no traffic. On release the store invalidates all *N* copies, all *N* waiters re-read, and all *N* try the `exchange`: a **thundering herd**, *O*(*N*) transfers per hand-over, of which *N* − 1 attempts fail.

**Backoff:** a waiter whose attempt failed pauses for a growing time before trying again, so fewer attempts collide. It cuts the traffic but needs tuning, and a pause that is too long leaves the lock idle.

### 6.2 Ticket lock

Take a number, wait until it is called:

```cpp
void lock()   { unsigned my = next.fetch_add(1, std::memory_order_relaxed);              // one atomic per acquire
                while (serving.load(std::memory_order_acquire) != my) cpu_relax(); }       // wait for my number
void unlock() { serving.store(serving.load(std::memory_order_relaxed) + 1, std::memory_order_release); }
```

It is **fair** (first come, first served) and each acquire is one atomic with no failed attempts, but all waiters still spin on the same `serving` line, so each release invalidates *N* copies and *N* waiters re-read it: *O*(*N*) transfers per hand-over. Linux used ticket locks until kernel 4.2.

### 6.3 MCS lock: one line, one core

In an **MCS lock** (Mellor-Crummey and Scott) each waiter spins on a flag in **its own queue node**, on its own cache line, and the releaser writes only its successor's flag:

```cpp
struct alignas(64) QNode { std::atomic<QNode*> next{nullptr}; std::atomic<bool> locked{false}; };
struct MCSLock {
    std::atomic<QNode*> tail{nullptr};
    void lock(QNode& me) {                                    // 'me' lives on the caller's stack
        me.next.store(nullptr, std::memory_order_relaxed);
        me.locked.store(true, std::memory_order_relaxed);
        QNode* prev = tail.exchange(&me, std::memory_order_acq_rel);        // the only shared atomic
        if (prev) {                                                          // someone holds it: queue up
            prev->next.store(&me, std::memory_order_release);
            while (me.locked.load(std::memory_order_acquire)) cpu_relax();   // spin on MY line
        }
    }
    void unlock(QNode& me) {
        QNode* succ = me.next.load(std::memory_order_acquire);
        if (!succ) {                                                         // nobody visible behind me
            QNode* expected = &me;
            if (tail.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel)) return;
            while (!(succ = me.next.load(std::memory_order_acquire))) cpu_relax();   // a waiter is still linking in
        }
        succ->locked.store(false, std::memory_order_release);                // hand over: one line to one core
    }
};
```

(The memory orders are the subject of [tutorial 20](20_memory_consistency_and_atomics.md); read them as "acquire when taking the lock, release when handing it over".) Per hand-over exactly **one** line moves, to exactly one core, however many are waiting: *O*(1). The price is the queue node and a slightly slower uncontended path. The Linux kernel's `qspinlock` is an MCS lock squeezed into 4 bytes, with the queue nodes kept per CPU. I tested the code above for correctness with 2 and 4 threads; with 2 threads on the 2 CPUs and a one-increment critical section it ran at 3.2 to 3.5 million acquisitions per second.

| Lock | While held | Lines moved per hand-over, *N* waiters | Fair? |
|---|---|---|---|
| TAS | every waiter keeps writing the line | continuous, grows with *N* | no |
| TTAS | silent (spin on a shared copy) | about *N* reads and *N* attempts | no |
| ticket | silent | *N* re-reads, 1 winner, no failed attempts | yes |
| MCS | silent (each spins on its own line) | **1** | yes |

### 6.4 What the VM showed

Million acquisitions per second, one lock, 0.3 s per point, three runs (so the ranges include run-to-run noise). *T* threads on the **2 CPUs** of the VM, so *T* = 4 is oversubscribed. "Work 0" is a critical section of one increment; "work 200" adds about 100 ns of work inside the lock.

| Lock | Work 0: *T* = 1 | *T* = 2 | *T* = 4 | Work 200: *T* = 1 | *T* = 2 | *T* = 4 |
|---|---|---|---|---|---|---|
| TAS | 119 to 123 | 4.9 to 29 | 1.2 to 1.8 | 10.0 to 10.3 | 0.2 to 4.2 | 0.1 to 0.8 |
| TTAS | 120 to 123 | 4.9 to 7.5 | 0.7 to 2.0 | 9.9 to 10.5 | 1.0 to 1.8 | 0.3 to 0.5 |
| TTAS + backoff | 119 to 122 | 5.3 to 8.8 | 0.5 to 2.0 | 9.9 to 10.5 | 1.2 to 2.6 | 0.1 to 0.5 |
| ticket | 116 to 128 | 1.6 to 2.1 | below 0.01 | 10.0 to 10.4 | 1.0 to 1.3 | below 0.01 |
| `pthread_mutex` | 60 to 63 | 4.9 to 8.7 | 2.4 to 3.0 | 9.7 to 10.0 | 1.7 to 1.9 | 0.8 to 0.9 |

Read the shape, not the ranking:

- **One thread to two is a collapse, not a gain.** With a one-increment critical section, the rate falls from about 120 million per second to 5 to 9 million for TTAS and backoff (a factor of 14 to 25) and to 2 million for the ticket lock. Each acquisition now starts with the line in the other core's cache.
- **A critical section is serial.** With 100 ns of work inside the lock, two threads get 1 to 2.6 million per second (TAS: 0.2 to 4.2): *less* than the single thread's 10 million, because the work is serialised (Amdahl's law with *s* = 1) and the hand-overs are added to it.
- **Oversubscription kills the spinning locks.** With 4 threads on 2 CPUs the holder (or, for the ticket lock, the *next* ticket holder) is often not running, and the others spin through their whole time slice. The ticket lock fell below 5,000 acquisitions per second. An MCS lock test with 4 threads on the 2 CPUs did 8,000 acquisitions in 2 ms in one run and 20,000 in 15.7 s (1,300 per second) in another: a queue lock must hand over to *exactly* the next waiter, and if that thread is asleep everyone waits. The mutex, which puts waiters to sleep in the kernel (a *futex*), degrades gracefully and was the best at *T* = 4.
- **Fairness and throughput differ.** TAS at *T* = 2 ranged from 5 to 29 million, probably because a thread that has just released the lock can take it again before the other core's request arrives, so one thread runs for long stretches (high throughput, the other starves). I could not count acquisitions per thread to confirm it.
- **A mutex costs two atomics, a spin lock one** (60 million per second against 120): lock and unlock of `pthread_mutex` each use a locked instruction, while a spin lock's release is a plain store.

### 6.5 Choosing and avoiding

- **In user space use a mutex** (it sleeps) or an adaptive one (spin briefly, then sleep). Spin locks belong where the holder cannot be preempted: inside the kernel with preemption disabled, or on dedicated, pinned cores.
- **Make the critical section short and the lock rarely contended**: shard the lock (one per bucket, as a concurrent hash table does), or make the data per-core.
- **A reader-writer lock is not free for readers:** taking a read lock is an atomic increment of a shared reader count, so many readers on many cores bounce that line. Read-mostly data does better with **RCU**, **seqlocks** or **hazard pointers**, where readers write nothing shared.
- On a big machine the lock's line is the bottleneck long before the CPU is: each hand-over costs tens of nanoseconds, so a contended lock is capped at roughly 10 million acquisitions per second (5 to 9 million measured here), whatever the core count.

---

## 7. Barriers

A **barrier** makes every thread wait until all have arrived: the end of each phase of a simulation, a parallel loop, a time step. Its cost is again cache-line traffic.

**Centralised sense-reversing barrier.** One shared counter; the last thread to arrive resets it and flips a shared **sense** flag; the others spin on that flag. The sense flips on every use, so the barrier can be reused at once without a second barrier to reset it:

```cpp
struct SenseBarrier {
    const int n;
    alignas(64) std::atomic<int>  count{0};
    alignas(64) std::atomic<bool> sense{false};
    explicit SenseBarrier(int n) : n(n) {}
    void wait(bool& local_sense) {                       // 'local_sense' is each thread's own variable, initially false
        local_sense = !local_sense;
        if (count.fetch_add(1, std::memory_order_acq_rel) == n - 1) {
            count.store(0, std::memory_order_relaxed);                   // last arriver: reset ...
            sense.store(local_sense, std::memory_order_release);         // ... and release everyone
        } else {
            while (sense.load(std::memory_order_acquire) != local_sense) cpu_relax();
        }
    }
};
```

The *N* `fetch_add`s on one line are **serial**, one hand-off each, and the flag release invalidates *N* − 1 copies: the cost grows linearly with *N*. With 64 threads and 90 ns per hand-off the arrival alone takes about 5.8 µs.

**Tree (combining) barrier.** Group the threads (say 4 per group); the last arriver in each group goes up to the next level, and the last at the top releases the way down. Each counter is touched by only 4 threads, and there are log₄ *N* levels. For *N* = 64: 3 levels of 4 serial arrivals, 12 hand-offs (about 1.1 µs) instead of 64. A **dissemination barrier** needs log₂ *N* rounds in which every thread signals one partner; no thread is "last", and it is popular on distributed machines.

**Spin or sleep?** *Measured*, 2 threads on the 2 CPUs: the sense-reversing barrier took **250 to 370 ns** per barrier (a few line hand-offs), `pthread_barrier_wait` took **12.7 to 13.3 µs**, 35 to 53 times longer, because it puts waiters to sleep in the kernel and wakes them with a system call and a scheduler decision. The spinning barrier is only safe with **one thread per core**: if the threads outnumber the cores, a spinner occupies the CPU that the thread it waits for needs. OpenMP runtimes therefore spin for a while and then sleep (`OMP_WAIT_POLICY`, `GOMP_SPINCOUNT`). I verified the barrier code above with 100,000 phases and a check that no thread ran ahead.

The practical consequence for parallel loops: **the barrier cost per phase sets the smallest useful phase.** For 90% efficiency a phase needs 9 times the barrier cost in work per thread: with a 13 µs barrier about 120 µs, with a 300 ns spin barrier about 3 µs.

---

## 8. Finding it on a real machine

Symptoms: threads added, time not reduced (or increased); high CPU use with low IPC; the time is in a few memory-touching functions; the profile shows nothing algorithmic.

**`perf c2c`** ("cache to cache") finds lines that bounce between cores. It samples loads and stores with their data addresses (using the processor's precise memory events: PEBS on Intel, and in recent versions IBS on AMD and SPE on Arm) and reports the lines on which loads **hit a Modified line in another core's cache**, which `perf` calls **HITM** (hit modified):

```text
perf c2c record -- ./myprogram          # or:  perf c2c record -a -- sleep 10
perf c2c report --stdio                 # tables of the hottest shared cache lines
```

What to read in the report:

1. **Shared Data Cache Line Table:** the lines sorted by **HITM**, local (same socket) and remote (other socket). A line at the top with thousands of HITMs is your hot line.
2. **For each line, the offsets within the line** that were accessed, how many loads and stores each took, and by which threads (cpu count) and functions (symbols). **Different threads on the same offset = true sharing** (a counter, a lock). **Different threads on different offsets = false sharing**: move the data apart.
3. Match the offset to a field with `pahole -C structname ./binary` (needs `-g`), which prints each field's offset and marks the cache-line boundaries.

The VM used for this tutorial gives no access to performance counters, so I cannot show a `perf c2c` report here; on a bare-metal Linux machine or a cloud instance that exposes the PMU (some "metal" or dedicated-host types) it works. Without it you can still test the hypothesis: pad the suspect structure and measure again (Problem D), or use `perf stat` to compare cache misses and cycles with and without threads.

---

## 9. Other machines and devices

- **Arm and others.** The same ideas hold with different names: Arm's CHI and ACE protocols define states equivalent to MOESI (Unique/Shared, Clean/Dirty); server meshes use a snoop filter or directory at the home node; the line is 64 bytes on Neoverse and Cortex-A cores and reported as 128 on Apple's. Arm's weaker memory model (tutorial 20) makes *ordering* the more important difference than coherence.
- **Non-coherent devices.** A DMA engine that does not snoop the CPU caches reads stale memory unless software first **cleans** (writes back) the buffer, and after the device writes it software must **invalidate** the CPU's copy. x86 PCIe DMA is coherent; many Arm SoC peripherals are not. Drivers use the DMA API (`dma_sync_*`) for exactly this ([tutorial 14](14_cache_optimisation_and_the_real_hierarchy.md), and [tutorial 26](26_interrupts_exceptions_and_dma.md) on DMA).
- **Coherence between chips.** CXL.cache, NVLink-C2C (the link between a Grace CPU and a Hopper or Blackwell GPU) and similar links extend the coherence domain to accelerators, so a CPU and a GPU can share pointers; each hop adds latency like the cross-socket directory step of section 3.3. GPUs inside themselves use much weaker coherence ([tutorial 22](22_gpu_execution_model.md) and [tutorial 23](23_gpu_memory_tensor_cores_and_scaling.md)).
- **Virtual machines and containers** hide the topology: you cannot tell from `lscpu` alone whether two vCPUs are two cores or two hyperthreads of one core, and the answer can differ from one session to the next. The hand-off test of section 4.2 tells you: a hand-off of 75 to 100 ns means separate cores, and siblings that share an L1 would be much faster.
---

## 10. What this means for your code

| Symptom | Likely cause | Try |
|---|---|---|
| Adding threads makes the program slower | a written cache line shared between threads: a lock, a counter, or per-thread data packed together | `perf c2c`; per-thread data merged at the end; pad what must stay shared |
| An array of per-thread counters or partial sums scales badly | false sharing (8-byte slots, eight per line) | accumulate in a local variable and write once; or `alignas(64)` slots |
| A statistics counter on the hot path costs more than the work around it | a contended atomic | per-thread (per-core) counters summed on read; batch the increments |
| One lock tops out at about 10 million acquisitions per second whatever the core count | the hand-off rate of the lock's cache line (section 6) | shard the lock, make the data per-core, shorten or batch the critical section |
| Spin locks collapse when there are more threads than cores | the holder, or the next in the queue, is not running | a mutex (sleeps), an adaptive lock, or pinned threads on dedicated cores |
| A reader-writer lock on read-mostly data does not scale | a read lock is an atomic increment of a shared counter | RCU, a seqlock, hazard pointers, or per-reader state |
| A parallel loop with many short phases is slow | the cost of a barrier per phase | fewer and larger phases; a spinning or tree barrier with one thread per core |
| A producer-consumer queue is slow | `head` and `tail` share a line, or each side reads the other's index on every item | separate lines; cache the other side's index; move items in batches |
| A refactor added a field and a hot path got slower though the instructions did not change | the new field moved a hot field into a line shared with a written one | `pahole`; `static_assert` on `offsetof` and `sizeof` for hot structures |
| Atomics on a packed or misaligned structure are very slow | split locks (the operation spans two lines) | keep atomics naturally aligned; check `-Wpacked` and `#pragma pack` |
| `seq_cst` stores are a hot spot on x86 | each is an `xchg` or a store plus a fence (5 ns against 0.25 ns) | a release store where the algorithm allows it (tutorial 20) |
| A false-sharing test shows nothing | back-to-back stores to one line are merged; the test is too gentle | use atomics, a fence or interleaved stores; run `perf c2c` on the real program |
| Data looks stale after a device wrote it (usually on Arm) | the DMA path is not coherent with the CPU caches | the DMA API: `dma_sync_*`, or coherent buffers ([tutorial 26](26_interrupts_exceptions_and_dma.md)) |

---

## 11. Mistakes that cost points

1. **Saying coherence orders operations on different variables.** Coherence gives one agreed value *per location*; the order between locations is consistency (tutorial 20).
2. **Using `volatile` for communication between threads.** It stops the compiler from caching the value in a register; it gives no atomicity and no ordering. The hardware keeps caches coherent anyway, so what you need is `std::atomic`.
3. **Thinking Exclusive means Modified.** E is clean and the only copy; M is dirty. The point of E is that the E-to-M transition is **silent**: counting a bus transaction for it is wrong.
4. **Saying a MESI cache always broadcasts.** Real chips filter with a snoop filter or use a directory; the protocol states are the same, the transport is not.
5. **Treating atomics as free** because they are lock-free. An atomic is one fixed price alone (5 to 6 ns) and one hand-off per contended operation (tens of ns).
6. **Treating all contention as a lock problem.** Two threads that never touch the same variable can still contend, through one cache line.
7. **Padding without aligning.** A `char pad[56]` after an 8-byte member gives a 64-byte struct, but if the array or the heap block starts at 16 bytes past a line boundary, every element straddles two lines. Use `alignas(64)`, which fixes size and alignment together.
8. **Padding everything,** or padding by a constant copied from another machine (64 on x86, 128 on Apple, 128 in some libraries because of prefetching).
9. **Concluding from one gentle microbenchmark that sharing is harmless** (section 4.3), or that two vCPUs share a core.
10. **Spinning in user space with more runnable threads than cores,** including in a container with a CPU quota: the holder is descheduled and everyone burns the time slice.
11. **Calling a read lock free.** It is an atomic on a shared line.
12. **Using a `compare_exchange` loop where `fetch_add` or `exchange` exists,** or forgetting that a CAS can fail under contention and retry.
13. **Using `std::hardware_destructive_interference_size` in a public header.** The value is a guess for the compile target and differs between compilers and flags.

---

## 12. Questions you will be asked

**What is the cache coherence problem?** Each core has private write-back caches, so the same memory location can be in several caches, and a store changes only the writer's copy. Without a protocol another core could keep reading the old value. Coherence guarantees one writer or many readers per line at any time (SWMR) and that a read returns the latest write to that location.

**What is the difference between coherence and consistency?** Coherence is about one location: all cores agree on the sequence of values it takes. Consistency (the memory model) is about the order in which operations on *different* locations become visible. A coherent machine can still show `flag = 1` before `data = 42` to another core.

**Write-invalidate or write-update?** Invalidate: the writer removes the other copies and then writes freely; one message per hand-off. Update: every write is broadcast. Writes come in bursts, so invalidate wins and every current CPU uses it.

**Explain MESI.** Four states per line: Modified (only copy, dirty), Exclusive (only copy, clean), Shared (clean, maybe several), Invalid. A read miss loads E if nobody else has the line, else S (and an M copy elsewhere supplies the data and drops to S). A write needs M: from E it is silent, from S it sends an upgrade that invalidates the others, from I a read-for-ownership. E exists so that private data does not pay an extra transaction for its first write.

**What do MOESI and MESIF add?** MOESI adds Owned (dirty and shared): the owner supplies the line to readers without writing it back to memory first. MESIF adds Forward: of several sharers exactly one answers a request, so the others stay quiet.

**Snooping or directory?** Snooping broadcasts every miss to all caches: lowest latency, traffic grows with the number of cores; filters help. A directory keeps, per line, who has it, and sends messages only to those: scales to many cores and sockets, costs storage and one more hop. Real machines use snoop filters inside a socket or an L3 domain and directories between them.

**What is false sharing, and how do you find and fix it?** Independent variables written by different cores sit in one line, so the line bounces between the cores although no data is shared. Signs: threads added, time not reduced; high HITM counts in `perf c2c` with different offsets in one line. Fix: keep the data per thread and merge at the end; otherwise pad and align the hot objects to the line size; separate hot written fields from read-mostly ones.

**Why can an atomic increment cost 6 ns alone and 40 ns with a second thread?** Alone it is a locked instruction on a line already in L1 (about 20 cycles, and a full barrier on x86). With a second thread on the same line the line has to travel from the other core's cache (75 to 100 ns here) before the operation can run.

**Why does one shared atomic counter not scale, and what do you do?** Every increment needs the line in M state, so the cores take turns: the throughput is about one increment per hand-off however many threads. Use one counter per thread (or core), each on its own line, and sum them on read; for an exact limit, give each thread a budget taken from the shared total in batches.

**Compare test-and-set, TTAS, ticket and MCS locks.** TAS writes on every spin and floods the line. TTAS spins on a read, quiet while held, but on release all waiters re-read and race (a thundering herd). Ticket: fair, one atomic per acquire, but all waiters still spin on one line. MCS: each waiter spins on its own line and the releaser wakes exactly one successor, so one transfer per hand-over (the basis of Linux's `qspinlock`).

**What is lock-holder preemption?** The OS deschedules the thread holding a spin lock (or the next in a queue), and the waiters spin through their time slices. User-space spin locks suffer; kernel spin locks disable preemption; user code should use a sleeping mutex or an adaptive lock.

**How does a sense-reversing barrier work, and what is better at scale?** A shared counter counts arrivals; the last arriver resets it and flips a shared sense flag that the others spin on; the flag flips on each use so the barrier can be reused at once. The *N* arrivals serialise on one line, so a tree barrier (log *N* levels, a few arrivals per node) or a dissemination barrier scales better.

**What is `perf c2c`?** A `perf` mode that samples memory accesses with data addresses and finds cache lines on which loads hit a Modified line in another core's cache (HITM). It lists the hottest lines, the offsets within them and the threads and functions that touch each offset; the same offset from several threads is true sharing, different offsets is false sharing.

**How big is a cache line, and how do you find out?** 64 bytes on x86 and most Arm cores, 128 as reported by Apple's M-series. `getconf LEVEL1_DCACHE_LINESIZE` or `lscpu` on Linux, `sysctl hw.cachelinesize` on macOS, `std::hardware_destructive_interference_size` in C++17 (a compile-time guess).

**Does a device that does DMA need cache maintenance?** On x86 PCIe DMA snoops the caches, so normally not. On many Arm SoCs the DMA path is not coherent: the driver must clean the buffer before the device reads it and invalidate it after the device writes (the DMA API does this).

**Why would adding one field to a struct make a program slower?** It can shift a hot field into a line that another thread writes (false sharing), or push a hot structure over a line boundary. Check offsets with `pahole` and guard them with `static_assert`.

---

## 13. Problems

**How to use this section.** Try each problem before you read the answers. A to C are paper problems (25 to 30 minutes each); D is paper plus a short program on your own machine.

### Problem A (paper): MESI, MSI and MOESI traces

Three cores, C0, C1, C2, and one cache line X that starts in no cache. The accesses, in order:

1. C1 reads  2. C1 writes  3. C0 reads  4. C2 reads  5. C0 writes  6. C1 reads  7. C2 writes  8. C2 reads

(a) Under MESI, give the state of X in (C0, C1, C2) after each access and the bus message used (`BusRd`, `BusRdX`, `BusUpgr`, or none).
(b) How many bus transactions does the sequence need under MSI, and how many under MESI? Which access is the difference?
(c) How many times is the line written back to memory under MESI, and under MOESI? In what state is the line at the end, and where is the up-to-date data?
(d) Classify each access as a hit, a compulsory miss, a coherence miss (the copy was invalidated by another core's write), or an upgrade (the data was present but ownership was needed).

*Where:* paper. *How:* section 2. A flush is a write-back to memory when the owner is in M (MSI, MESI); in MOESI the owner keeps the dirty line in O and supplies it to the next requester without a memory write.

### Problem B (paper): find the false sharing in a layout

A single-producer single-consumer ring buffer (1024 slots of 8 bytes):

```cpp
struct Ring {
    std::atomic<uint32_t> head;    // next slot to read;  written by the consumer only
    std::atomic<uint32_t> tail;    // next slot to write; written by the producer only
    uint32_t  capacity;            // 1024, never changes
    uint32_t  mask;                // 1023, never changes
    uint64_t* slots;               // never changes
};
```

The producer reads `head` (is there room?), writes a slot, writes `tail`. The consumer reads `tail` (is there data?), reads a slot, writes `head`.

(a) Give the offset of every field and `sizeof(Ring)`. Which cache lines does a `Ring` occupy if it starts on a 64-byte boundary? Name the pairs of fields that share a line and say, for each pair, whether the sharing is true or false.
(b) Rule: count one hand-off whenever a core needs a line that the other core has written since it last used it. In steady state the queue is neither empty nor full and each side handles one item per step. How many hand-offs per item does this layout cause for the `Ring` structure, and how many for the slot data (a line holds 8 slots)?
(c) Redesign the structure: put `head`, `tail` and the read-only fields on separate lines, and give each side a private cached copy of the other's index (`cached_tail` for the consumer, `cached_head` for the producer) that it refreshes only when the ring looks empty or full. Write the structure, give its `sizeof`, and compute the hand-offs per item if each side refreshes once per 256 items and a refresh costs 2 hand-offs. At 90 ns per hand-off, what item rate does each design allow at most?
(d) The consumer keeps up with the producer item by item, so the queue is almost always empty. What happens to the slot data, and what do you change?

*Where:* paper. *How:* section 4.4. Offsets follow the natural alignment of each type.

### Problem C (paper): counters, locks and barriers by the numbers

Use 90 ns per cache-line hand-off, 6 ns for an uncontended atomic and 0.3 ns for a plain add to an owned line.

(a) Sixteen threads each add 10 million times to a counter. Time for: one shared atomic (worst case: every increment finds the line in another core's cache, and the hand-offs are serial); one atomic per thread on its own line; one plain single-writer counter per thread. How does the first compare with one thread doing all 160 million increments alone with an atomic?
(b) A lock is released with 8 threads waiting. How many transfers of the lock's cache line does the release set off for TTAS, a ticket lock and an MCS lock, and how does the number grow with the number of waiters?
(c) With one waiter (two threads), a TTAS hand-over costs two transfers (the waiter's re-read of the lock, then its `exchange`), and an MCS hand-over costs three (the `exchange` on `tail`, the store into the predecessor's `next`, and the release writing the waiter's `locked` flag). Predict the acquisition rate of each and compare with the measurements of section 6 (TTAS 4.9 to 7.5 million per second, MCS 3.2 to 3.5).
(d) A barrier for 64 threads, arrivals only: time for the centralised counter and for a tree with 4 arrivals per node. If each phase has 50 µs of work per thread, what fraction of the time does each barrier cost?

*Where:* paper. *How:* sections 5 to 7.

### Problem D (your machine): measure, find and fix false sharing

Compile and run this program (Linux: `g++ -O2 -std=c++17 -pthread counters.cpp -o counters`; macOS: `clang++ -O2 -std=c++17 counters.cpp -o counters`). Each thread increments only its own counter.

```cpp
// counters.cpp - one event counter per thread, all in one array.
// Add -DPAD=64 (or 128) to give every counter its own line.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

#ifdef PAD
struct alignas(PAD) Counter { std::atomic<long> n{0}; };
#else
struct Counter { std::atomic<long> n{0}; };
#endif

constexpr long ITERS = 20'000'000;      // increments per thread
Counter counters[8];                    // one slot per thread

int main(int argc, char** argv) {
    int threads = argc > 1 ? std::atoi(argv[1]) : 2;
    std::printf("sizeof(Counter) = %zu bytes, %d thread(s)\n", sizeof(Counter), threads);

    auto t0 = std::chrono::steady_clock::now();
    std::vector<std::thread> pool;
    for (int id = 0; id < threads; ++id)
        pool.emplace_back([id] {
            for (long i = 0; i < ITERS; ++i)
                counters[id].n.fetch_add(1, std::memory_order_relaxed);
        });
    for (auto& t : pool) t.join();
    double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    long total = 0;
    for (int id = 0; id < threads; ++id) total += counters[id].n.load();
    std::printf("total = %ld (expected %ld), %.3f s\n", total, ITERS * threads, s);
}
```

(a) Before running: the work per thread is identical in every run. Predict how the time of `./counters 2` compares with `./counters 1`, and with the `-DPAD=64` build.
(b) Run `./counters 1`, `./counters 2` and the `-DPAD=64` build with 2 threads, three times each. Report the medians and the slowdown. On an Apple-silicon Mac also build with `-DPAD=128`; is 64 enough?
(c) Run `taskset -c 0 ./counters 2` (Linux): both threads on one CPU. Why is it fast although the counters share a line?
(d) Build with `-DPAD=32` (two counters per 64-byte line). What do you predict for threads 0 and 1? For threads 0 and 2?
(e) Fix the program without padding, in the way of section 4.4 point 1 (a local accumulator, one write at the end). How does it compare with the padded version, and what does it cost in memory? If `perf c2c` works on your machine, run it on the unpadded build and find the line.

*Where:* a terminal. *How:* section 4. Use the median of three runs; stop other programs; on a laptop keep it plugged in. `hardware_concurrency()` tells you how many threads you can use.

---

## Answers

### Problem A

(a) MESI, states in (C0, C1, C2):

| Step | Access | After | Message |
|---|---|---|---|
| 1 | C1 reads | I E I | `BusRd` (no other copy, so Exclusive) |
| 2 | C1 writes | I M I | none (E to M is silent) |
| 3 | C0 reads | S S I | `BusRd`; C1 supplies the dirty line, writes it back, drops to S |
| 4 | C2 reads | S S S | `BusRd`; the line is clean, a sharer or memory supplies it |
| 5 | C0 writes | M I I | `BusUpgr`; C1 and C2 invalidated |
| 6 | C1 reads | S S I | `BusRd`; C0 supplies the dirty line, writes it back, drops to S |
| 7 | C2 writes | I I M | `BusRdX`; C0 and C1 invalidated |
| 8 | C2 reads | I I M | none (hit in M) |

(b) MESI: steps 1, 3, 4, 5, 6, 7 = **6 transactions**. MSI has no Exclusive state: step 1 loads the line as S and step 2 needs a `BusUpgr`, so **7**. The difference is **step 2**, the write to private data.

(c) MESI writes the line back to memory at **steps 3 and 6**: **2** times. MOESI: at step 3 C1 goes to Owned (C0 gets S), at step 4 the owner supplies C2, at step 5 C0 upgrades from S and becomes the single dirty copy, at step 6 C0 goes to Owned and supplies C1, at step 7 the owner supplies C2 and is invalidated. **0** memory write-backs during the sequence. At the end the line is **Modified in C2**, and memory is stale: the data exist only in C2's cache until it is evicted (then MOESI and MESI both write once).

(d) Step 1: **compulsory miss** (C1's first access). Step 2: **hit** (permission already held, in E). Steps 3 and 4: **compulsory misses** (first access by C0 and by C2). Step 5: **upgrade** (C0 has the data in S but needs ownership). Step 6: **coherence miss** (C1's copy was invalidated by C0's write at step 5). Step 7: **coherence miss** (C2's copy was invalidated at step 5; it must fetch the line again to write it). Step 8: **hit**. Totals: 3 compulsory, 2 coherence misses, 1 upgrade, 2 hits.

### Problem B

(a) Offsets (natural alignment): `head` 0 to 3, `tail` 4 to 7, `capacity` 8 to 11, `mask` 12 to 15, `slots` 16 to 23 (a pointer needs 8-byte alignment). `sizeof(Ring)` = **24**. All of it is in **one** line (bytes 0 to 23 of a 64-byte line). Pairs: `head` and `tail` are written by different cores and sit in one line, so every write by one core takes the line from the other: **false sharing** of the writes. In addition each core *reads* the other's index (to check for room or for data): that is **true sharing**, which is needed, but it can be made rare, see (c). The read-only fields `capacity`, `mask`, `slots` are read by both cores on every operation and written by none, but they sit in the line that is written on every step: **false sharing** (each write by one core invalidates the other core's copy of data it only reads).

(b) Per step, the producer needs the `Ring` line (the consumer wrote `head` since): 1 hand-off; the consumer needs it (the producer wrote `tail`): 1 hand-off; so **2 hand-offs per item** for the structure. The slot data: the producer fills a line with 8 items and the consumer then reads it: 1 hand-off per 8 items = **0.125**. Total **2.125** hand-offs per item.

(c) One design:

```cpp
struct Ring {
    alignas(64) std::atomic<uint32_t> head;   // consumer writes
    uint32_t cached_tail;                      // consumer's copy of tail, same line as head
    alignas(64) std::atomic<uint32_t> tail;   // producer writes
    uint32_t cached_head;                      // producer's copy of head, same line as tail
    alignas(64) uint32_t capacity;             // read-only after setup, own line
    uint32_t mask;
    uint64_t* slots;
};
```

Offsets 0, 4, 64, 68, 128, 132, 136; the end is 144, rounded up to a multiple of 64: `sizeof(Ring)` = **192**. Hand-offs per item: data 0.125, plus the two refreshes: 2 sides × 2 hand-offs ÷ 256 items = 0.0156; total **0.1406**. At 90 ns: original 2.125 × 90 = 191 ns per item, at most **5.2 million items per second**; redesigned 0.1406 × 90 = 12.7 ns, at most **79 million per second**, about **15 times** more.

(d) If the consumer reads each slot right after the producer wrote it, the line that holds the slot moves **for every item** (true sharing: the consumer must get what the producer just wrote), about 1 hand-off per item (90 ns, at most 11 million items per second) instead of 1/8, and the two cores alternate on the same line. Nothing about the layout fixes that, because the data really travel. The changes are on the algorithm side: **batch** (the producer publishes `tail` after every 8 or 16 items, the consumer takes a batch), accept some latency for throughput, or give each side more work per item so the transfer is hidden. This is the throughput against latency trade-off in every queue.

### Problem C

(a) Total 16 × 10 million = 160 million increments. **Shared atomic:** 160 × 10⁶ × 90 ns = **14.4 s**. **One atomic per thread:** 10 × 10⁶ × 6 ns = **0.06 s** in parallel on 16 cores, plus 16 reads to sum (16 × 90 ns = 1.4 µs): a speedup of 14.4 / 0.06 = **240 times**. **Plain single-writer counters:** 10 × 10⁶ × 0.3 ns = **3 ms**, **4,800 times** faster than the shared atomic. One thread doing all 160 million alone: 160 × 10⁶ × 6 ns = **0.96 s**, so the 16-thread shared counter is **15 times slower than one thread**. (The worst-case assumption is pessimistic for 2 threads, where the measured cost was 22 ns per increment because a core often performs several increments before the line is taken; with 16 contenders such reuse is rarer.)

(b) **TTAS:** the releasing store invalidates the 8 copies; the 8 waiters re-read the line (8 transfers) and each tries an `exchange` (8 more, of which 7 fail): about **16**, growing as 2 *N*. **Ticket:** the release writes `serving`; the 8 waiters re-read it: **8** transfers, one winner, no failed atomics; grows as *N*. **MCS:** the releaser writes the successor's own flag: **1** transfer, independent of *N*.

(c) **TTAS:** 2 × 90 ns = 180 ns per acquisition, **5.6 million per second**, inside the measured 4.9 to 7.5. **MCS:** 3 × 90 ns = 270 ns, **3.7 million per second**, close to the measured 3.2 to 3.5. The simple model of counting hand-offs predicts both within about 15%; it is good enough to choose between designs.

(d) Centralised: 64 serial arrivals × 90 ns = **5.76 µs**. Tree: 64 = 4³, so 3 levels with 4 serial arrivals each: 12 × 90 ns = **1.08 µs**, 5.3 times less. Cost per phase with 50 µs of work: centralised 5.76 / (50 + 5.76) = **10.3%**; tree 1.08 / (50 + 1.08) = **2.1%**.

### Problem D

(a) Prediction: with `-DPAD=64` each thread has its own line, so two threads take the same time as one (the work per thread is the same and runs in parallel). With 8-byte slots both counters are in one line, every atomic needs the line from the other core, and the time should be several times longer.

(b) On the test VM (two separate cores): 1 thread **0.115 to 0.122 s**; 2 threads, 8-byte slots **0.82 to 1.04 s** (7 to 9 times slower than one thread); `-DPAD=64` **0.115 to 0.139 s**; `-DPAD=128` **0.114 to 0.117 s**. 64 was as good as 128 on this x86 machine. On an Apple-silicon Mac, expect the unpadded run to be several times slower, and test whether `-DPAD=64` is enough: if the hardware sharing granule is 128 bytes, 64 may still show part of the penalty and 128 should remove it. Run it several times, because the scheduler may place the threads on efficiency cores.

(c) With both threads on one CPU only one runs at a time, so a line never moves to another core: the run took **0.23 s**, twice the single-thread time (the two threads run one after the other, 0.115 s each) and with no coherence cost. False sharing needs two cores to run at the same moment. This is also why a test on a single-core container or a pinned process shows nothing.

(d) With 32-byte slots, slots 0 and 1 are in one 64-byte line and slots 2 and 3 in the next: threads 0 and 1 **still false-share** (measured **0.78 to 1.09 s**), while threads 0 and 2 are on different lines and should not interfere (the test VM has only two CPUs, so I could not run three threads). `sizeof(Counter)` printed as 32 is the clue: padding smaller than the line is not padding.

(e) A local accumulator, as in section 4.4: each thread keeps `long local` in a register (the compiler will normally use one `add` per iteration or fold the loop), and writes `counters[id].n.store(local)` once. It is as fast as the padded version or much faster (the atomic is gone: no 6 ns per increment), and needs no extra memory per slot; the padded version uses 64 bytes per counter for 8 bytes of data. In `perf c2c report` the unpadded build shows one line at the top with a large HITM count, two offsets (0 and 8) accessed by two different threads, which means false sharing; the padded build shows none.

---

## Cheat sheet

- **Problem and guarantee:** private write-back caches can hold stale copies. Coherence = SWMR (one writer or many readers per line) + every read returns the latest write to that location, per line (64 B on x86 and most Arm cores; Apple reports 128). Not the same as consistency (order between different locations, tutorial 20).
- **Write-invalidate** (all real CPUs) beats write-update: one message per hand-off, not per write.
- **MSI:** M (dirty, only copy), S (clean, shared), I. **MESI** adds E (clean, only copy): **E to M is silent**, which saves the upgrade for private data. **MOESI** adds O (dirty and shared; the owner supplies readers without a memory write-back; AMD, Arm-style states). **MESIF** adds F (one designated sharer answers; Intel).
- **Messages:** `BusRd` (read miss), `BusRdX` (write miss, read for ownership), `BusUpgr` (S to M, no data), flush (the owner supplies the dirty line). A read of a line that is M elsewhere: the owner supplies it and goes to S (O in MOESI).
- **Snooping:** broadcast to all caches, lowest latency, traffic about *N*²; fixed by **snoop filters** (an inclusive L3's tags). **Directory:** a home node per line tracks the sharers (bit vector *N*/512 of the line: 64 cores 12.5%, 256 cores 50%; limited pointers; coarse vectors; sparse directories); a miss costs three hops when the line is dirty elsewhere. Real chips: filters inside a cluster, directories between clusters and sockets.
- **Coherence misses (the fourth C):** true sharing (real communication: reduce it) and false sharing (different variables, one line: move them apart). A hand-off of a dirty line costs about a DRAM access (75 to 102 ns here against 120 to 135 ns for DRAM; L1 hit about 1 ns).
- **False sharing:** per-thread slots in an array, adjacent fields written by different threads, a lock next to its data, `malloc`'d neighbours. Fixes in order: per-thread data merged at the end; `alignas(64)` on hot written objects; split hot from cold; write less often. Do not pad everything (64 B per counter, L1 is 32 to 48 KB). 128 B padding on x86 because of the adjacent-line prefetcher in some libraries; Apple 128. A gentle store-only test can hide false sharing (0.9 to 1.3 times) while atomics, fences and interleaved stores show 5 to 8 times.
- **Atomics:** uncontended `lock xadd` 5.6 to 6.0 ns (about 20 cycles, a full barrier on x86), `cmpxchg` loop about 10 ns, plain add 0.3 ns, seq_cst store 5.4 to 5.8 ns against release store 0.25 ns, `mfence` 15 ns. Contended on one line: 40 to 49 ns each, 7 to 8 times slower, and the pair is 4 times slower than one thread. Misaligned atomic = split lock, very slow. Arm: LL/SC loops fail under contention; LSE (`ldadd`, `cas`) scale better.
- **Counters:** shared atomic (worst case one hand-off per increment, ~10 to 50 million per second total); per-thread slots each on its own line, sum on read (fuzzy snapshot); single-writer plain counters (0.3 ns); budgets of batch size for exact limits.
- **Locks:** TAS (write on every spin), TTAS (spin on a read, thundering herd on release, about 2 *N* transfers), backoff (tuning), ticket (fair, *N* re-reads per release), **MCS** (spin on your own line, 1 transfer per hand-over, Linux `qspinlock`). A contended lock is capped near 10 million acquisitions per second (5 to 9 measured). Spinning breaks when the holder is descheduled (4 threads on 2 CPUs: ticket below 5,000 per second); mutex (futex) degrades gracefully. A mutex costs two atomics, a spin lock one. Read locks are atomics; use RCU or seqlocks for read-mostly data.
- **Barriers:** sense-reversing centralised (*N* serial arrivals, 250 to 370 ns for 2 threads), tree or dissemination (log *N*), blocking `pthread_barrier` (12.7 to 13.3 µs for 2 threads, sleeps). Spin only with one thread per core. For 90% efficiency a phase needs 9 times the barrier cost in work.
- **Tools:** `perf c2c record` then `perf c2c report --stdio` (HITM = a load that hit a Modified line in another core; same offset from several threads = true sharing, different offsets = false), `pahole -C struct`, `getconf LEVEL1_DCACHE_LINESIZE`, `sysctl hw.cachelinesize`, `static_assert(offsetof…)`, `std::hardware_destructive_interference_size` (compile-time guess). VMs may hide the topology and the PMU; use the hand-off test.

**Next:** tutorial 20, Memory Consistency and Atomics (sequential consistency, TSO on x86 and the weaker Arm, POWER and RISC-V models, store buffers and reordering, fences, acquire and release, litmus tests, compare-and-swap and the ABA problem, the Arm LSE atomics, transactional memory, why `volatile` is not atomic). Say "next" when you are ready.
