# CO 13: Cache Basics

*Part of the [Computer Organization plan](00_plan.md) · Core tutorial · Time: about 4 hours (2 reading, 2 problems) · You need: [tutorial 01](01_how_a_computer_works.md) (the latency ladder), [tutorial 03](03_boolean_logic_and_combinational_circuits.md) (comparators and multiplexers) and [tutorial 12](12_inside_a_modern_core.md) (where the L1 cache sits in a core)*

By the end of this tutorial you can:

- explain locality, and why a small fast memory in front of a big slow one works;
- split an address into tag, index and offset for a direct-mapped, a set-associative and a fully associative cache, and count the bits;
- trace a list of accesses through a cache by hand and count hits and misses;
- compare the replacement policies (LRU, pseudo-LRU, FIFO, random) with Belady's optimum;
- explain write-through, write-back, write-allocate and the write buffer, and say what a store miss costs;
- compute the average memory access time (AMAT) and the CPI of a two-level cache;
- explain CAM and TCAM, and use a TCAM for longest-prefix match and for firewall rules;
- read a measured cache "staircase" and find the size and associativity of a cache by experiment.

Tutorial 12 ended with a warning: the memory row is usually the biggest bound. This tutorial opens the memory system. It builds one cache from the gate level up (an address, a tag comparison, a decision), and then asks the questions that decide performance: where may a line go, who is thrown out, when is memory written, and what does it all cost on average.

**Where the numbers come from.** Timings marked *measured* were taken on the same cloud virtual machine as tutorial 12, with GCC 13, pinned to one core, with the clock calibrated first. The VM reports a Cascade Lake Xeon with a 32 KB, 8-way L1 and a 33 MB L3. The measurements below disagree with that report (section 2 and section 4.2): the core underneath has a 48 KB, 12-way L1. A virtual machine can present an older CPU model than the hardware it runs on, so **measure the cache, do not trust the label**. Exact numbers will differ on your machine; the shapes will not.

> **The one-sentence model.** A cache is a small, fast memory that holds copies of recently used *lines* of a big, slow memory; an address finds its copy by being cut into three fields: the **offset** (which byte in the line), the **index** (which set to look in) and the **tag** (which line of that set, the part that is compared).

---

## 1. Why caches work

### 1.1 Locality

Programs do not touch memory at random. Two habits make caches possible:

- **Temporal locality:** what you used a moment ago, you will probably use again soon.
- **Spatial locality:** what is next to what you used, you will probably use soon.

```c
long sum = 0;
for (int i = 0; i < n; i++)
    sum += a[i];
```

`sum` and `i` are used every iteration (temporal). `a[i]` is followed by `a[i+1]`, which sits right next to it (spatial). The instructions of the loop body run again and again (temporal), and each instruction is followed by the next one (spatial). A cache exploits both: it *keeps* what was used (temporal) and it *fetches a whole neighbourhood* at once (spatial).

Code with no locality gets no help. Walking a linked list whose nodes are scattered over memory, or `a[idx[i]]` with random `idx`, fetches a new line for almost every access.

### 1.2 Why not make all memory fast?

Memory can be fast, large or cheap; you can have two. Two reasons:

| | **SRAM** (caches) | **DRAM** (main memory) |
|---|---|---|
| Cell | 6 transistors holding a bit in a feedback loop | 1 transistor and 1 capacitor holding charge |
| Speed | fast: about a nanosecond or less for a small array | slow: 60 to 100 ns for a random access (tutorial 16) |
| Cost per bit | high (large cell) | low (small cell); an order of magnitude or more cheaper |
| Keeps its value | while powered | no: charge leaks, so each row is refreshed at least every 64 ms, and a read must write the value back |
| Where | on the CPU chip | on separate chips (or stacked next to the CPU) |

Size also costs time. A bigger array has longer wires and a larger decoder, so a 32 KB array is faster than a 32 MB one, even in the same technology. The answer is a **hierarchy**: a small, fast level close to the core, backed by bigger and slower levels.

| Level | Typical size | Typical time | Managed by |
|---|---|---|---|
| Registers | a few hundred bytes | under 1 cycle | compiler |
| L1 cache (split: instructions and data) | 32 to 128 KB per core | 3 to 5 cycles | hardware |
| L2 cache | 0.25 to 3 MB per core | 12 to 17 cycles | hardware |
| L3 cache | several MB to hundreds of MB, shared | 40 to 80 cycles | hardware |
| DRAM | gigabytes | 200 to 400 cycles (60 to 100 ns) | operating system and hardware |
| SSD | terabytes | 20 to 100 µs | operating system |

Everything from L1 to L3 is hardware: no instruction says "load into the cache". You see it only as speed.

### 1.3 Words you will use all the time

- **Line** (also called block): the unit of transfer between levels. 64 bytes on x86 and most Arm server cores; Apple's M-series report 128 bytes (`sysctl hw.cachelinesize` on a Mac). Every transfer moves whole lines.
- **Hit:** the data are in this level. **Miss:** they are not, and the next level is asked.
- **Hit time:** the time of a hit. **Miss penalty:** the extra time of a miss.
- **Hit rate / miss rate:** fractions of accesses; miss rate = 1 − hit rate.
- **Working set:** the lines a loop keeps reusing. If it fits in a level, that level serves almost every access after the first.

---

## 2. The staircase, measured

How would you find the sizes of the caches of a machine without reading a manual? Build a footprint of a chosen size, walk it in a way the hardware cannot predict, and time one step. The walk is a **pointer chase**: the buffer holds 64-byte nodes, each node holds the address of the next one, and the nodes form a random cycle.

```c
void *p = start;
for (long i = 0; i < steps; i++)
    p = *(void **)p;          // each load needs the previous one, so the time per step is the latency
```

Because every load depends on the one before it, nothing overlaps (tutorial 11), and the prefetchers cannot guess the next address (the order is random). The time per step is the **latency of the level that holds the footprint**.

| Footprint | *Measured* time per step | Cycles (at about 3.2 GHz) | Where the footprint lives |
|---|---|---|---|
| 16 KB | 1.5 ns | 5 | L1 |
| 32 KB | 1.5 ns | 5 | L1 |
| 128 KB | 4.3 ns | 14 | L2 |
| 512 KB | 5.4 ns | 17 | L2 |
| 2 MB | 25 ns | 79 | L3 |
| 4 MB | 24 ns | 77 | L3 |
| 16 MB | 125 to 145 ns | about 400 to 450 | DRAM |
| 64 MB | 135 ns (95 ns with huge pages) | about 420 (300) | DRAM |

Plotted against the footprint, this is a **staircase**: flat while the footprint fits, then a jump to the next level. What it tells you:

