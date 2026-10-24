# CO 14: Cache Optimisation and the Real Hierarchy

*Part of the [Computer Organization plan](00_plan.md) · Core tutorial · Time: about 5 hours (2.5 reading, 2.5 problems) · You need: [tutorial 13](13_cache_basics.md) (cache basics), and [tutorial 11](11_superscalar_and_out_of_order_execution.md) and [tutorial 12](12_inside_a_modern_core.md) (the window, MSHRs, the four bounds)*

By the end of this tutorial you can:

- classify a miss as compulsory, capacity, conflict or coherence, and say what fixes each;
- explain inclusive, exclusive and non-inclusive caches and victim caches, and what they do to capacity;
- explain non-blocking caches, MSHRs and memory-level parallelism, and measure how many misses a core overlaps;
- explain what hardware prefetchers do and when they fail, and when a software prefetch pays;
- explain why the L1 is virtually indexed and physically tagged, and the "4 KB per way" rule;
- restructure loops (interchange, blocking, tiling) and data (array of structs against struct of arrays, hot/cold splitting, line-sized nodes) to touch fewer lines;
- name the memory types (write-back, write-through, write-combining, uncached), say why device registers are uncached, and use non-temporal stores and cache-line flushes;
- explain cache partitioning (Intel CAT, Arm MPAM) and a noisy neighbour;
- find cache misses with `perf`, and with a cache simulator when the machine has no counters.

Tutorial 13 built one cache. This tutorial is about the other half of the question: your program is slow because of the cache, **what do you change?** Almost every answer is one of three moves: touch fewer lines, touch them in a better order, or overlap the misses you cannot avoid.

**Where the numbers come from.** Timings marked *measured* were taken on a cloud virtual machine, pinned to one core, with GCC 13 and `-O2 -fno-tree-vectorize` so that scalar code stays scalar. **The cloud gave a different machine from the one in tutorials 12 and 13.** This one is an Emerald Rapids Xeon (2.1 GHz nominal, about 3 to 4 GHz in these runs), with a 48 KB 12-way L1 data cache, a 2 MB 16-way L2 and a very large shared L3, and its cache labels in `/sys` agree with the measurements. A pointer chase there gives: L1 about 5 cycles (1.3 ns), L2 about 20 cycles (5 ns), L3 about 55 ns, DRAM 140 to 220 ns. The numbers are therefore not comparable with the earlier tutorials except in shape. Run-to-run noise on a shared VM is 10 to 30%, so some tables show ranges. As on most clouds, there were no hardware counters.

> **The one-sentence model.** Every cache optimisation is one of three moves: touch fewer lines, touch them in a better order, or overlap the misses you cannot avoid.

---

## 1. The three Cs, and a fourth

### 1.1 Definitions

Every miss has a cause, and the cause tells you the fix.

- **Compulsory** (cold) miss: the first access to a line. Even an infinite cache takes it.
- **Capacity** miss: the working set is larger than the cache. Defined as: a miss that is not compulsory and that a **fully associative** cache of the *same size* (with LRU) would also take.
- **Conflict** miss: a miss that the same-size fully associative cache would have avoided. The size was enough; the mapping was not (too many hot lines in one set).
- **Coherence** miss (the fourth C): another core wrote the line, so our copy was invalidated (section 1.3 and tutorial 19).

To classify, simulate three caches: an infinite one (what is compulsory), a fully associative one of the same size (what is capacity) and the real one (the rest is conflict). On a real machine you can use two shortcuts: if halving the cache size raises the misses, capacity matters; if doubling the associativity lowers them, conflict matters.

### 1.2 A worked classification

A cache of 4 lines, 2-way (2 sets, set = line number mod 2), LRU. The accesses are line numbers; the program first loops twice over five lines, then loops over three even lines:

```text
line:             0 1 2 3 4 | 0 1 2 3 4 | 0 2 4 | 0 2 4
2-way, 4 lines:   M M M M M | M H M H M | M M M | M M M       2 hits, 14 misses
fully assoc., 4:  M M M M M | M M M M M | M H H | H H H       5 hits, 11 misses
classification:   C C C C C | P h P h P | P X X | X X X
```

(`C` compulsory, `P` capacity, `X` conflict, `h` hit.) The 14 misses of the real cache are:

| Kind | Count | Where | Why |
|---|---|---|---|
| Compulsory | 5 | the first touch of lines 0 to 4 | cannot be avoided by any cache |
| Capacity | 4 | the second loop over 5 lines, and the first even line after it | five lines do not fit in four slots, and LRU evicts each line just before its reuse (tutorial 13, section 6.2) |
| Conflict | 5 | the second loop, over lines 0, 2 and 4 | lines 0, 2 and 4 all map to set 0, which has two ways; the fully associative cache holds all three |

| Kind | Typical fixes |
|---|---|
| Compulsory | prefetch; longer lines or sequential access (more use per miss); there is no way to remove the first touch |
| Capacity | tiling, so each phase fits; shrink the data (smaller types, struct of arrays, compression); a bigger cache |
| Conflict | more ways; padding or non-power-of-two strides; a victim buffer (section 2.3); spread hot data over more sets |
| Coherence | give each thread its own lines; pad hot per-thread data to 64 bytes; update shared data less often |

### 1.3 The fourth C, and a warning about the cloud

When one core writes a line, every other core's copy is invalidated, and the next read there misses. If two cores write *different words of the same line*, the line bounces between them on every write: **false sharing**. It is the commonest hidden cost of multithreaded code, and tutorial 19 measures it.

A warning for your own measurements. On this VM two threads, each incrementing its own counter 100 million times, took 0.24 s when the counters were 8 bytes apart (one line) and 0.23 s when they were 64 bytes apart. With real separate cores the first case is many times slower. So the two virtual CPUs seem to be two hyperthreads of **one physical core**, which share an L1 cache and cannot bounce lines; the guest has no way to tell. If a sharing experiment shows no effect in the cloud, check this first.

---

## 2. The real hierarchy: inclusive, exclusive, and the victim cache

### 2.1 Three ways to stack levels

```text
Inclusive:      every line in L1 is also in L2, and every line in L2 is also in L3
Exclusive:      a line is in exactly one level; L3 is filled by what L2 evicts
Non-inclusive:  no rule: a line may or may not be in both levels
```

| | Inclusive | Exclusive | Non-inclusive |
|---|---|---|---|
| Unique capacity | the size of the largest level | the sum of the levels | in between |
| Finding a line | the L3 tags say what any private cache may hold: no L3 hit means no core has it | a separate directory or snoop filter is needed | a separate snoop filter is needed |
| Cost | **back-invalidation**: when L3 evicts a line, the copies in L1 and L2 must be invalidated, even hot ones | every L2 eviction is a transfer into L3 | needs the snoop filter, but no wasted copies and no back-invalidation |

Example: 8 cores with a 1 MB L2 each and a 16 MB shared L3. Inclusive: the 8 MB of L2 data are all duplicated in the L3, so **16 MB** of unique data fit. Exclusive: 8 × 1 + 16 = **24 MB**, 50% more. This is why server chips moved away from inclusive L3 as the private L2 grew.

