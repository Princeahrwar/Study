# OS 12 — Virtual memory, paging and the TLB

> Prerequisite: `04_computer_organization/13_cache_basics.md`, `04_computer_organization/14_cache_optimisation_and_the_real_hierarchy.md` and `04_computer_organization/15_virtual_memory_and_the_tlb.md` (the hardware side of this note).
> Lab: tlb2.cpp (removed). Diagram: `diagrams/memory_load_path.mmd`.

## The framing

Every address your program uses is a lie the hardware agrees to maintain. The
translation is not free, it is not always cached, and on a large working set it
can cost more than the data access itself.

Most engineers know virtual memory exists. Far fewer can compute **TLB reach**,
and that number is what decides real data-plane design.

## The translation, exactly

A 48-bit canonical x86-64 virtual address is five fields:

```
   [47:39]  PML4 index   9 bits
   [38:30]  PDPT index   9 bits
   [29:21]  PD   index   9 bits
   [20:12]  PT   index   9 bits
   [11:0]   page offset  12 bits
```

Why nine bits? Because 2^9 = 512 entries, and 512 x 8 B = 4096 B = exactly one
page. The structure is self-similar by construction: every level of the page
table is itself one page. That is the whole reason for the number, and it is a
satisfying thing to be able to explain.

AArch64 is the same idea with configurable granule (4 KiB, 16 KiB or 64 KiB) and
typically 3 or 4 levels. The 16 KiB granule is why Apple silicon page behaviour
differs from Linux-on-x86 intuition.

`CR3` (x86) / `TTBR0_EL1` (ARM) holds the physical address of the top-level
table. Changing it *is* an address-space switch, and it is why context switches
between processes cost more than between threads.

### Huge pages fall out of the same structure

If a page-directory entry sets the "page size" bit, translation **stops early**
and the remaining index bits become offset:

- Stop at PD level: 21 offset bits -> **2 MiB page**
- Stop at PDPT level: 30 offset bits -> **1 GiB page**

So a huge page is not a special mechanism bolted on; it is the walk terminating
one level sooner. Shorter walk, and — far more importantly — **one TLB entry
covering 512x more memory**.

## The walk and its cost

On a TLB miss, hardware walks the table. Up to four dependent memory reads, each
of which can itself miss cache. A naive full walk could cost 300+ ns.

It usually does not, because of **page-walk caches (PWC)** — small structures
caching upper-level entries. Since the upper levels change rarely and cover huge
ranges, they hit almost always. That is why a measured TLB miss is typically
20-40 ns rather than 300.

But at very large footprints even the *leaf* page-table entries stop fitting in
cache, and the walk's own loads start missing to DRAM. That is the second knee
in the measurements below.

## TLB reach — the number nobody computes

```
   L1 dTLB:  ~64 entries  x 4 KiB = 256 KiB reach
   STLB:   ~2048 entries  x 4 KiB = ~8 MiB reach

   with 2 MiB pages:
   STLB:   ~1024 entries  x 2 MiB = ~2 GiB reach
```

Now apply it. Your flow table is 400 MiB. Backed by 4 KiB pages it needs 102,400
TLB entries and your STLB holds about 2,000. **It can never be TLB-resident, no
matter how much L3 the machine has.** Backed by 2 MiB pages it needs 200 entries
and fits comfortably.

Hugepages there are not a micro-optimisation. They change the asymptote.

## Measured: isolating TLB cost from cache cost

tlb2.cpp (removed) chases a pointer through one node per page, so the *data* footprint
stays tiny while the *page* footprint grows. Dependent loads, so no MLP hides the
walk. Scattered offsets within each page, so set conflicts do not contaminate it
(the first version of this benchmark got that wrong — see `04_computer_organization/13_cache_basics.md`, section 4.2).

x86 Xeon:

| Footprint | 4 KiB pages | 2 MiB pages | Hugepage gain |
| --- | --- | --- | --- |
| 256 KiB | 1.77 ns | 1.81 ns | — |
| 1 MiB | 4.29 ns | 4.29 ns | — |
| 4 MiB | 7.92 ns | 8.07 ns | — |
| 16 MiB | 20.34 ns | 16.86 ns | 1.2x |
| 64 MiB | 27.44 ns | 21.14 ns | 1.3x |
| 256 MiB | 71.11 ns | 56.80 ns | 1.25x |

Your ARM64 machine, same binary, same benchmark:

| Footprint | 4 KiB pages | 2 MiB pages | Hugepage gain |
| --- | --- | --- | --- |
| 256 KiB | 0.69 ns | 0.68 ns | — |
| 1 MiB | 2.05 ns | 0.68 ns | 3.0x |
| 4 MiB | 2.54 ns | 1.82 ns | 1.4x |
| 16 MiB | 23.61 ns | 5.34 ns | **4.4x** |
| 64 MiB | 25.55 ns | 12.04 ns | 2.1x |
| 256 MiB | 33.34 ns | 20.59 ns | 1.6x |

**Read those two tables against each other.** On x86 hugepages bought 20-30%, and
an x86-only engineer would reasonably conclude they are a minor tuning knob. On
ARM they bought up to 4.4x. ARM is what your APs run.

This is the single most valuable empirical finding in the whole study system, and
it came from running a benchmark on your own hardware rather than trusting a
table. Do exactly this for every claim you plan to make in an interview.

## Getting huge pages