1. **Each step is 3 to 6 times slower than the one before.** One dependent miss to DRAM costs as much as about 90 L1 hits.
2. **The edges are the cache sizes.** The L1 step lies between 32 KB and 64 KB (section 4.2 pins it to 48 KB), the L2 step is at about 1 MB, and L3 ends between 4 and 8 MB. The `lscpu` report says the L3 is 33 MB, but it is shared by the whole socket and by other tenants, and a program in this VM sees the step at 4 to 8 MB. I cannot see from inside why; the lesson is that *the usable size is what you measure*.
3. **A server's L3 is slower than a desktop's.** Tutorial 01's round numbers (10 to 15 ns) are for a small chip; a big mesh-connected server L3 is 20 to 30 ns, as here.
4. **Huge pages shorten the DRAM step** (95 against 135 ns): fewer address translations (tutorial 15). This also reconciles the two DRAM figures in this tutorial: the 60 to 100 ns of the table in section 1 is the usual bare-metal figure for the memory access itself, and 95 ns with huge pages is inside it; the extra 40 ns with 4 KB pages is mostly address translation, a TLB miss and, in a guest, a nested page walk (tutorials 15 and 27).
5. **Run-to-run noise is 10 to 30% at the DRAM end** on a shared VM. The steps are real; the third digit is not.

Cost depends on the *footprint*, not on the number of instructions. The same dependent walk is 3 times slower when its data move from L1 to L2, about 16 times slower in L3 and about 90 times slower in DRAM.

---

## 3. One cache, from the address up

### 3.1 The line, and the address split

Memory is cut into aligned 64-byte lines. For a byte address `a`: the **line number** is `a / 64` (`a >> 6`) and the **offset** is `a mod 64` (the low 6 bits). The bytes `0x1000` to `0x103F` are one line; `0x1040` starts the next. A cache holds *copies of whole lines*, so it must answer two questions for every access:

1. Is the line with this number in the cache?
2. If so, where?

The answers come from the **mapping rule**: where is a given line allowed to sit? Three rules cover everything.

### 3.2 Direct-mapped: one possible place

Each line has exactly one slot: `slot = line number mod (number of slots)`. In bits: the low bits of the line number are the **index** (which slot) and the remaining high bits are the **tag**. Many lines share a slot, so each slot stores the tag of the line that is in it now.

A toy cache makes this concrete: 8-bit addresses, 16-byte lines (4 offset bits) and 4 slots (2 index bits), which leaves 2 tag bits.

```text
address (8 bits):   | tag (2) | index (2) | offset (4) |
                       7   6     5   4       3      0

slot   valid   tag    data (16 bytes)
 0       1      01    ...
 1       0      --
 2       0      --
 3       0      --
```

**Lookup:** use the index to read one slot; hit if `valid` is 1 **and** the stored tag equals the address tag; the offset picks the bytes inside the line. That is one comparator and no search.

Take `0x44` = `0100 0100`: tag `01`, index `00`, offset `0100`. Slot 0 holds tag `01`, so it hits. Now take `0x04` = `0000 0100`: tag `00`, index `00`; and `0x84` = `1000 0100`: tag `10`, index `00`. **All three addresses want slot 0.** If a program alternates between `0x04` and `0x44`, every access finds the other one's tag, and every access misses, while slots 1 to 3 sit empty. That is a **conflict miss**: nothing is wrong with the capacity, only with the mapping.

### 3.3 Fully associative: any place

Let a line go in any slot. There is no index: the tag is the whole line number, and a lookup compares it against **every** slot at once (one comparator per slot; the tag array is a CAM, section 9). The mapping can never cause a conflict. The costs are the comparators (power and area) and the need to *choose* a victim among all slots (section 6). This is used for small structures: TLBs, victim buffers, the tiny caches inside a router.

### 3.4 Set-associative: a few possible places

The compromise, used by almost every real cache: the index selects a **set** of N slots, called **ways**, and the line may sit in any way of its set. N = 1 is direct-mapped; N = all slots is fully associative.

```text
size = sets x ways x line size          sets = size / (ways x line size)
offset bits = log2(line size)           index bits = log2(sets)          tag bits = address bits - index - offset
```

**Example (the standard exam question):** 32 KB, 8-way, 64-byte lines, 48-bit addresses. Lines = 32768 / 64 = 512. Sets = 512 / 8 = 64. Offset = 6 bits, index = 6 bits, tag = 48 − 12 = **36 bits**.

The same 32 KB and 64-byte lines in the three organisations:

| Organisation | Sets | Index bits | Tag bits (48-bit address) | Comparators per lookup |
|---|---|---|---|---|
| Direct-mapped | 512 | 9 | 33 | 1 |
| 8-way set-associative | 64 | 6 | 36 | 8 |
| Fully associative | 1 | 0 | 42 | 512 |

More ways mean fewer conflict misses, and more comparators, more power and a slightly slower hit. Typical L1 data caches have 8 to 12 ways; L2 and L3 have 8 to 20.

### 3.5 What the hardware does on a lookup

```text
 address:   | tag (36) | index (6) | offset (6) |
                            │
               ┌────────────┴─────────────┐
               ▼                          ▼
      read the 8 tags of the set     read the 8 lines of the set (64 B each)
               │                          │
      8 comparators: stored tag == address tag, AND valid
               │                          │
               ├─ hit = OR of the 8       └─ 8-to-1 mux picks the matching line
               ▼                                         │
         hit or miss                       offset selects the bytes
```

An L1 cache reads the tags and all eight data lines **in parallel**, and picks one afterwards. It spends power (eight lines read, seven thrown away) to save time. This is the comparators and multiplexers of tutorial 03 in use.

**Why the index uses the bits just above the offset, not the top bits.** Consecutive lines then fall into consecutive sets, so an array spreads over all the sets. With the top bits as index, a whole array would sit in one set and fight itself.

### 3.6 What a cache really stores

A line in the cache is more than 64 bytes of data. It also needs a **tag**, a **valid** bit, usually a **dirty** bit (section 7), and some replacement state (section 6). For 16 KB, direct-mapped, 64-byte lines, 40-bit addresses: 256 lines, offset 6, index 8, tag 26. Per line the tag array stores 26 + 2 = 28 bits, so 256 × 28 = 7,168 bits = 896 bytes, **5.5%** on top of the data. Smaller lines mean more tag overhead per byte; that is one reason lines are not 8 bytes.

### 3.7 Split first level, unified outer levels

The L1 is split into an **instruction cache** and a **data cache**, so that a fetch and a load can happen in the same cycle (the structural hazard of tutorial 09) and so that code and data do not evict each other. L2 and L3 are **unified**: one cache for both.