As reported, older Intel client chips had an inclusive L3; Intel server chips since Skylake-SP use a non-inclusive L3 that behaves mostly as a victim cache of the L2; AMD Zen's L3 is a victim cache of the L2 of its core complex. An inclusive L3 also lets a program on one core evict lines from another core's L1 by evicting them in L3, which cross-core cache attacks use (tutorial 30).

### 2.2 The victim cache

Many conflict misses come from a few lines that are evicted and wanted back soon. A **victim cache** (or victim buffer) is a tiny fully associative buffer next to the cache that holds the lines recently evicted from it. On a miss, the cache looks in the victim buffer; if the line is there, it is swapped back in, much faster than a trip to the next level.

### 2.3 The same trace with a one-entry victim buffer

Take the trace of section 1.2 and add a **single-entry** victim buffer to the 2-way cache:

```text
line:                 0 1 2 3 4 | 0 1 2 3 4 | 0 2 4 | 0 2 4
2-way:                M M M M M | M H M H M | M M M | M M M       2 hits
2-way + 1 victim:     M M M M M | V H V H V | V V V | V V V       11 hits
```

(`V` is a hit in the victim buffer.) One extra line of storage turns 9 of the 14 misses into hits, including capacity misses of the first loop, because the cache plus the buffer now hold 4 + 1 = 5 lines, the whole working set of that loop. In real designs a victim buffer of a few entries removes a good share of the conflict misses of a small direct-mapped cache.

---

## 3. Non-blocking caches, MSHRs and memory-level parallelism

### 3.1 The idea

A **blocking** cache stops everything at the first miss. A **non-blocking** (lockup-free) cache keeps serving hits and accepts further misses while earlier ones are outstanding: *hit under miss* and *miss under miss*. The structure that makes this possible is the **MSHR** (miss status holding register): one entry per outstanding **line**. An entry records which line is being fetched and which instructions wait for it. If a second access wants the same line (a *secondary miss*), it joins the existing entry and does not start another fetch. When the data arrive, all waiting accesses are woken.

The number of MSHRs (the L1 calls them *fill buffers* on Intel) bounds the **memory-level parallelism (MLP)**: how many misses are in flight at once. Typical values are 10 to 16 at the L1 and more at L2. Tutorial 11 showed that the out-of-order window finds independent loads; MSHRs decide how many of them can actually be outstanding.

### 3.2 Little's law, and a measurement

**Little's law:** average number in flight = throughput × latency. For misses: lines in flight = (lines per second) × (latency per miss).

Experiment: K independent pointer chases, advanced together through a random 1 GiB footprint (so each hop is a DRAM miss with a TLB miss, 221 ns when alone). When K is larger, the core can have K misses in flight.

| Chains K | *Measured* time per access | Speed-up over K = 1 |
|---|---|---|
| 1 | 221 ns | 1.0 |
| 2 | 118 ns | 1.9 |
| 4 | 54 ns | 4.1 |
| 8 | 28 ns | 7.9 |
| 12 | 22 ns | 10.0 |
| 16 | 19 ns | 11.5 |
| 24 | 18 ns | 12.1 |
| 32 | 19 ns | 11.8 |

The gain is almost linear up to about 10, then it **flattens at about 12 times**. By Little's law, 221 ns of latency divided by 18.4 ns per access is **12 misses in flight**. That is this core's limit for this pattern (random lines in 4 KB pages); the L1 fill buffers (10 to 16 on recent cores) and the page walkers are the structures that set it. Two consequences:

1. **Pointer chasing has MLP 1.** A linked list or a tree walk pays the full 221 ns per step, because the next address depends on the load.
2. **Independent lookups run 12 times faster, if you let them overlap.** One core with 12 misses in flight and 64-byte lines can move at most 12 × 64 B / 221 ns, about 3.5 GB/s, of random lines. Sequential streams do better, because the prefetchers use deeper queues at L2 (the array sums in section 7 reach 13 GB/s of lines).

**Software consequence:** when you have many independent lookups (packets in a batch, keys of a join), do them **together**: interleave 8 to 16 lookups, or issue the first load of each before using any result. This is the idea behind batch processing in packet frameworks (DPDK, VPP) and "group prefetching" in database joins. Interview version: "latency is fixed, so I raise the number of misses in flight."

---

## 4. Prefetching

### 4.1 Hardware prefetchers

A prefetcher watches the stream of misses and fetches lines before they are asked for. Typical units:

- **Next-line** at L1 and L2: after a miss to line L, fetch L + 1.
- **Stride detector** (per load instruction): recognises a constant stride, forward or backward.
- **Streamer** at L2 and L3: sees a run of ascending or descending lines and runs ahead within the 4 KB page.
- **Adjacent-line** at L2 (Intel cores): fetches the neighbour, so that pairs of 64-byte lines behave like a 128-byte line.