Three mechanisms, and the differences matter:

```bash
# 1. Transparent huge pages (THP) — automatic, best-effort, can be undone by
#    the kernel. Check what your system does:
cat /sys/kernel/mm/transparent_hugepage/enabled      # always | madvise | never

# 2. Explicit hugetlbfs — reserved up front, never broken up, never swapped.
#    This is what you want for a data plane.
echo 1024 > /proc/sys/vm/nr_hugepages                # reserve 1024 x 2 MiB
mount -t hugetlbfs none /mnt/huge

# 3. Verify you actually got them — do not assume:
grep -i huge /proc/meminfo
awk '/AnonHugePages/ {s+=$2} END {print s" kB"}' /proc/self/smaps
```

```cpp
#include <sys/mman.h>

// Per-allocation request for THP. Align to 2 MiB or the kernel often cannot
// honour it — this is the step people skip, and then conclude THP "does nothing".
void* alloc_huge(size_t bytes) {
    size_t aligned = (bytes + (2u << 20) - 1) & ~((size_t)(2u << 20) - 1);
    void* p = mmap(nullptr, aligned, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) return nullptr;
    madvise(p, aligned, MADV_HUGEPAGE);
    return p;
}

// For a real data plane, prefer explicit reservation so the kernel can never
// split your pages under memory pressure:
void* alloc_hugetlb(size_t bytes) {
    return mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
}
```

**Verify, always.** In writing these tutorials the THP benchmark initially showed
no benefit; checking `AnonHugePages` in `/proc/self/smaps` confirmed the pages
*were* granted and the real cause was elsewhere. Without that check the
conclusion would have been wrong in print.

### The cost of huge pages

Not free. Internal fragmentation (a 2 MiB page for a 4 KiB allocation wastes
2044 KiB), longer page-fault latency when one is faulted in, and THP compaction
stalls that show up as *tail latency* — the kernel defragmenting memory to
produce a huge page can block your thread for hundreds of microseconds. For a
latency-sensitive data plane, use `hugetlbfs` reservations and set THP to
`madvise` rather than `always`.

## The page fault path

Worth knowing by name, because "is this a minor fault?" is a real diagnostic step:

```
  access -> TLB miss -> page walk -> PTE not present -> #PF exception
     -> do_page_fault() -> find the VMA -> handle_mm_fault()
        -> minor: page is in memory (page cache, COW, or zero page) — microseconds
        -> major: must read from disk/swap — milliseconds
```

- **Minor fault**: anonymous first touch, copy-on-write after fork, mapping a
  file page already in page cache.
- **Major fault**: actual I/O. In a data plane, any major fault is a bug.

```bash
perf stat -e minor-faults,major-faults ./app
/usr/bin/time -v ./app       # also prints both
```

`mlock`/`mlockall` pins pages so they are never reclaimed — standard practice in
low-latency and RT systems, along with pre-faulting the heap at startup.

## Related things that bite

- **TLB shootdown.** When one thread unmaps or changes a mapping, every core
  running that address space must invalidate its TLB entries. The kernel does
  this with an IPI, so *other cores stop and service an interrupt*. A busy
  `munmap`/`mprotect` loop on one thread is a measurable latency source on
  unrelated threads sharing the mm. This is a genuinely good answer to "what else
  could cause a tail-latency spike?"
- **ASID / PCID.** Tags TLB entries with an address-space id so a context switch
  need not flush the whole TLB. Without it, process switches are much more
  expensive.
- **KPTI**, the Meltdown mitigation, splits kernel and user page tables and made
  syscalls significantly more expensive. It is why syscall-heavy workloads
  regressed on some kernel upgrades.

## Interview angles

**"What happens on a TLB miss?"**
Page walk, up to four dependent loads, mitigated by page-walk caches. Give the
20-40 ns figure and say why it is not 300.

**"When would you use huge pages?"**
Compute reach first: working set versus `STLB entries x page size`. Then mention
the costs — fragmentation, compaction stalls — and that you would use hugetlbfs
rather than THP for latency-sensitive paths. Then, if you can, cite your own
ARM-vs-x86 measurement. That combination is a Staff-level answer.

**"Why is 512 entries per page table level?"**
512 x 8 B = 4096 B = one page. Self-similar.

**"A thread has a 400 us latency spike and the core is isolated. Ideas?"**
TLB shootdown IPI from another thread, THP compaction stall, or a major fault.
Naming the shootdown specifically is a differentiator.

## Exercises

1. Compute TLB reach for your own machine. Find the entry counts with `cpuid`
   (x86) or from the vendor's TRM (ARM). Write down the 4 KiB and 2 MiB reach.
2. Run tlb2.cpp (removed) and find *your* knees. Compare where they fall to your computed
   reach. They should agree within a factor of two; if not, work out why.
3. Allocate a 1 GiB flow table with 4 KiB pages and with hugetlbfs. Run a random
   lookup benchmark over both. Report the difference and the `dtlb_load_misses.
   walk_active` counter for each.
4. Provoke a TLB shootdown: one thread in a tight `mmap`/`munmap` loop, another
   doing latency-sensitive work pinned to a different core. Measure the second
   thread's p99.9 with and without the first thread running.
5. Set THP to `always` and run a latency histogram over a long allocation-heavy
   workload. Then set it to `madvise` and repeat. Look specifically at the tail,
   not the mean. This is the compaction-stall effect made visible.
