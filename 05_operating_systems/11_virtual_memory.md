# OS 11: Virtual memory: demand paging and replacement

*Part of the [Operating systems plan](00_plan.md) · Time: about 10 hours (7 reading and experiments, 3 problems) · You need: [OS 10](10_memory_management.md) (address spaces, paging and the page-table entry) and [CO 15](../04_computer_organization/15_virtual_memory_and_the_tlb.md) (the x86-64 walk, the TLB, and the measured cost of a minor and a major fault)*

OS 10 made each process's address space and gave it pages. This tutorial is about the next step, the one that makes the whole thing worth having: a process does not need all its pages in memory at once, so the operating system can keep only some of them in physical frames and fetch the rest when they are touched. That is **demand paging**. It raises three questions that every systems interview asks in some form. What exactly happens on a page fault, and what does it cost? When memory is full, which page do you evict (FIFO, OPT, LRU, Clock and the rest, and why does FIFO sometimes get worse with more memory)? And what goes wrong when the pages that are needed do not fit (thrashing, and what a working set is)? CO 15 already holds the hardware walk and the cost of one fault, so this chapter does not repeat them. It adds the things CO 15 leaves out: every replacement algorithm simulated on the same inputs, the real accessed bit read from a running process, frame allocation, and a real thrashing curve measured by squeezing a process into a memory-limited cgroup with a swap file. The exam work is the same each time: trace a reference string by hand, compute an effective access time with faults, and size a working set.

By the end you can:

- describe demand paging and the page-fault sequence step by step, and tell the three outcomes apart: a **minor** fault (the page is in memory, only the mapping is missing), a **major** fault (the page must be read from storage) and an **invalid** fault (a bad access, which becomes `SIGSEGV`); with measured costs (anonymous first touch 3.02 / 2.84 microseconds per page on this machine, a random cold file read 46.37 / 28.04);
- compute the effective access time with faults, `EAT = (1 - p) * m + p * F`, and say why the fault rate must be tiny (measured: with a 37 microsecond fault, one fault in 3,690 accesses already costs 10 percent; with an 8 millisecond disk, one in 799,990);
- run FIFO, LRU, OPT, Clock (second chance), NRU, LFU and MRU on a reference string by hand, and say what a simulation shows (textbook string, 3 frames: OPT 9, LRU 12, Clock 14, FIFO 15 faults; on a phased workload with 100 frames FIFO faulted 33.45 percent, LRU 24.70 and OPT 9.89);
- show Bélády's anomaly with the string `1 2 3 4 1 2 5 1 2 3 4 5` (FIFO: 9 faults with 3 frames, 10 with 4) and explain why LRU and OPT cannot show it (the stack property);
- explain the loop that defeats LRU and FIFO (a loop over 101 pages with 100 frames: 100 percent faults; OPT and MRU 1.49 percent) and the case where nothing helps (uniform random access);
- read the hardware accessed bit of a real process through `/proc/self/clear_refs` and `smaps` (measured: touching 10 MiB after a clear reported 9,316 KiB referenced, within about 10 percent) and say how that is the raw material of Clock and of working-set estimation;
- divide frames among processes (equal, proportional, priority; global and local replacement) and say which one a cgroup memory limit is;
- state the working-set model and the page-fault-frequency scheme, and read a thrashing curve (measured: 13 ns per access while the data fit in a 64 MiB cgroup, 5747.1 / 6720.8 ns at 72 MiB and 26053.0 / 23657.2 ns at 128 MiB);
- say what copy-on-write, memory-mapped files and swap space add to demand paging, with the measured behaviours (all-zero pages are not written to swap; read-ahead turns 65,536 page touches into 1 major fault).

**Where the numbers come from.** Programs were compiled with GCC 13.3 (`-O2 -Wall -Wextra`) and run on a Linux 6.18.44 kernel in a Firecracker micro-VM with 2 vCPUs, 8 GiB of memory and 4 KiB pages, as root (needed for the swap file, the memory cgroup and `clear_refs`). Swap is not enabled on this machine by default; the programs that need it create a 512 MiB swap file, enable it, and remove it afterwards. The thrashing runs use a version 1 memory cgroup with a 64 MiB limit. The algorithm definitions and the stated properties (AMAT, the policies, Clock, thrashing, the swap daemon and its watermarks) are from the OSTEP chapters on beyond-physical memory (both fetched; see Sources), and the `clear_refs` and `smaps` semantics are from the kernel's `proc` document (fetched, first part only). The working-set model, page-fault frequency and the allocation schemes are from memory except where a program reproduces them. Facts from general knowledge are marked "from memory".

**Data and timing.** The replacement simulations are counts on fixed pseudo-random inputs, so they are exactly repeatable. The fault costs and the thrashing curve are timings of a live system and vary from run to run: the table of thrashing results below shows two runs side by side, and where they disagree it says so. Two caveats apply to every major-fault number. The virtual disk of a Firecracker guest is probably backed by the host's page cache, so a "major fault" here costs tens of microseconds, not the milliseconds of a rotating disk; the shape of the effects is real, the absolute disk latency is not. And one of the thrashing runs was killed by the cgroup's out-of-memory handler, which is reported where it happened.

---

## 1. Demand paging

### 1.1 The idea and the valid bit

Without demand paging the OS would load a whole program (all its code, data and stack) before running it, and a process could never be larger than memory. Demand paging loads a page **only when it is first needed**. A page-table entry has a present (valid) bit ([OS 10](10_memory_management.md) section 5.3): when it is clear the hardware cannot translate the address and traps to the kernel. The kernel decides whether the access was legal, brings the page in if so, sets the bit and restarts the instruction. The process never sees any of this except as a delay.

This is what makes four common facts possible. A process can have more virtual memory than there is RAM. A program starts running after its first page arrives, not after the whole binary does. A `malloc` of a gigabyte costs nothing until the pages are touched (section 1.4). And many processes can share one copy of a library, because each maps the same file pages.

### 1.2 The page-fault sequence

When the CPU executes an instruction whose address has no valid translation, the following happens (OSTEP gives the hardware and software control flow; the detail is standard):

1. The MMU raises a **page fault** exception with the faulting address (on x86-64, in `CR2`) and the cause (read or write, user or kernel, present or not).
2. The kernel's fault handler looks up the faulting address in the process's list of **memory regions** (on Linux, the VMAs, `vm_area_struct`). This is the legality check.
3. If no region covers the address, or the region does not allow this kind of access (a write to a read-only region), the fault is **invalid**: the process receives `SIGSEGV`. Nothing is loaded.
4. Otherwise the fault is legal and the kernel finds out why the page is not mapped, which splits into the **minor** and **major** cases below. It obtains a free frame, which may mean evicting another page first (sections 3 and 4).
5. For a major fault the kernel starts a storage read into the frame and **blocks the process**; the scheduler runs something else (this is why a fault is a good time to context-switch, and why a fault costs the process milliseconds but costs the machine nothing if other work is ready). When the read completes an interrupt wakes the process.
6. The kernel updates the page-table entry (frame number, present bit, permission bits), possibly flushes a stale TLB entry, and returns. The faulting instruction runs again, and this time it succeeds.

The restart in step 6 is a hardware requirement: the instruction must be restartable, so a fault in the middle of an instruction must leave no half-done side effects. Architectures have to be designed for this (a block-copy instruction that faults halfway needs either to touch both ends first or to be able to resume).

### 1.3 Minor, major and invalid

| Kind | The page is... | The work | Typical cost |
| --- | --- | --- | --- |
| minor | in memory already, or needs no I/O (a new zeroed page, a page in the page cache or swap cache, a copy-on-write copy) | find or make a frame, fix the page table | microseconds |
| major | only on storage (a file page not cached, a page swapped out) | the above plus a storage read and a context switch | tens of microseconds on an SSD or host-cached disk, milliseconds on a rotating disk |
| invalid | not part of any legal access | deliver `SIGSEGV` | microseconds, and the process dies by default |

The measurement program touches memory in six situations and counts the faults the kernel reports for each (`getrusage` minor and major fault counters). The first two share a mapping: the first write to each page of fresh anonymous memory, and the second write to the same pages.

```c
    /* 1. First touch of anonymous memory (a minor fault that also zeroes a frame). */
    char *a = mmap(NULL, anon, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    madvise(a, anon, MADV_NOHUGEPAGE);
    long mi = minflt(), ma = majflt(); double t0 = now();
    for (size_t off = 0; off < anon; off += PG) a[off] = 1;
    report("anonymous, first write", anon / PG, now() - t0, mi, ma);
    mi = minflt(); ma = majflt(); t0 = now();
    for (size_t off = 0; off < anon; off += PG) a[off] = 2;
    report("anonymous, second write (no fault)", anon / PG, now() - t0, mi, ma);
    munmap(a, anon);
```