---

## 4. Two more measurements

### 4.1 The machine charges per line, not per byte

Sum 4-byte integers over a 64 MiB buffer, but read only every `s`-th integer. The buffer has 1,048,576 lines of 64 bytes (16 integers each).

| Stride `s` (integers) | Bytes between reads | Reads | Lines touched | *Measured* time for the pass | Time per read |
|---|---|---|---|---|---|
| 1 | 4 | 16,777,216 | 1,048,576 | 11.3 ms | 0.68 ns |
| 4 | 16 | 4,194,304 | 1,048,576 | 7.8 ms | 1.9 ns |
| 16 | 64 | 1,048,576 | 1,048,576 | 6.8 ms | 6.5 ns |
| 64 | 256 | 262,144 | 262,144 | 2.7 ms | 10.3 ns |
| 256 | 1024 | 65,536 | 65,536 | 0.46 ms | 7.1 ns |

Go from stride 1 to stride 16: the program does **16 times fewer reads** and runs only **1.7 times faster**, because it still touches every line, and a line costs the same whether you use 4 bytes or 64. Only when the stride passes 16 does the pass touch fewer lines and get much faster (not in exact proportion: the hardware prefetcher fetches neighbouring lines, tutorial 14). Per read, the cost rises about 15 times from stride 1 to stride 64.

**Rule:** count *lines touched*, not bytes used. A struct of 8 bytes in a 64-byte line wastes 56 bytes of every fetch if you only read that field.

### 4.2 Finding the associativity by a collision experiment

Take N nodes spaced exactly **4096 bytes apart**, link them in a ring and chase the ring. With 64 sets and 64-byte lines (6 offset bits and 6 index bits, bits 6 to 11), two addresses 4096 apart have **the same index**: all N nodes land in one set. Measured cycles per step:

| N nodes | Spaced 4096 B apart (same set) | Spaced 4096 + 64 B apart (different sets) |
|---|---|---|
| 1 to 12 | 4.9 | 4.9 |
| **13** | **7.5** | 4.9 |
| 14 | 10.5 | 4.9 |
| 16 | 9.7 | 4.9 |
| 20 | 9.5 | 4.9 |
| 26 | 9.6 | 4.9 |

The same-set ring is fast up to N = 12, and slow from N = 13: the set has **12 ways**, and the 13th line cannot fit, so lines are evicted and re-fetched from L2 (about 10 cycles). The lines at 4096 + 64 spacing are spread over different sets and never collide, however many there are. From this: 64 sets × 12 ways × 64 bytes = **48 KB**, which is the L1 size that the staircase suggested, and not the 8-way 32 KB that `lscpu` reported. (The set count stays at 64 because tutorial 14 will show that the L1 index is limited by the 4 KB page: caches of this kind grow by adding ways.)

The practical lesson is the left column: **buffers, ring slots and hash buckets spaced by a power of two compete for the same sets.** A ring of 16 slots spaced 4096 bytes apart can keep only 12 of its 16 hot lines in L1, although the 16 lines are just 1 KB of data. Pad by one line (4096 + 64) and the problem disappears.

---

## 5. Tracing accesses by hand

Take a cache of 64 bytes: 4 slots of 16-byte lines. Offset = 4 bits. The byte addresses accessed, with their line numbers (`address >> 4`):

```text
addresses: 0x00 0x40 0x04 0x44 0x10 0x08 0x48 0x14 0x80 0x00 0x40 0x84
lines:       0    4    0    4    1    0    4    1    8    0    4    8
```

Note that `0x00`, `0x04` and `0x08` are the *same line* (line 0), so a cache that kept it would hit twice. **Direct-mapped** (4 sets; set = line mod 4, tag = line div 4):

| # | Address | Line | Set | Tag | Held in the set before | Result |
|---|---|---|---|---|---|---|
| 1 | 0x00 | 0 | 0 | 0 | empty | miss |
| 2 | 0x40 | 4 | 0 | 1 | line 0 | miss, evicts 0 |
| 3 | 0x04 | 0 | 0 | 0 | line 4 | miss, evicts 4 |
| 4 | 0x44 | 4 | 0 | 1 | line 0 | miss, evicts 0 |
| 5 | 0x10 | 1 | 1 | 0 | empty | miss |
| 6 | 0x08 | 0 | 0 | 0 | line 4 | miss, evicts 4 |
| 7 | 0x48 | 4 | 0 | 1 | line 0 | miss, evicts 0 |
| 8 | 0x14 | 1 | 1 | 0 | line 1 | **hit** |
| 9 | 0x80 | 8 | 0 | 2 | line 4 | miss, evicts 4 |
| 10 | 0x00 | 0 | 0 | 0 | line 8 | miss, evicts 8 |
| 11 | 0x40 | 4 | 0 | 1 | line 0 | miss, evicts 0 |
| 12 | 0x84 | 8 | 0 | 2 | line 4 | miss, evicts 4 |

**1 hit in 12.** Lines 0, 4 and 8 all map to set 0 and fight over it, while sets 2 and 3 are never used.

The same twelve accesses in a **2-way** cache (2 sets, set = line mod 2, LRU inside a set), and in a **fully associative** cache of 4 lines (LRU):

```text
direct-mapped    M M M M M M M H M M M M     1 hit,  11 misses
2-way, LRU       M M H H M H H H M M M M     5 hits,  7 misses
fully assoc.     M M H H M H H H M H H H     8 hits,  4 misses
```

In the 2-way cache, lines 0, 4 and 8 share set 0 with only two ways, so the third one (line 8) pushes out the others. In the fully associative cache, four distinct lines (0, 4, 1, 8) fit in four slots, so only the first touch of each misses.

A preview of the classification in tutorial 14:

| Cache | Misses | **Compulsory** (first touch of lines 0, 4, 1, 8) | The rest (**conflict** here, since the 4 lines fit) |
|---|---|---|---|
| Direct-mapped | 11 | 4 | 7 |
| 2-way | 7 | 4 | 3 |
| Fully associative | 4 | 4 | 0 |

The compulsory misses (a line is never in the cache the first time) cannot be removed by any cache organisation, only by prefetching. The others come from the mapping, and more associativity removes them.

---

## 6. Replacement: who is thrown out

On a miss in a full set, one line must go. A direct-mapped cache has no choice (one slot). A set-associative or fully associative cache must pick a **victim** among N candidates.

### 6.1 The policies

