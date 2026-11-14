# OS 10: Memory management: address spaces, allocation and paging

*Part of the [Operating systems plan](00_plan.md) · Time: about 9 hours (6 reading and experiments, 3 problems) · You need: [OS 02](02_processes.md) (the process address space), [C 03](../02_c_language/03_memory_stack_heap_and_malloc.md) (user-space `malloc`, free lists and fragmentation) and [CO 15](../04_computer_organization/15_virtual_memory_and_the_tlb.md) (the x86-64 walk, the TLB, huge pages, measured)*

This is the classical operating-systems memory chapter: how an operating system gives each process its own address space and how it hands out the physical memory behind it. It covers the schemes in the order they were invented, because each one exists to fix a failure of the one before it: base and bounds, contiguous allocation with its fragmentation, segmentation, paging, and then the page-table structures that make paging affordable. Two other tutorials already hold the machinery this one leans on: CO 15 has the hardware walk, the page-table entry and the measured cost of TLB misses, and C 03 has user-space allocators. Here the work is the OS side and the exam side: translate an address by hand, size a page table, choose a placement policy, split and merge buddy blocks, and compute an effective access time. Interview questions come in a fixed set. Compare first, best and worst fit. What is external fragmentation, and why does paging remove it and leave internal fragmentation? Translate this virtual address with a two-level table. How big is a page table, and why is a flat one impossible for 64-bit addresses? What does an inverted page table trade? How does the buddy allocator work and what does it waste? What is the slab allocator for? Compute the effective access time for a given TLB hit ratio. Every answer below is backed by a program that was compiled and run, and several read the real page tables, the real buddy lists and the real slab caches of this machine.

By the end you can:

- explain logical against physical addresses and binding time, and translate with base and bounds (and say what protection it gives and what it cannot do);
- run first, next, best and worst fit on a hole list by hand, and say what a simulation shows (measured: holes per allocated block came out at 0.43 to 0.51 for every policy, the "50 percent rule"; worst fit failed 4.7 percent of requests at 70 percent use and 88 percent at 85 percent use while first and best fit failed none);
- distinguish internal from external fragmentation and say which each scheme suffers (measured for the buddy allocator: an average of 25.8 percent of each block wasted on random requests, and 50 percent for the worst request size);
- implement split and coalesce in a buddy allocator (a working one is in section 3), find a block's buddy by flipping one address bit, and say when buddies fail to merge (measured: half the memory free and a 64-byte request still fails);
- say what a slab cache is for and read the real caches of this kernel (`task_struct` objects 5,888 bytes, 5 to a slab of 8 pages);
- translate by hand with segmentation and with a two-level page table, naming the fault for each failure (not present, write to read-only, kernel-only, bounds, no such segment);
- compute page-table sizes for flat, two-level, hashed and inverted designs, and read the real page-table entries of a running process through `/proc/self/pagemap` (measured: a page that was only read maps the shared zero page; two views of one shared page have one frame number; a dense 64 MiB region needs 128 KiB of tables, a sparse one with the same data size needs 2,052 KiB);
- compute the effective access time of a TLB for any hit ratio and table depth (a 98 percent hit ratio costs 3 percent with a one-level table and 9 percent with four levels).

**Where the numbers come from.** Programs were compiled with GCC 13.3 (`-O2 -Wall -Wextra`) and run on a Linux 6.18.44 kernel in a Firecracker micro-VM with 2 vCPUs, 8 GiB of memory, 4 KiB pages, as root (the frame numbers in `/proc/self/pagemap` are readable only with `CAP_SYS_ADMIN`). The classical algorithms and their stated properties are from the OSTEP chapters on free-space management and on smaller page tables, and the pagemap bit layout is from the kernel's `pagemap` document (all fetched; see Sources). The 50 percent rule and the textbook placement exercises are from memory except where the program reproduces them. Facts from general knowledge are marked "from memory".

**Data and timing.** Everything here is a count or a size, not a time, except the effective-access-time model, which is arithmetic with stated inputs. The allocation simulations use a fixed pseudo-random seed, so their output is the same on every run. The page-table-memory and buddy-list readings are of a live system and vary slightly between runs (the 1 GiB-spacing line below came out at 64 KiB in one run and 68 KiB in another); where that matters the text says so.

---

## 1. Address spaces and the simplest scheme

### 1.1 Logical and physical addresses

A program refers to **logical** (virtual) addresses; the memory chips see **physical** addresses. Something must translate between them on every access, and it has to be hardware, the **memory-management unit** (MMU), because a software lookup per instruction would be far too slow. The operating system sets up the translation (loads registers or builds tables) and the MMU applies it. Two reasons this indirection exists: **protection** (a process can name only the addresses the OS has mapped for it) and **relocation** (the OS can place a process anywhere in physical memory without the program knowing).

The address a program contains can be bound at three times. At **compile or link time** it is absolute, which is fixed placement and the reason early programs had to be loaded at one address. At **load time** the loader patches the addresses (relocatable code). At **run time** the MMU translates on every access, so the process can be moved while it runs; that is what every modern system does and what the rest of this chapter is about.

### 1.2 Base and bounds

The simplest MMU has two registers per process. The **base** is the physical address where the process's memory starts; the **bound** (or limit) is its size. On each access the hardware checks `vaddr < bound` and, if so, issues `base + vaddr`; otherwise it raises a fault. Both registers are saved and restored on a context switch, and changing them is a privileged operation, so a process cannot escape its region. The program below translates a few addresses for a process with base 16384 and bound 4096:

```text
  vaddr    100 -> physical 16484
  vaddr   4095 -> physical 20479
  vaddr   4096: FAULT, outside bounds (4096 >= 4096)
```

Base and bounds gives protection and relocation at almost no cost, and it has two flaws. The process must occupy **one contiguous** physical region, so the OS must find a big enough hole (section 2). And the whole region is allocated, including the unused gap between the heap and the stack: a process with a 4 KiB code segment, a 4 KiB heap and a 4 KiB stack, spread over a 64 KiB address space, still occupies 64 KiB. That waste inside the process's region is **internal fragmentation** (the space is allocated and unused; this is how the OSTEP address-space chapters describe it, from memory), and segmentation (section 4) and paging (section 5) exist to remove it.

---

## 2. Contiguous allocation and external fragmentation

### 2.1 The hole list

With base and bounds the OS manages physical memory as a set of variable-size regions. Free regions are **holes**. A request for `n` bytes is satisfied from a hole of at least `n`: the used part is taken and the rest stays a (smaller) hole. When a region is freed it is merged (**coalesced**) with free neighbours. The question is which hole to choose. The four classical answers, with what OSTEP says about each:

- **First fit**: the first hole that is big enough. Fast, since it stops early; OSTEP notes it can pollute the start of the list with small leftovers.
- **Next fit**: like first fit but resume the search where the last one stopped, spreading allocations over the list. OSTEP says its performance is similar to first fit.
- **Best fit**: the smallest hole that is big enough. Leaves the smallest leftover, and needs a full scan (OSTEP: a heavy performance penalty for naive versions).
- **Worst fit**: the largest hole, hoping the leftover is still useful. OSTEP: most studies show it performs badly.

### 2.2 A simulation

The program keeps a 1,000,000-unit arena as a list of segments in address order, splits on allocation and coalesces on free, and runs 300,000 requests. Request sizes are uniform from 1 to a maximum; the system is kept at a fixed fraction of use by freeing random live blocks before each allocation when it would go over. Because use stays below the target, **every request is for fewer bytes than are free in total**, so a failure is by construction an external fragmentation failure: enough memory, no single hole big enough. The part that matters is the placement code:

```c
static int find(int policy, long size)                  /* 0 first, 1 next, 2 best, 3 worst; -1 if none */
{
    int best = -1;
    int start = policy == 1 ? next_idx % nseg : 0;
    for (int k = 0; k < nseg; k++) {
        int i = (start + k) % nseg;
        steps++;
        if (seg[i].used || seg[i].size < size) continue;
        if (policy == 0 || policy == 1) return i;
        if (best < 0 || (policy == 2 ? seg[i].size < seg[best].size : seg[i].size > seg[best].size)) best = i;
    }
    return best;
}

static int place(int policy, long size)                 /* returns the segment's start address, or -1 */
{
    int i = find(policy, size);
    if (i < 0) return -1;
    long start = seg[i].start;
    if (seg[i].size > size) {                           /* split: the used part first, the rest stays free */
        insert_at(i + 1);
        seg[i + 1].start = start + size; seg[i + 1].size = seg[i].size - size; seg[i + 1].used = 0;
        seg[i].size = size;
    }
    seg[i].used = 1;
    next_idx = i + 1;
    return (int)start;
}

static void release(long start)                         /* free and coalesce with both neighbours */
{
    int i = 0;
    while (seg[i].start != start) i++;
    seg[i].used = 0;
    if (i + 1 < nseg && !seg[i + 1].used) { seg[i].size += seg[i + 1].size; remove_at(i + 1); if (next_idx > i + 1) next_idx--; }
    if (i > 0 && !seg[i - 1].used) { seg[i - 1].size += seg[i].size; remove_at(i); if (next_idx > i) next_idx--; }
}
```

The first table is requests of 1 to 2,000 units at 70 percent use; the second, the same at 85 percent; the third, requests of 1 to 20,000 at 70 percent. Columns: failed requests per 1,000; holes per allocated block, averaged over the run; the share of all free memory that sits in the three biggest holes; the average number of list segments examined per request; and the average size of the largest hole.

```text
arena 1000000, requests uniform 1..2000, target use 70%
policy fail/1000 holes/blk free in 3 biggest holes steps/request largest hole
first       0.00      0.50                    69%        521.3       199846
next        0.00      0.50                     5%          5.9         5641
best        0.00      0.46                    82%       1015.5       233900
worst      46.78      0.50                     2%       1086.8         1752
```

```text
arena 1000000, requests uniform 1..2000, target use 85%
policy fail/1000 holes/blk free in 3 biggest holes steps/request largest hole
first       0.00      0.50                    28%        630.2        32847
next       35.49      0.50                     4%        146.0         2182
best        0.00      0.46                    59%       1227.4        77392
worst     879.88      0.43                     0%       8079.2          153
```

```text
arena 1000000, requests uniform 1..20000, target use 70%
policy fail/1000 holes/blk free in 3 biggest holes steps/request largest hole
first       0.00      0.51                    62%         53.0       139688
next        0.45      0.51                    28%          5.5        36111
best        0.00      0.51                    72%        103.8       165369
worst      56.49      0.51                    15%        109.1        17038
```

What the three tables show:

- **The 50 percent rule.** Holes per allocated block is **0.43 to 0.51 in every row of every policy**. Knuth's result (from memory) says that in steady state, with first fit, the number of holes is about half the number of allocated blocks, (the usual argument is that, in steady state, the holes created by frees and the holes removed by allocations and merges balance at that ratio). The simulation reproduces it and shows that it holds for the other policies as well. Its textbook consequence (from memory): with N allocated blocks about N/2 holes exist, and if holes are about the size of blocks, about a third of the memory is unusable.
- **First fit and best fit never failed** in any of the three runs at these sizes. Best fit kept the largest holes (233,900 units on average at 70 percent against 199,846 for first fit, and 77,392 against 32,847 at 85 percent) because it picks the tightest fit and leaves big holes alone, but it examined about twice as many segments per request (1,015 against 521) as it needs the whole list.
- **Next fit is cheap and spreads fragmentation.** It examined 5.9 segments per request, almost a hundred times fewer than first fit, and its largest hole was tiny (5,641 units at 70 percent, 2,182 at 85 percent); at 85 percent use it failed 3.5 percent of requests (35.49 per 1,000). It chops up the big hole at the end of the list instead of reusing the small holes at the front.
- **Worst fit is the worst.** At 70 percent use it failed 4.7 percent of requests (46.78 per 1,000) and held only 1,752 units in its largest hole, and at 85 percent use it failed **88 percent** of them. By always splitting the biggest hole it destroys the large holes that big requests need, which is exactly the effect OSTEP warns about.

These are one workload each (uniform sizes, random lifetimes); real allocation patterns differ, and OSTEP's own summary is that no strategy is best for every request stream.

### 2.3 Two kinds of fragmentation