Output of the whole program, two runs (first run, second run; the programs were run twice and both are shown so the spread is visible):

```text
anonymous, first write               65536 pages       3.02 us/page  minor faults   65539  major faults       0
anonymous, second write (no fault)   65536 pages       0.03 us/page  minor faults       0  major faults       0
file cache residency after writing it: 100%
file in page cache, read             65536 pages       0.05 us/page  minor faults     271  major faults       0
file cache residency after fadvise DONTNEED: 0%
cold file, sequential read           65536 pages       1.66 us/page  minor faults    2052  major faults       1
cold file, random read                4096 pages      46.37 us/page  minor faults       0  major faults    3992
copy-on-write, child writes          16384 pages       2.14 us/page  minor faults   16386  major faults       0
pages swapped out by MADV_PAGEOUT: 16384 of 16384 (pswpout 651512 -> 667896)
swapped-out pages, read back         16384 pages       0.79 us/page  minor faults   16384  major faults       0
swap-ins counted by the kernel (pswpin): 0
all-zero pages written to swap by MADV_PAGEOUT: 0 of 16384
```

```text
anonymous, first write               65536 pages       2.84 us/page  minor faults   65539  major faults       0
anonymous, second write (no fault)   65536 pages       0.03 us/page  minor faults       0  major faults       0
file cache residency after writing it: 100%
file in page cache, read             65536 pages       0.04 us/page  minor faults     271  major faults       0
file cache residency after fadvise DONTNEED: 0%
cold file, sequential read           65536 pages       1.28 us/page  minor faults    2052  major faults       1
cold file, random read                4096 pages      28.04 us/page  minor faults       0  major faults    3992
copy-on-write, child writes          16384 pages       2.32 us/page  minor faults   16386  major faults       0
pages swapped out by MADV_PAGEOUT: 16384 of 16384 (pswpout 860344 -> 876728)
swapped-out pages, read back         16384 pages       0.75 us/page  minor faults   16384  major faults       0
swap-ins counted by the kernel (pswpin): 0
all-zero pages written to swap by MADV_PAGEOUT: 0 of 16384
```

Reading it:

- **The first write to an anonymous page costs about 3 microseconds** (3.02 and 2.84 in the two runs), against 0.03 for the second write. That is a minor fault: one trap, one frame taken from the free list and zeroed (4 KiB of stores), one page-table entry written. 65,536 pages gave 65,539 minor faults, one each and no major ones. `MADV_NOHUGEPAGE` keeps transparent huge pages out of it so that the count is per 4 KiB page ([CO 15](../04_computer_organization/15_virtual_memory_and_the_tlb.md) has the huge-page effect).
- **A file in the page cache costs 0.04 to 0.05 microseconds per page with only 271 faults for 65,536 pages.** The kernel mapped many pages per fault ("fault-around", and possibly large folios; this is from memory and the program does not separate the two). So the cost of touching cached file pages is far below the cost of an anonymous first touch: no zeroing, and one fault serves many pages.
- **A cold file read sequentially cost 1.28 to 1.66 microseconds per page and one major fault.** The kernel noticed the sequential pattern and **read ahead**, so almost every later touch found its page already in the cache. The 2,052 minor faults are the fault-around mapping of those cached pages.
- **A cold file read in random order cost 46 and 28 microseconds per page in the two runs, with 3,992 major faults of 4,096 touches.** Random access defeats read-ahead (the program says so with `MADV_RANDOM`), so nearly every touch is a real storage read. This is the machine's major-fault cost, and the spread between runs is large (the mean, 37 microseconds, is the figure used for `F` in section 2).
- **A copy-on-write write costs about 2 microseconds per page** (section 1.4).
- **Swapped-out pages read back at 0.75 to 0.79 microseconds per page with zero major faults.** This is not a true swap-in and is explained in section 1.6; do not quote it as the cost of reading from swap.

### 1.4 Zero-fill, copy-on-write and why `malloc` is free

Two kinds of minor fault deserve names, because they are how an OS avoids work.

**Zero-fill on demand.** Anonymous memory (heap, stack, `mmap` with `MAP_ANONYMOUS`) has no file behind it. When a process touches such a page for the first time the kernel gives it a zeroed frame. Memory a process has asked for but not touched costs no frame at all, which is why `malloc(1 << 30)` returns at once and why a program's resident size can be far below its virtual size. ([OS 10](10_memory_management.md) section 5.4 shows the page-table side: a page that is only read maps a shared zero page.)

**Copy-on-write (COW).** At `fork` the child must have a copy of the parent's address space, but copying it all would be wasted if the child calls `exec` at once (this is the usual case). Instead the kernel gives the child the **same frames**, marks both processes' entries read-only, and counts the sharing. The first write by either process faults; the kernel sees that the page is a COW page, allocates a new frame, copies the 4 KiB, points the writer's entry at the copy with write permission, and restarts. Pages that are never written are never copied. ([OS 02](02_processes.md) section 6 covers `fork` and COW in depth.) The program below has a child write every page of a 64 MiB region after `fork`:

```c
    /* 5. Copy-on-write after fork: the child writes every page of a 64 MiB region. */
    size_t cow = 64UL << 20;
    char *c = mmap(NULL, cow, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    madvise(c, cow, MADV_NOHUGEPAGE);
    for (size_t off = 0; off < cow; off += PG) c[off] = 1;
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        long cmi = minflt(); double ct = now();
        for (size_t off = 0; off < cow; off += PG) c[off] = 2;
        report("copy-on-write, child writes", cow / PG, now() - ct, cmi, 0);
        fflush(stdout);
        _exit(0);
    }
    waitpid(pid, NULL, 0);
    munmap(c, cow);
```

The child's 16,384 writes cost 2.14 and 2.32 microseconds per page (the two runs above), with 16,386 minor faults: one per page, each doing a copy. That is cheaper than a first touch of anonymous memory (which also has to zero the frame, and here the copy replaces the zeroing), and it is paid only for the pages the child actually changes. Note the `fflush(stdout)` before `fork`: without it the child inherits the unflushed output buffer and prints it twice, a bug the first version of this program had.

### 1.5 Memory-mapped files

`mmap` of a file makes the file's pages part of the address space, filled by demand paging from the **page cache** (the OS's cache of file data). Touching a mapped page faults; the kernel finds the page in the cache (minor) or reads it (major); two processes that map the same file share the same cached frames. The cold-file lines in the output above are exactly this. Two things follow. **Read-ahead** makes sequential access cheap (1.3 to 1.7 microseconds per page against 28 to 46 for random), so a program that reads a mapped file in order should do so, and one that jumps around should not expect help (and may call `madvise` to say which: `MADV_SEQUENTIAL`, `MADV_RANDOM`, `MADV_WILLNEED`). And the file is the backing store: a page of a file mapping that is evicted needs no swap space, because a clean page can be dropped and re-read from the file, and a dirty `MAP_SHARED` page is written back to the file.

### 1.6 Swap space