- **LRU (least recently used):** evict the line unused for the longest time. It bets on temporal locality. Exact LRU must keep the order of all N ways: log2(N!) bits per set, which is 16 bits for 8 ways, and must update that order on every hit. That is too expensive for a large L1.
- **Pseudo-LRU (tree PLRU):** an approximation with N − 1 bits per set (7 bits for 8 ways). Section 6.3 shows how.
- **FIFO:** evict the line that arrived first. A hit changes nothing, so it needs only a pointer (3 bits for 8 ways). It ignores how much a line is used.
- **Random:** evict any line. It needs almost no state (one shared counter). It sounds silly, but it never has a systematic worst case, and its miss rate is close to LRU's on many programs, especially in larger caches.
- **Belady's optimum (OPT):** evict the line whose next use is **farthest in the future**. This is the best possible policy for a given cache, but it needs the future, so no hardware can do it. It is the **yardstick**: replay a recorded trace under OPT to see how far a real policy is from the ideal.
- **RRIP (in a sentence):** each line has a small counter (2 bits) that predicts how soon it will be reused; new lines enter as "probably not reused soon", a hit promotes a line, and the victim is a line predicted far away. This protects the cache from a one-time scan, which pure LRU does not. Variants of the idea are believed to be used in recent shared L3 caches.

### 6.2 Where LRU fails: a loop slightly larger than the cache

Take a fully associative cache of 4 lines and a loop that walks 5 lines in a circle: 1 2 3 4 5 1 2 3 4 5 ... (20 accesses, 4 passes).

| Policy | Hits out of 20 |
|---|---|
| LRU | **0** |
| FIFO | **0** |
| Random | about 9 (average over many runs) |
| OPT (Belady) | 12 |

Why LRU gets nothing: when line 5 arrives, LRU evicts line 1 (the oldest). Line 1 is needed next, so it misses and evicts line 2, which is needed next, and so on. **Every line is evicted just before it is reused.** OPT knows better: when line 5 arrives it evicts line 4, which is needed last, so lines 1, 2 and 3 hit. Random does not know anything, but it sometimes keeps the right lines by luck, and wins by 9 hits to 0.

This is a real effect: a loop over an array just larger than the cache is the worst case for LRU. It is one reason modern shared caches use adaptive policies instead of pure LRU.

### 6.3 Tree pseudo-LRU, by hand

A 4-way set has 3 bits arranged as a tree. Each node says which side the next victim is on (0 = left, 1 = right).

```text
              root
            /      \
          L          R
         /  \       /  \
       w0    w1   w2    w3
```

**On an access to a way, set every node on its path to point away from it. To choose a victim, follow the bits from the root.**

Touch ways 0, 1, 2, 3 in this order (all start 0):

| After touching | root | L | R | Next victim (follow the bits) |
|---|---|---|---|---|
| w0 | 1 (right) | 1 (w1) | 0 | w2 |
| w1 | 1 | 0 (w0) | 0 | w2 |
| w2 | 0 (left) | 0 | 1 (w3) | w0 |
| w3 | 0 | 0 | 0 (w2) | **w0** |

After the four touches the victim is way 0, which is also the true LRU choice (w0 is the oldest). Now touch way 0 once more: root = 1, L = 1, and the victim is **way 2**. True LRU would choose **way 1** (the order is now w1, w2, w3, w0, oldest first). PLRU chose a different old line, but never the line just used. With 7 bits instead of 16, that is the price, and in practice the miss rates are close to LRU.

---

## 7. Write policies: when is memory written?

Reads are simple: a hit returns the data, a miss fetches the line. Writes need two decisions.

### 7.1 On a write hit: write-through or write-back

- **Write-through:** update the cache **and** the next level on every store. The cache never holds the only copy, so lines can be dropped any time and the design is simple. Cost: every store uses next-level bandwidth.
- **Write-back:** update only the cache and set the line's **dirty** bit. The next level is updated when the dirty line is **evicted**. Many stores to one line cost one write. Cost: the next level is out of date, so another core or a device must be told (coherence, tutorial 19; DMA, [tutorial 26](26_interrupts_exceptions_and_dma.md)), and evicting a dirty line costs a write.

### 7.2 On a write miss: write-allocate or not

- **Write-allocate:** fetch the line into the cache first, then write into it. This is called a **read for ownership (RFO)** on a multicore chip. It assumes you will use the line again.
- **No-write-allocate:** send the write to the next level and do not bring the line into the cache. It assumes you will not use it again.

The usual pairs: **write-back + write-allocate** (the normal CPU data cache: stores gather in the cache), and **write-through + no-write-allocate** (simple small caches, many GPU L1s and some microcontrollers).

### 7.3 The write buffer

A small queue between a cache and the next level holds writes that have not yet been sent, so the core does not wait for the slower level. A write-through cache needs one badly; a write-back cache uses one for dirty lines being evicted. The buffer can **merge** stores to the same line, and a load may need to check it first. When it fills, the core stalls. (It sits below the store queue of tutorial 12, which holds stores until they retire; the write buffer holds them after.)

### 7.4 What a store miss costs: a worked example

Write 1 MiB of fresh data once, with 4-byte stores, into memory that is not in the cache (16,384 lines of 64 bytes):

| Design | Reads from next level | Writes to next level | Total traffic |
|---|---|---|---|
| Write-back + write-allocate | 16,384 lines (1 MiB) | 16,384 lines when evicted (1 MiB) | **2 MiB** |
| Write-through + no-write-allocate, with a merging write buffer | 0 | 16,384 line writes (1 MiB) | **1 MiB** |
| Write-through, no merging | 0 | 262,144 four-byte writes (1 MiB) | 1 MiB in 16 times more transactions |

The common design **reads 1 MiB it is about to overwrite** and moves twice the data. That waste is the reason for non-temporal stores (tutorial 14), and for `memset` and `memcpy` implementations that use special instructions for large buffers.

Now the opposite case: a loop makes 1,000 stores to one word that stays in the cache. Write-through sends 1,000 writes (unless merged). Write-back sends one line, when it is evicted. **Write-back wins when data are written many times before eviction, and loses its advantage for write-once data.**

---

## 8. AMAT and CPI: one number for the whole hierarchy

### 8.1 The formula

**Average memory access time = hit time + miss rate × miss penalty.**

With two cache levels, the miss penalty of L1 is the average time of L2, which includes its own misses:

```text
AMAT = T_L1 + m_L1 x ( T_L2 + m_L2 x T_mem )
```

`m_L1` is the fraction of all accesses that miss L1. `m_L2` is the **local** miss rate: the fraction of the accesses *that reach L2* that miss. The **global** miss rate of L2 is `m_L1 x m_L2`, the fraction of all accesses that go to memory. Mixing up local and global is the most common mistake in this topic.

### 8.2 A worked example