- **External fragmentation**: the free memory exists but is split into holes too small for the request. Contiguous allocation, segmentation and any variable-size allocator suffer from it. The table above is a measurement of it.
- **Internal fragmentation**: memory inside an allocated unit is wasted, because the unit is bigger than the request. Fixed-size schemes suffer from it: paging (the unused tail of a process's last page), the buddy allocator (power-of-two rounding, section 3.2), and slab caches (objects that do not fill the slab).

The two trade against each other. Making the allocation unit small lowers internal waste and raises the count of units to manage; making it large does the opposite.

### 2.4 Compaction and swapping

**Compaction** slides all allocated regions to one end of memory so the holes merge into one. It needs every region to be **relocatable at run time** (that is why binding must be at run time; with base and bounds the OS only updates the base register), it costs a memory copy of everything moved, and the process cannot run during its own move. The simulation gives the idea of the cost: at 70 percent use of this 1,000,000-unit arena, up to 700,000 units would move, and that is why compaction is rare and why paging was invented, which makes it unnecessary. **Swapping** moves a whole process to disk when memory is short and brings it back later; its cost is the process's size over the disk's bandwidth (a 100 MiB process on a disk moving 100 MiB per second takes a second each way; from memory, and this is arithmetic, not a measurement). Modern systems swap pages, not processes ([OS 11](00_plan.md)).

---

## 3. Free-space management beyond the hole list

The hole list of section 2 is a linked list of free regions with a scan per request. The physical memory of a real kernel needs something faster and with less external fragmentation. The options, in the order they appear in OS courses:

### 3.1 Bitmaps and free lists

For **fixed-size units** (page frames) a **bitmap** holds one bit per frame (1 = in use). Allocation scans for a zero bit, freeing clears one; the bitmap is tiny (for 4 KiB frames, one bit per 4 KiB is 0.003 percent of memory) and external fragmentation does not exist for single-frame requests, but finding a run of several contiguous free frames means scanning. A **free list** threads the free frames through themselves (each free frame holds the address of the next), which makes single-frame allocation O(1) and costs no extra memory, at the price of no cheap way to find contiguous runs. A variable-size free list with headers, splitting and coalescing is the user-space `malloc` of [C 03](../02_c_language/03_memory_stack_heap_and_malloc.md); OSTEP's chapter adds the standard header layout (a size field and a magic number just before the returned pointer, so `free` finds the size) and the point that a request for `n` bytes searches for `n` plus the header.

### 3.2 The buddy allocator

The **buddy allocator** is the compromise the Linux kernel uses for its physical page allocator. Memory is a block of `2^N` units. A request is rounded up to a power of two; if no free block of that size exists, a larger block is **split in halves** repeatedly until one of the right size appears (the other halves go on free lists of their own sizes). When a block is freed, the allocator checks whether its **buddy**, the other half of the block it was split from, is also free; if so the two merge into one block of twice the size, and the check repeats upward. The reason this is cheap: a block's buddy differs from it in exactly one address bit (bit `k` for a block of size `2^k`), so the buddy's address is `offset XOR size`, and one word per block records whether it is free.

The working code, with the free lists stored inside the free blocks themselves:

```c
static int order_for(long size)                         /* smallest order whose block holds size bytes */
{
    int o = MINORD;
    while ((1L << o) < size) o++;
    return o;
}

static long buddy_alloc(int ord)                        /* returns an offset, or -1 */
{
    int o = ord;
    while (o <= MAXORD && free_head[o] < 0) o++;        /* find the smallest free block that is big enough */
    if (o > MAXORD) return -1;
    long off = free_head[o];
    list_del(o, off);
    while (o > ord) {                                   /* split: keep the low half, free the high half */
        o--;
        list_add(o, off + (1L << o));
    }
    return off;
}

static void buddy_free(long off, int ord)
{
    while (ord < MAXORD) {
        long buddy = off ^ (1L << ord);                 /* the buddy differs in exactly one address bit */
        if (free_ord[buddy >> MINORD] != ord) break;    /* buddy is in use or split: stop merging */
        list_del(ord, buddy);
        if (buddy < off) off = buddy;
        ord++;
    }
    list_add(ord, off);
}
```

The program runs a 1 MiB arena with 32-byte minimum blocks and does four things: shows the internal waste for some request sizes, measures it over random requests, checks that split and merge are inverses, and builds the worst case for external fragmentation.

```text
request -> block (internal waste)
      33 ->      64  waste  48.4%
      64 ->      64  waste   0.0%
      65 ->     128  waste  49.2%
     100 ->     128  waste  21.9%
    1000 ->    1024  waste   2.3%
    1025 ->    2048  waste  50.0%
    4096 ->    4096  waste   0.0%
    4097 ->    8192  waste  50.0%
   70000 ->  131072  waste  46.6%
uniform requests 1..1000: average waste 25.8% of the blocks handed out
after 1024 allocations of 1 KiB (the whole arena):  free blocks by order (32 B ... 1 MiB): 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0
after freeing them all:  free blocks by order (32 B ... 1 MiB): 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1
arena full of 32 B blocks (32768), every other one freed: free bytes 524288, a 64 B request FAILS (no two buddies are both free)
after freeing the rest:  free blocks by order (32 B ... 1 MiB): 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1
```

- **Internal fragmentation is the price.** A request just above a power of two wastes almost half its block (33 bytes get 64, 65 get 128, 1,025 get 2,048, 4,097 get 8,192: 48 to 50 percent), and one just below wastes nothing (64, 4,096). Averaged over random requests of 1 to 1,000 bytes the waste was **25.8 percent of the memory handed out**. This is what OSTEP means by "this scheme can suffer from internal fragmentation".
- **Split and merge are inverses.** Taking 1,024 blocks of 1 KiB uses the whole arena (all free counts zero); giving them all back leaves one free block of 1 MiB, the order-20 entry. Every merge found its buddy.
- **External fragmentation still exists.** The last experiment fills the arena with 32-byte blocks, frees every other one, and asks for 64 bytes. Half the memory (524,288 bytes) is free and the request **fails**: each free block's buddy is in use, so no two free blocks are adjacent buddies and nothing merged. The fix is the same as everywhere: free the others and the arena becomes one block again (the last line).

**In the real kernel.** Linux keeps free lists per order for each memory zone, from order 0 (one 4 KiB page) to order 10 (4 MiB), the eleven columns below, and `/proc/buddyinfo` prints the number of free blocks on each. This is the machine's own, with the memory those counts represent computed from them:

```text
zone Normal free blocks by order (order 0 = 4 KiB ... order 10 = 4 MiB):
  count: 5 31 119 142 233 213 125 57 23 13 63
  KiB:   20 248 1904 4544 14912 27264 32000 29184 23552 26624 258048
  total free 418300 KiB; blocks of order 0 and 1 hold 0.1% of the free memory
```

Most of the free memory is in the biggest blocks, and very little in the smallest, as expected on a mostly idle system; on a long-running loaded machine the large orders drain and a request for a high-order contiguous block (a huge page, a driver's DMA buffer) can fail even with plenty of memory free, which is the buddy allocator's external fragmentation made visible (from memory: this is what `/proc/buddyinfo` is usually read for, and why the kernel has compaction for movable pages).

### 3.3 Segregated lists and the slab allocator

A **segregated list** dedicates a free list to one popular request size, so an allocation is a pop and a free is a push, with no search and no splitting. The question OSTEP raises is how much memory to give each size. The **slab allocator** (Bonwick, for the Solaris kernel, OSTEP's account) applies the idea to **kernel objects**: for each frequently used type (a lock, an inode, a process descriptor) there is an **object cache**. A cache gets memory from the page allocator in **slabs** (one or more contiguous pages) and carves each slab into objects of exactly the type's size. Objects freed go back to the cache **already initialised**, which saves the cost of constructing them again. A slab whose objects are all free can be returned to the page allocator. The result: no internal fragmentation inside a slab beyond the remainder, no external fragmentation among objects, and very fast allocation.

The kernel's own caches (`/proc/slabinfo`, readable as root) show the structure. For each cache, the object size, the number of objects per slab, the pages per slab, and the bytes left over at the end of each slab (computed: `pages * 4096 - objects * size`):

```text
name objsize objs/slab pages/slab unused-bytes-per-slab (page size 4096)
inode_cache         624   13   2     80
dentry              192   21   1     64
vm_area_struct      192   21   1     64
files_cache         704   23   4    192
task_struct        5888    5   8   3328
pid                 192   21   1     64
mm_struct          1600   20   8    768
kmalloc-4k         4096    8   8      0
kmalloc-2k         2048   16   8      0
kmalloc-1k         1024   16   4      0
kmalloc-512         512   16   2      0
kmalloc-256         256   16   1      0
kmalloc-128         128   32   1      0
kmalloc-64           64   64   1      0
kmalloc-32           32  128   1      0
```

Read `task_struct`: 5,888 bytes per object, 5 per slab, slabs of 8 pages (32,768 bytes), so 3,328 bytes (10 percent) per slab are the remainder. `dentry`, `vm_area_struct` and `pid` are 192-byte objects, 21 to a single page, with 64 bytes of waste. The generic `kmalloc-N` caches are the kernel's segregated lists: sizes in powers of two (the table shows 32 bytes to 4 KiB; a `kmalloc(100)` is served from `kmalloc-128`, wasting 28 bytes: internal fragmentation again), with zero waste per slab for these power-of-two sizes. The amount of memory the kernel holds in slabs right now, with the memory spent on page tables (section 6) beside it:

```text
MemTotal:        8216668 kB
Slab:              34292 kB
SReclaimable:      14828 kB
SUnreclaim:        19464 kB
PageTables:         5420 kB
```

---

## 4. Segmentation

Base and bounds gives one region per process. **Segmentation** gives each **segment** (code, heap, stack, and in general any logical unit) its own base and limit, so a process occupies several smaller regions, and the gap between the heap and the stack costs nothing. The virtual address is split into a **segment number** and an **offset**; the hardware looks the segment up in a **segment table**, checks `offset < limit`, checks the segment's protection bits, and forms `base + offset`. The stack segment records that it **grows downward**, so its offset is counted from the top and the hardware subtracts. Sharing is natural (two processes can map the same code segment) and protection is per segment (code read and execute, heap read and write).

The program below uses a 14-bit address, the top 2 bits for the segment and the low 12 for the offset, with code at 32,768 (2 KiB), heap at 34,816 (3 KiB) and a 2 KiB stack at 28,672 growing downward, and tries seven accesses:

```c
static void seg_translate(unsigned vaddr, int write)
{
    unsigned s = vaddr >> 12, off = vaddr & 0xfff;
    segment_t *g = &seg[s];
    printf("  vaddr %5u = segment %u (%s), offset %4u: ", vaddr, s, g->name, off);
    if (g->limit == 0) { printf("FAULT, no such segment\n"); return; }
    long eff = off;
    if (g->grows_down) {
        eff = (long)off - 4096;                          /* offset counts down from the top of a 4 KiB region */
        if (-eff > (long)g->limit) { printf("FAULT, beyond the stack's limit\n"); return; }
    } else if (off >= g->limit) { printf("FAULT, offset %u beyond limit %lu\n", off, g->limit); return; }
    if (write && !(g->prot & 2)) { printf("FAULT, write to a read-only segment\n"); return; }
    printf("physical %lu\n", g->base + eff);
}
```

```text
segmentation: code at 32768 (2 KiB), heap at 34816 (3 KiB), stack at 28672 (2 KiB, grows down)
  vaddr   100 = segment 0 (code), offset  100: physical 32868
  vaddr   100 = segment 0 (code), offset  100: FAULT, write to a read-only segment
  vaddr  4396 = segment 1 (heap), offset  300: physical 35116
  vaddr  7196 = segment 1 (heap), offset 3100: FAULT, offset 3100 beyond limit 3072
  vaddr  8292 = segment 2 ((none)), offset  100: FAULT, no such segment
  vaddr 16288 = segment 3 (stack), offset 4000: physical 28576
  vaddr 13288 = segment 3 (stack), offset 1000: FAULT, beyond the stack's limit
```

Each access shows one outcome: an offset inside its segment translates; a **write to the code segment** faults on protection; an offset past the heap's limit faults on bounds; the unused segment number faults; a stack address near the top translates (96 bytes below the top maps to 28,576), and one 3,096 bytes below the top is past the stack's 2 KiB limit and faults, which is where a real system would grow the stack instead of killing the process.

Segmentation brings back external fragmentation, because segments are variable size and must be contiguous; it also needs the compiler or loader to cut the program into segments. It survives today in two forms: the x86 segment registers (nearly unused in 64-bit mode, where `fs` and `gs` serve for thread-local storage; from memory) and, on every system, the **virtual memory areas** the kernel keeps per process: the list in `/proc/self/maps`, with an address range and permissions per region, is segmentation's logical idea implemented on top of paging.

---

## 5. Paging

### 5.1 The idea and the arithmetic

Paging cuts the **virtual** address space into fixed-size **pages** and **physical** memory into frames of the same size, and a **page table** maps page numbers to frame numbers. Any page can go in any frame, so there is **no external fragmentation** at all, and no need for contiguity. For a page size of `2^k` bytes the low `k` bits of an address are the **offset** and the high bits are the **virtual page number** (VPN); translation replaces the VPN with the frame number and keeps the offset: `physical = frame * page_size + offset`. This is the arithmetic every exam asks for; the program below does it with a two-level table (section 6) and a 32-bit address of 10 + 10 + 12 bits, printing the split and the entry's checks.

```c
static void translate(uint32_t va, int write, int user)
{
    uint32_t d = va >> 22, t = (va >> 12) & 1023, off = va & 4095;
    printf("va 0x%08x = dir %3u, table %4u, offset 0x%03x: ", va, d, t, off);
    if (!(pdir[d] & P_VALID)) { printf("FAULT, directory entry %u not valid\n", d); return; }
    uint32_t pte = tables[(pdir[d] >> 12) - 100][t];
    if (!(pte & P_VALID)) { printf("FAULT, page not present\n"); return; }
    if (write && !(pte & P_WRITE)) { printf("FAULT, write to a read-only page\n"); return; }
    if (user && !(pte & P_USER)) { printf("FAULT, kernel-only page\n"); return; }
    printf("frame %u -> pa 0x%08x\n", pte >> 12, ((pte >> 12) << 12) | off);
}
```

The program maps four pages (VPN 0x400 to frame 7 writable, 0x401 to frame 8 read-only, 0x402 to frame 12 kernel-only, 0x7ffff to frame 300 writable) and tries seven accesses:

```text
va 0x00400123 = dir   1, table    0, offset 0x123: frame 7 -> pa 0x00007123
va 0x00401ffc = dir   1, table    1, offset 0xffc: frame 8 -> pa 0x00008ffc
va 0x00401ffc = dir   1, table    1, offset 0xffc: FAULT, write to a read-only page
va 0x00402010 = dir   1, table    2, offset 0x010: FAULT, kernel-only page
va 0x00403000 = dir   1, table    3, offset 0x000: FAULT, page not present
va 0x7ffff0ab = dir 511, table 1023, offset 0x0ab: frame 300 -> pa 0x0012c0ab
va 0x10000000 = dir  64, table    0, offset 0x000: FAULT, directory entry 64 not valid
page-table sizes, 32-bit address space, 4 KiB pages:
  one-level table, 4-byte entries         1048576 entries x 4 B =       4096 KiB
  same with 16 KiB pages                   262144 entries x 4 B =       1024 KiB
  same with 1 MiB pages (huge)               4096 entries x 4 B =         16 KiB
this process maps 4 pages in 2 regions; two-level tables used: 1 directory (4 KiB) + 2 tables (8 KiB) = 12 KiB
48-bit address space, 4 KiB pages, 8-byte entries (x86-64):
  one-level table                     68719476736 entries x 8 B =  536870912 KiB
  a 4-level tree allocates only the tables on the paths to pages that are mapped
```

Walk through the first three by hand to check the method. `0x00400123` is `0000 0000 0100 0000 0000 0001 0010 0011` in binary: the top 10 bits are 1 (directory entry 1), the next 10 are 0 (table entry 0), the low 12 are `0x123`. The entry maps to frame 7, so the physical address is `7 * 4096 + 0x123 = 0x7000 + 0x123 = 0x7123`. The second address, `0x00401ffc`, has table entry 1 and offset `0xffc` and maps to frame 8, giving `0x8ffc`; the same access as a **write** faults because the entry is read-only. The page at `0x00402010` is present but marked kernel-only, so a user access faults; `0x00403000` has no valid entry; and `0x10000000` has a directory index of 64 whose directory entry is not valid, so no second-level table exists for it at all.

### 5.2 Fragmentation, page size and the page table

Paging removes external fragmentation and leaves **internal fragmentation in the last page** of each region: on average half a page per region (from memory; it follows from sizes being uniform modulo the page size). The choice of page size is the classic trade-off. For a process of size `s`, page size `p` and page-table entry size `e`, the memory overhead is about `s*e/p` for the table plus `p/2` of wasted tail. The minimum is at `p = sqrt(2*s*e)`; with `s = 1 MiB` and `e = 4 bytes` that is about 2,896 bytes, so 2 KiB or 4 KiB pages (from memory; this is the textbook derivation and the optimum is a flat one, so the number matters less than the shape). Smaller pages mean bigger tables and more TLB pressure; larger pages mean more internal waste. Real systems pick 4 KiB (x86-64, Arm) and use huge pages (2 MiB, 1 GiB) where the data is big and dense ([CO 15](../04_computer_organization/15_virtual_memory_and_the_tlb.md) has the costs and the measurements).

### 5.3 What is in a page-table entry

A page-table entry holds the frame number and a few control bits (CO 15 has the exact x86-64 layout). The ones every OS course asks for:

- **Valid or present**: whether the page is in memory. A clear bit with the mapping otherwise intact means the page is on disk or has not been allocated yet; the access raises a **page fault** and the OS decides which ([OS 11](00_plan.md)).
- **Protection**: read, write, execute, and the user/supervisor bit.
- **Reference (accessed)**: set by hardware on any use; the replacement algorithms of OS 11 read it.
- **Dirty (modified)**: set by hardware on a write; a clean page can be dropped without writing it back.
- **Caching control and global** bits, and on x86-64 a no-execute bit.

### 5.4 The real thing: reading a running process's page tables

On Linux `/proc/self/pagemap` has one 64-bit entry per virtual page of the process: bit 63 present, bit 62 swapped, bit 61 file-backed or shared anonymous, bit 56 exclusively mapped, bits 0 to 54 the frame number (the kernel `pagemap` document; since Linux 4.2 the frame number reads as zero for processes without `CAP_SYS_ADMIN`, a Rowhammer mitigation, so this ran as root). The program prints, for several addresses, the four 9-bit indexes the x86-64 walk uses (CO 15 section 3.1), the page offset, and the pagemap entry:

```c
static uint64_t entry(void *p)
{
    uint64_t e = 0;
    off_t off = (off_t)((uintptr_t)p / 4096) * 8;
    if (pread(pm_fd, &e, 8, off) != 8) { perror("pread pagemap"); exit(1); }
    return e;
}

static void show(const char *label, void *p)
{
    uint64_t e = entry(p);
    unsigned long va = (unsigned long)p;
    printf("%-26s va 0x%012lx  idx %3lu/%3lu/%3lu/%3lu +0x%03lx  ", label, va,
           (va >> 39) & 511, (va >> 30) & 511, (va >> 21) & 511, (va >> 12) & 511, va & 4095);
    if (e >> 63) printf("present  pfn 0x%llx  pa 0x%llx  exclusive %d  file/shared %d\n", (unsigned long long)(e & ((1ULL << 55) - 1)),
                        (unsigned long long)((e & ((1ULL << 55) - 1)) * 4096 + (va & 4095)), (int)((e >> 56) & 1), (int)((e >> 61) & 1));
    else printf("not present (swapped %d)\n", (int)((e >> 62) & 1));
}
```

```text
fresh anonymous page 0     va 0x7f84243aa000  idx 255/ 16/289/426 +0x000  not present (swapped 0)
after writing page 0       va 0x7f84243aa000  idx 255/ 16/289/426 +0x000  present  pfn 0x11a961  pa 0x11a961000  exclusive 1  file/shared 0
page 1 after only reading  va 0x7f84243ab000  idx 255/ 16/289/427 +0x000  present  pfn 0x3b33  pa 0x3b33000  exclusive 0  file/shared 0
page 1 after writing       va 0x7f84243ab000  idx 255/ 16/289/427 +0x000  present  pfn 0x1288ce  pa 0x1288ce000  exclusive 1  file/shared 0
page 1, address +100       va 0x7f84243ab064  idx 255/ 16/289/427 +0x064  present  pfn 0x1288ce  pa 0x1288ce064  exclusive 1  file/shared 0
shared mapping, first view va 0x7f84243a9000  idx 255/ 16/289/425 +0x000  present  pfn 0x1053bd  pa 0x1053bd000  exclusive 1  file/shared 1
second view reads 42
shared mapping, second view va 0x7f84243a8000  idx 255/ 16/289/424 +0x000  present  pfn 0x1053bd  pa 0x1053bd000  exclusive 0  file/shared 1
a global variable          va 0x55edfbf56010  idx 171/439/479/342 +0x010  present  pfn 0x12bf04  pa 0x12bf04010  exclusive 1  file/shared 0
a stack variable           va 0x7ffd9bd91e84  idx 255/502/222/401 +0xe84  present  pfn 0x105080  pa 0x105080e84  exclusive 1  file/shared 0
```

Read it line by line:

- **A fresh anonymous page is not present.** The kernel allocates virtual address space on `mmap` and the frame on first touch (demand paging, OS 11). After the write, the page is present with a frame number and `exclusive 1`.
- **A page that was only read is present but is a different, shared frame.** Page 1 after only a read maps frame `0x3b33` with `exclusive 0`, which is the system's shared **zero page** (from memory: the kernel maps one read-only zero frame for read faults on untouched anonymous memory). After the **write**, the page has its own frame and `exclusive 1`: the write fault replaced the mapping, which is copy-on-write in its simplest form.
- **The offset is untouched by translation.** Address `+100` in page 1 maps to the same frame with physical address ending `...064`: frame number times 4,096 plus the offset, exactly the formula of 5.1 (the printed `pa` is that product).
- **Two virtual pages, one frame.** The shared mapping of a `memfd` is mapped twice, at two virtual addresses. Both entries show the same frame number (`0x1053bd` in this run) and the file/shared bit; writing 42 through one view and reading it through the other gives 42. This is how shared memory and shared libraries work at the hardware level.
- **Ordinary variables** (a global in the data segment, a local on the stack) are present, each in its own frame, and their four indexes show the huge gap between the two regions in the virtual address space (a top-level index of about 170 for the data segment against 255 for the stack): a flat table would span the whole gap, and the tree does not (section 6).

---

## 6. Page-table structures

### 6.1 Why a flat table is impossible

A **linear** (flat) page table has one entry per virtual page. With 32-bit addresses and 4 KiB pages that is 2^20 entries, and with 4-byte entries **4 MiB per process**, regardless of how little of the address space the process uses (OSTEP's arithmetic; the program recomputes it). Bigger pages shrink it (16 KiB pages: 1 MiB), at the cost of more internal fragmentation. For 48-bit addresses the flat table would hold 2^36 entries, **512 GiB per process**, which is why every 64-bit system uses something else.

### 6.2 Multi-level tables

Cut the flat table into page-sized pieces and keep a **page directory** with one entry per piece; mark a directory entry invalid when its whole piece is unused, and **never allocate** that piece. A virtual address is split into one index per level plus the offset: 10 + 10 + 12 for the 32-bit two-level case above, and 9 + 9 + 9 + 9 + 12 for x86-64's four levels (each table is one 4 KiB page of 512 eight-byte entries). OSTEP's small example makes the saving concrete: a 16 KiB address space with 64-byte pages needs a 256-entry linear table (1 KB, in 16 page-sized pieces), and with a two-level split of 4 + 4 + 6 bits only three pages are allocated (one directory and two table pieces) instead of sixteen, because only two of the pieces hold valid entries. The process of the earlier program mapped 4 pages in 2 regions and used a directory (4 KiB) and two tables (8 KiB), 12 KiB, against the 4,096 KiB of a flat table.

The **time-space trade-off** (OSTEP): a TLB hit costs the same as before; a TLB miss now needs one memory read per level instead of one, so two reads for a two-level table and four for x86-64. The design is also more complex. Smaller tables are bought with slower misses, which is why the hit ratio of section 7 matters so much.

### 6.3 Measured: what tables cost on the real kernel

`/proc/self/status` reports the memory the kernel uses for the process's page tables as `VmPTE` (on this kernel it counts all levels; `VmPMD` does not exist). The program maps a region, touches pages in a pattern, and reads the difference. THP is switched off for the region so that the numbers are for 4 KiB pages:

```c
static void scenario(const char *label, size_t range, size_t stride)
{
    long pte0 = status_kb("VmPTE:");
    char *p = mmap(NULL, range, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    madvise(p, range, MADV_NOHUGEPAGE);
    long touched = 0;
    for (size_t off = 0; off < range; off += stride) { p[off] = 1; touched++; }
    long pte1 = status_kb("VmPTE:");
    printf("%-30s range %5zu MiB, pages touched %6ld (%6ld KiB of data): page-table memory +%5ld KiB\n",
           label, range >> 20, touched, touched * 4, pte1 - pte0);
    munmap(p, range);
}
```

```text
dense: every page of 64 MiB    range    64 MiB, pages touched  16384 ( 65536 KiB of data): page-table memory +  128 KiB
sparse: one page per 2 MiB     range  1024 MiB, pages touched    512 (  2048 KiB of data): page-table memory + 2056 KiB
sparse: one page per 1 GiB     range  8192 MiB, pages touched      8 (    32 KiB of data): page-table memory +   68 KiB
a flat table for a 1 GiB range would hold 262144 entries x 8 B = 2048 KiB whatever is touched
```

```text
dense: every page of 64 MiB    range    64 MiB, pages touched  16384 ( 65536 KiB of data): page-table memory +  128 KiB
sparse: one page per 2 MiB     range  1024 MiB, pages touched    512 (  2048 KiB of data): page-table memory + 2052 KiB
sparse: one page per 1 GiB     range  8192 MiB, pages touched      8 (    32 KiB of data): page-table memory +   64 KiB
a flat table for a 1 GiB range would hold 262144 entries x 8 B = 2048 KiB whatever is touched
```

- **Dense use is cheap.** Touching every page of 64 MiB (16,384 pages, 64 MiB of data) cost 128 KiB of tables: each 4 KiB page-table page holds 512 entries and covers 2 MiB, so 32 of them, and 0.2 percent of the data size.
- **Sparse use is expensive, per byte of data.** Touching one page in every 2 MiB over 1 GiB (512 pages, 2 MiB of data) cost **2,052 KiB** of tables for 2,048 KiB of data, about one byte of table for every byte of data (every touched page needs its own page-table page, 4 KiB of table to map one 4 KiB page), against 0.2 percent in the dense case. That equals what a flat table for that 1 GiB range would hold (262,144 entries of 8 bytes, 2,048 KiB), so a tree has no advantage when the use is that sparse; its advantage is for the **gaps**.
- **Gaps cost nothing.** Touching eight pages spaced 1 GiB apart over 8 GiB (8 pages, 32 KiB of data) cost 64 to 68 KiB, consistent with one page-table page and one next-level page per touched page (16 pages of 4 KiB; the extra 4 KiB in one run is probably one more upper-level page, from memory). A flat table for 8 GiB would be 16,384 KiB. The tree charged only for the paths to the pages that exist.

### 6.4 Hashed and inverted page tables

For very large address spaces other structures are used.

- A **hashed page table** hashes the virtual page number into a table of chains, each entry holding the VPN, the frame and a next pointer. Its size follows the amount of memory mapped, not the size of the address space, and lookup takes one hash plus a short chain walk. It is used where addresses are sparse (from memory: some 64-bit architectures and software TLB refills).
- An **inverted page table** has **one entry per physical frame**, recording which process and virtual page occupy it (OSTEP gives PowerPC as the example). Its size is proportional to physical memory, shared by all processes. Lookup has to search for the entry that holds the pair (process id, VPN), so in practice a hash table is built on top. The program below builds a 4,096-frame inverted table with a hash chain through the entries and measures the lookups:

```c
static void insert(int frame, uint32_t pid, uint32_t vpn)
{
    unsigned h = hash(pid, vpn);
    ipt[frame].pid = pid; ipt[frame].vpn = vpn; ipt[frame].used = 1;
    ipt[frame].next = hash_anchor[h]; hash_anchor[h] = frame;
}

static int lookup(uint32_t pid, uint32_t vpn)           /* returns the frame, or -1 (page fault) */
{
    lookups++;
    for (int f = hash_anchor[hash(pid, vpn)]; f >= 0; f = ipt[f].next) {
        probes++;
        if (ipt[f].pid == pid && ipt[f].vpn == vpn) return f;
    }
    return -1;
}
```

```text
frames in use   25%: average chain entries examined per successful lookup 1.11
frames in use   50%: average chain entries examined per successful lookup 1.24
frames in use   75%: average chain entries examined per successful lookup 1.38
frames in use  100%: average chain entries examined per successful lookup 1.49
8 GiB of RAM, 4 KiB pages: 2097152 frames; inverted table at 16 bytes per frame = 32768 KiB for ALL processes
per-process flat tables for 48-bit spaces would need 2^36 entries x 8 B = 512 GiB each; 100 processes of 32-bit spaces with flat 4-byte tables: 400 MiB
```

The average number of entries examined per successful lookup was 1.1 at 25 percent of frames in use, and 1.5 when all frames were in use: close to `1 + load/2`, the expected chain length of a well-spread hash with chaining (from memory). For 8 GiB of RAM with 4 KiB pages (2,097,152 frames) the table at 16 bytes per entry would be 32 MiB, **for all processes together**, against 4 MiB per process for flat 32-bit tables (400 MiB for 100 processes) and hundreds of gigabytes per process for flat 48-bit tables. The costs: shared pages are awkward (one frame can have only one entry, so a page shared by several processes needs extra structure), every miss searches a chain, and the table is not indexed in a way hardware can walk cheaply. That is why mainstream x86-64 and Arm systems use multi-level trees and treat the inverted design as a niche idea.

### 6.5 Hybrid and swappable tables

One more idea from OSTEP: **hybrid** paging and segmentation gave each segment its own page table with a base and a bound counting its valid pages, so the gaps between segments needed no table space; it still assumed segments and reintroduced variable-size tables and their external fragmentation, which is why multi-level tables won. And page tables themselves can live in **kernel virtual memory** and be paged out to disk under pressure (OSTEP's VAX/VMS case study), at the price of extra faults.

---

## 7. The TLB and effective access time

Every translation needs the page-table entry, and walking the table costs memory reads. The **translation lookaside buffer** (TLB) caches recent translations in a small associative memory: on a **hit** the frame number comes from the TLB and the access costs a TLB probe plus one memory access; on a **miss** the hardware (or the OS, on some architectures) walks the table, adds the translation to the TLB, and the access completes. CO 15 explains the structure and the real costs, including the measured per-hop cost of a TLB miss on this class of machine, and OS 12's older note has the TLB-reach measurements; this section is the arithmetic the exams use.

With a TLB access time `t`, a memory access time `m`, a hit ratio `h` and a table with `L` levels:

```
hit:   t + m                              (probe, then the data)
miss:  t + L*m + m                        (probe, one read per level, then the data)
EAT  = h * (t + m) + (1 - h) * (t + (L + 1) * m)
```

```c
static double eat(double hit, double tlb_ns, double mem_ns, int levels)
{
    double miss_cost = tlb_ns + (levels + 1) * mem_ns;      /* TLB probe, one memory read per level, then the data */
    double hit_cost = tlb_ns + mem_ns;
    return hit * hit_cost + (1 - hit) * miss_cost;
}
```

The program evaluates it for `t = 1 ns` and `m = 100 ns`:

```text
TLB lookup 1 ns, memory access 100 ns
hit ratio       1 level     2 levels     4 levels
0.800          121.0 ns     141.0 ns     181.0 ns
0.900          111.0 ns     121.0 ns     141.0 ns
0.980          103.0 ns     105.0 ns     109.0 ns
0.999          101.1 ns     101.2 ns     101.4 ns
slowdown over a no-translation access (100 ns), 98% hits: 3.0% (1 level), 9.0% (4 levels)
```

Read the table: at a hit ratio of 80 percent the effective time is 21 to 81 percent worse than an untranslated access, depending on the depth; at 98 percent it is 3 percent worse with one level and 9 percent with four; at 99.9 percent the depth hardly matters (101.1 against 101.4 ns). The lesson is the one that decides real designs: **deep tables are affordable only because the hit ratio is high**, and the hit ratio is high because programs have locality and the TLB covers its **reach** (entries times page size; CO 15 section 5.3 has the numbers and the huge-page cure). A TLB with 64 entries of 4 KiB pages covers 256 KiB; a program that touches more than that at random will miss often whatever the formula says. The model also leaves out cache effects (the page-table reads usually hit the cache, so a real miss costs less than `L*m`), the cost of a page fault (OS 11 adds it as another term), and flushing the TLB on a context switch, which tagged TLB entries (ASIDs) avoid.

---

## What it costs

| Scheme | Wastes | Needs | Measured here |
| --- | --- | --- | --- |
| Base and bounds | Whole region incl. gaps | Contiguous region per process | 4 KiB bound, translation by two registers |
| First, next, best, worst fit | External fragmentation | Hole list, scan per request | 0.43 to 0.51 holes per block; worst fit failed 4.7 and 88 percent of requests |
| Buddy allocator | Internal: up to 50 percent per block | Power-of-two sizes, per-block flag | average 25.8 percent waste; external failure with half the memory free |
| Slab cache | Slab remainder | One cache per object type | `task_struct`: 3,328 of 32,768 bytes per slab |
| Segmentation | External fragmentation | Segment table, contiguous segments | 7 test accesses, 5 outcomes |
| Paging | Internal: half a page per region | Page table, MMU | flat 32-bit table 4,096 KiB; two-level for the test process 12 KiB |
| Two-level or four-level table | Slower TLB miss (one read per level) | Tables allocated on demand | dense 64 MiB: 128 KiB of tables; sparse 1 per 2 MiB: 2,052 KiB |
| Inverted table | Shared pages, chain walks | Hash on top | 1.1 to 1.5 entries per lookup; 32 MiB for 8 GiB of RAM |
| TLB | Reach is small | Hit ratio of 98 percent or more | 98 percent hits: 3 percent (1 level), 9 percent (4 levels) slower |

## Common mistakes

1. **Calling both kinds of fragmentation by one name.** External means free memory in unusable pieces (contiguous allocation, segmentation); internal means waste inside an allocated unit (paging's last page, buddy rounding).
2. **Saying paging has no fragmentation.** It has none external; the last page of every region is internal waste.
3. **Quoting worst fit as a sensible policy.** Both the textbook and the measurement above say it performs worst.
4. **Forgetting that compaction needs run-time relocation** and that it copies memory: it is not free, and paging makes it unnecessary.
5. **Computing the page-table size with the wrong entry count.** Entries = address space size divided by page size, not by the number of frames; the size is entries times entry size.
6. **Translating the offset.** Only the page or segment number is translated; the offset is added unchanged. Mixing up the number of offset bits and the number of page-number bits is the commonest arithmetic slip.
7. **Treating all faults as one kind.** A page that is not present, a write to a read-only page and a kernel-only page are three different faults with three different fixes (OS 11 handles the first).
8. **Assuming a buddy merge always happens.** Two free blocks merge only if they are each other's buddies; adjacent free blocks that are not buddies stay split, as the 64-byte failure shows.
9. **Using the effective access time formula with the wrong miss cost.** A miss costs the probe, one memory read per level, and the data read: `t + (L+1)*m`, not `t + L*m`.
10. **Believing a multi-level table is always smaller.** It is smaller for sparse address spaces with gaps; for a sparse use of single pages (one per 2 MiB) it was as big as the flat table.

## Questions out loud

1. *First, best and worst fit: which and why?* First or best fit. Best leaves the biggest holes and costs a full scan; first is fast; worst destroys the large holes and failed far more requests in the simulation.
2. *Internal against external fragmentation, with an example of each.* External: free memory split into small holes (contiguous allocation). Internal: waste inside the allocated unit (the last page of a region, buddy rounding).
3. *Why does paging remove external fragmentation?* Every allocation unit is one frame and any frame fits any page, so no hole is too small.
4. *Translate this address with a two-level table.* Split the address into directory index, table index and offset; read the directory entry, then the table entry; check valid and protection bits; the physical address is the frame number times the page size plus the offset (section 5.1).
5. *How big is the page table, and why not flat for 64 bits?* Entries times entry size; 2^36 entries of 8 bytes is 512 GiB per process for a 48-bit space.
6. *What does a multi-level table save, and what does it cost?* It allocates tables only for used regions; a TLB miss reads memory once per level.
7. *How does a buddy allocator find a block's buddy?* Flip bit `k` of the offset for a block of size `2^k`.
8. *What is the slab allocator for?* Fast allocation of one kind of kernel object, with no search, no internal fragmentation inside a slab, and objects returned initialised.
9. *What is an inverted page table and what does it trade?* One entry per frame; its size follows physical memory, but lookup searches (hashed) and sharing a frame between processes is awkward.
10. *Compute the effective access time.* `h*(t+m) + (1-h)*(t+(L+1)*m)`; with `t = 10`, `m = 200`, `h = 0.9`, two levels, that is 250 ns.

---

## Problems

**Where to solve them.** Pencil and paper for A to D, then check against the programs; the programs are in the text and build with `gcc -O2`. E can be checked by hand-simulating the buddy lists or by adapting `g2_buddy.c`. For the real-system parts, `/proc/self/pagemap` and `/proc/buddyinfo` need root on Linux.

**How to approach them.** For A, write the address in binary and cut it into fields before touching the tables. For B, keep the hole list as numbers and update it after each placement. For C, write the formula first with the probabilities, then plug in the numbers. For D, find the number of bits per level from the page size and entry size before counting levels. For E, track the free lists after each step.

### Problem A (E, entry level): translate three addresses

Using the mappings of section 5.1 (VPN `0x400` to frame 7 writable user, `0x401` to frame 8 read-only user, `0x402` to frame 12 kernel-only, `0x7ffff` to frame 300 writable user; 10 + 10 + 12 bit address), translate `0x00400fff`, a read of `0x00401004`, a **write** to `0x00401004`, and a read of `0x7ffff000`. Give the physical address or the fault.

### Problem B (M, medium): place the requests

Holes in address order: 100 KB, 500 KB, 200 KB, 300 KB, 600 KB. Requests in order: 212 KB, 417 KB, 112 KB, 426 KB. Place them with first fit, best fit and worst fit, and say which policies can place all four. Then say what compaction would do for the one that fails.

### Problem C (M, medium): effective access time

Memory takes 200 ns, the TLB 10 ns, and the hit ratio is 90 percent. Compute the effective access time for a one-level and a two-level table. Then find the hit ratio needed to keep the two-level effective time within 10 percent of 200 ns.

### Problem D (H, hard): design a page table

A machine has 40-bit virtual addresses, 4 KiB pages and 8-byte page-table entries. How many bits does each level index if each table is one page? How many levels are needed? A process has 100 MiB of data in one contiguous region: how much memory do its page tables use? Compare with the flat table.

### Problem E (H, hard): buddy by hand

A 1,024 KiB arena, minimum block 64 KiB, always taking the lowest-address free block of the needed size. Requests: A 70 KiB, B 35 KiB, C 80 KiB, D 60 KiB, then free A, C, B, D in that order. Give the placement and the block size for each request, the free lists after each free, and the point at which the arena becomes one block again. How much memory is lost to internal fragmentation while all four are allocated?

---

## Answers

**A.** `0x00400fff`: directory 1, table 0, offset `0xfff`, frame 7, physical `0x7fff`. Read of `0x00401004`: table 1, offset 4, frame 8, physical `0x8004`. Write to `0x00401004`: the page is read-only, so a protection fault. Read of `0x7ffff000`: directory 511, table 1023, offset 0, frame 300, physical `300 * 4096 = 0x12c000`. (The program's output for the same mappings includes `0x7ffff0ab` giving `0x0012c0ab`.)

**B.** The program's logic, applied by hand (verified with a script):

- **First fit**: 212 into the 500 (leaving 288), 417 into the 600 (leaving 183), 112 into the 288 (leaving 176), and 426 fails (holes left: 100, 176, 200, 300, 183).
- **Best fit**: 212 into the 300 (88 left), 417 into the 500 (83), 112 into the 200 (88), 426 into the 600 (174). All four are placed.
- **Worst fit**: 212 into the 600 (388), 417 into the 500 (83), 112 into the 388 (276), and 426 fails.

Only best fit places all four. The failure in the other two is external fragmentation: after first fit the free memory is 100 + 176 + 200 + 300 + 183 = 959 KB, more than 426 KB, but no single hole is that big. Compaction would slide the allocated regions together and merge the holes into one 959 KB hole, so the 426 KB request would fit, at the price of copying all the allocated memory and pausing the processes involved.

**C.** One level: `0.9 * (10 + 200) + 0.1 * (10 + 2 * 200) = 189 + 41 = 230 ns`. Two levels: `0.9 * 210 + 0.1 * (10 + 3 * 200) = 189 + 61 = 250 ns`. For the two-level effective time to be at most 220 ns (10 percent over 200): `210 + (1 - h) * 400 <= 220`, so `1 - h <= 0.025` and `h >= 97.5 percent`. (Check with the formula: `h * 210 + (1-h) * 610` at `h = 0.975` is 220 ns.)

**D.** Offset 12 bits; a 4 KiB table holds 4096 / 8 = 512 entries, so 9 bits per level. The page number is 40 - 12 = 28 bits. Three levels of 9 bits cover 27, so a fourth is needed for the last bit, or the top level can be larger: 10 + 9 + 9 = 28 bits with a top table of 1,024 entries (8 KiB, two contiguous pages). Memory for 100 MiB: 25,600 pages; at 512 entries per table page that is 50 table pages (200 KiB), 1 page for the next level up (4 KiB) and the top table (8 KiB in the 10 + 9 + 9 design), so about 212 KiB, 0.2 percent of the data. The flat table would have 2^28 entries of 8 bytes, 2 GiB, regardless of use. The design choices: a larger top level avoids a fourth level and its extra memory read on every TLB miss, at the price of an allocation of two contiguous pages per process.

**E.** Sizes: A 70 KiB gets a 128 KiB block, B 35 KiB a 64 KiB block, C 80 KiB a 128 KiB block, D 60 KiB a 64 KiB block. With lowest-address-first placement: A at 0 to 128, B at 128 to 192, C at 256 to 384, D at 192 to 256 (D takes the 64 KiB block that was the other half of the split that served B). Free lists with all four allocated: one 128 KiB block at 384 and one 512 KiB block at 512. Internal fragmentation: (128 - 70) + (64 - 35) + (128 - 80) + (64 - 60) = 58 + 29 + 48 + 4 = 139 KiB of 384 KiB handed out, 36 percent. After free A: 128 KiB at 0 and 384 are free (not buddies of each other). After free C: its buddy at 384 is free, so they merge into 256 KiB at 256; that block's buddy (at 0, size 256 KiB) is not free because B is still allocated in 128 to 192. Free lists: 128 KiB at 0, 256 KiB at 256, 512 KiB at 512. After free B: its buddy D at 192 is still allocated, so 64 KiB at 128 stays on its own. After free D: D's buddy (128) is free, so they merge into 128 KiB at 128; that block's buddy at 0 is free, so they merge into 256 KiB at 0; its buddy at 256 is free, so 512 KiB at 0; its buddy at 512 is free, so the whole 1,024 KiB at the end. The arena becomes one block only on the **last** free; a single block still allocated anywhere in it prevents the final merges. (Verified with a script using the same placement rule.)

---

## Cheat sheet

- Translation: split the address into number and offset; translate the number; keep the offset. `pa = frame * page_size + offset`.
- Base and bounds: `vaddr < bound`, then `base + vaddr`. Segmentation: segment number selects base and limit, stack grows down.
- Fit policies: first (fast), best (tightest, full scan, keeps big holes), worst (destroys big holes, worst measured), next (cheap, chops the end).
- 50 percent rule: holes are about half the number of allocated blocks (measured 0.43 to 0.51).
- External fragmentation: holes too small (contiguous allocation, segmentation, buddy at times). Internal: waste inside the unit (paging's last page, buddy rounding up to 50 percent, slab remainder).
- Buddy: round up to a power of two, split down, merge up; the buddy of a block of size `2^k` at `off` is `off XOR 2^k`; merge only with the buddy.
- Slab: object caches over page-sized slabs, objects reused initialised; `/proc/slabinfo` and `/proc/buddyinfo` show the real ones.
- Flat page table size: `(address space / page size) * entry size`. Two-level: allocate second-level tables only where used. Inverted: one entry per frame, hash to search.
- Real tables: `/proc/self/pagemap` entry: bit 63 present, bits 0 to 54 frame number. A read-only touch maps the zero page; a write allocates a frame.
- EAT = `h*(t+m) + (1-h)*(t+(L+1)*m)`. Depth matters little at 99.9 percent hits and a lot at 80 percent.

## Sources

Measured on the VM described above. Not fetched, and stated from memory in the text: Knuth's 50 percent rule (reproduced by the simulation, not read), the page-size optimum `sqrt(2*s*e)`, the zero page and its behaviour, the expected hash chain length, segment registers in 64-bit mode, hashed page tables in practice, and `/proc/buddyinfo` as a diagnostic for high-order failures. OSTEP's address-space chapters (base and bounds, segmentation, paging) were not fetched; the two chapters below were.

- [OSTEP: Free-space management (PDF)](https://pages.cs.wisc.edu/~remzi/OSTEP/vm-freespace.pdf), fetched: free lists, splitting and coalescing, headers, best, worst, first and next fit, segregated lists, slab allocator, buddy allocator
- [OSTEP: Paging: smaller tables (PDF)](https://pages.cs.wisc.edu/~remzi/OSTEP/vm-smalltables.pdf), fetched: linear table sizes, bigger pages, hybrid tables, multi-level tables, the time-space trade-off, inverted tables
- [Linux kernel: pagemap, kpageflags and kpagecount](https://docs.kernel.org/admin-guide/mm/pagemap.html), fetched: the 64-bit pagemap entry layout and the permission rule for frame numbers
- [C 03: Memory, stack, heap and malloc](../02_c_language/03_memory_stack_heap_and_malloc.md), [CO 15: Virtual memory and the TLB](../04_computer_organization/15_virtual_memory_and_the_tlb.md) and [OS 02: Processes](02_processes.md), the neighbouring material