They fail in four situations: **pointer chasing** (the next address is not known), **random access**, **crossing a 4 KB page** (the next page's translation may not exist, which is a hidden argument for huge pages), and **short loops** (the detector needs several iterations to train).

*Measured*, summing 64 million 8-byte integers: sequentially, 0.98 ns per element, which is 7.8 ns per 64-byte line; by random gather (`a[idx[i]]`, every access a new line), 16 ns per access; with a regular 512-byte stride (every access a new line), 19.6 ns per access. Both one-line-per-access patterns are limited by the MLP of section 3; the sequential case is 2 to 2.5 times faster per line because the prefetchers keep more lines in flight, and 16 times faster per element because 8 elements share each line.

### 4.2 Software prefetch

```c
for (size_t i = 0; i < n; ++i) {
    __builtin_prefetch(&table[idx[i + 16]], 0 /*read*/, 0 /*use once*/);
    sum += work(table[idx[i]]);          // idx[i] was prefetched 16 iterations ago
}
```

`__builtin_prefetch(address, rw, locality)` is a hint: it does not stop the program, and a bad address is ignored (it is safe to run past the end of an array). **It only helps when the out-of-order window cannot see far enough ahead by itself.** Experiment: the random gather of section 4.1, with a loop body of 0, 96 or 576 independent ALU uops after each load, and a prefetch distance of 0 (none) up to 64 iterations:

| Prefetch distance | Body 0 uops | Body 96 uops | Body 576 uops |
|---|---|---|---|
| none | 13 ns | 51 ns | **210 ns** |
| 4 | 17 ns | 36 ns | 67 ns |
| 8 | 16 ns | 27 ns | 45 ns |
| 16 | 18 ns | 20 ns | **37 ns** |
| 64 | 20 ns | 19.5 ns | 40 ns |

*Measured* ns per iteration.

- **Body of 0 uops:** the window already holds many independent loads, so the hardware overlaps the misses (section 3); prefetch adds only overhead (13 to 20 ns).
- **Body of 96 uops:** the window (about 500 entries) holds only about 5 iterations, so the misses are only partly overlapped. Prefetching 16 iterations ahead gives 2.5 times.
- **Body of 576 uops:** the body is larger than the window, so the next iteration's load is not even issued until this one retires; each miss is paid in full (210 ns). A prefetch distance of 16 gives **5.7 times**.

**Rules.** Prefetch when the loop is long per element, or the address is known early but the load is far away in the program (a loop over `idx[]` that reads `table[idx[i]]`, the children of a tree node, the next bucket). Choose the **distance ≈ memory latency ÷ time per iteration**, rounded up (here about 200 ns / 37 ns, so 6 or more; the curve flattens by 16). Too small and the data arrive late (the distance-4 rows); too large and the line is evicted before use, and the prefetches use up fill buffers. Always measure: software prefetch makes a program slower about as often as faster.

---

## 5. Virtually indexed caches (VIPT) and aliasing

### 5.1 The problem

Programs use virtual addresses; the cache would like physical ones. Translation (the TLB, tutorial 15) takes time, and the L1 must answer in 4 to 5 cycles. Three choices:

- **PIPT** (physically indexed, physically tagged): translate first, then index. Simple and always correct, but the translation sits in front of every L1 access. Used for L2 and L3.
- **VIVT** (virtual index, virtual tag): no translation at all, but two virtual addresses for the same physical page create two copies, and the cache must be flushed or tagged on a context switch. Rare.
- **VIPT** (virtual index, physical tag): **start the set lookup with the virtual address while the TLB translates in parallel**, then compare the physical tag. The usual L1 design.

### 5.2 The 4 KB-per-way rule

VIPT works without trouble when the **index bits and offset bits are all inside the page offset**, because the page offset is the same in the virtual and physical address. With 4 KB pages that is 12 bits:

```text
offset bits + index bits  <=  12        so    sets x line size  <=  4096        so    one way  <=  4 KB
cache size  =  ways x (sets x line size)  <=  ways x 4 KB
```

| L1 data cache | Ways | Sets | One way | Fits the rule? |
|---|---|---|---|---|
| 32 KB | 8 | 64 | 4 KB | yes (ways × 4 KB = 32 KB) |
| 48 KB (this VM) | 12 | 64 | 4 KB | yes (12 × 4 KB) |
| 64 KB, 8-way | 8 | 128 | 8 KB | **no**: index bit 12 is above the page offset |

This is the answer to **"why was the L1 stuck at 32 KB for so long?"** With 4 KB pages and 64 sets, the only way to grow without breaking the rule is to add ways, and ways cost power and time: 8-way gave 32 KB, 12-way gives 48 KB. (Apple's 128 KB L1, reported as 8-way, matches 8 × 16 KB: Apple uses 16 KB pages.)

### 5.3 Aliases (synonyms)

If an index bit comes from above the page offset, two virtual addresses that map to the *same physical page* can have different values in that bit. They land in **different sets**: the same data are cached twice, and a write to one copy leaves the other stale. The fixes: the OS gives such pages the same "colour" (page colouring), or the hardware detects and merges the aliases (as some Arm cores do), or the design simply follows the rule of 5.2. A 64 KB 8-way L1 with 4 KB pages has 2 colours.

---
## 6. Loop interchange, blocking and tiling

### 6.1 Interchange: change the order, not the work

The three nested loops of a matrix product `C[i][j] += A[i][k] * B[k][j]` can be written in six orders with the same result. Only the **innermost** loop's strides matter for the first look. Count lines touched per inner iteration (8 doubles per 64-byte line, rows longer than the cache, no reuse assumed between inner loops):

| Order (outer to inner) | Inner loop walks | `A[i][k]` | `B[k][j]` | `C[i][j]` | Lines per multiply-add |
|---|---|---|---|---|---|
| ijk, jik | k | stride 1: 1/8 | stride N: **1** | in a register: 0 | 1.125 |
| ikj, kij | j | in a register: 0 | stride 1: 1/8 | stride 1: 1/8 | **0.25** |
| jki, kji | i | stride N: **1** | in a register: 0 | stride N: **1** | 2.0 |

Prediction: ikj and kij are best, ijk and jik about four and a half times worse, jki and kji eight times worse. *Measured* for N = 1024 doubles (2^30 multiply-adds, scalar code), with the rows exactly 8192 bytes apart, and with each row padded by one line (8256 bytes apart):

| Version | Row stride 8192 B | Row stride 8256 B (padded) |
|---|---|---|
| ijk | **7.2 s** (6.7 ns per multiply-add) | 1.2 s (1.1 ns) |
| ikj | 0.5 to 0.8 s (about 0.6 ns) | 0.49 s (0.45 ns) |
| tiled 32 × 32, ikj inside | 0.5 s (0.5 ns) | **0.34 s** (0.32 ns) |

Two effects are mixed in the first column. Interchange alone (ijk to ikj) is about **10 times**. In ijk the inner loop walks a **column** of `B`: 1024 lines, 8192 bytes apart. Because 8192 is a multiple of 4096, all of them fall in the same L1 sets (tutorial 13, section 4.2), so they also fight each other: **conflict misses on top of capacity misses**. Padding each row by 64 bytes spreads the column over many sets, and ijk alone becomes 6 times faster (7.2 s to 1.2 s) with no change to the algorithm. The same effect shows without padding if you simply pick another size: at N = 1000 (rows 8000 bytes apart, not a multiple of 4096) ijk takes **0.85 to 0.9 s**, eight times faster than at N = 1024, although it does 7% less work.

### 6.2 Blocking (tiling): reuse before eviction

Interchange fixes the order; **blocking** also adds *reuse*. Split the matrices into T × T tiles and multiply tile by tile. While a tile of `A`, `B` and `C` sits in L1, each element is used T times before it is evicted, so the lines fetched per multiply-add drop by about a factor T.

**Choosing T.** Three tiles of doubles must fit with room to spare. Use about half the L1 (the rest holds the stack, the loop's other data, and some ways are lost to conflicts): 3 × T² × 8 B ≤ 24 KB gives T² ≤ 1024, so **T = 32**. For T = 32 a step touches 3 tiles × 32 rows × 4 lines = 384 lines for 32³ = 32,768 multiply-adds, which is 0.012 lines per multiply-add against 0.25 for plain ikj, 20 times fewer (formula: 3/(8T)). The measured best was T = 32: 0.32 ns against 0.45 for ikj, a gain of 1.4 times, much smaller than 20, because ikj already streams `B` from L3 with the help of the prefetchers, and a loop with two loads and a store per multiply-add is then limited by the store port and the uop count (tutorial 12). Real libraries (BLAS) tile for L1, L2 and registers at once.

### 6.3 Blocked transpose, measured

Transposing a 4096 × 4096 matrix of 4-byte floats (64 MiB) is the standard blocking example, because one of the two arrays is always walked by columns. *Measured*, best of several runs, milliseconds (the noise between runs is about 25%):

| Version | Row stride 16,384 B (power of two) | Row stride 16,448 B (padded by a line) |
|---|---|---|
| naive | about **200** | 85 to 110 |
| blocked 8 × 8 | 55 to 70 | 29 to 46 |
| blocked 32 × 32 | 60 to 75 | 25 to 39 |
| blocked 64 × 64 | 57 to 73 | 40 to 43 |
| blocked 128 × 128 | 75 to 140 | 47 to 56 |
| blocked 256 × 256 | 150 to 205 | 41 to 47 |

What it shows: (1) blocking is worth about **3 times** over the naive loop, in both columns; (2) padding alone halves the naive loop (200 to 95 ms): conflicts; (3) with a power-of-two stride, big tiles stop working: a 256-row tile is as slow as no blocking, because its 256 rows, 16 KB apart, fall into only a few sets and evict each other, while with padding any tile up to the size of L2 still works; (4) the good range of tile sizes is wide (8 to 64): do not search for the exact optimum, pick a size from the cache arithmetic and check that it is not worse than the neighbours. For the transpose, both tiles must fit: 2 × T² × 4 B, so T = 64 uses 32 KB.

### 6.4 Related transformations

- **Loop fusion:** merge two loops over the same array into one, so the second operation finds the data still in cache. Opposite of **fission**, which splits a loop whose body has too many streams for the prefetchers or too many live registers.
- **Compilers** do some interchange and fusion at `-O3`, only when they can prove the transformation is safe (no aliasing, no dependence). Tiling is rarely automatic. Check the output, and do it by hand for hot loops.

---

## 7. Data layout: touch fewer lines

### 7.1 Array of structs against struct of arrays

```c
struct P { double x, y, z, vx, vy, vz, m, id; };   // 64 bytes
P aos[N];                                          // array of structs: one line per record
double x[N], y[N], z[N], vx[N], vy[N], vz[N], m[N], id[N];   // struct of arrays: one array per field
```

*Measured*, summing fields over N = 40 million records (the AoS array is 2.4 GiB, one field is 320 MB), one core, ms:

| Loop reads | Array of structs | Struct of arrays | Ratio |
|---|---|---|---|
| 1 field (`x`) | 198 ms (4.9 ns per record) | **35 ms** (0.9 ns) | **5.6** |
| 3 fields (`x + y + z`) | 180 ms | 82 ms | 2.2 |
| all 8 fields | 212 ms | 199 ms | 1.1 |

The array of structs reads **one 64-byte line per record whatever the field**: 13 GB/s of lines for 1.6 GB/s of useful data. The struct of arrays reads only the lines of the fields the loop uses: 1/8 of a line per record for one field. When all 8 fields are used, the bytes are the same and the two layouts tie.

**Rule:** the layout should follow the access pattern. Many records, few fields per loop: struct of arrays. Each loop uses most fields of one record (a lookup that reads the whole entry): array of structs. SIMD loops like a hybrid, **AoSoA**: blocks of 8 or 16 records stored field by field (tutorial 21).

### 7.2 Hot/cold splitting and line-sized records

Not all fields are equal. A flow record of 96 bytes might have a 16-byte key and 16 bytes of counters that the packet path reads and writes, and 64 bytes of configuration and statistics that only the control plane reads. Split it:

```c
struct FlowHot  { uint8_t key[16]; uint64_t pkts, bytes; };     // 32 bytes: two per line
struct FlowCold { /* 64 bytes of rarely used fields */ };       // same index, separate array
```

A lookup that probes 4 consecutive slots touches 4 × 96 = 384 bytes (**6 to 7 lines**) with the combined record, and 4 × 32 = 128 bytes (**2 to 3 lines**) with the hot record. Further habits:

- **Order fields** largest first to avoid padding; check `sizeof` with a `static_assert`.
- **Align** hot structures to a line (`alignas(64)`), so that a record does not straddle two lines (a load that crosses a line boundary costs two accesses).
- **Size nodes to lines:** a node of 64 bytes costs one miss; a node of 72 bytes costs two for some of its fields.
- **Bucket the hash table:** four 16-byte entries in one 64-byte bucket means a lookup is one line (the idea of project `pfilt`).
- **Per-thread data on separate lines** (`alignas(64)`) to avoid false sharing (tutorial 19).

### 7.3 What this means for the data structures you choose

Rough line counts for one lookup among 1 million entries (estimates, to compare structures, not to predict times):

| Structure | Lines touched per lookup | Why |
|---|---|---|
| `std::map` or any binary tree with one node per allocation | about 20 dependent nodes (the top ones are usually cached) | each node is a separate allocation, and each step waits for the previous load (MLP 1, section 3) |
| Sorted array with binary search | about 16 lines (20 probes, the last 4 in one line) | no pointers, but the probes jump over large strides |
| B-tree with 16 keys per node | about 5 nodes (log₁₆ of 10⁶), 1 to 2 lines each | a node is read with one or two misses and searched in cache |
| Chained hash table (`std::unordered_map`) | 2 to 3 dependent lines (bucket array, node, maybe a second node) | pointer to a separately allocated node |
| Open addressing, 4 entries per line | about 1 | the probe sequence stays in one line |

This is why "an array beats a linked list" in real programs, why B-trees beat binary trees in memory as well as on disk, and why flat hash maps are faster than node-based ones: **big-O counts operations; the cache counts lines.**

---

## 8. Memory types, non-temporal stores and cache-line flushes

### 8.1 Not all memory is cacheable

The page tables (tutorial 15) mark each page with a **memory type** that tells the core and the caches how to treat accesses to it:

| Type | x86 name | Arm name | Behaviour | Used for |
|---|---|---|---|---|
| Write-back | WB | Normal, write-back | cached; writes held until eviction; accesses may be reordered and speculated | all ordinary RAM |
| Write-through | WT | Normal, write-through | cached; every write also goes to memory | rare (some frame buffers, legacy) |
| Write-combining | WC | Normal, non-cacheable | not cached; stores gather in a 64-byte **write-combining buffer** and leave as one burst; reads are uncached and slow; weakly ordered | frame buffers, GPU or NIC memory windows written in bulk |
| Uncached | UC | Device (nGnRnE and relatives) | no caching, no combining, no speculation, strongly ordered | device registers (memory-mapped I/O) |

In Linux a driver gets these with `ioremap()` (uncached device memory) and `ioremap_wc()` (write-combining); ordinary RAM is write-back.

### 8.2 Why device registers are uncached

1. **The value changes without the CPU:** a status bit is set by hardware. A cached copy would never see it.
2. **Reads have side effects:** reading an interrupt status register may clear it, and reading a FIFO register pops an entry. A speculative or duplicated read changes the device.
3. **Every write must reach the device, once, in order:** a write-back cache would hold the write until eviction and merge two writes to one location into one.
4. **No speculation or reordering:** the core must not run a load ahead of the one before it.

`volatile` stops only the compiler from removing or reordering the access; the **memory type** is what stops the hardware. A write-combining mapping is fine for a frame buffer, where order inside the burst does not matter and a read has no side effects, and wrong for a register. Reads of device registers are slow (a round trip over PCIe, hundreds of nanoseconds to microseconds), which is why drivers avoid polling them in hot loops.

### 8.3 Non-temporal stores

Tutorial 13 showed that a store miss reads the line first (read for ownership), so writing a fresh buffer moves twice the data and fills the cache with lines you will not read. A **non-temporal (streaming) store** goes through the write-combining buffers straight to memory: no read, no cache pollution.

```c
#include <immintrin.h>
for (size_t i = 0; i + 8 <= n; i += 8)
    _mm256_stream_ps(dst + i, v);     // aligned 32-byte non-temporal store
_mm_sfence();                         // required before anyone else reads the data
```

*Measured*, filling a buffer, GB/s:

| Buffer size | Ordinary stores | Non-temporal stores |
|---|---|---|
| 64 KB | 49 | 17 |
| 1 MB | 61 | 17 |
| 8 MB | 24 | 18.5 |
| 64 MB | 18 | 16 |
| 512 MB | **6.6** | **19** |
| 2 GB | 7.8 | 15 |

Below the L2 the ordinary stores win by 3 to 4 times, because the buffer stays in cache; at 64 MB they tie; beyond that, in DRAM, the non-temporal stores are **2 to 3 times faster**. **Rule:** use them for buffers larger than the cache share you can use and not read back soon. The `_mm_sfence()` is not optional: non-temporal stores are weakly ordered, the one exception to x86's strong store ordering (tutorial 20).

### 8.4 Flushing a line

- **x86:** `clflush` and `clflushopt` write the line back if it is dirty and **invalidate** it; `clwb` writes it back and **keeps** it.
- **Arm:** `DC CIVAC` (clean and invalidate) and `DC CVAC` (clean) by address.

Uses: (1) **DMA with non-coherent devices**, common on Arm systems: clean the buffer before the device reads memory, invalidate before the CPU reads what the device wrote (on x86 the hardware keeps DMA coherent); (2) **persistent memory**: write back so that data reach the persistent medium; (3) **timing side channels** (Flush+Reload, tutorial 30).

*Measured:* a load of a cached line took 29 ns, and the same load right after `clflush` took 203 ns. Both include about 25 ns of timer overhead, so the real difference is about **175 ns**: easy to see, which is exactly what Flush+Reload exploits.

---

## 9. Cache partitioning and noisy neighbours

The L3 and the memory bandwidth are shared by all cores of a chip, and in the cloud by tenants that never meet. A batch job that streams over 200 MB evicts the hot lines of a latency-critical service from the shared L3; the service's miss rate rises, and its 99th-percentile latency jumps, **although it has its own cores**. This is the **noisy neighbour**.

Hardware answers:

- **Intel Resource Director Technology (RDT):** **CAT** (cache allocation) gives each *class of service* a bit mask of L3 ways (the mask must be contiguous); **MBA** limits memory bandwidth; **CMT/MBM** monitor cache occupancy and bandwidth per class. AMD has equivalents (PQoS).
- **Arm MPAM** (memory partitioning and monitoring): requests carry a partition ID, and caches and memory controllers apply per-partition limits on capacity portions and bandwidth.

On Linux, the interface is the `resctrl` filesystem. For a 20-way L3, give the service 12 ways (60%) and the batch job 8 ways (40%):

```bash
mount -t resctrl resctrl /sys/fs/resctrl
mkdir /sys/fs/resctrl/service /sys/fs/resctrl/batch
echo "L3:0=fff00" > /sys/fs/resctrl/service/schemata    # ways 8-19 of cache domain 0
echo "L3:0=000ff" > /sys/fs/resctrl/batch/schemata      # ways 0-7
echo $SERVICE_PID > /sys/fs/resctrl/service/tasks
```

(`fff00` has 12 one-bits, `000ff` has 8. Masks may overlap to share ways, and hardware usually needs at least a few ways per class. This VM has no `resctrl`, so these commands were not run here.) Related measures: pin threads and keep them on one socket (tutorial 16), use huge pages (tutorial 15), isolate cores for the critical service, and reserve whole machines or sockets when the guarantee must be hard.

---

## 10. Finding misses

### 10.1 With hardware counters

```bash
perf stat -e cycles,instructions,L1-dcache-loads,L1-dcache-load-misses,LLC-loads,LLC-load-misses ./app
perf stat --topdown ./app                 # is the program memory bound at all? (tutorial 12)
perf record -e mem_load_retired.l3_miss:pp ./app && perf report   # which source lines miss (Intel event name; it differs by core)
perf mem record ./app && perf mem report  # which data addresses, and from which level
perf c2c record ./app && perf c2c report  # which lines are shared between cores (tutorial 19)
```

The generic names (`L1-dcache-load-misses`, `LLC-load-misses`) are aliases for model-specific events, and not every machine provides all of them. A miss **rate** alone proves nothing: 50% misses in a loop with high MLP can be faster than 5% in a dependent chain. Always read a rate together with the top-down bucket (tutorial 12).

### 10.2 Without counters: simulate

Virtual machines and containers often have no counters (this one had none). **Cachegrind** (`valgrind --tool=cachegrind`) runs the program on a simulated cache that you configure, and counts misses per instruction and per source line. *Measured:* summing a 1024 × 1024 `int` array (4 MiB) by rows and by columns, with the cache set to 32 KB 8-way and an LL of 2 MB 16-way:

```bash
valgrind --tool=cachegrind --D1=32768,8,64 --LL=2097152,16,64 ./rc row
```

| Walk | D1 read miss rate | D1 read misses (of 1,048,576 reads) |
|---|---|---|
| by rows | **6.1%** | 67,000 (one per 16 ints, one per line) |
| by columns | **95.1%** | 1,050,000 (nearly every read) |

The ratio is 16, the number of integers per line. The cost: the run is 20 to 50 times slower, and the simulated cache is a plain LRU cache without prefetchers, so absolute numbers differ from the hardware. But it is deterministic, needs no special permissions, and `cg_annotate` points at the source lines.

### 10.3 A triage method

1. **Is it memory bound?** Top-down (or compare time against the arithmetic bound of tutorial 12).
2. **Which level?** L1, L2, L3 or DRAM misses (counters, or the staircase of tutorial 13).
3. **Which kind?** Compulsory, capacity, conflict, coherence (section 1): halve the size, double the ways, run with one thread.
4. **Apply the fix for that kind** (the table in section 11), then measure again.

---

## 11. What this means for your code

| Symptom | Likely cause | Try |
|---|---|---|
| Time jumps when the data outgrow L2 or L3 | capacity | tile or block; shrink the data; struct of arrays; process in passes |
| Fast at N = 1000, slow at N = 1024 | conflict (power-of-two stride) | pad rows or buffers by one line; change the stride |
| Walks a list, tree or chained hash | dependent misses, MLP 1 | flatten (arrays, B-tree, open addressing); batch independent lookups; prefetch the next nodes |
| Reads one field of a big struct | wasted lines | struct of arrays; hot/cold split |
| Random access with a long body per element | the window is too small to overlap misses | software prefetch about latency ÷ iteration time ahead |
| Writes a huge buffer once | read-for-ownership traffic, cache pollution | non-temporal stores plus `sfence` |
| Slower with more threads on shared data | coherence misses, false sharing | one line per thread; update shared data less often |
| Latency spikes when another job runs | shared L3 and bandwidth | CAT or MPAM; pin and isolate; reserve hardware |

---
## 12. Mistakes that cost points

1. **Treating all misses alike.** Compulsory, capacity, conflict and coherence misses have different fixes. Name the kind first (section 1).
2. **Optimising big-O and ignoring lines.** A binary tree and a B-tree with the same number of keys differ by 4 to 5 times in lines touched (section 7.3).
3. **Using power-of-two strides and sizes.** N = 1024 was eight times slower than N = 1000 for the same loop (section 6.1). Pad by one line.
4. **Assuming bigger tiles are better.** The tile (all its rows) must fit and its rows must not collide; with a power-of-two stride a 256-row tile was as slow as no tiling (section 6.3).
5. **Using a linked structure for a lookup-heavy workload.** Pointer chasing has MLP 1: 221 ns a hop here; independent lookups overlap about 12 at a time (section 3).
6. **Adding software prefetch everywhere.** On a loop with a short body the hardware already overlaps the misses, and the prefetches only add work (13 ns without, 16 to 20 ns with). It pays when the body is long (section 4.2).
7. **A prefetch distance that is too short or too long.** Too short and the data arrive late; too long and the line is evicted before use. Distance ≈ latency ÷ iteration time.
8. **Choosing array of structs or struct of arrays by habit.** The layout must follow what each loop reads (5.6 times for one field, 1.1 for all eight).
9. **Using `volatile` as if it were a memory type.** It stops the compiler only. A device register needs an uncached mapping, or the CPU may cache, merge or reorder the access (section 8.2).
10. **Using non-temporal stores for small or soon-reused buffers.** Below the L2 they are 3 to 4 times slower; and forgetting `_mm_sfence()` lets another thread read stale data.
11. **Quoting a miss rate without context.** 50% misses with high MLP can beat 5% in a dependent chain. Pair a rate with a top-down bucket.
12. **Believing a sharing or cache measurement taken in a VM without checking the topology.** Two vCPUs may be hyperthreads of one core (section 1.3), and the cache sizes the VM reports may be wrong (tutorial 13).

---

## 13. Questions you will be asked

**What are the 3 Cs? What is the fourth?** Compulsory (first touch), capacity (the working set exceeds the cache; a fully associative cache of the same size misses too), conflict (the mapping puts too many hot lines in one set; the same-size fully associative cache would have hit). The fourth is coherence: another core invalidated the line.

**How would you tell conflict misses from capacity misses on a real program?** Double the associativity or shift the data by one line: if misses fall, they were conflicts. Halve the working set: if misses fall sharply, capacity.

**This loop is fast at N = 1000 and slow at N = 1024. Why?** A power-of-two row stride puts the elements of a column in the same sets, so they evict each other (conflict) in addition to the capacity effect. Pad the rows by one line or change the stride.

**Inclusive against exclusive cache?** Inclusive keeps every L1 and L2 line also in L3: simple coherence lookup, but duplicated lines waste capacity and L3 evictions force back-invalidation. Exclusive keeps a line in one level, so capacity is the sum, at the price of moving lines between levels.

**What is a victim cache?** A small fully associative buffer that holds lines just evicted from a cache, so a line wanted back soon (a conflict miss) is found there.

**What is an MSHR?** A miss status holding register: one entry per outstanding line. It lets a cache keep working during a miss, merges later accesses to the same line, and its count limits the number of misses in flight (memory-level parallelism).

**How many misses can a core have in flight, and why does it matter?** About 10 to 16 at the L1. By Little's law, the bandwidth for random lines is lines in flight × 64 B ÷ latency. Pointer chasing uses 1; independent lookups can use all of them.

**When does a hardware prefetcher fail?** Pointer chasing, random access, crossing a 4 KB page, and short loops that do not train it.

**When does software prefetch help?** When the loop body is long enough that the window cannot see the next misses, and the address is known early. Set the distance to about latency ÷ iteration time and measure.

**Why is the L1 cache not larger than 32 or 48 KB?** A VIPT L1 must keep its index and offset bits inside the 4 KB page offset, so one way is at most 4 KB and the size is ways × 4 KB. Growth needs more ways, which cost time and power. (Apple uses 16 KB pages, hence 128 KB with 8 ways.)

**What is a cache alias or synonym?** Two virtual addresses for one physical page that map to different sets because an index bit is above the page offset. The data would be cached twice; avoid it by design, by page colouring, or by hardware detection.

**Explain blocking and how you choose the tile size.** Process the data in tiles that fit in the cache so each tile is reused many times before eviction. For matrix multiply, three T × T tiles of doubles in about half of L1; with a 48 KB L1, 3 × T² × 8 ≤ 24 KB gives T = 32 (with a 32 KB L1, T = 24). The lines per operation drop to about 3/(8T).

**Array of structs or struct of arrays?** Struct of arrays when loops read few fields of many records (it fetched 5.6 times fewer lines for one field); array of structs when each loop uses most fields of a record. Hot/cold splitting keeps frequently used fields together.

**Why are device registers mapped uncached?** Reads have side effects, values change under the CPU, and every write must reach the device once and in order. A cache would hide, merge or reorder them. `volatile` only affects the compiler.

**What is write-combining memory?** Uncached memory where stores gather in a 64-byte buffer and leave as a burst. Good for frame buffers and device windows written in bulk; reads are slow; ordering is weak.

**What are non-temporal stores, and when do you use them?** Stores that bypass the cache and avoid the read-for-ownership. For buffers much larger than the cache that will not be read soon (2 to 3 times faster in DRAM here). Follow with `sfence`.

**What do `clflush` and `clwb` do, and where are they used?** Flush a line (`clwb` keeps it in the cache). Used for non-coherent DMA, persistent memory, and in timing attacks.

**How do you protect a latency-critical service from a noisy neighbour?** Partition the shared cache with CAT (or MPAM on Arm) by giving it its own ways, limit the neighbour's bandwidth with MBA, pin and isolate cores, and monitor with CMT and MBM.

**How do you find cache misses on a machine without hardware counters?** Use a cache simulator such as Cachegrind with an explicit cache geometry. It is slow and approximate, but deterministic, and it shows misses per source line.

---

## 14. Problems

**How to use this section.** Try each problem before you read the answers. A to C are paper problems (25 to 30 minutes each); D mixes paper work with a short run on your own machine.

### Problem A (paper): classify the misses

A cache of 4 lines, 2-way (2 sets; set = line number mod 2), LRU. The accesses are line numbers:

```text
0 2 4 0 2 4 1 2 3 4 0 1
```

(a) Hit or miss for each access in the 2-way cache. How many hits?
(b) The same for a fully associative cache of 4 lines with LRU.
(c) Classify every miss as compulsory, capacity or conflict. How many of each?
(d) Add a one-entry victim buffer to the 2-way cache. How many hits now?
(e) For each kind of miss found in (c), say what you would change in the data or the program.

*Where:* paper. *How:* sections 1 and 2; trace the two caches access by access with the method of tutorial 13, section 5, and compare them.

### Problem B (paper, optional run): loop orders and tiles

The product `C[i][j] += A[i][k] * B[k][j]` with N = 2048 doubles per side, 64-byte lines, a 32 KB 8-way L1 (64 sets).

(a) For each of the six loop orders, count the lines touched per multiply-add (as in section 6.1) and rank them.
(b) Choose a tile size T so that three T × T tiles of doubles fit in half the L1. How many lines per multiply-add does the tiled loop touch, and how many times fewer than the best untiled order?
(c) The row stride is 2048 doubles = 16 KiB. Which sets do the rows of a 16-row tile fall into, and what happens? What padding do you choose, and why?
(d) Explain in two sentences why `ijk` was eight times slower at N = 1024 than at N = 1000 (section 6.1).
(e) *(Optional.)* Time `ijk` and `ikj` on your machine for N = 1000 and N = 1024 (compile with `gcc -O2 -fno-tree-vectorize`).

*Where:* paper for (a) to (d); your machine for (e). *How:* section 6. A line holds 8 doubles; a stride of N doubles touches a new line each step.

### Problem C (paper): parallel misses and prefetch

A core has 10 MSHRs, a memory latency of 90 ns and 64-byte lines.

(a) What is the highest bandwidth of random-line accesses, in GB/s? How many lines must be in flight to reach 25 GB/s?
(b) A gather reads 20 million random 8-byte elements, each from a different line. How long does it take at MLP 1, and at the MSHR limit (ignore compute)?
(c) A hash join probes keys one at a time and a probe costs one miss. It is rewritten to compute the bucket addresses of 8 keys first and then read all 8. What speed-up do you expect? What if the batch is 16?
(d) A loop does 60 ns of work per element (about 800 uops) and reads `table[idx[i]]` from a random line. Estimate the time per element without prefetch, the best prefetch distance, and the time with it. Repeat for a body of 10 uops.
(e) Why can no software prefetch speed up `p = p->next` on a linked list? Name two ways to fix the layout.

*Where:* paper. *How:* sections 3 and 4. Use Little's law: in flight = throughput × latency.

### Problem D (paper and your machine): VIPT, memory types, layout

(a) With 4 KB pages and 64-byte lines, what is the largest VIPT L1 with 8 ways, and with 16 ways? For a 64 KB, 4-way L1: how many index bits, how many of them above the page offset, and how many page colours? What is the largest 8-way L1 with 16 KB pages?
(b) Choose the memory type (write-back, write-combining, uncached) and give one reason: (i) the 16 MB frame buffer of a display, written by the CPU; (ii) a NIC doorbell register; (iii) a ring of descriptors in RAM, written by the CPU and read by a cache-coherent DMA device (x86); (iv) the same ring on an Arm SoC whose DMA is not cache-coherent; (v) a 4 GB buffer, filled once by the CPU, in a GPU memory window across PCIe.
(c) A flow table has 1 million entries of 96 bytes: a 16-byte key, 16 bytes of counters (updated on every packet) and 64 bytes of cold data. A lookup probes up to 4 consecutive slots. How many lines does it touch with the combined entry, and with a hot/cold split (32-byte hot entries)? How large is the hot table, and where does it fit?
(d) *On your machine:* save this program as `rc.c`, compile it with `gcc -O2 rc.c -o rc`, and run `valgrind --tool=cachegrind --D1=32768,8,64 ./rc r` (rows) and then `./rc c` (columns). Compare the **read** miss rate, the first number in the brackets on the "D1 miss rate" line. (Valgrind does not run on Apple silicon macOS: use a Linux machine, VM or container.)

```c
#include <stdio.h>
#define N 1024
static int a[N][N];
int main(int argc, char **argv) {
    long s = 0;
    for (int i = 0; i < N; i++) for (int j = 0; j < N; j++) a[i][j] = i ^ j;
    if (argv[1][0] == 'r') { for (int i = 0; i < N; i++) for (int j = 0; j < N; j++) s += a[i][j]; }
    else                   { for (int j = 0; j < N; j++) for (int i = 0; i < N; i++) s += a[i][j]; }
    printf("%ld\n", s);
}
```

*Where:* paper for (a) to (c); a Linux terminal for (d). *How:* sections 5, 7, 8 and 10 (the miss rates to expect are in section 10.2).

---

## Answers

### Problem A

(a) Even lines (0, 2, 4) map to set 0 and odd lines (1, 3) to set 1. Set 0 sees 0 2 4 0 2 4 2 4 0; set 1 sees 1 3 1. With two ways and LRU (oldest first in the brackets):

```text
access:  0     2     4      0      2      4      1     2      3      4      0      1
set 0:   [0]   [0,2] [2,4]  [4,0]  [0,2]  [2,4]  .     [4,2]  .      [2,4]  [4,0]  .
set 1:   .     .     .      .      .      .      [1]   .      [1,3]  .      .      [1,3]
result:  M     M     M      M      M      M      M     H      M      H      M      H
```

The first six accesses cycle three lines through two ways, so LRU always evicts the line that comes next: no hit. Then 2 and 4 hit by luck of the order, and the final 1 hits in set 1. Pattern `M M M M M M M H M H M H`: **3 hits, 9 misses**.

(b) Fully associative, 4 lines, LRU (oldest first):

```text
access:  0    2      4        0        2        4        1          2          3          4          0          1
cache:   [0]  [0,2]  [0,2,4]  [2,4,0]  [4,0,2]  [0,2,4]  [0,2,4,1]  [0,4,1,2]  [4,1,2,3]  [1,2,3,4]  [2,3,4,0]  [3,4,0,1]
result:  M    M      M        H        H        H        M          H          M          H          M          M
```

Pattern `M M M H H H M H M H M M`: **5 hits, 7 misses**. Lines 0, 2 and 4 all fit, so accesses 4 to 6 hit; line 3 evicts line 0, and the last two misses are lines 0 and 1 coming back after eviction.

(c) Compulsory: the first touches of lines 0, 2, 4, 1 and 3 (accesses 1, 2, 3, 7 and 9): **5**. Conflict: accesses 4, 5 and 6 (the fully associative cache hits, the 2-way cache misses): **3**. Capacity: access 11 (line 0 misses in both caches and is not a first touch): **1**. Total 9 = 5 + 3 + 1.

```text
classification:   C C C X X X C h C h P h
```

(d) With a one-entry victim buffer: `M M M V V V M H M H V H`, **7 hits**. Each line that set 0 evicts goes into the buffer, and it is found there when it is wanted back at accesses 4, 5, 6 and 11 (`V` is a hit in the buffer). Three misses of the 2-way cache vanish at accesses 4 to 6 and one more at access 11: the buffer makes the 2-way cache behave almost like the fully associative one (7 hits against 5, because the cache plus the buffer hold 5 lines).

(e) **Compulsory:** nothing to change (prefetching could hide them). **Conflict:** lines 0, 2, 4 are all even, so they collide in set 0 of a 2-way cache; shift the data by one line, or pad, so they spread over both sets; or give the cache more ways. **Capacity:** the one capacity miss comes from cycling over five lines; process the data in smaller passes that fit.

### Problem B

(a) Per multiply-add (8 doubles per line):

| Order | Inner loop | Lines per multiply-add |
|---|---|---|
| ikj, kij | j | 0 + 1/8 + 1/8 = **0.25** |
| ijk, jik | k | 1/8 + 1 + 0 = **1.125** |
| jki, kji | i | 1 + 0 + 1 = **2.0** |

Ranking: ikj and kij best; ijk and jik 4.5 times more lines; jki and kji 8 times more.

(b) Half of 32 KB is 16 KB: 3 × T² × 8 B ≤ 16,384 gives T² ≤ 682, so T ≤ 26. Choose **T = 24** (3 × 576 × 8 = 13,824 bytes) or the safer power of two **T = 16** (3 × 256 × 8 = 6,144 bytes). Lines per multiply-add = 3/(8T): T = 24: **0.0156**; T = 16: 0.0234. The best untiled order has 0.25, so the tiled loop touches **16 times** (T = 24) or **10.7 times** (T = 16) fewer lines.

(c) A row stride of 16 KiB is 4 × 4096, so the address bits 6 to 11 (the index) are the same for every row: all rows of the tile start in the same set. A 16-row tile uses 16 doubles = 128 B = 2 lines per row, so every row falls into the **same 2 sets**, which have 8 ways each: only **8 of the 16 rows** can be in L1 at once (and the three tiles compete), so the tile is evicted while being used: conflict misses. **Pad each row by one line** (8 doubles, a stride of 2056 doubles): each row then starts one set later, and the 16 rows spread over about 17 sets, at most 2 per set.

(d) At N = 1024 the rows are 8192 bytes apart, so all 1024 lines of a column of `B` map to the same set and evict each other; at N = 1000 the rows are 8000 bytes apart (125 lines), and the column spreads over all 64 sets, so only the ordinary capacity misses remain (and those hit in L2).

(e) Expect `ijk` to be several times slower at 1024 than at 1000 on a machine with 64 sets and 8 to 12 ways (we measured 7.2 s against 0.85 s), and `ikj` to be about the same for both.

### Problem C

(a) Highest bandwidth = 10 × 64 B / 90 ns = 640 / 90 = **7.1 GB/s**. For 25 GB/s: lines in flight = 25 × 10⁹ × 90 × 10⁻⁹ / 64 = 35.2, so **36 lines**, which is 3.6 times the MSHRs of this core: one core cannot do it with random lines (several cores, or the prefetchers' deeper queues for sequential streams).

(b) MLP 1: 20 × 10⁶ × 90 ns = **1.8 s**. At the MSHR limit (10 in flight): **0.18 s**.

(c) A batch of 8 overlaps 8 misses: about **8 times** faster (90 ns per key becomes about 11 ns). With a batch of 16 the limit is 10 MSHRs, so about **9 to 10 times**, hardly better than 8.

(d) **800-uop body:** the body is longer than the window (about 500 uops), so no overlap: time ≈ 60 + 90 = **150 ns** per element. Distance = ⌈90 / 60⌉ = 2, use 3 for margin. With the prefetch the miss hides behind the work: about **60 ns**, 2.5 times faster. **10-uop body:** the window holds about 50 iterations, so the hardware overlaps misses up to the MSHR limit: 90 ns / 10 = **9 ns** per element, and the prefetch gives nothing (it only adds uops).

(e) The address of the next node is stored in the current node, so it is not known until the current load returns; there is nothing to prefetch earlier. Fixes: allocate the nodes contiguously in traversal order (or use an array or a B-tree); or store a **jump pointer** to the node 8 steps ahead in each node and prefetch that.

### Problem D

(a) The largest VIPT L1 is ways × 4 KB: **32 KB** with 8 ways, **64 KB** with 16 ways. For 64 KB, 4-way, 64-byte lines: sets = 65,536 / (4 × 64) = 256, so 8 index bits + 6 offset bits = 14 bits; the page offset is 12 bits, so **2 index bits** (bits 12 and 13) are above it, giving **4 page colours**. With 16 KB pages the offset is 14 bits, and the largest 8-way L1 is 8 × 16 KB = **128 KB**.

(b) (i) **Write-combining:** bulk writes, no reads, order inside a burst does not matter. (ii) **Uncached (device):** the write must reach the device once and in order; combining or delaying it could lose or reorder a doorbell. (iii) **Write-back:** the hardware keeps DMA coherent, so the ring can be cached; make sure the descriptor writes are ordered before the doorbell write (a write barrier). (iv) Either map the ring **non-cacheable or write-combining** (`dma_alloc_coherent`), or keep it write-back and do explicit cache maintenance: clean before the device reads, invalidate before the CPU reads what the device wrote. (v) **Write-combining** (a write-combining mapping of the device window), or non-temporal stores into it: write-once data, no point in caching.

(c) Combined entries: 4 × 96 = 384 bytes, which is **6 lines** if aligned (up to 7). Hot entries: 4 × 32 = 128 bytes = **2 lines** (up to 3). The hot table is 1,000,000 × 32 B = **32 MB** (the cold table is 64 MB; the combined table is 96 MB). 32 MB does not fit in a 2 MB L2, but it fits in a large shared L3 (this VM reports 260 MB, shared with other tenants), so many lookups can hit L3 instead of going to DRAM; the 96 MB combined table would not fit in a typical 32 MB L3 at all. The counters are updated per packet, so only the hot lines become dirty.

(d) Expect a read miss rate of about **6%** by rows (one miss per 16 `int`s, i.e. one per line) and about **95%** by columns (nearly every read misses), a ratio of about 16, as in section 10.2. The array (4 MiB) is far larger than the simulated 32 KB cache, so each line is fetched again once per pass; by rows each fetched line serves 16 reads, by columns it serves none before it is evicted, because the elements of a column are 4096 bytes apart and fall in the same set.

---

## Cheat sheet

- **Three moves:** touch fewer lines, touch them in a better order, overlap the misses you cannot avoid.
- **The Cs:** compulsory (first touch: prefetch), capacity (working set too big: tile, shrink, SoA), conflict (set overload: more ways, pad, victim buffer), coherence (other core wrote: separate lines). Classify with an infinite, a same-size fully associative and the real cache.
- **Hierarchy:** inclusive (capacity = largest level, back-invalidation), exclusive (capacity = sum), non-inclusive. 8 × 1 MB L2 and 16 MB L3: 16 MB inclusive, 24 MB exclusive. Victim buffer: a few fully associative lines for recently evicted lines (one line turned 2 hits into 11 in the worked trace).
- **MSHRs and MLP:** one entry per outstanding line, merges secondary misses. In flight = throughput × latency (Little). Measured: pointer chase 221 ns; with 12 or more independent chains the time per access flattens at about 18 ns, which is 12 in flight. Pointer chasing = MLP 1; batch independent lookups.
- **Prefetch:** hardware (next-line, stride, streamer, adjacent-line) fails on pointer chasing, random access, page crossings, short loops. Software prefetch helps only when the body is longer than the window; distance ≈ latency ÷ iteration time (body 576 uops: 210 to 37 ns).
- **VIPT:** index and offset inside the page offset, so one way ≤ 4 KB and size ≤ ways × 4 KB (32 KB 8-way, 48 KB 12-way; Apple 128 KB with 16 KB pages). Beyond that: aliases, page colouring.
- **Loops:** interchange (ijk to ikj: about 10 times), tile with 3·T²·8 B ≤ half of L1 (T = 32 for a 48 KB L1), lines per operation about 3/(8T); pad power-of-two strides (ijk 7.2 s to 1.2 s; N = 1000 took 0.85 s).
- **Layout:** AoS reads one line per record (5.6 times slower for one field, equal for all fields); hot/cold split; align to 64 B; line-sized nodes; flat hash and B-trees beat node-based structures.
- **Memory types:** WB (RAM), WT, WC (write-combining burst: frame buffers), UC/Device (registers: side effects, order, volatile values). `volatile` is compiler-only.
- **Non-temporal stores:** skip the read-for-ownership; win beyond about 64 MB (2 to 3 times in DRAM), lose below L2 (3 to 4 times); need `sfence`. **Flush:** `clflush`, `clflushopt`, `clwb`; Arm `DC CIVAC`; flushed load 203 ns against 29 ns.
- **Partitioning:** CAT way masks (contiguous), MBA, CMT/MBM; Arm MPAM; Linux `resctrl`. Cure for noisy neighbours.
- **Tools:** `perf stat`/`record`/`mem`/`c2c` and top-down; without counters, `valgrind --tool=cachegrind` (rows 6%, columns 95% read misses). A rate alone proves nothing.
- **Cloud cautions:** cache sizes and CPU labels may be wrong; two vCPUs may share one core (no false sharing seen).

**Next:** tutorial 15, Virtual Memory and the TLB (paging and segmentation, page tables and PTE bits, the TLB and TLB reach, page faults, huge pages, page-walk caches, protection keys). Say "next" when you are ready.