L1 hit 4 cycles, L1 miss rate 6%; L2 hit 14 cycles, L2 local miss rate 25%; memory 300 cycles.

```text
AMAT       = 4 + 0.06 x (14 + 0.25 x 300) = 4 + 0.06 x 89 = 4 + 5.34 = 9.34 cycles
global L2 miss rate = 0.06 x 0.25 = 1.5%
without an L2:  4 + 0.06 x 300 = 22 cycles
```

The L2 cuts the average from 22 to 9.34 cycles, 2.4 times, and 98.5% of accesses never reach memory.

**How sensitive is it?** A single level with hit time 4 and penalty 300:

| Hit rate | Miss rate | AMAT |
|---|---|---|
| 99% | 1% | 4 + 3 = **7** cycles |
| 95% | 5% | 4 + 15 = **19** cycles |
| 90% | 10% | 4 + 30 = **34** cycles |

Dropping from a 99% to a 95% hit rate makes memory **almost three times slower on average**. A hit rate is not "close to 100%, so fine"; the last few percent are where the time goes.

### 8.3 From AMAT to CPI

The pipeline hides the L1 hit time, so only the time **above** a hit counts as a stall:

```text
CPI = base CPI + (memory accesses per instruction) x (AMAT - T_L1)
```

With base CPI 0.5 and 0.35 data accesses per instruction, and AMAT 9.34 from above:

```text
CPI = 0.5 + 0.35 x (9.34 - 4) = 0.5 + 0.35 x 5.34 = 2.37
memory stalls: 1.87 of 2.37 cycles, which is 79% of the time
```

The core could run at 0.5 cycles per instruction, and it runs at 2.37: **memory takes four fifths of the time**.

**Two cautions.** First, this model assumes the core waits for every miss. A real out-of-order core overlaps misses (memory-level parallelism, tutorials 11 and 14), so it usually beats the formula, and a pointer chase (no overlap) matches it well. Second, the formula holds the hit time out of the CPI, so it does not charge a design that makes the L1 hit slower; for such a comparison use AMAT, which includes the hit time.

---

## 9. CAM and TCAM: memory that searches

### 9.1 CAM

A normal RAM takes an **address** and returns the **data**. A **content-addressable memory (CAM)** takes the **data** (a key) and returns the **address** of the entry that holds it (or says "none"), in one cycle, because **every entry has its own comparator** and all of them compare at once. A fully associative cache is a RAM for the data plus a CAM for the tags. TLBs (tutorial 15) use the same idea, and so do the lookup tables in routers.

The price is what you would expect: all entries switch on every lookup, so a CAM uses a lot of power and area per bit, and stays small.

### 9.2 TCAM