Anonymous pages have no file, so when memory is short the only way to free their frames is to write them to **swap space** (a swap partition or file) and mark the page-table entry not present, with the entry holding the swap location instead of a frame number (OSTEP: the present bit tells the hardware the page is not in memory, and the OS reuses the entry's other bits for the disk address). A later touch is a major fault that reads the page back.

The program tested this by asking the kernel to page out a 64 MiB region with `MADV_PAGEOUT` and then reading it:

```c
    /* 6. Swap-in: touch 64 MiB, ask the kernel to page it out, touch it again. */
    size_t sw = 64UL << 20;
    char *w = mmap(NULL, sw, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    madvise(w, sw, MADV_NOHUGEPAGE);
    for (size_t off = 0; off < sw; off += PG) w[off] = (char)(off / PG % 251 + 1);   /* nonzero: all-zero pages are not written to swap */
    long out0 = vmstat("pswpout"), in0 = vmstat("pswpin");
    if (madvise(w, sw, MADV_PAGEOUT) != 0) perror("madvise PAGEOUT");
    usleep(300000);
    madvise(w, sw, MADV_PAGEOUT);                      /* a second pass drops the pages that are now clean copies in the swap cache */
    usleep(300000);
```

Three measured findings, all in the output above:

- **All 16,384 pages were written to swap** (the kernel's `pswpout` counter rose by that much), so the swap file works as expected.
- **Reading them back was not a swap-in.** The kernel's `pswpin` counter stayed at 0 and the touches were minor faults at 0.75 to 0.79 microseconds. After a page is written to swap the kernel keeps its frame for a while in the **swap cache**, with the page marked clean; the later touch found the page there and only re-mapped it. This is a real mechanism (a page that is written out and wanted again soon costs no read), but it means this test measures the swap cache, not the swap device. True swap-ins are in the thrashing runs of section 6, where memory pressure forces the frames away.
- **Pages whose content is all zero are not written to swap at all** (the last line: 0 of 16,384 written). The kernel recognises a page of zeros and drops it, recreating it as a zero page on the next touch. This is a measured behaviour on this kernel, found because the first version of the test wrote zeros and swapped nothing; a test of swap that uses zero-filled memory proves nothing about swap.

OSTEP's description of how the swap daemon is driven fits what is observed: when the number of free pages falls below a **low watermark**, a background thread evicts pages until a **high watermark** of free pages is reached, and it **clusters** writes so that several pages go out in one I/O. (This is the "page daemon" or "swap daemon" of the text; Linux's `kswapd` is the same idea, from memory.) Evicting in the background, before memory is completely full, keeps a page fault from having to wait for a write to make room.

---

## 2. Effective access time with page faults

### 2.1 The formula

If `m` is the time of a memory access, `F` the time to service a page fault and `p` the **page-fault rate** (the probability that an access faults, 0 to 1), the effective access time is:

`EAT = (1 - p) * m + p * F`

OSTEP writes the same thing as the average memory access time, `AMAT = T_M + P_Miss * T_D`, with `T_M` the cost of a memory access and `T_D` the cost of going to disk, and gives the same lesson: with `T_M` of 100 nanoseconds and `T_D` of 10 milliseconds, a hit rate of 90 percent gives an average of about 1 millisecond (ten thousand times slower than memory) and a hit rate of 99.9 percent gives about 10.1 microseconds. The program computes the formula for four fault costs, two of them measured above:

```c
static double eat(double p, double m_ns, double fault_ns) { return (1 - p) * m_ns + p * fault_ns; }
```

```text
memory access 100 ns
fault service time                                     p = 1e-3     p = 1e-5     p = 1e-7   largest p for a 10% slowdown
minor fault, 2.9 us (measured, anonymous first touch)     102.8 ns     100.0 ns     100.0 ns   3.57e-03 (one fault per 280 accesses)
major fault, 37 us (measured, random cold file)        136.9 ns     100.4 ns     100.0 ns   2.71e-04 (one fault per 3690 accesses)
major fault, 100 us (a typical SSD)                    199.9 ns     101.0 ns     100.0 ns   1.00e-04 (one fault per 9990 accesses)
major fault, 8 ms (a rotating disk)                   8099.9 ns     180.0 ns     100.8 ns   1.25e-06 (one fault per 799990 accesses)
```

### 2.2 What the table says

- **A minor fault is cheap enough to tolerate in bulk.** At 2.9 microseconds (the mean of the two first-touch figures) a fault rate as high as 0.36 percent (one in 280 accesses) costs only 10 percent. A program that first-touches fresh memory pays this, and it is the price of zero-fill.
- **A major fault is 13 times that.** With the measured 37 microsecond fault (the mean of the two random-read figures), 10 percent slowdown is reached at one fault in 3,690 accesses. The fault rate must be below about 0.03 percent to stay within 10 percent.
- **A rotating disk makes the tolerable rate absurdly small.** At 8 milliseconds the limit is one fault in 799,990 accesses, a rate of 1.25 in a million. This is the arithmetic behind the sentence "demand paging only works because programs have locality": a program whose accesses were spread uniformly over data twice the size of memory would fault on half its accesses (section 6 measures exactly this) and run thousands of times slower with a disk, not 10 percent slower.
- **The same formula prices a replacement policy.** If policy A faults 33 percent less often than policy B at the same memory size, then for a major-fault cost the run time is nearly in the same ratio, because the fault term dominates. That is why section 4 counts faults and nothing else.

Two refinements that exam questions add, both still just arithmetic. **Dirty victims double the I/O:** if the page chosen for eviction is modified it must be written before the frame is reused, so the fault service time is a read plus a write. With a fraction `d` of dirty victims and a read or write of `t`, `F = (1 + d) * t`. (Check: 8 ms per transfer and 40 percent dirty victims gives 11.2 ms, which is `0.6 * 8 + 0.4 * 16`.) **A TLB sits in front:** the `m` above is itself an effective memory time that includes the TLB and page-walk cost of [OS 10](10_memory_management.md) section 7, so a full model nests the two.

---
## 3. Page replacement: when memory is full

### 3.1 The problem

Demand paging keeps filling frames. Sooner or later a fault finds no free frame, and the OS must choose a **victim**: a page to evict so its frame can be reused. If the victim is clean (unchanged since it was loaded or last written) the frame is reused at once, since a copy is on storage already. If it is **dirty** it must be written out first, which is what the **modified (dirty) bit** in the page-table entry records ([OS 10](10_memory_management.md) section 5.3). A policy that prefers clean victims saves a write per fault, which in section 2.2 was a doubling of the fault cost.

The fault pattern depends on one thing the OS cannot see in advance: which pages the program will touch next. The standard way to study policies is a **reference string**, the sequence of page numbers a program touches (consecutive touches of the same page collapse into one, since the second cannot fault). Given a string and a number of frames, a policy produces a number of faults, and fewer is better. Two facts frame everything. The first touch of each distinct page always faults (a **cold miss**), so the number of distinct pages is a floor for every policy. And more frames never hurt in an ideal world: the fault count falls as frames rise, reaching the floor when all distinct pages fit. The one famous exception is FIFO (section 4.3).

### 3.2 The policies compared on one string

The simulator below implements seven policies. It is one function: the policy chooses `v`, the index of the frame to evict, and everything else (hits, loading, bookkeeping) is shared. Read the switch as the definitions of the policies:

```c
static long simulate(int policy, int nf)
{
    int frame[MAXF], loaded[MAXF], last[MAXF], cnt[MAXF], rbit[MAXF], mbit[MAXF], where[MAXPAGE];
    int used = 0, hand = 0;
    long faults = 0;
    static int nxt[MAXREF];                             /* OPT: next use of the page referenced at each position */
    if (policy == OPT) {
        static int seen[MAXPAGE];
        for (int p = 0; p < MAXPAGE; p++) seen[p] = 1 << 30;
        for (int i = nref - 1; i >= 0; i--) { nxt[i] = seen[ref[i]]; seen[ref[i]] = i; }
    }
    static int nextuse_of_frame[MAXF];
    memset(where, -1, sizeof where);
    for (int i = 0; i < nref; i++) {
        int p = ref[i], f = where[p];
        if (f >= 0) {                                    /* hit */
            last[f] = i; cnt[f]++; rbit[f] = 1; if (wr[i]) mbit[f] = 1; nextuse_of_frame[f] = nxt[i];
        } else {                                         /* fault */
            faults++;
            if (used < nf) f = used++;
            else {
                int v = 0;
                switch (policy) {
                case FIFO: for (int k = 1; k < nf; k++) if (loaded[k] < loaded[v]) v = k; break;
                case LRU:  for (int k = 1; k < nf; k++) if (last[k] < last[v]) v = k; break;
                case MRU:  for (int k = 1; k < nf; k++) if (last[k] > last[v]) v = k; break;
                case LFU:  for (int k = 1; k < nf; k++) if (cnt[k] < cnt[v] || (cnt[k] == cnt[v] && loaded[k] < loaded[v])) v = k; break;
                case OPT:  for (int k = 1; k < nf; k++) if (nextuse_of_frame[k] > nextuse_of_frame[v]) v = k; break;
                case CLOCK:                              /* second chance: skip and clear referenced pages */
                    for (;;) { if (!rbit[hand]) { v = hand; hand = (hand + 1) % nf; break; } rbit[hand] = 0; hand = (hand + 1) % nf; }
                    break;
                case NRU: {                              /* lowest class 2*R + M, ties broken by position */
                    int best = 9;
                    for (int k = 0; k < nf; k++) { int c = 2 * rbit[k] + mbit[k]; if (c < best) { best = c; v = k; } }
                    break;
                }
                }
                where[frame[v]] = -1;
                f = v;
            }
            frame[f] = p; where[p] = f; loaded[f] = i; last[f] = i; cnt[f] = 1; rbit[f] = 1; mbit[f] = wr[i]; nextuse_of_frame[f] = nxt[i];
        }
        if (policy == NRU && i % 1000 == 999) for (int k = 0; k < used; k++) rbit[k] = 0;   /* the periodic clear */
    }
    (void)next_use;
    return faults;
}
```

Every program in this section runs these same functions on fixed inputs, so the numbers are exactly repeatable. The first input is the textbook string `7 0 1 2 0 3 0 4 2 3 0 3 2 1 2 0 1 7 0 1` with 3 frames:

```text
textbook string of 20 references, 3 frames (faults):
                             FIFO    LRU    OPT  Clock    LFU    MRU
3 frames                       15     12      9     14     13     16
```

OPT faults 9 times (the lower bound), LRU 12, LFU 13, Clock 14, FIFO 15 and MRU 16. The rest of the section explains each.

---

## 4. The algorithms

### 4.1 OPT, the best possible (and unrealisable)

**OPT** (Bélády's optimal algorithm, also MIN) evicts the page whose **next use is farthest in the future**. It has the fewest faults of any algorithm on a given string and frame count. It cannot be implemented in a real system, because it needs the future, so its role is the yardstick: a policy is "within 10 percent of OPT" or "twice OPT". OSTEP uses the same role for it. In the simulator OPT is computed by a backward pass that records, for each reference, the index of the next reference to the same page (`nxt[]`).

### 4.2 Randomness and the no-locality case

**Random** evicts any page. OSTEP uses it as a baseline; it has no input that is worst for it (from memory). When the access pattern has no locality at all, no policy can do better than another: all fault at the same rate. The measured case is uniform random access over 1,000 pages:

```text
uniform random over 1,000 pages, 50,000 references (fault rate, percent):
                             FIFO    LRU    OPT  Clock
100 frames                  90.02  90.00  60.38  90.02
500 frames                  50.40  50.52  20.22  50.46
900 frames                  11.32  11.01   3.89  11.11
```

With 100 frames of 1,000, every policy faults on 90 percent of references (10 percent chance the page is resident); with 500 frames, 50 percent; with 900, about 11 percent. FIFO, LRU and Clock agree to within a fraction of a point, and OPT (which can see the future) does much better at 60, 20 and 3.9 percent. The lesson is not about OPT: it is that **a replacement policy can only exploit locality that exists**. When it does not, the only fix is more memory.

### 4.3 FIFO and Bélády's anomaly

**FIFO** evicts the page that has been in memory longest. It needs only a queue of frames (the simulator uses the load time) and ignores how the page has been used: the oldest page may be the one in constant use. It also has a surprising property. Take the reference string `1 2 3 4 1 2 5 1 2 3 4 5` and count FIFO faults:

```text
Belady's string 1 2 3 4 1 2 5 1 2 3 4 5 (faults by number of frames):
                             FIFO    LRU    OPT
1 frames                       12     12     12
2 frames                       12     12      9
3 frames                        9     10      7
4 frames                       10      8      6
5 frames                        5      5      5
6 frames                        5      5      5
7 frames                        5      5      5
```

FIFO takes **9 faults with 3 frames and 10 with 4**. More memory made it worse. This is **Bélády's anomaly**. The hand trace shows why. With 3 frames: faults on 1, 2, 3, then 4 evicts 1 (memory 2 3 4), 1 evicts 2 (3 4 1), 2 evicts 3 (4 1 2), 5 evicts 4 (1 2 5), then 1 and 2 hit, 3 evicts 1 (2 5 3), 4 evicts 2 (5 3 4), 5 hits: 9 faults. With 4 frames: 1 2 3 4 fault, 1 and 2 hit, then 5, 1, 2, 3, 4, 5 each fault because every page is evicted just before its next use (the oldest page is always the one needed soon): 10 faults. The cause is that a FIFO memory of `n + 1` frames does not always contain the contents of a memory of `n` frames, so extra memory can hold the wrong pages.

LRU and OPT never do this, because they are **stack algorithms**: the set of pages in memory with `n` frames is always a subset of the set with `n + 1` (from memory, as a stated property; the LRU and OPT columns above are non-increasing, as the property predicts: LRU 12, 12, 10, 8, 5 and OPT 12, 9, 7, 6, 5). Note LRU is not better than FIFO at every frame count (at 3 frames LRU had 10 and FIFO 9 on this string): it is a property of the whole curve, not of every point.

### 4.4 LRU: use the past to predict the future

**LRU** (least recently used) evicts the page unused for the longest time, on the bet that the recent past predicts the near future (locality). It is close to OPT when the bet holds. OSTEP shows it beating FIFO and Random on a workload where 80 percent of references go to 20 percent of pages. On the textbook string above, LRU's 12 against FIFO's 15 is the same effect.

Exact LRU is expensive. It needs the time of the last use of **every page, updated on every memory reference**, or a stack of pages reordered on every reference. No hardware does that for main memory; the update would cost more than the faults it saves. What hardware does provide is one bit per page, the accessed (reference) bit, which it sets on access. Real systems approximate LRU from that bit, as Clock does next.

### 4.5 Clock (second chance) and NRU

**Second chance / Clock.** Arrange the frames in a circle with a **hand**. Each frame has a use (reference) bit that the hardware sets when the page is touched. On a fault the hand sweeps forward: a frame with use bit 1 has the bit cleared to 0 and is passed over (a second chance); the first frame with use bit 0 is the victim, and the new page takes its place with bit 1. A page that is used often keeps getting its bit set between sweeps and survives; a page not used since the hand last passed is evicted. OSTEP describes the algorithm in these terms, and also the improvement to prefer **clean** pages over dirty ones. The simulator's `CLOCK` case is exactly the loop. Here is the textbook string traced with 3 frames (each frame shown as page and use bit; the hand points at the frame it will examine next; the trace came from a short script that mirrors the simulator, and its total of 14 faults equals the simulator's):

| Ref | Result | Frames (page, use bit) | Hand |
| --- | --- | --- | --- |
| 7 | fault | 7:1 . . | 1 |
| 0 | fault | 7:1 0:1 . | 2 |
| 1 | fault | 7:1 0:1 1:1 | 0 |
| 2 | fault | 2:1 0:0 1:0 | 1 |
| 0 | hit | 2:1 0:1 1:0 | 1 |
| 3 | fault | 2:1 0:0 3:1 | 0 |
| 0 | hit | 2:1 0:1 3:1 | 0 |
| 4 | fault | 4:1 0:0 3:0 | 1 |
| 2 | fault | 4:1 2:1 3:0 | 2 |
| 3 | hit | 4:1 2:1 3:1 | 2 |

Read the fourth row: the hand was at frame 0 with all three use bits set, so it cleared all three (a full sweep) and came round to evict the page in frame 0, which was 7. That worst case (every bit set) makes Clock behave as FIFO for one sweep; that is why its 14 faults sit near FIFO's 15 on this short string and well below it on longer ones.

**NRU (not recently used)** is a coarser relative. The OS periodically clears all use bits (the simulator clears every 1,000 references) and classifies each page by its use and modify bits: class 0 (not used, clean), 1 (not used, dirty), 2 (used, clean), 3 (used, dirty). On a fault it evicts a page from the lowest non-empty class. It needs no ordering at all and prefers pages that are old **and** cheap to evict. (From memory; it is a textbook algorithm and is not in OSTEP.)

### 4.6 LFU, MFU and MRU

**LFU** (least frequently used) evicts the page with the fewest references, **MFU** the most, on the argument that a page with a small count has just arrived and will be used. Counting references has a weakness the measurements below show: a page that was hot long ago keeps its large count and is never evicted, while the new hot pages (small counts) are evicted at once. LFU needs **ageing** (halve the counts periodically) to be usable. **MRU** (most recently used) evicts the page just touched. It sounds absurd and is correct for exactly one pattern, the loop in section 4.8.

### 4.7 Scan resistance

One more real-world problem. A sequential scan of a huge file (a backup, a `cat`, a database table scan) touches each page once and never again, but a plain LRU or Clock treats each as "recent" and evicts the process's real working set to make room for it. OSTEP mentions the fix in its discussion of scan resistance and names ARC as an example: a policy keeps track of **frequency as well as recency**, so a page seen once does not push out a page seen many times. Linux's page cache splits pages into active and inactive lists for this reason (from memory): a page enters the inactive list and is promoted to active only if it is referenced again, so a one-pass scan never reaches the active list.

### 4.8 The results

Three further measurements. First, a **phased workload**: 100,000 references over 2,000 pages, in five phases of 20,000 references, each with its own 100-page "hot set" receiving 90 percent of references (the rest are uniform over all pages), and 30 percent of references are writes. This models a program whose locality shifts. Fault rates in percent:

```text
phased workload, 100,000 references, 2,000 pages, hot set of 100 pages per phase (fault rate, percent):
                             FIFO    LRU    OPT  Clock    NRU    LFU
50 frames                   60.53  59.01  27.87  59.63  58.67  87.66
80 frames                   42.37  37.07  14.73  38.95  35.69  80.96
100 frames                  33.45  24.70   9.89  27.63  22.85  77.33
120 frames                  27.17  15.69   8.80  18.98  14.56  73.61
200 frames                  16.55   9.58   7.23   9.88   9.61  60.67
400 frames                  10.67   8.52   5.40   8.54   8.55  42.44
```

What to see:

- **Below the hot-set size nothing helps much.** With 50 frames and a 100-page hot set all but OPT fault 58 to 61 percent of the time (LFU 87.66). The memory is too small for the locality.
- **Around the hot-set size the policies separate.** At 100 frames FIFO faults 33.45 percent, Clock 27.63, LRU 24.70, NRU 22.85, and OPT 9.89. FIFO is **35 percent worse than LRU**. Clock sits between them, as an approximation of LRU should.
- **Once memory exceeds the hot set the policies converge.** At 400 frames FIFO is 10.67 and LRU, Clock and NRU are all about 8.5, and OPT is 5.40: the remaining gap to OPT is mostly what a policy that cannot see the future loses at each phase change, when the new hot set must be fetched page by page.
- **LFU is disastrous** (77.33 percent at 100 frames): the previous phase's hot pages keep large counts and stay resident, so the current phase's hot pages are evicted as fast as they arrive. This is the missing-ageing failure of section 4.6, not a bug. NRU is best of the practical policies at 80, 100 and 120 frames here; this is one workload and one seed, so read it as "NRU is competitive", not "NRU beats LRU".

Second, the loop that defeats LRU and FIFO:

```text
loop over 101 pages, 20,200 references, 100 frames (fault rate, percent):
                             FIFO    LRU    OPT  Clock    LFU    MRU
loop of 101                100.00 100.00   1.49 100.00 100.00   1.49
```

A program loops over 101 pages and has 100 frames. Each page is used once per pass, so the page that LRU or FIFO or Clock evicts is always **the one needed next**: every reference faults (100 percent) after the first pass. OPT and MRU fault only 1.49 percent (the 101 first touches, then about one fault per 100 references). OSTEP has the same example with 50 pages and a cache of 49. This is the cost of a "recent means soon" bet on cyclic access, and it is why databases that scan tables in cycles use their own buffer manager with different policies, not the OS's (from memory).

Third, read the phased table the other way, as **frames needed for a target fault rate**. To get under 10 percent, OPT needs 100 frames (9.89 percent), LRU, NRU and Clock get there by 200 frames (9.58, 9.61 and 9.88; the table has no row between 120 and 200 to say where they cross), and FIFO is still at 16.55 percent at 200 and 10.67 at 400. LRU with 120 frames (15.69 percent) does about as well as FIFO with 200 (16.55 percent). **A better policy is a way to buy memory.**

### 4.9 The real accessed bit

All of the above assumed a use bit. Linux exposes the real one. Writing `1` to `/proc/PID/clear_refs` clears the accessed bit of every page of the process (the kernel document also describes `2` for anonymous pages only, `3` for file pages only, `4` for the soft-dirty bit and `5` to reset the peak resident size), and the `Referenced:` line of `smaps` reports how much of each mapping has the bit set. The two together are a working-set meter: clear, run for an interval, read. The program touches memory after a clear and reads the counter:

```c
static void clear_refs(void)
{
    FILE *f = fopen("/proc/self/clear_refs", "w");
    fputs("1", f);                                       /* 1: clear the referenced (accessed) bit of all pages */
    fclose(f);
}

static long referenced_kb(void *base)                    /* the Referenced: figure of the mapping that starts at base */
{
    FILE *f = fopen("/proc/self/smaps", "r");
    char line[512]; unsigned long lo, hi; int in = 0; long kb = -1;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "%lx-%lx ", &lo, &hi) == 2) in = (lo == (unsigned long)base);
        else if (in && !strncmp(line, "Referenced:", 11)) { sscanf(line + 11, " %ld", &kb); break; }
    }
    fclose(f);
    return kb;
}
```

```text
allocated and touched 200 MiB; referenced right after touching: 204800 KiB
after clearing, touching   1 MiB: referenced    1024 KiB
after clearing, touching  10 MiB: referenced    9316 KiB
after clearing, touching  50 MiB: referenced   50588 KiB
after clearing, touching 100 MiB: referenced  102280 KiB
after clearing, touching 200 MiB: referenced  204692 KiB
after clearing, 200,000 random touches in the first quarter: referenced 51128 KiB (quarter = 51200 KiB)
```

The program allocated and touched 200 MiB (every page was referenced, 204,800 KiB), then for each span cleared the bits, touched a prefix of the region, and read back the counter. **Touching 1 MiB gave 1,024 KiB, 10 MiB gave 9,316 KiB, 50 MiB gave 50,588 KiB, 100 MiB 102,280 KiB and 200 MiB 204,692 KiB.** Four of the five are within 2 percent of the true figure and 10 MiB is 9 percent low (an earlier run gave 49,344 KiB for 50 MiB, so the error is a few percent in either direction; the program was run twice and only the final run is shown). The kernel can also set or clear bits itself while the program runs, and the reading reports at the granularity of what `smaps` counts, so treat the result as an estimate, not an exact count (the kernel document does not say how exact it is, and does not describe the cost of clearing, which needs a TLB flush for the process and was not measured). The last line shows the working-set idea on a skewed access: 200,000 random touches confined to the first quarter of the region marked 51,128 KiB of the 51,200 KiB quarter as referenced, and nothing outside it. This is how a page-replacement policy, or a monitoring tool, learns which pages are in use.

---

## 5. Allocating frames to processes

### 5.1 How many frames does a process get

Section 4 chose a victim among the frames of one process. With several processes the question comes first: how are the frames divided?

- **A minimum.** Each process needs enough frames to run one instruction, and the minimum is set by the architecture: an instruction that can name a memory operand through an indirect reference may touch several pages (the instruction itself, the pointer and the target, each possibly across a page boundary). Fewer frames than this and the instruction can never complete (from memory).
- **Equal allocation.** With `m` frames and `n` processes, give each `m / n`. Fair, and wasteful: a tiny process cannot use its share and a large one starves. (62 frames and two processes: 31 each.)
- **Proportional allocation.** Give process `i` of size `s_i` the share `a_i = s_i / S * m`, where `S` is the sum of sizes. With 62 frames and two processes of 10 and 127 pages, `S = 137` and the shares are `62 * 10 / 137 = 4.5` and `62 * 127 / 137 = 57.5`, so 4 and 57 (rounded down, the leftover frame to the larger one, or any rule the exam states).
- **Priority allocation.** Use the proportional rule with priorities (or priority and size) in place of size, so that a high-priority process gets more frames and may take frames from a lower-priority one when it faults.

### 5.2 Global and local replacement

When a process faults, from whose frames may the victim come?

- **Global replacement:** from any process's frames. A process with high demand can take frames from one that is idle, so total throughput is usually better. The cost: a process's fault rate depends on what **other** processes do, so the same program takes different times in different runs, and a memory-hungry process can degrade everyone.
- **Local replacement:** from the faulting process's own frames only. Each process's fault rate depends only on itself and its allocation, which is predictable and isolated, but a process whose allocation is too small thrashes while another's frames sit unused.

Linux uses a global reclaim by default and adds **cgroups** (control groups) for isolation. A memory cgroup is a **local policy with a hard limit**: processes in the group are charged for their pages and, when the group reaches its limit, the kernel reclaims from that group alone, evicting to swap if there is swap and killing a process if there is not enough to reclaim. The next section measures exactly that, and it is how containers (Docker, Kubernetes pods) get a fixed memory budget.

---

## 6. Thrashing and the working set

### 6.1 What thrashing is

A system is **thrashing** when it spends more time paging than doing useful work (OSTEP: "constantly paging"). It happens when the pages that the running processes actively use do not fit in memory, so every fault evicts a page that is needed almost at once, which then faults again. The classical story (from memory) is a feedback trap: heavy paging leaves the CPU idle waiting for I/O, the scheduler sees low CPU utilisation and **admits more processes** to raise it, the new processes take frames from the existing ones, paging increases, utilisation falls further, and the system collapses. Replacement policy cannot fix it: no policy can keep a set of pages in memory that is larger than memory. The fixes are to reduce the number of active processes or add memory. OSTEP names the OS-level answer **admission control**: do less work well rather than all of it badly. Linux's last resort is the OOM killer.

### 6.2 Locality and the working set

Programs show **locality**: over any short interval they use a small subset of their pages, and the subset changes slowly (a function and its data, then the next phase). Denning's **working-set model** (from memory) makes the idea precise: the **working set** `WS(t, delta)` of a process at time `t` is the set of distinct pages referenced in the last `delta` references (the **window**). Its size, `WSS`, is the number of frames the process needs to run without many faults. If the sum of the working-set sizes of all processes, `D = sum of WSS_i`, is larger than the number of frames `m`, the system will thrash; the **working-set policy** is to keep each process's working set resident and to **suspend** a process (swap out all its pages) whenever `D > m`, so the others keep running.

The window size is the delicate choice. Too small and the set misses pages the process still needs (the locality is not captured); too large and it spans several localities and over-estimates. The program implements the policy for one process, a page stays resident while it was referenced within the last `delta` references, and compares it with LRU given the same number of frames (the average resident size the working-set rule used):

```c
static void ws_policy(int delta, double *avg_resident, double *fault_rate)
{
    static int lastuse[MAXPAGE];
    long faults = 0; double sum = 0;
    for (int p = 0; p < MAXPAGE; p++) lastuse[p] = -(1 << 30);
    for (int i = 0; i < nref; i++) {
        int p = ref[i];
        if (i - lastuse[p] > delta) faults++;
        lastuse[p] = i;
        if (i % 50 == 0) {                               /* sample the resident set size */
            int n = 0;
            for (int q = 0; q < MAXPAGE; q++) if (i - lastuse[q] <= delta) n++;
            sum += n;
        }
    }
    *avg_resident = sum / ((nref + 49) / 50);
    *fault_rate = 100.0 * faults / nref;
}
```

```text
working-set policy on the phased workload: window delta, average resident pages, fault rate; LRU with that many frames for comparison
  delta    20: resident   19.3 pages, fault rate  84.60%   (LRU with  19 frames:  84.14%)
  delta    50: resident   41.7 pages, fault rate  66.04%   (LRU with  42 frames:  65.23%)
  delta   100: resident   68.9 pages, fault rate  45.11%   (LRU with  69 frames:  44.74%)
  delta   200: resident  101.6 pages, fault rate  24.00%   (LRU with 102 frames:  23.64%)
  delta   500: resident  145.2 pages, fault rate  10.77%   (LRU with 145 frames:  10.84%)
  delta  2000: resident  285.2 pages, fault rate   9.10%   (LRU with 285 frames:   9.13%)
```

The results are about equal: with a window of 200 references the working-set rule held an average of 101.6 pages and faulted 24.00 percent of the time, and LRU with 102 frames faulted 23.64 percent; at window 2,000, 285.2 pages and 9.10 percent against 9.13 percent. **On a single process the working-set policy is LRU with a variable memory size**, nothing better. Its value is elsewhere: it is a rule for **how many frames each process should have** and when to stop admitting processes, which a fixed-allocation LRU cannot say. The table also shows the "knee" of the curve: from 24 percent at 102 pages to 10.8 percent at 145 to 9.1 percent at 285; adding frames past the working set buys almost nothing.

### 6.3 Page-fault frequency

The working set is expensive to measure exactly (it needs a history of references). **Page-fault frequency (PFF)** controls allocation from a cheap signal, the fault rate itself (from memory). Set an upper and a lower threshold. If a process's fault rate rises above the upper threshold it needs more frames, so give it some; if it falls below the lower, it has more than it needs, so take some away. If a process needs frames and none are free, suspend a process. The rate is the working-set idea measured from its symptom.

### 6.4 A real thrashing curve

Everything above is a model. Here is the real thing, measured. The program touches pages of a region at random and reports the time per access; it runs inside a memory cgroup limited to **64 MiB**, with a 512 MiB swap file enabled. Everything that fits in 64 MiB stays resident; beyond that the kernel must evict to swap. The access loop:

```c
        for (int k = 0; k < 16; k++) {
            s = s * 6364136223846793005UL + 1442695040888963407UL;
            w[((s >> 33) % npages) * PG] += 1;
            n++;
        }
```

Each run touches the whole region once first (which may already swap), then accesses random pages for 1.5 seconds, and reports the rate and the kernel's `pswpin`, `pswpout` and the process's major-fault counter. Two runs, first and second, with the working set in MiB:

```text
working set   16 MiB:     75087198 accesses/s        13.3 ns/access  major faults       0  swap-ins       0  swap-outs       0
working set   32 MiB:     74026103 accesses/s        13.5 ns/access  major faults       0  swap-ins       0  swap-outs       0
working set   48 MiB:     64345854 accesses/s        15.5 ns/access  major faults       0  swap-ins       0  swap-outs       0
working set   56 MiB:     67684476 accesses/s        14.8 ns/access  major faults       0  swap-ins       0  swap-outs       0
working set   60 MiB:     87885131 accesses/s        11.4 ns/access  major faults       0  swap-ins       0  swap-outs       0
working set   72 MiB:       174001 accesses/s      5747.1 ns/access  major faults   30408  swap-ins   30366  swap-outs   30942
working set   80 MiB:        81668 accesses/s     12244.7 ns/access  major faults   25058  swap-ins   41410  swap-outs   36498
working set   96 MiB:        65008 accesses/s     15382.6 ns/access  major faults   32957  swap-ins   32954  swap-outs   28618
working set  128 MiB:        38383 accesses/s     26053.0 ns/access  major faults   28877  swap-ins   28902  swap-outs   28958
```

```text
working set   16 MiB:     77396292 accesses/s        12.9 ns/access  major faults       0  swap-ins       0  swap-outs       0
working set   32 MiB:     73657716 accesses/s        13.6 ns/access  major faults       0  swap-ins       0  swap-outs       0
working set   48 MiB:     73770664 accesses/s        13.6 ns/access  major faults       0  swap-ins       0  swap-outs       0
working set   56 MiB:     74987208 accesses/s        13.3 ns/access  major faults       0  swap-ins       0  swap-outs       0
working set   60 MiB:     76919004 accesses/s        13.0 ns/access  major faults       0  swap-ins       0  swap-outs       0
working set   64 MiB:      5174256 accesses/s       193.3 ns/access  major faults   30238  swap-ins   30257  swap-outs   36222
working set   72 MiB:       148792 accesses/s      6720.8 ns/access  major faults   25941  swap-ins   40032  swap-outs   36455
working set   80 MiB:       102085 accesses/s      9795.7 ns/access  major faults   31468  swap-ins   31599  swap-outs   29973
working set   96 MiB:        67578 accesses/s     14797.7 ns/access  major faults   34340  swap-ins   34342  swap-outs   32149
working set  128 MiB:        42270 accesses/s     23657.2 ns/access  major faults   31754  swap-ins   31753  swap-outs   31959
```

What the curve shows:

- **While the data fits, an access costs 11 to 16 nanoseconds and there are no faults** (16 to 60 MiB). That is memory speed (with the cache and TLB behaviour of random page accesses).
- **At the limit (64 MiB, second run) it is 193 nanoseconds** with about 30,000 swap-ins in 1.5 seconds. The process's own code and libraries also count against the cgroup, so a 64 MiB region does not quite fit in a 64 MiB limit. A first run of the same size was **killed by the kernel** (the log said "Memory cgroup out of memory: Killed process ... h3_thrash", with a resident size of 65,280 KiB): when the group cannot reclaim fast enough, the OOM handler ends a process. That is why the first table has no 64 MiB row. The thrashing region is sharp, and at its edge the outcomes are unstable.
- **Past the limit the cost rises by 400 to 2,000 times.** At 72 MiB, 5,747 and 6,721 ns per access; at 80 MiB, 12,245 and 9,796; at 96 MiB, 15,383 and 14,798; at 128 MiB, 26,053 and 23,657. The two runs differ by 4 to 25 percent at the same size (sources of the spread: how the kernel happened to pick victims and the other activity of the virtual machine), but the order of magnitude and the trend are stable.
- **The rate is about 30,000 faults per 1.5 seconds regardless of size** (25,000 to 34,000 major faults in each run). The system is **fault-bound**: the paging device sets the speed, not the program. The time per access then equals the fraction of accesses that fault, times the cost of one fault.

That last statement can be checked. If accesses are uniform over a `W` MiB region and 64 MiB can stay resident, the fraction that fault is `1 - 64 / W`: 0.111 at 72 MiB, 0.2 at 80, 0.333 at 96 and 0.5 at 128. Dividing the measured time per access by that fraction gives the cost of one fault: 52 and 60 microseconds at 72 MiB (5,747 and 6,721 over 0.111), and **49.0, 44.4 and 47.3 microseconds** at 80, 96 and 128 MiB (second run: 9,796, 14,798 and 23,657 over 0.2, 0.333 and 0.5). So one fault costs about 44 to 60 microseconds, mostly 45 to 50, and it is steady across sizes, which is what the "fault-bound" reading predicts. It is also above the 37 microsecond mean of a cold random file read in section 1.3 (28 and 46 in the two runs), and it should be: here a fault must read the needed page **and write out a dirty victim** (the swap-out counts are close to the swap-in counts in every row), which is the `F = (1 + d) * t` of section 2.2 with `d` near 1. The approximation assumes the cgroup holds exactly 64 MiB of the region; the real figure is a little lower (the binary itself counts), so the fault cost is slightly overstated.

This is what thrashing looks like from the program's side: nothing breaks, nothing is reported, the program runs 400 times slower once its data exceeds memory by 12 percent, and 2,000 times slower at twice the memory, and the only symptoms are the counters (`pswpin` and `pswpout` rising, `si` and `so` in `vmstat`, major faults in `/proc/PID/stat`). The cure is the one the model prescribes: fewer pages in use or more memory, and the limit that matters is the working set, not the virtual size.

---
## What it costs

| Mechanism | What it costs | What it buys | Measured here |
| --- | --- | --- | --- |
| Anonymous first touch (zero-fill) | one minor fault, one zeroed frame | untouched memory is free | 3.02 / 2.84 microseconds per page, against 0.03 for a second write |
| Page cache hit through a mapping | a fault that maps many pages at once | no copy between kernel and user | 0.04 to 0.05 microseconds per page, 271 faults for 65,536 pages |
| Cold file, sequential | one major fault, then read-ahead | sequential access runs at near-cache speed | 1.3 to 1.7 microseconds per page, 1 major fault |
| Cold file, random | one major fault per touch | nothing helps | 28 and 46 microseconds per page in two runs |
| Copy-on-write | one copy per page written | `fork` costs nothing for pages never written | 2.1 and 2.3 microseconds per page written |
| Swap | a write per dirty victim, a read per fault | more virtual memory than RAM | all-zero pages skipped; a swap fault about 44 to 60 microseconds |
| FIFO | queue of frames, no use information | simplest | Bélády's anomaly; 33.45 percent faults at 100 frames |
| LRU (exact) | an update on every reference | the best practical policy | 24.70 percent at 100 frames |
| Clock | the hardware use bit and a hand | LRU-like at no per-reference cost | 27.63 percent at 100 frames |
| Thrashing | every access can fault | none | 13 ns in memory, 5,700 to 26,000 ns once swapping |

## Common mistakes

1. **Treating every page fault as a disk read.** A minor fault needs no I/O (zero-fill, page cache, copy-on-write) and costs microseconds; a major fault needs a storage read; an invalid fault kills the process. Say which.
2. **Forgetting that the faulting instruction restarts.** The OS fixes the mapping and returns to the same instruction, which runs again. Hardware must make instructions restartable.
3. **Using the formula with the wrong probability.** In `EAT = (1 - p) * m + p * F`, `p` is the fault rate per memory access, not per page or per instruction, and a tiny `p` still matters because `F` is thousands of times `m`.
4. **Ignoring the dirty bit.** A dirty victim must be written before its frame is reused, so a fault costs a read plus a write, and a policy that prefers clean victims is cheaper.
5. **Saying more frames always means fewer faults.** True for LRU and OPT (stack algorithms), false for FIFO: the string `1 2 3 4 1 2 5 1 2 3 4 5` has 9 faults with 3 frames and 10 with 4.
6. **Quoting LRU as the answer to everything.** Looping over one page more than memory holds makes LRU, FIFO and Clock fault on every reference; MRU does best there. LRU also lets a one-pass scan push out a real working set (scan resistance).
7. **Proposing exact LRU for main memory.** It needs an update on every reference. Real systems approximate it with the hardware accessed bit (Clock, NRU, active and inactive lists).
8. **Using LFU without ageing.** A page that was hot long ago keeps a large count and stays; new hot pages are evicted at once (77.33 percent faults at 100 frames in the measurement).
9. **Reading a swap test on zero-filled memory.** The kernel does not write all-zero pages to swap, so a test that fills memory with zeros swaps nothing and measures nothing.
10. **Calling a swap-cache hit a swap-in.** A page written to swap but still in the swap cache comes back as a cheap minor fault (0.75 to 0.79 microseconds here, `pswpin` unchanged); only pressure that removes it makes the next touch a real swap-in.
11. **Fixing thrashing with a better replacement policy.** If the working sets exceed memory no policy helps; reduce the number of active processes or add memory. Admitting more processes when the CPU is idle waiting for paging makes it worse.
12. **Thinking a cgroup limit is a hint.** It is a hard local limit: past it the group reclaims from itself, and when it cannot, the kernel kills a process (it did, once, in the measurement).

## Questions out loud

1. *What happens on a page fault, step by step?* Trap with the faulting address; look up the address in the process's regions; if illegal, `SIGSEGV`; otherwise find a frame (evicting a victim if none is free), read the page if it is not in memory (blocking the process), update the page-table entry, restart the instruction (section 1.2).
2. *Minor against major fault?* Minor: the page is in memory or needs no I/O (zero-fill, page cache, copy-on-write); microseconds. Major: it must be read from storage; tens of microseconds on fast media, milliseconds on a rotating disk.
3. *Compute the effective access time with page faults.* `(1 - p) * m + p * F`. With `m` = 200 ns, `F` = 8 ms and `p` = 1 in 1,000, it is 8,199.8 ns, forty-one times slower than memory.
4. *Why does demand paging work at all?* Locality: a program uses a small, slowly changing part of its memory, so the fault rate stays far below the tiny value the formula demands.
5. *Name the replacement policies and one weakness of each.* FIFO ignores use and has Bélády's anomaly; OPT needs the future; LRU is too expensive to do exactly; Clock approximates LRU and degrades to FIFO when all bits are set; LFU needs ageing; MRU is right only for loops.
6. *What is Bélády's anomaly and who is immune?* More frames can cause more faults (FIFO, 9 then 10 on `1 2 3 4 1 2 5 1 2 3 4 5`). Stack algorithms (LRU, OPT) are immune because the memory at `n` frames is a subset of the memory at `n + 1`.
7. *How does the Clock algorithm work?* Frames in a circle with a hand and a use bit set by hardware; on a fault the hand clears set bits as it passes and evicts the first frame with a clear bit; the new page enters with its bit set.
8. *Why not implement exact LRU?* A timestamp or a stack update on every memory reference. No hardware does that; real kernels use the accessed bit.
9. *What is thrashing and how do you stop it?* More paging than useful work, because the active pages do not fit; suspend or do not admit processes (working set, PFF, admission control) or add memory; the OOM killer is the last resort.
10. *What is the working set?* The distinct pages referenced in the last `delta` references; its size is how many frames the process needs; if the sum over processes exceeds memory, suspend one.
11. *Global against local replacement?* Global picks victims from any process: better throughput, unpredictable per-process behaviour. Local picks from the faulting process: isolated and predictable, may waste frames. A memory cgroup is local with a hard limit.
12. *What does copy-on-write do and when does it pay?* After `fork` the child shares the parent's frames read-only; the first write to a page copies it. It pays whenever the child writes few pages (the usual `fork`-then-`exec`).

---

## Problems

**Where to solve them.** Pencil and paper for A to D. E is paper work with a small amount of arithmetic. Check A and C against the programs in the text: they build with `gcc -O2 -Wall`, and `h1_replace` takes the reference string from an array in `main`, so any string can be tried by changing it. For the real-system side, `/proc/PID/smaps` and `/proc/vmstat` are readable on any Linux machine; `clear_refs` and swap need root.

**How to approach them.** For A, draw the frames as a row of boxes and write one line per reference with the result, never a running count only. For B, write the formula first, then substitute. For C, trace both sizes in parallel and look for the reference where they diverge. For D, do the arithmetic on a share before rounding. For E, write the reference string with the window drawn under it and list the distinct pages.

### Problem A (E, entry level): count the faults

The reference string is `2 3 2 1 5 2 4 5 3 2 5 2` and there are 3 frames, all empty at the start. Count the page faults for FIFO, LRU, OPT and Clock (all use bits start at 0 and are set when a page is loaded or touched; the hand starts at frame 0).

### Problem B (E, entry level): effective access time

A memory access takes 200 ns. A page fault takes 8 ms to service. (a) What is the effective access time if 1 access in 1,000 faults? (b) What fault rate keeps the slowdown within 10 percent? (c) If 40 percent of victims are dirty and each disk transfer takes 8 ms, what is the fault service time?

### Problem C (M, medium): the anomaly

Take the string `3 2 1 0 3 2 4 3 2 1 0 4`. Count FIFO faults with 3 frames and with 4 frames, and the same for LRU. Say which policy shows an anomaly and explain in one sentence why the other cannot.

### Problem D (M, medium): frame allocation

A system has 62 free frames and two processes of 10 and 127 pages. Allocate the frames equally and proportionally. Which allocation wastes frames and by how many? What additional rule would you add for a process that is much larger than its share?

### Problem E (H, hard): the working set

The reference string of one process is `2 6 1 5 7 7 7 7 5 1 6 2 3 4 1 2 3 4 4 4 3 4 3 4 4 4` (26 references, numbered 1 to 26). (a) Give its working set at reference 10 and at reference 26 for a window `delta` of 10 and of 4. (b) Three processes have working-set sizes 5, 4 and 3 and 10 frames are available; what does the working-set policy do, and which process would you suspend? (c) What does the answer to (a) say about the best window for this process?

---

## Answers

**A.** The string needs the distinct pages 2, 3, 1, 5, 4, so five cold misses at minimum.

- **FIFO: 9 faults.** Memory (oldest first) after each fault: `2`, `2 3`, hit on 2, `2 3 1`, 5 evicts 2 giving `3 1 5`, 2 evicts 3 giving `1 5 2`, 4 evicts 1 giving `5 2 4`, hit on 5, 3 evicts 5 giving `2 4 3`, hit on 2, 5 evicts 2 giving `4 3 5`, 2 evicts 4 giving `3 5 2`. Faults at references 1, 2, 4, 5, 6, 7, 9, 11, 12: nine.
- **LRU: 7 faults.** Memory (least recently used first) after each fault: `2`, `2 3`, `3 2 1`, 5 evicts 3 giving `2 1 5`, hit on 2 (a hit moves it to the recent end), 4 evicts 1 giving `5 2 4`, hit on 5, 3 evicts 2 giving `4 5 3`, 2 evicts 4 giving `5 3 2`, then hits on 5 and 2. Faults at references 1, 2, 4, 5, 7, 9, 10: seven.
- **OPT: 6 faults** (checked by a script): the five cold misses (references 1, 2, 4, 5, 7) plus reference 10. At reference 7 OPT evicts 2, whose next use is at 10, the farthest of the three; page 2 then faults once more.
- **Clock: 8 faults.** Frames `2:1` `3:1` after two faults and a hit on 2; the fourth reference 1 fills the third frame and moves the hand to frame 0; reference 5 sweeps (clears all three bits) and evicts frame 0 (page 2); then 2 evicts frame 1 (page 3), 4 evicts frame 2 (page 1), 5 hits, 3 evicts frame 0 (page 5), 2 hits, 5 evicts frame 2 (page 4), 2 hits. Eight faults, one fewer than FIFO and one more than LRU. (A short script that mirrors the simulator gives 9, 7, 6 and 8 for the four policies.)

**B.** (a) `0.999 * 200 + 0.001 * 8,000,000 = 199.8 + 8,000 = 8,199.8` ns, about 41 times the memory time. (b) For a 10 percent slowdown the effective time must be 220 ns: `200 + p * (8,000,000 - 200) = 220`, so `p = 20 / 7,999,800 = 2.5` in a million, one fault in 400,000 accesses. (c) A clean victim needs one transfer (8 ms) and a dirty one two (16 ms): `0.6 * 8 + 0.4 * 16 = 11.2` ms.

**C.** FIFO: 9 faults with 3 frames and 10 with 4 (Bélády's anomaly; the string is the textbook one with the pages relabelled 3 2 1 0 and 4). LRU: 10 with 3 frames and 8 with 4, so it improves. LRU cannot show the anomaly because it is a stack algorithm: the pages it keeps with `n` frames are always among those it keeps with `n + 1`, so extra memory can only turn a miss into a hit.

**D.** Equal: 31 and 31. The 10-page process can use only 10, so 21 frames are idle. Proportional: `62 * 10 / 137 = 4.53` and `62 * 127 / 137 = 57.47`, so 4 and 57 with one frame left over. Equal allocation wastes 21 frames; proportional wastes at most the one left over. Rule to add: a **minimum** number of frames per process (enough to run an instruction) and, for a process far below its working-set size, a priority or fault-rate rule (PFF) that gives more frames from a process with spare ones.

**E.** (a) Reference 10: with `delta` = 10 the last 10 references (positions 1 to 10: `2 6 1 5 7 7 7 7 5 1`) give `{1, 2, 5, 6, 7}`; with `delta` = 4 positions 7 to 10 (`7 7 5 1`) give `{1, 5, 7}`. Reference 26: positions 17 to 26 (`3 4 4 4 3 4 3 4 4 4`) give `{3, 4}` for `delta` 10, and positions 23 to 26 (`3 4 4 4`) give `{3, 4}` for `delta` 4. (b) The demand is `5 + 4 + 3 = 12`, more than the 10 frames, so the system would thrash. The working-set policy suspends a process until `D <= m`. Suspending the process with the smallest working set that restores the balance (the 3, leaving `D = 9`) loses the least work, but the 4 (leaving 8) or the lowest-priority process is also defensible; the rule is "suspend until demand fits", and which one is a policy choice (the one with the lowest priority, or the most recently started, or the one with the least work lost). (c) The window changes the answer in the first locality: `delta` 4 sees `{1, 5, 7}` while `delta` 10 sees `{1, 2, 5, 6, 7}`, two pages from the previous locality. In the second locality the two windows agree. A window should be long enough to cover a locality but shorter than a phase; too long, and the set carries pages from the previous one and over-estimates the frames needed.

---

## Cheat sheet

- Page fault: trap, check region, find frame (evict if full), read if needed (block), fix the entry, restart the instruction. Minor: no I/O. Major: I/O. Invalid: `SIGSEGV`.
- `EAT = (1 - p) * m + p * F`. A 10 percent slowdown allows `p = 0.1 * m / (F - m)`. Measured here: minor 2.9 microseconds (one fault in 280 accesses), major 37 microseconds (one in 3,690), 8 ms disk (one in 799,990).
- Dirty victim: fault cost is a read plus a write, `F = (1 + d) * t`.
- OPT: farthest next use; the bound, not implementable. FIFO: oldest; Bélády's anomaly. LRU: longest unused; stack algorithm; needs per-reference updates. Clock: hand and use bit; clears bits as it sweeps; evicts the first clear. NRU: classes by use and modify bits. LFU: needs ageing. MRU: right for loops only.
- Stack property: pages at `n` frames are a subset of the pages at `n + 1`; LRU and OPT have it, FIFO does not.
- Textbook string, 3 frames: OPT 9, LRU 12, LFU 13, Clock 14, FIFO 15, MRU 16 faults. Belady string: FIFO 9 then 10.
- Loop over `n + 1` pages with `n` frames: LRU, FIFO, Clock fault every reference; MRU and OPT do not. No locality: all policies equal.
- Allocation: equal `m / n`; proportional `s_i / S * m`; priority. Global against local replacement. A memory cgroup is a local policy with a hard limit.
- Working set `WS(t, delta)`: distinct pages in the last `delta` references. `D = sum of WSS > m` means thrashing: suspend. PFF: fault rate above the upper threshold gives frames, below the lower takes them.
- Thrashing is a cliff: 13 ns per access in memory, about 6,000 to 26,000 ns once the data exceeds memory by 12 to 100 percent (measured, fault-bound at about 45 to 50 microseconds per fault).
- Zero-fill on demand; copy-on-write on `fork`; mapped files fault from the page cache with read-ahead; swap holds anonymous pages, skips all-zero pages and keeps recently written pages in the swap cache.
- Counters: `getrusage` minor and major faults, `/proc/vmstat` `pswpin` and `pswpout`, `smaps` `Referenced` and `Swap`, `clear_refs`.

## Sources

Measured on the VM described above. Fetched and used: the two OSTEP chapters and the kernel `proc` document (first part only: `clear_refs` and `smaps`; its remaining sections and the cost of `clear_refs` were not read). Not fetched, and stated from memory in the text: the working-set model and its window, page-fault frequency, the allocation schemes, NRU, LFU and MFU, the minimum-frames argument, the classical thrashing feedback loop, the stack property as a stated fact (the measured tables agree with it), fault-around and large folios, the Linux active and inactive lists and `kswapd`, and database buffer managers. OSTEP's chapters on address translation and page tables were not fetched for this tutorial (OS 10 has them).

- [OSTEP: Beyond physical memory: policies (PDF)](https://pages.cs.wisc.edu/~remzi/OSTEP/vm-beyondphys-policy.pdf), fetched: average memory access time, OPT, FIFO, Random, LRU, the 80-20 workload, the loop workload, Clock and the use bit, dirty pages, thrashing, admission control, scan resistance
- [OSTEP: Beyond physical memory: mechanisms (PDF)](https://pages.cs.wisc.edu/~remzi/OSTEP/vm-beyondphys.pdf), fetched: swap space, the present bit, the page-fault control flow, the swap daemon and its watermarks
- [Linux kernel: The /proc filesystem](https://docs.kernel.org/filesystems/proc.html), fetched in part: `clear_refs`, `smaps` fields
- [OS 10: Memory management](10_memory_management.md), [CO 15: Virtual memory and the TLB](../04_computer_organization/15_virtual_memory_and_the_tlb.md), [OS 12: Virtual memory and the TLB in practice](12_virtual_memory_and_tlb.md) and [OS 02: Processes](02_processes.md), the neighbouring material
