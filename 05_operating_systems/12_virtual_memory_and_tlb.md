# OS 12: Virtual memory and the TLB in practice

*Part of the [Operating systems plan](00_plan.md) · Time: about 9 hours (6 reading and experiments, 3 problems) · You need: [OS 10](10_memory_management.md) (paging and the page-table entry), [OS 11](11_virtual_memory.md) (demand paging, replacement, thrashing) and [CO 15](../04_computer_organization/15_virtual_memory_and_the_tlb.md) (the hardware walk, the TLB and its measured cost)*

This replaces an earlier, shorter note. OS 10 and OS 11 explain how virtual memory works, and CO 15 holds the hardware: the x86-64 and ARM64 walks, TLB structure and the measured cost of a TLB miss. This tutorial is the practitioner's view, the one that decides what happens on a real server or an embedded board. Which of the numbers a tool reports can you believe (virtual size, resident size, proportional size)? What does `malloc` actually promise, and when does the machine break that promise? When do huge pages help, what do they cost, and how do transparent and explicit huge pages differ? What does it cost to pay for page faults in advance? And when memory runs out, who does the kernel kill, and can you steer it? Interviews ask these in the form of incidents: a process was killed with plenty of memory showing free; RSS says 40 GiB but the machine has 32; a data plane slowed down after a kernel upgrade; a container died at its limit. Each section below answers one of them with a program that was compiled and run, and the reading of the numbers is the point.

By the end you can:

- compute TLB reach and say when it, not the cache, is the limit (measured: random reads over 256 MiB cost 13.1 / 13.2 ns per read on 4 KiB pages and 8.2 / 8.5 ns with transparent huge pages, in two runs);
- read virtual size, resident size (RSS) and proportional size (PSS) of a process and say what each counts (measured: after `fork` the parent's PSS for 30 MiB of shared pages dropped from 30 to 15 MiB while its RSS stayed at 31 MiB; and `MADV_FREE` freed nothing visible while `MADV_DONTNEED` dropped RSS from 65 to 1 MiB);
- state what the three overcommit modes do and predict the result of a given `mmap` (measured on an 8 GiB machine: mode 0 refused 9 GiB and granted 7 GiB but granted four 3 GiB mappings together, 12 GiB; mode 1 granted 64 GiB; mode 2 refused everything beyond its 4,012 MiB limit);
- choose between transparent huge pages and `hugetlbfs`, and read the counters that show which you got (measured: 256 MiB mapped as 128 huge pages; the cost, one byte per 2 MiB extent growing RSS by 128 MiB against 256 KiB; a pool of 16 reserved 8 and failed with ENOMEM when empty);
- say what pre-faulting buys and does not (measured: `MAP_POPULATE` and `mlock` cost about 1.1 microseconds per page up front against about 1.7 on demand, and take the later touch to 0.03);
- explain how the OOM killer picks a victim and what you can set to change it (measured: the largest process in the cgroup died whether or not it was the one that asked for memory; a positive `oom_score_adj` moved the victim; a negative one was refused without `CAP_SYS_RESOURCE`);
- diagnose a memory incident from `/proc/meminfo`, `smaps_rollup`, `/proc/vmstat` and `dmesg`, and name the knob for each cause.

**Where the numbers come from.** Programs were compiled with GCC 13.3 (`-O2 -Wall -Wextra`) and run on a Linux 6.18.44 kernel in a Firecracker micro-VM with 2 vCPUs, 8 GiB of memory, 4 KiB pages, as root (to change `vm.overcommit_memory`, the huge-page pool and a memory cgroup, all restored afterwards). Transparent huge pages were in `madvise` mode, as the machine ships. The semantics of the overcommit modes, the sysctls, transparent huge pages and `hugetlbfs` are from the kernel documents on overcommit accounting, the `vm` sysctls, transparent hugepages and hugetlb pages (all fetched; see Sources). The x86-64 and ARM64 hardware is in CO 15 and is not repeated here. The OOM badness formula is from memory (the kernel's `oom_badness`), and the measurements reproduce its behaviour. Facts from general knowledge are marked "from memory".

**Data and timing.** Counts and sizes in this chapter are exact and repeat. Timings vary: each timed program was run twice and both are shown, and where they differ widely the text says so. Because this is a virtual machine, anything about the TLB and huge pages depends on how the **host** backs the guest's memory (CO 15 section 6.4), so the huge-page timings below are measurements of this guest on this host, not constants.

---

## 1. What the hardware gives, in one page

Everything in this chapter sits on four facts from CO 15 and OS 10. They are repeated here as arithmetic so the rest can use them.

**The walk.** A 64-bit virtual address is cut into a page offset and one index per table level. x86-64 with 4 KiB pages and 48-bit addresses uses four levels of 9 bits each (512 entries of 8 bytes in a 4 KiB table) plus a 12-bit offset: 4 x 9 + 12 = 48. AArch64 with a 4 KiB granule uses the same layout (levels 0 to 3). A walk without caching reads memory once per level. A 2 MiB huge page ends the walk one level early (a three-read walk) and a 1 GiB page two levels early.

**The TLB.** A small cache of finished translations (CO 15 section 5): about 2,048 second-level entries on the machine CO 15 measured, which cover `2,048 x 4 KiB = 8 MiB` with small pages and `2,048 x 2 MiB = 4 GiB` with huge ones (the best case; real chips have fewer entries for 2 MiB pages). Reach is the arithmetic that matters:

| Page size | Entries to map 1 GiB | A 2,048-entry TLB reaches | Walk reads |
| --- | --- | --- | --- |
| 4 KiB | 262,144 | 8 MiB | 4 |
| 2 MiB | 512 | 4 GiB | 3 |
| 1 GiB | 1 | 2,048 GiB | 2 |

**Arm differences that change the numbers.** The page size (granule) is a choice: 4, 16 or 64 KiB. With 16 KiB pages (Apple silicon, some Linux ARM64 kernels) the same entry count reaches four times more memory and a table holds 2,048 entries, ; with 64 KiB pages the kernel's huge page is 512 MiB. A program that assumes 4096 anywhere breaks on such a machine; ask `sysconf(_SC_PAGESIZE)` (from memory for the kernel configurations; CO 15 section 3.4 has the granule table).

**What a fault costs.** OS 11 measured it: about 3 microseconds for a first touch, tens of microseconds for a read from storage. The rest of this chapter is about the decisions that make a program take those faults, or avoid them.

The measured effect of reach on a real access pattern, from the transparent-huge-page program of section 4: 20 million random single-byte reads over a 256 MiB region that was already resident, best of three timings. With 4 KiB pages it cost 13.1 / 13.2 ns per read (first run, second run), and with transparent huge pages 8.2 / 8.5 ns.

Huge pages made random access about 35 to 40 percent faster here (13 to 8 nanoseconds). Every read still has a cache miss (256 MiB is beyond the cache), so the saving is the TLB-miss share of the access, which is what reach predicts: with 4 KiB pages the region needs 65,536 entries for a 2,048-entry TLB, with huge pages 128. This is a guest of a hypervisor, so the gain is a measurement of this host's backing, not a promise (CO 15 saw the gain come and go between sessions).

---

## 2. Reading a process's memory: virtual, resident and proportional

### 2.1 Three numbers, three meanings

Every memory tool prints some of these, and they answer different questions.

- **Virtual size (VSZ, `VmSize`)**: the address space the process has mapped. Costs nothing by itself; `malloc` of a gigabyte raises it by a gigabyte (section 3).
- **Resident set size (RSS, `VmRSS`)**: the pages of the process that are in physical memory right now. It counts a shared page in full for every process that maps it, so **summing RSS over processes over-counts**.
- **Proportional set size (PSS, `Pss` in `smaps_rollup`)**: each page counted as its size divided by the number of processes mapping it. **Summing PSS over processes gives real memory use** (to within the accounting of shared kernel pages).

The program measures all three through `fork` and through the two ways to give memory back. The helper reads them from `/proc/self/status` and `/proc/self/smaps_rollup`:

```c
static long field(const char *path, const char *key)          /* a "Key: value kB" line */
{
    FILE *f = fopen(path, "r"); char line[256]; long v = -1;
    while (fgets(line, sizeof line, f)) if (!strncmp(line, key, strlen(key)) && line[strlen(key)] == ':') { sscanf(line + strlen(key) + 1, " %ld", &v); break; }
    fclose(f);
    return v;
}

static void show(const char *what)
{
    long vsz = field("/proc/self/status", "VmSize"), rss = field("/proc/self/status", "VmRSS");
    long pss = field("/proc/self/smaps_rollup", "Pss");
    printf("%-46s VSZ %6ld MiB  RSS %6ld MiB  PSS %6ld MiB\n", what, vsz >> 10, rss >> 10, pss >> 10);
    fflush(stdout);
}
```

```text
at start                                       VSZ      2 MiB  RSS      1 MiB  PSS      0 MiB
after mmap of 100 MiB (nothing touched)        VSZ    102 MiB  RSS      1 MiB  PSS      0 MiB
after touching the first 30 MiB                VSZ    102 MiB  RSS     31 MiB  PSS     30 MiB
parent right after fork (child has not written) VSZ    102 MiB  RSS     31 MiB  PSS     15 MiB
child after writing 10 MiB (child's view)      VSZ    102 MiB  RSS     31 MiB  PSS     20 MiB
parent after the child wrote 10 MiB and exited VSZ    102 MiB  RSS     31 MiB  PSS     30 MiB
a second region of 64 MiB, touched             VSZ     66 MiB  RSS     65 MiB  PSS     64 MiB
after MADV_FREE of all of it                   VSZ     66 MiB  RSS     65 MiB  PSS     64 MiB
touched again, then MADV_DONTNEED              VSZ     66 MiB  RSS      1 MiB  PSS      0 MiB
a read after MADV_DONTNEED returns 0 (the page comes back zeroed)
```

(The second run printed identical numbers, as it should: these are page counts, not timings.) Read it line by line:

- **After the 100 MiB `mmap`, VSZ jumped from 2 to 102 MiB and RSS stayed at 1 MiB.** Virtual memory is not memory.
- **After touching 30 MiB, RSS is 31 MiB and PSS is 30.** For a process that shares nothing, RSS and PSS agree (the 1 MiB in RSS beyond the 30 is the program's own binary and libraries, shared with other processes, which is why its PSS share rounds to 0).
- **Right after `fork`, the parent's RSS is unchanged at 31 MiB but its PSS is 15.** The 30 MiB of pages are now mapped by two processes (copy-on-write, OS 11 section 1.4), so each is charged half. RSS has not moved because the parent still has all those pages resident; PSS reports what the parent **costs the machine**.
- **After the child wrote 10 MiB, the child's PSS is 20**: its 10 MiB of fresh private copies plus half of the 20 MiB it still shares. The parent, once the child exits, is back at 30: the shared pages have one owner again.

### 2.2 Giving memory back: `MADV_FREE` and `MADV_DONTNEED`

After the fork section the program maps a second 64 MiB region and touches it all (RSS 65 MiB). Then:

- **`MADV_FREE` of the whole region: RSS stayed at 65 MiB.** `MADV_FREE` says "the contents are no longer needed"; the kernel is allowed to take the pages **lazily**, when it needs memory, and until it does they remain resident and still count. A program that frees with `MADV_FREE` and watches RSS will conclude nothing happened.
- **After touching again, `MADV_DONTNEED`: RSS fell to 1 MiB at once**, and a later read of the region returned 0: the pages were dropped and the next touch gets fresh zero pages. This is the immediate form, and it is what `free` in many allocators uses to return memory to the system (glibc's `malloc_trim` does this for the heap, from memory).

Two practical consequences. A monitoring system that alerts on RSS will see an allocator that uses `MADV_FREE` as a leak that never ends until memory pressure arrives (jemalloc has options for which advice it uses; from memory). And RSS is a sample of a moving state: never compare it across kernels or allocators without saying how the memory is returned.

### 2.3 The files to read

| File | What it gives |
| --- | --- |
| `/proc/PID/status` | `VmSize`, `VmRSS`, `VmHWM` (peak RSS), `RssAnon`, `RssFile`, `RssShmem`, `VmSwap` |
| `/proc/PID/smaps_rollup` | The sum over all mappings: `Rss`, `Pss`, `Shared_Clean`, `Private_Dirty`, `AnonHugePages`, `Swap` |
| `/proc/PID/smaps` | The same per mapping; expensive to read often (the kernel document says so for `AnonHugePages`) |
| `/proc/PID/pagemap` | Per page: present, swapped, frame number (needs `CAP_SYS_ADMIN` for frame numbers; OS 10 section 5.4) |
| `/proc/meminfo` | System totals: `MemFree`, `MemAvailable`, `Committed_AS`, `CommitLimit`, `AnonHugePages`, `HugePages_*` |
| `/proc/vmstat` | Counters since boot: `pswpin`, `pswpout`, `thp_fault_alloc`, `thp_fault_fallback`, `oom_kill` |

The rule for a service that forks workers: **report PSS, not RSS**, or the dashboard will show 200 workers each at the size of the shared data and an impossible total.

---

## 3. Overcommit: what `malloc` promises

### 3.1 The promise and the three modes

An `mmap` or `malloc` for memory the process will touch later only has to create a mapping (OS 11 section 1.4: nothing is allocated until the first touch). So the kernel can grant more virtual memory than physical memory exists, betting that not everything will be touched. **Overcommit** is that bet, and `vm.overcommit_memory` sets how the kernel bets (kernel documents, fetched):

- **Mode 0, heuristic (the default).** The kernel refuses allocations that clearly exceed what the machine could provide (the `vm` sysctl document says: exceeding total RAM plus swap; the overcommit document gives no exact test, and the measurement below shows it is applied to each request alone), and otherwise grants them.
- **Mode 1, always.** Every allocation succeeds, whatever its size. Meant for programs that map huge sparse arrays and touch little.
- **Mode 2, never overcommit.** The total committed address space may not exceed swap plus `vm.overcommit_ratio` percent of RAM (50 by default), or `vm.overcommit_kbytes` if that is set. Allocation calls fail with `ENOMEM`, and in exchange a process that already holds its memory should not be killed when it touches it.

The kernel counts the commitment in `Committed_AS` (in `/proc/meminfo`), and the limit it applies in mode 2 is `CommitLimit`. What is charged: per the kernel document, private writable anonymous mappings at full size (and a private writable file mapping too), shared anonymous mappings at full size, but not read-only private anonymous mappings or shared or read-only file mappings, since the file is their backing. `fork` charges the child's private writable mappings again.

### 3.2 Measured

The program asks for increasing sizes with plain `mmap` and touches nothing, once in each mode, on this 8,024 MiB machine with no swap (`CommitLimit` 4,012 MiB: half of RAM, as the formula says):

```c
static void *try_map(size_t gib)
{
    void *p = mmap(NULL, gib << 30, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? NULL : p;
}

int main(void)
{
    FILE *f = fopen("/proc/sys/vm/overcommit_memory", "r"); int mode = -1; if (fscanf(f, "%d", &mode) != 1) return 1; fclose(f);
    printf("overcommit_memory = %d, MemTotal %ld MiB, CommitLimit %ld MiB\n", mode, meminfo_kb("MemTotal") >> 10, meminfo_kb("CommitLimit") >> 10);
    size_t sizes[] = {1, 3, 5, 7, 9, 16, 64};
    for (int i = 0; i < 7; i++) {
        void *p = try_map(sizes[i]);
        printf("  one mmap of %2zu GiB: %s\n", sizes[i], p ? "granted" : strerror(errno));
        if (p) munmap(p, sizes[i] << 30);
    }
    void *keep[4]; int got = 0;
    for (int i = 0; i < 4; i++) { keep[i] = try_map(3); if (keep[i]) got++; }
    printf("  four mmaps of 3 GiB held together: %d granted, Committed_AS now %ld MiB\n", got, meminfo_kb("Committed_AS") >> 10);
    for (int i = 0; i < 4; i++) if (keep[i]) munmap(keep[i], 3UL << 30);
    return 0;
}
```

```text
overcommit_memory = 0, MemTotal 8024 MiB, CommitLimit 4012 MiB
  one mmap of  1 GiB: granted
  one mmap of  3 GiB: granted
  one mmap of  5 GiB: granted
  one mmap of  7 GiB: granted
  one mmap of  9 GiB: Cannot allocate memory
  one mmap of 16 GiB: Cannot allocate memory
  one mmap of 64 GiB: Cannot allocate memory
  four mmaps of 3 GiB held together: 4 granted, Committed_AS now 12864 MiB
```

```text
overcommit_memory = 1, MemTotal 8024 MiB, CommitLimit 4012 MiB
  one mmap of  1 GiB: granted
  one mmap of  3 GiB: granted
  one mmap of  5 GiB: granted
  one mmap of  7 GiB: granted
  one mmap of  9 GiB: granted
  one mmap of 16 GiB: granted
  one mmap of 64 GiB: granted
  four mmaps of 3 GiB held together: 4 granted, Committed_AS now 12864 MiB
```

```text
overcommit_memory = 2, MemTotal 8024 MiB, CommitLimit 4012 MiB
  one mmap of  1 GiB: granted
  one mmap of  3 GiB: granted
  one mmap of  5 GiB: Cannot allocate memory
  one mmap of  7 GiB: Cannot allocate memory
  one mmap of  9 GiB: Cannot allocate memory
  one mmap of 16 GiB: Cannot allocate memory
  one mmap of 64 GiB: Cannot allocate memory
  four mmaps of 3 GiB held together: 1 granted, Committed_AS now 3648 MiB
```

What it shows:

- **Mode 0 refused 9 GiB, granted 7 GiB** on an 8 GiB machine with no swap. So the test is "larger than RAM plus swap" on one request. **But four separate 3 GiB mappings, 12 GiB in all, were all granted** (`Committed_AS` 12,864 MiB, 1.6 times RAM): the heuristic looks at each request alone. This is the standard route to the OOM killer: nothing fails when memory is reserved, and the failure comes later, when the pages are touched.
- **Mode 1 granted 64 GiB**, eight times the RAM.
- **Mode 2 refused 5 GiB and above**, and of the four 3 GiB mappings only one fit: the first took `Committed_AS` to 3,648 MiB, the second would have made it about 6.7 GiB, above the 4,012 MiB limit. 3 GiB succeeded alone because the system's existing commitment was about 590 MiB (3,072 + 590 is below 4,012; 5,120 + 590 is not).

### 3.3 Why anyone runs mode 2, and why most do not

Mode 2 makes failure **early and local**: `malloc` returns `NULL`, the program can handle it, and nothing is killed at a random moment. Safety-critical and some embedded systems use it for this reason (from memory). Its costs are real. The sum of commitments is far above real use for ordinary software: a large process that forks a small helper is charged for a second copy of all its writable memory, so `fork` or `posix_spawn` of a big process can fail with `ENOMEM` while the machine is nearly empty (Problem E works a case). Programs reserve large address ranges (language runtimes, thread stacks, garbage-collected heaps) and are charged for them. And 50 percent of RAM as the default ratio is a policy that often needs tuning.

The default, mode 0, puts the failure at touch time, as the OOM killer (section 6). The rule that follows: **a successful `malloc` does not mean the memory exists.** A program that must have its memory should touch or lock it at start-up (section 5) so that the failure comes at start-up, when it can be handled, not during service.

---
## 4. Huge pages in practice

Section 1 gave the reason: with 4 KiB pages a large working set cannot stay in the TLB. A huge page (2 MiB on x86-64 and on ARM64 with 4 KiB granule) is one TLB entry and one page-table entry for 512 small pages. Linux offers two ways to get them, and they are very different tools.

### 4.1 Transparent huge pages (THP)

THP is automatic and best effort: the kernel tries to back a suitably aligned 2 MiB range with one huge page when the process faults on it, and a background thread, **`khugepaged`**, can later merge small pages into huge ones. The controls are in `/sys/kernel/mm/transparent_hugepage/` (kernel document, fetched):

- **`enabled`**: `always` (every eligible mapping), `madvise` (only mappings the program marked with `madvise(MADV_HUGEPAGE)`) or `never`. This machine runs `madvise`, and the kernel document recommends it as the safer default, since only regions that ask get huge pages.
- **`defrag`**: how hard the kernel works to find a free 2 MiB block when none is available at the fault: `always` stalls the faulting process to reclaim and compact memory, `madvise` (the default) stalls only for regions that asked, `defer` leaves it to background work, `never` falls back to small pages at once.
- **Counters:** `AnonHugePages` in `/proc/meminfo` (system) or `smaps` (per mapping), and in `/proc/vmstat` `thp_fault_alloc` (huge pages given at a fault) and `thp_fault_fallback` (faults that fell back to small pages because no huge page was available). The document says that frequent fallbacks or compaction stalls point to fragmentation or an aggressive `defrag` setting.

The program measures four things. It allocates 2 MiB-aligned regions (THP only maps aligned ranges), and uses `madvise` per region to compare the two behaviours in one process:

```c
static char *aligned_map(size_t bytes)                       /* a 2 MiB aligned anonymous region */
{
    char *p = mmap(NULL, bytes + 2 * MIB, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    char *a = (char *)(((unsigned long)p + 2 * MIB - 1) & ~(2 * MIB - 1));
    return a;
}

static void touch_all(const char *label, int advice, size_t bytes)
{
    char *a = aligned_map(bytes);
    madvise(a, bytes, advice);
    long alloc0 = vmstat("thp_fault_alloc");
    double t0 = now();
    for (size_t off = 0; off < bytes; off += 4096) a[off] = 1;
    double dt = now() - t0;
    printf("%-26s %6.2f us per 4 KiB page, huge pages mapped %4ld MiB of %zu MiB, thp_fault_alloc +%ld\n", label, dt / (bytes / 4096) * 1e6,
           status_kb("AnonHugePages") >> 10, bytes / MIB, vmstat("thp_fault_alloc") - alloc0);
    munmap(a, bytes);
}

static double random_access(char *a, size_t bytes, long n)
{
    unsigned long s = 1; volatile char sink = 0;
    double t0 = now();
    for (long i = 0; i < n; i++) { s = s * 6364136223846793005UL + 1442695040888963407UL; sink += a[(s >> 20) % bytes]; }
    return (now() - t0) / n * 1e9;
}
```

**First touch.** Touching a fresh 256 MiB region page by page:

```text
MADV_NOHUGEPAGE              3.49 us per 4 KiB page, huge pages mapped    0 MiB of 256 MiB, thp_fault_alloc +0
MADV_HUGEPAGE                2.93 us per 4 KiB page, huge pages mapped  256 MiB of 256 MiB, thp_fault_alloc +128
one byte in each of 64 huge-page-sized extents, MADV_NOHUGEPAGE : RSS grew by    256 KiB
one byte in each of 64 huge-page-sized extents, MADV_HUGEPAGE   : RSS grew by 131072 KiB
random reads over 256 MiB, MADV_NOHUGEPAGE :  13.1 ns per read (best of 3)
random reads over 256 MiB, MADV_HUGEPAGE   :   8.2 ns per read (best of 3)
```

```text
MADV_NOHUGEPAGE              3.50 us per 4 KiB page, huge pages mapped    0 MiB of 256 MiB, thp_fault_alloc +0
MADV_HUGEPAGE                2.76 us per 4 KiB page, huge pages mapped  256 MiB of 256 MiB, thp_fault_alloc +128
one byte in each of 64 huge-page-sized extents, MADV_NOHUGEPAGE : RSS grew by    256 KiB
one byte in each of 64 huge-page-sized extents, MADV_HUGEPAGE   : RSS grew by 131072 KiB
random reads over 256 MiB, MADV_NOHUGEPAGE :  13.2 ns per read (best of 3)
random reads over 256 MiB, MADV_HUGEPAGE   :   8.5 ns per read (best of 3)
```

With `MADV_HUGEPAGE` the kernel mapped the region as **128 huge pages** (`thp_fault_alloc` +128, 256 MiB reported as huge in `smaps_rollup`) and each 4 KiB step of the loop was cheaper than with small pages: 2.76 to 2.93 microseconds against 3.49 to 3.50 (an earlier, development run of the same program gave 1.5 against 3.1, so the size of the gain varies a lot between runs on this virtual machine). The reason is that one fault zeroes and maps 2 MiB at once, so the cost of the trap is spread over 512 pages; the zeroing cost, not the trap, then dominates. A single huge-page fault is slow in absolute terms (about 1.4 to 1.5 milliseconds in the two runs above: 512 pages at 2.8 to 2.9 microseconds; 0.8 milliseconds in an earlier development run), which is the **latency** cost of THP: the first touch of a 2 MiB range stalls for over a millisecond, whereas 4 KiB faults stall for 3 microseconds each and spread out.

**Bloat.** The second block of each run shows the cost that the kernel document names as the waste caveat. The program touches **one byte** in each of 64 successive 2 MiB extents (128 MiB of address space): with small pages RSS grows by **256 KiB** (64 pages); with THP it grows by **131,072 KiB, 128 MiB**, a factor of 512. A program that scatters small allocations across a large address range (a sparse hash table, an allocator that spreads arenas, a fork child that writes one word per 2 MiB) can multiply its memory use under `always`, and it is a known cause of "memory grew when THP was switched on" incidents (from memory). Under `madvise` the damage is limited to what the program marks.

**Gain.** The last block is the random-read result of section 1. The two runs, 13.1 and 13.2 ns without and 8.2 and 8.5 ns with huge pages, are stable, so this host does back the guest's memory with pages large enough to show the effect.

### 4.2 Explicit huge pages: `hugetlbfs`

The explicit route is a **reserved pool** of huge pages, set up by the administrator (`vm.nr_hugepages`, or `hugepages=` on the kernel command line, which the kernel document calls the most reliable because memory is not yet fragmented) and claimed with `mmap(..., MAP_HUGETLB, ...)` or by mounting `hugetlbfs`. Pool pages are **not swappable**, are never used for anything else, and are never split. The pool fields in `/proc/meminfo` are `HugePages_Total` (the pool), `HugePages_Free` (not yet handed out), `HugePages_Rsvd` (promised to a mapping but not yet faulted in: this is what guarantees the page will be there at fault time) and `HugePages_Surp` (pages above the persistent pool). The program claims 8 huge pages (16 MiB), once with an empty pool and once with a pool of 16, and watches the counters:

```c
int main(void)
{
    pool("pool");
    size_t want = 8 * 2 * MIB;
    char *a = mmap(NULL, want, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
    if (a == MAP_FAILED) { printf("mmap of 8 huge pages (16 MiB): failed: %s\n", strerror(errno)); return 0; }
    printf("mmap of 8 huge pages (16 MiB): granted\n");
    pool("after mmap, nothing touched");
    double t0 = now();
    for (size_t off = 0; off < want; off += 2 * MIB) a[off] = 1;
    double dt = now() - t0;
    pool("after touching all 8");
    printf("first touch of a huge page: %.1f us each\n", dt / 8 * 1e6);
    void *b = mmap(NULL, 2 * 2 * MIB, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
    printf("a further mmap of 2 huge pages: %s\n", b == MAP_FAILED ? strerror(errno) : "granted");
    return 0;
}
```

```text
pool                               HugePages_Total   0  Free   0  Rsvd   0
mmap of 8 huge pages (16 MiB): failed: Cannot allocate memory
```

```text
pool                               HugePages_Total  16  Free  16  Rsvd   0
mmap of 8 huge pages (16 MiB): granted
after mmap, nothing touched        HugePages_Total  16  Free  16  Rsvd   8
after touching all 8               HugePages_Total  16  Free   8  Rsvd   0
first touch of a huge page: 282.3 us each
a further mmap of 2 huge pages: granted
```

```text
pool                               HugePages_Total  16  Free  16  Rsvd   0
mmap of 8 huge pages (16 MiB): granted
after mmap, nothing touched        HugePages_Total  16  Free  16  Rsvd   8
after touching all 8               HugePages_Total  16  Free   8  Rsvd   0
first touch of a huge page: 834.5 us each
a further mmap of 2 huge pages: granted
```

- **With an empty pool the `mmap` failed with `ENOMEM` at once.** Unlike THP, there is no fallback to small pages: the request is refused, and the program must handle it. That is the point of the interface (the page is guaranteed), and its operational cost (someone must size the pool).
- **With a pool of 16, the `mmap` of 8 reserved them**: `Free` stayed at 16 and `Rsvd` rose to 8 (promised, not yet used). Touching them took `Free` to 8 and `Rsvd` to 0. A further `mmap` of 2 pages was granted because 8 were still free. The reservation is made **at `mmap` time**, so the failure comes at the `mmap`, not at the first touch, which is what a data plane wants.
- **The first touch of each huge page cost 282 and 835 microseconds in the two runs**, a threefold spread in two runs of 8 touches: a huge-page fault zeroes 2 MiB, so it is a large fixed cost per page, and it varies on this host (the program takes only 8 samples). A reserved pool is usually touched once at start-up to move this cost out of the service path.
- **1 GiB pages exist as well.** Writing 1 to `/sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages` succeeded and `Hugetlb` in `/proc/meminfo` read 1,048,576 kB, while `HugePages_Total` (which counts only the default 2 MiB size) stayed 0, so `HugePages_Total` alone does not show a 1 GiB pool. A 1 GiB page ends the walk two levels early, and reaching one needs contiguous physical memory, which fragmentation usually denies after the machine has run for a while; this is why 1 GiB pages are reserved at boot (the kernel document; the allocation here succeeded only because the machine was nearly empty).

### 4.3 Which one

| | THP | `hugetlbfs` |
| --- | --- | --- |
| Setup | none (`madvise` and alignment) | pool sized by the administrator |
| Guarantee | none: falls back to small pages, can be split | the page is reserved at `mmap` |
| Swappable | yes (split first) | no |
| Cost of failure | silently slower | `mmap` fails |
| Latency | a fault may stall for compaction (`defrag`) | none after the pool exists |
| Risk | memory bloat; surprising stalls | pool wastes memory if oversized; fragmentation at boot |
| Typical use | general servers, JVM and database heaps that opt in | data planes (DPDK-style packet buffers), databases with fixed buffer pools |

The practical rule: **verify, do not assume.** Whichever you chose, read `AnonHugePages` (THP) or `HugePages_Free` and `Rsvd` (`hugetlbfs`) after the process is up and check that the mapping got what you asked for, as CO 15 section 9.3 shows.

---

## 5. Paying for page faults in advance

### 5.1 The problem

A page fault is a few microseconds (OS 11 measured 3), which is nothing for most programs and a lot for a packet-processing loop with a budget of a few hundred nanoseconds per packet, or for a real-time thread. Worse is the **unpredictability**: the first touch of a page may need a free frame, and under memory pressure that means waiting for reclaim. The remedy is to take the faults at start-up, before the time-critical part begins. There are three ways, and the program measures each on a 256 MiB region (65,536 pages):

```c
int main(void)
{
    size_t bytes = 256 * MIB, pages = bytes / 4096;
    long m0; double t0, t1;

    char *a = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    madvise(a, bytes, MADV_NOHUGEPAGE);
    m0 = minflt(); t0 = now();
    for (size_t off = 0; off < bytes; off += 4096) a[off] = 1;
    t1 = now();
    printf("demand touch, then use:   mmap+touch %6.2f us per page, minor faults %6ld\n", (t1 - t0) / pages * 1e6, minflt() - m0);
    munmap(a, bytes);

    m0 = minflt(); t0 = now();
    char *b = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
    t1 = now();
    long during = minflt() - m0;
    for (size_t off = 0; off < bytes; off += 4096) b[off] = 2;
    printf("MAP_POPULATE:             mmap %6.2f us per page (faults counted during mmap %ld), later touch %5.2f us per page (extra faults %ld)\n",
           (t1 - t0) / pages * 1e6, during, (now() - t1) / pages * 1e6, minflt() - m0 - during);
    munmap(b, bytes);

    char *c = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    madvise(c, bytes, MADV_NOHUGEPAGE);
    t0 = now();
    int rc = mlock(c, bytes);
    t1 = now();
    m0 = minflt();
    for (size_t off = 0; off < bytes; off += 4096) c[off] = 3;
    printf("mlock of untouched:       mlock %6.2f us per page (rc %d), later touch %5.2f us per page (extra faults %ld)\n",
           (t1 - t0) / pages * 1e6, rc, (now() - t1) / pages * 1e6, minflt() - m0);
    munlock(c, bytes);
    munmap(c, bytes);
    return 0;
}
```

```text
demand touch, then use:   mmap+touch   1.70 us per page, minor faults  65539
MAP_POPULATE:             mmap   1.09 us per page (faults counted during mmap 65536), later touch  0.03 us per page (extra faults 0)
mlock of untouched:       mlock   1.08 us per page (rc 0), later touch  0.03 us per page (extra faults 0)
```

```text
demand touch, then use:   mmap+touch   1.67 us per page, minor faults  65539
MAP_POPULATE:             mmap   1.07 us per page (faults counted during mmap 65536), later touch  0.03 us per page (extra faults 0)
mlock of untouched:       mlock   1.06 us per page (rc 0), later touch  0.02 us per page (extra faults 0)
```

- **Demand touch** cost 1.7 microseconds per page in these two runs (65,539 minor faults, one per page). In two earlier development runs of the same program it was 2.5 and 5.0, so this figure moves a lot.
- **`MAP_POPULATE`** cost 1.1 microseconds per page inside the `mmap` call, and the later touch cost 0.03 with no further faults.
- **`mlock`** of untouched memory cost 1.1 microseconds per page and left the same 0.03 on later touches.

The lesson is not that pre-faulting is much cheaper in total: **1.1 against 1.7 microseconds is a saving of about a third** (the pages must still be allocated and zeroed; what disappears is the trap per page). Its value is that **the cost moves**: all of it is paid at one chosen moment, and the later accesses can never fault.

### 5.2 `mlock` is a different promise

`MAP_POPULATE` only loads the pages now; the kernel may still evict them later under pressure, and a later touch faults again. **`mlock`** pins them: they stay resident until `munlock`, so there is no major fault later, and the memory cannot be swapped. It is the tool for secrets that must not reach swap, and for real-time data. Its limits: a process is limited by `RLIMIT_MEMLOCK` (8,192 KiB on this machine, shown by `ulimit -l`; the 256 MiB `mlock` above succeeded only because the program ran as root), and a locked page is a page the reclaimer cannot use, so a system with too much locked memory has no room left to manage (`mlockall` of a large process is a classic way to cause an out-of-memory condition; from memory). The pairing for a data plane is: reserve the memory (huge pages), pre-fault it, lock it, and do all of that before the first packet.

---

## 6. Memory pressure: swap and the OOM killer

### 6.1 Reclaim, swap and `swappiness`

When free memory falls, the kernel reclaims in this order of cheapness (OS 11 section 1.6 has the mechanism): clean page-cache pages are dropped, dirty file pages are written back and dropped, and anonymous pages are written to swap if there is any. `vm.swappiness` (0 to 200, default 60) sets the relative cost the kernel assumes for swapping anonymous pages against reclaiming file pages: at 100 they are weighted equally, lower makes swap costlier, higher cheaper (kernel document, fetched). A machine with **no swap**, like this one, has no anonymous reclaim at all: an anonymous page is permanent until the process frees it, so the only recourse when memory is exhausted is the OOM killer. This is the common configuration on embedded devices and on containers hosts that prefer a fast death to a thrashing slowdown. The measured cost of the alternative is OS 11 section 6: a process at twice its memory ran 2,000 times slower.

`vm.min_free_kbytes` (67,584 on this machine) is the amount of free memory the kernel tries to keep as a cushion, so that allocations in the kernel (a network interrupt that needs a page) do not have to wait. Setting it very low risks deadlock under load and very high triggers out-of-memory early (kernel document).

### 6.2 How the OOM killer chooses

When an allocation cannot be satisfied even after reclaim, the **out-of-memory killer** picks a process and kills it with `SIGKILL`. By default (`vm.oom_kill_allocating_task = 0`) it scans all candidates and picks the one with the highest **badness**; if that sysctl is set it kills the allocating task instead (kernel document). The badness, from memory, is roughly the process's resident pages plus swap plus page-table pages, in thousandths of the memory available to it (total RAM, or the cgroup limit when the process is in a memory cgroup), plus **`oom_score_adj`**, a per-process value from -1000 to +1000 that is added to the score. So `oom_score_adj = -1000` exempts a process, and +1000 makes it the first choice. `/proc/PID/oom_score` shows the current score, and the kernel writes the kill into `dmesg`.

The program starts two children in a memory cgroup that holds 64 MiB (no swap) and cannot hold both. The first child touches its memory and holds it; 0.3 seconds later the second starts and pushes the cgroup past its limit. The child code, run for each case with chosen sizes and `oom_score_adj` values:

```c
static void child(const char *name, const char *cg, size_t mb, int adj)
{
    char path[256], num[32];
    snprintf(path, sizeof path, "%s/cgroup.procs", cg); snprintf(num, sizeof num, "%d", getpid()); put(path, num);
    int fd = open("/proc/self/oom_score_adj", O_WRONLY);
    snprintf(num, sizeof num, "%d", adj);
    const char *setres = (write(fd, num, strlen(num)) < 0) ? strerror(errno) : "ok";
    close(fd);
    char *a = mmap(NULL, mb * MIB, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    madvise(a, mb * MIB, MADV_NOHUGEPAGE);
    for (size_t off = 0; off < mb * MIB; off += 4096) a[off] = 1;
    snprintf(path, sizeof path, "/proc/%d/oom_score", getpid());
    printf("  %s: touched %zu MiB, asked adj %5d (%s), oom_score_adj now %5ld, oom_score %4ld\n", name, mb, adj, setres, read_long("/proc/self/oom_score_adj"), read_long(path));
    fflush(stdout);
    usleep(700000);
    _exit(0);
}
```

Six runs, each printing what each child saw and what happened:

```text
cgroup limit 64 MiB, no swap; first child starts, then the second
  first : touched 40 MiB, asked adj     0 (ok), oom_score_adj now     0, oom_score  670
  second: touched 40 MiB, asked adj     0 (ok), oom_score_adj now     0, oom_score  670
  first : killed by signal 9
  second: exited normally, status 0
```

```text
cgroup limit 64 MiB, no swap; first child starts, then the second
  first : touched 40 MiB, asked adj     0 (ok), oom_score_adj now     0, oom_score  670
  first : exited normally, status 0
  second: killed by signal 9
```

```text
cgroup limit 64 MiB, no swap; first child starts, then the second
  first : touched 40 MiB, asked adj   500 (ok), oom_score_adj now   500, oom_score 1003
  second: touched 40 MiB, asked adj     0 (ok), oom_score_adj now     0, oom_score  670
  first : killed by signal 9
  second: exited normally, status 0
```

```text
cgroup limit 64 MiB, no swap; first child starts, then the second
  first : touched 50 MiB, asked adj     0 (ok), oom_score_adj now     0, oom_score  670
  second: touched 20 MiB, asked adj     0 (ok), oom_score_adj now     0, oom_score  668
  first : killed by signal 9
  second: exited normally, status 0
```

```text
cgroup limit 64 MiB, no swap; first child starts, then the second
  first : touched 20 MiB, asked adj     0 (ok), oom_score_adj now     0, oom_score  668
  first : exited normally, status 0
  second: killed by signal 9
```

```text
cgroup limit 64 MiB, no swap; first child starts, then the second
  first : touched 40 MiB, asked adj     0 (ok), oom_score_adj now     0, oom_score  670
  second: touched 40 MiB, asked adj  -500 (Permission denied), oom_score_adj now     0, oom_score  670
  first : killed by signal 9
  second: exited normally, status 0
```

Reading the six:

- **Equal processes (40 and 40 MiB, both `adj` 0): the first was killed**, not the second, which was the one that asked for the memory that did not exist. Both had score 670 (about 42 MiB of the 64 MiB limit in thousandths, with the page tables), so the tie went to the one found first. **The OOM killer does not kill "the process that ran out"**; it kills the one it scores worst.
- **40 MiB then 40 MiB with `adj` +500 on the second: the second died**, during its allocation (it never printed). The +500 put its score above the first's, so here the victim and the allocator happened to be the same.
- **`adj` +500 on the first: the first died** (score 1,003 against 670), even though the second was the allocator.
- **A 50 MiB first child and a 20 MiB second (both `adj` 0): the first died** and the small second survived. The big process is the victim whoever asked.
- **A 20 MiB first child and a 50 MiB second: the second died**, the big one again (this time it was also the allocator).
- **Asking for `oom_score_adj` -500 was refused with "Permission denied"** and the adjustment stayed at 0, so the first child died as in the equal case. **Lowering `oom_score_adj` below its current value needs `CAP_SYS_RESOURCE`**, and this sandbox lacks it even as root; the same applies in production to anything that tries to protect itself without the capability (a systemd unit sets it with `OOMScoreAdjust=`, from memory). **Raising it needs no privilege**, which is why a worker can volunteer to be killed first.

The kernel log for the six kills, in order (`anon-rss` is the victim's resident size at the moment of death; the victims of runs two and five were killed partway through their allocation, with 24 and 44 MiB resident):

```text
[ 1011.653642] Memory cgroup out of memory: Killed process 1148 (k5_oom) total-vm:43684kB, anon-rss:41092kB, file-rss:1180kB, shmem-rss:0kB, UID:0 pgtables:124kB oom_score_adj:0
[ 1012.696250] Memory cgroup out of memory: Killed process 1152 (k5_oom) total-vm:43684kB, anon-rss:24456kB, file-rss:996kB, shmem-rss:0kB, UID:0 pgtables:96kB oom_score_adj:500
[ 1013.438125] Memory cgroup out of memory: Killed process 1154 (k5_oom) total-vm:43684kB, anon-rss:41092kB, file-rss:1180kB, shmem-rss:0kB, UID:0 pgtables:120kB oom_score_adj:500
[ 1014.476184] Memory cgroup out of memory: Killed process 1157 (k5_oom) total-vm:53924kB, anon-rss:51332kB, file-rss:1180kB, shmem-rss:0kB, UID:0 pgtables:148kB oom_score_adj:0
[ 1015.553099] Memory cgroup out of memory: Killed process 1161 (k5_oom) total-vm:53924kB, anon-rss:44936kB, file-rss:996kB, shmem-rss:0kB, UID:0 pgtables:132kB oom_score_adj:0
[ 1016.229227] Memory cgroup out of memory: Killed process 1163 (k5_oom) total-vm:43684kB, anon-rss:41092kB, file-rss:1180kB, shmem-rss:0kB, UID:0 pgtables:132kB oom_score_adj:0
```

### 6.3 Global OOM, cgroup OOM and what to configure

The test above is a **cgroup** OOM: the group reached its limit, and the killer chose among the processes of that group, with score measured against the group's limit. The same code path runs system-wide when the whole machine is exhausted, with scores against total RAM. The consequences for operation:

- **A container with a memory limit dies at its limit, alone**, and its own sizing is the cause. The kill shows in the host's `dmesg` as "Memory cgroup out of memory", and in `/proc/vmstat` as an `oom_kill` count.
- **Protecting the critical process** needs `oom_score_adj` lowered (with the capability) on it, or raised on the expendable ones (no privilege needed).
- **`vm.panic_on_oom`** (default 0) can instead panic the machine (1 or 2), for systems where a reboot into a known state is better than killing an arbitrary process, and `vm.oom_kill_allocating_task` kills the requester instead of scanning (kernel document).
- **The best protection is not to reach it**: size cgroup limits from PSS and working-set measurements (section 2), reserve the memory that must exist (section 5), and watch `MemAvailable` and `pswpin`, not `MemFree`, which includes cache that the kernel will happily use.

---
## 7. A diagnosis table

The incidents interviewers describe map onto a short list. Start from the symptom, read the file, name the cause, and name the knob.

| Symptom | Read | Likely cause | Knob |
| --- | --- | --- | --- |
| Process killed, `free` shows memory | `dmesg` for "Out of memory" or "Memory cgroup out of memory" | a cgroup limit, or a burst; `MemFree` ignores reclaimable cache | cgroup `memory.limit_in_bytes`, `oom_score_adj`, size from PSS |
| RSS of a service is huge, the machine is fine | `smaps_rollup` `Pss` per worker | RSS counts shared pages once per process | report PSS (section 2) |
| RSS did not fall after the program freed memory | the allocator's advice (`MADV_FREE` against `MADV_DONTNEED`) | lazy return, pages still resident | allocator options, `malloc_trim` |
| `malloc` succeeds, then the process dies on touch | `Committed_AS` against RAM, `vm.overcommit_memory` | mode 0 heuristic checks each request alone (12 GiB granted on 8 GiB) | touch or `mlock` at start-up; mode 2 for early failure |
| `fork` or `posix_spawn` fails with `ENOMEM`, machine nearly empty | `CommitLimit` against `Committed_AS` | mode 2 charges the child a second copy of writable memory | `vm.overcommit_ratio`, `vfork` or `posix_spawn` with a small helper, mode 0 |
| Memory grew a lot after switching THP on | `AnonHugePages`, `thp_fault_alloc` | sparse touches fill whole 2 MiB pages (512 times, measured) | `enabled=madvise`, mark only dense regions |
| Occasional millisecond stalls at first touch | `compact_stall` and `thp_fault_fallback` in `/proc/vmstat` | huge-page fault stalls for compaction or zeroing 2 MiB | `defrag=madvise` or `defer`, pre-fault at start-up |
| A `MAP_HUGETLB` mapping fails | `HugePages_Total`, `Free`, `Rsvd` | pool empty or too small, or fragmented at boot | `vm.nr_hugepages`, `hugepages=` on the command line |
| Throughput collapses, no kills | `pswpin` and `pswpout` rising, major faults | thrashing (OS 11 section 6) | fewer processes, more memory, a smaller working set |
| Data plane slow after a move to another machine | `AnonHugePages` or `HugePages_*` after start-up | the huge pages were not actually granted | verify at start-up, fail loudly |

---

## What it costs

| Mechanism | What it costs | What it buys | Measured here |
| --- | --- | --- | --- |
| TLB reach with 4 KiB pages | a walk of 4 reads per miss over 8 MiB | none | random reads over 256 MiB: 13 ns |
| THP | latency of a 2 MiB fault, memory bloat, compaction | one TLB entry per 2 MiB | 8 ns random reads; one byte per 2 MiB costs 2 MiB (512 times) |
| `hugetlbfs` | a reserved pool, administrator work | guaranteed huge pages, no swap, no stall after start | first touch of each 2 MiB: 282 and 835 microseconds |
| Overcommit mode 0 | failure at touch time, not at `mmap` | big sparse mappings and fork cost nothing | 12 GiB granted on 8 GiB |
| Overcommit mode 2 | early `ENOMEM`, fork of a large process charged twice | no surprise kill of a process that holds its memory | 3 GiB granted, then 5 GiB refused at a 4,012 MiB limit |
| `MAP_POPULATE` and `mlock` | the faults paid at a chosen time | no later faults; `mlock` also no eviction | 1.1 microseconds per page against 1.7 on demand |
| `MADV_FREE` | RSS that does not fall at once | cheap, lazy return | RSS stayed at 65 MiB |
| `MADV_DONTNEED` | a fresh zero page on the next touch | memory returned now | RSS fell 65 to 1 MiB |
| The OOM killer | a process, chosen by score | the machine survives | the largest process in the cgroup died whoever asked |

## Common mistakes

1. **Summing RSS across processes.** RSS counts shared pages once per process. Sum PSS to get the machine's use (section 2).
2. **Treating virtual size as memory use.** `VmSize` rose by 100 MiB at `mmap` and RSS did not move.
3. **Believing `malloc` returned non-NULL means the memory exists.** Under mode 0, 12 GiB was granted on an 8 GiB machine; the failure arrives at touch time as a kill.
4. **Assuming the OOM killer kills the process that asked.** It kills the highest score, usually the largest resident process, plus the `oom_score_adj` offset (measured in six cases).
5. **Trying to protect a process with a negative `oom_score_adj` without the capability.** The write fails with `EACCES` (measured). Raising it needs no privilege.
6. **Expecting `MADV_FREE` to lower RSS.** It does not until the kernel reclaims; use `MADV_DONTNEED` for an immediate drop.
7. **Enabling THP `always` on a sparse workload.** One byte per 2 MiB touched 2 MiB (512 times the memory in the measurement).
8. **Assuming `MAP_HUGETLB` falls back.** It fails with `ENOMEM` if the pool is empty; the fallback must be written by the program.
9. **Not verifying that huge pages were granted.** Read `AnonHugePages`, `HugePages_Free` and `Rsvd` after start-up, and fail if they are not what the design needs.
10. **Using `HugePages_Total` as the size of all huge-page pools.** It counts only the default size; 1 GiB pages show in `Hugetlb` (measured: 0 against 1,048,576 kB).
11. **Pre-faulting and expecting the memory to stay.** `MAP_POPULATE` loads pages but the kernel may evict them later; only `mlock` pins them.
12. **Reading `MemFree` as available memory.** Reclaimable cache is not free, but it is available; watch `MemAvailable` and `pswpin`.

## Questions out loud

1. *What is the difference between VSZ, RSS and PSS?* VSZ is mapped address space, RSS is resident pages (shared pages counted fully in each process), PSS divides each shared page by the number of sharers; sum PSS for real use.
2. *A service's workers show 2 GiB RSS each and the box has 32 GiB. Is there a problem?* Not necessarily: most of it is shared after `fork`. Compare PSS, and check `Committed_AS` and `MemAvailable`.
3. *What does overcommit mean, and what are the modes?* The kernel grants more virtual memory than exists. Mode 0 refuses only clearly excessive single requests, mode 1 grants everything, mode 2 enforces `CommitLimit` of swap plus a share of RAM and makes `malloc` fail early.
4. *Why can `malloc` succeed and the process still be killed?* The grant is a mapping; pages exist only at first touch, and if the machine cannot supply them then, the OOM killer runs.
5. *How does the OOM killer pick?* The highest badness, about resident plus swap plus page tables in thousandths of the memory limit, plus `oom_score_adj` (from -1000 to +1000). It does not necessarily kill the allocating task (`vm.oom_kill_allocating_task` changes that).
6. *How do you protect a critical process from it?* Lower its `oom_score_adj` (needs `CAP_SYS_RESOURCE`), or raise those of expendable processes (unprivileged), and size limits properly.
7. *What does TLB reach tell you?* Entries times page size. 2,048 entries reach 8 MiB with 4 KiB pages and 4 GiB with 2 MiB pages, so a working set far over 8 MiB walks page tables on most accesses.
8. *Transparent against explicit huge pages?* THP is automatic and best effort (can fall back, split, bloat); `hugetlbfs` is a reserved pool with a guarantee, no swap and no fallback.
9. *What does THP cost?* A long first-touch fault (zeroing 2 MiB), possible compaction stalls, and memory bloat for sparse touches.
10. *How do you avoid page faults on a latency-sensitive path?* Reserve, pre-fault (`MAP_POPULATE` or touching), and `mlock` the memory at start-up; use huge pages for the TLB.
11. *`MADV_FREE` against `MADV_DONTNEED`?* DONTNEED drops the pages immediately (next touch is a zero page); FREE marks them reclaimable and the kernel takes them lazily, so RSS stays until pressure.
12. *When would you run mode 2?* When early, local failure is worth more than the cost of charging forks and large address-space reservations: safety and embedded systems with fixed memory.

---

## Problems

**Where to solve them.** Paper for A to E. All the quantities can be checked on a Linux machine: `/proc/self/smaps_rollup` for A, `/proc/meminfo` and `sysctl vm.overcommit_memory` for B and E, `/proc/PID/oom_score` for D. The programs in the text build with `gcc -O2 -Wall`; mode changes, the huge-page pool and cgroups need root, so try them in a virtual machine.

**How to approach them.** For A, count which pages are shared at each moment and divide by the number of sharers. For B, compute the limit before comparing. For C, write the miss fraction first, then the average cost. For D, compute each score as resident in thousandths of the limit plus the adjustment. For E, do the per-process arithmetic once, then multiply.

### Problem A (E, entry level): RSS and PSS through a fork

A process maps 200 MiB and touches 50 MiB of it (ignore its binary and libraries). It forks, then the child writes 20 MiB of the touched pages. Give RSS and PSS of parent and child (a) before the fork, (b) right after the fork, (c) after the child's writes, with both alive. What does the sum of the two RSS values say, and what does the sum of the PSS values say?

### Problem B (E, entry level): overcommit mode 2

A machine has 16 GiB of RAM and 4 GiB of swap, and `Committed_AS` is 9 GiB. In mode 2 with the default ratio, what is `CommitLimit`? Is a 2 GiB `mmap` granted? A 4 GiB one? What ratio would let the 4 GiB request through? Which of the three modes would grant a 64 GiB request?

### Problem C (M, medium): reach and cost

A program does random reads over a 1 GiB array on a machine with a 2,048-entry second-level TLB. Give the reach and the fraction of reads that miss the TLB with 4 KiB pages and with 2 MiB pages (best case). If a cache miss costs 80 ns and each TLB miss adds a walk of 30 ns on average, give the average read time for each and the ratio.

### Problem D (M, medium): the OOM victim

A cgroup has a limit of 1,024 MiB. Process A has 600 MiB resident and `oom_score_adj` 0, B has 300 MiB and +400, C has 100 MiB and 0. Ignoring page tables, compute the scores, say who is killed first, second and third, and give the smallest integer `oom_score_adj` for A that makes A the first victim.

### Problem E (H, hard): a forking service

A service loads 2 GiB of read-only data into its heap, then forks 200 workers; each worker then writes 150 MiB of its own private data. The machine has 64 GiB and no swap. (a) What is the sum of the workers' RSS and what is the real memory use? (b) What PSS does each worker report? (c) Does it fit, and what does mode 0 do? (d) In mode 2 with the default ratio, how many processes (parent included) can exist, if each is charged 2,048 + 150 MiB? (e) Name two ways to make the design work in mode 2.

---

## Answers

**A.** (a) Before the fork: RSS 50, PSS 50. (b) Right after the fork: both processes have RSS 50 (they map the same 50 MiB) and each PSS 25 (50 MiB shared between two). (c) After the child wrote 20 MiB: the child has 20 MiB of private copies and still shares the other 30; the parent's 20 MiB, whose frames are no longer shared with the child, are now private to it. Each process has RSS 50, and PSS = 20 + 30 / 2 = 35. The sum of RSS is 100 MiB and the sum of PSS is 70 MiB. The real memory used is 70 MiB (50 original frames plus the 20 new copies): PSS adds up to the real figure, RSS over-counts it.

**B.** `CommitLimit = swap + 50 percent of RAM = 4 + 8 = 12 GiB`. With 9 GiB committed, a 2 GiB request brings it to 11 GiB, which is within the limit: granted. A 4 GiB request would bring it to 13 GiB, above 12: refused with `ENOMEM`. To allow 4 GiB the limit must reach 13 GiB: `4 + r * 16 / 100 >= 13` so `r >= 56.25`, a ratio of 57. Mode 1 grants the 64 GiB request (mode 0 refuses it, since it exceeds RAM plus swap, 20 GiB; mode 2 refuses it).

**C.** Reach with 4 KiB pages: `2,048 x 4 KiB = 8 MiB`, so the array needs 262,144 entries and the fraction of reads that find their page in the TLB is about `8 / 1,024 = 0.8 percent`, so **99.2 percent miss**. With 2 MiB pages: reach `2,048 x 2 MiB = 4 GiB`, and the array needs 512 entries, so it fits and the miss fraction is close to 0 (the best case; real chips have fewer 2 MiB entries, but 512 is far below any of them). Average time: 4 KiB pages `80 + 0.992 x 30 = 109.8 ns`; 2 MiB pages about `80 ns`. Ratio `109.8 / 80 = 1.37`, so huge pages make the reads about 37 percent faster, in the same direction as the 13 against 8 nanoseconds measured in section 1 (the measured gain is larger, 1.6 times; the 30 ns walk cost here is an assumption, not a measurement).

**D.** Scores (resident in thousandths of the limit, plus the adjustment): A `600 / 1,024 x 1,000 = 586`; B `300 / 1,024 x 1,000 = 293`, plus 400 is `693`; C `100 / 1,024 x 1,000 = 98`. B is killed first (693), then A (586), then C (98). For A to be the first victim its score must exceed 693: `586 + adj > 693` so `adj > 107`, and the smallest integer is **108**. Note that A is the largest process, yet B, the smaller, is first: the adjustment can outweigh size.

**E.** (a) Each worker's RSS is `2,048 + 150 = 2,198 MiB`; for 200 workers the sum is `439,600 MiB = 429 GiB`, over six times the machine. Real use is the shared 2,048 MiB once plus `200 x 150 = 30,000 MiB`, so `32,048 MiB = 31.3 GiB`. (b) PSS per worker: the 2,048 MiB is shared by 200 processes (the parent exits or is ignored), `2,048 / 200 = 10.24 MiB`, plus 150 private, so `160.24 MiB`; times 200 is `32,048 MiB`, matching the real use. (c) It fits in 64 GiB (49 percent). Mode 0 enforces no total, so every `fork` is granted and the whole service starts. (d) In mode 2 with no swap and ratio 50, `CommitLimit = 32 GiB = 32,768 MiB`. Each process is charged for its private writable mappings, 2,198 MiB. `14 x 2,198 = 30,772 MiB` fits and `15 x 2,198 = 32,970 MiB` does not, so **14 processes** (the parent and 13 workers); the 14th `fork` fails with `ENOMEM` while the machine is half empty (ignoring other commitments, which lower it further). (e) Make the shared data a **read-only mapping** (a read-only private mapping, or a shared file mapping, is not charged: section 3.1), so only the 150 MiB is charged per worker; or give the data to the workers through a `MAP_SHARED` mapping of a file (also not charged as writable private memory). Raising the ratio to 100 would still allow only `65,536 / 2,198 = 29` processes, so ratio changes cannot fix it; moving to mode 0 would, at the price of the late failure.

---

## Cheat sheet

- Reach = entries x page size. 2,048 entries: 8 MiB with 4 KiB pages, 4 GiB with 2 MiB pages, 2,048 GiB with 1 GiB pages. Walk reads: 4, 3, 2.
- Sizes: VSZ (mapped), RSS (resident, shared pages counted in every process), PSS (shared pages divided by sharers; sum it). `MADV_DONTNEED` drops RSS now; `MADV_FREE` lazily.
- Overcommit: mode 0 refuses a single request above RAM plus swap (measured: 9 GiB no, 7 GiB yes, 4 x 3 GiB yes); mode 1 grants all; mode 2 limit = swap + ratio percent of RAM (`CommitLimit`, `Committed_AS`).
- Charges: private writable and shared anonymous mappings at full size; read-only private anonymous, shared or read-only file mappings free; `fork` charges the child again.
- THP: `enabled` always, madvise, never; `defrag`; `khugepaged`; counters `AnonHugePages`, `thp_fault_alloc`, `thp_fault_fallback`. Costs: a long first touch, compaction stalls, up to 512 times bloat on sparse touches.
- `hugetlbfs`: pool via `vm.nr_hugepages` or `hugepages=`; `MAP_HUGETLB` fails with `ENOMEM` if empty; `HugePages_Total`, `Free`, `Rsvd`, `Surp`; not swappable; 1 GiB pages show in `Hugetlb`.
- Pre-fault: demand about 1.7 microseconds per page, `MAP_POPULATE` and `mlock` about 1.1, later touch 0.03; only `mlock` pins; `RLIMIT_MEMLOCK` limits non-root.
- OOM: score about resident plus swap plus page tables in thousandths of the limit, plus `oom_score_adj` (-1000 to +1000); the largest process dies whoever asked; lowering the adjustment needs `CAP_SYS_RESOURCE`; `vm.oom_kill_allocating_task` and `vm.panic_on_oom` change the policy.
- Watch `MemAvailable`, `Committed_AS`, `pswpin`, `pswpout`, `thp_fault_fallback`, `oom_kill` in `/proc/vmstat`, and `dmesg`.

## Sources

Measured on the VM described above (sysctls, the huge-page pool and the cgroup restored afterwards). Fetched and used: the four kernel documents below. Not fetched, and stated from memory in the text: the OOM badness formula and its use of the limit (the measurements reproduce its behaviour), `malloc_trim`, allocator `MADV_FREE` options, `systemd`'s `OOMScoreAdjust=`, `mlockall` side effects, THP-switch incidents, ARM64 granule configurations in Linux kernels, and the `/proc/PID/smaps_rollup` field list.

- [Linux kernel: Overcommit accounting](https://docs.kernel.org/mm/overcommit-accounting.html), fetched: the three modes, `CommitLimit` in mode 2 and what each mapping type is charged
- [Linux kernel: Transparent hugepage support](https://docs.kernel.org/admin-guide/mm/transhuge.html), fetched: `enabled`, `defrag`, `khugepaged`, counters, `madvise` flags, the waste caveat
- [Linux kernel: Documentation for /proc/sys/vm](https://docs.kernel.org/admin-guide/sysctl/vm.html), fetched: `overcommit_memory`, `overcommit_ratio`, `swappiness`, `min_free_kbytes`, `panic_on_oom`, `oom_kill_allocating_task`, `nr_hugepages`
- [Linux kernel: HugeTLB pages](https://docs.kernel.org/admin-guide/mm/hugetlbpage.html), fetched: the pool, reservations, the `meminfo` fields, 1 GiB pages, boot-time allocation
- [OS 10: Memory management](10_memory_management.md), [OS 11: Virtual memory](11_virtual_memory.md) and [CO 15: Virtual memory and the TLB](../04_computer_organization/15_virtual_memory_and_the_tlb.md), the neighbouring material