A **ternary CAM** stores each bit as **0, 1 or X** (don't care). A stored entry such as `1010****` matches every key whose first four bits are `1010`. If several entries match, the **lowest-numbered entry wins** (a priority encoder picks it), so the order of the entries is the priority. TCAMs are the workhorse of routers and switches for **longest-prefix match** and for **access-control lists (ACLs)**.

**Longest-prefix match.** An IP address matches many routes (`10.1.2.77` matches `10.0.0.0/8`, `10.1.0.0/16` and `10.1.2.0/24`), and the rule is to use the longest one. Store the routes **sorted by prefix length, longest first**, and the first match is the longest:

| Entry | Pattern (8-bit toy) | Port |
|---|---|---|
| 0 | `101011**` | 2 |
| 1 | `1010****` | 1 |
| 2 | `********` | 0 (default) |

Key `10101101` matches all three entries; the encoder returns entry 0, so port 2. Key `10100011` fails entry 0 (its 5th and 6th bits are `00`, not `11`) and matches entry 1: port 1. Key `11110000` matches only the default: port 0. A lookup takes one cycle, however many routes there are.

The same with IPv4 prefixes:

| Priority | Route | Next hop |
|---|---|---|
| 0 | 10.1.2.0/24 | C |
| 1 | 10.1.0.0/16 | B |
| 2 | 10.0.0.0/8 | A |
| 3 | 192.168.0.0/16 | D |
| 4 | 0.0.0.0/0 | E |

`10.1.2.77` → C (24 bits). `10.1.9.1` → B (16 bits). `10.200.0.1` → A (8 bits). `8.8.8.8` → E (default). Adding a route means finding its place in the order, which can mean moving other entries; hardware and drivers have tricks for that, and it is a known pain point.

**ACLs and range expansion.** A TCAM entry is a value and a mask, so it matches *prefixes of numbers*, not ranges. A firewall rule "destination port 1024 to 65535" must be broken into aligned blocks:

```text
1024-2047  = 000001** ********      (prefix of 6 bits)        2048-4095  = 00001*** ********
4096-8191  = 0001**** ********      8192-16383 = 001***** ********
16384-32767 = 01****** ********     32768-65535 = 1******* ********     -> 6 entries
```

A range such as 2000 to 2999 needs 8 entries, and the worst case for 16-bit ports (1 to 65534) needs 30. A rule with a source range **and** a destination range multiplies: 6 × 6 = 36 entries for one rule. This is why firewall rule sets are measured in "TCAM entries after expansion", not rules, and why some rule shapes are far more expensive than others.

**Cost and alternatives.** A TCAM cell needs about 16 transistors against 6 for an SRAM cell (it stores the mask as well), and every entry compares on every lookup, so TCAMs are power-hungry and hold thousands to a few hundred thousand entries. CPUs do not use them for routing. In software the same job is done with tries, hash tables per prefix length, or large direct tables (DPDK's LPM library uses a 2^24-entry table for the first 24 bits). Knowing both sides is the interview answer.

---

## 10. What this means for your code

- **Think in lines.** The cost is lines touched (section 4.1). Keep the hot fields of a struct together within 64 bytes, walk arrays in order, and do not fetch a line for 8 bytes you could have had for free next to something else you use.
- **Compare your working set with the staircase.** If the hot data fit in L2 (about 1 MB here), the program runs at L2 speed. Measure the staircase of your machine once; it takes ten lines.
- **Avoid power-of-two spacing** for ring slots, per-thread arenas and hash buckets (section 4.2). Pad by one line.
- **A store miss is a read.** Writing a big fresh buffer moves twice the data (section 7.4). Reuse buffers that are already warm.
- **Small miss rates matter.** 99% against 95% is 7 against 19 cycles (section 8.2).
- **Do not trust cache sizes reported inside a VM.** Measure them.

---

## 11. Mistakes that cost points

1. **Thinking the cache works on bytes.** It works on lines. Reading 4 bytes costs the same line as reading 64 (section 4.1).
2. **Mixing up index and tag.** The index chooses the set; two addresses with the same index compete. The tag only identifies which line is in a slot. The index comes from the bits just *above the offset*, not from the top of the address.
3. **Forgetting the overhead.** A cache of "32 KB" holds 32 KB of data plus tag, valid, dirty and replacement bits (5 to 7% more).
4. **Thinking "fully associative" is always best.** It removes conflict misses, but it costs a comparator per line, and LRU on a loop just larger than the cache hits 0 times where a direct-mapped cache hits 4 (Problem B).
5. **Mixing local and global miss rates.** An L2 with a 25% local miss rate may have a global rate of 1.5%. In the AMAT formula the L2 miss rate multiplies the L1 miss penalty, not the whole access.
6. **Writing the AMAT of two levels as a plain sum.** It is `T_L1 + m_L1 x (T_L2 + m_L2 x T_mem)`, nested; the L2 and memory terms are paid only on an L1 miss.
7. **Forgetting the read for ownership.** A store to a line that is not cached reads the line first. Writing a fresh 1 MiB buffer moves 2 MiB (section 7.4).
8. **Saying "write-back is always better".** It wins when lines are written many times; for write-once data it adds a useless read. And it makes the next level stale, which coherence and DMA must handle.
9. **Spacing hot data by a power of two.** Ring slots, per-thread arenas and hash buckets at 4 KB or 64 KB spacing compete for the same sets and thrash a cache that is "mostly empty" (section 4.2).
10. **Treating a high hit rate as "fine".** 99% against 95% is 7 against 19 cycles on average (section 8.2).
11. **Believing the label inside a VM.** `lscpu` said 32 KB, 8-way; the measurement said 48 KB, 12-way (sections 2 and 4.2).
12. **Treating a TCAM range as a single entry.** A range is a set of prefixes; it can cost up to 30 entries for a 16-bit field, and ranges in two fields multiply.

---

## 12. Questions you will be asked

**What is locality, and why do caches work?** Programs reuse recent data (temporal) and use data near recent data (spatial). A cache keeps recent lines and fetches whole lines, so most accesses hit a small fast memory.

**What are the three ways to map a line to a cache slot?** Direct-mapped: one possible slot (one comparator, conflict misses). Fully associative: any slot (a comparator per slot, no conflicts). Set-associative: a set of N slots chosen by the index (N comparators; the practical middle).

**Take a 32 KB, 8-way cache with 64-byte lines. How many sets, and how do you split a 48-bit address?** 32768 / (8 × 64) = 64 sets; offset 6 bits, index 6 bits, tag 36 bits.

**Why is the index taken from the low bits above the offset?** Consecutive lines then go to consecutive sets, so a sequential array uses every set. With high bits, a whole array would sit in one set.

**What happens on a lookup in a set-associative cache?** Index selects the set; the tags of all ways are compared with the address tag in parallel, and valid is checked; a match selects the way, and the offset selects the bytes. L1 reads all the data ways at the same time and picks one afterwards, to save time.

**Direct-mapped or 8-way: what is the trade?** More ways mean fewer conflict misses but more comparators, more power and a slightly slower hit. Going from 1 to 2 ways helps most; beyond 8 the gain is small.

**Why a bigger line?** More spatial locality per miss and less tag overhead per byte. The costs: a longer fill, more bytes wasted when only some are used, fewer lines in the cache (more conflicts), and more false sharing on multicore (tutorial 19).

**Which replacement policies exist?** LRU (exact, costly), tree pseudo-LRU (N − 1 bits), FIFO, random, and Belady's OPT as the unreachable best. Modern shared caches add insertion and re-reference prediction (RRIP-style) to survive scans.

**Why can LRU be terrible?** A loop over slightly more lines than the cache holds evicts every line just before it is reused: 0 hits. Random or an adaptive policy keeps part of the loop.

**Write-through or write-back?** Write-through sends every store on; write-back sends a line once, when it is evicted. CPU data caches are write-back with write-allocate; write-through appears in simple designs and many GPU L1s.

**What is a write buffer?** A small queue between a cache and the next level so stores do not stall the core; it can merge stores to one line. The core stalls only when it is full.

**What does a store miss cost?** With write-allocate, a read of the line first (read for ownership) and a write-back later: twice the traffic for write-once data.

**Define AMAT and compute it for a two-level cache.** `T_L1 + m_L1 x (T_L2 + m_L2 x T_mem)` with `m_L2` the local miss rate. Example: 4 + 0.06 × (14 + 0.25 × 300) = 9.34 cycles.

**How do you find the cache sizes and associativity of a machine without a manual?** A pointer chase over growing footprints gives a staircase whose edges are the sizes (section 2). A ring of N nodes spaced a power of two apart gives a cliff at N = ways + 1 (section 4.2). Then compare with `lscpu` or `sysctl`.

**What is a TCAM, and where is it used?** A memory whose bits can be 0, 1 or don't-care, and which compares a key against every entry in one cycle, returning the first (highest-priority) match. Routers and switches use it for longest-prefix match and ACLs. Ranges must be expanded into prefixes, and it is expensive in power and area.

**Why is the L1 split into instruction and data caches?** So that a fetch and a load can happen in the same cycle (no structural hazard), and so code and data do not evict each other.

---

## 13. Problems

**How to use this section.** Try each problem before you read the answers. A, B and C are paper problems (25 to 30 minutes each); D mixes paper work with a few commands on your own machine.

### Problem A (paper): cache geometry

Three caches, all with 64-byte lines and 40-bit physical addresses: **C1** 16 KB direct-mapped; **C2** 32 KB 4-way; **C3** 4 KB fully associative.

(a) For each: number of lines, number of sets, offset, index and tag bits.
(b) Split the address `0x12345678` into tag (hex), index (decimal) and offset (decimal) for each cache.
(c) How big is the tag array of each, with one valid and one dirty bit per line, in bytes and as a percentage of the data size?
(d) Give a second address that collides with `0x12345678` in C1, and say how many lines of the same set each cache can hold at once.
(e) The VM reported a 32 KB, 8-way L1, and the measurement in section 4.2 found 48 KB, 12-way (64-byte lines). Compute the index bits of both. Does a 4096-byte spacing collide in both?

*Where:* paper. *How:* section 3. Line number = address / 64; index = line number mod sets; tag = line number / sets.

### Problem B (paper): hits and misses

A cache of 64 bytes with 16-byte lines (4 lines). The byte addresses accessed, in order:

```text
0x00 0x10 0x20 0x30 0x40 0x04 0x14 0x24 0x34 0x44 0x08 0x18
```

(a) **Direct-mapped:** the line number, set and tag of each access, hit or miss, and the number of hits.
(b) **2-way set-associative, LRU:** hits and misses.
(c) **Fully associative with LRU,** then with Belady's OPT: hits.
(d) The direct-mapped cache beats the fully associative LRU cache. Explain. How many hits would a fully associative cache of 5 lines (80 bytes) get?

*Where:* paper. *How:* sections 5 and 6. First convert every address to a line number (`address >> 4`).

### Problem C (paper): AMAT and CPI

A 3 GHz core: L1 hit 4 cycles, L1 miss rate 5%; L2 hit 12 cycles, local miss rate 20%; memory 250 cycles. Base CPI 0.6; 0.4 data accesses per instruction; instruction fetches always hit.

(a) AMAT in cycles, the global L2 miss rate, and how many of every 1,000 accesses reach memory.
(b) AMAT with no L2.
(c) CPI, and the share of time spent stalled on memory.
(d) Two upgrades. **X:** a bigger L2 (hit 14 cycles, local miss rate 12%). **Y:** add an L3 between L2 and memory (hit 40 cycles, catches half of the L2 misses; the other half go to memory). Compute AMAT, CPI and the speed-up over the original for each. What would you ask before choosing?
(e) Give two reasons why a real out-of-order core usually does better than this CPI.

*Where:* paper. *How:* section 8. Keep local and global miss rates apart.

### Problem D (paper and your machine): policies, writes, TCAM

(a) A 4-way set uses tree PLRU (section 6.3). Fill ways 0, 1, 2, 3 in that order, then touch way 2, then way 0. Which way does PLRU evict next? Which would true LRU evict? Which would FIFO evict?
(b) A loop stores `0` to every element of a 16 KiB `int` array (4,096 elements) 100 times; the array is not in the cache at the start and is never read. Count the transfers to the next level for (i) write-back with write-allocate and (ii) write-through with no-write-allocate and no merging.
(c) A TCAM holds the routes 12.0.0.0/8 → 1, 12.34.0.0/16 → 2, 12.34.56.0/24 → 3 and 0.0.0.0/0 → 4. Give the order of the entries, and the next hop for 12.34.56.7, 12.34.99.1, 12.200.0.1 and 9.9.9.9. Then: how many entries does the port range 1000 to 1999 need, and how many does a rule with source ports 1000 to 1999 *and* destination ports 2000 to 2999 need?
(d) *On your machine:* find the line size, the L1 data cache size and its associativity (Linux: `lscpu` and `/sys/devices/system/cpu/cpu0/cache/index0/`; macOS: `sysctl hw.cachelinesize hw.l1dcachesize hw.l2cachesize`, and `hw.perflevel0.l1dcachesize` for the performance cores of Apple silicon). Compute the sets and the index bits. Which spacing of hot data would collide on your machine?

*Where:* paper for (a) to (c); your terminal for (d). *How:* sections 6.3, 7.4, 9 and 4.2.

---

## Answers

### Problem A

(a)

| | Lines | Sets | Offset | Index | Tag |
|---|---|---|---|---|---|
| C1 (16 KB, direct) | 256 | 256 | 6 | 8 | 26 |
| C2 (32 KB, 4-way) | 512 | 128 | 6 | 7 | 27 |
| C3 (4 KB, fully assoc.) | 64 | 1 | 6 | 0 | 34 |

(b) `0x12345678` = line number `0x48D159`, offset `0x38` = 56. **C1:** index = `0x48D159` mod 256 = `0x59` = 89; tag = `0x48D159 >> 8` = `0x48D1`. **C2:** index = `0x48D159` mod 128 = 89; tag = `0x48D159 >> 7` = `0x91A2`. **C3:** no index; tag = `0x48D159`. All three have offset 56.

(c) C1: 256 × (26 + 2) = 7,168 bits = **896 bytes**, 5.5% of 16 KB. C2: 512 × (27 + 2) = 14,848 bits = **1,856 bytes**, 5.7%. C3: 64 × (34 + 2) = 2,304 bits = **288 bytes**, 7.0%. The fully associative cache has the longest tags and the smallest data, so the highest overhead. (Replacement bits are extra.)

(d) Any address differing by a multiple of 16 KB (`0x4000`) has the same index in C1: `0x12349678` (index 89, tag `0x48D2`). Lines held at once in that set: C1 **1**, C2 **4**, C3 **64** (everything is in one set).

(e) 32 KB 8-way: 512 lines / 8 = 64 sets, 6 index bits. 48 KB 12-way: 768 lines / 12 = 64 sets, 6 index bits. Both use address bits 6 to 11 as index, so **4096-byte spacing collides in both**; the 8-way cache holds 8 such lines and the 12-way cache holds 12 (which is how the experiment in section 4.2 can tell them apart).

### Problem B

Lines: 0 1 2 3 4 0 1 2 3 4 0 1.

(a) Set = line mod 4, tag = line div 4. Lines 0 and 4 both go to set 0 (tags 0 and 1); lines 1, 2, 3 go to sets 1, 2, 3 (tag 0).

```text
access:   1 2 3 4 5 6 7 8 9 10 11 12
line:     0 1 2 3 4 0 1 2 3  4  0  1
result:   M M M M M M H H H  M  M  H        4 hits, 8 misses
```

Access 5 (line 4) evicts line 0; access 6 (line 0) evicts line 4; accesses 7 to 9 hit lines 1, 2 and 3, which sit alone in their sets; accesses 10 and 11 repeat the 0/4 conflict; access 12 hits line 1.

(b) 2 sets: even lines (0, 2, 4) go to set 0, odd lines (1, 3) to set 1.

```text
result:   M M M M M M H M H  M  M  H        3 hits, 9 misses
```

Set 1 holds both its lines and hits (accesses 7, 9, 12). Set 0 sees 0, 2, 4, 0, 2, 4, 0, a cycle of three lines in two ways, and LRU gets no hit there.

(c) Fully associative LRU: a cycle of 5 lines in 4 slots, so every line is evicted just before reuse: **0 hits**. OPT:

```text
result:   M M M M M H H H M  H  H  H        6 hits
```

(At access 5 OPT evicts line 3, used latest; at access 9 it evicts line 2.)

(d) The loop of 5 lines is the worst case for LRU with 4 slots. In the direct-mapped cache only lines 0 and 4 share a set, so only they thrash; lines 1, 2 and 3 own their slots and hit. A fully associative cache of 5 lines has 5 compulsory misses and then **7 hits** (12 − 5). The lesson: more associativity does not help if the policy is wrong for the pattern, and the damage of one bad pattern is limited when the cache is partitioned into sets.

### Problem C

(a) AMAT = 4 + 0.05 × (12 + 0.20 × 250) = 4 + 0.05 × 62 = **7.1 cycles**. Global L2 miss rate = 0.05 × 0.20 = **1%**, so **10** of every 1,000 accesses reach memory.

(b) 4 + 0.05 × 250 = **16.5 cycles**, 2.3 times worse.

(c) CPI = 0.6 + 0.4 × (7.1 − 4) = 0.6 + 1.24 = **1.84**. Stalls are 1.24 / 1.84 = **67%** of the time.

(d) **X:** AMAT = 4 + 0.05 × (14 + 0.12 × 250) = 4 + 0.05 × 44 = **6.2**; CPI = 0.6 + 0.4 × 2.2 = **1.48**; speed-up 1.84 / 1.48 = **1.24**. **Y:** the penalty after an L2 miss is 40 + 0.5 × 250 = 165, so AMAT = 4 + 0.05 × (12 + 0.2 × 165) = 4 + 0.05 × 45 = **6.25**; CPI = 0.6 + 0.4 × 2.25 = **1.50**; speed-up **1.23**. They are nearly equal on paper. Ask about area and power (a bigger private L2 per core against a shared L3), about the real miss rates of your workload, and whether other cores can use the L3.

(e) (1) It overlaps several misses (memory-level parallelism) instead of waiting for each. (2) It keeps running independent instructions while a miss is outstanding, and the prefetchers turn many misses into hits. The formula is a good model of a pointer chase and a pessimistic one for streaming code.

### Problem D

(a) After filling 0, 1, 2, 3 the bits are root 0, L 0, R 0. Touch 2: root 0, R 1 (points to way 3). Touch 0: root 1, L 1 (points to way 1). Final bits: root 1, L 1, R 1. Follow them: root → right pair, R = 1 → **way 3**. **PLRU evicts way 3.** The use order is now 1, 3, 2, 0 (oldest first), so **true LRU evicts way 1**. **FIFO evicts way 0** (the oldest loaded; hits do not change its order). All three differ.

(b) The array is 4,096 × 4 = 16 KiB = 256 lines and fits in L1. (i) **Write-back + write-allocate:** the first pass misses on each of the 256 lines and reads it (RFO); everything after that hits; the dirty lines are written back once: 256 line writes. Total **512 line transfers** (32 KiB). (ii) **Write-through + no-write-allocate:** the program never reads, so the lines are never allocated and **every store goes to the next level**: 100 × 4,096 = **409,600 four-byte writes**. (A merging write buffer would cut the count, but not to 256.) Write-back wins by a huge margin when lines are reused.

(c) Order (longest prefix first): 12.34.56.0/24, 12.34.0.0/16, 12.0.0.0/8, 0.0.0.0/0. Results: 12.34.56.7 → **3**; 12.34.99.1 → **2**; 12.200.0.1 → **1**; 9.9.9.9 → **4** (default). The range 1000 to 1999 needs **7** entries: 1000 to 1007 (/13), 1008 to 1023 (/12), 1024 to 1535 (/7), 1536 to 1791 (/8), 1792 to 1919 (/9), 1920 to 1983 (/10), 1984 to 1999 (/12); the sizes 8 + 16 + 512 + 256 + 128 + 64 + 16 add to 1,000. The range 2000 to 2999 needs 8, so the two-range rule needs 7 × 8 = **56 entries**.

(d) Examples. A typical x86 laptop or server: line 64 B, L1D 32 or 48 KB, 8 or 12 ways, so 64 sets and 6 index bits; 4096-byte spacing collides. A Mac with Apple silicon reports a 128-byte line, and its performance cores are reported to have a 128 KB L1 data cache; `sysctl` does not show the associativity, so take the 8 ways that reviewers report and say that it is an assumption. Redo the arithmetic with those numbers: the spacing that collides is then different from the 4096 bytes of x86. If `lscpu` is run in a VM, compare with section 4.2 before trusting it.

---

## Cheat sheet

- **Locality:** temporal (reuse) and spatial (neighbours). A cache keeps recent lines and fetches whole lines.
- **Hierarchy:** registers, L1 (32 to 128 KB, 3 to 5 cycles), L2 (12 to 17 cycles), L3 (40 to 80 cycles, shared), DRAM (200 to 400 cycles). SRAM on the chip, DRAM outside (6 transistors against 1 transistor and a capacitor).
- **Measured:** pointer chase gives a staircase: L1 5 cycles, L2 14, L3 77, DRAM 300 to 450; each step 3 to 6 times slower. This VM: L1 48 KB, 12-way (the label said 32 KB, 8-way).
- **Address:** offset = log2(line); index = log2(sets); tag = the rest. `sets = size / (ways x line)`. 32 KB, 8-way, 64 B: 64 sets, 6 + 6 + 36 bits.
- **Mapping:** direct-mapped (1 place, 1 comparator, conflicts), set-associative (N places in a set), fully associative (any place, CAM). Index from the low bits above the offset.
- **Lookup:** index, read the ways, compare tags (valid), mux, offset. Tag, valid and dirty bits add 5 to 7%.
- **Charged per line:** a pass touching every 16th integer does 16 times fewer reads and runs only 1.7 times faster.
- **Power-of-two spacing:** same index; the ring cliff is at ways + 1 (4096 B apart, 12 ways: cliff at 13). Pad by one line.
- **Replacement:** LRU (exact, log2(N!) bits), tree PLRU (N − 1 bits), FIFO, random, OPT (the yardstick), RRIP (re-reference prediction). LRU fails on a loop larger than the cache (0 hits where random gets about 9 of 20).
- **Writes:** write-through (every store goes on) against write-back (dirty bit, write on eviction); write-allocate (read for ownership) against no-write-allocate; write buffer. A fresh 1 MiB write moves 2 MiB with write-back + write-allocate.
- **AMAT** = `T_L1 + m_L1 x (T_L2 + m_L2 x T_mem)`, `m_L2` local. 99% against 95% hit rate: 7 against 19 cycles. `CPI = base + accesses/instr x (AMAT - T_L1)`; out-of-order cores beat it by overlapping misses.
- **CAM and TCAM:** CAM = key in, address out, every entry compares; TCAM adds don't-care bits and picks the first match, so entries sorted longest prefix first give longest-prefix match. Ranges become prefixes (1024 to 65535 = 6 entries, worst case 30; two ranges multiply). Power-hungry, small.

**Next:** tutorial 14, Cache Optimisation and the Real Hierarchy (the 3 Cs, inclusive and exclusive caches, victim caches, non-blocking caches and MSHRs, prefetching, virtually-indexed caches and aliasing, blocking and tiling, data layout, memory types and non-temporal stores, cache partitioning, and finding misses with `perf`). Say "next" when you are ready.
